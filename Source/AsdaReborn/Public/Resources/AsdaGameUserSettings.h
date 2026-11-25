// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaGameUserSettings — ПРОФИЛЬ ИГРОКА ПОРТА: штатный объект пользовательских настроек UE
//   (подкласс `UGameUserSettings`), в котором живут ВСЕ настройки окна `SYSTEM_Option_NEW` (69500).
//
// ★★★ ЗАЧЕМ ЭТОТ КЛАСС (требование владельца 19.09.2026: «все настройки сохраняются в конфигах игры —
//   НЕ как в оригинале, а как ПРАВИЛЬНО в UE»).
//   До 19.09 окно писало свои значения строками прямо в `GConfig` — секция `[AsdaOptions]` файла
//   `GameUserSettings.ini`, мимо объекта настроек движка. Так в UE не делают: у движка есть единый
//   объект профиля игрока (`GEngine->GetGameUserSettings()`), у него штатный жизненный цикл
//   `LoadSettings` → `ApplySettings` / `ApplyNonResolutionSettings` → `SaveSettings`, и проект
//   расширяет его ПОДКЛАССОМ с полями `UPROPERTY(Config)`. Класс регистрируется в
//   `Config/DefaultEngine.ini`:
//       [/Script/Engine.Engine]
//       GameUserSettingsClassName=/Script/AsdaReborn.AsdaGameUserSettings
//   после чего движок сам создаёт ИМЕННО этот объект, сам зовёт `LoadSettings` при старте и
//   `ApplyNonResolutionSettings` при инициализации игры.
//
// ЧТО ГДЕ ХРАНИТСЯ (секция `[/Script/AsdaReborn.AsdaGameUserSettings]` файла
//   `Saved/Config/<Платформа>/GameUserSettings.ini`):
//   • оконный режим и разрешение — ШТАТНЫЕ поля родителя (`FullscreenMode`, `ResolutionSizeX/Y`,
//     …), меняются ТОЛЬКО штатными методами (`SetFullscreenMode`, `SetScreenResolution`,
//     `ApplyResolutionSettings`) — через `UAsdaVideoSettingsSubsystem::ApplyResolution`;
//   • качество 3D — ШТАТНЫЕ группы scalability (`[ScalabilityGroups]`, `SetShadowQuality` и т.д.),
//     через `UAsdaVideoSettingsSubsystem`;
//   • громкость музыки/эффектов, «заглушить» музыку/эффекты, яркость, флаги вкладок «Интерфейс»
//     и «3D», уровень «Свечения» — ПОЛЯ ЭТОГО КЛАССА ниже;
//   • ★ (19.09.2026, фаза 2) ручки рендера, вынесенные из dev-окна Ctrl+N в окно настроек: метод
//     сглаживания, резкость, анизотропия, затенение окружения, размытие в движении, блики объектива,
//     объёмные облака, дальность прорисовки — ТОЖЕ поля этого класса (правило «отрицательное = не менялось»);
//     масштаб рендера, лимит кадров, вертикальная синхронизация, группы качества и оконный режим —
//     ШТАТНЫЕ поля родителя; графический API — штатная секция движка `[D3DRHIPreference]`;
//   • привязки клавиш — НЕ в этом классе. ★ Поправлено ревизией 19.09.2026: прежняя редакция здесь
//     утверждала, что раскладка сохраняется в `Input.ini` через `UInputSettings::ActionMappings`. Это
//     НЕВЕРНО: каждый старт, включая запуски владельца, пишет в лог «применено действий — 0» —
//     ActionMappings в `Input.ini` на деле не сохраняются. ЕДИНСТВЕННОЕ хранилище раскладки —
//     `<ProjectDir>/system/Shortcutkey.ini` (`UAsdaKeybindConfig`). ВТОРОЙ КОПИИ НЕТ, поэтому этот файл
//     надо беречь как пользовательские данные: тесты уводят его в песочницу параметром
//     `-AsdaShortcutkeyIni=<путь>`. Дублировать раскладку сюда не стали — это был бы второй источник
//     правды для одних и тех же клавиш.
//
// ★ ОДНОРАЗОВАЯ МИГРАЦИЯ (`MigrateFromLegacyConfig`). Смена класса меняет имя секции ini: прежние
//   значения родителя лежат в `[/Script/Engine.GameUserSettings]`, а объект подкласса читает свою
//   секцию. Без переноса у игрока при первом запуске сбросились бы разрешение и оконный режим, а
//   значения окна настроек из `[AsdaOptions]` пропали бы. Поэтому при ПЕРВОМ чтении (в секции
//   подкласса нет метки `AsdaSettingsVersion`) все ключи старой секции родителя копируются в новую,
//   а `[AsdaOptions]` раскладывается по полям. Старые секции НЕ удаляются (данные игрока не стираем),
//   просто больше не читаются. Метка версии ставится сразу, повтора миграции не бывает.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "AsdaGameUserSettings.generated.h"

UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class ASDAREBORN_API UAsdaGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UAsdaGameUserSettings(const FObjectInitializer& ObjectInitializer);

	// Текущий профиль игрока (объект, созданный движком по `GameUserSettingsClassName`).
	//   nullptr — если в конфиге не зарегистрирован класс (тогда движок создал бы голый родитель).
	static UAsdaGameUserSettings* Get();

	// ── Штатный жизненный цикл UGameUserSettings ──────────────────────────────────────────────
	// Сброс к умолчаниям: родитель сбрасывает свои поля, мы — свои (умолчания оригинала, см. ниже).
	virtual void SetToDefaults() override;
	// Чтение профиля: перед штатным чтением — одноразовая миграция старых секций; после — поле `Language` из ini.
	virtual void LoadSettings(bool bForceReload = false) override;
	// ★ 09.10.2026 Запись профиля: штатная `SaveConfig` + поле `Language` ВРУЧНУЮ и только непустым (см. поле ниже).
	virtual void SaveSettings() override;
	// Применение «не-разрешенческих» настроек: родитель — scalability/vsync/…; мы — звук и яркость.
	//   Движок зовёт это сам при старте игры, поэтому сохранённые громкость и гамма действуют с первого
	//   кадра, без открытия окна настроек.
	virtual void ApplyNonResolutionSettings() override;

	// Раздать звук и яркость потребителям (отдельно от родителя — окно зовёт это на лету).
	void ApplyAudioSettings() const;
	void ApplyGammaSetting() const;

	// Диагностика: строка со всеми полями профиля.
	FString Describe() const;

	// ── ПОЛЯ ПРОФИЛЯ (каждое — `UPROPERTY(Config)`, пишется штатным `SaveSettings`; кроме `Language` — см. у поля) ───────────────

	// Метка миграции/формата полей. 0 = секция подкласса ещё ни разу не сохранялась.
	UPROPERTY(Config)
	int32 AsdaSettingsVersion = 0;

	// ★★★ [ПОРТ] 09.10.2026 ЯЗЫК ИГРЫ (мультиязычность, `Public/Text/AsdaLang.h`): "ru" | "en" | "ar"; пусто — игрок ещё
	//   не выбирал (на экране логина показывается окно первого выбора). Читает его `FAsdaLang::InitAtStartup` ДО
	//   создания этого объекта (через GConfig) — поэтому язык действует со СЛЕДУЮЩЕГО запуска. Пишут окно первого
	//   выбора, строка «Язык» окна настроек и `asda.Lang`. «По умолчанию» окна настроек его НЕ сбрасывает
	//   (`ResetAsdaFieldsToDefaults` поле не трогает): сброс графики не должен заново спрашивать язык.
	//   ★ 09.10.2026 (решение владельца, партия multilang_owner_batch): поле НЕ `Config`. Класс помечен
	//   `configdonotcheckdefaults`, поэтому штатная `SaveConfig` писала КАЖДОЕ поле, в том числе пустое, — и любой запуск
	//   (даже headless-редактор) дописывал в GameUserSettings.ini строку `Language=`, пока игрок язык не выбрал.
	//   Теперь ключ читается в `LoadSettings` и пишется в `SaveSettings` вручную (тот же раздел и ключ, что у прежнего
	//   Config-поля, — формат файла прежний), причём ТОЛЬКО непустым; пустая строка, оставленная прежними сборками, при
	//   сохранении удаляется. `FAsdaLang` по-прежнему читает ключ через GConfig до создания профиля.
	UPROPERTY(Transient)
	FString Language;

	// Громкость фоновой музыки, 0…1. В окне — полоса 70013 с диапазоном эталона 0…256
	//   (`uiScrollbarSetRange(…, 0, 256)`); умолчание эталона `configInitDefaults` = 128 → 0.5.
	UPROPERTY(Config)
	float MusicVolume = 0.5f;

	// Громкость звуковых эффектов, 0…1 (полоса 70016, умолчание эталона 128 → 0.5).
	UPROPERTY(Config)
	float SfxVolume = 0.5f;

	// Музыка заглушена (чекбокс-динамик 70012: отмечен = перечёркнутый динамик = тишина).
	UPROPERTY(Config)
	bool bMusicMuted = false;

	// Эффекты заглушены (чекбокс-динамик 70015).
	UPROPERTY(Config)
	bool bSfxMuted = false;

	// Яркость (полоса 70058, диапазон эталона 1…100; 50 = нейтраль, `GEngine->DisplayGamma` 2.2).
	UPROPERTY(Config)
	int32 Gamma = 50;

	// Вкладка «Интерфейс», панель 1 (70170, приём заявок; эталонные байты +1105…+1111), 7 флагов.
	UPROPERTY(Config)
	TArray<uint8> InterfaceCommunity;

	// Панель 2 (70250, показ имён/ОЗ; +1112…+1114, +1121…+1123), 6 флагов.
	UPROPERTY(Config)
	TArray<uint8> InterfaceNames;

	// Панель 3 (70350, прочее; +1115…+1120, +1145…+1148), 10 флагов.
	UPROPERTY(Config)
	TArray<uint8> InterfaceOther;

	// Вкладка «Система», панель 3D (70100; +1136…+1144), 9 флагов.
	UPROPERTY(Config)
	TArray<uint8> Render3D;

	// ★★★ «СВЕЧЕНИЕ» (LEVEL OF GLOW EFFECT оригинала, `config.txt` поле 32), пункт 0…4 — ЕДИНСТВЕННЫЙ
	//   источник для обоих окон: комбо 70121 окна настроек и строки «Уровень свечения» dev-окна Ctrl+N.
	//   До 19.09.2026 (фаза 2) источников было ТРИ, и два из них не сходились никогда: окно писало поле
	//   `FlareMode` этого профиля, которое НИКТО не применял, а dev-окно — строку `[AsdaVideo] GlowLevel`,
	//   которую и читала видео-подсистема. Поле `FlareMode` снято, значение переносит миграция v2.
	//   Смысл пунктов — ПО ЭТАЛОНУ (разбор в `UAsdaVideoSettingsSubsystem::SetGlowLevel`): на ЧЁМ светится
	//   ореол — 0 «Нет», 1 «Персонаж» (всё, кроме монстров), 2 «Монстр», 3 «Персонаж+монстр», 4 «Все» (кадр целиком).
	UPROPERTY(Config)
	int32 GlowLevel = 4;

	// ★★★ 17.10.2026 (синхронизация боя, Ф5) РЕЖИМ ПОКАЗА ВСПЛЫВАЮЩИХ ЦИФР УРОНА — порт поля `Field10112` оригинала
	//   (u16 `g_InventoryBag + 0x4F00`, геттер `CClass_GetField10112@0x9A21D0`, entity_char.c:1767):
	//     2 — всплывающих надписей нет вообще (`FloatingText_Spawn@0x9A8CF0` при 2 не рисует ничего, entity_char.c:3214;
	//         строки в чат, полоса HP, звуки и эффекты — остаются);
	//     1 — все: цифры соседей по мобам (entity_char.c:848+), PvP-наблюдателю (:2097+), ударам мобов по чужим (combat_pvp.c:7998);
	//     0 — только касающиеся меня: я бью / бьют меня / (у проков) бьёт моя выбранная цель (combat_pvp.c:1595–1630).
	//   [ОРИГИНАЛ] В этой сборке поле пишет только инициализация (0) — пункта настройки, ini, реестра и пакета нет.
	//   [ПОРТ] Умолчание 1 — прямой запрос владельца 17.10.2026 («у всех одинаково… сообщения урона»).
	//   ★ 18.10.2026 (явное разрешение владельца): пункт «Цифры урона» в окне настроек 69500 — вкладка «Настройки игры», страница
	//   «Отображение имён» (`UAsdaSystemOptionSubsystem`, строка I1; узлы — шаг ассета `Tools/asda_ui_option_dmgtext_row.py`);
	//   поле пишется по «ОК/Применить» и действует сразу. Ещё — `asda.Combat.DamageTextMode` (вне Shipping) или ini.
	//   Читать — `UAsdaWorldSubsystem::GetDamageTextMode`/`ShouldShowDamageText` (одна точка).
	UPROPERTY(Config)
	int32 DamageTextMode = 1;

	// ★★★ 19.10.2026 [ПОРТ] (запрос владельца) ЧУВСТВИТЕЛЬНОСТЬ МЫШИ И ПОВОРОТА КАМЕРЫ — строки I2/I3 окна настроек 69500
	//   (вкладка «Настройки игры», страница «Разное»; узлы — шаг ассета `Tools/asda_ui_option_mouse_rows.py`). В оригинале
	//   пунктов нет. Оба поля — проценты 10…360 (★ v2, было 20…180), 100 = как без настройки; пишутся по «ОК/Применить» и действуют сразу.
	// Скорость КУРСОРА, % от скорости указателя Windows игрока. Курсор в игре системный (`SetHardwareCursor`), поэтому
	//   скорость ставится системным вызовом только пока окно игры активно и возвращается при сворачивании/выходе/падении
	//   (`FAsdaCursorSpeed`, разбор там). 100 — системную скорость не трогаем вовсе.
	UPROPERTY(Config)
	int32 CursorSpeedPercent = 100;
	// Скорость поворота камеры мышью при зажатой ПКМ, % (множитель к 0.01 рад/px орбиты; читать — `GetCameraTurnScale`).
	UPROPERTY(Config)
	int32 CameraTurnPercent = 100;
	// Служебное: скорость указателя Windows (1…20), которую игра сменила и ещё не вернула; 0 — возвращать нечего. Пишется
	//   при смене, стирается при возврате — если процесс упал, следующий запуск вернёт скорость по этой метке.
	UPROPERTY(Config)
	int32 CursorSpeedRestoreLevel = 0;
	// Границы и умолчание обоих процентов.
	static constexpr int32 kMouseSensMin = 10;    // ★ 19.10.2026 v2 (владелец «в два раза»): было 20…180
	static constexpr int32 kMouseSensMax = 360;
	static constexpr int32 kMouseSensDefault = 100;
	// Множитель поворота камеры мышью (1 = как без настройки). Без профиля — 1.
	static float GetCameraTurnScale();

	// ★★★ 25.10.2026 [ПОРТ] (запрос владельца) РЕЖИМ CTRL ХОДЬБЫ ВПРИСЯДКУ — строка I4 «Присед» окна настроек 69500 (вкладка
	//   «Настройки игры», страница «Разное»; узлы — шаг ассета `Tools/asda_ui_option_crouch_row.py`). У оригинала и у донора
	//   AsdaFableClassic клавиши приседа нет — пункт новый. 0 «Нажатие» — одиночное нажатие Ctrl включает/выключает присед
	//   (как с 24.10.2026, умолчание); 1 «Зажатие» — присед, пока Ctrl удерживается. Пишется по «ОК/Применить», действует сразу.
	//   Читать — `GetCrouchMode` (одна точка; разбор режима — `AAsdaPlayerCharacter::TickCrouchInput`).
	UPROPERTY(Config)
	int32 CrouchMode = 0;
	static constexpr int32 kCrouchToggle = 0;   // «Нажатие» — Ctrl переключает
	static constexpr int32 kCrouchHold = 1;     // «Зажатие» — присед, пока Ctrl зажат
	// Режим Ctrl приседа (`kCrouchToggle`/`kCrouchHold`); без профиля — «Нажатие».
	static int32 GetCrouchMode();

	// ══════════ ★ 19.09.2026 (фаза 2): РУЧКИ РЕНДЕРА, ВЫНЕСЕННЫЕ ИЗ Ctrl+N В ОКНО НАСТРОЕК ══════════
	//   ПРАВИЛО ВСЕХ ПОЛЕЙ НИЖЕ: отрицательное значение = «игрок не менял» ⇒ профиль НИЧЕГО не
	//   переопределяет, и действует значение проекта/уровня качества (для владельца это его калибровка:
	//   FXAA, резкость 0.6 и т.д., перенесённые миграцией). Поле становится ≥ 0 ТОЛЬКО когда игрок сам
	//   выбрал значение в окне настроек или в Ctrl+N — поэтому «ОК» с нетронутыми строками калибровку не
	//   сбивает. Применяет их `ApplyRenderOverrides` (из `ApplyNonResolutionSettings`, то есть при старте
	//   игры и при каждом штатном применении настроек) с приоритетом консоли — тем же, каким их ставило
	//   dev-окно, иначе пины проекта (`[SystemSettings]`, `RendererSettings`) их бы не пустили.
	//   Прежнее хранилище — строки `[AsdaVideo]`/`[AsdaRender]` dev-окна; их ОДИН раз переносит миграция v2.

	// Метод сглаживания `r.AntiAliasingMethod`: 0 выкл / 1 FXAA / 2 TAA / 4 TSR; −1 — не менялся.
	UPROPERTY(Config)
	int32 AntiAliasingMethod = -1;

	// Резкость тонмаппера `r.Tonemapper.Sharpen` (0.6 / 0.9 / 1.2); < 0 — не менялась.
	UPROPERTY(Config)
	float TonemapperSharpen = -1.f;

	// Анизотропная фильтрация `r.MaxAnisotropy` (4 / 8 / 16); −1 — не менялась (значение даёт уровень текстур).
	UPROPERTY(Config)
	int32 MaxAnisotropy = -1;

	// Затенение окружения `r.AmbientOcclusionLevels`: 1 = вкл (−1, «по уровню»), 0 = выкл (0); −1 — не менялось.
	UPROPERTY(Config)
	int32 AmbientOcclusion = -1;

	// Размытие в движении `r.MotionBlur.Amount`: 1 = вкл (0.5), 0 = выкл (0); −1 — не менялось.
	UPROPERTY(Config)
	int32 MotionBlur = -1;

	// Блики объектива: 1 = вкл (`r.LensFlareQuality` 2 + сила бликов пост-обработки 1.0), 0 = выкл; −1 — не менялись
	//   (действует проектное `r.DefaultFeature.LensFlare=False` — бликов нет).
	UPROPERTY(Config)
	int32 LensFlare = -1;

	// Объёмные облака `asda.Clouds.Field`: 1 / 0; −1 — не менялись.
	UPROPERTY(Config)
	int32 VolumetricClouds = -1;

	// Дальность прорисовки `asda.Cull.Scale`, множитель 0.5…2.0; < 0 — не менялась (действует пин проекта).
	UPROPERTY(Config)
	float DrawDistanceScale = -1.f;

	// ★★★ 19.09.2026: МАСШТАБ РЕНДЕРА ВЫШЕ 100 % (суперсэмплинг), 101…200; −1 — не используется.
	//   Штатное поле масштаба `UGameUserSettings` (ResolutionQuality) упирается в потолок движка 100 %
	//   (`Scalability::MaxResolutionScale`), а в UE5 `r.ScreenPercentage` допускает и больше. Фаза 2 окна
	//   настроек перевела масштаб ТОЛЬКО на штатное поле и тем срезала 125/150 %, которые были в Ctrl+N, —
	//   владелец играл на 125 %. Значение > 100 хранится ЗДЕСЬ (штатное поле при этом = 100) и ставится в
	//   `r.ScreenPercentage` из `ApplyRenderOverrides` поверх уровня качества. Читать/писать масштаб —
	//   ТОЛЬКО через `GetRenderScalePercent`/`SetRenderScalePercent` (их зовут оба окна).
	UPROPERTY(Config)
	int32 RenderScaleAbove100 = -1;

	// Границы масштаба рендера для обоих окон, %.
	static constexpr int32 kRenderScaleMin = 10;
	static constexpr int32 kRenderScaleMax = 200;
	// Действующий масштаб рендера, %: поле суперсэмплинга, иначе штатное поле, иначе (штатное 0 = «как
	//   в проекте») — текущий `r.ScreenPercentage`.
	int32 GetRenderScalePercent() const;
	// Записать масштаб (10…200): ≤ 100 — в штатное поле, > 100 — в `RenderScaleAbove100`. Применение —
	//   штатным `ApplyNonResolutionSettings`, сохранение — `SaveSettings` (решает вызывающий).
	void SetRenderScalePercent(int32 Percent);

	// ══════════ ОБЩИЙ API ПЕРЕОПРЕДЕЛЕНИЙ (им пользуются ОБА окна) ══════════
	// Применить все заданные поля к ключам консоли (незаданные не трогаются).
	void ApplyRenderOverrides() const;

	// ★ Ключи консоли, которые принадлежат профилю. dev-окно Ctrl+N направляет СВОИ записи этих ключей
	//   сюда (`SetCVarOverride`/`ClearCVarOverride`), а не в `[AsdaVideo]`/`[AsdaRender]` — один источник.
	static bool IsProfileOwnedCVar(const FString& Name);
	// Записать значение ключа в соответствующее поле профиля и применить. false — ключ не профильный.
	//   bSave — сразу сохранить профиль на диск (dev-окно пишет сразу, окно настроек — по «ОК»).
	bool SetCVarOverride(const FString& Name, const FString& Value, bool bSave);
	// Сбросить поле к «не менялось» и снять приоритет консоли с ключа (вернётся значение проекта/уровня).
	bool ClearCVarOverride(const FString& Name, bool bSave);

	// ★ Графический API (DX12/DX11). Хранилище — ШТАТНАЯ секция движка `[D3DRHIPreference] PreferredRHI`
	//   этого же файла: её читает `ChoosePreferredRHI` ДО создания объекта профиля (WindowsDynamicRHI.cpp),
	//   поэтому полем `UPROPERTY` она быть не может. Доступ к ней — только через эти две функции (их же
	//   зовёт dev-окно), чтобы место хранения было одно. Нет ключа = проектный DX12.
	static bool GetPendingRHIIsDx12();
	static void SetPendingRHI(bool bDx12);

	// Число флагов в каждом наборе (длина фиксирована эталоном; при чтении длина выравнивается).
	static constexpr int32 kNumCommunity = 7;
	static constexpr int32 kNumNames     = 6;
	static constexpr int32 kNumOther     = 10;
	static constexpr int32 kNum3D        = 9;

