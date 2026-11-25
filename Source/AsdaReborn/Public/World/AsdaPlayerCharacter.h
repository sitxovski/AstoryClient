// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaPlayerCharacter — локальный игрок в мире (порт AsdaStory, реверс player_control_move_re.md +
//   player_anim_appearance_re.md). Editor-Friendly Pawn:
//   - Базовый скелет ch1_{m|f}_cremmy_preview (скелет cre_cremmy == ch4-скелет клипов мира) + части/экип
//     через единую систему UAsdaCharAssembly (chest/pants/shoes/gloves/helmet/лицо/волосы).
//   - Анимации мира = ch4_{g}_00_* (ассеты previewanim_12..19): idle01/idle02/idle03/sitdown01/walk01/
//     run01/death01/back01. Машина состояний = CChar_SetState@0xA13D50: move=RUN (walk только slow-бафф),
//     смерть=слот 7. Атака — фаза боя (клипы в наборах оружия).
//   - Движение: сервер-авторитет. 4006 шлёт контроллер; движение стартует по ОТВЕТУ 4007 (result=1,
//     speed@+21 юнит/мс) — линейная интерполяция к цели по XZ, мгновенный поворот, Y=снап террейна
//     (charMoveToWorldPos@0xA1E3D0: Y нигде не передаётся). result=5 — анти-чит снап.
//   - Орбит-камера: SpringArm+Camera; зум колесом шаг ×0.25 в [170..1270], ПКМ-драг 0.01 рад/px
//     (Camera_MouseWheelZoom@0x959350 / Camera_MouseRotate@0x959590) — управляет контроллер.
#pragma once

#include "CoreMinimal.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "GameFramework/Pawn.h"
#include "Engine/DataTable.h"
#include "UObject/ObjectKey.h"              // ★ charsel_preload: TObjectKey — владение флагом полных мипов
#include "Characters/AsdaWornParts.h"   // ★ R3: FAsdaWornParts — узкий заголовок вместо AsdaCharAssembly.h
#include "World/AsdaClanFlagData.h"   // FAsdaClanFlagData — дизайн эмблемы клана (пакет 6542)   ★ R3: FAsdaClanFlagData — узкий заголовок вместо AsdaClanFlag.h
#include "AsdaPlayerCharacter.generated.h"

// ★★★ ОБЩАЯ ТОЧКА: БАЗА ПУТИ АССЕТОВ ПРЕВЬЮ-СКЕЛЕТА ПЕРСОНАЖА (закон `port-shared-points`).
//   Этот один скелет (`cre_cremmy` + hair-кости) несёт ВСЁ: тело мирового персонажа, превью
//   char-select, клипы мира `previewanim_12..19`, эмоции, наборы оружия, клипы навыков и парные
//   клипы «одержимости». Полный путь производится суффиксами: «» = SkeletalMesh,
//   «anim_%02d» = AnimSequence (`UAsdaModelAnimSubsystem::BuildSkelAnimPath`).
//   ★ Живёт ЗДЕСЬ, а не в заголовке логина: владелец ассета — мировой персонаж, а логин и все
//     прочие потребители зависят от него, а не наоборот.
//   ⚠ 02.09.2026 сведено: тот же литерал был выписан ЧЕТЫРЕ раза в `Login/AsdaLoginGameMode.cpp`,
//     ДВАЖДЫ в `Login/AsdaPossessScene.cpp`, ТРИЖДЫ в `World/AsdaPlayerCharacter.cpp` и ОДИН раз
//     в `World/AsdaWorldSubsystem.cpp`. При переименовании ассета часть копий отстала бы молча.
//   ★ волна 2 п. 7 (14.09.2026): значения берутся из реестра путей ассетов (Resources/AsdaAssetPaths.h, генерируемый).
inline constexpr const TCHAR* kAsdaCharPreviewBaseM = AsdaAssetPaths::kCharPreviewBaseM;
inline constexpr const TCHAR* kAsdaCharPreviewBaseF = AsdaAssetPaths::kCharPreviewBaseF;

// База по полу ЗАПИСИ ПЕРСОНАЖА (`Sex`: 2 = женский, иначе мужской — так его кладут пакеты 1023/1024
//   и так его читают все потребители порта). Для «пола-флага» эталона (1 = мужской) есть
//   `FAsdaPossessData::IsMale`; здесь именно поле записи, чтобы не плодить третью трактовку.
inline const TCHAR* AsdaCharPreviewBase(uint8 Sex) { return (Sex == 2) ? kAsdaCharPreviewBaseF : kAsdaCharPreviewBaseM; }

// ★ 10.10.2026 (копание, `Reports/owner_1010/digging.md`): ОТКУДА пришло состояние лопаты 40/43 — от этого у эталона
//   зависят стартовый эффект, его привязка и правило смены меша инструмента:
//   • Self     — свой, `Item_UseShovelingTool` → `CCharacter_SetAction` case 40/43 (entity_char.c:17240-17400): эффект
//                `c-shovel-a_01` для ОБОИХ состояний, матрица по курсу и рельефу, без привязки; меш — по прежнему режиму таймера;
//                [ИСПРАВЛЕНИЕ] 10.10.2026 (`asda.Shovel.StartFxFix`, по умолчанию 1): своему 43 — `c-shovel-b_01` (яма до 8.5 с);
//   • Peer4025 — сосед, 4025 110/114 (network.c:39716/39958): `c-shovel-a_01` (110) / `c-shovel-b_01` (114), матрица соседа;
//   • Spawn    — сосед появился уже копающим (`GameNet_OnUserSpawn`, game_main.c:7303/7425): тот же выбор эффекта, привязка 6.
enum class EAsdaShovelEntry : uint8
{
	Self,       // свой персонаж (`CCharacter_SetAction`)
	Peer4025,   // сосед по 4025 110/114 (`Recv_CharSpawnFx` → `CChar_SetState`)
	Spawn,      // сосед появился в состоянии 110/114 (`GameNet_OnUserSpawn`)
};

UENUM(BlueprintType)
enum class EAsdaPlayerState : uint8
{
	None, Idle, Move, Sit, Dead   // None = стартовый сентинел: гард SetAnimState не глотает ПЕРВЫЙ Idle (иначе спавн в ref-T-позе)
};

// ★★★ СОСТОЯНИЕ ДЕЙСТВИЯ ПЕРСОНАЖА 1:1 С ЭТАЛОНОМ (дедуп №2, В3 S7, 12.10.2026). Поле `+47551` (0x2E6FC, в декомпиле
//   `effectSlots[112].field_8`) пишет ОДИН вход — `CChar_SetState@0xA13D50` (entity_char.c:19193-19952); значения enum = номера
//   его case. Разбор всех веток — `Reports/dedup_audit2/wave3_S7.md` §2 и `RE/docs/client/logic/action_state_machine.md` §2.
//   ⚠ `Net_GetCurrentMapType` эталона читает ЭТО поле (миснейм): 6 = МЁРТВ, «типа карты» нет.
//   В порту одно поле `AAsdaPlayerCharacter::BusyAction` держит «занятую» часть (`bActionBusy = 1`: 33…46, 47, 51, 60…67, 68) —
//   прежде это были 4 признака (эмоция / рыбалка / 68 / лопата). Ход, сидение, смерть, замах и навык ведут прежние поля
//   (`AnimState`, `SitPhase`, гейты замаха/навыка); полный номер для гейтов собирает `AAsdaPlayerController::KbEtalonActionState`.
enum class EAsdaActionState : uint8
{
	Idle       = 0,    // покой (`case 0`)
	Move       = 1,    // ход (`case 1`: слот 5 бег / 4 при баффах 112/186, bActionBusy = 0)
	Move2      = 2,    // ход, второй код (`case 2` = setup `case 1`)
	Attack     = 5,    // базовая атака (`case 5`: спешивание, trail оружия)
	Dead       = 6,    // смерть (`case 6`: слот 7, звук 63/64, bActionBusy = 0)
	SitDown    = 7,    // садится (`case 7`: трек 3 с 0, bActionBusy = 0)
	Sitting    = 8,    // сидит (`case 8`: трек 3 с фазы 1500/2000 мс)
	StandUp    = 9,    // встаёт (`case 9`: хвост трека 3, bActionBusy = 0)
	CastA      = 13,   // каст (`case 13`: сброс зарядных таймеров слота навыка)
	CastHit    = 14,   // каст: удар (`case 14`: бленд 3000 мс)
	Short15    = 15,   // короткое действие слота 0 (`case 15`)
	Short22    = 22,   // короткое действие (`case 22`)
	Skill      = 24,   // применение навыка (`case 24`: спешивание, bActionBusy = 0)
	Long25     = 25,   // долгое действие слота 0 (`case 25`: бленд 30000 мс)
	Variant26  = 26,   // вариант 25 (`case 26`)
	CastB      = 27,   // каст, вариант B (`case 27` = setup `case 13`)
	DeadIdle   = 29,   // труп после клипа смерти (ставит тик состояния 6; в `CChar_SetState` своего case нет)
	EmoteFirst = 33,   // эмоции 33…46 (слоты 10…23, `LABEL_154`/свой case), bActionBusy = 1
	Shovel     = 40,   // лопата (трек 17) — в диапазоне эмоций, `LABEL_154` (+ `digMineState = 0`)
	AutoShovel = 43,   // авто-лопата (трек 20), `LABEL_154`
	EmoteLast  = 46,   // последняя эмоция
	Action47   = 47,   // захват точки CampWar (трек 25, `LABEL_213`), bActionBusy = 1, клип в ЦИКЛЕ — `EnterCampPose` (★ 30.10.2026)
	Action51   = 51,   // проигравший PvP (трек 26, `LABEL_213`), bActionBusy = 1, клип один раз → 0 — `EnterCampPose` (★ 30.10.2026)
	FishFirst  = 60,   // рыбалка 60…67 (трек «действие − 33»), bActionBusy = 1
	FishLast   = 67,   // последнее действие рыбалки
	Mining68   = 68,   // «добыча» (minewar, трек 36, `LABEL_213`), bActionBusy = 1
};

// Классы номеров `EAsdaActionState` (одна точка; номер — как у эталона, `int32`).
namespace AsdaActionState
{
	// Лопата 40 / авто-лопата 43.
	inline bool IsShovel(int32 S) { return S == 40 || S == 43; }
	// Эмоция: 33…46 без лопат 40/43 (у порта это отдельная система копания).
	inline bool IsEmote(int32 S) { return S >= 33 && S <= 46 && !IsShovel(S); }
	// Рыбалка 60…67.
	inline bool IsFish(int32 S) { return S >= 60 && S <= 67; }
	// `bActionBusy = 1` по `CChar_SetState`: 33…46, 47, 51, 60…67, 68.
	inline bool IsBusyCode(int32 S) { return (S >= 33 && S <= 47) || S == 51 || (S >= 60 && S <= 68); }
}

// ★ СЛОТ МОУШЕН-НАБОРА (эталон CCharacter_LoadMotionAnims@0x9B84D0: клип пишется в pModelObj+(slot<<6)).
//   Из этих слотов выбирает состояние (CChar_SetState@0xA13D50). ИМЕННО ЭТИ клипы зависят от НАДЕТОГО
//   ОРУЖИЯ — набор ch4_{m|f}_{SS} выбирается по ItemSubType (см. MotionSetOf). Эмоции (слоты 3,10..26)
//   и смерть (слот 7) ВСЕГДА берутся из набора _00 и от оружия не зависят.
enum class EAsdaMotionSlot : uint8
{
	Idle01 = 0,   // слот 0 — покой №1 (боевая стойка, «руки подняты»); у набора _08 сюда грузится idle05
	Idle02,       // слот 1 — покой №2 (мирный, руки вниз)
	Idle03,       // слот 2 — покой №3 (фиджет)
	Walk01,       // слот 4 — шаг (только под slow-баффом 112/186)
	Run01,        // слот 5 — бег
	Back01,       // слот 24 — движение назад
	Idle04,       // слот 8 — ТОЛЬКО набор _08 (баллиста); ссылок на проигрывание в срезе нет
	Idle05,       // — idle05 набора _08 (он же слот 0 при animClass 11)
	Idle06        // слот 9 — ТОЛЬКО набор _08; ссылок на проигрывание в срезе нет
};

// Строка DT_RideEffect — шлейф и звуки ЕЗДЫ (Data/Ride_effect_table.bin, магия "RET", запись 260 б).
//   Ключ строки = Code транспорта (== Special_ItemList.Code). Импортёр: Tools/asda_build_ride_effect_dt.py.
//   Раскладка записи (CONFIRMED по декомпилу; §1.3 отчёта раньше считала +132 резервом — ошибка):
//     +4   EffectFile  — Effect_SpawnByName на кости M_DUMMY/G_DUMMY каждые 2500 мс бега (item_inventory.c)
//     +68  SoundFile   — loop-звук движения каждые 2000 мс (2D для себя, 3D для чужих)
//     +132 SoundFile2 / +196 SoundFile3 — «_id2»/«_id1»: charItemSoundChargeTick@0xF27740 чередует их
//          по флагу-переключателю (двухтактный «мотор» у велосипеда/метлы/мустанга и т.п., 93 записи).
USTRUCT(BlueprintType)
struct FAsdaRideEffectRow : public FTableRowBase
{
	GENERATED_BODY()
	// Код предмета-транспорта (дубль RowName).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Mount") int32 Code = 0;
	// Имя .jeb шлейфа без расширения, нижний регистр → BP «BP_Fx_<EffectName>» в /Game/AsdaAssets/Effects.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Mount") FString EffectName;
	// Имя .wav без расширения, нижний регистр → USoundWave в /Game/AsdaAssets/Sound.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Mount") FString SoundName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Mount") FString SoundName2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Mount") FString SoundName3;
};

// ★★★ ПОДМЕНА ОДНОГО СЛОТА МАТЕРИАЛА В РЕЖИМЕ «ДУША» (SoulState == 1): что стояло в слоте и на
//   какой translucent-MID мы его сменили. USTRUCT с UPROPERTY, а не голая структура: режим держится
//   МИНУТАМИ, а исходник слота (обычно MID от `ApplyItemTexture`/`ApplyFace`) после подмены больше
//   никем не удерживается — без GC-видимой ссылки его соберёт сборщик, и возвращать было бы нечего.
USTRUCT()
struct FAsdaSoulSlotSwap
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<class UMeshComponent> Comp;              // компонент (тело / часть / оружие / причёска)
	UPROPERTY() int32 Slot = 0;                                          // индекс материального слота
	UPROPERTY() TObjectPtr<class UMaterialInterface> Orig;               // исходный материал слота (возвращается при выходе из 1)
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Mid;          // наш MID на M_AsdaCharLitSoul (Tex + SoulOpacity 0.3)
};

UCLASS()
class ASDAREBORN_API AAsdaPlayerCharacter : public APawn
{
	GENERATED_BODY()
	// ★ 27.09.2026: самотест своего 4007 (`AsdaMove4007SelfTest.cpp`) читает скорость/цель шага напрямую.
	friend struct FAsdaMove4007SelfTest;
	friend struct FAsdaFishAnimSelfTest;   // ★ 09.10.2026 (В5fix): самотест рыбалки `asda.Fish.AnimSelfTest` — клип покоя `IdleClipIdx`
	friend struct FAsdaDedupW2CTestAccess; // ★ 11.10.2026 (дедуп №2, В2-C): самотест `asda.Dedup.W2C.Ride` — канал звука транспорта (F13)
	friend struct FAsdaActionStateSelfTest; // ★ 12.10.2026 (дедуп №2, В3 S7): самотест `asda.Action.SelfTest` — единое состояние действия, преамбула, танцпол

public:
	AAsdaPlayerCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Player")
	TObjectPtr<class USceneComponent> Root;
	// Масштабированный узел (0.01) для ТЕЛА и ВСЕГО экипа — единый масштаб (иначе части экипа, крепясь к
	//   Root, оставались бы в 100× больше тела). Камера — на Root (не масштабируется).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Player")
	TObjectPtr<class USceneComponent> MeshRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Player")
	TObjectPtr<class USkeletalMeshComponent> BaseSkel;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Player")
	TObjectPtr<class USpringArmComponent> CameraArm;

	// ★ Хитбокс для пикинга (клик по ЧУЖОМУ игроку; свой павн из трейса исключается контроллером).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Player")
	TObjectPtr<class UCapsuleComponent> PickHitbox;

	// ★ Плашка над головой (порт JUI TARGETMARK_MAN1 1:1): имя/гильдия/титул. Создаётся в конструкторе →
	//   видна в редакторе. У СВОЕГО персонажа (просьба юзера 2026-07-29): имя как у всех, полоса HP —
	//   ВСЕГДА видна (bShowHp ставит мир-система при спавне self; у чужих HP — только у выбранной цели).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Player")
	TObjectPtr<class UAsdaNameplateComponent> Nameplate;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Player")
	TObjectPtr<class UCameraComponent> Camera;

	// Пол (1 муж / 2 жен) — выбирает базовый скелет и набор клипов.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Player")
	uint8 Sex = 1;

	// Сетка проходимости текущей карты (DA_WalkGrid) — блокирует шаг в непроходимый тайл (клиентское
	//   предсказание; сервер валидирует авторитетно). nullptr → без ограничений.
	UPROPERTY(BlueprintReadWrite, Category="Asda|Player")
	TObjectPtr<class UAsdaWalkGrid> WalkGrid;

	// Собрать внешность: базовый скелет по полу + части (UAsdaCharAssembly) + лицо/волосы. Зовётся при спавне.
	UFUNCTION(BlueprintCallable, Category="Asda|Player")
	void InitAppearance(uint8 InSex, const FAsdaWornParts& Parts);

