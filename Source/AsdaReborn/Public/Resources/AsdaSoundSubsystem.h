// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSoundSubsystem — единый менеджер ЗВУКА (по образу оригинала CSoundMgr/CSoundFileTable, audio.c).
//   Оригинал (ETALON_RESOURCE_MANAGERS.md §2, SOUND_TABLE_RE.md): CSound_PlayName2D@0xFA26E0 / PlayName3D@0xFA2750
//   берут ЧИСЛОВОЙ soundId → CSoundFileTable_GetNameById@0xFA6390 → имя wav (таблица SFT.BIN, 216 записей,
//   id==индекс). CSoundStream_PlayMusic@0xFA0C10 (единичный трек + fade). Record-звуки (NPC/креатуры) —
//   отдельная подсистема: Sound_BuildEntryWavName@0xF71450 = "<base>_%d.wav" (спец a3==11 → "_99.wav").
//   Порт реализует ОБА пути: по имени (FName) И по числовому id (через импортированную таблицу DT_SoundTable).
//   GameInstanceSubsystem — звук нужен только в рантайме. Правило never-invent: имена — из SFT.BIN, не выдумываем.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/EngineTypes.h"   // FTimerHandle (очередь смены музыки)
#include "AsdaSoundSubsystem.generated.h"

class USoundBase;
class UAudioComponent;
class USoundConcurrency;
class USoundAttenuation;
class USoundWave;   // ★ 08.11.2026: петля вызова — обёртка волны (LoopVariant)
class USoundCue;    // ★ 08.11.2026: петля вызова — рантайм-обёртка с бесшовной петлёй
class UWorld;
class AActor;

// ★★★ КЛАСС ЗВУКА ДЛЯ 3D-ЗАТУХАНИЯ (30.10.2026, баг игроков «звуки боя без затухания по расстоянию»).
//   Оригинал радиусы по классам не делит: у FMOD3 у всех 3D-звуков min 100 / max 10000 игровых единиц, rolloff 0.7
//   (`CSoundEx_ctor@0xFA23B0`, `Sound3DSettings_ctor@0xFA2A90`; SPEC_FMOD_ATTEN.md). Владелец 30.10: радиусы — по слуху
//   человека, рост персонажа = рост человека. Классы и их радиусы — `UAsdaSoundSubsystem::GetClassSpec` (таблица там же).
//   [ПОРТ] Кривая внутри радиуса остаётся эталонной (inverse, rolloff 0.7 от внутреннего радиуса 1 м = 100 ед. оригинала).
UENUM(BlueprintType)
enum class EAsdaSoundClass : uint8
{
	Auto     UMETA(DisplayName="По имени"),        // класс выводится из имени ассета (`ClassifyByName`)
	Step     UMETA(DisplayName="Шаги"),            // шаги, движение персонажей и мобов
	Item     UMETA(DisplayName="Вещи"),            // падение и подбор вещи, звон монет
	Action   UMETA(DisplayName="Действие"),        // взмах оружия, тетива, бросок, всплеск, копание, механизмы
	Hit      UMETA(DisplayName="Удар"),            // удар по телу/броне/щиту, крит, попадание снаряда, барьер
	Voice    UMETA(DisplayName="Голос"),           // голос персонажа: вскрик, смерть, клич
	Creature UMETA(DisplayName="Существо"),        // голос моба: рык агра, крик, смерть
	Magic    UMETA(DisplayName="Магия"),           // навыки, взрывы, эффекты, столб входа, церемонии
	Vehicle  UMETA(DisplayName="Транспорт"),       // транспорт
	Ambient  UMETA(DisplayName="Окружение"),       // окружение карты (радиусы — из данных карты, Pos.POS)
	Ui       UMETA(DisplayName="Интерфейс"),       // интерфейс — только 2D, в 3D-путь не попадает
	Count    UMETA(Hidden)                         // число классов (служебное)
};

