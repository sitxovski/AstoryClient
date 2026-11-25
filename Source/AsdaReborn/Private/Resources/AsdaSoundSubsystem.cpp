// Copyright (c) 2026 @sitxovski (Discord)
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (ToAssetName)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"       // FSoundAttenuationSettings (min/max distance для 3D)
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"                 // очередь смены музыки (последовательные фейды)
#include "Misc/Paths.h"
#include "Sound/SoundConcurrency.h"       // общий лимит SFX-голосов (пул каналов FMOD3 оригинала)
#include "GameFramework/PlayerController.h" // точка слушателя для отсечки по дальности
#include "AudioDevice.h"                  // FAudioDevice::CreateComponent — компонент с затуханием ДО старта
#include "GameFramework/Actor.h"          // источник звука (лимит голосов на источник)
#include "Sound/AmbientSound.h"            // ★ 05.11.2026: запечённые в уровень эмиттеры (MapSound_*) — под ползунок «Звук»
#include "Engine/Level.h"                  // ★ 05.11.2026: обход акторов уровня до их активации
#include "ActiveSound.h"                   // ★ 05.11.2026: звук нажатия кнопки Slate — с громкостью эффектов
#include "Sound/SlateSound.h"              // FSlateSound (звук стиля кнопки)
#include "Sound/ISlateSoundDevice.h"       // интерфейс звукового устройства Slate
#include "Framework/Application/SlateApplication.h"   // установка своего устройства звука Slate
#include "Resources/AsdaGameUserSettings.h"   // ★ 05.11.2026: «Прорисовка травы» (Render3D[2]) — гейт эмиттеров карты, как GRASS RENDER
#include "Containers/Ticker.h"              // ★ 05.11.2026: покадровый драйвер эмиттеров карты
#include "Camera/PlayerCameraManager.h"    // позиция камеры — точка условия запуска эмиттера
#include "UObject/UObjectIterator.h"       // обход подсистем звука
#include "Sound/SoundCue.h"                // ★ 08.11.2026: петля вызова — рантайм-обёртка волны (LoopVariant)
#include "Sound/SoundNodeWavePlayer.h"     // ★ 08.11.2026: Wave Player с bLooping (FMOD LOOP_NORMAL)
#include "Sound/SoundWave.h"               // ★ 08.11.2026: длительность и свойства волны
#include "AsdaDevConsole.h"                // ★ R4, доля a: ASDA_DEV_CVAR — ключ правки производительности
#include "AsdaPerf.h"                      // ★ R4, доля a: область замера перебора подсистем звука

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSound, Log, All);

// ★★★ ЛИМИТ ОДНОВРЕМЕННЫХ ГОЛОСОВ [ОРИГИНАЛ] (разбор palace_of_queen, 11.09.2026).
//   Эталон: CSoundMgr_Init(g_pSoundMgr, 2, 44100, 16) (network.c:3277) → FSOUND_Init(44100, 16, 0)
//   (audio.c:1859): у FMOD3 ВСЕГО 16 каналов на все звуки, и музыкальный стрим
//   (FSOUND_Stream_PlayEx(-1,…), audio.c:251/279) занимает один из них. Каждый SFX — FSOUND_PlaySoundEx(-1,…)
//   (audio.c:986/1007): при занятом пуле голос не заводится (channel == -1 → CSound_FreeSample).
//   У порта лимита не было вовсе (только движковые 32 голоса), и шаги десятков мобов складывались в гул.
//   Здесь все одноразовые SFX (2D и 3D) — одна группа на (MaxVoices − 1) голосов (один — музыке).
//   ★★★ ПРАВИЛО ПЕРЕПОЛНЕНИЯ ПУЛА ДОКАЗАНО ПО БИНАРЮ libFMOD3 (ревизия 17.09.2026), а не по семантике.
//   Клиент обрабатывает ровно один исход — `ch = FSOUND_PlaySoundEx(-1, …); if (ch == -1) отказ`
//   (`CSound_PlaySound3D@0xFA2920`, `CSound_PlaySound2D@0xFA2A00` — единственные два вызова в бинаре).
//   ⚠ ИЗ ЭТОГО НЕЛЬЗЯ ЗАКЛЮЧИТЬ, что оригинал теряет звук: решение принимает САМА БИБЛИОТЕКА, и эта
//   ветка практически мертва. `fmod.dll` упакована UPX (статический дизасм даёт мусор), её распаковали
//   загрузкой в 32-битный процесс; аллокатор канала `sub_10025E25` при `channel == FSOUND_FREE`:
//     * каналы с `priority >= 256` не трогает вовсе — это стримы (музыке ставится 0x100);
//     * среди остальных ищет МИНИМАЛЬНЫЙ priority, при равных — МИНИМАЛЬНУЮ текущую громкость
//       `[chan+0x74]` (микшер пишет туда (leftVol+rightVol)/2, шкала 0..255);
//     * ОТКАЗЫВАЕТ только если найденная жертва ВАЖНЕЕ нового звука.
//   А приоритеты у всех игровых SFX ОДИНАКОВЫ: конструктор сэмпла ставит 255, и игра их не меняет
//   (ни `FSOUND_SetPriority`, ни `Sample_SetDefaults`, ни `SetMaxPlaybacks` в декомпиле нет).
//   ⇒ Условие отказа не выполняется никогда, и выбор всегда падает на САМЫЙ ТИХИЙ звук — это ровно
//   `StopQuietest`. Ставить `PreventNew` нельзя: группа общая для всех одноразовых SFX, и при занятых
//   голосах молча пропадали бы щелчки интерфейса, свои шаги, звук крита и смерти, которые в оригинале
//   украли бы канал и прозвучали.
//   ⚠ Само НАЛОЖЕНИЕ звуков мобов — поведение оригинала: выделенного канала на сущность нет,
//   дедупликации по имени нет, кулдауна и вероятности нет (проверено по всем 38 точкам вызова).
//   ★ «16 каналов» — это ПРОГРАММНЫЙ пул `FSOUND_Init(44100, 16, 0)`. Игра грузит сэмплы с флагами
//   `FSOUND_HW3D/HW2D`, и при живом аппаратном микшере пул был бы другим (`sub_10026215` выбирает по
//   `sample+0x168`), но на Vista и новее аппаратного DirectSound-3D нет — значит эти самые 16.
static int32 GAsdaSoundMaxVoices = 16;
static FAutoConsoleVariableRef CVarAsdaSoundMaxVoices(
	TEXT("asda.Sound.MaxVoices"),
	GAsdaSoundMaxVoices,
	TEXT("Пул звуковых голосов как у FMOD3 оригинала (16, один из них — музыка). 0 = без лимита (прежнее поведение порта)."),
	ECVF_Default);

// ★★★ ТИШИНА ЗА ПРЕДЕЛОМ СЛЫШИМОСТИ [ПОРТ — исправление оригинала, решение владельца 11.09.2026].
//   У FMOD3 спад 1/d прекращается на maxDist и дальше держится «пол» 100/7030 = −36.9 дБ
//   (SPEC_FMOD_ATTEN.md §3) — дальние мобы всей карты не глохнут никогда. Владелец: «звуки должны
//   затихать с расстоянием». Кривая до 0.9·maxDist остаётся эталонной (inverse, rolloff 0.7), на
//   последних 10 % плавно (smoothstep) сводится к нулю — без щелчка на границе; источник дальше
//   maxDist не заводится вовсе и голос не занимает. 0 = эталонный пол −36.9 дБ.
// ★★★ КУПОЛ СЛЫШИМОСТИ ИГРОКА (просьба владельца 19.09.2026: «стандартный, средний, как в современных
//   играх»). Радиус в МЕТРАХ, за которым звуки мира не слышны вовсе; ограничивает радиус СВЕРХУ.
//   35 м — типичное значение для звуков мобов и боя в современных MMO/ARPG (обычный диапазон 30–40 м).
//   Оригинал купола не имеет: его умолчание — 100 м. ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА по прямому решению владельца;
//   0 = как в оригинале. Внутри купола кривая затухания остаётся эталонной (см. разбор в PlaySound3D).
static float GAsdaSoundHearingRadiusM = 35.f;
static FAutoConsoleVariableRef CVarAsdaSoundHearingRadius(
	TEXT("asda.Sound.HearingRadiusM"),
	GAsdaSoundHearingRadiusM,
	TEXT("Купол слышимости игрока, метры (35 = современный стандарт; 0 = как в оригинале, 100 м)."),
	ECVF_Default);

static int32 GAsdaSoundSilenceBeyondMax = 1;
static FAutoConsoleVariableRef CVarAsdaSoundSilenceBeyondMax(
	TEXT("asda.Sound.SilenceBeyondMax"),
	GAsdaSoundSilenceBeyondMax,
	TEXT("1 = 3D-звук плавно затихает к maxDist и дальше не играет (исправление); 0 = пол −36.9 дБ как у FMOD3 оригинала."),
	ECVF_Default);

// ★★★ [ИСПРАВЛЕНИЕ] ЗАТУХАНИЕ — ДО СТАРТА ЗВУКА (30.10.2026, баг игроков «звуки боя без затухания по расстоянию»).
//   ПРИЧИНА БАГА. Прежний путь: `UGameplayStatics::SpawnSoundAtLocation(…, AttenuationSettings = nullptr, …)` — функция
//   ЗАПУСКАЕТ звук сразу, — а затухание ставилось ПОСЛЕ, `UAudioComponent::AdjustAttenuation`. В UE 5.8 решение «у звука есть
//   затухание и позиция» принимается один раз, при старте: `UAudioComponent::PlayInternal` пишет
//   `ActiveSound.bHasAttenuationSettings = (GetAttenuationSettingsToApply() != nullptr)` (AudioComponent.cpp:826), а у
//   звука без своего ассета затухания там nullptr. `AdjustAttenuation` у активного звука меняет только
//   `ActiveSound.AttenuationSettings` (AudioComponent.cpp:1569) — флаг остаётся false, и `FActiveSound::UpdateWaveInstances`
//   (ActiveSound.cpp:1182) не зовёт `UpdateAttenuation`: ни спада громкости, ни панорамы. ВСЕ «3D»-звуки порта (бой, мобы,
//   навыки, эффекты, окружение карт) звучали как 2D на полной громкости с любой дистанции — кривая FMOD3 существовала
//   только на бумаге. Исправление: компонент создаётся с объектом затухания (`FAudioDevice::CreateComponent`,
//   `Params.AttenuationSettings`), приоритет и лимиты ставятся до `Play`. 0 = прежний путь (затухание после старта).
static int32 GAsdaSoundSpatial3D = 1;
static FAutoConsoleVariableRef CVarAsdaSoundSpatial3D(
	TEXT("asda.Sound.Spatial3D"),
	GAsdaSoundSpatial3D,
	TEXT("1 = 3D-звук стартует с затуханием и панорамой (исправление); 0 = прежний путь (затухание после старта — не действовало)."),
	ECVF_Default);

// ★★★ [ПОРТ] КЛАССЫ 3D-ЗВУКА (30.10.2026, владелец: «у каждого эффекта, моба, звуков мобов и персонажей установи радиусы
//   звучания нормальные» по слуху человека, рост персонажа = рост человека). 1 = радиусы по классу (`GetClassSpec`);
//   0 = прежний единый закон (внутренний 1 м, внешний — купол `asda.Sound.HearingRadiusM`).
static int32 GAsdaSoundClasses = 1;
static FAutoConsoleVariableRef CVarAsdaSoundClasses(
	TEXT("asda.Sound.Classes"),
	GAsdaSoundClasses,
	TEXT("1 = радиусы 3D-звука по классу (шаги, удар, голос, магия…); 0 = единый радиус для всех (прежнее поведение)."),
	ECVF_Default);

// Множитель всех радиусов 3D-звука (классовых, купола и данных карты) — для подстройки на живой сцене. 1 = таблица.
static float GAsdaSoundDistanceScale = 1.f;
static FAutoConsoleVariableRef CVarAsdaSoundDistanceScale(
	TEXT("asda.Sound.DistanceScale"),
	GAsdaSoundDistanceScale,
	TEXT("Множитель радиусов 3D-звука (1 = таблица классов; 2 = вдвое дальше; 0.5 = вдвое ближе)."),
	ECVF_Default);

// ★ [ПОРТ] Лимиты против «каши» в толпе (30.10.2026): голосов класса на сцену (ближние вытесняют дальних) и на один источник,
//   «вес концентрации» (прежние голоса класса чуть тише при каждом новом). Оригинал этого не делал — только общий пул 16 каналов
//   (`asda.Sound.MaxVoices`, он остаётся). 0 = без классовых лимитов (прежнее поведение).
static int32 GAsdaSoundClassConcurrency = 1;
static FAutoConsoleVariableRef CVarAsdaSoundClassConcurrency(
	TEXT("asda.Sound.ClassConcurrency"),
	GAsdaSoundClassConcurrency,
	TEXT("1 = лимит голосов по классу и на источник (ближние вытесняют дальних); 0 = только общий пул (прежнее поведение)."),
	ECVF_Default);

// ★ [ПОРТ] Приглушение верхних частот с расстоянием (поглощение воздухом и препятствиями — главный слуховой признак дали):
//   с 3 внутренних радиусов до края — срез с 20 кГц до 6 кГц. 0 = без фильтра.
static int32 GAsdaSoundDistanceLPF = 1;
static FAutoConsoleVariableRef CVarAsdaSoundDistanceLPF(
	TEXT("asda.Sound.DistanceLPF"),
	GAsdaSoundDistanceLPF,
	TEXT("1 = дальние звуки глуше (срез верхов 20→6 кГц к краю радиуса); 0 = без фильтра."),
	ECVF_Default);

// ★★★ МАСШТАБ «РОСТ ПЕРСОНАЖА = РОСТ ЧЕЛОВЕКА» (правило владельца 30.10.2026).
//   Рост модели персонажа замерен самотестом `asda.SelfTest.Sound3D` (раздел «рост»: верх головы − подошва по костям
//   скелета в позе покоя, мужская и женская базовые модели); средний рост человека — 1,72 м (мужчины ~1,76, женщины ~1,64).
//   Отсюда 156 / 1,72 = 90,7 ед. на метр (замер 30.10: мужская 155,9, женская 156,3 — совпадает с эталонным дампом «рост ~156»).
//   Эталонный FMOD3 считал `distancefactor 0.01` (100 ед. = 1 м) — расхождение 10 %, правило владельца — рост персонажа.
static constexpr float kAsdaHumanHeightM    = 1.72f;    // средний рост человека, м
static constexpr float kAsdaCharHeightUnits = 156.f;    // замеренный рост модели персонажа, ед. UE (самотест 30.10: муж 155,9, жен 156,3)

// ★★★ ПОРОГ СЛЫШИМОСТИ — фон открытой местности днём (лес, поле, лёгкий ветер): 45 дБ(А).
//   Радиус затухания до нуля R = 10^((L1 − L_фон)/20) м — точка, где звук с громкостью L1 на 1 м по закону −6 дБ на удвоение
//   расстояния опускается до фона и маскируется им. Сверху ограничен куполом `asda.Sound.HearingRadiusM` (решение владельца 19.09).
static constexpr float kAsdaBackgroundDb = 45.f;