	// ── Движение ──
	// Идти В ТОЧКУ (click-to-move): едет до DstGame и останавливается. cur/dst GAME XZ, speed юнит/мс.
	void NetMove(const FVector2D& CurGame, const FVector2D& DstGame, float SpeedUnitsPerMs);
	// ★ 27.09.2026 — порт `charMoveToWorldPos@0xA1E3D0` (грант своего 4007 по эталонной схеме,
	//   ключ `asda.Move.EtalonSpeedSync`): идти В ТОЧКУ от ТЕКУЩЕЙ позиции актора с заданной скоростью —
	//   БЕЗ снапа в серверный cur (у эталона его нет: cur служит только для расчёта времени пути).
	//   Скорость берётся как есть (подогнанная под серверное время), без подмены на 0.35.
	void NetMoveFromActor(const FVector2D& DstGame, float SpeedUnitsPerMs);
	// ★ 27.09.2026 — Р2: renderId ЦЕЛИ ХОДА, эталонное старшее слово `CCharacter::targetRenderId` (+0x32DFE).
	//   Пишет ТОЛЬКО 4007 (`Recv_EntityMove`, поле пакета +25; своему при result 0 — −1) и копия структуры
	//   персонажа (`charCopyFromNetStruct@0xEFD880`). Читатель в оригинале один — горячая клавиша отряда «взять
	//   цель помеченного соотрядника» (`UI_DispatchTargetContextMenu@0xC532D0`, 0xC54260): его цель = младшее
	//   слово, а если такой сущности нет — это слово. Скан бинаря по смещению 0x32DFE: 5 обращений, других
	//   читателей нет. В порте читателя пока нет (реестр отложенного), значение хранится 1:1.
	int32 MoveTargetRenderId = -1;
	// Непрерывное движение В НАПРАВЛЕНИИ (WASD/стрелки): едет пока задано, стоп по StopMove. DirGame — норм. game XZ.
	//   FaceDirGame — куда СМОТРИТ меш (эталон: пятясь на S, персонаж остаётся лицом по камере-вперёд,
	//   buffFxSlots[208]=backpedal); bBackpedal — клип back01 (anim_19, слот 24) вместо run01.
	void SetMoveDirection(const FVector2D& DirGame, float SpeedUnitsPerMs,
		const FVector2D& FaceDirGame, bool bInBackpedal);
	// Остановить локальное движение (отпущены клавиши / достигнута цель).
	void StopMove();
	// Снап (4009 / 4007 result=5): телепорт в позицию, стоп.
	void NetSnap(const FVector2D& CurGame);
	// ★ 22.10.2026 (ревизия плавности, ВАЖНО-Б): ЧИСТЫЙ снап — перенос сервером 4056 (воскрешение/короткий телепорт): без хвоста
	//   поправки (прежний хвост гасится) и с мгновенным курсом (`SnapFacingToLogical`).
	void NetSnapHard(const FVector2D& CurGame);
	// ★ 29.10.2026 (Charge 537/597) [ОРИГИНАЛ] шаг рывка: `player_setPositionAndTarget@0xA23F60` из `Char_UpdateDash@0xB3B010` —
	//   актор в точку (высота земли/воды как у хода), без «хвоста» поправки, клип и состояние НЕ трогаются (идёт клип навыка),
	//   курс не меняется (персонаж уже смотрит на цель навыка). Ход (если был) гасится.
	void SetDashPosGame(const FVector2D& PosGame);
	// ★ Остановить локальное движение НА МЕСТЕ (без телепорта): в бою персонаж стоит и бьёт,
	//   иначе он «дрейфует» к бродящей цели и сервер глушит замахи (гейт движения).
	void StopMoving();
	// Текущая позиция в GAME-координатах XZ (для отправки 4006).
	FVector2D GetGamePosXZ() const;
	// Едем ли сейчас (нужно маршрутизатору: персонаж, упёршийся в стену, гасит bMoving — это сигнал
	//   «отрезок перекрыт», по которому контроллер перестраивает маршрут).
	bool IsMoving() const { return bMoving; }

	// ★ КУРС персонажа в UE-градусах (yaw «носа»). Разворачивается МЕШ (MeshRoot), а не актор —
	//   GetActorRotation().Yaw всегда 0 и для курса НЕ ГОДИТСЯ (из-за этого маркер игрока на мини-карте
	//   всегда смотрел вверх). Меш довёрнут на MeshYawOffset — снимаем его обратно.
	//   На мини-карте (экран +X = game X, экран +Y = game Z) угол поворота иконки = ровно этот yaw:
	//   FaceGameDir даёт yaw = atan2(dirGameX, −dirGameZ) + offset, а «вверх» иконки при повороте на φ
	//   уходит в (sin φ, −cos φ) — совпадает при φ = yaw.
	UFUNCTION(BlueprintCallable, Category="Asda|Player")
	float GetFacingYawUE() const;

	// ★ 27.09.2026 (пакет 4024, `Reports/owner_0927/pkt4024_facing.md`): НАПРАВЛЕНИЕ ВЗГЛЯДА в игровых
	//   координатах (x, z), единичный вектор из курса меша (`GetFacingYawUE`). Это аналог поля эталона
	//   `CCharacter+47718*4` (`effectSlots[112].gap_298+12`, поля 5..7): его пишут ТОЛЬКО вместе с
	//   `charComputeFacingAngleFromDir` (game.c:78554, 80327, 80424, 68714) нормированным (dst−cur)/|dst−cur|.
	//   Все 20+ отправителей 4024 эталона (`Send_CharStatePos@0x9F9EF0`) кладут в тело +5/+9 именно его.
	//   Единая точка для всех отправителей 4024 порта — не дублировать формулу по месту.
	FVector2D GetFacingDirGame() const;

	// Доворот меша (не актора!) под базовую ориентацию модели: «фронт» модели = локальный +Y, а движение
	//   выставляем по +X → визуально бок; правим на -90° (тест: бег вперёд → смотрел влево). Правится в редакторе.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Player")
	float MeshYawOffset = -90.f;

	// Состояние анимации.
	void SetAnimState(EAsdaPlayerState NewState);
	UPROPERTY(BlueprintReadOnly, Category="Asda|Player")
	EAsdaPlayerState AnimState = EAsdaPlayerState::None;   // до первого SetAnimState — чтобы Idle реально проиграл клип на спавне

	// Фаза «сесть/сидит/встать» эталонными номерами состояний (0 нет, 7, 8, 9) — для живой проверки.
	UFUNCTION(BlueprintPure, Category="Asda|Player")
	int32 GetSitPhase() const { return SitPhase; }
	// ★ 27.09.2026 (pkt4024_facing.md §6): «сидячая» часть эталонного состояния действия `+47551` — 7 садится,
	//   8 сидит, 9 встаёт, 0 — не сидит. Фаза клипа, а без неё (клип не найден) `AnimState == Sit` = 8.
	//   Единая точка: её читают `AAsdaPlayerController::KbEtalonActionState`, переключатель «сесть/встать»
	//   и выбор иконки `act_updown`/`act_standup` (хотбар, окно действий).
	int32 GetEtalonSitState() const
	{
		if (SitPhase == 7 || SitPhase == 8 || SitPhase == 9) { return SitPhase; }
		return (AnimState == EAsdaPlayerState::Sit) ? 8 : 0;
	}
	// ★★★ 24.10.2026 (выход из сидения 1:1, ключ `asda.Char.SitExitEtalon`): эталонная запись `CChar_SetState(…, X)`
	//   (`CCharacter_SetAction@0x9EE720`, entity_char.c:16390) ПЕРЕПИСЫВАЕТ 7/8/9 сразу — без хвоста «встаёт» и без 4024
	//   (код 0 шлёт только тик состояния 9 по концу клипа, `Player_TickSendStatePosOnAnimEnd`). Так эмоция 33…46, замах 5/15,
	//   навык 24 и `CharFx_AutoSabzil_State(…, 0)` сцен (гача, ремесло) снимают сидение; по их концу — покой (0), а не снова «сидит».
	//   bIdleClip — поставить клип покоя (переход в 0); false — клип ставит вызывающий. Возврат: сидение было и снято.
	bool DropSitForEtalonState(bool bIdleClip, const TCHAR* Why);
	// ★ 24.10.2026: вход в сцену/окно эталона «стоп + встать, если сидит, иначе покой» — элитный магазин (`charBeginAutoMove@0xC3E420`),
	//   парикмахерская (`actionExecuteSlotAbility@0xCF0900`), магазин снаряжения (`tradeWinConfirmSubmit@0xC37CB0`), бустер в городе
	//   (`skillAction_TriggerSabzil@0xD222C0`): на ходу 4008, затем `charActionStateIs7to9` → состояние 9 («встаёт», БЕЗ 109; по концу — 0),
	//   иначе состояние 0. Ключ 0 — прежний порт (ничего).
	void ApplyEtalonSceneStop(const TCHAR* Why);
	// Ключ `asda.Char.SitExitEtalon` (1 — выход из сидения по эталону; 0 — прежний порт).
	static bool IsSitExitEtalon();

	// ★ «НЕДЕЕСПОСОБЕН» — порт эталонного isMovementActionState@0x95DBE0 (entity_char.c:54):
	//     state 6 (смерть) / 7 / 8 / 9 (нокдаун-варианты) → ВЕСЬ мировой ввод глохнет
	//     (клик-мув input.c:170, ПКМ ui_core.c:969, WASD math_geom.c:622, подбор item_inventory.c:22254,
	//      скиллы entity_char.c:24590). Пока в порту из этого набора реализовано только состояние
	//     смерти — 7/8/9 добавить сюда, когда появятся (закон no-scope-cutting: зафиксировано письменно).
	UFUNCTION(BlueprintPure, Category="Asda|Player")
	bool IsIncapacitated() const { return AnimState == EAsdaPlayerState::Dead; }

	// ★ ВХОД В МИР УЖЕ МЁРТВЫМ (4003 regenState == 200 / рассинхрон по 6001 причина 1).
	//   Персонаж должен СРАЗУ оказаться в позе трупа, БЕЗ проигрывания клипа гибели: гибель произошла
	//   в прошлой сессии, повторно её показывать нельзя. Реализация — тот же клип моушен-слота 7
	//   (death01, ассет 18), но проигрывание останавливается на ПОСЛЕДНЕМ кадре (у обычной смерти это
	//   тот же самый кадр, только доигранный) — поза идентична, «падения» не видно.
	UFUNCTION(BlueprintCallable, Category="Asda|Player")
	void EnterDeadPoseInstant();

	// ═══ ★ ОБЕСЦВЕЧИВАНИЕ СЦЕНЫ НА СМЕРТИ (просьба юзера 2026-08-03) ═══════════════════════════════
	//   Чёрно-белым становится ТОЛЬКО 3D-сцена: пост-обработка камеры (ColorSaturation) применяется до
	//   композита UMG, поэтому окно выбора воскрешения остаётся ЦВЕТНЫМ — ровно как просил юзер.
	//   Включается по 4053 (своя смерть), гаснет по 4055/4056 (воскрешение). Blend — плавный.
	//   ⚠ В срезе декомпила клиента механизм обесцвечивания не найден (ни шейдера, ни фуллскрин-квада),
	//     поэтому это НЕ реконструкция кода оригинала, а реализация наблюдаемого поведения по ТЗ юзера.
	UFUNCTION(BlueprintCallable, Category="Asda|Player")
	void SetDeathDesaturation(bool bOn);
	// Длительность перехода цвет↔ч/б, с.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Player")
	float DeathDesatBlendSec = 0.6f;
	// Текущая доля обесцвечивания 0..1 (1 = полностью ч/б).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Player")
	float DeathDesatCur = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Player")
	bool bDeathDesatOn = false;
	// Служебный unbound-объём пост-обработки (Priority 10000) — гарантированное ч/б поверх PP_<map>.
	UPROPERTY(Transient, BlueprintReadOnly, Category="Asda|Player")
	TWeakObjectPtr<class APostProcessVolume> DeathPPV;
private:
	void ApplyDeathDesaturation();   // записать текущую долю в пост-обработку камеры
public:

	virtual void Tick(float DeltaSeconds) override;
	// ★ Уничтожение приаттаченных эффектов персонажа (крылья/кольцо соулмейта): UE при Destroy актора
	//   дочерние АКТОРЫ не убивает — они просто отцепляются и остаются висеть в мире (юзер 2026-07-30:
	//   «когда люди уходят/телепортируются, крылья остаются»).
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// ★ ЗАМАХ обычной атаки (реверс Char_LoadMotionAnim@0x9B9890: наборы ch4_{m,f}_<грп>_normalattackNN;
	//   ассеты anim_20..28). Возвращает длину клипа; по концу вернёт idle, если стоим.
	float PlayAttackClip(int32 WeaponSubType);