// Параметры класса 3D-звука (строка таблицы `UAsdaSoundSubsystem::GetClassSpec`). Радиусы — в МЕТРАХ.
struct FAsdaSoundClassSpec
{
	const TCHAR* Name = TEXT("");    // имя класса для журналов и самотеста
	float SourceDb = 0.f;            // громкость источника на 1 м, дБ(А) — основание расчёта радиуса
	float InnerM = 1.f;              // радиус полной громкости, м (эталон: min 100 ед. = 1 м)
	float OuterM = 35.f;             // радиус затухания до нуля, м (до купола `asda.Sound.HearingRadiusM`)
	float Priority = 1.f;            // приоритет голоса UE (кого глушить при нехватке голосов)
	int32 MaxPerClass = 0;           // голосов класса одновременно на всю сцену (0 — без лимита)
	int32 MaxPerSource = 0;          // голосов класса на один источник (0 — без лимита)
	float ConcVolumeScale = 1.f;     // «вес концентрации»: множитель громкости прежних голосов класса при каждом новом
};

// План запуска 3D-звука — ровно то, что получит движок в момент старта (его же строит игровой путь, его же читает самотест).
struct FAsdaSound3DPlan
{
	EAsdaSoundClass Class = EAsdaSoundClass::Auto;   // итоговый класс (после вывода по имени)
	float Mn = 0.f;                                  // внутренний радиус, ед. UE
	float Mx = 0.f;                                  // радиус затухания до нуля, ед. UE (после купола и множителя)
	USoundAttenuation* Att = nullptr;                // затухание, которое уйдёт в движок ДО старта
	USoundConcurrency* ClassConc = nullptr;          // группа класса (nullptr — лимит класса выключен)
	float Priority = 1.f;                            // приоритет голоса
	int32 MaxPerSource = 0;                          // лимит голосов класса на источник
	bool bCulled = false;                            // источник за радиусом — звук не заводится
};

// Строка таблицы звуков (SFT.BIN → CSV → DT_SoundTable). RowName=id, поля id + wav (с расширением .wav).
USTRUCT(BlueprintType)
struct FAsdaSoundTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaSound") int32   id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaSound") FString wav;
};