float UAsdaSoundSubsystem::CharHeightUnits() { return kAsdaCharHeightUnits; }

float UAsdaSoundSubsystem::UnitsPerMeter() { return kAsdaCharHeightUnits / kAsdaHumanHeightM; }

// ★★★ [ОРИГИНАЛ] ЗВУК ЧУЖОГО УДАРА — 3D У ЦЕЛИ (30.10.2026). Эталон: удар ДРУГОГО персонажа по мобу звучит
//   `sound3D_playNameAtZeroPos(…, позиция цели, …)` своей таблицей (`Player_ProcessAttackTick@0x99F2A0` entity_char.c:944-956 и
//   :1030-1042 крит, `CCharacter_AttackAnimTick@0x9A2200` :2221-2236 / :2303-2316 — обе зовёт `charUpdateActionStateTick` для
//   чужих); 2D (`csoundPlayName2D`) — только ветки СВОЕГО удара (`CCharacter_ResolveBasicAttack@0xA47DC0`,
//   `Skill_SpawnHitFeedback@0x99B0F0`, их зовёт тик своего персонажа render_core.c:3533/3542). Порт играл 2D-таблицу своего
//   удара на ЛЮБОЙ удар, видимый игроку, — бой далёких игроков звучал в ухо на полной громкости. 0 = прежнее (2D всем).
static int32 GAsdaSoundForeignHit3D = 1;
static FAutoConsoleVariableRef CVarAsdaSoundForeignHit3D(
	TEXT("asda.Sound.ForeignHit3D"),
	GAsdaSoundForeignHit3D,
	TEXT("1 = звук чужого удара — 3D у цели по таблице оригинала; 0 = 2D-звук своего удара на любой удар (прежний порт)."),
	ECVF_Default);
bool UAsdaSoundSubsystem::ForeignHit3DOn() { return GAsdaSoundForeignHit3D != 0; }

// ★★★ [ИСПРАВЛЕНИЕ] СОБЫТИЯ МИРА У ЧУЖИХ — 3D (30.10.2026, владелец: «2D только у интерфейса»). Оригинал часть событий чужих
//   играет 2D: столб входа/воскрешения чужого (`GameNet_OnLocalPlayerMapEntry@0xF43390`, ветка чужого), падение добычи
//   (`DropItemMgr_SpawnGroundItem@0xF443D0` → `CSoundFlag_PlaySound2D`), барьер при ударе моба по чужому. Порт к тому же играл
//   2D церемонию соулмейтов (эталон — 3D у каждого из пары). Теперь все — 3D в точке события; свои — как прежде (2D у себя
//   и 3D у себя по громкости одинаковы). 0 = прежнее.
static int32 GAsdaSoundWorldEvents3D = 1;
static FAutoConsoleVariableRef CVarAsdaSoundWorldEvents3D(
	TEXT("asda.Sound.WorldEvents3D"),
	GAsdaSoundWorldEvents3D,
	TEXT("1 = события мира у чужих (вход, барьер, церемония, дроп) — 3D в точке события; 0 = прежнее (2D)."),
	ECVF_Default);
bool UAsdaSoundSubsystem::WorldEvents3DOn() { return GAsdaSoundWorldEvents3D != 0; }

// ★★★ ТАБЛИЦА КЛАССОВ 3D-ЗВУКА. Громкость источника на 1 м (дБ(А), типовые значения акустики) → радиус до нуля (формула выше).
//   Внутренний радиус — 1 м (эталонный min 100 ед.), у крупных существ больше (размер источника). Приоритет — кого движок
//   глушит последним; лимиты — голосов класса на сцену и на один источник; вес — множитель прежних голосов класса.
FAsdaSoundClassSpec UAsdaSoundSubsystem::GetClassSpec(EAsdaSoundClass Class, FName Name)
{
	struct FRow { const TCHAR* Name; float Db; float Inner; float Prio; int32 MaxClass; int32 MaxSrc; float Vol; };
	static const FRow kRows[(int32)EAsdaSoundClass::Count] = {
		/* Auto     */ { TEXT("по имени"),   90.f, 1.f, 1.0f, 0, 0, 1.00f },   // не используется: класс выводится до таблицы
		/* Step     */ { TEXT("шаги"),       60.f, 1.f, 0.5f, 6, 1, 0.85f },   // бег по грунту ~60 дБ → 5,6 м
		/* Item     */ { TEXT("вещи"),       60.f, 1.f, 0.8f, 4, 2, 0.90f },   // падение вещи, звон монет ~60 дБ → 5,6 м
		/* Action   */ { TEXT("действие"),   70.f, 1.f, 1.0f, 6, 2, 0.85f },   // взмах, тетива, всплеск, копание ~70 дБ → 17,8 м
		/* Hit      */ { TEXT("удар"),       90.f, 1.f, 1.5f, 8, 2, 0.85f },   // удар по броне/щиту ~90 дБ → 178 м (купол)
		/* Voice    */ { TEXT("голос"),      80.f, 1.f, 2.0f, 4, 1, 0.90f },   // крик человека ~80 дБ → 56 м (купол)
		/* Creature */ { TEXT("существо"),   80.f, 1.f, 1.2f, 6, 2, 0.85f },   // рык малого зверя ~80 дБ (крупнее — громче)
		/* Magic    */ { TEXT("магия"),      90.f, 1.f, 1.5f, 8, 3, 0.85f },   // взрыв, разряд, удар заклинания ~90 дБ (купол)
		/* Vehicle  */ { TEXT("транспорт"),  75.f, 1.f, 1.0f, 4, 1, 0.90f },   // галоп, мотор ~75 дБ → 31,6 м
		/* Ambient  */ { TEXT("окружение"),  90.f, 1.f, 0.6f, 0, 0, 1.00f },   // радиусы — из данных карты (Pos.POS), здесь запас
		/* Ui       */ { TEXT("интерфейс"),   0.f, 0.f, 1.0f, 0, 0, 1.00f },   // только 2D
	};
	const int32 Idx = FMath::Clamp((int32)Class, 0, (int32)EAsdaSoundClass::Count - 1);
	const FRow& R = kRows[Idx];
	FAsdaSoundClassSpec S;
	S.Name = R.Name; S.SourceDb = R.Db; S.InnerM = R.Inner; S.Priority = R.Prio;
	S.MaxPerClass = R.MaxClass; S.MaxPerSource = R.MaxSrc; S.ConcVolumeScale = R.Vol;
	// Размер существа — по приставке ассета моба (`mon_s_` малый, `mon_m_` средний, `mon_b_` большой, `mon_l_` огромный):
	//   громче источник (+0/+3/+6/+10 дБ) и крупнее зона полной громкости (1/1/1,5/2 м). Касается шагов и голоса существа.
	if (!Name.IsNone() && (Class == EAsdaSoundClass::Step || Class == EAsdaSoundClass::Creature))
	{
		const FString N = Name.ToString().ToLower();
		if (N.StartsWith(TEXT("mon_m_")))      { S.SourceDb += 3.f; }
		else if (N.StartsWith(TEXT("mon_b_"))) { S.SourceDb += 6.f;  S.InnerM = 1.5f; }
		else if (N.StartsWith(TEXT("mon_l_"))) { S.SourceDb += 10.f; S.InnerM = 2.f; }
	}
	S.OuterM = (S.SourceDb > kAsdaBackgroundDb) ? FMath::Pow(10.f, (S.SourceDb - kAsdaBackgroundDb) / 20.f) : 0.f;
	return S;
}

// ★★★ ПРАВИЛО НАЗНАЧЕНИЯ КЛАССА ПО ИМЕНИ АССЕТА — для мест вызова без явного класса (старый `PlaySound3D`, данные навыков,
//   эффектов, хранителя). Имена — из клиента оригинала (Data/Sound): `mon_*_<фаза>` — звуки мобов (Sound_BuildEntryWavName),
//   `S_c_*`/`Snd_char_*` — персонажи и навыки, `S_e_*` — эффекты и механизмы, `Sod_drop_*` — падение вещей, `S_OBJ_*` — карта.
//   Интерфейса здесь нет: интерфейсные имена, сыгранные в 3D (автоматы бустера и гачи в мире), — «действие» у автомата.
EAsdaSoundClass UAsdaSoundSubsystem::ClassifyByName(FName Name)
{
	const FString N = Name.ToString().ToLower();
	auto Has = [&N](const TCHAR* S) { return N.Contains(S); };
	// Мобы: «<база>_<фаза>», фазы эталона (AAsdaMobActor::PlayMobSound): 1/2 бег/шаг, 3 рык агра, 4 смерть, 5/6/9/10 удар
	//   моба, 7 получение урона, 8 крит по мобу, 11–14 навыки, 30–32 рык замаха.
	if (N.StartsWith(TEXT("mon_")) || N.StartsWith(TEXT("greengnom")))
	{
		int32 Us = INDEX_NONE;
		N.FindLastChar(TEXT('_'), Us);
		const int32 Phase = (Us != INDEX_NONE) ? FCString::Atoi(*N.Mid(Us + 1)) : -1;
		switch (Phase)
		{
		case 1: case 2:                       return EAsdaSoundClass::Step;
		case 5: case 6: case 7: case 8:
		case 9: case 10:                      return EAsdaSoundClass::Hit;
		case 11: case 12: case 13: case 14:
		case 99:                              return EAsdaSoundClass::Magic;
		default:                              return EAsdaSoundClass::Creature;   // 3, 4, 30–32 и прочие — голос существа
		}
	}
	if (N.StartsWith(TEXT("s_obj"))) { return EAsdaSoundClass::Ambient; }
	if (Has(TEXT("ride")) || Has(TEXT("riding")) || Has(TEXT("vehicle"))) { return EAsdaSoundClass::Vehicle; }
	if (N.StartsWith(TEXT("sod_drop")) || Has(TEXT("pickup")) || Has(TEXT("coin")) || N.StartsWith(TEXT("s_e_get")))
	{
		return EAsdaSoundClass::Item;
	}
	if (Has(TEXT("walk")) || Has(TEXT("_run"))) { return EAsdaSoundClass::Step; }
	if (Has(TEXT("fishing")) || Has(TEXT("shovel")) || Has(TEXT("mining")) || N.StartsWith(TEXT("s_e_")) || N.StartsWith(TEXT("s_item")))
	{
		// Рыбалка, копание, добыча; механизмы автоматов и интерфейсные имена, сыгранные в мире; хранитель души — ниже.
		if (N.StartsWith(TEXT("s_e_sb_")) || N.StartsWith(TEXT("s_e_sm_"))) { return EAsdaSoundClass::Magic; }
		return EAsdaSoundClass::Action;
	}
	// Персонажи: голос (вскрик крита `S_c_cri1/2`, смерть, ярость), удары и криты оружия, взмахи и выстрелы.
	if (N == TEXT("s_c_cri1") || N == TEXT("s_c_cri2") || Has(TEXT("die")) || Has(TEXT("cridamage")) || Has(TEXT("anger")))
	{
		return EAsdaSoundClass::Voice;
	}
	if (Has(TEXT("damage")) || Has(TEXT("normal2")) || Has(TEXT("cri")) || Has(TEXT("hit")) || Has(TEXT("barrior"))
		|| Has(TEXT("crash")) || Has(TEXT("balnormal2")))
	{
		return EAsdaSoundClass::Hit;
	}
	if (Has(TEXT("normal1")) || Has(TEXT("attack")) || Has(TEXT("arrowfly")) || Has(TEXT("shot")) || Has(TEXT("swing")))
	{
		return EAsdaSoundClass::Action;
	}
	return EAsdaSoundClass::Magic;   // навыки (`S_c_F2*`, `Hellfire`, `Icearrow`…), вход, уровень, воскрешение, эффекты
}

// ★ 05.11.2026: предобъявления — определения ниже по файлу, рядом с ключами громкости (`asda.Sound.LevelAmbientSlider`,
//   `asda.Sound.SlateSoundSlider`).
static int32 AsdaApplySfxToLevelAmbients(UWorld* World);   // множитель эффектов всем AmbientSound игрового мира
static void AsdaHookLevelAmbients(bool bOn);               // повесить/снять колбэки мира (счётчик подсистем)
static void AsdaInstallSlateSoundDevice();                 // своё устройство звука Slate (раз на процесс, только игра)

// ★ R4, доля a (14.11.2026; запись 11 от 15.11.2026 — прежний обход и ключ убраны) [ПОРТ, производительность]: живые подсистемы
//   звука — списком, а не обходом всех объектов. Покадровый драйвер эмиттеров карты (`AsdaMapEmitterTick`) каждый кадр шёл
//   `TObjectIterator` по всему массиву объектов процесса ради единственного экземпляра подсистемы звука. Экземпляр
//   записывается в список при `Initialize`; из списка он уходит, когда объект освобождён (слабая ссылка), — так набор
//   совпадает с обходом всех объектов и в окне между `Deinitialize` и сборкой мусора.
static TArray<TWeakObjectPtr<UAsdaSoundSubsystem>> GAsdaSoundInstances;   // экземпляры подсистемы звука (все, что прошли Initialize и ещё не освобождены)

// Перебор живых подсистем звука в порядке создания (область замера `Sound.EachInstance`).
static void AsdaForEachSoundSubsystem(TFunctionRef<void(UAsdaSoundSubsystem*)> Fn)
{
	ASDA_PERF_SCOPE("Sound.EachInstance");
	for (int32 i = GAsdaSoundInstances.Num() - 1; i >= 0; --i)
	{
		if (!GAsdaSoundInstances[i].IsValid(/*bEvenIfGarbage=*/true)) { GAsdaSoundInstances.RemoveAt(i); }   // без перестановки: порядок обхода — порядок создания
	}
	for (int32 i = 0; i < GAsdaSoundInstances.Num(); ++i)
	{
		if (UAsdaSoundSubsystem* S = GAsdaSoundInstances[i].Get(/*bEvenIfGarbage=*/true)) { Fn(S); }
	}
}

void UAsdaSoundSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	GAsdaSoundInstances.AddUnique(this);   // ★ R4, доля a: экземпляр — в список (перебор вместо обхода всех объектов)
	LoadSoundTable();   // id→имя из SFT.BIN (DT_SoundTable)
	// ★ 05.11.2026: звуки, запечённые в уровень, — под ползунком «Звук» с первого кадра каждого мира (см. `asda.Sound.LevelAmbientSlider`).
	AsdaHookLevelAmbients(true);
	AsdaApplySfxToLevelAmbients(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	// ★ 05.11.2026: щелчок кнопок Slate — под ползунком «Звук» (см. `asda.Sound.SlateSoundSlider`). Только в игре: в редакторе
	//   устройство Slate общее с интерфейсом редактора. Ставится раз на процесс и живёт до `FSlateApplication::Shutdown`
	//   (он идёт раньше выгрузки модулей — `FEngineLoop::Exit`).
	AsdaInstallSlateSoundDevice();
}