	// ★ Звук ЗАМАХА (не попадания). Эталон `Player_ProcessAttackTick` entity_char.c:788-812:
	//   3D у атакующего, один раз на проход клипа, только для ближнего оружия 0..5.
	void PlaySwingSound(int32 WeaponSubType);
	// ★★ ЭТАЛОННАЯ МОДЕЛЬ «ВЗВОД ОДИН РАЗ → ЦИКЛ АТАК» (Player_ProcessAttackTick@0x99F2A0, юзер 2026-08-03:
	//   было «взвод-атака-взвод» — каждый 4028 перезапускал клип с 0 кадра).
	//   Эталон: клип атаки стартует с кадра 0 ТОЛЬКО при входе в бой (интро [0..WindowTime) = «взвод»);
	//   по КАЖДОМУ завершении клипа при живом бое кадр перематывается на WindowTime
	//   (CObject_SetField13(track6, this+47749): entity_char.c:1101-1107 ближний / 1390-1392 лук /
	//   1611 баллиста) — интро не переигрывается. Повторный вызов при идущем цикле = гейт `field_8 != 5`
	//   (Recv_CastState, network.c:42051): клип НЕ трогается, продлевается только удержание.
	//   HoldSec <= 0 — держать цикл до StopAttackAnim (свой перс: эталон 52121==100, без таймаута);
	//   HoldSec > 0  — удержание по времени (сосед: обновляется каждым пакетом 4029).
	//   Возврат: длина клипа при первом входе («взвод»), 0 — цикл уже идёт (перезапуска не было).
	float StartAttackSwing(int32 WeaponSubType, float HoldSec);
	// ★ Ограничить удержание идущего цикла (смена цели в бою: эталон переписывает state=5 ПРЯМОЙ записью,
	//   клип продолжает цикл — Player_UpdateAutoActionState, entity_char.c:24048). Даём циклу конечную
	//   отсрочку: возобновление атаки по новой цели подхватит его БЕЗ повторного «взвода», иначе он
	//   штатно доиграет и выйдет в стойку. Цикла нет — ничего не делает.
	void HoldAttackLoop(float Sec);
	// ═══ ★★★ ЗАМАХ СВОЕГО ПЕРСОНАЖА ПО СЕРВЕРНЫМ ЧАСАМ (15.10.2026, `wave_PORT_attack_rhythm.md`) ═══════════════
	//   ГДЕ В ЗАМАХЕ УХОДИТ УДАР (эталон, локальный тик атаки `Skill_SpawnHitFeedback@0x99B0F0` — имя ВРЁТ):
	//   • ближнее оружие 0..5 (skill_buff.c:744-818): каждый переход порога `петля·W + B[шаг]` двигает шаг; 4028 уходит,
	//     когда новый шаг ЧЁТНЫЙ (`if (шаг % 2) goto …`), то есть на кадрах B[1], B[3], B[5]…; на конце клипа — ещё один,
	//     если число шагов нечётное (:804). Меч/копьё (6 шагов) — три удара за проход 3.0 с, двуручный/посох (4) — два;
	//   • лук 8/10 (:1054-1080), арбалет 9 (:1244-1270), баллиста 11 (:1350-1380) — один 4028 на КОНЦЕ прохода клипа.
	//   Порт прежде слал один 4028 на проход у ВСЕХ: ближний бой выходил в 2–3 раза реже сервера, лук/арбалет — на 44–48 %
	//   чаще (каждый второй выстрел отбрасывался). Теперь точки отправки — эталонные, а скорость клипа на каждом отрезке
	//   «точка → точка» подгоняется так, чтобы следующая точка пришла ровно к серверному «можно»
	//   (`UAsdaAttackRhythmSubsystem`): анимация, звук замаха, выстрел и отправка идут по одной серверной длительности.
	//   Соседей это не касается — их замах держат их пакеты 4029.
	//   ★ [ОРИГИНАЛ] Отступления от оригинала, помеченные ревизией (НИЗК-6): (1) темп клипа своего замаха задаёт сервер, а не
	//   `(M+1)·dt` оригинала: ближний бой играет на ~11 % быстрее (проход 3.0 с при трёх ударах по 0.93 с), дальнобойный —
	//   медленнее: у оригинала по декомпилу и длинам клипов проход лука/арбалета 0.93/0.73 с при серверных 1440/1080, то есть
	//   оригинал сам стрелял чаще сервера и терял выстрелы (живьём не проверено — оригинал не запускаем); (2) «хвостовые» 4028
	//   петли 1 не воспроизводятся (см. `ComputeAttackSendPoints`); (3) удар уходит с запасом и к абсолютному дедлайну
	//   (`UAsdaAttackRhythmSubsystem`), а не в кадре тика замаха. Всё — исправления дефектов оригинала (`fix-original-defects`).
	// Свой персонаж и часы включены (`asda.Combat.ServerRhythm`, ритм от анимации).
	bool IsServerRhythmOwner() const;
	// Пересчитать скорость клипа и таймеры на отрезок «текущая позиция → следующая точка отправки». Зовут: «взвод»,
	//   каждая точка отправки, конец прохода, 4026 новой серии при идущем цикле. Не свой персонаж — ничего не делает.
	void BeginSendSegment(const TCHAR* Why);
	// Заявка навыка ушла (эталон: состояние 25 — тик атаки остановлен): снять цикл замаха и его таймеры, картинку не трогать
	//   (клип навыка придёт с 5257). Возобновит замах боевой тик контроллера, когда навык освободится и часы позволят.
	void SuspendAttackLoopForSkill();
	// Цикл замаха жив (идёт «взвод» или проход).
	bool IsAttackLoopAlive() const { return AttackLoopClipIdx >= 0; }
	// Клип-время от кадра 0 до ПЕРВОЙ точки отправки нового замаха этим оружием (0 — клипа нет).
	float FirstSendSegmentClipSec(int32 WeaponSubType);
	// Точки отправки текущего цикла (клип-секунды; первая петля и последующие) — для самотеста.
	const TArray<float>& GetAttackSendPoints(bool bFirstPass) const { return bFirstPass ? AttackSendPtsFirst : AttackSendPtsLoop; }
	bool IsAttackFirstPass() const { return bAttackFirstPass; }
	// Скорость клипа замаха по серверным часам (0 — не задана, действует обычный темп).
	float GetAttackSyncRate() const { return AttackSyncRate; }
	// ★ 15.10.2026 (волна mobpos_swing, задача 2): скорости клипа всех отрезков текущей серии замаха по порядку (с «взвода»)
	//   — для самотеста ровности темпа; сбрасывается при «взводе».
	const TArray<float>& GetAttackSegmentRates() const { return AttackSegRates; }
	// Текущий отрезок первого прохода замедлен до темпа цикла (ровный темп серии, `asda.Combat.EvenSwingTempo`).
	bool IsAttackSegmentEvened() const { return bAttackSegEvened; }
	// Реальная длительность, на которую спланирован текущий отрезок замаха (клип-расстояние / скорость), сек.
	float GetAttackSegmentPlannedSec() const { return AttackSegPlannedS; }
	// Точка перемотки цикла (клип-секунды) — для самотеста.
	float GetAttackLoopRewindSec() const { return AttackLoopRewindS; }
	// ★ Ревизия СРЕДН-5: через сколько РЕАЛЬНЫХ секунд замах дойдёт до K-го (с 0) СЛЕДУЮЩЕГО кадра удара от текущей позиции
	//   клипа (с переходом через конец прохода). Эталон снимает результат очереди уронов на пороге шага, то есть цифры ответа
	//   ложатся на ближайшие кадры ударов, а не все разом. −1 — цикла нет/кадров нет.
	float UpcomingHitDelaySec(int32 K) const;
	// ═══ ★★★ 17.10.2026 СИНХРОНИЗАЦИЯ БОЯ, Ф2 — ЗАМАХ СОСЕДА ОТ ПАКЕТОВ (`Reports/combat_sync_2026-10-17/03_DESIGN.md` §4) ═══
	//   [ПОРТ] Один ПРИНЯТЫЙ 4029 соседа — один ОТРЕЗОК клипа «точка отправки → точка отправки» (ровно то, что атакующий играет
	//   на один свой 4028: у дальнобойного — проход клипа, у ближнего — два удара; эталонные точки — `ComputeAttackSendPoints`).
	//   Не в замахе — «взвод» с кадра 0 (эталон: 4029 вне серии ставит состояние 5, network.c:42070–42081); в замахе — отрезок
	//   встаёт в очередь и начнётся по окончании текущего. Темп отрезков — номинальный темп × коэффициент по среднему интервалу
	//   прихода 4029 этого соседа (выборка = естественная длительность отрезка прошлого 4029 / интервал), зажат в 0.8…1.25 (тот же
	//   подход, что `BeginSendSegment` у атакующего, но источник интервала — моменты прихода пакетов). Отрезок без пакета
	//   («проход без ответа») доигрывается тем же темпом без цифр; тишина дольше «отрезок + 0.5 с» после последнего — запасной
	//   конец серии (основной — отказы 4029, `UAsdaWorldSubsystem::StopPeerAttackSeries`). Ключ отката —
	//   `asda.Combat.SwingPlanFromPackets 0` (замах соседа по времени, как после Ф1).
	// План отрезка, взведённого принятым 4029: кадры удара этого отрезка в реальных секундах от «сейчас».
	struct FAsdaPeerSwingPlan
	{
		TArray<float> HitDelaysS;   // через сколько реальных секунд — кадры удара отрезка по порядку (у дальнобойного — один, выстрел)
		int32 QueuedAhead = 0;      // 0 — отрезок идёт сейчас; N — начнётся после N отрезков
		bool bStarted = false;      // этим пакетом начат замах («взвод» с кадра 0)
		float SegDurS = 0.f;        // длительность отрезка (реальные с) по его темпу
		float StartInS = 0.f;       // через сколько реальных секунд отрезок начнётся (0 — уже идёт)
	};
	// Принять 4029 соседа (зовёт мир, `OnCombatAnim`): старт/очередь отрезка, подгонка темпа, план кадров удара отрезка.
	//   ★ 17.10.2026 (ревизия Ф2, ВАЖНО 1) `bNewSeries` — пакет начал НОВУЮ серию соседа при идущем цикле (4029 вне серии, смена
	//   цели): [ОРИГИНАЛ] сброс очереди и наполнение заново (network.c:42069–42081) — значения пакета снимаются на БЛИЖАЙШИХ кадрах
	//   удара, поэтому очередь отрезков прошлой серии снимается, а пакету отдаётся текущий отрезок (только кадры впереди; нет их —
	//   следующий отрезок). ★ Тот же приём — у 4204 PvP соседа (Ф4).
	void PeerAcceptSwingPacket(int32 WeaponSubType, bool bRanged, FAsdaPeerSwingPlan& Out, bool bNewSeries = false);
	// Замах этого персонажа ведут пакеты (сосед, Ф2).
	bool IsPeerPacketDriven() const { return bPeerPktDriven; }
	// Ключ `asda.Combat.SwingPlanFromPackets` (1 — Ф2: замах соседа от пакетов и цифры всех ролей по плану кадров).
	static bool IsSwingPlanFromPackets();
	// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 3): ключ `asda.Combat.OwnHitSwingGuard` — таймеры цифр своего замаха не переносятся на
	//   «взвод» следующего замаха (разбор у объявления ключа в .cpp).
	static bool IsOwnHitSwingGuardOn();
	// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 3): номер замаха — растёт на «взводе», выходе из цикла (`StopAttackAnim`) и снятии цикла
	//   навыком (`SuspendAttackLoopForSkill`). Таймер цифры своего удара переставляется только в пределах своего номера.
	int32 GetAttackSwingGen() const { return AttackSwingGen; }
	// Коэффициент темпа соседа по интервалам прихода 4029 (1 — выборок нет), зажат в 0.8…1.25.
	float GetPeerTempoCoef() const;
#if !UE_BUILD_SHIPPING
	// Журнал отрезков соседа (самотест): начало (мир-с), взят пакетом, скорость клипа, позиция начала (клип-с), первый проход.
	//   ★ Ревизия Ф2 (мелочь 4): только в отладочных сборках; кольцо из последних 64 записей (старые вытесняются).
	struct FAsdaPeerSegSeen { double StartS = 0.0; bool bClaimed = false; float Rate = 1.f; float StartClip = 0.f; bool bFirstPass = false; };
	const TArray<FAsdaPeerSegSeen>& GetPeerSegLog() const { return PeerSegLog; }
	// Естественные длительности первых Count отрезков нового замаха этим оружием (реальные с при номинальном темпе) — расписание
	//   самотеста «атакующий шлёт 4028 в точках отправки». Пусто — клипа/цикла нет.
	void NaturalSegmentsS(int32 Count, TArray<float>& Out) const;
#endif
	// ★★★ 17.10.2026 (Ф4, PvP) — КАДРЫ УДАРА, ПРОЙДЕННЫЕ КЛИПОМ ЗАМАХА С ПРОШЛОГО ОПРОСА.
	//   [ОРИГИНАЛ] Удар по персонажу снимает значение очереди на КАЖДОМ кадре удара клипа (`playerArrayPopFront` внутри тика
	//   анимации: свой — `CCharacter_ResolveBasicAttack` entity_char.c:26886, чужой — `CCharacter_AttackAnimTick` :2068). Мир
	//   опрашивает этот метод каждый кадр у атакующих PvP-серии и снимает по значению на каждый пройденный кадр. В `OutSteps` —
	//   шаг комбо каждого кадра (0…N−1; у дальнобойного прохода цикла без кадра — точка перемотки, шаг 0). Опрос с начала
	//   «взвода» включает кадр 0. Возврат — число пройденных кадров.
	int32 ConsumeCrossedHitFrames(TArray<int32>& OutSteps);
	// Снять отсчёт пройденных кадров (следующий опрос начнёт с текущей позиции клипа).
	void ResetHitFrameScan() { HitScanPos = -1.f; }
	// ★★★ 17.10.2026 (ревизия Ф2, мелочь 5 — найдено сценарием «своя роль под серверными часами»): цифры своего замаха ставятся
	//   таймерами на кадры удара по ТЕКУЩЕЙ скорости клипа, а точка отправки (`BeginSendSegment`) меняет скорость следующего отрезка
	//   по серверным часам — таймеры кадров за ней уезжали (живой прогон: до 69 мс). Цель таймера хранится как «кадр клипа» (номер
	//   прохода + позиция), и при смене скорости мир переставляет таймеры (`UAsdaWorldSubsystem::RetimeOwnHits`).
	// Кадр клипа через DelayS реальных секунд по текущей скорости: номер прохода и позиция (false — цикла нет).
	bool ClipTargetFromDelay(float DelayS, int32& OutSerial, float& OutPos) const;
	// Через сколько реальных секунд клип дойдёт до кадра (номер прохода, позиция) по текущей скорости (−1 — цикла нет; 0 — позади).
	float DelayToClipTarget(int32 Serial, float Pos) const;
	// Скорость отрезка своего замаха сменилась (`BeginSendSegment`) — подписчики переставляют таймеры кадров.
	FSimpleMulticastDelegate OnSegmentRateChanged;
	// ★ Ревизия СРЕДН-1: перевзвести 10-секундный боевой покой (эталон `CChar_SetState(…, 20)` по 4205 для любого userIdx).
	void RearmCombatStance() { SetCombatStance(true); }
	static constexpr double kCombatStanceHoldS = 10.0;   // эталон: `> 10000.0` мс (game_main.c:7869, :8119)
	// ═══ ★★★ ЭМОЦИИ (`Reports/char_info_window.md` §7) ═══════════════════════════════════════
	// Эталон `CChar_SetState@0xA13D50` (entity_char.c:19788-19867): состояния **33…46** берут клип из
	//   слота `pModelObj + (state-23)*64` (слоты 10…23), ставят кадр на 0 и взводят `bActionBusy`.
	//   Тик `CCharacter_UpdateActionState@0xA16D20` по концу прохода возвращает состояние 0 — то есть
	//   эмоция ОДНОРАЗОВАЯ, зацикливания нет. Клипы всегда из набора `_00` и от оружия не зависят.
	// В порту клипы импортированы как `anim_86…97` (Tools/export_char_emotions.py + import_char_emotions.py).
	// Возвращает длину клипа (сек); 0 — состояние не эмоция либо клип не найден.
	float PlayEmotionClip(int32 EmoteState);
	// Индекс ассета `anim_NN` для эталонного состояния 33…46 (−1 = состояние не эмоция).
	static int32 EmotionClipIdx(int32 EmoteState);
	// Идёт ли сейчас клип эмоции (гейт: движение/атака его прерывают, как в эталоне).
	bool IsEmotionAnimActive() const;
	// ★★ В3 S7 (12.10.2026): ЕДИНОЕ «ЗАНЯТОЕ» СОСТОЯНИЕ ДЕЙСТВИЯ — номер эталона `+47551` из `BusyAction` (33…46 эмоция — пока
	//   идёт её клип, 40/43 лопата, 60…67 рыбалка, 68 «добыча»), 0 — «занятого» состояния нет. Его читает
	//   `AAsdaPlayerController::KbEtalonActionState` (прежде — три признака и без 68).
	int32 GetBusyActionCode() const;
	// Эталонный `bActionBusy == 1` (`charUpdateActionStateTick@0xA216B0` условие `self[52164] == 1`): оружие за спину, фиджета нет,
	//   гашение оружия рыбалки. Прежде одно и то же условие было собрано из 4 признаков в трёх местах.
	bool IsEtalonActionBusy() const { return GetBusyActionCode() != 0; }
	// Клип персонажа принадлежит действию (эмоция / рыбалка / лопата): перезапуски клипа состояния (смена оружия, конец боевого
	//   цикла, крылья, уборка оружия, конец вставания) его не перебивают. 68 сюда не входит: его клип в порту — покой
	//   (клипа трека 36 нет, PORT_PENDING «Рыбалка, ревизия В4/В5», п.1).
	bool IsActionClipOwned() const { return GetBusyActionCode() != 0 && BusyAction != EAsdaActionState::Mining68; }
	// Номер эмоции 33…46 (без 40/43), −1 — эмоция не стоит (для журнала; клип мог уже кончиться — см. `IsEmotionAnimActive`).
	int32 GetEmotionState() const { return AsdaActionState::IsEmote((int32)BusyAction) ? (int32)BusyAction : -1; }
	// ★ В3 S7: ключ отката `asda.Action.LegacyBusyGates` (1 — гейты «занят» как до 12.10.2026; разбор исправлений — у ключа в .cpp).
	static bool IsLegacyBusyGates();
	// ★★★ ТЕКУЩАЯ ПОЗИЦИЯ КЛИПА АТАКИ, секунды от его начала. Нужна, чтобы планировать показ
	//   удара от того места, где анимация НАХОДИТСЯ СЕЙЧАС, а не от нуля: со второго прохода клип
	//   стартует не с начала, а с точки перемотки (у баллисты — 1.17 с при длине 2.00 с), и кадр
	//   удара приходится на самое начало прохода. Отсчёт от нуля давал отставание больше секунды.
	float GetAttackClipPositionSec() const;
	// ★★★ ПРОЙДЕННАЯ ЧАСТЬ ЗАМАХА В РЕАЛЬНОМ ВРЕМЕНИ = позиция клипа / множитель темпа.
	//   Позиция клипа считается в СОБСТВЕННОМ времени клипа (скорость 1), а тайминги ударов у нас
	//   отмеряются РЕАЛЬНЫМ временем и уже поделены на темп (см. `GetComboTiming`). Смешивать их
	//   нельзя: при темпе 1.3 «осталось до кадра удара» завышалось бы на треть. Одна точка на обоих
	//   потребителей — планирование показа ударов и планирование смерти цели.
	float GetAttackSwingElapsedSec() const;
	// ★★★ МНОЖИТЕЛЬ ТЕМПА АТАКИ ЭТОГО персонажа (состояния 27/174/28/233/247 + надетые предметы).
	//   Считает ЕДИНАЯ точка порта `UAsdaWorldSubsystem::GetAttackTempoMul` — там же весь разбор
	//   эталона и оба исправленных дефекта оригинала. Здесь только удобный доступ от актора.
	//   1.0 — «темп обычный» (мира/подсистемы нет, персонажа нет в реестре, сумма вкладов нулевая).
	float AttackTempoMul() const;
	// ★★★ МНОЖИТЕЛЬ ТЕМПА АНИМАЦИЙ НАВЫКОВ И ЭФФЕКТОВ этого персонажа (эталонное поле
	//   `CCharacter + 0x500`, `animSpeedScale`). Это ВТОРАЯ, ОТДЕЛЬНАЯ от `AttackTempoMul` шкала:
	//   считается по надетым предметам, а состояние 78 их ПЕРЕБИВАЕТ и применяется ТОЛЬКО к
	//   показу навыков — клип применения, пороги фаз COMBO, собственное время эффектов каста.
	//   К замаху базовой атаки она не относится: эталон отдаёт в тик атаки сырой deltaTime.
	//   Весь разбор эталона — у `UAsdaWorldSubsystem::GetAnimSpeedScale`; здесь только доступ.
	float AnimSpeedScaleMul() const;
	// ★ Множитель темпа, ЗАФИКСИРОВАННЫЙ на текущий проход клипа замаха. Именно с этой скоростью
	//   проигрывается клип и именно по ней копится его позиция, поэтому обратный перевод позиции в
	//   реальное время обязан делить на ЭТО число, а не на опрошенный заново «живой» множитель:
	//   иначе наложение или спад баффа посреди замаха давали скачок оценки пройденного времени
	//   (достижимо до ~30 %). Ставится в `PlayWorldClip`, сбрасывается в 1 для всех неатакующих клипов.
	float AttackClipTempo = 1.f;
	// Прервать эмоцию и вернуться в клип текущего состояния (эталон: `CChar_SetState(…, 0|1)`).
	//   ★ 30.10.2026: снимает и позы 47/51 (`LeaveCampPose`) — та же безусловная запись `+47551` любой сменой состояния.
	void StopEmotionClip();
	// ═══ ★★★ ПОЗЫ 47 «ЗАХВАТ ТОЧКИ» И 51 «ПРОИГРАВШИЙ PvP» (30.10.2026, `Reports/faction_select/BATTLE_GAPS_RE.md` §2) ═══
	//   Эталон `CChar_SetState@0xA13D50` case 47/51 (entity_char.c:19869, `LABEL_213`): кадр 0, `animPlayFlag = 1`, `bActionBusy = 1`.
	//   47 → слот 25 → трек 25 `ch4_{m|f}_00_occupa_00_0.jbb` (1.000 с): тик case 14 (entity_char.c:20490) по концу клипа ставит
	//     кадр 0, состояние НЕ меняет — бесконечный цикл; снимает только 6724 sub 0 (если ещё 47) или любая другая запись состояния.
	//   51 → слот 26 → трек 26 `ch4_{m|f}_00_loser_00_0.jbb` (4.267 с): по концу клипа `SetState(0)`, `bActionBusy = 0` — один раз.
	//   Клипы набора `_00` от пола, не от оружия: anim_98 / anim_99 (Tools/campwar_export_clips.py + campwar_import_char_clips.py).
	static constexpr int32 kClipOccupy = 98;   // anim_98 — поза захвата (состояние 47)
	static constexpr int32 kClipLoser  = 99;   // anim_99 — жест поражения (состояние 51)
	// Войти в позу 47 или 51 (`CChar_SetState` / свой `CharFx_AutoSabzil_State`). Возвращает длину клипа (сек); 0 — не поставлена
	//   (не 47/51, нет мира или клипа — тогда состояние не меняется). `Why` — источник (журнал `[действие]`).
	float EnterCampPose(int32 State, const TCHAR* Why);
	// Снять позу, если стоит именно `State` (6724 sub 0: `if (+47551 == 47) SetState(0)`): покой, оружие в руку, клип покоя.
	void LeaveCampPose(int32 State, const TCHAR* Why);
	// Стоящая поза: 47 / 51, 0 — нет.
	int32 GetCampPoseState() const { return (BusyAction == EAsdaActionState::Action47 || BusyAction == EAsdaActionState::Action51) ? (int32)BusyAction : 0; }

	// ═══ ★★★ ПРЫЖОК И ПЛАВАНИЕ — ИЗ ДОНОРА AsdaFableClassic (23.10.2026, решение владельца) ═══════════════════
	//   Реверс `Asda2.exe` донора (только статика): прыжок `0x40E280` (ввод) / `0x4AF1A0` (свой) / `0x4AE120` (чужой),
	//   сеть `0x8C0690(131)` → 4024, приём 4025 действие 131 (`0x7E63CE`); плавание `0x46C6E0` (свой) / `0x46D230` (чужой),
	//   брызги `0x4D33E0`. Сводка — MEMANTO (тег «ДОНОР AsdaFableClassic, Asda2.exe») и `Reports/fable_mounts/`.
	//   [ПОРТ] В эталоне AsdaStory этих механик НЕТ; это перенос из донора. Ключи `asda.Jump.Enable` / `asda.Swim.Enable`.
	//   Клипы набора `_00` донора: anim_235 idlejump01, 236 runjump01, 237 swim01, 238 swimloop01 (export_char_fable_clips.py).
	static constexpr int32 kClipJumpIdle = 235;   // прыжок с места (слот 24 донора)
	static constexpr int32 kClipJumpRun  = 236;   // прыжок с бега (слот 37 донора)
	static constexpr int32 kClipSwimMove = 237;   // плывёт (swim01, слот 39 донора, цикл)
	static constexpr int32 kClipSwimIdle = 238;   // держится на воде (swimloop01, слот 38 донора, цикл)
	// Код действия 4024/4025 прыжка — тот же, что шлёт донор (0x83). Старый сервер (`CharEMotionSync@0x546730`) принимает
	//   любой код 100…400 и рассылает его соседям; особые коды сервера (108, 112/113, 200, 300/301) он не задевает.
	static constexpr int32 kJumpActionCode = 131;
	// Начать прыжок. bFromNet = прыжок чужого по 4025 (пакет не шлём). Возврат: true — прыжок начат.
	//   Клип: стоит → idlejump01, движется → runjump01; движение не прерывается; в воде клип плавания перебивает прыжок
	//   (донор: ветка плавания ставит свой слот в том же кадре — прыжок не виден, время его идёт).
	bool StartJump(bool bFromNet);
	// Идёт прыжок (флаг донора +0x35960).
	bool IsJumpActive() const { return bJumpActive; }
	// Плывёт (флаг донора +0x35961): глубокая вода (земля + 100 ниже поверхности), не верхом.
	bool IsSwimming() const { return bSwimming; }
	// ★ 09.11.2026 [ИСПРАВЛЕНИЕ] (пачка 20, баг 2): плывущий смотрит по ходу и пятясь (ключ `asda.Swim.FaceMoveDir`; тело — _Move.cpp).
	bool SwimFacesMoveDir() const;
	// Транспорт в глубокой воде (флаг донора +0x35962) — держится на воде, брызги транспорта.
	bool IsMountInWater() const { return bMountInWater; }
	// Опрос клавиши прыжка своего персонажа (команда 95 «EXT JUMP», по умолчанию Пробел) — из `TickSwimAndJump` своего персонажа.
	void TickJumpInput(bool bKeyDown, bool bInputAllowed);
	// Игровой ввод для прыжка не заглушён: нет набора в чате/лавке/полях ввода, гачи, рыбалки, модалки, свободной камеры.
	bool IsJumpInputAllowed() const;
	// Самотест: отказ последней попытки прыжка (пусто — прыжок начат).
	const FString& GetLastJumpRefusal() const { return LastJumpRefusal; }
	// ★ 24.10.2026 [ПОРТ] (владелец: «прыжок поднимает и всё над головой»): на сколько клип прыжка поднял тело над точкой
	//   актора (UE-единицы, ≥ 0; 0 — на земле / не прыгает / ключ `asda.Jump.OverheadFollow 0`). Прыжок донора — без физики:
	//   высоту даёт кость корня клипа, сам актор (а с ним плашка, облачко, корона) остаётся на земле. Эту величину
	//   прибавляют к высоте всего, что висит над головой: плашка (`UAsdaNameplateComponent::GetPlateWorldPoint`), облачко
	//   чата, надпись хранителя души, корона владельца добычи, сам хранитель. Плавание отдельной поправки не требует —
	//   там меняется высота самого актора (`StandZ`).
	float GetOverheadLiftZ() const { return JumpLiftZ; }