UCLASS()
class ASDAREBORN_API UAsdaSoundSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── SFX 2D по ИМЕНИ ассета из /Game/AsdaAssets/Sound/<Name>.<Name> (аналог CSound_PlayName2D по имени) ──
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void PlaySound2D(FName Name, bool bLoop = false);

	// ★ 22.10.2026: ГЛУШЕНИЕ коротких 2D-звуков интерфейса на время СЛУЖЕБНОГО переоткрытия окон после травела
	//   (`UAsdaWindowSubsystem::ReopenTravelKeptWindows`). У оригинала окна при смене карты не закрываются вовсе —
	//   звуков открытия/закрытия там нет; порт же пересобирает окна, снесённые движком, игровыми функциями владельцев
	//   (у них свои звуки 5/6). Петли (`bLoop`) и 3D не глушатся. Глубина — счётчик вложенности.
	struct FUiSoundMute
	{
		FUiSoundMute()  { ++UiSoundMuteDepth; }
		~FUiSoundMute() { --UiSoundMuteDepth; }
	};
	static int32 UiSoundMuteDepth;

	// ── ★ [ПОРТ] 06.10.2026 (волна 2-А, окно разборки 32000): 2D-ПЕТЛЯ по имени С ВОЗВРАТОМ компонента ──
	//   `PlaySound2D(…, true)` петлю заводит, но компонент не отдаёт — остановить её вызывающий не может, а эталон
	//   глушит петлю по имени (`sub_9349C7(g_pSoundMgr, "S_e_disjoint_process.wav")`). Компонент не авто-удаляется
	//   и записан в `SfxLoops` (ползунок «Звук»); останавливает и отпускает его вызывающий. nullptr — звука нет.
	UAudioComponent* PlaySound2DManaged(FName Name);

	// ── SFX 3D позиционный по имени (аналог CSound_PlayName3D). nullptr если звук не найден ──
	//   Дефолт min/max distance = 100/10000 (эталон CSoundEx_ctor).
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	UAudioComponent* PlaySound3D(FName Name, FVector Location, float MinDist = 100.f,
		float MaxDist = 10000.f, bool bLoop = false);

	// ★★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F13): 3D-НАСТРОЙКА СВОЕГО АУДИОКОМПОНЕНТА ТЕМ ЖЕ ЗАКОНОМ, ЧТО `PlaySound3D`.
	//   Кто держит свой компонент мимо менеджера (канал транспорта персонажа: эталон глушит прежний звук того же канала),
	//   получает здесь ту же кривую FMOD3 (min/max, по умолчанию 100/10000 — эталон `CSoundEx_ctor`) и купол слышимости
	//   `asda.Sound.HearingRadiusM`. Прежде канал транспорта чужого игрока звучал БЕЗ затухания и вне купола.
	//   Возврат false — источник за куполом при `asda.Sound.SilenceBeyondMax 1`: вызывающий его не заводит (как `PlaySound3D`).
	bool Configure3D(UAudioComponent* AC, const FVector& Location, float MinDist = 100.f, float MaxDist = 10000.f) const;

	// ── ★★★ 3D-ЗВУК ПО КЛАССУ (30.10.2026). Радиусы, приоритет и лимиты — из таблицы класса (`GetClassSpec`).
	//   `Source` — кто звучит (моб, персонаж): по нему ограничивается число голосов класса на один источник; nullptr — без лимита.
	//   `Class == Auto` — класс выводится из имени ассета. nullptr — звука нет или источник за радиусом.
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	UAudioComponent* PlaySound3DAs(FName Name, FVector Location, EAsdaSoundClass Class, const AActor* Source = nullptr, bool bLoop = false);
	// То же по числовому id (SFT.BIN).
	UAudioComponent* PlaySound3DByIdAs(int32 SoundId, FVector Location, EAsdaSoundClass Class, const AActor* Source = nullptr);
	// Свой аудиокомпонент (канал транспорта) — затухание класса ставится ДО старта; false — источник за радиусом.
	bool Configure3DAs(UAudioComponent* AC, const FVector& Location, EAsdaSoundClass Class);

	// Класс по имени ассета (правило назначения для мест вызова без явного класса; таблица правил — в .cpp).
	static EAsdaSoundClass ClassifyByName(FName Name);
	// Строка таблицы класса. `Name` уточняет размер существа (`mon_s/m/b/l_*`) — громче и крупнее у больших.
	static FAsdaSoundClassSpec GetClassSpec(EAsdaSoundClass Class, FName Name = NAME_None);
	// Единиц UE в метре: замеренный рост персонажа (ед.) / рост человека 1,72 м.
	static float UnitsPerMeter();
	// Замеренный рост модели персонажа, ед. UE (мужская и женская модели; замер — самотест asda.SelfTest.Sound3D).
	static float CharHeightUnits();
	// План запуска (тот же расчёт, что у игрового пути). MinDist/MaxDist > 0 — явные радиусы (окружение карты), иначе — класс.
	bool Plan3D(FName Name, const FVector& Location, EAsdaSoundClass Class, float MinDist, float MaxDist, bool bLoop, FAsdaSound3DPlan& Out);
	// Громкость плана на дистанции (движковый расчёт затухания, 0…1).
	static float EvalGain(const FAsdaSound3DPlan& Plan, float DistUnits);
	// ★ 30.10.2026 `asda.Sound.ForeignHit3D`: звук чужого удара — 3D у цели по таблице эталона (иначе прежний 2D всем).
	static bool ForeignHit3DOn();
	// ★ 30.10.2026 `asda.Sound.WorldEvents3D`: события мира у ЧУЖИХ (вход/воскрешение, барьер, церемония, дроп) — 3D в точке события.
	static bool WorldEvents3DOn();

	// ── SFX по ЧИСЛОВОМУ ID (эталон CSound_PlayName2D/3D: soundId → CSoundFileTable → имя) ──
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void PlaySound2DById(int32 SoundId, bool bLoop = false);   // ★ 22.10.2026: глушится `FUiSoundMute` (кроме петель)

	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	UAudioComponent* PlaySound3DById(int32 SoundId, FVector Location, float MinDist = 100.f,
		float MaxDist = 10000.f, bool bLoop = false);

	// ── Record-звук (NPC/креатуры) с вариантом: "<Base>_<Variant>.wav", Variant==11 → "_99" (Sound_BuildEntryWavName) ──
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void PlayRecordSound2D(FName BaseName, int32 Variant, bool bLoop = false);

	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	UAudioComponent* PlayRecordSound3D(FName BaseName, int32 Variant, FVector Location,
		float MinDist = 100.f, float MaxDist = 10000.f, bool bLoop = false);

	// ── Музыка: единичный трек (как CSoundStream). Сменяет текущий с fade. Из /Game/AsdaAssets/Sounds/Music/ ──
	// FadeSec по умолчанию 0.533с = игровой 16-кадровый фейд (CSound_SetupFade volume/16 × 33.33мс/кадр).
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void PlayMusic(FName Track, bool bLoop = true, float FadeSec = 0.533f);
	// Громкость музыки поменяли из консоли (asda.Sound.MusicVolume) — поправить играющий трек сразу.
	static void OnMusicVolumeChanged(class IConsoleVariable* Var);

	// ★★★ ГРОМКОСТЬ ИЗ ПРОФИЛЯ ИГРОКА (19.09.2026, `UAsdaGameUserSettings`).
	//   Единственная точка, через которую окно настроек 69500 и профиль игрока задают звук.
	//   ПОЧЕМУ С ПРИОРИТЕТОМ «КОНСОЛЬ». У ключей `asda.Sound.*` есть шкала приоритетов UE: значение,
	//   однажды заданное из консоли (в `Input.ini` владельца история хранит `asda.Sound.MusicVolume 0`),
	//   глушит все последующие записи с приоритетом `SetByCode`/`SetByGameSetting` — движок их молча
	//   отбрасывает, и ползунок окна «ничего не делал». Выбор игрока в окне — самый явный источник,
	//   поэтому он пишет ключи рангом `ECVF_SetByConsole`; из консоли ими по-прежнему можно управлять.
	//   Громкость 0…1; `bMuted` — чекбоксы-динамики 70012/70015.
	static void ApplyUserVolumes(float MusicVolume01, bool bMusicMuted, float SfxVolume01, bool bSfxMuted);

	// Действующий множитель громкости звуковых эффектов (0…1, с учётом «заглушено»). Нужен тем, кто
	//   держит СВОЙ аудиокомпонент мимо менеджера (напр. канал транспорта у персонажа).
	static float GetSfxVolumeMul();

	// Переставить громкость всех живых петель эффектов (и вычистить мёртвые ссылки). Зовётся при
	//   смене громкости эффектов — из профиля игрока и из консольного ключа `asda.Sound.SfxVolume`.
	void RefreshSfxLoopVolumes();

	// Музыка по числовому id (эталон CSoundStream_PlayById@0xFA0C90).
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void PlayMusicById(int32 TrackId, bool bLoop = true, float FadeSec = 0.533f);

	// ── Смена трека ЧЕРЕЗ ОЧЕРЕДЬ, как оригинал при переходе город↔поле ──
	// Эталон cSoundSet_QueueOrSetByIndex@0xFA0F50 → CSoundStream_QueueNext@0xFA05B0 (ставит имя в
	//   ожидание + CSoundStream_SetupFadeOut) и CSound_UpdateFade@0xFA0620 (по достижении нуля —
	//   StreamStop и CSound_StreamPlay нового трека с фейдом входа). Т.е. фейды ПОСЛЕДОВАТЕЛЬНЫЕ,
	//   а не одновременные. Повторные вызовы, пока идёт гашение, ИГНОРИРУЮТСЯ (эталонный флаг CSound+72).
	//   Дефолт 0.533с = 16 кадров игрового фейда (шаг громкости volume/16 при 30 fps).
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void QueueMusic(FName Track, float FadeOutSec = 0.533f, float FadeInSec = 0.533f);

	// Имя трека, который играет (или уже поставлен в очередь). None — музыки нет.
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	FName GetMusicTrack() const { return bMusicQueued ? PendingMusicTrack : CurrentMusicTrack; }

	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void StopMusic(float FadeSec = 0.f);

	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	bool IsMusicPlaying() const;

	// ── id → имя (аналог CSoundFileTable_GetNameById). Возвращает FName без расширения .wav. None если нет ──
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	FName ResolveSoundName(int32 SoundId) const;

	// Перезапустить последний сыгранный 2D SFX (аналог storedSoundId — рестарт после fade).
	UFUNCTION(BlueprintCallable, Category="AsdaSound")
	void RestartStoredSound();

	// Резолв SoundBase по имени (кэш LoadObject). Folder — подпапка под /Game/AsdaAssets/ (напр. "Sound").
	USoundBase* ResolveSound(FName Name, const TCHAR* Folder = TEXT("Sound"));