void UAsdaSoundSubsystem::Deinitialize()
{
	AsdaHookLevelAmbients(false);   // ★ 05.11.2026: снять колбэки мира с последней подсистемой
	if (MusicComp) { MusicComp->Stop(); MusicComp = nullptr; }
	SoundCache.Empty();
	IdToName.Empty();
	Super::Deinitialize();
}

void UAsdaSoundSubsystem::LoadSoundTable()
{
	IdToName.Empty();
	UDataTable* DT = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Sound/DT_SoundTable.DT_SoundTable"),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!DT)
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("DT_SoundTable не найдена — звук по числовому id недоступен"));
		return;
	}
	DT->ForeachRow<FAsdaSoundTableRow>(TEXT("LoadSoundTable"),
		[this](const FName&, const FAsdaSoundTableRow& Row)
		{
			// Имя wav из SFT.BIN несёт расширение (.wav) — срезаем: ассеты в проекте без него.
			const FName Base(*FPaths::GetBaseFilename(Row.wav));
			if (!Base.IsNone()) { IdToName.Add(Row.id, Base); }
		});
	UE_LOG(LogAsdaSound, Log, TEXT("Таблица звуков загружена: %d id→имя"), IdToName.Num());
}

FName UAsdaSoundSubsystem::ResolveSoundName(int32 SoundId) const
{
	if (const FName* Found = IdToName.Find(SoundId)) { return *Found; }
	return NAME_None;
}

FName UAsdaSoundSubsystem::BuildVariantName(FName BaseName, int32 Variant)
{
	// Sound_BuildEntryWavName: "<base>_<variant>", спец-кейс variant==11 → "<base>_99".
	const int32 V = (Variant == 11) ? 99 : Variant;
	return FName(*FString::Printf(TEXT("%s_%d"), *BaseName.ToString(), V));
}

USoundBase* UAsdaSoundSubsystem::ResolveSound(FName Name, const TCHAR* Folder)
{
	if (Name.IsNone())
	{
		return nullptr;
	}
	if (TObjectPtr<USoundBase>* Found = SoundCache.Find(Name))
	{
		return Found->Get();
	}
	// Имена трёх wav оригинала содержат ПРОБЕЛ ("S_c_M2Blood Invade0.wav") — в имени пакета UE
	//   пробел недопустим, импортёр кладёт их как "S_c_M2Blood_Invade0". Нормализуем так же,
	//   как для текстур названий локаций (UAsdaZoneSubsystem::LoadNameTexture).
	// Конвенция раскладки проекта: пакет = имя ассета (<Folder>/<N>.<N>), N = AsdaMapRegistry::ToAssetName(имя) (★ волна 2).
	const FString Path = AsdaAssetPaths::SoundObject(Folder, Name.ToString());   // ★ волна 2 п. 7: правило в реестре путей
	USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	SoundCache.Add(Name, Snd);   // кэшируем и nullptr (не долбить отсутствующее)
	return Snd;
}

// ★ Ключ ОТСТУПЛЕНИЯ от оригинала при полном пуле голосов. 0 (умолчание) — как оригинал: новый звук
//   ворует канал у самого тихого. 1 — новый звук не заводится вовсе; это НЕ эталон, и вместе с шагами
//   мобов глохнут интерфейс, свои шаги и криты — но ключ оставлен на случай, если владелец захочет
//   послушать разницу на живой сцене.
static int32 GAsdaSoundPoolFullPreventNew = 0;
static FAutoConsoleVariableRef CVarAsdaSoundPoolFull(
	TEXT("asda.Sound.PoolFullPreventNew"),
	GAsdaSoundPoolFullPreventNew,
	TEXT("0 (умолчание) = при полном пуле новый звук обрывает самый тихий (как оригинал); 1 = новый звук не заводится."),
	ECVF_Default);

USoundConcurrency* UAsdaSoundSubsystem::GetSfxConcurrency()
{
	if (GAsdaSoundMaxVoices <= 0) { return nullptr; }   // ключ отката: лимита нет
	// Один канал пула оригинала держит музыкальный стрим → SFX достаётся на один меньше.
	const int32 WantMax = FMath::Max(1, GAsdaSoundMaxVoices - 1);
	// ★ Движок копирует настройки в группу только при её создании и держит группу, пока в ней звучит
	//   хоть один звук, — правка MaxCount у живого объекта не действует (и пишется из игрового потока
	//   при чтении аудиопотоком). Поэтому при смене лимита создаём НОВЫЙ объект: он даёт новую группу
	//   с новым лимитом, а старая доиграет свои звуки и исчезнет сама.
	const bool bWantQuietest = (GAsdaSoundPoolFullPreventNew == 0);
	if (!SfxConcurrency || SfxConcurrencyMax != WantMax || SfxConcurrencyQuietest != bWantQuietest)
	{
		SfxConcurrency = NewObject<USoundConcurrency>(this);
		// ★ `StopQuietest` = «украсть канал у самого тихого», как делает libFMOD3 (разбор выше).
		SfxConcurrency->Concurrency.ResolutionRule = (GAsdaSoundPoolFullPreventNew != 0)
			? EMaxConcurrentResolutionRule::PreventNew
			: EMaxConcurrentResolutionRule::StopQuietest;
		SfxConcurrency->Concurrency.MaxCount = WantMax;
		SfxConcurrencyMax = WantMax;
		SfxConcurrencyQuietest = bWantQuietest;
	}
	return SfxConcurrency;
}

bool UAsdaSoundSubsystem::IsBeyondHearing(UWorld* World, const FVector& Location, float MaxDist) const
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) { return false; }
	// Затухание UE считает от точки-переопределения слушателя (asda.Sound.ListenerAtPlayer — игрок),
	//   иначе от камеры; отсекаем по той же точке, что и кривая.
	FVector Ear;
	if (!PC->GetAudioListenerAttenuationOverridePosition(Ear))
	{
		FVector Front, Right;
		PC->GetAudioListenerPosition(Ear, Front, Right);
	}
	return FVector::DistSquared(Ear, Location) > FMath::Square(MaxDist);
}

// ★ ПРЕДОБЪЯВЛЕНИЕ (добавлено 2026-09-17 волной «окна 4200/8000»). `AsdaSfxVolumeMul()`
//   ОПРЕДЕЛЕНА НИЖЕ по файлу (рядом с CVar `asda.Sound.SfxVolume`), а зовут её ТРИ точки запуска
//   звука ВЫШЕ определения — модуль не собирался целиком: `error C3861: 'AsdaSfxVolumeMul':
//   identifier not found` (строки 197/206/228). Это чисто порядок объявлений: ни поведение, ни
//   само определение не тронуты. Если владелец блока перенесёт определение выше — предобъявление
//   можно снять.
static FORCEINLINE float AsdaSfxVolumeMul();

// ★ 06.11.2026: журнал запросов 2D-звука для самотестов (под `-nosound` звук не звучит, проверяется сам запрос: имя найдено и
//   дошло до запуска). Пишется только между `AsdaSoundTapStart` и `AsdaSoundTapStop`.
static bool GAsdaSoundTapOn = false;
static TArray<FName> GAsdaSoundTap;   // имена запрошенных звуков по порядку
// Начать запись журнала (прежняя запись стирается).
ASDAREBORN_API void AsdaSoundTapStart() { GAsdaSoundTap.Reset(); GAsdaSoundTapOn = true; }
// Закончить запись и отдать журнал.
ASDAREBORN_API TArray<FName> AsdaSoundTapStop() { GAsdaSoundTapOn = false; return GAsdaSoundTap; }

// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] ОДНОРАЗОВЫЕ 3D-ЗВУКИ ТОЖЕ СЛЕДУЮТ ПОЛЗУНКУ НА ЛЕТУ. Эталон: громкость эффектов FMOD3 —
//   общая громкость каналов эффектов, звучащие каналы меняются сразу. У порта громкость одноразового 3D-звука бралась
//   в момент запуска, и «заглушить звук» не доставал звуки, уже звучащие (крик, взрыв навыка — до нескольких секунд).
//   Теперь компонент одноразового 3D-звука тоже попадает в список, который переставляет `RefreshSfxLoopVolumes`
//   (слабые ссылки; отыгравшие вычищаются при наполнении). 0 — прежнее поведение (только петли).
static int32 GAsdaSfxLiveOneShots = 1;
static FAutoConsoleVariableRef CVarAsdaSfxLiveOneShots(
	TEXT("asda.Sound.SfxLiveOneShots"),
	GAsdaSfxLiveOneShots,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] Полоса «Звук» и «заглушить» меняют громкость уже звучащих одноразовых 3D-звуков. "
	     "1 = да (умолчание), 0 = прежнее поведение (громкость на момент запуска)."),
	ECVF_Default);
// Порог наполнения списка, после которого отыгравшие одноразовые звуки вычищаются (чтобы список не рос весь сеанс).
static constexpr int32 kAsdaSfxTrackCompactAt = 256;

// ★ 22.10.2026: глубина глушения звуков интерфейса (`FUiSoundMute`, объявление в заголовке).
int32 UAsdaSoundSubsystem::UiSoundMuteDepth = 0;

// ★★★ 08.11.2026 [ОРИГИНАЛ] ФЛАГ ПЕТЛИ ВЫЗОВА (пачка 18, баг 3: «звук захвата базы в Битве армий играет один раз»).
//   Эталон `CSound_LoadSample@0xFA2580` (AsdaStory_game.c:589384): флаг вызова 1 → `FSOUND_Sample_SetMode(pSample, 2)`
//   (FSOUND_LOOP_NORMAL), 0 → `…, 1` (LOOP_OFF). Флаг 1 у оригинала ровно в 4 звуках (сверка с бинарём —
//   Reports/batch18/sound_loop/REPORT.md): `S_c_get_motion` (6724 sub 1), `S_e_pr_gc_idle_00` (гача, 7 мест),
//   `s_item_boosting01` (окно бустера), `S_e_disjoint_process` (разборка). Порт передавал `bLoop`, но зацикливание
//   отдавал ассету, а у всех волн эффектов `bLooping = False` — петля звучала один раз. Теперь волна без собственной
//   петли при `bLoop` играет через рантайм-обёртку `USoundCue` → Wave Player (`bLooping`) — бесшовная петля движка,
//   как LOOP_NORMAL у FMOD. Музыка и запечённые эмиттеры (`bLooping = True` в ассете) идут как прежде.
static int32 GAsdaSoundLoopFlag = 1;
static FAutoConsoleVariableRef CVarAsdaSoundLoopFlag(
	TEXT("asda.Sound.LoopFlag"),
	GAsdaSoundLoopFlag,
	TEXT("[ОРИГИНАЛ 08.11.2026] Флаг петли вызова звука (FMOD LOOP_NORMAL): звук с bLoop повторяется, пока его не остановят. "
	     "1 = по эталону (умолчание), 0 = прежнее поведение порта (петля только у ассетов с bLooping — звук эффекта играл один раз)."),
	ECVF_Default);

// Журнал петель для самотеста: «старт/повтор №N/стоп <имя>». Пишется только между Start и Stop.
static bool GAsdaSoundLoopJournalOn = false;
static TArray<FString> GAsdaSoundLoopJournal;   // строки журнала по порядку
ASDAREBORN_API void AsdaSoundLoopJournalStart() { GAsdaSoundLoopJournal.Reset(); GAsdaSoundLoopJournalOn = true; }
ASDAREBORN_API TArray<FString> AsdaSoundLoopJournalStop() { GAsdaSoundLoopJournalOn = false; return GAsdaSoundLoopJournal; }
ASDAREBORN_API TArray<FString> AsdaSoundLoopJournalPeek() { return GAsdaSoundLoopJournal; }
// Запись строки журнала петли: всегда в лог (Log), в журнал самотеста — пока он включён.
static void AsdaLoopNote(const FString& Line)
{
	UE_LOG(LogAsdaSound, Log, TEXT("[петля] %s"), *Line);
	if (GAsdaSoundLoopJournalOn) { GAsdaSoundLoopJournal.Add(Line); }
}

USoundBase* UAsdaSoundSubsystem::LoopVariant(USoundBase* Snd)
{
	if (!Snd || GAsdaSoundLoopFlag == 0 || Snd->IsLooping()) { return Snd; }   // уже петля (музыка, эмиттеры карты) — как есть
	USoundWave* Wave = Cast<USoundWave>(Snd);
	if (!Wave) { return Snd; }   // звуков-цепочек у порта нет: все эффекты — волны (Content/AsdaAssets/Sound)
	if (TObjectPtr<USoundCue>* Found = LoopCues.Find(Wave)) { return Found->Get(); }
	USoundCue* Cue = NewObject<USoundCue>(this, NAME_None, RF_Transient);
	USoundNodeWavePlayer* Player = NewObject<USoundNodeWavePlayer>(Cue, NAME_None, RF_Transient);
	Player->SetSoundWave(Wave);
	Player->bLooping = true;   // FSOUND_LOOP_NORMAL
	Cue->FirstNode = Player;
	// Свойства волны переходят на обёртку без изменений: класс, затухание, лимиты, приоритет. Громкость цепочки 1.0
	//   (у `USoundCue` по умолчанию 0.75 — петля стала бы тише того же звука одноразово).
	Cue->SoundClassObject = Wave->SoundClassObject;
	Cue->AttenuationSettings = Wave->AttenuationSettings;
	Cue->ConcurrencySet = Wave->ConcurrencySet;
	Cue->Priority = Wave->Priority;
	Cue->VolumeMultiplier = 1.f;
	Cue->PitchMultiplier = 1.f;
	Cue->CacheAggregateValues();
	Cue->Duration = INDEFINITELY_LOOPING_DURATION;   // вне редактора длительность не пересчитывается: петля = «бесконечно»
	LoopCues.Add(Wave, Cue);
	return Cue;
}