	// ═══ ★★★ ХОДЬБА ВПРИСЯДКУ И ВИД НЕВИДИМОСТИ (24.10.2026, решение владельца) ══════════════════════════════════
	//   Донор AsdaFableClassic (`Asda2.exe`, статика): клип `ch4_{m|f}_05_hidewalk_01` грузится в слот 40 модели при любом
	//   оружии и играет ВМЕСТО клипа хода (слот 5) при невидимости — состояния 112 «Скрытность» / 186 «Быстрая маскировка»
	//   (`sub_4C2440`; `0x482500`/`0x4A3AF0`/`0x4A8130`), темп: время клипа += 2.857·скорость·dt·(длина 40 / длина 5).
	//   Эталон AsdaStory при тех же состояниях идёт шагом walk01 (слот 4, `skillHasBuffType112or186@0xA386F0`).
	//   [ПОРТ] Владелец: присед — и по Ctrl (одиночное нажатие вкл/выкл), и при ЛЮБОЙ невидимости (112/186, GM-скрытие),
	//   свой и у соседей; при Ctrl ход ×0.5 (сервер, патч P80: статус хода 4006 @+3 = 4 делит скорость, как скрытность).
	//   Соседи узнают присед из статуса в 4007 (сервер эхом шлёт тело 4006), невидимость — из своих пакетов эффектов.
	//   Стойки вприсядку у донора нет — в покое обычный клип покоя. Клип — anim_239 (export_char_fable_clips.py crouch).
	static constexpr int32 kClipCrouchWalk = 239;     // ходьба вприсядку (донор hidewalk_01, слот 40)
	static constexpr int32 kCrouchMoveStatus = 4;     // статус хода 4006 @+3 в приседе (обычный бег — 2)
	// renderId цели последнего своего 4006 (@+25; −1 — без цели). Пишет `UAsdaNetSubsystem::SendMove4006`; читает досылка
	//   статуса приседа, чтобы подход к цели не терял её.
	int32 LastSent4006TargetRid = -1;
	// Свой присед по Ctrl: включить/выключить. Возврат — присед после вызова; отказ — причина в `GetLastCrouchRefusal`.
	bool SetCrouch(bool bOn, const TCHAR* Why);
	// Свой присед включён (Ctrl).
	bool IsCrouchOn() const { return bCrouchOwn; }
	// Присед соседа по статусу хода 4007 (4 — присед, иначе — обычный ход). Ставит мир по каждому гранту соседа.
	void SetNetCrouch(bool bOn);
	// Сосед идёт вприсядку (последний 4007 со статусом 4).
	bool IsNetCrouch() const { return bCrouchNet; }
	// Невидимость навыка (состояние 112 или 186 у этого персонажа). Ставит `UAsdaStealthSubsystem` по списку эффектов.
	void SetSkillStealth(bool bOn);
	// У персонажа есть состояние 112/186 (эталонное `skillHasBuffType112or186`).
	bool IsSkillStealth() const { return bSkillStealth; }
	// Клип хода сейчас — ходьба вприсядку (Ctrl, присед соседа или невидимость при ключе `asda.Crouch.HideAnim`).
	bool IsCrouchWalkShown() const;
	// Статус хода для 4006 своего персонажа: 4 в приседе (ключ `asda.Crouch.NetStatus`), иначе 2 (эталон).
	int32 GetMoveStatusFor4006() const;
	// Опрос Ctrl своего персонажа (из `TickSwimAndJump`): переключение на ОТПУСКАНИИ, если, пока Ctrl был зажат, не нажималась
	//   ни одна другая клавиша или кнопка мыши (Ctrl+N/Y/W/P/C/S чата и прочие сочетания присед не трогают).
	void TickCrouchInput(bool bCtrlDown, bool bOtherKeyDown, bool bInputAllowed);
	// Самотест: причина последнего отказа приседа (пусто — принят).
	const FString& GetLastCrouchRefusal() const { return LastCrouchRefusal; }
	// Темп клипа ходьбы вприсядку по формуле донора: (скорость / 0.35) · (длина anim_239 / длина бега набора); ключ
	//   `asda.Crouch.ClipTempo 0` — темп 1. ★ 24.10.2026: со знаком `CrouchClipTimeSign` (пятясь — отрицательный, клип назад).
	float CrouchClipRate() const;
	// ★ 24.10.2026 [ПОРТ] направление времени клипа приседа: −1 — идёт назад относительно курса (проекция направления хода на
	//   логический курс < −0.17, то есть угол > ~100°: S, S+A/S+D), иначе +1 (вперёд, вбок, стоит). У оригинала обычный ход
	//   назад — свой клип back01 (трек 24 вперёд), у приседа донора такого клипа нет. Ключ `asda.Crouch.BackwardReverse 0` — всегда +1.
	float CrouchClipTimeSign() const;
	// ★ Вид невидимости (эталон `CharManager_UpdateAll@0x9D56C0` / `charEntityUpdateTick@0xA1ED30`, поле α +6329):
	//   α модели 0…1 и «рисуется ли». Ставит `UAsdaStealthSubsystem` каждый кадр. α < 1 — третья причина подмены слотов на
	//   translucent-MID (после «души» и GM-скрытия); bRender == false — актор и подвесные эффекты скрыты (выбор недоступен).
	void SetStealthVisual(float Alpha, bool bRender);
	// Текущая α невидимости (1 — обычный вид).
	float GetStealthAlpha() const { return StealthAlpha; }
	// Невидимость не прячет персонажа целиком (эталонный `RenderCtx_SetEnable(…, 1)`).
	bool IsStealthRendered() const { return bStealthRendered; }
	// Причина «невидимость» подмены слотов активна (α < 1 и ключ `asda.Stealth.Visual`).
	bool IsStealthTranslucencyActive() const;

	// ═══ ★★★ РЫБАЛКА: ДЕЙСТВИЯ 60…67, УДОЧКА, РЫБА НА КРЮЧКЕ (волна В5, 08.10.2026) ═══════════════
	//   Реверс: `Reports/owner_1008/fishing_parts/world_anim.md` §2-§3, отчёт `fishing_impl_W5.md`.
	//   Действие: `CChar_SetState@0xA13D50` case 60…67 (game.c:77075-77137) — клип трека `action−33`
	//   (`Fs_ch_{m|f}_ani_*`, у нас `anim_225…232` = `kFishClipBase + action−60`, только по полу), кадр 0,
	//   `bActionBusy = 1` (⇒ оружие за спину, `charUpdateActionStateTick@0xA216B0` условие `self[52164]==1`).
	//   Конец клипа: `CCharacter_UpdateActionState@0xA16D20` case 16…23 (game.c:77714-77743): кадр клипа → 0
	//   (клип повторяется), затем свой → `Fishing_OnStateMessage` (подсистема рыбалки, `OnFishingClipEnd`),
	//   чужой → 60/61→61, 63/64→61, 65/66 → флаг [15036]=0 и состояние 0, 62/67 — без перехода (клип идёт по кругу).
	static constexpr int32 kFishClipBase = 225;   // ассет anim_225 = действие 60 (импорт волны В5а)
	// Поставить действие рыбалки 60…67 (bSelf — свой персонаж: конец клипа идёт в автомат рыбалки). 0 — выйти
	//   в покой (`CChar_SetState(…, 0)`). Возвращает длину клипа (0 — действие не рыбалки / клипа нет / отклонено).
	float PlayFishAction(int32 Action, bool bSelf);
	// Снять действие рыбалки и вернуть клип текущего состояния (переход в состояние 0 / перебивание движением).
	void StopFishAction(const TCHAR* Why = TEXT(""));
	// Идёт ли действие рыбалки (эталонное состояние +47551 в 60…67).
	bool IsFishActionActive() const { return AsdaActionState::IsFish((int32)BusyAction); }   // ★ В3 S7: из единого поля
	// ★ В45fix2 (ревизия В4/В5, МЕЛК-3, 09.10.2026): `CChar_SetState(…, 9)` «встаёт» — 4025 109 (`Recv_CharSpawnFx`
	//   g:551808-551809). Состояние 9 переписывает ЛЮБОЕ прежнее (g:76456), ставит кадр хвоста клипа sitdown01 (трек 3,
	//   3504/4004 мс по полу, g:76869-76878), `bActionBusy = 0`; по концу клипа — состояние 0. Действие рыбалки 60…67 при
	//   этом снимается, а удочку (флаг [15036]) эталон снимает только в состояниях ≤ 2/5/6 (g:607922) — т.е. по концу вставания.
	void EnterStandUpState();
	// ★ В45fix2 (ревизия В4/В5, МЕЛК-5, 09.10.2026): `CChar_SetState(…, 68)` — 4025 130 (`Recv_CharSpawnFx` g:551554-551555),
	//   состояние «добыча» (minewar: трек 36 `Dr_ch_ani_{m|f}_idle_00.jbb`, `Char_LoadMotionAnim` case 25; петля со звуком
	//   `S_e_minewar_mining_01.wav`, `CCharacter_UpdateActionState` case 25 g:77746-77754; `bActionBusy = 1`, g:77139-77144).
	//   Система добычи в порту НЕ портирована (клипа трека 36 нет) — сделан минимум 1:1 для рыбалки: 68 переписывает действие
	//   60…67 / эмоцию / сидение / ход, держит признак «занят» (оружие скрыто при флаге), флаг [15036] и удочку НЕ снимает
	//   (68 ∉ ≤ 2/5/6). Вместо клипа добычи — клип покоя (PORT_PENDING «Рыбалка, ревизия В4/В5», п.1).
	void EnterEtalonState68();
	// Стоит ли эталонное состояние 68 «добыча» (до любой следующей смены состояния).
	bool IsEtalonState68() const { return BusyAction == EAsdaActionState::Mining68; }   // ★ В3 S7: из единого поля
	// ★ Решение владельца 09.10.2026 (п. 5): эталонное состояние 40 (лопата) / 43 (авто-лопата) — `CChar_SetState` ставит
	//   `bActionBusy = 1`, оружие уходит за спину (как у эмоций 33…46). Свой — `Item_UseShovelingTool`, соседи — 4025 110/114.
	//   ★ 10.10.2026: клип копания, удары, инструмент на `8_a1`, стартовый эффект; снимает любая смена состояния и конец клипа. Разбор — в .cpp.
	// ★ 10.10.2026 (копание): `Src` — откуда пришло состояние (стартовый эффект и правило меша инструмента, см. `EAsdaShovelEntry`),
	//   `PrevTimerMode` — только для своего: режим таймера действия ДО этого применения (33/34/0, `sub_928DED(g_pActionTimer)`),
	//   по нему эталон решает, менять ли меш инструмента.
	void EnterEtalonShovelState(int32 State, EAsdaShovelEntry Src = EAsdaShovelEntry::Peer4025, int32 PrevTimerMode = 0);
	// Текущее состояние лопаты: 40 / 43, 0 — нет.
	int32 GetEtalonShovelState() const { return AsdaActionState::IsShovel((int32)BusyAction) ? (int32)BusyAction : 0; }   // ★ В3 S7: из единого поля
	// ★ 10.10.2026 (копание): индексы клипов копания на скелете превью (`digging_export_clips.py`): трек 17 / трек 20 эталона.
	static constexpr int32 kShovelClipIdx40 = 233;   // anim_233 = ch4_98_Shoveling_{m|f}.jbb (лопата, состояние 40)
	static constexpr int32 kShovelClipIdx43 = 234;   // anim_234 = ch4_98_Shoveling_{m|f}_01.jbb (авто-лопата, состояние 43)
	// ★ 10.10.2026 (копание): ИТОГ КОПКИ 5430 (`network.c:1530-1592`, case 0x1536): гасит эффект удара, эффект и звук итога
	//   (`c-suc-a_00` + `S_e_shovelling02.wav` / `c-fail-a_00` + `S_e_shovelling03.wav`), снимает «занят». Нотис 9003/9004 —
	//   у вызывающего (только свой). Разбор — в .cpp.
	void OnMiningResult5430(bool bSuccess);
	// ★ 10.10.2026 (копание), диагностика самотеста: ударов в текущем/последнем проходе (`digStrokeCount` у 40 /
	//   `mineStrokeCount` у 43), время трека клипа копания (мс), всего ударов с начала жизни актора.
	int32 GetShovelStrokeCount() const { return GetEtalonShovelState() == 43 ? ShovelMineStroke : ShovelDigStroke; }
	float GetShovelTrackMs() const { return ShovelTrackMs; }
	int32 GetShovelStrikeTotal() const { return ShovelStrikeTotal; }
	// ★ 10.10.2026 (копание), диагностика: моменты ударов текущего/последнего прохода — время с входа в 40/43 по часам самого
	//   персонажа (сумма DeltaSeconds его тиков, как растёт трек), с. Не зависит от того, когда снаружи на это посмотрели.
	const TArray<float>& GetShovelStrikeLog() const { return ShovelStrikeLog; }
	// ★ 10.10.2026 (копание), диагностика: компонент меша инструмента (nullptr — ещё не создан) и признак «загружен авто-меш» ([216]).
	class UStaticMeshComponent* GetShovelToolComp() const { return ShovelToolComp; }
	bool IsShovelToolAuto() const { return bShovelToolAuto; }
	// ★ 10.10.2026 (копание), диагностика: живой стартовый эффект (`worldMatrix[0]`) и эффект удара (`worldMatrix[1]`).
	AActor* GetShovelStartFx() const { return ShovelStartFx.Get(); }
	AActor* GetShovelHitFx() const { return ShovelHitFx.Get(); }
	// ★ 10.10.2026 (копание): порог удара i (0…4), мс — `digStrokeThresholds`/`mineStrokeThresholds` (одна строка combo time).
	int32 GetShovelThresholdMs(int32 I) const { return (I >= 0 && I < 5) ? ShovelThrMs[I] : 0; }
	// ★ 09.10.2026 (автолопата, ревизия six_fixes МЕЛК-1): СБРОС СЕССИИ при живом акторе — снять 40/43 вместе с таймером конца
	//   клипа (иначе конец клипа отправил бы 5428 уже в новую сессию) и вернуть оружие из-за спины. Сети нет. [ПОРТ] оригинал
	//   при разрыве закрывает клиент — сверять не с чем. `Why` — для журнала. Ничего не делает, если лопаты нет.
	void AbortEtalonShovelState(const TCHAR* Why);
	// ★ Ревизия six_items СРЕДН-1 (09.10.2026): длительность состояния лопаты 40/43, с — конец клипа копания эталона
	//   (`CCharacter_UpdateActionState@0xA16D20` case 7/10); разбор и источники — в .cpp. 0 — состояние не 40/43.
	float ShovelStateSeconds(int32 State) const;
	// ★ six_items (09.10.2026): ключ `asda.Shovel.NetSync` — сетевая часть копания своего (4024 110/114 на старте, 5428 в конце).
	static bool IsShovelNetSync();
	// Текущее действие рыбалки (0 — нет).
	int32 GetFishAction() const { return IsFishActionActive() ? (int32)BusyAction : 0; }   // ★ В3 S7: из единого поля
	// Действие поставлено как «своё» (конец клипа уходит в автомат рыбалки).
	bool IsFishActionSelf() const { return bFishActionSelf; }
	// Поля персонажа `CCharacter[1].gap_A42` для показа удочки/рыбы (свой — из подсистемы рыбалки каждый
	//   кадр, чужой — из 6173 и концов клипов): [15036] флаг «рыбачит», [15066] рыба есть, [15068] размер %, [15076] код.
	void SetFishFields(bool bFlag, bool bHasFish, int32 SizePct, int32 Code);
	// [15036] флаг «рыбачит» (+226060).
	bool GetFishFlag() const { return bFishFlag; }
	// [15066] рыба на крючке.
	bool GetFishHasFish() const { return bFishHasFish; }
	// [15068] размер, %.
	int32 GetFishSizePct() const { return FishSizePct; }
	// [15076] FishCode / id улова.
	int32 GetFishCode() const { return FishCode; }
	// Сменить «свой/чужой» для показа удочки (свой — по флагу, чужой — по действию и удочке; эталон 55024/55827).
	void SetFishSelfOwner(bool bSelf) { bFishOwnerSelf = bSelf; }
	// Код надетого оружия (предмет слота 9; эталон `char+0x835` = `gap_7FE[49]`) — пишет `SetWeaponFromItem`.
	int32 GetEquippedWeaponItemId() const { return EquippedWeaponItemId; }
	// ── диагностика / самотест `asda.Fish.AnimSelfTest` ──
	// Компонент удочки рыбалки (nullptr — не вставлена).
	class USkeletalMeshComponent* GetFishRodComp() const { return FishRodComp; }
	// Компонент рыбы на крючке (nullptr — не вставлена).
	class USkeletalMeshComponent* GetFishHookComp() const { return FishHookComp; }
	// Текущий клип удочки (= действие − 60; −1 — удочки нет).
	int32 GetFishRodClip() const { return FishRodComp ? FishRodClip : -1; }
	// Имя меша удочки (для журнала/самотеста).
	const FString& GetFishRodMeshName() const { return FishRodMeshName; }
	// Видна ли рыба на крючке сейчас.
	bool IsFishHookVisible() const;
	// Имя ассета, который РЕАЛЬНО крутит скелет персонажа (как сторож эмоций).
	FString GetPlayingClipName() const;
	// Скрыты ли оружие и щит на время рыбалки (эталон `char[52136] = 0` — признак порта; реальную скрытость частей
	//   самотест проверяет по компонентам и акторам с метками).
	bool IsFishWeaponHidden() const { return bFishWeaponHidden; }
	// Вставлена ли удочка ([15037]; true и тогда, когда меша нет).
	bool IsFishRodInserted() const { return bFishRodInserted; }
	// Сколько раз заспавнен эффект fish_end у ЧУЖОГО (самотест: защита от повтора [15100]).
	int32 GetFishPeerFxCount() const { return FishPeerFxCount; }