private:
	// ★ Дедуп №2, В2-C (F13): радиусы 3D-звука — min (≤0 → 100) и max (≤min → 10000), max ограничен куполом слышимости.
	static void HearingRange(float MinDist, float MaxDist, float& OutMn, float& OutMx);
	// ★ Дедуп №2, В2-C (F13): кривая затухания FMOD3 inverse (rolloff 0.7) + спад к нулю у края купола — на компонент.
	static void ApplyEtalonAttenuation(UAudioComponent* AC, float Mn, float Mx);

	// Построить имя record-варианта: "<Base>_<Variant>" (Variant==11 → "<Base>_99"). Без расширения.
	static FName BuildVariantName(FName BaseName, int32 Variant);

	// Загрузить таблицу id→имя из DT_SoundTable (SFT.BIN импорт).
	void LoadSoundTable();

	// id → имя wav (без .wav). Из SFT.BIN (DT_SoundTable). Аналог CSoundFileTable idToNameMap.
	UPROPERTY(Transient)
	TMap<int32, FName> IdToName;

	// Кэш имя→SoundBase (не грузить один и тот же wav повторно — аналог CSoundFileTable/pSample).
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<USoundBase>> SoundCache;

	// Единичный музыкальный стрим (как CSoundStream — один трек одновременно).
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComp;

	// ★ Живые ПЕТЛИ звуковых эффектов (2D и 3D с bLoop), запущенные менеджером. Одноразовые звуки
	//   громкость берут в момент запуска, а петля играет долго — без этого списка ползунок «Звук»
	//   не доходил бы до уже звучащих петель. Слабые ссылки: компонент умирает вместе с миром.
	TArray<TWeakObjectPtr<UAudioComponent>> SfxLoops;

	// Последний сыгранный 2D SFX (storedSoundId) — для рестарта.
	UPROPERTY(Transient)
	FName StoredSoundName;

	// ── Очередь смены музыки (эталон CSound: pending-имя + флаг +72 + время фейда +26) ──
	FName       CurrentMusicTrack;              // что играет сейчас
	FName       PendingMusicTrack;              // что стартует после гашения
	bool        bMusicQueued = false;           // эталонный CSound+72: смена уже поставлена в очередь
	float       PendingFadeInSec = 0.f;         // эталонный CSound+26: время фейда входа
	FTimerHandle MusicQueueTimer;

	// Колбэк по завершении гашения — стартует отложенный трек (эталон CSound_UpdateFade, ветка «догасли»).
	void StartQueuedMusic();

	// ★ Общая группа одновременных SFX-голосов — аналог пула каналов FMOD3 оригинала
	//   (FSOUND_Init(44100, 16) — network.c:3277 → audio.c:1859). Создаётся лениво; ключ asda.Sound.MaxVoices.
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> SfxConcurrency;

	// Лимит, с которым создан текущий SfxConcurrency; при смене ключа объект пересоздаётся (0 — ещё не создан).
	int32 SfxConcurrencyMax = 0;
	// ★ Правило переполнения, с которым создана текущая группа: движок читает настройки ТОЛЬКО
	//   при создании группы, поэтому смена ключа требует НОВОГО объекта (как и смена лимита).
	bool SfxConcurrencyQuietest = false;

	// Группа ограничения SFX-голосов с актуальным лимитом из asda.Sound.MaxVoices; nullptr — лимит выключен (0).
	USoundConcurrency* GetSfxConcurrency();

	// Источник дальше MaxDist от точки слушателя (по ней считается затухание) — звук не заводить вовсе.
	bool IsBeyondHearing(UWorld* World, const FVector& Location, float MaxDist) const;

	// ── ★★★ 30.10.2026: классы 3D-звука ──
	// Прежний путь 3D-запуска (ключ `asda.Sound.Spatial3D 0`): затухание после старта — для сравнения «было/стало».
	UAudioComponent* PlaySound3DLegacy(FName Name, FVector Location, float MinDist, float MaxDist, bool bLoop);
	// Общий путь всех 3D-запусков: план → компонент с затуханием ДО старта → лимит на источник.
	UAudioComponent* Play3DInternal(FName Name, const FVector& Location, EAsdaSoundClass Class, float MinDist, float MaxDist,
		const AActor* Source, bool bLoop);
	// Затухание (min, max) — кэш объектов: движок копирует настройки в звук при старте, объект можно делить.
	USoundAttenuation* GetAttenuation(float Mn, float Mx);
	// Группа класса (лимит голосов класса, вес концентрации); nullptr — выключено.
	USoundConcurrency* GetClassConcurrency(EAsdaSoundClass Class);
	// Не больше `Max` звучащих голосов класса у источника: лишний — самый старый — гасится.
	void LimitPerSource(const AActor* Source, EAsdaSoundClass Class, int32 Max, UAudioComponent* NewAC);

	// Кэш затуханий: ключ — радиусы и режимы, значение — объект, который уходит в движок при старте.
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<USoundAttenuation>> AttCache;
	// Группы классов (индекс — класс). Пересоздаются при смене лимита/ключа (движок читает настройки при создании группы).
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundConcurrency>> ClassConc;
	// Подпись каждой группы класса (лимит×1000 + вес×100): при расхождении группа пересоздаётся.
	TArray<int32> ClassConcSig;
	// Звучащие голоса по (источник, класс) — для лимита на источник. Слабые ссылки, чистятся при каждом запуске.
	TMap<uint64, TArray<TWeakObjectPtr<UAudioComponent>>> SourceVoices;

	// ── ★★★ 08.11.2026 [ОРИГИНАЛ] ФЛАГ ПЕТЛИ ВЫЗОВА (пачка 18, баг 3; Reports/batch18/sound_loop/REPORT.md) ──
	//   Эталон: последний аргумент `CSoundFlag_PlaySound2D` / `Audio_Play3DSound` — флаг петли вызова; `CSound_LoadSample@0xFA2580`
	//   ставит `FSOUND_Sample_SetMode(…, 2)` (FSOUND_LOOP_NORMAL) при 1 и `…, 1` (LOOP_OFF) при 0. Петля — свойство ВЫЗОВА, не файла.
	//   Порт отдавал зацикливание ассету, а у всех 1466 волн эффектов `bLooping = False` — звук с флагом 1 играл один раз.
	//   Волна, не зацикленная сама, при `bLoop` играет через рантайм-обёртку (Wave Player с bLooping — бесшовная петля движка).
	//   Ключ `asda.Sound.LoopFlag` (1 — по эталону, 0 — прежнее поведение).
	USoundBase* LoopVariant(USoundBase* Snd);
	// Журнал петли (старт / повтор №N / стоп): подписка до `Play()` — процент проигрывания у петли растёт за 1.0.
	void TrackLoop(UAudioComponent* AC, FName Name);
	void OnLoopPercent(const UAudioComponent* AC, const USoundWave* Wave, const float Pct);
	void OnLoopFinished(UAudioComponent* AC);
	// Обёртки по волне (одна на волну, живут с подсистемой).
	UPROPERTY(Transient)
	TMap<TObjectPtr<USoundWave>, TObjectPtr<USoundCue>> LoopCues;
	// Звучащие петли: имя и число пройденных кругов.
	struct FLoopTrack { FName Name; int32 Laps = 0; };
	TMap<TWeakObjectPtr<UAudioComponent>, FLoopTrack> LoopTracks;
};

// ★ 08.11.2026: журнал петель для самотеста `asda.SelfTest.SoundLoop` (строки «старт/повтор/стоп <имя>»). Пишется между Start и Stop.
ASDAREBORN_API void AsdaSoundLoopJournalStart();
ASDAREBORN_API TArray<FString> AsdaSoundLoopJournalStop();
ASDAREBORN_API TArray<FString> AsdaSoundLoopJournalPeek();