void UAsdaSoundSubsystem::TrackLoop(UAudioComponent* AC, FName Name)
{
	if (!AC) { return; }
	// Компоненты, ушедшие вместе с миром без «конца проигрыша», вычищаются здесь (слабые ключи).
	for (auto It = LoopTracks.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	LoopTracks.Add(TWeakObjectPtr<UAudioComponent>(AC), FLoopTrack{ Name, 0 });
	AC->OnAudioPlaybackPercentNative.AddUObject(this, &UAsdaSoundSubsystem::OnLoopPercent);
	AC->OnAudioFinishedNative.AddUObject(this, &UAsdaSoundSubsystem::OnLoopFinished);
	// Длина круга — длительность самой волны (у обёртки длительность «бесконечно»).
	const TObjectPtr<USoundWave>* WaveOf = Cast<USoundCue>(AC->Sound) ? LoopCues.FindKey(Cast<USoundCue>(AC->Sound)) : nullptr;
	const float Len = WaveOf ? WaveOf->Get()->Duration : (AC->Sound ? AC->Sound->Duration : 0.f);
	AsdaLoopNote(FString::Printf(TEXT("старт %s (круг %.2f с%s)"), *Name.ToString(), Len,
		(AC->Sound && AC->Sound->IsLooping()) ? TEXT(", петля") : TEXT(", БЕЗ петли")));
}

void UAsdaSoundSubsystem::OnLoopPercent(const UAudioComponent* AC, const USoundWave* Wave, const float Pct)
{
	// Процент у петли растёт за 1.0 (кадры от старта / кадры волны) — целая часть = пройденные круги.
	FLoopTrack* T = LoopTracks.Find(TWeakObjectPtr<UAudioComponent>(const_cast<UAudioComponent*>(AC)));
	if (!T) { return; }
	// Голос микшера до инициализации отдаёт прежний процент своего слота (`FMixerSource::GetPlaybackPercent`) — у нового звука
	//   первый отчёт бывает «66». За один отчёт (кадр) петля проходит меньше круга: скачок больше круга — не наш, пропуск.
	if (Pct > (float)T->Laps + 1.5f) { return; }
	const int32 Laps = FMath::FloorToInt(Pct);
	while (T->Laps < Laps) { ++T->Laps; AsdaLoopNote(FString::Printf(TEXT("повтор %s №%d"), *T->Name.ToString(), T->Laps)); }
}

void UAsdaSoundSubsystem::OnLoopFinished(UAudioComponent* AC)
{
	FLoopTrack T;
	if (!LoopTracks.RemoveAndCopyValue(TWeakObjectPtr<UAudioComponent>(AC), T)) { return; }
	AsdaLoopNote(FString::Printf(TEXT("стоп %s (кругов %d)"), *T.Name.ToString(), T.Laps));
}

void UAsdaSoundSubsystem::PlaySound2D(FName Name, bool bLoop)
{
	if (!bLoop && UiSoundMuteDepth > 0) { return; }   // ★ 22.10.2026: служебное переоткрытие окон после травела — молча
	USoundBase* Snd = ResolveSound(Name);
	if (!Snd)
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlaySound2D: не найден звук '%s'"), *Name.ToString());
		return;
	}
	if (GAsdaSoundTapOn) { GAsdaSoundTap.Add(Name); }   // ★ 06.11.2026: журнал самотеста
	StoredSoundName = Name;   // storedSoundId — запомнить для рестарта (эталон CSound stored)
	UE_LOG(LogAsdaSound, Verbose, TEXT("ЗВУК 2D %s%s"), *Name.ToString(), bLoop ? TEXT(" петля") : TEXT(""));   // ★ 30.10: для самотеста Sound3D
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) return;
	if (bLoop)
	{
		// Луп 2D требует управляемый компонент (PlaySound2D — fire-and-forget). Спавним, держим от авто-дестроя.
		//   ★ 08.11.2026 [ОРИГИНАЛ]: зацикливание — флаг ВЫЗОВА (FMOD LOOP_NORMAL), а не ассета: `LoopVariant` (asda.Sound.LoopFlag).
		if (UAudioComponent* AC = UGameplayStatics::SpawnSound2D(World, LoopVariant(Snd), AsdaSfxVolumeMul(), 1.f, 0.f, nullptr, false, false))
		{
			AC->bAutoDestroy = false;
			TrackLoop(AC, Name);   // журнал до старта: подписка на процент действует с `Play()`
			AC->Play();
			// Петля живёт долго — запоминаем, чтобы ползунок «Звук» дотягивался и до неё.
			SfxLoops.Add(AC);
		}
	}
	else
	{
		// Одноразовый 2D делит пул голосов с 3D (у FMOD3 пул общий); петли управляются вызывающим — вне пула.
		UGameplayStatics::PlaySound2D(World, Snd, AsdaSfxVolumeMul(), 1.f, 0.f, GetSfxConcurrency());
	}
}

// ★ [ПОРТ] 06.10.2026 (волна 2-А): та же ветка петли, что у `PlaySound2D(…, true)`, но компонент отдаётся вызывающему.
UAudioComponent* UAsdaSoundSubsystem::PlaySound2DManaged(FName Name)
{
	USoundBase* Snd = ResolveSound(Name);
	if (!Snd)
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlaySound2DManaged: не найден звук '%s'"), *Name.ToString());
		return nullptr;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) { return nullptr; }
	// ★ 08.11.2026 [ОРИГИНАЛ]: петля — флаг вызова (`LoopVariant`, asda.Sound.LoopFlag); старт после подписки журнала.
	UAudioComponent* AC = UGameplayStatics::SpawnSound2D(World, LoopVariant(Snd), AsdaSfxVolumeMul(), 1.f, 0.f, nullptr, false, false);
	if (AC)
	{
		AC->bAutoDestroy = false;
		TrackLoop(AC, Name);
		AC->Play();
		SfxLoops.Add(AC);   // петля живёт долго — ползунок «Звук» обязан до неё дотягиваться
	}
	return AC;
}

// ★★★ 30.10.2026: прежний вход сохраняет подпись. Радиусы по умолчанию (100/10000 — эталонные дефолты `CSoundEx_ctor`) значат
//   «радиусы не заданы» — класс выводится из имени ассета; явные радиусы (окружение карты Pos.POS, хранитель) — как заданы.
UAudioComponent* UAsdaSoundSubsystem::PlaySound3D(FName Name, FVector Location, float MinDist,
	float MaxDist, bool bLoop)
{
	return Play3DInternal(Name, Location, EAsdaSoundClass::Auto, MinDist, MaxDist, nullptr, bLoop);
}

UAudioComponent* UAsdaSoundSubsystem::PlaySound3DAs(FName Name, FVector Location, EAsdaSoundClass Class, const AActor* Source, bool bLoop)
{
	return Play3DInternal(Name, Location, Class, 0.f, 0.f, Source, bLoop);
}

UAudioComponent* UAsdaSoundSubsystem::PlaySound3DByIdAs(int32 SoundId, FVector Location, EAsdaSoundClass Class, const AActor* Source)
{
	const FName Name = ResolveSoundName(SoundId);
	if (Name.IsNone())
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlaySound3DByIdAs: id %d нет в таблице"), SoundId);
		return nullptr;
	}
	return Play3DInternal(Name, Location, Class, 0.f, 0.f, Source, false);
}

// Явные радиусы: оба заданы, max > min и это не эталонные дефолты 100/10000 («не задано»).
static bool AsdaExplicitRadii(float MinDist, float MaxDist)
{
	return MinDist > 0.f && MaxDist > MinDist && !(FMath::IsNearlyEqual(MinDist, 100.f) && FMath::IsNearlyEqual(MaxDist, 10000.f));
}

bool UAsdaSoundSubsystem::Plan3D(FName Name, const FVector& Location, EAsdaSoundClass Class, float MinDist, float MaxDist,
	bool bLoop, FAsdaSound3DPlan& Out)
{
	Out = FAsdaSound3DPlan();
	const bool bExplicit = AsdaExplicitRadii(MinDist, MaxDist);
	EAsdaSoundClass C = Class;
	if (C == EAsdaSoundClass::Auto) { C = bExplicit ? EAsdaSoundClass::Ambient : ClassifyByName(Name); }
	if (C == EAsdaSoundClass::Ui || C == EAsdaSoundClass::Count) { C = EAsdaSoundClass::Action; }   // интерфейсное имя в мире — у автомата
	Out.Class = C;
	const FAsdaSoundClassSpec Spec = GetClassSpec(C, Name);
	const float Upm = UnitsPerMeter();
	const float Scale = FMath::Max(0.05f, GAsdaSoundDistanceScale);
	float Mn = 0.f, Mx = 0.f;
	if (GAsdaSoundClasses == 0 || bExplicit)
	{
		// Прежний единый закон или явные радиусы (данные карты): эталонная пара min/max, купол сверху.
		HearingRange(MinDist, MaxDist, Mn, Mx);
		Mx = FMath::Max(Mn + 1.f, Mx * Scale);
	}
	else
	{
		Mn = FMath::Max(1.f, Spec.InnerM * Upm);
		Mx = Spec.OuterM * Upm * Scale;
		if (GAsdaSoundHearingRadiusM > 0.f) { Mx = FMath::Min(Mx, GAsdaSoundHearingRadiusM * Upm * Scale); }   // купол владельца 19.09
		Mx = FMath::Max(Mn + 1.f, Mx);
	}
	Out.Mn = Mn;
	Out.Mx = Mx;
	Out.Priority = Spec.Priority;
	Out.MaxPerSource = (GAsdaSoundClassConcurrency != 0 && !bLoop) ? Spec.MaxPerSource : 0;
	Out.Att = GetAttenuation(Mn, Mx);
	Out.ClassConc = bLoop ? nullptr : GetClassConcurrency(C);
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	// Источник за радиусом не заводится вовсе — голос пула не тратит (петли ждёт вызывающий — их не отсекаем).
	Out.bCulled = (GAsdaSoundSilenceBeyondMax != 0) && !bLoop && IsBeyondHearing(World, Location, Mx);
	return true;
}

float UAsdaSoundSubsystem::EvalGain(const FAsdaSound3DPlan& Plan, float DistUnits)
{
	if (!Plan.Att) { return 1.f; }
	// Движковый расчёт (тот же `FBaseAttenuationSettings::Evaluate`, что зовёт аудиопоток): источник в нуле, слушатель на оси X.
	return Plan.Att->Attenuation.Evaluate(FTransform::Identity, FVector(DistUnits, 0.f, 0.f));
}

UAudioComponent* UAsdaSoundSubsystem::Play3DInternal(FName Name, const FVector& Location, EAsdaSoundClass Class, float MinDist,
	float MaxDist, const AActor* Source, bool bLoop)
{
	if (GAsdaSoundSpatial3D == 0)
	{
		return PlaySound3DLegacy(Name, Location, MinDist, MaxDist, bLoop);
	}
	USoundBase* Snd = ResolveSound(Name);
	if (!Snd)
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlaySound3D: не найден звук '%s'"), *Name.ToString());
		return nullptr;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) { return nullptr; }
	FAsdaSound3DPlan Plan;
	Plan3D(Name, Location, Class, MinDist, MaxDist, bLoop, Plan);
	// Строка запуска (Verbose) — по ней самотест `asda.SelfTest.Sound3D` сверяет РЕАЛЬНЫЙ путь мест вызова (какой звук, 2D/3D, класс).
	UE_LOG(LogAsdaSound, Verbose, TEXT("ЗВУК 3D %s класс=%s мин=%.0f макс=%.0f%s%s"), *Name.ToString(), GetClassSpec(Plan.Class).Name,
		Plan.Mn, Plan.Mx, bLoop ? TEXT(" петля") : TEXT(""), Plan.bCulled ? TEXT(" ЗА РАДИУСОМ") : TEXT(""));
	if (Plan.bCulled) { return nullptr; }
	FAudioDevice* Dev = World->GetAudioDeviceRaw();
	if (!Dev) { return nullptr; }   // звук выключен (-nosound) — как у SpawnSoundAtLocation
	// Компонент с затуханием и группами ДО старта: решение «позиционный звук» движок принимает в `PlayInternal`.
	FAudioDevice::FCreateComponentParams Params(World);
	Params.SetLocation(Location);
	Params.AttenuationSettings = Plan.Att;
	if (!bLoop)
	{
		if (USoundConcurrency* Pool = GetSfxConcurrency()) { Params.ConcurrencySet.Add(Pool); }   // пул 16 каналов оригинала
		if (Plan.ClassConc) { Params.ConcurrencySet.Add(Plan.ClassConc); }
	}
	Params.bPlay = false;
	Params.bAutoDestroy = !bLoop;
	// ★ 08.11.2026 [ОРИГИНАЛ]: петля — флаг вызова (`LoopVariant`, asda.Sound.LoopFlag), не свойство ассета.
	UAudioComponent* AC = FAudioDevice::CreateComponent(bLoop ? LoopVariant(Snd) : Snd, Params);
	if (!AC) { return nullptr; }   // движок сам отсёк короткий звук вне слышимости
	AC->SetWorldLocation(Location);
	AC->bAllowSpatialization = true;
	AC->bIsUISound = false;
	AC->bStopWhenOwnerDestroyed = false;
	AC->bAutoDestroy = !bLoop;
	AC->bOverridePriority = true;
	AC->Priority = Plan.Priority;
	AC->SetVolumeMultiplier(AsdaSfxVolumeMul());
	if (bLoop) { TrackLoop(AC, Name); }   // ★ 08.11.2026: журнал петли (старт/повтор/стоп) — подписка до старта
	AC->Play();
	// Петлю запоминаем: её громкость обязана меняться вместе с ползунком «Звук» окна настроек.
	if (bLoop) { SfxLoops.Add(AC); }
	else if (GAsdaSfxLiveOneShots != 0)   // ★ 05.11.2026: и одноразовый — ползунок достаёт его, пока звучит
	{
		if (SfxLoops.Num() >= kAsdaSfxTrackCompactAt) { SfxLoops.RemoveAllSwap([](const TWeakObjectPtr<UAudioComponent>& P) { return !P.IsValid(); }); }
		SfxLoops.Add(AC);
	}
	if (Plan.MaxPerSource > 0 && Source) { LimitPerSource(Source, Plan.Class, Plan.MaxPerSource, AC); }
	return AC;
}

// Прежний путь (`asda.Sound.Spatial3D 0`): запуск без затухания, затухание — после старта (не действует, см. разбор ключа).
UAudioComponent* UAsdaSoundSubsystem::PlaySound3DLegacy(FName Name, FVector Location, float MinDist, float MaxDist, bool bLoop)
{
	USoundBase* Snd = ResolveSound(Name);
	if (!Snd)
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlaySound3D: не найден звук '%s'"), *Name.ToString());
		return nullptr;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) return nullptr;
	// ★ Дедуп №2, В2-C (F13, 11.10.2026): радиусы и затухание — общие тела `HearingRange`/`ApplyEtalonAttenuation` (их же зовёт
	//   `Configure3D` для чужих компонентов, напр. канала транспорта). Для PlaySound3D порядок и значения прежние.
	float Mn = 0.f, Mx = 0.f;
	HearingRange(MinDist, MaxDist, Mn, Mx);
	const bool bSilence = GAsdaSoundSilenceBeyondMax != 0;
	// ★ [ПОРТ] Источник за пределом слышимости не заводим вовсе — не тратит голос пула (asda.Sound.SilenceBeyondMax).
	//   Петли не трогаем: ими управляет вызывающий и ждёт компонент обратно.
	if (bSilence && !bLoop && IsBeyondHearing(World, Location, Mx)) { return nullptr; }
	// ★ 08.11.2026 [ОРИГИНАЛ]: петля — флаг вызова (`LoopVariant`, asda.Sound.LoopFlag). Прежний путь стартует сам — журнал без «старта до подписки».
	UAudioComponent* AC = UGameplayStatics::SpawnSoundAtLocation(World, bLoop ? LoopVariant(Snd) : Snd, Location, FRotator::ZeroRotator,
		AsdaSfxVolumeMul(), 1.f, 0.f, nullptr, bLoop ? nullptr : GetSfxConcurrency());
	if (AC && bLoop) { TrackLoop(AC, Name); }
	if (AC)
	{
		// Петлю запоминаем: её громкость обязана меняться вместе с ползунком «Звук» окна настроек.
		if (bLoop) { AC->bAutoDestroy = false; SfxLoops.Add(AC); }
		else if (GAsdaSfxLiveOneShots != 0)   // ★ 05.11.2026: и одноразовый (см. `asda.Sound.SfxLiveOneShots`)
		{
			if (SfxLoops.Num() >= kAsdaSfxTrackCompactAt) { SfxLoops.RemoveAllSwap([](const TWeakObjectPtr<UAudioComponent>& P) { return !P.IsValid(); }); }
			SfxLoops.Add(AC);
		}
		ApplyEtalonAttenuation(AC, Mn, Mx);
	}
	return AC;
}