	// Длина атак-клипа для оружия (без проигрывания) — период автоатаки (эталон: период = клип 6).
	float AttackClipLen(int32 WeaponSubType);
	// Индекс атак-ассета по подтипу оружия: 0/4→20, 1→21, 3→22, 8/10→23, 9→24, 2→25, 5→26, 11→27, 6→28.
	static int32 AttackClipIdx(int32 WeaponSubType);
	// ★ Является ли ассет `anim_NN` клипом АТАКИ. Нужен `PlayWorldClip`: скорость воспроизведения,
	//   равную множителю темпа, получают ТОЛЬКО клипы атаки — покой, движение, фиджеты, эмоции,
	//   «сесть/встать» и сидячие клипы транспорта играют со скоростью 1, как в эталоне (там темпом
	//   умножается шаг времени одних лишь часов замаха). Множество строится из `AttackClipIdx`,
	//   чтобы список индексов жил в ОДНОМ месте.
	static bool IsAttackClipAsset(int32 AssetIdx);

	// ── ★ НАБОР МОУШЕНОВ ПО ОРУЖИЮ (CCharacter_LoadMotionAnims@0x9B84D0) ───────────────────────────
	// Эталон: CCharacter_SetupModelFromItem@0x9E97A0 кладёт ItemRecord[33] (=ItemSubType, оффсет 132)
	//   в CCharacter::equippedWeaponType и перезагружает КАТЕГОРИИ 0 (покой), 2 (движение), 3 (атака)
	//   с этим номером. Внутри LoadMotionAnims он превращается в суффикс набора ch4_{m|f}_{SS}:
	//     0,4→_01  1→_01(атака 03)  2→_05  3→_02  5→_06  6→_09  8,10→_03  9→_04  11→_08  99/нет→_00
	//   Плюс спец-правило по ПРЕДМЕТУ: ItemSubType==10 && ItemVariant==1 (удочки) → класс 2 (набор _05);
	//   ItemVariant==2 (буры) → класс 10 (набор _03). Обработано в SetWeaponFromItem.
	// Текущее оружие персонажа (ItemSubType). 99 = нет оружия/кулаки → набор _00.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Anim")
	int32 EquippedWeaponSubType = 99;
	// Установить оружие по записи предмета (учитывает ItemVariant-исключения) и перезапустить клип состояния.
	void SetWeaponFromItem(int32 ItemId, class UAsdaItemTableSubsystem* Items);
	// Установить класс анимаций напрямую (уже разрешённый ItemSubType); 99 = безоружный.
	void SetWeaponSubType(int32 InSubType);
	// Номер набора движений (0,1,2,3,4,5,6,8,9) по ItemSubType оружия.
	static int32 MotionSetOf(int32 InSubType);
	// Действующий набор с учётом «оружие убрано» (город/седло) — эталон charUpdateActionStateTick@0xA216B0.
	int32 CurMotionSet() const { return bWeaponMotionHidden ? 0 : MotionSetOf(EquippedWeaponSubType); }
	// Индекс ассета anim_NN для слота набора (см. таблицу в .cpp).
	int32 MotionClipIdx(EAsdaMotionSlot Slot) const;

	// ★ «ОРУЖИЕ УБРАНО» (эталон charUpdateActionStateTick@0xA216B0 / render_core.c:3410, поле char+226184,
	//   по сети — gap_A42[15160]): взводится, если (а) тайл под ногами в БЕЗОПАСНОЙ (городской) зоне —
	//   бит 0x80 атрибута тайла .TER, (б) персонаж В СЕДЛЕ, (в) активна трансформация (sub_9231EA) или
	//   режим self[52164]==1 (в порту таких систем нет). На переходе клиент ВЫЗЫВАЕТ SetupModelFromItem
	//   заново → набор моушенов переключается на _00 (безоружный) и обратно ПРЯМО НА ХОДУ.
	//   ★ 2026-08-01: по ЭТОМУ ЖЕ флагу МЕШ оружия и его свечение переезжают на кость спины "2_a1"
	//   (щит — "_2_a1") с эталонными поправками — UAsdaCharAssembly::WeaponAttachInfo/WeaponGlowAttachRaw.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Anim")
	bool bWeaponMotionHidden = false;
	// Пересадить меш оружия и свечение заточки «в руку ↔ за спину» по текущему bWeaponMotionHidden.
	//   Зовётся при смене флага (тик), после пересборки внешности и после пересоздания свечения.
	void RefreshWeaponStow();
	// Имя кости, на которой СЕЙЧАС висит меш оружия (для диагностики/консольной проверки).
	FName CurrentWeaponBone() const;
	// Выключатель правила «в городе анимации безоружные» (для отладки в редакторе).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Anim")
	bool bPeaceZoneUnarmedMotion = true;
	// Невидимость навыка (эталон skillHasBuffType112or186 — состояния 112/186; «slow» — миснейм прежнего комментария):
	//   движение идёт слотом walk01, а не run01. ★ 24.10.2026 ставит `SetSkillStealth` (подсистема UAsdaStealthSubsystem
	//   по списку эффектов). При ключе `asda.Crouch.HideAnim 1` (умолч.) клип хода — присед донора (`IsCrouchWalkShown`).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Anim")
	bool bSlowWalkBuff = false;
	// Замах активен: SetAnimState не трогает клип (гейт state-13/14 эталона — движение не перебивает атаку).
	bool IsAttackAnimActive() const;
	double AttackAnimUntilS = 0.0;
	// Разворот меша по game-направлению (в бою — лицом к цели). Публично: нужен контроллеру.
	//   ★ 22.10.2026 (плавность, `World/AsdaBlendAnimInstance.h`): ставит ЛОГИЧЕСКИЙ курс сразу (его читают `GetFacingYawUE`,
	//   `GetFacingDirGame` — пакеты, мини-карта, выстрелы), а видимая модель доворачивается в тике со скоростью
	//   `asda.Anim.TurnDegPerSec` ([ОРИГИНАЛ] 600 °/с, `objAI_FaceTargetByUserIdx@0xA23350`). Ключ 0 — мгновенно, как прежде.
	void FaceGameDir(const FVector2D& DirGame);
	// ★ 22.10.2026: повернуть СРАЗУ (логический и видимый курс) — перенос сервером 4056 (`NetSnapHard`) и самотесты. Спавн снапает
	//   курс сам (первые 0,25 с жизни актора, `FaceGameDir`).
	void SnapFacingToLogical();
	// ★ 22.10.2026: ВИДИМЫЙ курс модели (UE-градусы, без доворота `MeshYawOffset`) — то, что на экране.
	float GetVisualFacingYawUE() const;
	// ★ 22.10.2026: текущий «хвост» сетевой поправки (смещение тела от актора, UE-единицы; ноль — хвоста нет).
	FVector GetVisualTail() const { return VisualTail; }
	// ★ ЦЕЛЬ РАЗВОРОТА В АТАКЕ — 1:1 objAI_FaceTargetByUserIdx@0xA23350: оригинал КАЖДЫЙ КАДР (вызов из
	//   общего цикла CharManager, entity_char.c:8541) для персонажа в боевом состоянии (5/24/14/27/13) с
	//   targetRenderId != -1 берёт ПОЗИЦИЮ ЦЕЛИ ИЗ СПИСКА СУЩЕСТВ (Entity_FindByRenderId / FindByUserIdx),
	//   dir = normalize(posЦели − posСвоя) → charComputeFacingAngleFromDir@0xA4B880. Направление НЕ из
	//   пакета. Ставят: мир-система (чужой атакующий, targetRenderId из 4029/4027) и контроллер (свой);
	//   цель пропала из списка → эталон сохраняет прежний dir (мы просто перестаём доворачивать).
	TWeakObjectPtr<AActor> AttackFaceTarget;
	// ★★★ РЕЖИМ-МАСКА ЦЕЛИ (эталонное `*(_WORD *)&pChar[1].gap_0[2]`, оно же `char + 104193` в словах).
	//   ХРАНИТ ТИП цели, лежащей в `AttackFaceTarget`, и читается покадровым
	//   `objAI_FaceTargetByUserIdx@0xA23350` (entity_char.c:23317): бит **8** → цель ищется
	//   `CharManager_FindByUserIdx` (ПЕРСОНАЖ), бит **1** → `Entity_FindByRenderId` (СУЩНОСТЬ).
	//   Пишет её `Recv_SkillCast@0xF4E370` (network.c:44877-44932) — см. `OnSkillCastFaceTarget`;
	//   значение 0 = «цели нет». Заведена ради П33: без неё ветка `modeMask == 3` эталона
	//   («в пакете и мобы, и персонажи») неотличима от однородных, и порт всегда перебивал цель
	//   первой записью, тогда как оригинал СОХРАНЯЕТ прежнюю, если она уже выбрана.
	uint16 TargetModeMask = 0;
	// ★★★ ТО ЖЕ САМОЕ ДЛЯ ПРИМЕНЕНИЯ НАВЫКА (симптом «направление игрока не устанавливается на моба
	//   во время применения атакующих скиллов», живой тест 2026-08-10).
	//   Гейт эталонного `objAI_FaceTargetByUserIdx@0xA23350` — это НЕ «идёт замах», а СПИСОК
	//   СОСТОЯНИЙ: `state ∈ {5, 24, 14, 27, 13} && targetRenderId != -1` (entity_char.c:23317).
	//   Состояние **24** ставит `Recv_SkillCast@0xF4E370` (ответ 5257) в ветке LABEL_45
	//   (`CChar_SetState(pChar, 24)`), он же пишет `pChar->targetRenderId = targetWords[0]` — индекс
	//   ПЕРВОЙ 29-байтной записи — и режим-маску `char+104193` (бит3 = цель-игрок, бит0 = цель-моб);
	//   состояние **14** ставит `Recv_SkillCastIdle` (5270, каст с полосой). Порт держал разворот
	//   только на состоянии 5 (`IsAttackAnimActive`), поэтому навык кастовался «стоя боком».
	//   ⚠ Цель разворота берётся ИЗ ЗАПИСЕЙ ПАКЕТА, а не из выбранной цели UI: при само-касте запись
	//   указывает на самого кастера, эталон получает нулевой вектор и ВЫХОДИТ БЕЗ РАЗВОРОТА
	//   (`if (dirLen == 0.0) return;`) — само-баффы персонажа не крутят.
	double SkillFaceUntilS = 0.0;
	// Идёт ли применение навыка с точки зрения разворота (состояния 24/14 эталона).
	bool IsSkillFaceActive() const;
	// ★★★ ГЕЙТ КЛИПА ПРИМЕНЕНИЯ НАВЫКА — до какого времени мира клип навыка нельзя подменять.
	//   Ставит и снимает ТОЛЬКО `UAsdaSkillFxSubsystem` (старт показа / его снятие / конец клипа).
	//   ЗАЧЕМ ОТДЕЛЬНОЕ ПОЛЕ. У эталона клип каста живёт в ОТДЕЛЬНОМ анимобъекте, а персонаж стоит в
	//   состоянии **24** (`CChar_SetState(pChar, 24)`, network.c:44938) — то есть НЕ в состоянии 0, и
	//   вариативность покоя (фиджет idle03, ветка состояния 0) до него не доходит по устройству.
	//   В порту клип на скелете один, поэтому «состояние 24 идёт» приходится держать временем, ровно
	//   как `AttackAnimUntilS` держит состояние 5. ⚠ `SkillFaceUntilS` для этого НЕ ГОДИТСЯ: он
	//   взводится лишь тогда, когда мир-подсистема нашла цель разворота в записях пакета, а у
	//   само-баффа и тогла записей нет вовсе.
	double SkillAnimUntilS = 0.0;
	// Играет ли сейчас клип применения навыка (гейт подмены клипа состояния).
	bool IsSkillAnimActive() const;
	// ★ Оборвать замах = эталонный CChar_SetState(pChar, t, 0, 0, 20): выход из состояния атаки в
	//   ПОКОЙ ПОДТИПА 20, т.е. в БОЕВУЮ СТОЙКУ (idle01), а не в мирный покой. В оригинале это
	//   происходит РОВНО в момент, когда цель реально умирает (её HP уходит в 0 на кадре удара) —
	//   вызывать раньше нельзя, иначе замах не отрабатывает (баг мгновенного кила, юзер 2026-07-31).
	void StopAttackAnim();

	// ★ БОЕВАЯ СТОЙКА (эталон: state 0 + субрежим 20, боевой покой = клип idle01/anim_12 «руки подняты»;
	//   меш-блендинг meshSetPartBlendFactorsByMode(2)). После смерти цели персонаж НЕ выходит из боя
	//   сразу — стоит в стойке, пока сервер не пришлёт сброс (4205) или не истечёт кулдаун.
	void SetCombatStance(bool bOn);
	bool IsCombatStance() const { return bCombatStance; }
	// ★ 07.11.2026 [ОРИГИНАЛ] ВЫРАЖЕНИЕ ЛИЦА (`asda.Char.FaceExpression`, World/AsdaPlayerCharacter_Face.cpp): 0 обычное, 1 глаза закрыты, 2 злое.
	int32 GetFaceMode() const { return FaceMode; }
	// Эталонное состояние действия, по которому выбирается лицо: свой — общая точка контроллера, сосед — признаки персонажа.
	int32 GetFaceActionState() const;
	// Имя текстуры, которая сейчас стоит на слоте лица (пусто — слота нет); для самотеста.
	FString GetFaceTextureShown() const;

	// ★ КРЫЛЬЯ (эталон charUpdateActionStateTick@0xA216B0: при bWingOn && !mounted бег-клип ПОЛНОСТЬЮ
	//   подменяется треком 35 = ch4_*_00_move_00 «бег со взмахами», ассет previewanim_35; idle НЕ меняется).
	//   Выставляет UAsdaWorldSubsystem::ApplyWingToActor по стейту крыла. SetWingsOn обновляет клип на лету,
	//   если уже бежим (крыло надели/сняли в движении).
	void SetWingsOn(bool bOn);
	bool bWingsOn = false;

	// ★ ТРАНСПОРТ (mount_system_re.md): персонаж к нему НЕ крепится — меш транспорта рисуется в ТОЙ ЖЕ
	//   матрице персонажа (эталон CCharacter_SetParts_RidingSystem@0x9EC900 грузит его в part-массив),
	//   а клипы персонажа подменяются на СИДЯЧИЕ Ch1_n_sit_<NNNN>_<VVV> (посадка запечена в анимациях):
	//   слот 0 = покой (000), слот 5 = движение (002). Взмахи крыльев в седле не играют (эталонный гейт
	//   !mounted), сами крылья остаются видимы. MountAnimSet = «0000».. «0017» из EffectEntry записи.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mount")
	TObjectPtr<class USkeletalMeshComponent> MountMesh;
	// ★ ШЛЕЙФ ЕЗДЫ — ПОВТОРЯЮЩИЙСЯ ОДНОРАЗОВЫЙ СПАВН, а не один вечный актор (эталон
	//   Item_UpdateSpecialEffectAndSound@0xF272E0, тик state==1 && bMounted):
	//     accumFx += dt(мс);  >= 2500 → Effect_SpawnByName(EffectFile, кость M_DUMMY/G_DUMMY, attach 6, scale 1), accum=0;
	//     accumSnd += dt(мс); >= 2000 → стоп прежнего канала + проигрыш SoundFile (2D себе / 3D чужим), accum=0.
	//   Сами .jeb шлейфов ОДНОРАЗОВЫЕ (endMode=0, period=3 с) — именно поэтому движок пересевает их каждые 2.5 с.
	//   В покое (state==0) работает charItemSoundChargeTick@0xF27740: тот же 2000-мс аккумулятор, но звук
	//   SoundFile2 (первый раз) / SoundFile3 (далее); эффект НЕ спавнится.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Mount")
	TObjectPtr<class UAudioComponent> MountAudio;
	FString RideFxName, RideSndMove, RideSndIdle1, RideSndIdle2;   // поля записи Ride_effect_table (+4/+68/+132/+196)
	UPROPERTY(Transient) TObjectPtr<UClass> RideFxClass;           // кэш BP_Fx_<RideFxName>
	float RideFxAccumMs = 0.f;      // аккумулятор эффекта (порог 2500)
	float RideSndAccumMs = 0.f;     // аккумулятор звука (порог 2000)
	bool bRideSndFirstIdle = true;  // первый покой-звук = SoundFile2, дальше SoundFile3 (worldMatrix[4] эталона)
	bool bRideWasMoving = false;    // переход покой→движение: стоп loop-звука + accum:=2000 (эталон self[52183]=2000.0f)
	int32 RideFxSpawnCount = 0;     // диагностика: первые 3 спавна шлейфа пишутся в лог
	void TickRideEffect(float DeltaSeconds);
	void SpawnRideTrailFx();
	void PlayRideSound(const FString& SoundName);
	// ★ ВСПЫШКА БЕЛОГО ДЫМА посадки/высадки: itm1_n_00_vehicle_0000_e.jeb на кости M_DUMMY/G_DUMMY,
	//   attach 7, scale 1.0 (посадка: Recv_MountVehicleRide@0xF2BCF0; высадка: CItem_OnRemoveByTable2Record
	//   @0xF264F0 и авто-высадка CChar_SetState@0xA13D50 case 5/24 — высадка ВСЕГДА без звука).
	void SpawnMountFlashFx();
	//   RideEffect/RideSound* — из Ride_effect_table (шлейф .jeb, звук движения, два звука покоя).
	void SetMount(const FString& ModelName, const FString& AnimSet, float HeightOffset,
		const FString& RideEffect = FString(), const FString& RideSound = FString(),
		const FString& RideSound2 = FString(), const FString& RideSound3 = FString());
	void ClearMount();
	bool IsMounted() const { return bMounted; }
	// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Mount.ListHoldsModel`, разбор у ключа в AsdaWorldSubsystem_Peers.cpp): транспорт этого персонажа
	//   ещё числится за ним (запись «id 15, type 2» эталона; у порта — кэш мира `MountByUserIdx`, его снимает только 5454 и снимки).
	//   Пока это так, модель транспорта локальные события (замах, смерть, смена вещей) не снимают. Ключ 0 — всегда false.
	bool IsMountHeldByServer() const;
	// ★ Высота посадки (HeightOffset записи спец-предмета, поле +0x127). Раньше аргумент SetMount
	//   отбрасывался — его читает компаньон-ХРАНИТЕЛЬ: эталон charPreviewUpdateRotation@0xFAA520
	//   прибавляет её к своим +30 при наличии у владельца эффекта посадки (soul_guardian_companion_re §5).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Mount")
	float MountHeightOffset = 0.f;