private:
	// Умолчания собственных полей (эталон `configInitDefaults@0x9509E0` и начальные значения буфера
	//   окна `FAsdaOptState`).
	void ResetAsdaFieldsToDefaults();
	// Привести длины наборов флагов к эталонным (старый/битый ini не должен ронять индексацию).
	void NormalizeFlagArrays();
	// Одноразовая миграция: вернуть true, если перенос выполнен в этом вызове.
	bool MigrateFromLegacyConfig();
	// ★ Миграция v2 (19.09.2026, фаза 2): перенос строк dev-окна `[AsdaVideo]`/`[AsdaRender]` (ручки,
	//   ставшие полями профиля), `[AsdaVideo] GlowLevel` и `[AsdaVideo] DisplayGamma`. true — перенос был.
	bool MigrateDevOverridesV2();
	// ★ Миграция v3 (19.09.2026): вернуть масштаб рендера > 100 %, который v2 урезала до штатного потолка.
	void MigrateRenderScaleV3();
	// ★ Миграция v4 (12.10.2026, пересмотр пресетов): снять переопределения анизотропии/AO/бликов (строки убраны из окон).
	void MigrateRetiredOverridesV4();
	// Разложить старую секцию `[AsdaOptions]` по полям (формат строк прежнего окна).
	bool ImportLegacyAsdaOptions();
};