// ★ Дедуп №2, В2-C (F13): радиусы слышимости 3D-звука — тело дословно из прежнего PlaySound3D. Разбор купола — ниже в теле.
void UAsdaSoundSubsystem::HearingRange(float MinDist, float MaxDist, float& OutMn, float& OutMx)
{
	const float Mn = (MinDist > 0.f) ? MinDist : 100.f;
	float Mx = (MaxDist > Mn) ? MaxDist : 10000.f;
	// ★★★ КУПОЛ СЛЫШИМОСТИ (ключ `asda.Sound.HearingRadiusM`, по умолчанию 35 м). Ограничивает радиус
	//   СВЕРХУ: звук со своим меньшим радиусом его сохраняет. 100 единиц = 1 м (эталонный distancefactor
	//   0.01: оригинальные 100/10000 — это 1 м и 100 м).
	//   ★ Внутри купола звук НЕ меняется: в кривой inverse g(d) = Mn/(Mn + 0.7·(d−Mn)) радиус Mx
	//   сокращается при подстановке нормированной дистанции, то есть громкость на заданном расстоянии от
	//   него не зависит. Меньший купол лишь обрывает звук раньше — плавно, на последних 10 % радиуса.
	//   ★ Побочно помогает от «гула»: источник за куполом ниже не заводится вовсе и не занимает голос пула.
	if (GAsdaSoundHearingRadiusM > 0.f)
	{
		const float Dome = FMath::Max(Mn + 1.f, GAsdaSoundHearingRadiusM * UnitsPerMeter());   // ★ 30.10: ед./м — по росту персонажа
		Mx = FMath::Min(Mx, Dome);
	}
	OutMn = Mn;
	OutMx = Mx;
}

// ★ Дедуп №2, В2-C (F13): затухание 3D-звука 1:1 с FMOD3 — тело дословно из прежнего PlaySound3D (разбор ниже).
//   ★ 30.10.2026: тело стало построителем настроек (`AsdaBuildEtalonAttenuation`) — им же заполняются объекты затухания классов.
static void AsdaBuildEtalonAttenuation(FSoundAttenuationSettings& Att, float Mn, float Mx);

void UAsdaSoundSubsystem::ApplyEtalonAttenuation(UAudioComponent* AC, float Mn, float Mx)
{
	if (!AC) { return; }
	FSoundAttenuationSettings Att;
	AsdaBuildEtalonAttenuation(Att, Mn, Mx);
	AC->AdjustAttenuation(Att);   // свой компонент (транспорт) настраивается ДО `Play` — тогда движок берёт настройки при старте
}

static void AsdaBuildEtalonAttenuation(FSoundAttenuationSettings& Att, float Mn, float Mx)
{
	const bool bSilence = GAsdaSoundSilenceBeyondMax != 0;
	{
		// ★ 3D-аттенюация 1:1 с FMOD3 (эталон FSOUND_HW3D + SetMinMaxDistance + rolloff 0.7; SPEC_FMOD_ATTEN.md).
		//   Ставится БЕЗУСЛОВНО для 3D (эталон: if(mode==4096) без max>min). Дефолты min/max=100/10000 (оба ctor'а).
		//   Кривая — inverse (логарифмический rolloff FMOD3): atten(d)=minDist/(minDist+0.7·(d−minDist)).
		//   ★ ИСПРАВЛЕНО 11.09.2026: прежде кривая шла 11 равномерными ключами, а линейная интерполяция между
		//     t=0 и t=0.1 у крутой 1/d давала у источника на 6–8 дБ ГРОМЧЕ эталона (d=300: −1.7 дБ против
		//     −7.6; d=595: −5.0 против −13.0). Теперь 25 ключей со сгущением к источнику: ошибка ≤ 0.2 дБ.
		//   За maxDist: при asda.Sound.SilenceBeyondMax=1 — плавный спад к нулю на последних 10 % (исправление
		//   оригинала), при 0 — эталонный floor 100/7030 ≈ 0.0142 = −36.9 дБ (constant-экстраполяция кривой).
		{
			Att.bAttenuate = true;
			Att.AttenuationShape = EAttenuationShape::Sphere;
			Att.AttenuationShapeExtents = FVector(Mn, 0.f, 0.f);
			Att.FalloffDistance = Mx - Mn;
			Att.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
			if (FRichCurve* Curve = Att.CustomAttenuationCurve.GetRichCurve())
			{
				Curve->Reset();
				// t — нормированная дистанция (d−min)/(max−min); g — FMOD3 inverse, rolloffFactor 0.7.
				auto Inv = [Mn, Mx](float t) { return Mn / (Mn + 0.7f * t * (Mx - Mn)); };
				constexpr float kFadeFrom = 0.9f;   // с этой доли пути до maxDist начинается сведение к нулю
				constexpr int32 kInvKeys  = 24;     // ключей эталонного участка (сгущены к источнику степенью 2.2)
				constexpr int32 kTailKeys = 8;      // ключей хвоста 0.9..1.0
				for (int32 k = 0; k <= kInvKeys; ++k)
				{
					const float t = FMath::Pow((float)k / kInvKeys, 2.2f) * kFadeFrom;
					Curve->AddKey(t, Inv(t));
				}
				for (int32 i = 1; i <= kTailKeys; ++i)
				{
					const float t = kFadeFrom + (1.f - kFadeFrom) * i / kTailKeys;
					float g = Inv(t);
					if (bSilence)
					{
						const float u = (t - kFadeFrom) / (1.f - kFadeFrom);
						g *= 1.f - u * u * (3.f - 2.f * u);   // smoothstep → ровно 0 на maxDist
					}
					Curve->AddKey(t, g);
				}
			}
			// ★ 30.10.2026: звук у самого слушателя (свои шаги, удары, навыки — d ≈ 0) не «прыгает» по каналам —
			//   внутри половины внутреннего радиуса он ненаправленный, к внутреннему радиусу плавно становится направленным.
			Att.bSpatialize = true;
			Att.NonSpatializedRadiusStart = Mn;
			Att.NonSpatializedRadiusEnd = Mn * 0.5f;
			// ★ 30.10.2026 (`asda.Sound.DistanceLPF`): дальние звуки глуше — срез верхов с 3 внутренних радиусов до края.
			Att.bAttenuateWithLPF = GAsdaSoundDistanceLPF != 0;
			Att.LPFRadiusMin = FMath::Min(Mn * 3.f, Mx * 0.5f);
			Att.LPFRadiusMax = Mx;
			Att.LPFFrequencyAtMin = 20000.f;
			Att.LPFFrequencyAtMax = 6000.f;
		}
	}
}

// ★ 30.10.2026: объект затухания для пары радиусов (и текущих режимов). Движок копирует настройки в звук при старте, поэтому
//   один объект делят все звуки с теми же радиусами; смена ключей даёт новый ключ кэша — новый объект.
USoundAttenuation* UAsdaSoundSubsystem::GetAttenuation(float Mn, float Mx)
{
	const FString Key = FString::Printf(TEXT("%d|%d|%d|%d"), FMath::RoundToInt(Mn), FMath::RoundToInt(Mx),
		GAsdaSoundSilenceBeyondMax != 0 ? 1 : 0, GAsdaSoundDistanceLPF != 0 ? 1 : 0);
	if (TObjectPtr<USoundAttenuation>* Found = AttCache.Find(Key)) { return Found->Get(); }
	USoundAttenuation* A = NewObject<USoundAttenuation>(this);
	AsdaBuildEtalonAttenuation(A->Attenuation, Mn, Mx);
	AttCache.Add(Key, A);
	return A;
}

// ★ 30.10.2026: группа класса — не больше `MaxPerClass` голосов класса на сцену; при переполнении гаснет САМЫЙ ДАЛЬНИЙ
//   (при равенстве — самый старый): в толпе слышны ближние удары, а не случайные. «Вес концентрации» — множитель громкости
//   прежних голосов группы при каждом новом. Движок читает настройки при создании группы — при смене ключа объект новый.
USoundConcurrency* UAsdaSoundSubsystem::GetClassConcurrency(EAsdaSoundClass Class)
{
	if (GAsdaSoundClassConcurrency == 0) { return nullptr; }
	const int32 Idx = (int32)Class;
	if (Idx <= 0 || Idx >= (int32)EAsdaSoundClass::Count) { return nullptr; }
	const FAsdaSoundClassSpec Spec = GetClassSpec(Class);
	if (Spec.MaxPerClass <= 0) { return nullptr; }
	if (ClassConc.Num() < (int32)EAsdaSoundClass::Count)
	{
		ClassConc.SetNum((int32)EAsdaSoundClass::Count);
		ClassConcSig.Init(0, (int32)EAsdaSoundClass::Count);
	}
	const int32 Sig = Spec.MaxPerClass * 1000 + FMath::RoundToInt(Spec.ConcVolumeScale * 100.f);
	if (!ClassConc[Idx] || ClassConcSig[Idx] != Sig)
	{
		USoundConcurrency* C = NewObject<USoundConcurrency>(this);
		C->Concurrency.MaxCount = Spec.MaxPerClass;
		C->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
		// `VolumeScale` в UE 5.8 приватный (геттер есть, сеттера нет) — пишем через отражение свойства структуры.
		if (FFloatProperty* P = FindFProperty<FFloatProperty>(FSoundConcurrencySettings::StaticStruct(), TEXT("VolumeScale")))
		{
			P->SetPropertyValue_InContainer(&C->Concurrency, Spec.ConcVolumeScale);
		}
		C->Concurrency.VolumeScaleMode = EConcurrencyVolumeScaleMode::Distance;   // дальние голоса класса — тише ближних
		ClassConc[Idx] = C;
		ClassConcSig[Idx] = Sig;
	}
	return ClassConc[Idx];
}

// ★ 30.10.2026: не больше `Max` звучащих голосов класса у одного источника (моб в серии ударов, персонаж в беге): лишний —
//   самый старый — гаснет с коротким спадом. Ключ — адрес источника и класс; мёртвые/доигравшие ссылки чистятся тут же.
void UAsdaSoundSubsystem::LimitPerSource(const AActor* Source, EAsdaSoundClass Class, int32 Max, UAudioComponent* NewAC)
{
	if (!Source || Max <= 0 || !NewAC) { return; }
	const uint64 Key = (static_cast<uint64>(reinterpret_cast<UPTRINT>(Source)) << 8) | static_cast<uint64>(Class);
	TArray<TWeakObjectPtr<UAudioComponent>>& List = SourceVoices.FindOrAdd(Key);
	List.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& W) { const UAudioComponent* A = W.Get(); return !A || !IsValid(A) || !A->IsPlaying(); });
	while (List.Num() >= Max)
	{
		if (UAudioComponent* Old = List[0].Get()) { Old->FadeOut(0.05f, 0.f); }
		List.RemoveAt(0);
	}
	List.Add(NewAC);
	// Редкая уборка ключей источников, которых больше нет (карта не растёт бесконечно).
	if (SourceVoices.Num() > 512)
	{
		for (auto It = SourceVoices.CreateIterator(); It; ++It)
		{
			It->Value.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& W) { const UAudioComponent* A = W.Get(); return !A || !IsValid(A) || !A->IsPlaying(); });
			if (It->Value.Num() == 0) { It.RemoveCurrent(); }
		}
	}
}

// ★ 30.10.2026: свой компонент (канал транспорта) — затухание КЛАССА объектом, поставленным ДО `Play` (как у `Play3DInternal`).
bool UAsdaSoundSubsystem::Configure3DAs(UAudioComponent* AC, const FVector& Location, EAsdaSoundClass Class)
{
	if (!AC) { return false; }
	if (GAsdaSoundSpatial3D == 0) { return Configure3D(AC, Location); }   // прежний путь
	FAsdaSound3DPlan Plan;
	Plan3D(AC->Sound ? AC->Sound->GetFName() : NAME_None, Location, Class, 0.f, 0.f, /*bLoop*/false, Plan);
	AC->bAllowSpatialization = true;
	AC->bOverrideAttenuation = false;   // настройки — из объекта, не из переопределения
	AC->AttenuationSettings = Plan.Att;
	AC->bOverridePriority = true;
	AC->Priority = Plan.Priority;
	return !Plan.bCulled;
}

// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F13): 3D-настройка СВОЕГО компонента тем же законом, что `PlaySound3D`.
bool UAsdaSoundSubsystem::Configure3D(UAudioComponent* AC, const FVector& Location, float MinDist, float MaxDist) const
{
	if (!AC) { return false; }
	float Mn = 0.f, Mx = 0.f;
	HearingRange(MinDist, MaxDist, Mn, Mx);
	AC->bAllowSpatialization = true;
	ApplyEtalonAttenuation(AC, Mn, Mx);
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	// Как у `PlaySound3D` (не петля): за куполом при `asda.Sound.SilenceBeyondMax 1` источник не заводится.
	return !(GAsdaSoundSilenceBeyondMax != 0 && IsBeyondHearing(World, Location, Mx));
}

// ★ ГРОМКОСТЬ ФОНОВОЙ МУЗЫКИ (заявка владельца 24.08.2026). Прежде трек играл на полной
//   громкости и заглушал всё остальное — шагов и мелких звуков было не слышно. Значение
//   меняется на лету: следующая смена трека подхватит его, а текущий поправится сразу.
static float GAsdaMusicVolume = 0.7f;
static FAutoConsoleVariableRef CVarAsdaMusicVolume(
	TEXT("asda.Sound.MusicVolume"),
	GAsdaMusicVolume,
	TEXT("Громкость фоновой музыки, 0..1 (по умолчанию 0.7). Меняется на лету."),
	FConsoleVariableDelegate::CreateStatic(&UAsdaSoundSubsystem::OnMusicVolumeChanged),
	ECVF_Default);