	// ── ★ ФЛАГ КЛАНА НА ТРАНСПОРТЕ (clan_flag_mount_re.md) ────────────────────────────────────────
	// Эталон: объект флага ТИПА 1 (uiClanFlagGetOrCreate@0xFAF910, таблица CBattleFlag_Construct@0xFAF3D0)
	//   = модель small_flag.jbm + клипы small_flag000/001/002.jbb. Он создаётся у ЛЮБОГО персонажа,
	//   у которого включён флаг клана (CCharacter+2505), а РИСУЕТСЯ только пока игрок в седле:
	//   mapobj_updateFlagLinkAnim@0xFB0CB0 гасит RenderCtx, если !bMounted || !clanFlagOn || стадия!=90.
	// Крепление: кость **Flag_Link** МЕША ТРАНСПОРТА (не персонажа!); мировая матрица флага =
	//   Rot(YawPitchRoll π,0,π/2) × BoneWorld(Flag_Link) × worldMatrix персонажа.
	// Полотнище (подсетка с текстурой "template.TEX") получает ИСПЕЧЁННУЮ ЭМБЛЕМУ клана —
	//   тот же render-target 128×64, что и на плашке (CClan_DrawClanFlagByIdx@0xFB27C0 → UAsdaClanFlag).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|ClanFlag")
	TObjectPtr<class USkeletalMeshComponent> ClanFlagMesh;
	// Кость транспорта, к которой крепится флаг (эталон: строковый поиск "Flag_Link").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|ClanFlag")
	FName ClanFlagBone = TEXT("Flag_Link");
	// Поворот флага ОТНОСИТЕЛЬНО кости Flag_Link. Значение по умолчанию — точный порт эталонного
	//   D3DXMatrixRotationYawPitchRoll(π, 0, π/2), пересчитанный в оси скелетала UE
	//   (проверено: BoneWorld_UE = C·BoneWorld_D3D·C⁻¹, C: (x,y,z)→(x,z,y)) ⇒ поворот 180° вокруг (1,0,1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|ClanFlag")
	FRotator ClanFlagAttachRotation;
	// Флаг клана (вкл + дизайн эмблемы) — приходит пакетами 4135/6542, раздаёт UAsdaWorldSubsystem.
	void SetClanFlag(bool bOn, const FAsdaClanFlagData& InFlag);
	bool HasClanFlag() const { return bClanFlagOn && ClanFlagData.IsValid(); }

	// ★ Аудит №3 (A9): «этот актор — СВОЙ персонаж?». Эталон различает 2D-звук своему
	//   (`CSoundFlag_PlaySound2D`) и 3D чужим (`Audio_Play3DSound`); проверка была размножена
	//   по месту у каждого звука транспорта. Ответ даёт `UAsdaWorldSubsystem::LocalPlayerActor`.
	bool IsLocalPlayerCharacter() const;

	// ═══ ★★★ РЕЖИМ «ДУША» (SoulState == 1) — ВИД В МИРЕ (`Reports/soul_silhouette_visual_re.md` §2, §8) ═══
	//   [ОРИГИНАЛ] `charEntityUpdateTick@0xA1ED30 :80703`: при `CCharacter+2241 == 1` КАЖДЫЙ кадр на все
	//   записи модели (0 = тело, 1..N = надетые части: броня, перчатки, шлем, причёска, оружие) ставится
	//   флаг «в очереди прозрачных» и α = 0x3E99999A = 0.3f; пиксель = tex.rgb, α = tex.a × 0.3,
	//   SRCALPHA/INVSRCALPHA, cull CW, без тинта/свечения/пульса. Свой персонаж — тем же тиком (:55012),
	//   т.е. игрок-душа и сам себя видит полупрозрачным. Плашка обычная, тень полная, крылья/маунт/
	//   подвесные эффекты α НЕ получают (отдельные узлы очереди), клипы/пикинг без изменений.
	//   Спавн-фейд и стелс-фейд у души НЕ умножаются (ветка `else` тика не достигается).
	//   НЕ силуэт и НЕ `silhoutte_tex_white` — та текстура грузится и никем не привязывается.
	//
	//   [ПОРТ] Blend-mode в рантайме у UE не переключается, поэтому слоты тела и всех компонентов с
	//   тегом `UAsdaCharAssembly::WornPartTag` пересаживаются на MID translucent-двойника мастера
	//   `M_AsdaCharLitSoul` (Tools/asda_build_charlit_soul_mat.py: копия M_AsdaCharLit + Opacity =
	//   Tex.A × SoulOpacity, ShadowReplace → тень полная, cull CW). `MountMesh`/`ClanFlagMesh`/крылья/
	//   питомец/свечение заточки тега не несут — не трогаются (эталон). Пересборка внешности
	//   (`InitAppearance` — 5001 у себя, 4021/4023 у чужих) пересаживает и НОВЫЕ части; выход из
	//   состояния 1 возвращает исходные материалы. Ключ отката: `asda.Soul.Translucent 0`.
	//   ⚠ Допущение: UE-translucent не пишет глубину (эталон — пишет), см. отчёт fix_soul_translucent_world.md.
	//   Зовёт `UAsdaWorldSubsystem::ApplySoulStateToActor` (свой и соседи) — сюда приходит ТОЛЬКО
	//   `SoulState == 1`; одержимость (2) на материалы не влияет.
	UFUNCTION(BlueprintCallable, Category="Asda|Player")
	void SetSoulTranslucent(bool bOn);
	// Взведён ли режим «душа» у этого актора (без учёта ключа отката).
	bool IsSoulTranslucent() const { return bSoulTranslucent; }
	// Режим «душа» взведён И ключ `asda.Soul.Translucent` включён — материалы реально подменены/подменяются.
	bool IsSoulTranslucencyActive() const;

	// ═══ ★★★ GM-«СКРЫТЬСЯ» (hide, опкод 6005/6006) — ВИД СВОЕГО ПЕРСОНАЖА ══════════════════════
	//   [ОРИГИНАЛ] Скрытие — ОДИН бит показа `CCharacter+0x40C` (`sub_9210D4`/`sub_9305E8`), и своему
	//   клиенту его ставит тот же обработчик 6006 (`localPlayerInputToggleByFlags@0xF49BD0`,
	//   `etalon/network.c:44004`): `CharManager_GetLocalPlayer()` → `sub_9210D4(v2,1)`. То есть
	//   у ОРИГИНАЛА скрывшийся оператор ИСЧЕЗАЕТ и для самого себя — полностью, без прозрачности.
	//   [ПОРТ] По требованию владельца свой персонаж под hide рисуется ПОЛУПРОЗРАЧНЫМ (тело +
	//   экипировка), а не пропадает: иначе оператор не видит, куда идёт и что на нём надето.
	//   Механизм НЕ новый — переиспользуется ровно тот же свап слотов на MID `M_AsdaCharLitSoul`,
	//   что у «души» (`SetSoulTranslucent`); отличается только источник признака и α
	//   (ключ `asda.Gm.HideAlpha`, у души α жёстко 0.3 по эталону). Соседи по-прежнему НЕ видят
	//   оператора вовсе — им приходит 4025/4103 и актор скрывается целиком.
	UFUNCTION(BlueprintCallable, Category="Asda|Player")
	void SetGmHideTranslucent(bool bOn);
	// Взведён ли GM-hide у этого актора (без учёта ключа отката).
	bool IsGmHideTranslucent() const { return bGmHideTranslucent; }
	// GM-hide взведён И ключ `asda.Gm.HideTranslucent` включён.
	bool IsGmHideTranslucencyActive() const;

	// ★ ЛЮБАЯ причина полупрозрачности (душа ИЛИ GM-hide) активна — материалы подменены/подменяются.
	//   Читает `UAsdaFadeComponent::BeginFade`: спавн-фейд поверх подменённых слотов не запускается
	//   (иначе фейд примет наш translucent-MID за «исходник» и вернёт его после себя).
	bool IsTranslucencyActive() const;
	// Требуемая α подменённых слотов для текущей причины. Душа — эталонные 0.3; GM-hide — ключ.
	float CurrentTranslucentAlpha() const;

	// ★ ТАЙМИНГИ ЗАМАХА ИЗ combo_time (эталон Char equip: field_39C = StepCount, кадры урона =
	//   GetEntryFloat84 = FrameTimeB[i] (+0x54), концы ударов = GetEntryFloat48 = FrameTimeA[i] (+0x30);
	//   кадр → мс ×33.333). Меч: 4 удара на 14/28/44/58 кадрах, полный замах 60 кадров = 2.0 с.
	//   ЭТО, а не длина UE-клипа, задаёт ритм боя и момент каждой цифры урона.
	//   ★★★ ТЕМП. Оба временны́х поля отдаются УЖЕ ПОДЕЛЕННЫМИ на множитель темпа атаки: в эталоне
	//   он растягивает шаг времени часов клипа замаха, а у нас те же четыре вещи (момент урона,
	//   звук замаха, шаг комбо, отправка пакета атаки) отмеряются реальным временем. Сам множитель
	//   лежит в `TempoMul` — он нужен там, где требуется ПОЗИЦИЯ В КЛИПЕ, а не реальное время
	//   (точка перемотки цикла атаки): клип-время = реальное × `TempoMul`.
	struct FAsdaComboTiming
	{
		int32 StepCount = 1;         // ударов за замах (сколько значений очереди тратится)
		TArray<float> HitTimesSec;   // моменты урона от начала замаха, РЕАЛЬНОЕ время (÷ темп)
		float TotalSec = 1.f;        // полная длительность замаха, РЕАЛЬНОЕ время (÷ темп)
		float TempoMul = 1.f;        // множитель темпа, которым поделены поля выше (1.0 = обычный)
		bool bValid = false;
	};
	FAsdaComboTiming GetComboTiming(int32 WeaponSubType);
	// Имя клипа атаки в combo_time по подтипу оружия (ch4_{m|f}_<грп>_normalattackNN).
	FString AttackClipName(int32 WeaponSubType) const;

	// ★★★ Аудит №3 (A3/Дд2/Дд6): ЕДИНЫЙ РАЗБОР `combo time.bin` → `DT_combo_time`.
	//   Было два независимых читателя одной таблицы: линейный обход на каждый вызов здесь и своя
	//   кэш-карта `ComboByClip` в `UAsdaSkillUseSubsystem` — две копии одних чисел в памяти и два
	//   разных способа поиска. Теперь загрузка идёт ЧЕРЕЗ `UAsdaItemTableSubsystem` (таблица в его
	//   манифесте, имя `combo_time`), а индекс «имя клипа (нижний регистр) → строка» — один на порт.
	//   При дублях имени в индексе остаётся ПЕРВАЯ строка обхода — так же, как отбирал прежний
	//   линейный поиск `GetComboTiming` (у него `break` на первом совпадении).
	static const TMap<FString, const struct FAsdaComboTimeRow*>& ComboTimeIndex(const UObject* WorldCtx);
	// Строка `combo_time` по имени клипа (регистр не важен); nullptr — строки нет.
	static const struct FAsdaComboTimeRow* FindComboTimeRow(const UObject* WorldCtx, const FString& ClipName);
	// ★ Аудит №3 (Дд6): кадр → миллисекунды. Эталонный множитель ×33.333 (1000/30). Прежде та же
	//   константа жила под двумя именами: `kFrameMs` здесь и `kFrameToMs` в подсистеме применения.
	static constexpr float kFrameToMs = 1000.f / 30.f;

private:
	// Проиграть мировой клип по индексу ассета (12..19), луп/один раз.
	float PlayWorldClip(int32 AssetIdx, bool bLoop);   // возвращает длину клипа (сек)

	// ── ★ Цикл атаки: эталонный рестарт с WindowTime, а не с 0 (см. StartAttackSwing) ──
	int32  AttackLoopClipIdx = -1;      // atk-ассет, который держит цикл (-1 = цикла нет)
	float  AttackLoopRewindS = 0.f;     // точка перемотки, сек = fmod(WindowTime, длина клипа)
	float  AttackLoopLenS = 0.f;        // длина атак-клипа, сек
	double AttackLoopHoldUntilS = 0.0;  // удержание цикла; < 0 = бесконечно (до StopAttackAnim)
	FTimerHandle AttackLoopTimer;       // срабатывает на конце каждого прохода клипа
	void OnAttackClipEnd();             // эталон LABEL_155: конец клипа → перемотка на W либо выход в стойку
	// ── ★ 15.10.2026: точки отправки 4028 внутри прохода и скорость отрезка (см. `BeginSendSegment`) ──
	TArray<float> AttackSendPtsFirst;   // клип-секунды точек отправки в ПЕРВОМ проходе («взвод» с кадра 0)
	TArray<float> AttackSendPtsLoop;    // то же в проходах цикла (от точки перемотки); значение = длина клипа → конец прохода
	bool bAttackFirstPass = true;       // идёт первый проход (до первой перемотки)
	float AttackSyncRate = 0.f;         // скорость клипа текущего отрезка по серверным часам (0 — не задана)
	FTimerHandle AttackSendTimer;       // срабатывает в точке отправки ВНУТРИ прохода (ближнее оружие)
	void OnAttackSendPoint();           // точка отправки внутри прохода: удар контроллеру + следующий отрезок
	TArray<float> AttackHitFirst;       // ★ ревизия СРЕДН-5: кадры ударов первого проигрывания (клип-секунды)
	TArray<float> AttackHitLoop;        //   то же в проходах цикла
	TArray<int32> AttackHitFirstStep;   // ★ Ф4: шаг комбо каждого кадра `AttackHitFirst` (тот же порядок)
	TArray<int32> AttackHitLoopStep;    // ★ Ф4: шаг комбо каждого кадра `AttackHitLoop`
	float HitScanPos = -1.f;            // ★ Ф4: позиция клипа прошлого опроса `ConsumeCrossedHitFrames` (−1 — опроса не было)
	bool bHitScanFirst = true;          // ★ Ф4: вид прохода на прошлом опросе
	bool bHitScanRanged = false;        // ★ Ф4: замах дальнобойным (8–11) — один выстрел на проход (см. `ConsumeCrossedHitFrames`)
	int32 AttackPassSerial = 0;         // ★ Ф4: номер прохода клипа замаха (растёт на каждой перемотке `OnAttackClipEnd`; ★ 18.10 — и на «взводе»/выходе)
	int32 AttackSwingGen = 0;           // ★ 18.10.2026 (ВАЖНО 3): номер замаха (см. `GetAttackSwingGen`)
	int32 HitScanSerial = 0;            // ★ Ф4: номер прохода на прошлом опросе `ConsumeCrossedHitFrames`
	void ComputeAttackHitFrames(int32 W, float Len, float Rewind, TArray<float>& OutFirst, TArray<float>& OutLoop);
	float FrameDtEma = 0.016f;          // ★ ревизия СРЕДН-2: сглаженная длительность кадра (точка отправки — за кадр до дедлайна)
	// ★ 22.10.2026 (плавность, `World/AsdaBlendAnimInstance.h`):
	float FacingYawLogical = 0.f;       //   логический курс MeshRoot (UE-градусы, С доворотом `MeshYawOffset`) — цель доворота
	bool bFacingYawSet = false;         //   логический курс задан хоть раз (до этого курс = поворот MeshRoot)
	FVector VisualTail = FVector::ZeroVector;   // «хвост» сетевой поправки: смещение MeshRoot от актора, гаснет экспонентой
	void TickAnimSmooth(float DeltaSeconds);    //   доворот видимой модели + гашение хвоста (каждый кадр, на любом выходе из Tick)
	void AddVisualTail(const FVector& OldLoc);  //   актор только что прыгнул из OldLoc сетевой поправкой → тело догоняет плавно
	// ★ 22.10.2026 (ревизия плавности, ВАЖНО-А): у СВОЕГО персонажа хвост несут и камера, и плашка (они на Root) — иначе экран прыгал
	//   при поправке, а тело уезжало из центра кадра. Ключ `asda.Anim.SelfCameraTail` (1; 0 — хвост только у тела, как было).
	void ApplyVisualTailToViews();
	FVector TailBaseCamera = FVector::ZeroVector;      //   исходное смещение стрелы камеры от Root (до хвоста)
	FVector TailBaseNameplate = FVector::ZeroVector;   //   исходное смещение плашки от Root
	bool bTailBasesCaptured = false;                   //   исходные смещения сняты
	bool bTailOnViews = false;                         //   хвост сейчас приложен к камере/плашке (снять при выключении ключа)
	double CombatStanceSinceS = 0.0;    // ★ ревизия СРЕДН-1: вход в боевой покой (эталон `fAutoActionStartTime`), мировое время
	void TickCombatStanceExit();        //   выход соседа из боевого покоя через 10 с (эталон подтип 20 → 15)
	// Точки отправки для оружия W (клип длиной Len, перемотка Rewind) — эталонное правило «чётный шаг».
	void ComputeAttackSendPoints(int32 W, float Len, float Rewind, TArray<float>& OutFirst, TArray<float>& OutLoop);
	// Клип-расстояние от позиции Pos до следующей точки отправки (через конец прохода — с перемоткой); bOutAtClipEnd —
	//   точка совпадает с концом прохода (её обслуживает `OnAttackClipEnd`).
	float NextSendDistClip(float Pos, bool& bOutAtClipEnd) const;
	// То же для явного вида прохода (bFirst — первый проход «взвода», иначе проход цикла) — общая часть с планом соседа (Ф2).
	float SendDistFromClip(float Pos, bool bFirst, bool& bOutAtClipEnd) const;
	// ── ★ Ф2: замах соседа от пакетов (разбор у `PeerAcceptSwingPacket`) ──
	bool bPeerPktDriven = false;        // замах ведут пакеты 4029 (сосед)
	bool bPeerRanged = false;           // оружие дальнобойное: в отрезке один кадр удара (выстрел)
	TArray<float> PeerQueuedRates;      // отрезки в очереди — по одному на принятый 4029: скорость клипа каждого
	bool bPeerSegClaimed = false;       // текущий отрезок взят пакетом (иначе доигрывается без ответа)
	double PeerSegStartS = 0.0;         // мир-время начала текущего отрезка
	float PeerSegStartClip = 0.f;       // позиция клипа в начале текущего отрезка (клип-с)
	bool bPeerSegStartFirst = true;     // текущий отрезок начался в первом проходе
	bool bPeerSegInclStart = true;      // кадр удара в самой точке начала относится к отрезку (кадр 0 «взвода», точка перемотки)
	float PeerSegDistClip = 0.f;        // клип-длина текущего отрезка (до следующей точки отправки, через перемотку)
	bool bPeerSegAtEnd = false;         // текущий отрезок кончается концом прохода клипа
	float PeerSegRate = 0.f;            // скорость клипа текущего отрезка (0 — не задана)
	double PeerLastAcceptS = -1.0;      // мир-время последнего принятого 4029
	float PeerLastAcceptNatS = 0.f;     // естественная длительность отрезка, взятого последним 4029 (реальные с при номинальном темпе)
	TArray<float> PeerCoefSamples;      // последние выборки коэффициента темпа (естественная длительность / интервал)
	FTimerHandle PeerSilenceTimer;      // запасной конец серии по тишине
#if !UE_BUILD_SHIPPING
	TArray<FAsdaPeerSegSeen> PeerSegLog;// журнал отрезков (самотест; кольцо из последних 64)
#endif
	void BeginPeerSegment(const TCHAR* Why, bool bInclStart);   // граница отрезка соседа: взять следующий (из очереди или без ответа)
	void ResetPeerPktState();           // снять состояние замаха от пакетов (выход из атаки)
	void OnPeerSilence();               // тишина дольше «отрезок + 0.5 с» — конец замаха
	float PeerNominalTempo() const;     // номинальный темп соседа (состояния и предметы, `GetAttackTempoMul`)
	// Кадры удара отрезка (клип-расстояния от его начала): начало, вид прохода, длина, «через конец прохода» (bAtEnd), включать ли
	//   кадр в самой точке начала. У дальнобойного — только первый кадр; проход цикла без кадра удара у дальнобойного (баллиста:
	//   кадр B0 = точке перемотки) — кадр в точке перемотки.
	void PeerSegHits(float StartPos, bool bStartFirst, float Dist, bool bInclStart, TArray<float>& OutDist) const;
	// ★ 15.10.2026 (волна mobpos_swing): самый короткий отрезок «точка → точка» прохода цикла в клип-секундах (0 — нет цикла).
	float SteadyLoopSegmentClip() const;
	TArray<float> AttackSegRates;       // скорости отрезков текущей серии (самотест ровности темпа)
	bool bAttackSegEvened = false;      // текущий отрезок первого прохода замедлен до темпа цикла
	float AttackSegPlannedS = 0.f;      // длительность текущего отрезка по плану (клип-расстояние / скорость), сек

	// ══════════ ★★★ «СЕСТЬ / СИДИТ / ВСТАТЬ» — ТРИ СЕГМЕНТА ОДНОГО КЛИПА ═══════════════════════
	// Реверс: `Reports/char_info_fix2.md` §3. У эталона это НЕ три клипа и НЕ обратное
	//   воспроизведение, а ОДИН клип слота 3 (`ch4_{m|f}_00_sitdown01.jbb`, он же наш `anim_15`),
	//   разрезанный по ВРЕМЕНИ. `CChar_SetState@0xA13D50` (entity_char.c:19657-19679) для состояний
	//   7/8/9 не меняет слот, а ставит `CObject_SetField13(pModelObj + 192, t)` — текущее время:
	//     state 7 «садится»  t = 0        → тик `objAnimMode32SetState@0xA169C0` при t > 1500 (муж) /
	//                                        2000 (жен) переводит в состояние 8;
	//     state 8 «сидит»    t = 1500/2000 → тик `objAnimTickMode32@0xA16A80` по достижении
	//                                        3500/4000 возвращает t на 1520/2020 ⇒ ЦИКЛ;
	//     state 9 «встаёт»   t = 3500/4000 → тик `charSetActionTrackAndState@0xA16C50` по концу
	//                                        прохода клипа зовёт `CChar_SetState(…, 0)`.
	//   Длина клипа из заголовка .jbb (+0x30): муж 5333.33 мс, жен 5666.67 мс при 30 к/с.
	//   ⇒ вставание = хвост 3500…5333 (муж) / 4000…5667 (жен), 55 / 50 кадров.
public:
	// ★ Константы публичные — их читает консольная проверка `asda.Player.SitClipInfo`.
	static constexpr float kSitDownEndMsMale   = 1500.f;   // конец «садится» (муж)
	static constexpr float kSitLoopEndMsMale   = 3500.f;   // конец цикла «сидит» = начало «встаёт» (муж)
	static constexpr float kSitDownEndMsFemale = 2000.f;
	static constexpr float kSitLoopEndMsFemale = 4000.f;
	static constexpr int32 kSitClipAsset = 15;             // anim_15 = слот 3 = sitdown01
private:

	int32 SitPhase = 0;                 // 0 нет, 7 садится, 8 сидит (цикл), 9 встаёт — номера состояний эталона
	FTimerHandle SitTimer;              // конец текущего сегмента
	void  BeginSitDown();               // состояние 7
	float BeginStandUp();               // состояние 9; возвращает длительность хвоста (сек), 0 = клипа нет
	void  OnSitPhaseTimer();            // 7→8, цикл 8, 9→покой
	void  ResumeSitIdle();              // состояние 8: кадр начала цикла + повторяющийся таймер отмотки
	void  SetSitClipPositionMs(float Ms);   // перемотка клипа sitdown01 на заданное время
	float SitDownEndSec() const;        // граница «садится/сидит», сек (по полу)
	float SitLoopEndSec() const;        // граница «сидит/встаёт», сек (по полу)
	float SitClipLenSec() const;        // полная длина клипа sitdown01, сек

	// ── ★ Эмоция: одноразовый клип anim_86…97 (эталон: конец прохода → CChar_SetState(0)) ──
	double EmotionUntilS = 0.0;         // до какого времени мира клип эмоции держит персонажа
	// ★ В3 S7: номер эмоции 33…46 живёт в едином поле `BusyAction` (прежде `EmotionState`); здесь — только время её клипа.
	FTimerHandle EmotionTimer;          // срабатывает на конце прохода клипа эмоции
	// ★ ДИАГНОСТИКА «эмоция не доигрывает» (2026-08-25): какой клип запущен эмоцией и когда.
	//   Каждый кадр сверяем с тем, что реально крутит скелет: если подменили — пишем в журнал,
	//   кто и на какой секунде. Без этого причину приходится искать вслепую.
	TWeakObjectPtr<class UAnimSequence> EmotionClipAsset;
	double EmotionStartedS = 0.0;
	void TickEmotionWatch();

	// ── ★★★ Рыбалка (волна В5): действие 60…67, удочка, рыба (см. публичный блок `PlayFishAction`) ──
	// ★★ В3 S7 (12.10.2026): ЕДИНОЕ «ЗАНЯТОЕ» СОСТОЯНИЕ ДЕЙСТВИЯ (эталон `+47551`, пишет `CChar_SetState`) вместо 4 признаков:
	//   эмоция 33…46 (`EmotionState`), рыбалка 60…67 (`FishAction`), 68 «добыча» (`bEtalonState68`), лопата 40/43
	//   (`EtalonShovelState`). Взаимное гашение (прежде вручную в 6 местах) — в `SetBusyAction`. Idle — «занятого» нет.
	EAsdaActionState BusyAction = EAsdaActionState::Idle;
	// Записать новое «занятое» состояние: прежнее снимается целиком (таймеры конца клипа, сторож клипа, инструмент лопаты) —
	//   как единственная запись поля в `CChar_SetState`. `Why` — источник (журнал `[действие]`). Клип и оружие не трогает.
	void SetBusyAction(EAsdaActionState New, const TCHAR* Why);
	// ★ В3 S7: ОДНА ПРЕАМБУЛА действия «на месте» (рыбалка 60…67, 68, лопата 40/43): `CChar_SetState` переписывает ход (1),
	//   сидение (7/8/9) и замах (5) без их собственного клипа — ход снимается без телепорта и клипа покоя, сидение без хвоста,
	//   цикл атаки гасится. Прежде — три одинаковые копии.
	void StopMotionForAction();
	// ★ В3 S7: вход в «занятое» действие на месте = преамбула + единое поле (общая часть `PlayFishAction` / 68 / лопаты).
	void BeginEtalonAction(EAsdaActionState New, const TCHAR* Why);
	FTimerHandle ShovelClipTimer;       // ★ six_items СРЕДН-1: конец клипа копания (40 — трек 17, 43 — трек 20) = конец состояния
	void OnShovelClipEnd();             // ★ six_items СРЕДН-1: конец клипа копания — ветка конца case 7/10 эталона
	void ClearEtalonShovelState();      // ★ six_items СРЕДН-1: снять 40/43 вместе с таймером конца клипа (любая смена состояния)
	// ── ★ 10.10.2026 (копание): клип, удары, инструмент, эффекты — поля эталона `CCharacter[1]` (структура `structs_ru.h`) ──
	int32  ShovelDigStroke = 0;         // `digStrokeCount` (+0xDC): ударов лопаты 40 в проходе (не больше 4)
	int32  ShovelMineStroke = 0;        // `mineStrokeCount` (+0xE0): ударов авто-лопаты 43 в проходе (не больше 7)
	float  ShovelTrackMs = 0.f;         // время трека клипа копания (мс): `Object_GetFloat13(трек 17|20)`
	int32  ShovelThrMs[5] = {0, 0, 0, 0, 0};   // пороги ударов, мс: [0] = combo +48 запись 0, [1..4] = +84 записи 0..3
	int32  ShovelStrikeTotal = 0;       // диагностика: всего ударов (звук + эффект) с начала жизни актора
	float  ShovelElapsedS = 0.f;        // диагностика: время с входа в 40/43 по тикам персонажа, с
	TArray<float> ShovelStrikeLog;      // диагностика: моменты ударов прохода (`ShovelElapsedS` на ударе), с
	bool   bShovelToolAuto = false;     // `[216]` (+209116 / [52279]): на кость `8_a1` загружен авто-меш (1) / ручной (0)
	TWeakObjectPtr<AActor> ShovelStartFx;   // `worldMatrix[0]` — стартовый эффект копания (гасится, когда состояние ≠ 40/43)
	TWeakObjectPtr<AActor> ShovelHitFx;     // `worldMatrix[1]` — эффект последнего удара (гасит следующий удар и итог 5430)
	UPROPERTY(Transient) TObjectPtr<class UStaticMeshComponent> ShovelToolComp;   // меш инструмента на `8_a1` (`pModelPartArray+4716`)
	void TickShovel(float DeltaSeconds);   // покадрово: удары по порогам, перемотка 43, гашение стартового эффекта (case 7/10)
	void ShovelStrike();                // один удар: звук `S_e_shovelling01.wav`, эффект `c-shovel-a_00` (привязка 6)
	void LoadShovelThresholds();        // пороги ударов из `DT_combo_time` по полу (`Char_LoadMotionAnim` case 0x1F)
	float ShovelClipMs(int32 State) const;   // длина клипа копания (ассет anim_233/234; нет ассета — 6000 мс заголовка .jbb)
	void LoadShovelToolMesh(bool bAuto);     // `meshLoad_BindSubsetTexturesWithState` + `sub_93DE55(…, «8_a1»)`, `[216] = bAuto`
	void UpdateShovelToolVisible();     // `sub_93362B(…, 1|0)`: инструмент виден ровно в 40/43
	void SpawnShovelStartFx(const TCHAR* JebBase, bool bFollow);   // стартовый эффект: матрица по курсу (bFollow = привязка 6)
	void StopShovelStartFx(bool bResetStrokes = true);   // `CEffectMgr_StopByOwner(worldMatrix[0])` (+ обнуление счётчиков ударов)
	bool   bShovelStartHandle = false;  // хэндл `worldMatrix[0]` ≠ −1: стартовый эффект хоть раз создан (эталон его в −1 не возвращает)
	float  ShovelLenMs = 6000.f;        // длина клипа текущего прохода, мс (снята на входе в 40/43)
	bool   bFishActionSelf = false;     // действие поставлено автоматом рыбалки своего персонажа
	bool   bFishOwnerSelf = false;      // свой персонаж (правило вставки удочки/рыбы — по флагу)
	FTimerHandle FishClipTimer;         // конец прохода клипа действия
	double FishClipStartedS = 0.0;      // когда запущен проход (сторож)
	TWeakObjectPtr<class UAnimSequence> FishClipAsset;   // клип, запущенный действием (сторож подмены)
	bool   bFishFlag = false;           // [15036] (+226060) флаг «рыбачит»
	bool   bFishHasFish = false;        // [15066] рыба на крючке
	int32  FishSizePct = 100;           // [15068] размер рыбы, % (ctor эталона = 100)
	int32  FishCode = 0;                // [15076] FishCode / id улова
	int32  FishFxLastAction = 0;        // [15100] последнее действие, для которого решалось о fish_end (чужой)
	int32  FishPeerFxCount = 0;         // диагностика: эффектов fish_end у чужого
	int32  EquippedWeaponItemId = 0;    // код надетого оружия (`char+0x835`), пишет SetWeaponFromItem
	UPROPERTY(Transient) TObjectPtr<class USkeletalMeshComponent> FishRodComp;    // удочка (`g_pSceneObjMgr` 0x35203C4)
	UPROPERTY(Transient) TObjectPtr<class USkeletalMeshComponent> FishHookComp;   // рыба (`dword_3520C4C`)
	FString FishRodMeshName;            // меш удочки (`ImgRecord+4` + «_00»)
	// Предмет, по которому вставлена удочка (только журнал). ★ В45fix2 (МЕЛК-8): [15084] оригинал не пишет; [15088] копирует его
	//   при вставке (`[15088] = [15084]`, 0xFCD738 / 0xEFF3BE, g:607585) — проверка `[15088] != [15084]` никогда не срабатывает.
	int32  FishRodLoadedItem = 0;
	bool   bFishRodInserted = false;    // [15037]/+226061 «удочка вставлена» (эталон ставит и тогда, когда меша нет — повтор только после снятия)
	int32  FishRodClip = 0;             // +464 объекта удочки — клип = действие − 60
	float  FishRodTrackPos[8] = {0,0,0,0,0,0,0,0};   // время каждого из 8 треков удочки (у эталона своё у каждого слота)
	int32  FishHookLoadedCode = 0;      // [15080] код рыбы, по которому загружена модель
	bool   bFishHookInserted = false;   // [15038]/+226062 «рыба вставлена» (эталон ставит и тогда, когда модели нет)
	bool   bFishWeaponHidden = false;   // оружие и щит (меш оружия, свечение оружия, аура щита) скрыты на время рыбалки (`char[52136] = 0`)
	TArray<TWeakObjectPtr<UObject>> FishHiddenParts;   // части, спрятанные рыбалкой (метка AsdaFishHidden) — умерла ссылка ⇒ пересборка
	int32  FishHideSig = -1;            // подпись набора частей на момент прохода скрытия (повтор прохода — только при смене)
	// Конец прохода клипа действия (таймер).
	void OnFishClipEnd();
	// Сторож: клип действия подменён на ходу — журнал (как TickEmotionWatch).
	void TickFishClipWatch();
	// Покадрово: вставка/снятие удочки и рыбы (эталон 55024-55056 свой / 55827-55870 чужой), посадка на кости.
	void TickFishAttachments(float DeltaSeconds);
	// Создать удочку по надетому предмету (`fishingInsertEntry` → `CFishing_LoadRodAnimations@0xFCD6F0`).
	void InsertFishRod();
	// Снять удочку (`charFxRemoveAttachedById`).
	void RemoveFishRod();
	// Поставить клип удочки (`attachedObjectSetField464ById@0xFCE050`).
	void SetFishRodClip(int32 Clip);
	// Создать рыбу по [15076] (`charFxAddAttachedById` → `CItemCollectUI_LoadPreviewModel@0xFD1370`).
	void InsertFishHook();
	// Снять рыбу (`charFxRemoveAttachedById2`).
	void RemoveFishHook();
	// Скрыть/вернуть оружие и щит: меш оружия, свечение заточки оружия, аура щита (эталон `char[52136]`; В5fix ВАЖН-1).
	void ApplyFishWeaponHide(bool bHide);
	// Эффект fish_end над персонажем (`Effect_SpawnByName(…, y + 130, …, attach 0)`).
	void SpawnFishEndFx(const TCHAR* JebBase);
	// ★★ КАЧЕСТВО ПЕРСОНАЖА НЕ ЗАВИСИТ ОТ РАССТОЯНИЯ (требование владельца 2026-08-25:
	//   «когда камера на отдалении — персонаж мыльный; качество меняться не должно»).
	//   Держим у всех его мешей нулевой уровень детализации и полную детализацию текстур.
	//   Ключ отката: `asda.Char.StableQuality 0`.
	void ApplyStableQuality();
	double NextStableQualityS = 0.0;   // пересматриваем раз в секунду (модель пересобирается на ходу); 0 = ещё не было прохода
	// Текстуры, которым ЭТОТ персонаж держит флаг полных мипов (реестр UAsdaTextureSubsystem; только при
	//   `asda.Char.StableQuality.Textures 2`). Снимается, когда персонаж скрыт, ключ выключен или актор ушёл.
	TSet<TObjectKey<class UStreamableRenderAsset>> StableQualityMips;
public:   // (секция вокруг — закрытая; общие точки нужны экрану выбора персонажа)
	// ★ ОБЩИЕ ТОЧКИ правила для ЛЮБОГО актора с мешами персонажа (задача charsel_preload 13.09.2026):
	//   половина LOD — скелетным SetForcedLOD(1), статическим SetForcedLodModel(1) (!bOn — решение движку);
	static void ApplyStableLodToActor(AActor* Actor, bool bOn);
	//   половина текстур — какие текстуры рисуют меши актора (флаг ставит владелец через реестр).
	static void CollectActorTextures(AActor* Actor, TSet<class UStreamableRenderAsset*>& Out);
	// Включено ли правило вообще (`asda.Char.StableQuality`) — главный выключатель обеих половин.
	static bool IsStableQualityEnabled();
	// Где действует половина текстур (`asda.Char.StableQuality.Textures`): 0 нигде, 1 экран выбора, 2 + мир.
	static int32 GetStableQualityTexturesMode();
private:

	// ── ★★★ Режим «душа»: подмена материалов (см. SetSoulTranslucent) ──
	bool bSoulTranslucent = false;            // SoulState == 1 у этого актора (ставит мир-подсистема)
	// ── ★★★ GM-«скрыться»: ВТОРАЯ причина той же подмены (см. SetGmHideTranslucent). Отдельный
	//   признак, а не переиспользование `bSoulTranslucent`: состояния независимы (оператор может
	//   войти душой и скрыться), у каждого свой ключ отката и своя α.
	bool bGmHideTranslucent = false;          // подтверждённое сервером «я скрыт» (ставит мир-подсистема)
	bool bSoulMaterialsApplied = false;       // слоты сейчас на translucent-MID (учитывает ключ отката)
	float AppliedTranslucentAlpha = -1.f;     // α, реально записанная в MID (для смены причины на лету)
	UPROPERTY(Transient) TArray<FAsdaSoulSlotSwap> SoulSwaps;                 // подменённые слоты
	UPROPERTY(Transient) TObjectPtr<class UMaterialInterface> SoulMaterialBase; // кэш M_AsdaCharLitSoul
	double NextSoulAdoptS = 0.0;              // подхват новых слотов — раз в секунду (как StableQuality)
	// Подменить/подхватить слоты тела и частей с тегом WornPartTag на translucent-MID (идемпотентно).
	void ApplySoulTranslucency();
	// Вернуть исходные материалы во все подменённые слоты (только если там всё ещё наш MID).
	void RestoreSoulMaterials();
	// Тик режима: сверка с ключом отката + периодический подхват новых компонентов.
	void TickSoulTranslucency();
	// GM-hide: раздать множитель α всем подвесным эффект-акторам (разбор — у определения).
	void ApplyGmHideToAttachedFx(float Scale);
	float AppliedFxAlphaScale = 1.f;          // последний розданный эффектам множитель (для лога)
	// GM-hide: снять/вернуть тень (ключ `asda.Gm.HideCastShadow`). Возвращаем ТОЛЬКО тем, у кого сняли.
	void ApplyGmHideShadow(bool bHiddenNow);
	TArray<TWeakObjectPtr<class UMeshComponent>> GmHideShadowOff;
	void OnEmotionClipEnd();            // вернуть клип текущего состояния (idle/бег/сидя)
	FTimerHandle CampPoseTimer;         // ★ 30.10.2026: конец одноразового клипа позы 51 (у 47 таймера нет — цикл)
	void OnCampPoseClipEnd();           // ★ 30.10.2026: конец клипа 51 → эталонное `SetState(0)`, `bActionBusy = 0`