// ★★★ ДВА КЛЮЧА ДЛЯ ОКНА НАСТРОЕК `SYSTEM_Option_NEW` (69500), панель «Звук» 70010 (17.09.2026).
//   ЭТАЛОН. Панель звука держит ЧЕТЫРЕ значения и раздаёт их звуковому менеджеру:
//     • чекбокс 70012 (`filler[1128]`) → `CSoundMgr::GetMusicDevice()` vtbl+8 — ЗАГЛУШИТЬ МУЗЫКУ
//       (★ в буфере лежит ИНВЕРСИЯ галочки: `filler[1128] = (a3 == 0)`);
//     • полоса  70013 (`+562`, диапазон 0…256) → тот же девайс vtbl+4 — ГРОМКОСТЬ МУЗЫКИ;
//     • чекбокс 70015 (`filler[1129]`) → `sub_92CBBE(g_pSoundMgr, …)` — ЗАГЛУШИТЬ ЭФФЕКТЫ;
//     • полоса  70016 (`+563`, диапазон 0…256) → `CSoundMgr_SetReverb_wrap` — ГРОМКОСТЬ ЭФФЕКТОВ
//       (имя «Reverb» — очередной мисномер IDA: подпись контрола — loc 30522 «Звук»).
//   Громкость музыки у порта уже была (`asda.Sound.MusicVolume`), а «музыка вкл/выкл» и громкость
//   эффектов существовать были обязаны — иначе две из четырёх строк панели не действовали бы.
//   Значения 0…256 окна переводятся в 0…1 самим окном настроек.
static int32 GAsdaMusicEnable = 1;
static FAutoConsoleVariableRef CVarAsdaMusicEnable(
	TEXT("asda.Sound.MusicEnable"),
	GAsdaMusicEnable,
	TEXT("Фоновая музыка вкл/выкл (порт чекбокса 70012 окна настроек). 0 = тишина, 1 = играет."),
	FConsoleVariableDelegate::CreateStatic(&UAsdaSoundSubsystem::OnMusicVolumeChanged),
	ECVF_Default);

static float GAsdaSfxVolume = 1.0f;
// Реакция на смену ключа эффектов — переставить громкость звучащих петель (определение ниже).
static void AsdaOnSfxVolumeChanged(IConsoleVariable* Var);
static FAutoConsoleVariableRef CVarAsdaSfxVolume(
	TEXT("asda.Sound.SfxVolume"),
	GAsdaSfxVolume,
	TEXT("Громкость звуковых эффектов, 0..1 (порт полосы 70016 окна настроек; 0 = эффекты заглушены чекбоксом 70015)."),
	FConsoleVariableDelegate::CreateStatic(&AsdaOnSfxVolumeChanged),
	ECVF_Default);

// Множитель громкости SFX для трёх точек запуска ниже. Отдельная функция, чтобы значение
//   бралось ОДИН раз и одинаково в 2D-петле, 2D-однократном и 3D.
static FORCEINLINE float AsdaSfxVolumeMul()
{
	return FMath::Clamp(GAsdaSfxVolume, 0.f, 1.f);
}

// Поправить громкость уже играющего трека, чтобы настройка действовала сразу, а не со следующей карты.
void UAsdaSoundSubsystem::OnMusicVolumeChanged(IConsoleVariable* /*Var*/)
{
	const float Vol = (GAsdaMusicEnable != 0) ? FMath::Clamp(GAsdaMusicVolume, 0.f, 1.f) : 0.f;
	for (TObjectIterator<UAsdaSoundSubsystem> It; It; ++It)
	{
		if (It->MusicComp && IsValid(It->MusicComp))
		{
			It->MusicComp->SetVolumeMultiplier(Vol);
			// Трек, запущенный немым (музыка была выключена или громкость 0), движок мог остановить
			//   как неслышимый. Игрок вернул звук — трек обязан зазвучать, а не ждать смены карты.
			if (Vol > 0.f && !It->MusicComp->IsPlaying() && !It->CurrentMusicTrack.IsNone() && !It->bMusicQueued)
			{
				It->MusicComp->Play();
			}
		}
	}
}

// Громкость эффектов поменяли из консоли (asda.Sound.SfxVolume) — поправить звучащие петли.
static void AsdaOnSfxVolumeChanged(IConsoleVariable* /*Var*/)
{
	for (TObjectIterator<UAsdaSoundSubsystem> It; It; ++It)
	{
		It->RefreshSfxLoopVolumes();
	}
}

// ★★★ 05.11.2026 [ИСПРАВЛЕНИЕ] ЗВУКИ, ЗАПЕЧЁННЫЕ В УРОВЕНЬ, — ПОД ПОЛЗУНКОМ «ЗВУК» (баг игроков: «гул дирижабля в Альпене
//   громкий даже при звуке 0 / выключенном звуке»).
//   ЭТАЛОН. Эмиттеры карты (Pos.POS секция E: на alpen 3, на flammio 5, все `S_OBJ_PROPEL1.WAV`) играют через общий 3D-путь
//   менеджера: `mapObjUpdateEmittersAndSounds@0xB5C480` / `mapObj_updateAttachedEmitters@0xB5D3B0` → `sound3D_playDefaultPos@0xFA5970`
//   → `sound3DCreatePlayName3D@0xFA2F90`, громкость канала — громкость эффектов `CSoundMgr.sfxDevice+0x18` (полоса 70016,
//   чекбокс 70015 «заглушить эффекты»). Отдельного ползунка «окружение» у оригинала нет — эмиттеры карты — это ЭФФЕКТЫ
//   (разбор `Reports/owner_0927/gfx_w1/F_sound.md` §1–2).
//   ПРИЧИНА. Порт запёк эмиттеры в уровни акторами `AAmbientSound` (`Tools/asda_place_sounds.py`, подписи `MapSound_*`,
//   auto_activate, петля). Движок заводит их сам при загрузке уровня — мимо менеджера, поэтому ни ползунок, ни «заглушить»
//   до них не доходили (громкость эффектов у порта — множитель на компоненте, класса звука нет).
//   ИСПРАВЛЕНИЕ. Каждому `AAmbientSound` игрового мира ставится действующий множитель эффектов: (1) сразу после инициализации
//   мира — ДО того, как движок активирует авто-компоненты (громкого кадра нет); (2) после инициализации акторов и при
//   подгрузке уровня; (3) при каждой смене громкости эффектов. Запечённый BGM-актор («MapBGM») этим путём тоже глушится,
//   но его и так сносит `UAsdaZoneSubsystem::SilenceLegacyLevelBgm`.
//   Ключ `asda.Sound.LevelAmbientSlider`: 1 — под ползунком (умолчание), 0 — прежнее поведение (множитель 1, мимо настроек).
static int32 GAsdaLevelAmbientSlider = 1;
static FAutoConsoleVariableRef CVarAsdaLevelAmbientSlider(
	TEXT("asda.Sound.LevelAmbientSlider"),
	GAsdaLevelAmbientSlider,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] Звуки, запечённые в уровень (AmbientSound: пропеллеры alpen/flammio), подчиняются громкости "
	     "эффектов и «заглушить эффекты» окна настроек. 1 = да (умолчание), 0 = прежнее поведение (всегда полная громкость)."),
	FConsoleVariableDelegate::CreateStatic(&AsdaOnSfxVolumeChanged),
	ECVF_Default);

// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] ЗВУК НАЖАТИЯ КНОПОК SLATE (стиль кнопки `PressedSlateSound`/`HoveredSlateSound`: окна входа,
//   выбора сервера и персонажа — `S_e_button09`) играет движковое устройство Slate напрямую, мимо менеджера и ползунка.
//   У оригинала щелчок кнопки — 2D-эффект менеджера (`CSoundFlag_PlaySound2D`) под громкостью эффектов. Порт ставит своё
//   устройство звука Slate, которое повторяет движковое (`FSlateSoundDevice::PlaySound`) и добавляет громкость эффектов.
//   Ключ `asda.Sound.SlateSoundSlider`: 1 — под ползунком (умолчание), 0 — прежнее поведение (полная громкость).
static int32 GAsdaSlateSoundSlider = 1;
static FAutoConsoleVariableRef CVarAsdaSlateSoundSlider(
	TEXT("asda.Sound.SlateSoundSlider"),
	GAsdaSlateSoundSlider,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] Звук нажатия/наведения кнопок Slate (окна входа) подчиняется громкости эффектов. "
	     "1 = да (умолчание), 0 = прежнее поведение (полная громкость)."),
	ECVF_Default);

// ★★ 05.11.2026 [ИСПРАВЛЕНИЕ] КРИВАЯ ЗАТУХАНИЯ ЗАПЕЧЁННЫХ ЭМИТТЕРОВ — ЭТАЛОННАЯ (пропеллеры были на 17–31 дБ громче оригинала).
//   ЭТАЛОН. Эмиттер карты играет `sound3D_playDefaultPos@0xFA5970` → `sound3DCreatePlayName3D@0xFA2F90` (`audio.c:1160-1161`
//   пишет min/max записи Pos.POS `+0x110/+0x114` в `Sound3DSettings+0x1C/+0x20`) → `CSound_LoadSample@0xFA2580`
//   (`FSOUND_Sample_SetMinMaxDistance`); затухание FMOD3 — inverse с rolloff 0.7: g = min / (min + 0.7·(d − min)), 1 внутри min
//   (`fmod.dll` 0x10013F74, разбор `F_sound.md` §3.2). Данные: alpen 10/3000 + 2 × 10/2000, flammio 5 × 10/2000 (ед. карты = см UE).
//   ПРИЧИНА. `Tools/asda_place_sounds.py` запёк акторам ЛИНЕЙНЫЙ спад движка (сфера min, спад до max): на 1 / 5 / 10 м
//   −0.4 / −2.5 / −6.0 дБ против эталонных −17.3 / −31.0 / −36.9 дБ.
//   ИСПРАВЛЕНИЕ. Тем же построителем, что у всех 3D-звуков менеджера (`AsdaBuildEtalonAttenuation`: inverse 0.7, у края —
//   плавно в 0 при `asda.Sound.SilenceBeyondMax 1`), с радиусами самого актора (min = радиус сферы, max = min + спад — данные
//   Pos.POS, которые запёк инструмент), max не выше купола `asda.Sound.HearingRadiusM`. Ставится до активации вместе с громкостью.
//   «Липкие» min/max оригинала (после первого пропеллера все 3D-звуки до выхода играют с 10/2000 — `F_sound.md` §1) — дефект,
//   НЕ повторяется: радиусы эмиттера касаются только его самого.
//   Ключ `asda.Sound.LevelAmbientEtalonCurve`: 1 — эталонная кривая (умолчание), 0 — прежний линейный спад инструмента.
static int32 GAsdaLevelAmbientEtalonCurve = 1;
// Реакция на смену ключа — перестроить затухание эмиттеров текущих миров (определение ниже).
static void AsdaOnLevelAmbientCurveChanged(IConsoleVariable* Var);
static FAutoConsoleVariableRef CVarAsdaLevelAmbientEtalonCurve(
	TEXT("asda.Sound.LevelAmbientEtalonCurve"),
	GAsdaLevelAmbientEtalonCurve,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] Звуки, запечённые в уровень (пропеллеры alpen/flammio), затухают по эталонной кривой FMOD3 "
	     "(inverse, rolloff 0.7, радиусы Pos.POS). 1 = да (умолчание), 0 = прежний линейный спад (на 17–31 дБ громче оригинала)."),
	FConsoleVariableDelegate::CreateStatic(&AsdaOnLevelAmbientCurveChanged),
	ECVF_Default);

// Радиусы эмиттера из его затухания: min = радиус сферы, max = min + спад (так их запёк `asda_place_sounds.py`; эталонный
//   построитель оставляет ту же пару — повторный вызов даёт те же радиусы).
static void AsdaLevelAmbientRadii(const UAudioComponent* AC, float& OutMn, float& OutMx)
{
	const FSoundAttenuationSettings& A = AC->AttenuationOverrides;
	OutMn = FMath::Max(1.f, (float)A.AttenuationShapeExtents.X);
	OutMx = OutMn + FMath::Max(1.f, A.FalloffDistance);
}

// Затухание одного эмиттера уровня: эталонное (ключ 1) или прежнее линейное инструмента (ключ 0). Только эмиттеры со своим
//   затуханием (как их ставит инструмент): чужие настройки (объект затухания у ассета) не трогаем.
static void AsdaApplyLevelAmbientCurve(UAudioComponent* AC)
{
	if (!AC || !AC->bOverrideAttenuation || !AC->AttenuationOverrides.bAttenuate) { return; }
	float Mn = 0.f, Mx = 0.f;
	AsdaLevelAmbientRadii(AC, Mn, Mx);
	FSoundAttenuationSettings Att;
	if (GAsdaLevelAmbientEtalonCurve != 0)
	{
		if (GAsdaSoundHearingRadiusM > 0.f) { Mx = FMath::Max(Mn + 1.f, FMath::Min(Mx, GAsdaSoundHearingRadiusM * UAsdaSoundSubsystem::UnitsPerMeter())); }
		AsdaBuildEtalonAttenuation(Att, Mn, Mx);
	}
	else
	{
		// Дословно то, что ставит `asda_place_sounds.py`: умолчания движка + сфера min и линейный спад до max.
		Att.bAttenuate = true;
		Att.AttenuationShape = EAttenuationShape::Sphere;
		Att.AttenuationShapeExtents = FVector(Mn, 0.f, 0.f);
		Att.FalloffDistance = Mx - Mn;
	}
	AC->AdjustAttenuation(Att);   // до активации — движок возьмёт при старте; у звучащего — переставит на лету
}

// Действующий множитель звука, запечённого в уровень: громкость эффектов при включённом ключе, иначе прежняя 1.
static float AsdaLevelAmbientMul()
{
	return (GAsdaLevelAmbientSlider != 0) ? AsdaSfxVolumeMul() : 1.f;
}

// ★★★ 05.11.2026 [ОРИГИНАЛ] ЗАПУСК ЭМИТТЕРА КАРТЫ ПО РАДИУСУ КАМЕРЫ И ДОИГРЫШ ПОСЛЕ ВЫХОДА (пропеллеры alpen/flammio).
//   ЭТАЛОН (`mapObjUpdateEmittersAndSounds@0xB5C480`, world_map.c:3911-3934, каждый кадр рендера мира):
//     • условие запуска `sub_B5D2F0@0xB5D2F0` (math_geom.c:1283): |камера − позиция записи|² < поле записи `+0x118`; камера —
//       `CRenderer_CaptureViewProj` (точка глаза); фрустум для звука не проверяется. У всех 8 записей `+0x118` = max²
//       (alpen 3000² и 2 × 2000², flammio 5 × 2000²) — радиус запуска = max, его порт берёт из радиусов актора;
//     • флаг «звучит» (`+0x120` бит 0) стоит → `sub_92C461` (звучит ли id) → нет — флаг снять, на следующем кадре запуск заново;
//     • флага нет → `sound3D_playDefaultPos(…, min, max, loop = 0)` — звук ОДНОРАЗОВЫЙ (`s_obj_propel1.wav` 6,37 с) и флаг;
//     • вне радиуса запись не трогается: звучащий проход ДОИГРЫВАЕТ до конца (≤ 6,4 с) и больше не запускается.
//   ГЕЙТ «ПРОРИСОВКА ТРАВЫ» (`GRASS RENDER`): `mapObjRenderWithEffects@0xB5E590` (world_map.c:4271) зовёт эту функцию только при
//     `byte_15CB4D7 == 1` (мисномер IDA `g_bShadowCleanupNeeded` = `config+119`, поле окна настроек `+1138`, контрол
//     `apResCtrl9[8]` = 3-й флажок панели 3D 70106, `optionsWndLoadSettingsToControls@0xED1C50` game_main.c:11958/12018);
//     иначе — `mapObj_updateAttachedEmitters@0xB5D3B0`, которому нужны флаги записи &2|&4, а у всех 8 записей флаги 0 —
//     эмиттеры молчат (звучащий проход доигрывает). В порту это флажок окна `State.D3[2]` = профиль `Render3D[2]`.
//   ПОРТ. Ассет `s_obj_propel1` зациклен (инструмент запёк петлю) — одноразовый проход с перезапуском «без паузы больше кадра»
//     порт даёт непрерывной петлёй, а доигрыш — остановкой на конце текущего прохода (длительность ассета).
//     Не повторено: обход только «активных» патчей ландшафта (`g_aMapPatchFlags & 2`, кость `+4 & 0x10`) — у порта нет патчей
//     ландшафта; радиус запуска (20–30 м) меньше зоны активных патчей вокруг камеры.
//   Ключи: `asda.Sound.MapEmitterTrigger` (1 — как в оригинале, 0 — прежняя вечная петля с автозапуском) и
//     `asda.Sound.MapEmitterGrassGate` (1 — эмиттеры молчат при выключенной «Прорисовке травы», 0 — без гейта).
static int32 GAsdaMapEmitterTrigger = 1;
static void AsdaOnMapEmitterTriggerChanged(IConsoleVariable* Var);   // смена ключа — отдать/забрать запуск у движка (ниже)
static FAutoConsoleVariableRef CVarAsdaMapEmitterTrigger(
	TEXT("asda.Sound.MapEmitterTrigger"),
	GAsdaMapEmitterTrigger,
	TEXT("[ОРИГИНАЛ 05.11.2026] Эмиттер карты (пропеллер) звучит, только пока камера ближе его радиуса (20–30 м), после выхода "
	     "доигрывает текущий проход (≤ 6,4 с). 1 = как в оригинале (умолчание), 0 = прежняя вечная петля с загрузки уровня."),
	FConsoleVariableDelegate::CreateStatic(&AsdaOnMapEmitterTriggerChanged),
	ECVF_Default);
static int32 GAsdaMapEmitterGrassGate = 1;
static FAutoConsoleVariableRef CVarAsdaMapEmitterGrassGate(
	TEXT("asda.Sound.MapEmitterGrassGate"),
	GAsdaMapEmitterGrassGate,
	TEXT("[ОРИГИНАЛ 05.11.2026] Эмиттеры карты не запускаются при выключенной «Прорисовке травы» (окно настроек, 3D №3; "
	     "эталон GRASS RENDER). 1 = как в оригинале (умолчание), 0 = без гейта."),
	ECVF_Default);

// Состояние эмиттера (аналог полей записи Pos.POS `+0x118` и `+0x120` бит 0).
struct FAsdaMapEmitter
{
	TWeakObjectPtr<UAudioComponent> AC;   // компонент запечённого актора
	float TrigSq = 0.f;                   // квадрат радиуса запуска (`+0x118`; = max²)
	bool bSounding = false;               // флаг «звучит» (`+0x120` бит 0)
	double PassStart = 0.0;               // начало текущего проигрыша, с
	double StopAt = 0.0;                  // конец доигрыша после выхода из радиуса, с (0 — не назначен)
};
static TArray<FAsdaMapEmitter> GAsdaMapEmitters;          // эмиттеры живых миров
static bool GAsdaMapEmitterHold = false;                  // самотест ведёт шаги сам — покадровый драйвер молчит
static FTSTicker::FDelegateHandle GAsdaMapEmitterTickHandle;   // покадровый драйвер

// Взять эмиттер под драйвер (до активации движком): автозапуск снять, радиус запуска = max (данные Pos.POS).
static void AsdaRegisterMapEmitter(UAudioComponent* AC)
{
	if (GAsdaMapEmitterTrigger == 0 || !AC || !AC->bOverrideAttenuation || !AC->AttenuationOverrides.bAttenuate) { return; }
	GAsdaMapEmitters.RemoveAll([](const FAsdaMapEmitter& E) { return !E.AC.IsValid(); });
	if (GAsdaMapEmitters.ContainsByPredicate([AC](const FAsdaMapEmitter& E) { return E.AC.Get() == AC; })) { return; }
	float Mn = 0.f, Mx = 0.f;
	AsdaLevelAmbientRadii(AC, Mn, Mx);
	FAsdaMapEmitter E;
	E.AC = AC;
	E.TrigSq = Mx * Mx;
	GAsdaMapEmitters.Add(E);
	AC->bAutoActivate = false;            // запуск — по радиусу камеры (драйвер), а не при загрузке уровня
	if (AC->IsActive()) { AC->Stop(); }   // подгруженный в живой мир уровень мог уже завести петлю
}

// Включена ли «Прорисовка травы» (флажок 3D №3 окна настроек, профиль `Render3D[2]`; нет профиля — умолчание оригинала TRUE).
ASDAREBORN_API bool AsdaMapEmitterGrassOn()
{
	const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
	return !P || P->Render3D.Num() <= 2 || P->Render3D[2] != 0;
}

// Один шаг драйвера (покадрово из тикера; самотест зовёт его с заданной камерой и временем).
ASDAREBORN_API void AsdaMapEmitterStep(UWorld* World, const FVector& Cam, double Now, bool bGrass)
{
	for (FAsdaMapEmitter& E : GAsdaMapEmitters)
	{
		UAudioComponent* AC = E.AC.Get();
		if (!AC || !IsValid(AC) || AC->GetWorld() != World || !AC->Sound) { continue; }
		const double Dur = FMath::Max(0.1f, AC->Sound->Duration);
		const bool bIn = FVector::DistSquared(Cam, AC->GetComponentLocation()) < E.TrigSq;
		if (bIn && (bGrass || GAsdaMapEmitterGrassGate == 0))
		{
			if (!E.bSounding)
			{
				AC->Play();   // проход с начала (эталон: `sound3D_playDefaultPos`, флаг `+0x120 |= 1`)
				E.bSounding = true;
				E.PassStart = Now;
			}
			E.StopAt = 0.0;   // в радиусе: перезапуск оригинала без паузы = петля продолжается
		}
		else if (E.bSounding && E.StopAt <= 0.0)
		{
			// Вне радиуса (или трава выключена): проход доигрывает до своего конца.
			const double Passes = FMath::Max(1.0, FMath::CeilToDouble((Now - E.PassStart) / Dur));
			E.StopAt = E.PassStart + Passes * Dur;
		}
		if (E.bSounding && E.StopAt > 0.0 && Now >= E.StopAt)
		{
			AC->Stop();
			E.bSounding = false;
			E.StopAt = 0.0;
		}
	}
}

// Для самотеста: состояние эмиттера (false — компонент не под драйвером).
ASDAREBORN_API bool AsdaMapEmitterInfo(const UAudioComponent* AC, bool& bSounding, double& StopAt, float& TrigR)
{
	for (const FAsdaMapEmitter& E : GAsdaMapEmitters)
	{
		if (E.AC.Get() != AC) { continue; }
		bSounding = E.bSounding;
		StopAt = E.StopAt;
		TrigR = FMath::Sqrt(E.TrigSq);
		return true;
	}
	return false;
}
// Для самотеста: заморозить покадровый драйвер (шаги ведёт тест).
ASDAREBORN_API void AsdaMapEmitterHold(bool bHold) { GAsdaMapEmitterHold = bHold; }

// Покадровый шаг: камера каждого игрового мира (точка глаза — `PlayerCameraManager`, как `CRenderer_CaptureViewProj`).
static bool AsdaMapEmitterTick(float /*Dt*/)
{
	if (GAsdaMapEmitterHold || GAsdaMapEmitterTrigger == 0 || GAsdaMapEmitters.Num() == 0) { return true; }
	const double Now = FPlatformTime::Seconds();
	const bool bGrass = AsdaMapEmitterGrassOn();
	// ★ R4, доля a (14.11.2026): перебор подсистем звука — `AsdaForEachSoundSubsystem` (список экземпляров); тело шага прежнее.
	AsdaForEachSoundSubsystem([Now, bGrass](UAsdaSoundSubsystem* Snd)
	{
		UGameInstance* GI = Snd->GetGameInstance();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (!W || !W->IsGameWorld() || !W->HasBegunPlay()) { return; }
		APlayerController* PC = W->GetFirstPlayerController();
		if (!PC || !PC->PlayerCameraManager) { return; }
		AsdaMapEmitterStep(W, PC->PlayerCameraManager->GetCameraLocation(), Now, bGrass);
	});
	return true;
}

// Смена ключа: 0 — вернуть эмиттерам прежнюю вечную петлю с автозапуском; 1 — взять их под драйвер.
static void AsdaOnMapEmitterTriggerChanged(IConsoleVariable* /*Var*/)
{
	if (GAsdaMapEmitterTrigger == 0)
	{
		for (FAsdaMapEmitter& E : GAsdaMapEmitters)
		{
			UAudioComponent* AC = E.AC.Get();
			if (!AC || !IsValid(AC)) { continue; }
			AC->bAutoActivate = true;
			if (!AC->IsPlaying() && AC->GetWorld() && AC->GetWorld()->HasBegunPlay()) { AC->Play(); }
		}
		GAsdaMapEmitters.Reset();
		return;
	}
	for (TObjectIterator<UAsdaSoundSubsystem> It; It; ++It)
	{
		UGameInstance* GI = It->GetGameInstance();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (!W || !W->IsGameWorld()) { continue; }
		for (ULevel* L : W->GetLevels())
		{
			for (AActor* A : L->Actors)
			{
				AAmbientSound* Amb = Cast<AAmbientSound>(A);
				if (Amb && IsValid(Amb)) { AsdaRegisterMapEmitter(Amb->GetAudioComponent()); }
			}
		}
	}
}

// Эталонная кривая всем `AAmbientSound` одного уровня (колбэки мира и смена ключа). Возврат — число акторов.
static int32 AsdaApplyCurveToLevelAmbients(ULevel* Level)
{
	if (!Level) { return 0; }
	int32 N = 0;
	for (AActor* A : Level->Actors)
	{
		AAmbientSound* Amb = Cast<AAmbientSound>(A);
		UAudioComponent* AC = (Amb && IsValid(Amb)) ? Amb->GetAudioComponent() : nullptr;
		if (!AC || !IsValid(AC)) { continue; }
		AsdaApplyLevelAmbientCurve(AC);
		AsdaRegisterMapEmitter(AC);   // ★ 05.11.2026: запуск по радиусу камеры (`asda.Sound.MapEmitterTrigger`)
		++N;
	}
	return N;
}

// То же для всех уровней игрового мира.
static void AsdaApplyCurveToWorldAmbients(UWorld* World)
{
	if (!World || !World->IsGameWorld()) { return; }
	for (ULevel* L : World->GetLevels()) { AsdaApplyCurveToLevelAmbients(L); }
}

static void AsdaOnLevelAmbientCurveChanged(IConsoleVariable* /*Var*/)
{
	for (TObjectIterator<UAsdaSoundSubsystem> It; It; ++It)
	{
		UGameInstance* GI = It->GetGameInstance();
		AsdaApplyCurveToWorldAmbients(GI ? GI->GetWorld() : nullptr);
	}
}

// Поставить множитель всем `AAmbientSound` одного уровня. `bAllowRestart` — после возврата громкости завести заново
//   авто-петлю, которую движок мог остановить как неслышимую (только в начавшем игру мире).
static int32 AsdaApplyMulToLevelAmbients(ULevel* Level, float Mul, bool bAllowRestart)
{
	if (!Level) { return 0; }
	int32 N = 0;
	for (AActor* A : Level->Actors)
	{
		AAmbientSound* Amb = Cast<AAmbientSound>(A);
		if (!Amb || !IsValid(Amb)) { continue; }
		UAudioComponent* AC = Amb->GetAudioComponent();
		if (!AC || !IsValid(AC)) { continue; }
		AC->SetVolumeMultiplier(Mul);
		if (bAllowRestart && Mul > 0.f && AC->bAutoActivate && AC->Sound && AC->IsRegistered() && !AC->IsPlaying())
		{
			AC->Play();
		}
		++N;
	}
	return N;
}

// Все уровни игрового мира (редакторский мир не трогаем: там эмиттеры не играют, а множитель сохранился бы в ассет).
//   Возврат — число обработанных акторов.
static int32 AsdaApplySfxToLevelAmbients(UWorld* World)
{
	if (!World || !World->IsGameWorld()) { return 0; }
	const float Mul = AsdaLevelAmbientMul();
	const bool bRestart = World->HasBegunPlay();
	int32 N = 0;
	for (ULevel* L : World->GetLevels()) { N += AsdaApplyMulToLevelAmbients(L, Mul, bRestart); }
	return N;
}

// Колбэки мира (статические — подсистема на GameInstance, а миры меняются при каждом переходе).
static void AsdaOnPostWorldInitForAmbients(UWorld* World, const UWorld::InitializationValues /*IVS*/)
{
	AsdaApplyCurveToWorldAmbients(World);   // ★ 05.11.2026: эталонная кривая — тоже до активации (`asda.Sound.LevelAmbientEtalonCurve`)
	const int32 N = AsdaApplySfxToLevelAmbients(World);
	if (N > 0) { UE_LOG(LogAsdaSound, Log, TEXT("Звуки уровня под ползунком «Звук»: %d (мир %s, до активации)"), N, *World->GetMapName()); }
}
// После инициализации акторов (страховка: актор, добавленный между инициализацией мира и активацией).
static void AsdaOnActorsInitForAmbients(const FActorsInitializedParams& P)
{
	AsdaApplySfxToLevelAmbients(P.World);
}
// Подгрузка уровня в уже живой мир (стриминг) — его эмиттеры тоже под ползунком.
static void AsdaOnLevelAddedForAmbients(ULevel* Level, UWorld* World)
{
	if (World && World->IsGameWorld())
	{
		AsdaApplyCurveToLevelAmbients(Level);   // ★ эталонная кривая подгруженного уровня
		AsdaApplyMulToLevelAmbients(Level, AsdaLevelAmbientMul(), World->HasBegunPlay());
	}
}