	// ★ Idle-бездействие (реверс player_anim_appearance_re.md §2: state 0 = слоты 0/1/2 idle01-03, вариативность):
	//   основной покой = idle02 (anim_13, руки вниз); после простоя — фиджет idle03 (anim_14) одноразово → назад в idle02.
	//   idle01 (anim_12, руки подняты/боевая стойка) — резерв под combat (animState==11), в мирном покое НЕ играем.
	float IdleTimer = 0.f;              // накопленное время в покое (сброс при движении/фиджете)
	float NextFidgetDelay = 8.f;        // порог до след. фиджета (рандом 6..12с)
	float FidgetRemaining = 0.f;        // >0 = фиджет играет (обратный отсчёт длины клипа)
	// Клипы покоя ТЕКУЩЕГО набора оружия (были константы 13/14/12 набора _00).
	int32 IdleMainClipIdx() const   { return MotionClipIdx(EAsdaMotionSlot::Idle02); }   // idle02 — мирный покой
	int32 IdleFidgetClipIdx() const { return MotionClipIdx(EAsdaMotionSlot::Idle03); }   // idle03 — фиджет
	int32 IdleCombatClipIdx() const { return MotionClipIdx(EAsdaMotionSlot::Idle01); }   // idle01 (слот 0) — боевая стойка
	// Клип покоя по текущему режиму (боевая стойка / мирный).
	//   ★ 23.10.2026 (плавание донора): в глубокой воде покой = swimloop01 (слот 38 донора) — ветка плавания донора
	//   перебивает слот покоя и боевой стойки в том же кадре (`0x4AF1A0`/`0x4AE120`).
	int32 IdleClipIdx() const { return bSwimming ? kClipSwimIdle : (bCombatStance ? IdleCombatClipIdx() : IdleMainClipIdx()); }
	bool bCombatStance = false;                  // в боевой стойке (idle → idle01, без фиджетов)
	// Пересчитать «оружие убрано» по позиции (безопасная зона) и седлу; при смене — перезапустить клип.
	void UpdateWeaponMotionHidden();
	// Для подмены на СИДЯЧИЕ клипы в седле: 0 = клип покоя, 1 = клип движения, -1 = не подменять.
	static int32 MountSubstKind(int32 AssetIdx);
	float TerrainZ(float UeX, float UeY) const;

	// Проходима ли game-точка по сетке карты (WalkGrid). Нет сетки → true (fail-open).
	bool IsGameWalkable(const FVector2D& GameXZ) const;

	FVector2D MoveDstGame = FVector2D::ZeroVector;   // цель движения (game XZ) — режим «в точку»
	FVector2D MoveDirGame = FVector2D::ZeroVector;   // направление — режим «пока держишь»
	FVector2D FaceDirGame = FVector2D::ZeroVector;   // куда смотрит меш в непрерывном режиме (пятясь — назад от MoveDir)
	float MoveSpeed = 0.35f;                          // юнит/мс (сервер; база 0.35 = 3.5 тайла/с)
	bool bMoving = false;
	bool bMoveToPoint = false;                        // true=к точке (click), false=по направлению (WASD)
public:
	// ★ 16.09.2026 (4062, self-коррекция): идёт ли СЕЙЧАС ход «в точку». Эталонная self-ветка 4062 не начинает
	//   движение, а поправляет уже взведённый клик-мув (читатели полей 52127…52130 сидят в switch по ключу
	//   52121, который 4062 не пишет). Поэтому обработчику нужен именно этот признак.
	bool IsMovingToPoint() const { return bMoving && bMoveToPoint; }
	// ★ 22.10.2026 (локальный старт, ревизия ВАЖНО-3): цель и скорость идущего хода «в точку» — чтобы при отказе сервера на бегу
	//   вернуться к прежнему гранту (сервер его продолжает), а не к точке отправки.
	FVector2D GetMoveDstGame() const { return MoveDstGame; }
	float GetMoveSpeed() const { return MoveSpeed; }
	// ★ 6071: новая серверная скорость для УЖЕ ИДУЩЕГО движения — без смены цели, направления и режима.
	//   Эталон (`Recv_CharMoveToCell`) гейтит это состоянием действия `== 1` («идёт») и пересчитывает
	//   текущий шаг: `charMoveStepToCell(текущая позиция → СОХРАНЁННАЯ цель, новая скорость)`. У нас шаг
	//   интегрируется покадрово от `MoveSpeed` к `MoveDstGame`, поэтому достаточно сменить скорость.
	//   Стоящего персонажа эталон не трогает — возвращаем false, чтобы вызывающий это видел.
	bool SetServerMoveSpeed(float SpeedUnitsPerMs);
	// ★★ 27.09.2026 — ход клавишами по эталону (`asda.Move.KeyboardEtalon 1`, `Reports/owner_0927/wasd_impl.md`).
	//   [ОРИГИНАЛ] `updatePlayerMovement@0x95CDF0` каждый кадр пишет `LocalPlayer[1].buffFxSlots[208] = «S нажата»`
	//   (байт +0x330D4; скан бинаря: писатели 0x95D307 / 0xA1C2A5 (сброс) / 0xEFEDDF (копия), читатели 0xA22AC2 и
	//   0xA236DD). В состоянии «идёт» читатель ставит клип 24 (back01) и разворачивает лицо на −направление шага —
	//   ЛЮБОГО шага, в том числе хода по гранту 4007 к точке. Контроллер при ключе 1 зовёт это каждый кадр.
	void SetEtalonBackpedal(bool bOn);
	// ★ Счётчик окончаний хода «в точку» самим персонажем (дошёл до цели / упёрся в непроходимую клетку) — сигнал
	//   контроллеру, что ход по гранту клавиатуры закончился и эталон в этот момент шлёт 4008 (`Player_ExecuteSkillOnTarget`
	//   game.c:68720-68730 / 68738-68745). Снапы (4009, result 5) и внешние остановки счётчик НЕ меняют.
	int32 GetPointMoveEndSerial() const { return PointMoveEndSerial; }
private:
	// ★ Пятимся (S, эталон LocalPlayer[1].buffFxSlots[208]): клип back01 вместо run01, лицом вперёд.
	bool bBackpedal = false;
	// ★ Признак «S нажата» эталонной клавиатурной схемы (см. `SetEtalonBackpedal`); вне ключа 1 всегда false.
	bool bEtalonBackpedal = false;
	// ★ Счётчик окончаний хода «в точку» (см. `GetPointMoveEndSerial`).
	int32 PointMoveEndSerial = 0;
	// ★ Транспорт: в седле клипы берутся из набора сидячих анимаций (MountAnimSet), а не из previewanim_*.
	bool bMounted = false;
	// ★ 26.10.2026 (ревизия wave_1025d 1.1) [ОРИГИНАЛ]: части последней сборки `InitAppearance` (и пол) — по ним видно, что сменился
	//   ТОЛЬКО щит: эталонный `CCharacter_SetParts_Armor@0x9EB3C0` (ветка 8 `tagCHARACTER_SetPartsModelByType@0x9E18D0`) меняет
	//   одну деталь модели и с транспорта не ссаживает. Ключ `asda.Char.ShieldKeepsMount`.
	FAsdaWornParts LastAppliedParts;
	uint8 LastAppliedSex = 0;          // пол последней сборки
	bool bHasLastAppliedParts = false; // сборка уже была
	void TickFaceExpression(float DeltaSeconds);   // покадрово: смена действия, конец клипа стойки, моргание
	void SetFaceMode(int32 Mode);                  // порт `meshSetPartBlendFactorsByMode@0x9E1DA0`: режим + сброс таймера
	void ApplyFaceTexture();                       // поставить на слот лица текстуру режима `FaceMode`
	int32 FaceMode = 0;                            // режим лица (эталон `gap_A42[22668]`)
	int32 FaceShownMode = -1;                      // режим, чья текстура стоит на слоте (-1 — неизвестно, поставить заново)
	float FaceTimerMs = 0.f;                       // таймер режима, мс (эталон `gap_A42[22664]`)
	int32 FaceLastState = -1;                      // состояние действия прошлого кадра
	double FaceStanceSinceS = -1.0;                // отметка входа в боевой покой, уже учтённая лицом
	double FaceClipEndS = 0.0;                     // конец прохода клипа боевого покоя (0 — не ждём)
	FString MountAnimSet;      // «0000».. «0017» (EffectEntry записи транспорта)
	FString MountModelName;    // «itm1_n_96_vehicle_00NN» (ModelEntry записи) — для клипов транспорта
	// Путь сидячего клипа по варианту: 000 = покой, 001 = шаг, 002 = движение.
	FString MountClipPath(const TCHAR* Variant) const;
	// ★ СИНХРОН СЕДОКА И ТРАНСПОРТА (эталон §2.2: таблица состояний CChar_SetState меняет клип
	//   транспорта [1316]+0 (_000) / +128 (_002) ТАМ ЖЕ, где клип персонажа — оба стартуют с кадра 0
	//   ВМЕСТЕ). Играет anim_00 (покой) / anim_02 (движение) на MountMesh, всегда луп.
	void PlayMountClip(bool bMove);

	// ★ Флаг клана на транспорте (см. публичную секцию выше).
	bool bClanFlagOn = false;              // эталон CCharacter+2505 (clanFlagOn)
	FAsdaClanFlagData ClanFlagData;        // 40-байтный дизайн эмблемы (+2506)
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> ClanFlagMID;   // материал ПОЛОТНИЩА с RT-эмблемой
	int32 ClanFlagClip = -1;               // текущий клип флага: 0/1 = покой, 2 = движение (эталон +568)
	float ClanFlagClipLeft = 0.f;          // остаток текущего клипа (эталон: смена по КОНЦУ клипа)
	bool bClanFlagBoneWarned = false;      // однократное предупреждение «у транспорта нет Flag_Link»
	// Создать/убрать флаг по эталонному условию (clanFlagOn && bMounted && кость Flag_Link есть).
	void UpdateClanFlagVisual();
	// Выбор клипа флага (эталон mapobj_updateFlagLinkAnim: state 1 → клип 2; иначе по концу клипа
	//   rand()%10 <= 5 ? клип 1 : клип 0).
	void TickClanFlagAnim(float DeltaSeconds);

	// ══════ ★★★ ТАНЦПОЛ В ALPEN — автоматический танец по зоне ═══════════════════════════════════
	//   Разбор: `Reports/alpen_dancefloor_trigger.md`. В оригинале это единственная зона такого
	//   рода во всей игре, и она захардкожена в клиенте — сервер о ней не знает вовсе.
	//   `sub_A220E0` (`render_core.c:3546-3564`, миснейм `Camera_UpdateListener`) каждый кадр в
	//   состояниях покоя проверяет: карта Alpen и позиция внутри прямоугольника → взводит флаг
	//   `CCharacter+0x33090`. Потребитель `CGameUI_UpdateFadeAnim@0xA17B00` (`game_main.c:8217-8241`)
	//   вместо клипа покоя берёт `41 + (rand()%10 <= 5)` (танец 1 или 2), играет его и шлёт серверу
	//   4024 с кодом 112/113; сервер рассылает 4025, и танец видят все. Клип кончился — снова покой,
	//   и всё повторяется, пока стоишь на полу.
	void TickAutoDanceZone(float DeltaSeconds);

	// ══════ ★ СЛЕДЫ И ЗВУК ШАГОВ ПО МАТЕРИАЛУ ПОВЕРХНОСТИ ════════════════════════════════════════
	//   Эталон `CCharacter_ApplyMoveState@0xA199E0` (`entity_char.c:21014-21080`) дважды за цикл бега
	//   смотрит на материал тайла под ногами (байт0 флага `.TER`) и роняет след со звуком: на фазе
	//   клипа 300 и 600. Флаги 0x400/0x800 не дают повторить шаг в том же цикле.
	//     0 — облачко пыли `eff_1_char_99_move01-a_00` + `S_c_run1`
	//     1 — всплеск `eff_1_char_99_move02-a_00` + `S_c_run0`, но ТОЛЬКО если нога над водой
	//     2 — `S_c_run2`   3 — `S_c_run3`   4 — `S_c_run0`   прочее — `S_c_run1`
	//   Звук в оригинале звучит только у своего персонажа (гейт `effectSlots[112].field_40`),
	//   а след видят все. Разбор: `Reports/alpen_dancefloor_trigger.md` §5.
	void TickFootsteps(float DeltaSeconds);
	float FootstepPhase   = 0.f;   // фаза цикла бега, мс (эталонный Float13)
	uint8 FootstepDoneMask = 0;    // 1 = шаг на 300 сыгран, 2 = на 600 (эталонные флаги 0x400/0x800)

	bool  bInAutoDanceZone   = false;   // прошлое состояние «внутри зоны» — для записи в лог по переходу
	float AutoDanceCooldown  = 0.f;     // сколько ещё играет текущий клип танца

	void PlayClanFlagClip(int32 Idx);
	// Клип состояния Move: отход назад (слот 24 back01, эталон pModelObj+1536); бег = слот 5 run01
	//   (слот 4 walk01 только под slow-баффом 112/186 — эталон CChar_SetState case 1/2), а при крыльях —
	//   35 (ch4_*_00_move_00, взмахи; эталон charUpdateActionStateTick@0xA216B0 подменяет ТОЛЬКО бег
	//   и ВСЕГДА клипом набора _00 — от оружия трек 35 не зависит).
	int32 MoveClip() const
	{
		if (bSwimming)  { return kClipSwimMove; }   // ★ 23.10.2026: плывёт — swim01 (донор: слот 39 вместо бега/пятки/крыльев)
		// ★ 24.10.2026 ходьба вприсядку (донор: слот 40 вместо слота 5); [ПОРТ] и пятясь — присед, а не back01 стоя
		//   (пятясь клип идёт назад — знак темпа `CrouchClipTimeSign`).
		if (IsCrouchWalkShown()) { return kClipCrouchWalk; }
		if (bBackpedal) { return MotionClipIdx(EAsdaMotionSlot::Back01); }
		if (bWingsOn)   { return 35; }
		return MotionClipIdx(bSlowWalkBuff ? EAsdaMotionSlot::Walk01 : EAsdaMotionSlot::Run01);
	}

	// ═══ ★ ПРЫЖОК / ПЛАВАНИЕ (донор AsdaFableClassic, 23.10.2026) — состояние; разбор у `StartJump` ═══
	bool bJumpActive = false;            // идёт прыжок (донор +0x35960)
	double JumpStartS = 0.0;             // мировое время начала прыжка (время клипа донора +0x34 слота 24/37)
	int32 JumpClipShown = -1;            // какой клип прыжка играет сейчас (235/236), -1 — не показан (вода, верхом, занят)
	FTimerHandle JumpTimer;              // конец показанного клипа прыжка
	bool bJumpKeyReleased = true;        // фронт нажатия (донор `byte_EDB390`: 1 — клавиша была отпущена)
	FString LastJumpRefusal;             // самотест: причина последнего отказа
	// ═══ ★ ХОДЬБА ВПРИСЯДКУ / ВИД НЕВИДИМОСТИ (24.10.2026) — состояние; разбор у `SetCrouch` / `SetStealthVisual` ═══
	bool bCrouchOwn = false;             // свой присед по Ctrl
	bool bCrouchNet = false;             // сосед: последний грант 4007 со статусом 4
	bool bSkillStealth = false;          // состояние 112/186 (ставит UAsdaStealthSubsystem)
	bool bCrouchCtrlWasDown = false;     // Ctrl был зажат в прошлом кадре (фронт отпускания)
	bool bCrouchOtherDuring = false;     // пока Ctrl зажат, нажималось что-то ещё (сочетание — не переключаем)
	FString LastCrouchRefusal;           // самотест: причина последнего отказа приседа
	float StealthAlpha = 1.f;            // α невидимости (эталон +6329), 1 — обычный вид
	bool bStealthRendered = true;        // эталонный RenderCtx_SetEnable: false — актор скрыт
	TArray<FKey> CrouchPrevKeysDown;     // клавиши/кнопки, зажатые в прошлом кадре (новое нажатие при зажатом Ctrl — сочетание)
	mutable int32 CrouchRunIdxCache = -1;     // темп приседа: для какого клипа бега закэширована длина
	mutable float CrouchRunLenCache = 0.f;    // … длина клипа бега набора, с
	mutable float CrouchLenCache = 0.f;       // … длина anim_239, с (0 — не загружена)
	// Перезапустить клип хода, если персонаж идёт и выбор клипа хода сменился (присед вкл/выкл, невидимость).
	void RefreshMoveClipAfterCrouchChange();
	// ★ 24.10.2026: смена «пятясь ↔ вперёд» на ходу. Клип хода остался приседом — только знак темпа (время клипа разворачивается
	//   с той же позы, без перезапуска с кадра 0); иначе — прежний перезапуск клипа хода (`PlayWorldClip(MoveClip())`).
	void PlayMoveClipAfterBackpedalSwitch();
	// Переписать α во ВСЕ уже подменённые слоты (без поиска компонентов) — α невидимости меняется каждый кадр.
	void UpdateTranslucentAlphaOnly();
	bool bSwimming = false;              // плывёт (донор +0x35961)
	bool bMountInWater = false;          // транспорт в глубокой воде (донор +0x35962)
	float SwimDepth = 0.f;               // глубина воды под персонажем (донор +0x35968 = waterY − groundY)
	float SwimEvalAccum = 0.f;           // накопитель тика пересчёта воды стоящего персонажа (позиция не менялась)
	FVector2D SwimEvalAtGame = FVector2D(FLT_MAX, FLT_MAX);   // где считали воду в последний раз
	float SwimSplashAccumMs = 0.f;       // брызги транспорта в воде: накопитель (донор — раз в 1000 / 100 мс)
	bool bSwimSplashLatch = false;       // брызги заплыва: одна на окно времени клипа (донор — глобальная защёлка)
	// ★ 24.10.2026: подъём «шапки» над головой вслед за телом в прыжке (разбор у `GetOverheadLiftZ`).
	float JumpLiftZ = 0.f;               //   текущий подъём тела клипом прыжка над точкой актора (≥ 0)
	float JumpRestRootZ = 0.f;           //   высота кости корня (M/G) над актором в покое/ходе перед прыжком — нулевая отметка
	bool bJumpRestValid = false;         //   нулевая отметка снята (кость корня найдена)
	// Высота кости корня тела (M — мужская, G — женская модель) над точкой актора без «хвоста» поправки; NaN — кости нет.
	float ReadRootBoneHeight() const;
	// Покадровое: подъём `JumpLiftZ` из живой позы, пока виден клип прыжка; иначе плавно гасится к нулю.
	void TickJumpLift(float DeltaSeconds);
	// Высота стояния в точке: земля, а в глубокой воде — поверхность − 90 (донор: Y = waterY − 20 против ground + 70);
	//   попутно пересчитывает признаки плавания. Заменяет `TerrainZ` при постановке персонажа.
	float StandZ(float UeX, float UeY);
	// Пересчитать признаки плавания по текущей позиции; при смене — перезапустить клип состояния и высоту.
	void UpdateSwimState(bool bForce);
	// Покадровое: время клипа плавания, брызги, переход покой↔ход во время прыжка.
	void TickSwimAndJump(float DeltaSeconds);
	// Показать клип прыжка под текущее состояние (покой/ход) с позицией, перенесённой из прежнего клипа прыжка.
	void ShowJumpClipForState();
	// Конец показанного клипа прыжка — вернуть клип состояния (донор: со следующего кадра играет обычный idle/run).
	void OnJumpClipEnd();
	// Снять прыжок без показа (смерть, посадка, эмоция и т. п.).
	void CancelJump(const TCHAR* Why);
	// Брызги донора `0x4D33E0`: mode 0 — swimeffet01 у позиции (+20 по высоте), mode 1 — swimeffet в 80 ед. впереди.
	void SpawnSwimSplash(int32 Mode, float Scale);
};