// Устройство звука Slate порта: движковое поведение + громкость эффектов (ключ `asda.Sound.SlateSoundSlider`).
class FAsdaSlateSoundDevice final : public ISlateSoundDevice
{
public:
	// Повтор `FSlateSoundDevice::PlaySound` (Engine/Private/Slate/SlateSoundDevice.cpp) + множитель эффектов.
	virtual void PlaySound(const FSlateSound& Sound, int32 UserIndex) const override
	{
		USoundBase* const Res = Cast<USoundBase>(Sound.GetResourceObject());
		if (!GEngine || !Res) { return; }
		const float Mul = (GAsdaSlateSoundSlider != 0) ? AsdaSfxVolumeMul() : 1.f;
		if (Mul <= 0.f) { return; }   // эффекты заглушены — щелчка нет (как у 2D-эффекта менеджера)
		FAudioDeviceHandle Dev = GEngine->GetActiveAudioDevice();
		if (!Dev) { return; }
		FActiveSound NewActiveSound;
		NewActiveSound.SetSound(Res);
		NewActiveSound.bIsUISound = true;
		NewActiveSound.UserIndex = (uint8)UserIndex;
		NewActiveSound.Priority = Res->Priority;
		NewActiveSound.SetVolume(Mul);
		Dev->AddNewActiveSound(NewActiveSound);
	}
	// Как движковое: длительность ассета.
	virtual float GetSoundDuration(const FSlateSound& Sound) const override
	{
		const USoundBase* Res = Cast<USoundBase>(Sound.GetResourceObject());
		return Res ? Res->Duration : 0.f;
	}
};

static bool GAsdaSlateSoundDeviceInstalled = false;   // своё устройство Slate уже стоит (ставится раз на процесс)
static int32 GAsdaAmbientHookRefs = 0;                // число живых подсистем, державших колбэки мира
static FDelegateHandle GAsdaPostWorldInitHandle;      // колбэк «мир инициализирован»
static FDelegateHandle GAsdaActorsInitHandle;         // колбэк «акторы мира инициализированы»
static FDelegateHandle GAsdaLevelAddedHandle;         // колбэк «уровень добавлен в мир»

// Для самотеста `asda.SelfTest.AmbientVolume` (без правки заголовка): стоит ли своё устройство Slate и с какой громкостью
//   оно сыграет щелчок сейчас (0 — не сыграет).
ASDAREBORN_API bool AsdaSlateSoundDeviceIsPorts() { return GAsdaSlateSoundDeviceInstalled; }
ASDAREBORN_API float AsdaSlateSoundEffectiveVolume() { return (GAsdaSlateSoundSlider != 0) ? AsdaSfxVolumeMul() : 1.f; }

static void AsdaHookLevelAmbients(bool bOn)
{
	if (bOn)
	{
		if (GAsdaAmbientHookRefs++ > 0) { return; }
		GAsdaPostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddStatic(&AsdaOnPostWorldInitForAmbients);
		GAsdaActorsInitHandle    = FWorldDelegates::OnWorldInitializedActors.AddStatic(&AsdaOnActorsInitForAmbients);
		GAsdaLevelAddedHandle    = FWorldDelegates::LevelAddedToWorld.AddStatic(&AsdaOnLevelAddedForAmbients);
		GAsdaMapEmitterTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&AsdaMapEmitterTick), 0.f);   // ★ драйвер эмиттеров
		return;
	}
	if (GAsdaAmbientHookRefs <= 0 || --GAsdaAmbientHookRefs > 0) { return; }
	FWorldDelegates::OnPostWorldInitialization.Remove(GAsdaPostWorldInitHandle);
	FWorldDelegates::OnWorldInitializedActors.Remove(GAsdaActorsInitHandle);
	FWorldDelegates::LevelAddedToWorld.Remove(GAsdaLevelAddedHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(GAsdaMapEmitterTickHandle);
	GAsdaMapEmitters.Reset();
}

static void AsdaInstallSlateSoundDevice()
{
	if (GIsEditor || GAsdaSlateSoundDeviceInstalled || !FSlateApplication::IsInitialized()) { return; }
	FSlateApplication::Get().InitializeSound(MakeShared<FAsdaSlateSoundDevice>());
	GAsdaSlateSoundDeviceInstalled = true;
	UE_LOG(LogAsdaSound, Log, TEXT("Звук кнопок Slate — через устройство порта (громкость эффектов)"));
}

void UAsdaSoundSubsystem::RefreshSfxLoopVolumes()
{
	const float Mul = AsdaSfxVolumeMul();
	for (int32 i = SfxLoops.Num() - 1; i >= 0; --i)
	{
		UAudioComponent* AC = SfxLoops[i].Get();
		if (!AC || !IsValid(AC)) { SfxLoops.RemoveAtSwap(i); continue; }
		AC->SetVolumeMultiplier(Mul);
	}
	// ★ 05.11.2026: и звуки, запечённые в уровень (см. `asda.Sound.LevelAmbientSlider`).
	AsdaApplySfxToLevelAmbients(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
}

float UAsdaSoundSubsystem::GetSfxVolumeMul()
{
	return AsdaSfxVolumeMul();
}

// ★★★ Громкость из профиля игрока — см. объявление в заголовке (почему не приоритет «кода»).
//   Пишем ЧЕРЕЗ сами ключи, но с приоритетом `ECVF_SetByConsole` — рангом явного выбора человека.
//   Так (1) запись не отбрасывается, даже если ключ когда-то задавали из консоли; (2) у ключа
//   синхронны обе копии значения — рабочая (её читает звук) и «главная» (её печатает консоль:
//   прямая запись в хранилище оставляла в печати старое число); (3) срабатывают штатные делегаты
//   смены — громкость звучащего трека и звучащих петель эффектов переставляется сразу.
void UAsdaSoundSubsystem::ApplyUserVolumes(float MusicVolume01, bool bMusicMuted, float SfxVolume01, bool bSfxMuted)
{
	CVarAsdaMusicVolume->Set(FMath::Clamp(MusicVolume01, 0.f, 1.f), ECVF_SetByConsole);
	CVarAsdaMusicEnable->Set(bMusicMuted ? 0 : 1, ECVF_SetByConsole);
	// «Заглушить эффекты» эталона (`sub_92CBBE(g_pSoundMgr, флаг)`) у порта выражается нулевым
	//   множителем: так же ведут себя и одноразовые звуки, и петли.
	CVarAsdaSfxVolume->Set(bSfxMuted ? 0.f : FMath::Clamp(SfxVolume01, 0.f, 1.f), ECVF_SetByConsole);
	// Делегаты ключей срабатывают только при СМЕНЕ значения — накат повторяем явно, чтобы трек,
	//   запущенный немым, гарантированно получил текущую громкость.
	OnMusicVolumeChanged(nullptr);
	AsdaOnSfxVolumeChanged(nullptr);
}

void UAsdaSoundSubsystem::PlayMusic(FName Track, bool bLoop, float FadeSec)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) return;

	// Единичный стрим (как CSoundStream): гасим текущий трек перед новым.
	// IsValid — компонент прошлого уровня после травела уже уничтожен (подсистема живёт на GameInstance).
	if (!IsValid(MusicComp)) { MusicComp = nullptr; }
	if (MusicComp)
	{
		MusicComp->bAutoDestroy = true;   // старый компонент сам уничтожится по завершении fade/stop (иначе утечка)
		if (FadeSec > 0.f) { MusicComp->FadeOut(FadeSec, 0.f); }
		else               { MusicComp->Stop(); }
		MusicComp = nullptr;
	}

	// Смена трека напрямую отменяет отложенную (эталон: StreamPlay сбрасывает pending-состояние).
	bMusicQueued = false;
	PendingMusicTrack = NAME_None;
	if (UGameInstance* GI = GetGameInstance()) { GI->GetTimerManager().ClearTimer(MusicQueueTimer); }
	CurrentMusicTrack = NAME_None;

	USoundBase* Music = ResolveSound(Track, AsdaAssetPaths::kMusicFolder);   // ★ волна 2 п. 7: папка музыки — реестр путей
	if (!Music)
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlayMusic: не найден трек '%s'"), *Track.ToString());
		return;
	}
	// bAutoDestroy=false — держим компонент, чтобы управлять (fade/stop). bLoop — если ассет-луп не выставлен.
	// ★ Ключ `asda.Sound.MusicEnable` (порт чекбокса 70012) гасит трек на старте так же, как и на лету.
	const float MusicVol = (GAsdaMusicEnable != 0) ? FMath::Clamp(GAsdaMusicVolume, 0.f, 1.f) : 0.f;
	// ★ 12.10.2026 (владелец: музыка с первого кадра и без обрывов): persist=true — единичный музыкальный стрим ПЕРЕЖИВАЕТ
	//   смену уровня (перезагрузка логина после выбора языка, уход на карту замера и возврат), как CSoundStream оригинала,
	//   у которого один стрим на процесс. Смена трека по-прежнему через PlayMusic/StopMusic (старый компонент гасится выше).
	MusicComp = UGameplayStatics::SpawnSound2D(World, Music, MusicVol, 1.f, 0.f, nullptr, true /*persist*/, true /*autoActivate*/);
	if (MusicComp)
	{
		MusicComp->bAutoDestroy = false;
		// ★★★ ИСПРАВЛЕНО 19.09.2026: цель фейда — 1.0, а НЕ громкость музыки.
		//   У `UAudioComponent` итоговая громкость = `VolumeMultiplier` × уровень фейда: это ДВА
		//   независимых множителя. Прежде здесь стояло `FadeIn(FadeSec, MusicVol)`, и громкость
		//   применялась ДВАЖДЫ (0.5 → 0.25), а главное — уровень фейда ЗАМЕРЗАЛ на значении момента
		//   старта трека. Ползунок окна настроек менял только `VolumeMultiplier`, поэтому трек, начатый
		//   при нулевой громкости (или при снятом «музыка вкл»), оставался немым при любом положении
		//   ползунка — «двигаешь, а громкость не меняется». Громкость несёт только множитель.
		if (FadeSec > 0.f) { MusicComp->FadeIn(FadeSec, 1.f); }
		CurrentMusicTrack = Track;
	}
}

// ── Очередь смены трека: гасим текущий, ПОСЛЕ гашения стартуем новый ──
//   Эталон CSoundStream_QueueNext@0xFA05B0: если смена уже поставлена (флаг CSound+72) — выходим,
//   иначе запоминаем имя + время фейда входа и запускаем гашение (CSoundStream_SetupFadeOut).
//   Эталон CSound_UpdateFade@0xFA0620: догасив до нуля — StreamStop, затем StreamPlay нового с фейдом.
void UAsdaSoundSubsystem::QueueMusic(FName Track, float FadeOutSec, float FadeInSec)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World) { return; }
	if (bMusicQueued) { return; }        // смена уже идёт — эталон игнорирует повторные постановки

	PendingMusicTrack = Track;
	PendingFadeInSec  = FMath::Max(0.f, FadeInSec);
	bMusicQueued      = true;

	const float FadeOut = FMath::Max(0.f, FadeOutSec);
	if (!IsValid(MusicComp)) { MusicComp = nullptr; }
	if (MusicComp)
	{
		MusicComp->bAutoDestroy = true;
		if (FadeOut > 0.f) { MusicComp->FadeOut(FadeOut, 0.f); }
		else               { MusicComp->Stop(); }
		MusicComp = nullptr;
	}
	CurrentMusicTrack = NAME_None;

	if (FadeOut > 0.f)
	{
		GI->GetTimerManager().SetTimer(MusicQueueTimer, this, &UAsdaSoundSubsystem::StartQueuedMusic, FadeOut, false);
	}
	else
	{
		StartQueuedMusic();
	}
}

void UAsdaSoundSubsystem::StartQueuedMusic()
{
	if (!bMusicQueued) { return; }
	const FName Track = PendingMusicTrack;
	const float FadeIn = PendingFadeInSec;
	bMusicQueued = false;
	PendingMusicTrack = NAME_None;
	if (Track.IsNone()) { return; }
	PlayMusic(Track, /*bLoop*/true, FadeIn);
}

void UAsdaSoundSubsystem::StopMusic(float FadeSec)
{
	bMusicQueued = false;
	PendingMusicTrack = NAME_None;
	CurrentMusicTrack = NAME_None;
	if (UGameInstance* GI = GetGameInstance()) { GI->GetTimerManager().ClearTimer(MusicQueueTimer); }
	if (!IsValid(MusicComp)) { MusicComp = nullptr; }
	if (!MusicComp) return;
	MusicComp->bAutoDestroy = true;   // уничтожить компонент после fade/stop (не копить остановленные)
	if (FadeSec > 0.f) { MusicComp->FadeOut(FadeSec, 0.f); }
	else               { MusicComp->Stop(); }
	MusicComp = nullptr;
}

bool UAsdaSoundSubsystem::IsMusicPlaying() const
{
	return IsValid(MusicComp) && MusicComp->IsPlaying();
}

// ── Звук по числовому ID (эталон CSound_PlayName2D/3D: soundId → CSoundFileTable → имя) ──

void UAsdaSoundSubsystem::PlaySound2DById(int32 SoundId, bool bLoop)
{
	if (!bLoop && UiSoundMuteDepth > 0) { return; }   // ★ 22.10.2026: служебное переоткрытие окон после травела — молча
	const FName Name = ResolveSoundName(SoundId);
	if (Name.IsNone())
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlaySound2DById: id %d нет в таблице"), SoundId);
		return;
	}
	PlaySound2D(Name, bLoop);
}

UAudioComponent* UAsdaSoundSubsystem::PlaySound3DById(int32 SoundId, FVector Location, float MinDist,
	float MaxDist, bool bLoop)
{
	const FName Name = ResolveSoundName(SoundId);
	if (Name.IsNone())
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlaySound3DById: id %d нет в таблице"), SoundId);
		return nullptr;
	}
	return PlaySound3D(Name, Location, MinDist, MaxDist, bLoop);
}

// ── Record-звук (NPC/креатуры) с вариантом "<Base>_<Variant>" (Variant==11 → "_99") ──

void UAsdaSoundSubsystem::PlayRecordSound2D(FName BaseName, int32 Variant, bool bLoop)
{
	PlaySound2D(BuildVariantName(BaseName, Variant), bLoop);
}

UAudioComponent* UAsdaSoundSubsystem::PlayRecordSound3D(FName BaseName, int32 Variant, FVector Location,
	float MinDist, float MaxDist, bool bLoop)
{
	return PlaySound3D(BuildVariantName(BaseName, Variant), Location, MinDist, MaxDist, bLoop);
}

void UAsdaSoundSubsystem::PlayMusicById(int32 TrackId, bool bLoop, float FadeSec)
{
	const FName Name = ResolveSoundName(TrackId);
	if (Name.IsNone())
	{
		UE_LOG(LogAsdaSound, Warning, TEXT("PlayMusicById: id %d нет в таблице"), TrackId);
		return;
	}
	PlayMusic(Name, bLoop, FadeSec);
}

void UAsdaSoundSubsystem::RestartStoredSound()
{
	if (!StoredSoundName.IsNone())
	{
		PlaySound2D(StoredSoundName, false);   // рестарт последнего 2D SFX (аналог storedSoundId)
	}
}
