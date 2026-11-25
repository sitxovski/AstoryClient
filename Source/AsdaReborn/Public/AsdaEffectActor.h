// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor — эффект карты AsdaStory: точная модель эмиттера движка (верифицирована live.dmp,
// см. Reports/effects_system_verification.md + память map-effect-emission).
// Механика 1:1: эмиссия emitAccum+=rate*dt (floor, clamp n>rate→1); спавн rand_pm=(rand-rand) —
// ТРЕУГОЛЬНОЕ распределение [-1,1]; конусная скорость (азимут 2π, угол ±coneAngle°, ось ConeAxis);
// march-дельты (death-birth)/life; тик pos+=vel·dt, vel+=(0,0,-10)·grav·dt (UE Z-вверх = game Y-вверх);
// эмиттер-параметры маршируют по track-узлам (кусочно-линейно; дефолт = 2 узла birth/death — так строит
// рантайм при disk-count=0). Тип 0xB = single-quad (без частиц). Рендер: camera-facing квады с vertex-color
// (цвет×текстура — как DrawPrimitiveUP оригинала), ProceduralMesh секция на эмиттер, дистанц-кулл ~10000.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/AsdaVisState.h"   // ★ FAsdaVisState — значимость эффекта по видимости (волна CPU 07.09.2026)   ★ R3: FAsdaVisState — узкий заголовок вместо AsdaVisibilitySubsystem.h
#include "World/AsdaFxLightSpec.h"           // ★ FAsdaFxLightSpec — числа света ЭТОГО эффекта (волна Ф2-ВСЕ 09.09.2026)
#include "UObject/ObjectSaveContext.h"        // ★ FObjectPreSaveContext — снимок поэмиттерного свечения перед записью (волна Ф8 10.09.2026)
#include "AsdaEffectActor.generated.h"

// ★★★ [ПОРТ] GM-СКРЫТИЕ (27.09.2026, доводка P39 «невидимость до конца») — ОБЩАЯ ТОЧКА.
//   Метка актора, которую `UAsdaWorldSubsystem::SetCharHiddenByGm` ставит персонажу (и его хранителю)
//   на время GM-скрытия и снимает при показе. По ней эффект, появившийся на скрытом персонаже ПОЗЖЕ
//   момента скрытия (бафф от соседа, свечение при смене оружия, столб 4056 и т.п.), гасит себя сам
//   (`AAsdaEffectActor::Tick`). [ОРИГИНАЛ] у эталона эффекты — узлы самого `CCharacter`, и бит
//   `char+0x40C` гасит их все разом, в том числе появившиеся позже; у нас эффект — отдельный актор.
inline constexpr const TCHAR* kAsdaGmHiddenTagName = TEXT("AsdaGmHidden");
// ★ 24.10.2026: ВТОРАЯ причина «носитель не рисуется» — невидимость навыка (112/186, `UAsdaStealthSubsystem`, α 0). Та же логика:
//   эффекты на носителе (приаттаченные И следующие за ним через FollowActor — attach-3/5/6/9/10/12) и появившиеся позже гасят
//   себя сами (`AAsdaEffectActor::Tick`), удочка рыбалки — тоже. Общая проверка — `AsdaHostHiddenByTag` (любая из двух меток).
inline constexpr const TCHAR* kAsdaStealthHiddenTagName = TEXT("AsdaStealthHidden");
// Актор помечен скрытым любой системой (GM-скрытие или невидимость навыка).
inline bool AsdaHostHiddenByTag(const AActor* A)
{
	if (!A) { return false; }
	static const FName kGm(kAsdaGmHiddenTagName), kSt(kAsdaStealthHiddenTagName);
	return A->Tags.Contains(kGm) || A->Tags.Contains(kSt);
}

class UProceduralMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;                // ★ Ф5: MID «силы свечения» (GlowMIDs)

// ★ Д1: режим посадки эффекта на носителя (единая точка ConfigureBoneAttach). Разные носители хотят
//   чуть разного набора флагов — вместо ручного набора в 5 местах (класс бага «забыли bFollowPosition»).
// ★ 15.10.2026 (ревизия mount_fx, п. 4): счётчики цены эффектов для бенча `asda.Mount.FxBench` — суммарные такты Tick
//   всех AAsdaEffectActor, число Tick и число шагов симуляции с начала процесса. Только не-Shipping (в Shipping — нули).
ASDAREBORN_API void AsdaFxBenchCounters(uint64& OutTickCycles, uint64& OutTicks, uint64& OutSteps);

UENUM()
enum class EAsdaFxAttachMode : uint8
{
	BoneFull,        // attach-10: только полный трансформ живой кости (диагностика asda.FxFollowLagTest)
	Wing,            // attach-10 крыльев: + FollowOffset, БЕЗ strictQuad, skel по FindComponentByClass
	WeaponGlow,      // attach-10 свечения заточки: + strictQuad + attachMatrix (оружие за спиной)
	OwnerBoundFx,    // attach-10 эффекта хранителя на кости владельца: + strictQuad
	CharWorld,       // attach-6: мировая матрица персонажа (шлейф транспорта), без кости
	// ★ ТОЛЬКО ПОЗИЦИЯ, БЕЗ ОРИЕНТАЦИИ — канал эффектов СОСТОЯНИЙ (баффов), `skill_fx_anim.md` §17.
	//   Соответствующие хендлеры эталона делают РОВНО `CEffect_SetPos(node+8, …)` и матрицу ноды не
	//   трогают (в отличие от attach-6/8, которые пишут в неё полную матрицу персонажа):
	CharPos,         // attach-3/5/9/12: позиция НОСИТЕЛЯ (персонажа/сущности) + Offset
	BonePos,         // attach-7: позиция КОСТИ носителя + Offset (`effectNodeUpdateCasterItemPos`)
};

// Track-узел: значения эмиттер-параметров в момент Time (кусочно-линейная интерполяция между узлами).
USTRUCT(BlueprintType)
struct FAsdaFxNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) float Time = 0.f;          // сек от старта периода
	UPROPERTY(EditAnywhere) float Rate = 0.f;          // частиц/сек
	UPROPERTY(EditAnywhere) float SizeX = 50.f;        // half-extent, мир.ед.
	UPROPERTY(EditAnywhere) float SizeY = 50.f;
	UPROPERTY(EditAnywhere) float Speed = 0.f;         // модуль скорости конуса
	UPROPERTY(EditAnywhere) float ConeAngle = 30.f;    // раствор конуса, градусы
	UPROPERTY(EditAnywhere) float Alpha = 1.f;
	UPROPERTY(EditAnywhere) FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere) float Gravity = 0.f;       // gravScale (диск, ×9.8 при спавне)
	UPROPERTY(EditAnywhere) FVector ConeAxis = FVector(0, 0, 1);  // UE-координаты (game (0,1,0)=вверх → UE +Z)
	// КОЛЬЦО (тип 0xE, SPEC §11): все каналы маршируют между узлами
	UPROPERTY(EditAnywhere) float RingScale = 1.f;     // uniform scale
	UPROPERTY(EditAnywhere) float RingRoll = 0.f;      // прокрутка вокруг своей Y («шов»), градусы
	UPROPERTY(EditAnywhere) FVector RingRot = FVector::ZeroVector;   // Эйлер X/Y/Z, градусы (game-оси)
	UPROPERTY(EditAnywhere) float RingH = 0.f;         // высота верхней кромки (game Y)
	UPROPERTY(EditAnywhere) float RingTopR = 0.f;      // радиус верхней кромки
	UPROPERTY(EditAnywhere) float RingBotR = 0.f;      // радиус нижней (y=0)
	UPROPERTY(EditAnywhere) float RingU = 0.f;         // UV-скролл U
	UPROPERTY(EditAnywhere) float RingV = 0.f;         // UV-скролл V
	UPROPERTY(EditAnywhere) FVector RingBasePos = FVector::ZeroVector;  // basePos-канал (диск 0x12C/0x138): СЫРОЙ МИР, game-оси (0xB: не вращается ни каналами, ни матрицей узла)
	UPROPERTY(EditAnywhere) FVector RingPosOfs  = FVector::ZeroVector;  // pos-канал (диск 0x150/0x15C): орбитальный — вращается канальными вращениями и матрицей узла (game-оси)
};

USTRUCT(BlueprintType)
struct FAsdaFxEmitter
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) int32 JebType = 0xD;
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> Material = nullptr;   // MIC (текстура+blend)
	// ★ Бленд D3D 6:4 (INVSRCALPHA:INVSRCCOLOR — «тёмный дым» flammio-a_09/10, worker02): Final =
	//   srcC×(1−srcA) + dst×(1−srcC). UE-декомпозиция: Material = Modulate-часть (dst×(1−Tex×VC.rgb)),
	//   MaterialB = Additive-добавка (Tex×VC.rgb×(1−VC.a)) в парной секции NumEmitters+i. nullptr = один проход.
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> MaterialB = nullptr;
	UPROPERTY(EditAnywhere) TArray<FAsdaFxNode> Nodes;  // >=2 (birth/death); маршируются по времени периода
	UPROPERTY(EditAnywhere) float Period = 5.f;         // сек; endMode карт = вечный loop/restart
	UPROPERTY(EditAnywhere) int32 EndMode = 3;          // 0=stop 1/2=restart 3=loop (карты: все бессмертны)
	UPROPERTY(EditAnywhere) float LifeBase = 1.f;       // жизнь частицы, сек
	UPROPERTY(EditAnywhere) float LifeVar = 0.f;
	UPROPERTY(EditAnywhere) float SizeXVar = 0.f;       // вариации при спавне (rand_pm*Var)
	UPROPERTY(EditAnywhere) float SizeYVar = 0.f;
	UPROPERTY(EditAnywhere) float SpeedVar = 0.f;
	UPROPERTY(EditAnywhere) float AlphaVar = 0.f;
	UPROPERTY(EditAnywhere) FVector ColorVar = FVector::ZeroVector;      // RGB jitter
	UPROPERTY(EditAnywhere) float GravityVar = 0.f;
	UPROPERTY(EditAnywhere) FVector PosBase = FVector::ZeroVector;       // СТАРТ точки эмиссии (UE-оси)
	UPROPERTY(EditAnywhere) FVector PosRange = FVector::ZeroVector;      // разброс СТАРТА (rand_pm, UE-оси)
	// ★★★ ДВИЖЕНИЕ САМОГО ЭМИТТЕРА (0xD/3/0x1B), отчёт `Reports/item_fx_audit.md` §2/§3.
	//   ЭТАЛОН `CEmitter_InitReset@0x120F0E0` при (пере)старте эмиттера:
	//       emitPos = PosBase + rand_pm·PosRange;  emitPosVel = (PosTarget − emitPos)/период;
	//       rotCur  = EmitRotStart + rand_pm·EmitRotVar;  rotVel = (EmitRotEnd − rotCur)/период;
	//   и `CEmitter_Update_v3@0x1211C20` КАЖДЫЙ КАДР: emitPos += dt·emitPosVel, rotCur += dt·rotVel,
	//   после чего мировая точка эмиссии = позиция холдера + emitPos·(RotX·RotY·RotZ)·RotY(holder).
	//   То есть `posBase/posTarget/posRange` — обычная тройка (СТАРТ, КОНЕЦ, разброс), как у размера,
	//   скорости и rate; порт же держал точку эмиссии КОНСТАНТОЙ и вращения не знал вовсе. Симптом —
	//   «эффект статичен и смещён»: у эмиттеров со скоростью частиц 0 (parup_* «Трофейникуса»,
	//   Object5 премиум-усилений, EXP_P* Камня души) марш точки эмиссии — ЕДИНСТВЕННОЕ движение.
	//   `bEmitterMotion` = ГЕЙТ (дефолт false = поведение как было): ставит сборщик там, где поля
	//   реально прочитаны с диска. Риск для карт измерен: 275 партикл-эмиттеров 23 карт,
	//   posTarget == posBase у ВСЕХ ⇒ принятые эффекты карт измениться не могут.
	UPROPERTY(EditAnywhere) FVector PosTarget = FVector::ZeroVector;     // КОНЕЦ марша точки эмиссии (UE-оси)
	UPROPERTY(EditAnywhere) FVector EmitRotStart = FVector::ZeroVector;  // rotX/rotY/rotZ старт, градусы, GAME-оси
	UPROPERTY(EditAnywhere) FVector EmitRotEnd   = FVector::ZeroVector;  // rotX/rotY/rotZ конец
	UPROPERTY(EditAnywhere) FVector EmitRotVar   = FVector::ZeroVector;  // разброс старта (в архиве везде 0)
	UPROPERTY(EditAnywhere) bool bEmitterMotion = false;
	UPROPERTY(EditAnywhere) int32 SpawnMode = 0;        // 2 = вектор VelBase вместо конуса
	UPROPERTY(EditAnywhere) FVector VelBase = FVector::ZeroVector;       // UE-оси (spawnMode=2)
	UPROPERTY(EditAnywhere) FVector VelRange = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) FVector VelTarget = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) bool bSingleQuad = false;   // тип 0xB: один keyframe-квад, частиц нет
	UPROPERTY(EditAnywhere) int32 BillboardMode = 0;    // 0=камера 1=цилиндр(верт. ось) 2=rot
	// ★ Атлас-раскадровка UV частиц (тип 0x17 «блёстки» крыльев / 0x1B / 0x13 флипбук; §D wings_soulmate_re).
	//   При Cols*Rows>1 каждая частица за свою жизнь проходит РОВНО один цикл атласа: кадр = Age/(Life/(Cols·Rows)),
	//   col=кадр%Cols, row=кадр/Cols, UV-регион ячейки. Cols=Rows=1 (дефолт) → полный [0..1] (не ломает 0xD).
	// ★ Спавн-объём эмиттера (КРУГ 3 крыльев, live cdb-захват wingcap_original.log + дизасм):
	//   диск +0x110/+0x114 → emitter+0x140/+0x144; таблица мод @+0x148 (ctor 0x120DE20).
	//   Мода 5 (@0x1213340): мировая точка эмиссии = база + rand_pm(треугольный)·Radius ПО КАЖДОЙ ОСИ
	//   (куб ±R в мировых осях). ВСЕ крылья: mode=5, R=50 → облако ±50 см вокруг персонажа.
	//   Дефолт mode=0 = точечный спавн (историческое поведение всех прочих эмиттеров).
	UPROPERTY(EditAnywhere) int32 SpawnPosMode = 0;
	UPROPERTY(EditAnywhere) float SpawnPosRadius = 10.f;
	UPROPERTY(EditAnywhere) int32 AtlasCols = 1;
	UPROPERTY(EditAnywhere) int32 AtlasRows = 1;
	// ★ ЗНАК КАНАЛЬНЫХ ВРАЩЕНИЙ меш-эмиттера (0x11/0x12/0x1F). false (ДЕФОЛТ) = историческое поведение,
	//   под которое КАЛИБРОВАНЫ ЭФФЕКТЫ КАРТ (фонари adsalight, радуга дирижаблей и пр. — их узлы
	//   подбирались визуально именно с ним; менять глобально НЕЛЬЗЯ — регрессия разворота на 180°).
	//   true = математически строгий знак (сопряжение P⁻¹·R·P, §H6) — ставится ТОЛЬКО генератором
	//   эффектов персонажей (крылья), где он сверен с live.dmp по всем трём осям.
	UPROPERTY(EditAnywhere) bool bStrictChannelRot = false;
	// ★ ОРБИТА pos-канала у БИЛЛБОРД-квадов (0xC billboardMode≠0). Эталон (модель конверсии §3, лоадер-трейс):
	//   pos-канал вращается КАНАЛЬНЫМИ вращениями → при rotY 0→360 и pos=(50,0,0) квад ЛЕТАЕТ ПО КРУГУ
	//   радиусом 50 (кольцо соулмейта: 8 квадов с разными фазами). Порт же крутил квад «на месте» (rotY
	//   уходил во внутриплоскостной спин), из-за чего все 8 квадов стояли в одной точке = «корявая анимация».
	//   Флаг (дефолт false) — чтобы не трогать откалиброванные карт-эффекты; ставится генератором char-fx.
	UPROPERTY(EditAnywhere) bool bStrictQuadOrbit = false;
	// ★ pos-КАНАЛ МЕШ-ЭМИТТЕРА через ПОЛНУЮ W (эталон 0x1229810: pos трансформируется Scale·Rot·parent,
	//   ground truth live.dmp §H3 — у крыла pos(0,2.5,0)·W = +25 см вверх). false (ДЕФОЛТ) = ИСТОРИЧЕСКОЕ
	//   поведение: pos прибавляется СЫРЫМ ПОСЛЕ канальных вращений — под него откалиброваны меш-эффекты
	//   КАРТ (alpenjewels, soulidol, tower, popo). true ставится ТОЛЬКО генераторами эффектов
	//   персонажей/транспорта (asda_build_char_fx / asda_build_mount_fx), где формула сверена с дампом.
	//   ⚠ ЗАКОН ПРОЕКТА: любое изменение ОБЩЕГО кода эффектов — только под флагом с дефолтом «как было».
	UPROPERTY(EditAnywhere) bool bStrictMeshPos = false;
	// ★ basePos КОЛЬЦА (0xE) через ЛОКАЛЬ актора (эталон MatrixTranslate(pos+basePos)·boneW): basePos
	//   получает scale+rotation мировой матрицы. false (ДЕФОЛТ) = ИСТОРИЧЕСКОЕ поведение карт — basePos
	//   добавляется В МИРЕ без масштаба; под него откалиброваны rainbow/rainbow2/фонари adsalight/tower
	//   (внесение локальной схемы в общую ветку без флага = регрессия №2 2026-07-30: rainbow уехал на
	//   338 юн, фонари на 77–182). true ставится ТОЛЬКО генераторами char/mount-эффектов; у кольца
	//   соулмейта актор identity — формулы совпадают, флаг включён для соответствия эталону.
	UPROPERTY(EditAnywhere) bool bStrictRingBasePos = false;
	// ★★★ [ПОРТ] «ОФСЕТЫ ЭТОГО КОЛЬЦА ЛЕЖАТ В ЭТАЛОННОЙ РАСКЛАДКЕ» (2026-09-11, задача «эффекты
	//   навыков не как в игре»). ЭТО ПРИЗНАК ДАННЫХ, А НЕ ПОВЕДЕНИЯ.
	//   ЗАЧЕМ. Печка кладёт офсеты кольца 0xE ДВУМЯ несовместимыми раскладками:
	//     ЛЕГАСИ  — `RingBasePos = basePos + pos` (без негации z), `RingPosOfs = 0`, у трек-узлов
	//               офсет вообще константа [0,0,0];
	//     ЭТАЛОН  — `RingBasePos = negate-z(POS-канал 0x3E4)`, `RingPosOfs = basePos 0x3FC` (сырой мир).
	//   Читающая их ветка `bEtalonMapPath` до сих пор включалась ПО СПОСОБУ ПРИВЯЗКИ АКТОРА
	//   (`bStrictQuadAttach`, который ставят `asda.Skill.StrictFx` / `asda.Item.StrictFx` /
	//   `WeaponGlow`), то есть ОДИН И ТОТ ЖЕ ассет читался по-разному в зависимости от того, КТО его
	//   заспавнил: у навыка — эталонной веткой, на стенде `asda.Fx.Light.Stand` и у эффектов
	//   состояний (`UAsdaEffectsSubsystem::SpawnBuffFx` флаг не ставит) — исторической.
	//   Отсюда два следствия: (1) диагностика стендом показывала НЕ ту ветку, что видит игрок
	//   (закон diagnostic-must-use-real-path), (2) исправленную раскладку нельзя было раскатать,
	//   не сломав тех потребителей, кто strict не ставит.
	//   Флаг снимает эту связь: раскладку объявляет САМ АССЕТ. Ставит его ТОЛЬКО тот сборщик,
	//   который реально уложил эталонную раскладку (`asda_build_char_fx.RING_ETALON_FX`).
	//   ДЕФОЛТ false ⇒ поведение всех ранее собранных эффектов (карты, крылья, соулмейт, транспорт,
	//   предметы, свечение заточки) БИТ-В-БИТ прежнее.
	//   ⚠ ЗАКОН ПРОЕКТА: любое изменение ОБЩЕГО кода эффектов — только под флагом с дефолтом «как было».
	UPROPERTY(EditAnywhere) bool bRingEtalonLayout = false;
	// ★★★ [ПОРТ] ОТМЕНА ХИРАЛЬНОГО ОТРАЖЕНИЯ У КОЛЬЦА 0xE (2026-09-10, задача «смещение эмиттеров»).
	//   КАНОН 0xE по дизасму (ringDraw@0x1215C10, `Docs/Port/systems/EFFECTS_CONVERSION_MODEL.md`): в цепочке
	//   центра и геометрии кольца ЗЕРКАЛА НЕТ — `det(N520) = +1` у ВСЕХ attach-узлов. Хиральный рефлект
	//   попал в общую ветку 28.07.2026 как компенсация ТРАНСПОНИРОВАННЫХ ротаций (эра до канона) и был
	//   снят 31.07 только для «чистых» 0xE-эффектов (`pure_ring_attached`, фонари/радуга2). Эффекты,
	//   у которых В ТОМ ЖЕ `.jeb` есть НЕ-кольцевой эмиттер (башня alpen `eff_noeaplhentower00_a_00`:
	//   два кольца 0xE + квад 0xB), под тот гейт не попали и продолжают отражаться.
	//   false (ДЕФОЛТ) = ИСТОРИЧЕСКОЕ поведение (рефлект как был) — карты, соулмейт, char/mount не тронуты.
	//   true ставит ТОЛЬКО печка карт и ТОЛЬКО кольцам, у которых нормаль пришла от РЕАЛЬНОГО сокета
	//   декорируемого меша (не дефолтная (0,0,1)): у дефолтной нормали рефлект неотличим от негации z
	//   pos-канала и на принятых эффектах (rainbow, inferoshouse) ничего не меняет — их не трогаем.
	//   ⚠ ЗАКОН ПРОЕКТА: любое изменение ОБЩЕГО кода эффектов — только под флагом с дефолтом «как было».
	UPROPERTY(EditAnywhere) bool bRingNoChiral = false;
	// ★★★ [ПОРТ] ПИВОТ КВАДА = СОБСТВЕННЫЙ ЦЕНТР ЭМИТТЕРА (2026-09-10, задача «эмиттер облетает эффект»).
	//   ЭТАЛОН `effectQuad_updateAndDrawBillboard@0x120AEB0` (`RE/etalon/effects_particles.c:97270`):
	//     вершины  = (±halfW, 0, ±halfH) — pos-канала в геометрии НЕТ ВООБЩЕ;
	//     M._11-33 = RotY(roll)·RotX·RotY·RotZ·N48·N520      (вращение квада ВОКРУГ СВОЕГО ЦЕНТРА);
	//     M._41-43 = basePos + posChan·(RotX·RotY·RotZ)·N48 + позиция узла   (центр — ОТДЕЛЬНО, БЕЗ roll).
	//   Историческая карт-ветка порта прибавляла pos-канал К УГЛАМ квада ДО вращений ⇒ вращение шло вокруг
	//   НАЧАЛА КООРДИНАТ ЭФФЕКТА, а не вокруг центра эмиттера = ОРБИТА (живой замер `asda.Fx.SectionDump`,
	//   `eff_mdel_obsdoor02-a_00` эм.13 «S-1»: центроид на 4 фазах (−17,1,300)→(146,117,−234)→(39,295,40)→
	//   (−142,198,175), |r| = 300.0 неизменно ⇒ окружность радиуса 300 вокруг актора; эталон = точка (0,0,300)).
	//   Вторая половина того же дефекта — ЗНАК: центр считался ТРАНСПОНИРОВАННЫМИ лямбдами (угол −A), из-за
	//   чего pos=(0,0,−300) при rotX=90 давал −300 вместо +300 (те же 2 квада «S-C-1/S-C-2» стояли на 600 юн
	//   НИЖЕ билбордов того же эффекта). Флаг считает центр эталонными D3DX row-vector ротациями
	//   (RotationX/Y/Z @0xFF4ADD/0xFF4BDA/0xFF4CED) — той же формулой, что уже принята для `bStrictQuadAttach`.
	//   false (ДЕФОЛТ) = ИСТОРИЧЕСКОЕ поведение (pos в углах, транспонированный знак) — ничего не трогает.
	//   true ставит ТОЛЬКО печка карт квад-эмиттерам (`asda_place_effects.main`); откат перепечкой с
	//   `ASDA_FX_NO_QUAD_PIVOT_FIX=1`. Геометрию (углы квада) флаг НЕ трогает.
	//   ⚠ ЗАКОН ПРОЕКТА: любое изменение ОБЩЕГО кода эффектов — только под флагом с дефолтом «как было».
	UPROPERTY(EditAnywhere) bool bStrictQuadPivot = false;
	// ★ Мерцание co-planar пары меш-эмиттеров (форджинг eft_creation01 a_00/a_01): в оригинале слои с
	//   ОДИНАКОВОЙ геометрией пишут глубину (ZWRITE ON) и сортируются по равной дистанции нестабильно →
	//   кадр к кадру виден попеременно то один, то другой слой. Эмиттеры с флагом рисуются по очереди
	//   (кадровое чередование по индексу) — честная эмуляция механизма, не выдуманная синусоида.
	UPROPERTY(EditAnywhere) bool bFlickerAlt = false;
	UPROPERTY(EditAnywhere) float StartDelay = 0.f;     // задержка старта эмиттера, сек (rec+0x20; каскады волн)
	// МОДЕЛЬ-эмиттер (.jem, JEB 0x11/0x12/0x1F): статичный меш (де-индексированные тройки вершин,
	// UE-локальные с запечённым node-scale); рендерится в трансформе актора (ZWRITE как оригинал — meshes).
	UPROPERTY(EditAnywhere) bool bMeshEmitter = false;
	UPROPERTY(EditAnywhere) TArray<FVector> MeshVerts;
	UPROPERTY(EditAnywhere) TArray<FVector> MeshNormals;
	UPROPERTY(EditAnywhere) TArray<FVector2D> MeshUV0;
	// ★ СКИН .jem (крылья машут!): в .jem есть скелет + треки A5/A6/A7 (t,pos)(t,quat)(t,scale) —
	//   эталон гоняет их через animTracks_evaluateAtTime(pMesh, animFrame), animFrame += 1 КАЖДЫЙ КАДР
	//   (effects_particles.c:103512), заворот на диапазон времён трека. Печка (asda_jem_parse.skin_frames)
	//   считает матрицы inv(W_кость(0))·W_кость(f) в stored-UE осях; здесь — плоский массив
	//   [кадр*SkinBones + кость]. MeshVertBone — палитр-кость каждой вершины (доминирующий вес).
	UPROPERTY(EditAnywhere) TArray<FTransform> SkinFrames;
	UPROPERTY(EditAnywhere) int32 SkinBones = 0;
	UPROPERTY(EditAnywhere) TArray<int32> MeshVertBone;
	// КОЛЬЦО-лента (тип 0xE, SPEC §11): 30 пар вершин по окружности, каналы в узлах Ring*
	UPROPERTY(EditAnywhere) bool bRing = false;
	UPROPERTY(EditAnywhere) float RingUTile = 1.f;     // U-повторов вокруг кольца (диск 0x13C)
	UPROPERTY(EditAnywhere) float RingVA = 0.99f;      // V нижней кромки (диск 0x140); верх = 0.01

	// ★★★ ВОЛНА Ф8 (10.09.2026): СВЕЧЕНИЕ ЭТОГО ЭМИТТЕРА (просьба владельца — «для каждого эмиттера
	//   добавь настройки свечения»). Группа видна прямо в элементе массива «Эмиттеры» панели деталей.
	//   ⚠ ЭТО ПОЛЕ ЖИВЁТ НЕДОЛГО: сборщик каналов (Tools/asda_fx_common.py::finish_effect_bp) при
	//     перепечке из .jeb записывает `emitters` ЦЕЛИКОМ, то есть стирает всё, что лежит внутри
	//     эмиттера. Поэтому ДОЛГОВЕЧНАЯ копия хранится ОТДЕЛЬНО — в AAsdaEffectActor::EmitterGlowSaved,
	//     и после перепечки восстанавливается по устойчивому ключу (имя + тип + номер), а не по
	//     голому порядковому номеру. См. шапку EmitterGlowSaved.
	UPROPERTY(EditAnywhere, Category = "Emitter Glow", meta = (DisplayName = "Свечение эмиттера"))
	FAsdaFxEmitterGlow Glow;
};

// ★ Дедуп №2 В1-C (F15, 11.10.2026): СТАТИЧЕСКИЙ ВЕС «СВЕТИМОСТИ» ЭМИТТЕРА для света эффектов = max по узлам
//   (яркость Rec.709 × альфа ≥ 0 × больший полу-экстент). Общая точка двух дословных копий: выбор донора общего света
//   (`UAsdaFxLightComponent::PickDonorEmitter`) и отбор кошелька (`UAsdaFxLightSubsystem`). Каналы цвета НЕ режутся
//   снизу — как в обеих копиях. Третья формула — `AsdaLum` (`AsdaFxLightComponent.cpp`, свёртка цвета узлов) — режет
//   отрицательные каналы и сюда НЕ сведена (это изменило бы поведение; записано в отчёт В1-C для В2).
namespace AsdaFxLight
{
	FORCEINLINE double EmitterWeight(const FAsdaFxEmitter& E)
	{
		double W = 0.0;
		for (const FAsdaFxNode& N : E.Nodes)
		{
			const double Lum = 0.2126 * N.Color.R + 0.7152 * N.Color.G + 0.0722 * N.Color.B;
			W = FMath::Max(W, Lum * FMath::Max(N.Alpha, 0.f) * FMath::Max(N.SizeX, N.SizeY));
		}
		return W;
	}
}

// ★★★ ВОЛНА Ф8: ДОЛГОВЕЧНАЯ КОПИЯ ПОЭМИТТЕРНЫХ НАСТРОЕК СВЕЧЕНИЯ.
//   Ключ намеренно СОСТАВНОЙ и проверяется по убыванию строгости (AAsdaEffectActor::RestoreEmitterGlow):
//     1) имя эмиттера + тип .jeb  — переживает и перестановку, и вставку эмиттера в середину;
//     2) номер + тип             — спасает эмиттеры с ПУСТЫМ именем (в архиве такие есть);
//     3) номер                   — последняя линия обороны.
//   Голый порядковый номер ключом быть НЕ МОЖЕТ: сборщик пересобирает список из .jeb, и добавленный
//   автором эмиттер сдвинул бы все настройки на один.
USTRUCT()
struct FAsdaFxEmitterGlowSaved
{
	GENERATED_BODY()
	// ★ ВСЕ ЧЕТЫРЕ — VisibleAnywhere, а не голый UPROPERTY(). Причина не косметическая: голое
	//   UPROPERTY() не выставляется НИ в панель деталей, НИ в Python, то есть владелец не может
	//   убедиться, что копия на месте, а самопроверка не может её прочитать — «сохранилось» пришлось
	//   бы принимать на веру (закон «доказывать вторым методом»).
	UPROPERTY(VisibleAnywhere, Category = "Effect Glow", meta = (DisplayName = "Имя эмиттера"))
	FString Name;                        // FAsdaFxEmitter::Name на момент сохранения
	UPROPERTY(VisibleAnywhere, Category = "Effect Glow", meta = (DisplayName = "Тип .jeb"))
	int32 JebType = 0;                   // FAsdaFxEmitter::JebType
	UPROPERTY(VisibleAnywhere, Category = "Effect Glow", meta = (DisplayName = "Номер на момент сохранения"))
	int32 Index = 0;
	UPROPERTY(VisibleAnywhere, Category = "Effect Glow", meta = (DisplayName = "Настройки свечения"))
	FAsdaFxEmitterGlow Glow;
};

// Живая частица (100 слотов/эмиттер — как движок).
struct FAsdaFxParticle
{
	float Age = -1.f, Life = 0.f;                      // Age<0 = слот свободен
	FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
	FVector RGB = FVector::OneVector, RGBRate = FVector::ZeroVector;
	float Alpha = 1.f, AlphaRate = 0.f;
	float SX = 50.f, SXRate = 0.f, SY = 50.f, SYRate = 0.f;
	float Grav = 0.f, GravRate = 0.f;
	// ★ 15.10.2026: трансформ актора эффекта в момент рождения частицы (`SpawnParticle`). Нужен свету эффекта на
	//   движущемся носителе: облако частиц «в кадре носителя» = текущий трансформ · (трансформ рождения)⁻¹ · Pos —
	//   ровно то облако, которое было бы у стоящего носителя (см. AAsdaEffectActor::RestVerts).
	FVector BirthLoc = FVector::ZeroVector;
	FQuat BirthRot = FQuat::Identity;
};

struct FAsdaFxEmitterState
{
	TArray<FAsdaFxParticle> Slots;                     // 100
	float EmitAccum = 0.f, LifeTimer = 0.f;
	float UvAccumU = 0.f, UvAccumV = 0.f;              // ★ 0xB: UV-скролл — АККУМУЛЯТОР (U+=uScroll·dt, обёртка >1→-1), эталон 92525
	// ★ Живое состояние движения эмиттера (гейт FAsdaFxEmitter::bEmitterMotion): точка эмиссии и её
	//   углы маршируют к КОНЦУ за период (эталон CEmitter_InitReset/CEmitter_Update_v3).
	FVector EmitPos = FVector::ZeroVector, EmitPosVel = FVector::ZeroVector;   // UE-оси
	FVector EmitRot = FVector::ZeroVector, EmitRotVel = FVector::ZeroVector;   // градусы, game-оси
	bool bStopped = false;
	bool bStarted = false;                             // StartDelay пройден (LifeTimer обнуляется на старте)
	// ★★★ ЦИКЛ 1:1 (08.10.2026, `asda.Fx.Loop`, `Reports/owner_1008/fx_loop.md`) — живое состояние
	//   конца периода у квадов/колец/мешей. Пишутся ТОЛЬКО режимами ЦИКЛ 1:1, у прежнего пути нули.
	float OverTime = 0.f;                              // сек ПОСЛЕ конца периода при удержании (марш каналов дальше END)
	int32 WrapCount = 0;                               // сколько раз период провернулся (марш-с-обнулением 0x13/0x14, пинг-понг)
	float FlipTime = 0.f;                              // время для кадра атласа-флипбука (продолжается при удержании)
	bool bFrozen = false;                              // endMode 0 по эталону: застыл видимым на КОНЦЕ (не гаснет)
	// ★★★ ЦИКЛ-3 (08.10.2026, решение владельца): обрезка альфы > 1 С ЗАПИСЬЮ в живой канал, как у эталона
	//   (draw 0x0B/0x18 `effectQuad_updateAndDrawBillboard@0x120AEB0`, 0x0C/0x19 `spriteBuildTransformAndDraw@0x120CF10`,
	//   0x0E `effectTrailRender@0x1215C10`: `if (alpha > 1) alpha = 1` в сам канал). Канал эталона марширует
	//   `alpha += dt·rate` и снапается на значение узла при переходе узла / реините / снапе конца, поэтому
	//   записанная поправка живёт до ближайшего снапа: выборка = NodeAt + AlphaClampOfs (см. CommitAlphaClamp).
	float AlphaClampOfs = 0.f;                         // накопленная поправка канала альфы от обрезок (≤ 0)
	int32 AlphaClampKey = -1;                          // ключ «сегмент трека + оборот» последнего кадра; смена = снап узла
	float AlphaClampPrevLife = 0.f;                    // LifeTimer прошлого кадра: шаг назад = рестарт периода (снап)
};

// ★★★ ЦИКЛ 1:1: режим конца периода эмиттера — выводится ОДИН раз из (тип с диска, endMode с диска,
//   endMode ассета, холдер карты) в `AAsdaEffectActor::ResolveLoopInfo`. Таблица соответствия с
//   адресами функций обновления — в `AsdaEffectActor.cpp`, блок «ЦИКЛ 1:1».
enum class EAsdaFxLoopMode : uint8
{
	Legacy = 0,   // данных нет / осознанная правка ассета — прежний путь порта по `E.EndMode`
	Restart,      // 1: рестарт периода (vtbl[1]) — как прежний `Fmod`
	Stop,         // 0 у порта: погасить эмиттер (прежнее поведение одноразовых каналов)
	Freeze,       // 0 у эталона: снап КОНЦА и `lifeTimer = −1` — застыть ВИДИМЫМ до конца контейнера
	Hold,         // 3: часть каналов держится на узле, остальные маршируют наклоном последнего сегмента
	WrapMarch,    // 3 у 0x13/0x14/0x15: `lifeTimer = 0` (флипбук заново), ВСЕ каналы маршируют дальше
	MarchAll,     // 2 у 0x0B/0x0C/0x0E: `return 0` без сброса — ВСЕ каналы маршируют вечно
	PingPong,     // 2 у 0x01/0x02/0x04/0x11: свап СТАРТ↔КОНЕЦ на каждом обороте
	SawFromEnd,   // 3 у меша 0x11: `age = 0` + снап КОНЦА, марш снова от КОНЦА — пила КОНЕЦ→2·КОНЕЦ−СТАРТ
};

// ★★★ ЦИКЛ 1:1: биты каналов, которые режим `Hold` ДЕРЖИТ (не маршируют) — по телам update-функций.
namespace AsdaFxLoopCh
{
	constexpr uint32 Alpha = 1u << 0, Color = 1u << 1, SizeX = 1u << 2, SizeY = 1u << 3;
	constexpr uint32 Scale = 1u << 4, Roll = 1u << 5, Rot = 1u << 6, PosOfs = 1u << 7, BasePos = 1u << 8;
	constexpr uint32 U = 1u << 9, V = 1u << 10, H = 1u << 11, TopR = 1u << 12, BotR = 1u << 13;
	constexpr uint32 Scale3 = 1u << 14;   // масштаб меша по осям (FAsdaFxNode::ConeAxis у меш-эмиттера)
	constexpr uint32 All = (1u << 15) - 1u;
}

// ★★★ ЦИКЛ 1:1: что известно об эмиттере из `DT_FxLoop` (не сбрасывается `ResetStates`).
struct FAsdaFxLoopRt
{
	EAsdaFxLoopMode Mode = EAsdaFxLoopMode::Legacy;   // режим конца периода
	uint32 HoldMask = 0;                              // для Hold: какие каналы держатся (AsdaFxLoopCh)
	int32 DiskType = -1;                              // тип записи .jeb (−1 = эмиттер в таблице не найден)
	int32 DiskEnd = -1;                               // endMode диска
	bool bSeedPatched = false;                        // трек подменён полным треком с сидами (только карты)
	TArray<FAsdaFxNode> SavedNodes;                   // исходный трек ассета — для отката `asda.Fx.LoopSeeds 0`
};

UCLASS()
class ASDAREBORN_API AAsdaEffectActor : public AActor
{
	GENERATED_BODY()
public:
	AAsdaEffectActor();
	// ★★★ ВОЛНА Ф8 (10.09.2026): ДОБАВЛЕН BlueprintReadOnly — НЕ ради блюпринтов, а ради САМОПРОВЕРКИ.
	//   В `-game` (единственный честный путь игрока — закон ui-check-game-path-geometry) Python не видит
	//   свойств, помеченных только EditAnywhere: `get_editor_property("effect_name")` кидает
	//   «Failed to find property». На этом споткнулся первый прогон волны Ф2 и первый прогон Ф8:
	//   эффект нельзя было найти по имени, и цель приходилось угадывать геометрически.
	//   Флаг НИЧЕГО НЕ МЕНЯЕТ ни в сериализации, ни в панели деталей.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda") FString EffectName;      // имя .jeb
	UPROPERTY(EditAnywhere, Category = "Asda") TArray<FAsdaFxEmitter> Emitters;
	UPROPERTY(EditAnywhere, Category = "Asda") float CullDistance = 10000.f;  // как движок (~9000-10000)

	// ★★★ ВОЛНА Ф2-ВСЕ (09.09.2026): СВЕТ ЭТОГО ЭФФЕКТА — ПОЛЯ, ВИДИМЫЕ И ПРАВИМЫЕ В РЕДАКТОРЕ.
	//   Материалы эффектов Unlit: эффект светится сам, но окружение не освещает. Рядом с ним заводится
	//   настоящий бестеневой источник (UAsdaFxLightComponent), и ВСЕ его числа лежат здесь — в ассете
	//   BP_Fx_<jeb> (общие для всех экземпляров) и, при желании, поштучно у актора, размещённого на карте.
	//   Дефолты проставляет Tools/asda_apply_fx_light_defaults.py из семейств Tools/asda_fx_light.json;
	//   правка руками взводит FxLight.bManual, и генератор такой эффект больше не трогает.
	//   ★ Компонент читает эти поля КАЖДЫЙ ТИК (а не кэширует при заведении) — именно поэтому правка
	//     ползунка в панели деталей меняет свет во вьюпорте немедленно, без перезапуска.
	//   ★★★ ВОЛНА Ф5 (10.09.2026): КАТЕГОРИЯ ПЕРЕИМЕНОВАНА «Свет эффекта» -> `Effect Glow`, и в неё же
	//     легла ЯРКОСТЬ САМОГО ЭФФЕКТА (поля «Сила свечения» структуры) — чтобы «как ярко светится» и
	//     «как сильно освещает» лежали одно под другим. Категория в .uasset не сериализуется, имена
	//     свойств не менялись — ни один из 1274 ассетов и 2385 акторов карт не затронут.
	// ★ ВОЛНА Ф8: BlueprintReadWrite — по той же причине, что и у EffectName: без него самопроверка
	//   в `-game` не может ни прочитать числа света, ни поставить форму для пары кадров «до/после».
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect Glow", meta = (DisplayName = "Effect Glow", ShowOnlyInnerProperties))
	FAsdaFxLightSpec FxLight;

	// ★★★ ВОЛНА Ф8 (10.09.2026): ДОЛГОВЕЧНАЯ КОПИЯ ПОЭМИТТЕРНЫХ НАСТРОЕК СВЕЧЕНИЯ.
	//   ЗАЧЕМ ОТДЕЛЬНЫЙ МАССИВ, А НЕ ПРОСТО ПОЛЕ В ЭМИТТЕРЕ. Сборщик каналов пишет `Emitters` ЦЕЛИКОМ
	//   (Tools/asda_fx_common.py::finish_effect_bp → `cdo.set_editor_property("emitters", …)`), поэтому
	//   ЛЮБОЕ поле ВНУТРИ эмиттера при перепечке из .jeb исчезает вместе со старым списком. Настройка
	//   владельца, сделанная руками, обязана перепечку пережить — значит, храниться она должна СНАРУЖИ
	//   массива эмиттеров, в свойстве, которого сборщик не касается.
	//   Копия обновляется при каждой правке (PostEditChangeProperty) и перед сохранением (PreSave),
	//   а восстанавливается в PostLoad и при первом тике — RestoreEmitterGlow по составному ключу.
	//   `EditFixedSize` + `VisibleAnywhere`: владельцу это видно (можно убедиться, что не потерялось),
	//   но правится оно НЕ здесь, а в самом эмиттере — иначе завелись бы два источника правды.
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Effect Glow", meta = (DisplayName = "Свечение эмиттеров: сохранённая копия (переживает перепечку)"))
	TArray<FAsdaFxEmitterGlowSaved> EmitterGlowSaved;

	// ★ Ф14 (МЕЛК-1, 20.09.2026): ПОТОЛОК ЗАПИСЕЙ-СИРОТ в долговечной копии. Запись без
	//   эмиттера-хозяина переносится при каждой записи (Ф13, С1) — и без потолка их число росло бы
	//   с каждой перепечкой, переименовавшей эмиттеры, а копия сериализуется в .uasset. Потолок
	//   считается ТОЛЬКО по сиротам: записи живых эмиттеров ограничены самим их числом.
	//   32 — с запасом: в архиве максимум эмиттеров у одного .jeb равен 16. Лишними отбрасываются
	//   САМЫЕ СТАРЫЕ, и о каждой отброшенной говорится в лог (молчаливая чистка неотличима от потери).
	// ★ Ф15 (МЕЛК-2, 20.09.2026): потолок срабатывал ТОЛЬКО в АВТОРИТЕТНОМ вызове SaveEmitterGlow
	//   (массив эмиттеров прямо сейчас правит владелец). Прежде он работал и в неавторитетном, то
	//   есть обычный Ctrl+S мог выбросить записи настроек владельца, а видно это было лишь в логе.
	// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-4, 20.09.2026): АВТОМАТИЧЕСКОГО ВЫБРОСА БОЛЬШЕ НЕТ ВОВСЕ ══════════════
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ Ф15. Условие «только в авторитетном вызове» не убрало выброс, а
	//   ПЕРЕНЕСЛО его на САМЫЙ ЧАСТЫЙ жест: авторитетным считается правка в панели деталей, то есть
	//   каждая правка любого поля эмиттера и КАЖДЫЙ ТИК ПЕРЕТАСКИВАНИЯ ПОЛЗУНКА (вид правки
	//   Interactive ≠ Unspecified ⇒ «панель» ⇒ авторитетно). Владелец, который «руками перерабатывает
	//   эффекты», проходил бы через этот выброс десятки раз за минуту — и узнавал бы о нём только из
	//   Warning в логе. Возражение «выбрасывать данные нельзя» от переноса с сохранения на правку
	//   никуда не делось: это те же настройки владельца, только теряются они теперь чаще.
	//   ЛЕЧЕНИЕ. SaveEmitterGlow НЕ ВЫБРАСЫВАЕТ НИ ОДНОЙ ЗАПИСИ НИКОГДА — ни в авторитетном вызове,
	//   ни в неавторитетном. Потолок остаётся ПОРОГОМ ПРЕДУПРЕЖДЕНИЯ: при его превышении владелец
	//   получает видимое сообщение (событие OnEmitterGlowOrphanOverflow → всплывающее уведомление
	//   редактора у конфигуратора) — ОДИН раз на актор за сеанс, чтобы перетаскивание ползунка не
	//   превращалось в поток сообщений. Убрать лишние записи можно только ЯВНОЙ командой
	//   `asda.Fx.Glow.DropOrphans` (см. DropOldestGlowOrphans), и она называет каждую отброшенную.
	//   32 — с запасом: в архиве максимум эмиттеров у одного .jeb равен 16.
	static constexpr int32 kEmitterGlowOrphanCap = 32;

	// ★ Ф16 (МЕЛК-4): «записей-сирот стало больше потолка» — предупреждение владельцу. Слушает
	//   редакторский плагин конфигуратора (он умеет показать всплывающее сообщение; игровой модуль
	//   своих окон не заводит). Тот же приём, что у OnPostEditUndo ниже.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FAsdaFxOnGlowOrphanOverflow,
		AAsdaEffectActor* /*Actor*/, int32 /*Orphans*/, int32 /*Cap*/);
	static FAsdaFxOnGlowOrphanOverflow& OnEmitterGlowOrphanOverflow();

	// ★ Ф16 (МЕЛК-4): ЯВНАЯ уборка сирот — по команде владельца, а не сама собой.
	//   Отбрасывает САМЫЕ СТАРЫЕ (они в начале массива: новые записи дописываются в конец) до
	//   потолка и называет в логе каждую отброшенную. bDryRun = только посчитать, ничего не менять.
	//   Возвращает число записей, которые отброшены (или были бы отброшены при bDryRun).
	//   Пакет при настоящей уборке помечается изменённым — иначе она потерялась бы без Ctrl+S.
	// ═══ ★★★ ВОЛНА Ф17 (С-1, 20.09.2026): УБОРКА ОБЯЗАНА ОТМЕНЯТЬСЯ, А НЕ ТОЛЬКО ОБЕЩАТЬ ══════════
	//   ЧТО БЫЛО НЕ ТАК. Внутри стоял голый `Modify()` с комментарием «уборка обязана попасть в
	//   транзакцию», но ТРАНЗАКЦИИ НЕ ОТКРЫВАЛ НИКТО: единственный вызывающий — консольная команда
	//   `asda.Fx.Glow.DropOrphans` — шла без FScopedTransaction (во всём AsdaEffectActor.cpp было
	//   ноль вхождений). А без валидного GUndo `SaveToTransactionBuffer` возвращает false и делает
	//   ТОЛЬКО MarkPackageDirty (UObjectGlobals.cpp:3215-3231, через Obj.cpp:1651-1679). Итог:
	//   отброшенные настройки владельца не возвращались Ctrl+Z, а пакеты оставались помеченными и
	//   уходили на диск его привычным «Save All» — потеря без отката (owner-asset-edits-never-lose).
	//   ВТОРАЯ ПОЛОВИНА БЕДЫ — ФЛАГ. Уборка идёт по CDO запечённых BP_Fx_*, а CDO блюпринта
	//   создаётся БЕЗ RF_Transactional (Class.cpp:5091) — то есть даже при открытой транзакции
	//   SaveToTransactionBuffer вышел бы по `bIsTransactional == false`. Флаг ставится руками, ровно
	//   как это делает сам движок (WidgetBlueprintEditorUtils.cpp:132-134 и :168-170).
	//   ЛЕЧЕНИЕ. Транзакция открывается ЗДЕСЬ, в самой функции: так накрыт ЛЮБОЙ вызывающий, а не
	//   только консольная команда. Команда при `all` открывает СВОЮ, объемлющую: вложение буфер
	//   поддерживает (TransBuffer.h:70-77 — запись заводит только внешний Begin), и владелец
	//   отменяет всю уборку ОДНИМ Ctrl+Z, а не по разу на эффект.
	// ═══ ★★★ ВОЛНА Ф18 (МЕЛК-3, 20.09.2026): БЕЗ ОТКАТА — НЕ ОТБРАСЫВАТЬ ВОВСЕ ═════════════════
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ Ф17. Транзакция открывается, флаг ставится, результат Modify
	//   ПРОВЕРЯЕТСЯ — и при ответе «не записано» уходит Warning… после чего записи ВСЁ РАВНО
	//   отбрасываются. То есть в единственном случае, ради которого проверка и заводилась (откат
	//   недоступен ⇒ потеря безвозвратна), поведение не отличалось от прежнего: настройки
	//   владельца уходили, вернуть их было нечем, а пакет оставался помеченным и уезжал на диск
	//   его привычным «Save All». Это ровно то, что запрещает owner-asset-edits-never-lose.
	//   ЛЕЧЕНИЕ. `bForceWithoutUndo` — ЯВНОЕ второе согласие владельца. Когда отката нет, уборка
	//   без этого признака НЕ ТРОГАЕТ НИЧЕГО (и даже не помечает пакет изменённым: проверка идёт
	//   ДО Modify, а Modify метит пакет и при неудаче — Obj.cpp:1666-1673), печатает причину и
	//   точную команду для повтора: `asda.Fx.Glow.DropOrphans <фильтр> force`.
	// ═══ ★★★ ВОЛНА Ф19 (МЕЛК-1, 20.09.2026): «НЕ ОСТАВЛЯЕТ СЛЕДОВ ВОВСЕ» БЫЛО ПРЕУВЕЛИЧЕНИЕМ ═════
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ Ф18. Утверждение «отказ не трогает ничего» держалось на одном доводе:
	//   проверка идёт ДО Modify. Но к моменту проверки было уже сделано ДВА действия:
	//     (1) открыта транзакция `FScopedTransaction`;
	//     (2) поставлен флаг RF_Transactional на CDO (SetFlags) — правка объекта.
	//   ★ ЧТО ИЗ ЭТОГО ПРОВЕРЕНО И ЧТО НЕ ПОДТВЕРДИЛОСЬ. Ревизия предположила, что действие (1)
	//     стирает СТЕК ПОВТОРА: UTransBuffer::BeginInternal (TransBuffer.h:77-90) при `UndoCount > 0`
	//     и вправду вынимает из UndoBuffer последние UndoCount записей — всё, что владелец отменил
	//     и мог бы вернуть Ctrl+Y. Однако ЭТО НЕ КОНЕЦ ИСТОРИИ: транзакция, оставшаяся ПУСТОЙ,
	//     считается «транзиентной» (FTransaction::IsTransient, EditorTransaction.cpp:515-527 — нет
	//     ни одной записи ⇒ да), и UTransBuffer::End (там же, 1332-1353) выталкивает её обратно и
	//     ВОССТАНАВЛИВАЕТ RemovedTransactions вместе с UndoCount. На ветке отказа в транзакцию
	//     никто ничего не клал (SetFlags записи не создаёт), поэтому Redo владельца уцелевал.
	//     Замер обеими точками (внутри транзакции и после её закрытия) — в самопроверке плагина,
	//     строка «МЕЛК-1/Ф19 след отказа».
	//   НАСТОЯЩИЙ СЛЕД, который отказ всё-таки оставлял, — флаг RF_Transactional на объекте, плюс
	//   ход буфера отмен «вынули-вернули» на время открытой транзакции. Последнее ещё и хрупко:
	//   положи в ту же транзакцию кто-нибудь хоть одну запись — и возврата уже не будет.
	//   ЛЕЧЕНИЕ. Проверка «откат доступен» перенесена ВЫШЕ обоих действий: отказ обязан быть пустым
	//   не «в итоге», а вовсе — без правки объекта и без хода буфера отмен. Раз транзакции ещё нет,
	//   `GUndo` на этом месте нулевой, и условие движка проверяется его же предикатом:
	//   `GEditor->CanTransact()` (EditorServer.cpp:1104-1110 — буфер Trans есть, пакеты не грузятся,
	//   PostLoad не выполняется) — ровно то, по чему решает сам FScopedTransaction
	//   (ScopedTransaction.cpp:20 `bShouldActuallyTransact && GEditor && GEditor->CanTransact()
	//   && ensure(!GIsTransacting)`). Уже открытая ОБЪЕМЛЮЩАЯ транзакция (команда `DropOrphans all`)
	//   видна по ненулевому GUndo и тоже считается доступным откатом.
	//   Флаг RF_Transactional в условие по-прежнему не входит: он ставится ПОСЛЕ проверки.
	//   ТЕКСТ ОТКАЗА приведён к факту: «ничего не изменено: пакет не помечен, транзакция не
	//   открывалась, стек повтора не тронут» — вместо прежнего «не оставляет следов вовсе»,
	//   которое было правдой лишь потому, что движок сам подчищал за нами пустую транзакцию.
	//   ПОЧЕМУ ПОВТОР КОМАНДЫ, А НЕ ВСПЛЫВАЮЩЕЕ СООБЩЕНИЕ, КАК У ПЕРЕПОЛНЕНИЯ СКЛАДА. У склада
	//   (OnEmitterGlowOrphanOverflow) событие приходит САМО, посреди правки в панели: владелец на
	//   консоль не смотрит, и без окна сообщение потерялось бы. Здесь наоборот — владелец ТОЛЬКО
	//   ЧТО напечатал команду и смотрит ровно в то окно, куда уходит ответ. А главное, окно
	//   сообщения ничего не спасает: пока оно всплывает, записи уже отброшены. Отказ — это не
	//   способ сообщить, это способ НЕ ПОТЕРЯТЬ; согласие даётся до действия, а не после.
	//   ОТКАТ СЧИТАЕТСЯ ДОСТУПНЫМ по тому же условию, по которому его считает движок: буфер отмены
	//   работоспособен (GUndo уже не нулевой — CoreGlobals.h:97 — ЛИБО его готов открыть редактор,
	//   GEditor->CanTransact(), EditorServer.cpp:1104-1110), объект в состоянии, допускающем правку
	//   (UObject::CanModify, Obj.cpp:1641-1650), пакет не скриптовый (UObjectGlobals.cpp:3218).
	//   Флаг RF_Transactional в условие не входит: его ставит сама функция — после проверки.
	int32 DropOldestGlowOrphans(bool bDryRun = false, bool bForceWithoutUndo = false);

	// ★ Ф17 (С-1): попала ли ПОСЛЕДНЯЯ уборка сирот в буфер отмены (результат UObject::Modify).
	//   «Проверка, а не вера»: тот же приём, что у конфигуратора (GAsdaGlowLastEditUndoable).
	//   Не UPROPERTY и не сериализуется: признак живёт один сеанс и нужен самопроверке и логу.
	// ★ Ф18 (МЕЛК-4): БЫЛО `static`, СТАЛО ПОЛЕ АКТОРА. Смысл признака — «как прошла уборка У
	//   ЭТОГО эффекта», а статик держал ОДНО значение на весь модуль: `DropOrphans all` проходит
	//   по десяткам эффектов, и после него в признаке оставался ответ ПОСЛЕДНЕГО — по нему нельзя
	//   было сказать, у кого именно откат не получился. При отказе по МЕЛК-3 (отката нет и
	//   согласия `force` нет) признак ставится в false: уборки не было, отменять нечего.
	bool bLastOrphanDropUndoable = false;

	// ★ Ф16 (МЕЛК-4): предупреждение о превышении потолка уже показано этому актору в этом сеансе.
	//   Не UPROPERTY и не сериализуется намеренно: признак живёт ровно один сеанс редактора.
	bool bGlowOrphanOverflowReported = false;

	// ═══ ★★★ ВОЛНА Ф17 (С-2, 20.09.2026): ЗАЛИПШИЙ ГЕЙТ — ЕДИНСТВЕННЫЙ ЕГО СИМПТОМ ════════════════
	//   ЧТО БЫЛО НЕ ТАК. Предупреждение о нераспознанной записи печаталось ТОЛЬКО при
	//   `GAsdaFxGlowEmittersWriter < 0` (AsdaEffectActor.cpp, ветка Ф16/СРЕДН-1). А при ключе,
	//   ОСТАВЛЕННОМ В 1 — гейт утёк из сборщика (Tools/asda_fx_common.py) или владелец поднял его
	//   сам и забыл — IsEmittersWriteFromDetailsPanel возвращает false СРАЗУ, ещё до разбора формы
	//   события. Каждая правка в панели считается перепечаткой скрипта: RestoreEmitterGlow кладёт
	//   обратно ПРЕЖНИЕ числа, правка владельца исчезает на глазах, а в лог не уходит НИЧЕГО
	//   (единственная строка на этом пути — Verbose, по умолчанию выключенная). Владелец видит
	//   «панель не работает» без единой подсказки о причине, и так — до перезапуска редактора.
	//   ЛЕЧЕНИЕ. Когда ключ поднят в 1, а форма события говорит «правил человек»
	//   (Property != MemberProperty ⇒ правился ЛИСТ структуры или элемент массива, либо
	//   ChangeType != Unspecified ⇒ ValueSet/Interactive/ArrayAdd — скрипт таких не шлёт), это
	//   ровно тот симптом. Он уходит Warning-ом в лог и событием — в конфигуратор, который покажет
	//   владельцу всплывающее сообщение с командой возврата ключа. ОДИН раз на актор за сеанс:
	//   перетаскивание ползунка даёт десятки таких событий в секунду.
	//   ПОЧЕМУ ТОЛЬКО ПРЕДУПРЕЖДЕНИЕ, А НЕ АВТОМАТИЧЕСКИЙ СБРОС КЛЮЧА. Ключ — решение владельца
	//   (значение 1 он мог поднять и намеренно, обкатывая сборщик); молча менять его за него
	//   значит заводить второй источник правды о том, кто пишет эмиттеры.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FAsdaFxOnGlowWriterGateSuspect,
		AAsdaEffectActor* /*Actor*/, int32 /*GateValue*/);
	static FAsdaFxOnGlowWriterGateSuspect& OnEmittersWriterGateSuspect();

	// ★ Ф17 (С-2): подозрение на залипший гейт уже показано этому актору в этом сеансе.
	//   Не UPROPERTY и не сериализуется: признак живёт ровно один сеанс редактора.
	// ═══ ★★★ ВОЛНА Ф18 (С-1, 20.09.2026): ПРИЗНАК ОБЯЗАН СБРАСЫВАТЬСЯ ══════════════════════════
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ Ф17. В ПРОДУКТЕ признак не сбрасывался НИГДЕ — сбросы стояли только
	//   в самопроверке плагина. А владелец за сеанс гоняет сборщик не один раз: первый залипший
	//   гейт он увидит, второй (после того как гейт опускался и поднимался снова) уйдёт в Verbose
	//   и не скажет ни слова. То есть оставался последний путь МОЛЧАЛИВОЙ ПОТЕРИ правки —
	//   ровно тот, ради закрытия которого волна Ф17 и делалась.
	//   ЛЕЧЕНИЕ Ф18 — сброс на ЛЮБОЙ записи в «Эмиттеры», увиденной при гейте НЕ РАВНОМ 1.
	// ═══ ★★★ ВОЛНА Ф19 (С-1, 20.09.2026): СБРОС ПО ЗАПИСИ В АКТОР НЕ СРАБАТЫВАЕТ ВОВСЕ ═══════════
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ Ф18. Сброс стоял ВНУТРИ ветки `Member == Emitters`, то есть требовал
	//   ЗАПИСИ В ЭТОТ АКТОР при гейте ≠ 1. А законный писатель ровно один — сборщик, и он держит
	//   гейт ПОДНЯТЫМ всё время своей работы (Tools/asda_fx_common.py::emitters_writer_gate,
	//   __enter__ поднимает в 1, __exit__ возвращает прежнее). Значит КАЖДАЯ запись сборщика
	//   приходит при гейте == 1 и признак НЕ СНИМАЕТ. Остаётся только рука владельца в панели —
	//   но именно её в сценарии дыры и нет:
	//     гейт залип → владелец правит эффект X → предупреждение, признак X поставлен →
	//     владелец опустил ключ, но X БОЛЬШЕ НЕ ТРОГАЛ → сборщик залип второй раз →
	//     владелец снова правит X при гейте 1 ⇒ сброса не было ⇒ только Verbose ⇒
	//     правка молча откатывается. Дыра Ф17 осталась открытой, лишь сузилась.
	//   ★ ЗАОДНО ИСПРАВЛЕН ДОВОД Ф18. Там стояло: «законная перепечка сборщиком при опущенном
	//     гейте признак бы НЕ сняла». Довод фактически НЕВЕРЕН — по закону Ф16 сборщики гейт
	//     поднимают ВСЕГДА, поэтому случая «перепечка при опущенном гейте» попросту не бывает.
	//     Ошибка была не в выводе, а в том, что довод маскировал настоящую дыру: при поднятом
	//     гейте не снимает признак НИКТО.
	//   ЛЕЧЕНИЕ — ЦЕПЛЯТЬ СБРОС ЗА САМ КЛЮЧ, А НЕ ЗА ЗАПИСЬ В АКТОР. У переменной
	//   `asda.Fx.Glow.EmittersWriter` заведён колбэк смены значения (IConsoleVariable::
	//   SetOnChangedCallback, ставится конструктором FAutoConsoleVariableRef в AsdaEffectActor.cpp).
	//   Как только значение уходит ИЗ 1, модульный счётчик «ЭПОХА ГЕЙТА» увеличивается на единицу.
	//   Актор вместо признака `уже предупредили` хранит НОМЕР ЭПОХИ, в которой предупреждение
	//   выдано. Предупреждать надо, когда сохранённая эпоха не совпадает с текущей.
	//   ЧТО ЭТО ДАЁТ. Опускание ключа снимает залипание у ВСЕХ эффектов разом и не требует, чтобы
	//   владелец после этого в каждый из них заглядывал. Одноразовость при этом сохраняется —
	//   в пределах одной эпохи счёт у каждого актора по-прежнему свой.
	//   ПОЧЕМУ КОЛБЭК, А НЕ ОПРОС ЗНАЧЕНИЯ ПРИ КАЖДОЙ ПРАВКЕ. Опрос видит только ТЕКУЩЕЕ значение
	//   и не отличает «гейт не опускался» от «опускался и поднят снова» — а это и есть второе
	//   залипание. Колбэк же вызывается на КАЖДОЙ принятой установке (ConsoleManager.cpp,
	//   FConsoleVariableBase::OnChanged из Set), то есть ловит само СОБЫТИЕ опускания.
	//   Значение -1 = «эпох ещё не было» невозможно: счётчик стартует с 0, а поле — с INDEX_NONE.
	//   Не UPROPERTY и не сериализуется: номер живёт ровно один сеанс редактора.
	int32 GlowWriterGateSuspectEpoch = INDEX_NONE;

	// ═══ ★★★ ВОЛНА Ф20 (С-1, 20.09.2026): ЭПОХИ МАЛО — НУЖЕН ЕЩЁ И ПОВТОР ПО ВРЕМЕНИ ═══════════
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ Ф19. Одноразовость стала «один раз на эффект ЗА ЭПОХУ ГЕЙТА», и это
	//   закрыло сценарий «ключ опускали между залипаниями». Но остался узкий путь, где ключ НЕ
	//   опускается вовсе:
	//     гейт залип → владелец правит эффект X → выдано ЕДИНСТВЕННОЕ предупреждение →
	//     всплывающее сообщение прожило свои 15 секунд и ушло, владелец его не заметил →
	//     владелец продолжает перебирать ТОТ ЖЕ эффект X ещё десять минут ⇒ эпоха та же,
	//     номер совпадает ⇒ каждая правка уходит в Verbose (по умолчанию выключенный) и
	//     откатывается МОЛЧА. Работа теряется без слов — ровно то, ради чего делалась Ф17.
	//   Значение возвращается на глазах, и НОВЫЙ эффект своё сообщение получит; но владелец,
	//   настраивающий один эффект, видит только «панель не работает» и никакой причины.
	//
	//   ЛЕЧЕНИЕ — ВТОРОЕ УСЛОВИЕ ПОВТОРА: ВРЕМЯ. Кроме номера эпохи актор запоминает МОМЕНТ
	//   последнего предупреждения. Предупреждаем, если эпоха сменилась ЛИБО с прошлого раза
	//   прошло больше `asda.Fx.Glow.GateWarnRepeatSec` секунд (умолчание 25).
	//   ПОЧЕМУ ПОТОК ОКОН ЭТИМ НЕ РОЖДАЕТСЯ. Гашение потока — это и есть задача интервала:
	//   перетаскивание ползунка даёт десятки событий в СЕКУНДУ, то есть все они укладываются в
	//   миллисекунды и попадают в одно окно интервала; второе сообщение может появиться не раньше
	//   чем через 25 с непрерывной работы. Худший случай — примерно два-три сообщения в минуту,
	//   и это ровно та частота, на которой владелец их и должен видеть.
	//   ПОЧЕМУ ИНТЕРВАЛ, А НЕ «СВОДКА РАЗ В N СЕКУНД» (второй вариант ревизии). Сводке нужен
	//   тикер: считать подавленные события мало — их надо КОМУ-ТО напечатать, а в тишине (владелец
	//   отошёл) печатать нечего и незачем. Заводить ради этого FTSTicker у актора значит завести
	//   ещё один источник сообщений, живущий отдельно от жеста владельца. Интервал же не требует
	//   ничего: он проверяется в том самом месте, куда и так приходит каждая правка, и сообщение
	//   появляется ровно тогда, когда владелец работает, — то есть когда его есть кому прочесть.
	//   СЧЁТ ПОДАВЛЕННЫХ ВСЁ РАВНО ВЕДЁТСЯ (поле ниже) и печатается В ПОВТОРНОМ сообщении: владелец
	//   узнаёт не только «ключ залип», но и «сколько ваших правок уже откатилось молча».
	//
	//   ЕДИНИЦА ВРЕМЕНИ — FPlatformTime::Seconds(), а не игровое время мира. Правка в панели
	//   деталей приходит и при остановленной симуляции, и в коммандлете, где мира может не быть
	//   вовсе; часы платформы монотонны и от состояния мира не зависят.
	//   Стартовое значение -1.0 значит «не предупреждали ни разу»: у монотонных часов такое
	//   значение недостижимо, а разность с ним заведомо больше любого интервала.
	//   Не UPROPERTY и не сериализуется: момент живёт ровно один сеанс редактора.
	double GlowWriterGateSuspectTimeSec = -1.0;

	// ★ Ф20 (С-1): сколько правок человеческой формы при поднятом ключе было ПОДАВЛЕНО (ушло в
	//   Verbose) с прошлого предупреждения. Каждая из них — молча откаченная работа владельца,
	//   поэтому число печатается в следующем же сообщении и там же обнуляется. Счёт ведётся на
	//   актор: у каждого эффекта своя картина.
	//   Не UPROPERTY и не сериализуется: счёт живёт ровно один сеанс редактора.
	int32 GlowWriterGateSuspectMuted = 0;

	// ★ Ф19 (С-1): текущая «эпоха гейта» — сколько раз `asda.Fx.Glow.EmittersWriter` уходил из 1.
	//   Одна на модуль: ключ тоже один. Нужна самопроверке плагина, чтобы ставить опыт от известной
	//   точки, и актору — чтобы сравнивать с собственным номером.
	static int32 GetGlowWriterGateEpoch();

	// ★ Ф20 (С-1): нынешний интервал повтора предупреждения о залипшем гейте, секунды
	//   (ключ `asda.Fx.Glow.GateWarnRepeatSec`). Нужен самопроверке плагина: она ставит опыт с
	//   заведомо коротким интервалом и обязана знать, какое значение сейчас в силе.
	static float GetGlowWriterGateWarnRepeatSec();

	// ★ Ф16 (ДИСЦИПЛИНА): метка компиляции ИГРОВОГО модуля — `__DATE__ __TIME__` единицы трансляции
	//   AsdaEffectActor.cpp. Нужна самопроверке плагина как ПРЯМОЕ свидетельство того, какой бинарь
	//   сейчас исполняется: время файла на диске этого не доказывает (старый .dll мог остаться
	//   загруженным), а препроцессорная метка печатается только тем кодом, который её и содержит.
	static FString BuildStamp();

	// Снять копию с текущих `Emitters[*].Glow` в `EmitterGlowSaved` (все эмиттеры разом).
	//   Зовётся из PostEditChangeProperty и PreSave. Пустые (полностью дефолтные) настройки в копию
	//   НЕ пишутся: иначе массив вырос бы на всех 1274 ассетах ради нулей.
	// ★★★ РЕВИЗИЯ Ф12 (К1, 20.09.2026): ПАРАМЕТР `bAuthoritative` — КТО ЗДЕСЬ ИСТОЧНИК ПРАВДЫ.
	//   ЗАЧЕМ ОН ПОНАДОБИЛСЯ. Сторожевое правило внутри (пустой свежий снимок при непустой копии
	//   ИГНОРИРУЕТСЯ) спасает от перепечки эмиттеров сборщиком: список `Emitters` уже заменён на
	//   голый из .jeb, настройки в нём ещё не восстановлены, и голое присваивание стёрло бы
	//   единственную уцелевшую копию. Но то же правило делало НЕПОЛНЫМ откат владельца: вернув
	//   ЕДИНСТВЕННЫЙ настроенный эмиттер в «взято от эффекта», он получал пустой свежий снимок —
	//   и запись с отменёнными числами оставалась в копии, а при следующей загрузке
	//   RestoreEmitterGlow возвращала её обратно (отменённая правка «воскресала»).
	//   ПОЧЕМУ РЕШАЕТ ИМЕННО ВЫЗЫВАЮЩИЙ. Из самих данных эти два случая НЕОТЛИЧИМЫ: и там, и там
	//   все `Emitters[*].Glow` дефолтные, а копия непустая. Отличается только НАМЕРЕНИЕ, и оно
	//   известно только вызывающему:
	//     false (по умолчанию) — «сохрани, если есть что»: PreSave и PostEditChangeProperty, куда
	//       перепечка и приходит. Сторож работает, смысл прежний;
	//     true — «в `Emitters` сейчас ровно то, что хотел владелец»: конфигуратор свечения
	//       (FAsdaFxGlowConfigModel::EmitterEditEnd) — он сам только что правил массив, перепечке
	//       между его правкой и этим вызовом взяться неоткуда. Пустой снимок тогда ЗАКОННО пустой,
	//       и копия чистится вместе с эмиттерами.
	//   Срабатывание сторожа пишется в лог (LogAsdaFxManualLock): молчащая защита неотличима от бага.
	// ★★★ ВОЛНА Ф13 (С1, 20.09.2026): ЗАПИСИ БЕЗ ЭМИТТЕРА-ХОЗЯИНА ПЕРЕНОСЯТСЯ ВСЕГДА.
	//   `bAuthoritative` отвечает только на вопрос «пустой снимок — это ноль настроек или ещё не
	//   восстановленная перепечка». Он НЕ даёт права выбрасывать записи копии, которым в нынешнем
	//   списке `Emitters` пары не нашлось: это единственный след настроек владельца после перепечки
	//   с переименованием. Такие записи переносятся дословно при ЛЮБОМ вызове, авторитетном и нет.
	// ═══ ★★★ ВОЛНА Ф14 (К-1, 20.09.2026): СТОРОЖ ТЕРЯЛ РУЧНОЙ ВОЗВРАТ ЧЕРЕЗ ПАНЕЛЬ ДЕТАЛЕЙ ═══════
	//   ЧТО БЫЛО НЕ ТАК. `bAuthoritative` ставил только конфигуратор. Панель деталей ходит сюда
	//   через PostEditChangeProperty, и та звала SaveEmitterGlow() БЕЗ параметра. Сценарий потери:
	//   у ассета настроен ОДИН эмиттер, владелец в панели возвращает ему «взято от эффекта» →
	//   свежий снимок пуст, копия непуста → сторож срабатывает → отменённая запись остаётся в
	//   EmitterGlowSaved, уходит в .uasset, и при следующей загрузке PostLoad → RestoreEmitterGlow
	//   возвращает отменённые числа. Работа владельца терялась молча.
	//   ВЫБРАННЫЙ ПРИЗНАК — ФОРМА СОБЫТИЯ ПРАВКИ, и решает его ВЫЗЫВАЮЩИЙ.
	// ═══ ★★★ ВОЛНА Ф15 (К-1, 20.09.2026): ПРИЗНАК Ф14 БЫЛ НЕВЕРЕН — ЗАМЕНЁН ═══════════════════════
	//   Ф14 сравнивала ИМЕНА свойств (`Leaf != Member`) и считала, что панель деталей всегда правит
	//   ЛИСТ. Оба утверждения опровергнуты:
	//     * операции панели над САМИМ массивом («+», вставка, удаление, дублирование, очистка,
	//       «Reset to Default» на массиве, вставка текста) дают Property == MemberProperty ==
	//       `Emitters` — PropertyHandleImpl.cpp:1438/1722/1911/1621/2403/666;
	//     * «Reset to Default» и вставка на ОДНОМ элементе дают Property = ArrayProperty->Inner, а
	//       UHT называет Inner ТЕМ ЖЕ именем (AsdaEffectActor.gen.cpp:1742 против :1743) — по FName
	//       снова выходит равенство.
	//   ⇒ ПКМ по эмиттеру → «Reset to Default» уходил в ветку сборщика: отменённые числа
	//   возвращались немедленно (RestoreEmitterGlow) и оставались в копии (неавторитетная запись).
	//   ТЕПЕРЬ решает AAsdaEffectActor::IsEmittersWriteFromDetailsPanel: явный ключ
	//   `asda.Fx.Glow.EmittersWriter`, затем сравнение свойств УКАЗАТЕЛЯМИ, затем вид правки
	//   `Event.ChangeType` (панель не присылает `Unspecified` никогда, скрипт из UE-Python —
	//   только его). Доказательства по строкам движка — в шапке этой функции в AsdaEffectActor.cpp.
	//   Панель ⇒ bAuthoritative = true; скрипт ⇒ false, сторож на месте.
	//   ★★★ ПРИЗНАК «ПО ДАННЫМ» ПРОБОВАЛСЯ И ОТВЕРГНУТ — ЗАМЕРОМ, А НЕ РАССУЖДЕНИЕМ.
	//   Пробовался отпечаток массива эмиттеров (число + имя + тип .jeb + признак «свечение
	//   дефолтное»), снимаемый в момент сверки копии с массивом. Прогон 20.09 09:11
	//   (Reports/fx_glow_editor5/run_try1.result.json) показал две его беды:
	//     * С признаком дефолта отпечаток расходится и при РУЧНОМ возврате (владелец гасит
	//       свечение — бит меняется точно так же, как при перепечке): ветка «неавторитетный вызов
	//       после ручного возврата» дала копию из 1 записи вместо 0. То есть признак по данным
	//       перепечку от руки НЕ ОТЛИЧАЕТ — ровно как и было сказано в задании;
	//     * без признака дефолта (один только состав) отпечаток перестаёт замечать перепечку с
	//       ТЕМИ ЖЕ именами — то есть ОСЛАБЛЯЕТ сторож и открывает потерю настроек владельца.
	//   Поэтому второго механизма в коде нет: лишний неработающий признак — это ложное чувство
	//   защищённости и мёртвый код, а закон owner-asset-edits-never-lose запрещает менять верную
	//   защиту на «вроде бы тоже работает».
	//   ЧТО ОСТАЁТСЯ НЕПОКРЫТЫМ. Возврат эмиттера к умолчанию путём, который НЕ проходит ни через
	//   PostEditChangeProperty, ни через конфигуратор, при Ctrl+S оставит запись в копии (сторож
	//   сработает). Это поведение ОСТОРОЖНОЕ — данные сохраняются, теряется лишь отмена, — и после
	//   этой правки таких путей в редакторе не осталось: панель деталей и конфигуратор авторитетны,
	//   а Ctrl+Z возвращает `EmitterGlowSaved` вместе с `Emitters` (оба UPROPERTY одного объекта).
	void SaveEmitterGlow(bool bAuthoritative = false);

	// Восстановить `Emitters[*].Glow` из `EmitterGlowSaved` по составному ключу. Пишет только туда,
	//   где сейчас лежит ДЕФОЛТ (Source == FromEffect и блик выключен): свежая правка важнее копии.
	//   Возвращает, сколько эмиттеров восстановлено (0 = восстанавливать было нечего).
	// ★★★ ВОЛНА Ф14 (МЕЛК-1): СОПОСТАВЛЕНИЕ — ОДНО НА ДВОИХ И БЕЗ КРАЖИ ЧУЖИХ ЗАПИСЕЙ.
	//   Было две ОТДЕЛЬНЫЕ копии трёхпроходного разбора — здесь и в AsdaGlowMatchSavedToEmitters
	//   (SaveEmitterGlow). Разойдись они хоть на строку — запись считалась бы принадлежащей одному
	//   эмиттеру при сохранении и другому при восстановлении. Теперь разбор ОДИН
	//   (AsdaGlowMatchSavedToEmitters), а «кому раздавать» решается уже по его итогу.
	//   ★ И слабейший ключ (голый номер) больше не отдаёт эмиттеру ЧУЖИЕ числа: запись, чьё имя
	//     есть у какого-то нынешнего эмиттера, по номеру никому другому не достаётся, и эмиттер,
	//     чьё имя есть в какой-то записи, по номеру чужую запись не берёт. Прежде запись
	//     «CFA_01 / тип 0xE» после смены типа эмиттера доставалась соседу с тем же номером.
	//   ★ Ф15 (МЕЛК-1): ограничение имён распространено и на проход «номер + тип .jeb». Ф14
	//     ограничила только проход «голый номер», и контрпример проходил мимо неё: запись
	//     {«CFA_01», тип 0xE, номер 1} при эмиттерах [0]{«BG»,0x5} [1]{«X»,0xE} [2]{«CFA_01»,0x4E}
	//     доставалась эмиттеру «X» по совпадению номера и типа. Совпадение типа ничего не
	//     доказывает: типов .jeb мало и у соседей они повторяются.
	//   ★ Раньше уже настроенный эмиттер в разборе не участвовал вовсе — и его запись мог забрать
	//     по слабому ключу сосед. Теперь запись сперва закрепляется за хозяином, и только потом
	//     решается, раздавать ли (раздаётся лишь в ДЕФОЛТНЫЕ эмиттеры — правило не изменилось).
	int32 RestoreEmitterGlow();

	// ★★★ ВОЛНА Ф8: ОРИЕНТИРОВАННАЯ КОРОБКА СЕКЦИИ ЭМИТТЕРА (главные оси облака вершин).
	//   ЗАЧЕМ. Прямоугольному источнику нужны не «центроид и радиус», а ПЛОСКОСТЬ: где она, как
	//   повёрнута, какой ширины и высоты. Радуга — плоская лента: её осевая коробка (AABB) даёт
	//   бессмысленный кубик со стороной в длину ленты, а собственные оси — настоящие 1500 × 40 × 2.
	//   КАК СЧИТАЕТСЯ. Ковариация облака вершин секции (один проход на сумму, второй на моменты),
	//   затем симметричное разложение 3×3 методом Якоби (10 свипов; матрица маленькая, сходится за 3-4).
	//   Оси возвращаются ПО УБЫВАНИЮ разброса: AxisA — самая длинная (вдоль ленты), AxisB — вторая,
	//   AxisN — НАИМЕНЬШАЯ, то есть НОРМАЛЬ плоскости. Полу-габариты — в тех же осях.
	//   ★ ЗНАК ОСЕЙ ПРОИЗВОЛЕН — у собственного вектора его нет. Поэтому у формы «прямоугольный» есть
	//     переключатель «в обратную сторону»: угадать, какая сторона ленты «лицевая», геометрия не даёт.
	//   false = секции нет или в ней меньше трёх вершин.
	//   ★ 15.10.2026: `bCarrierFrame` — у эмиттера МИРОВЫХ частиц эффекта на движущемся носителе считать по облаку
	//     «в кадре носителя» (RestVerts), а не по оставшемуся позади; у прочих секций флаг ничего не меняет.
	bool EmitterSectionFrame(int32 EmitterIdx, FVector& OutCenter,
		FVector& OutAxisA, FVector& OutAxisB, FVector& OutAxisN, FVector& OutHalf, bool bCarrierFrame = false) const;
	// ★ ПЕРЕОПРЕДЕЛЕНИЕ КАМЕРЫ НАБЛЮДАТЕЛЯ (для РЕДАКТОРСКИХ ПРЕВЬЮ-СЦЕН; дефолт ВЫКЛ = как было).
	//   Позиция/поворот камеры нужны эффекту в трёх местах: дистанц-кулл (Tick), билборд-ориентация
	//   частиц (BuildQuads) и биллборд-квады 0xC. В игре это PlayerCameraManager, в редакторе —
	//   перспективный вьюпорт ЭТОГО мира (GetViewLocation → AsdaView, волна 3 п.11). Свой FPreviewScene
	//   (браузер предметов, любые превью-панели) движку неизвестен: камера бралась от вьюпорта
	//   УРОВНЯ, и при открытой карте (камера в десятках тысяч юнитов от нуля) эффект превью
	//   попадал под дистанц-кулл ⇒ ClearFxSection ⇒ «эффекта нет вообще», а без кулла квады
	//   смотрели на чужую точку. Владелец превью выставляет эти поля каждый кадр.
	UPROPERTY(EditAnywhere, Category = "Asda") bool bUseViewOverride = false;
	UPROPERTY(EditAnywhere, Category = "Asda") FVector ViewLocationOverride = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Asda") FRotator ViewRotationOverride = FRotator::ZeroRotator;
	// ★ Базовый трансформ (актор) УЖЕ в UE-осях (напр. кость шкатулки char-select, экспортированная с G2U-бейком):
	//   пропускать внутренний game→UE своп квадов (UE=(-Gz,Gx,Gy)) — иначе ДВОЙНАЯ конвертация → «глиф торчит вниз».
	//   + basePos/holderOfs домножать на актор-скейл (в ×100-сцене raw-подъём слишком мал). audit_effects.md К1/К3.
	UPROPERTY(EditAnywhere, Category = "Asda") bool bBaseAlreadyUE = false;

	// ★ Вращение+сияние .jem-меша (кристалл глифа char-select). Включается извне (AsdaLoginGameMode для глифов),
	//   чтобы карт-меши не крутились. Эталон jemEffect_UpdateModelEmitter: спин вокруг вертикали + пульс-alpha.
	UPROPERTY(EditAnywhere, Category = "Asda") bool bSpinMeshEmitters = false;
	// ★ Скорость .jem-скина: эталон марширует animFrame на 1 за КАДР РЕНДЕРА (не за секунду) — при
	//   60 FPS клиента это 60 кадров/с. Правится в редакторе (кадр/с).
	UPROPERTY(EditAnywhere, Category = "Asda") float MeshAnimFps = 60.f;
	UPROPERTY(EditAnywhere, Category = "Asda") float TimeScale = 1.f;
	// ★ ЗНАК ПОВОРОТА узловых квадов (rotX/rotY/rotZ). Игра ПРАВОРУКАЯ (RH) — положительный угол крутит в
	//   другую сторону, чем наша формула. Для метки клика это критично: при текущем знаке вертикальные
	//   стрелки-шевроны уходят ПОД ЗЕМЛЮ (высота −45…−5 вместо +5…+45, реверс: «на высоте ~25»).
	//   Эффекты КАРТ проверены с текущим знаком (48/48), поэтому глобально не меняем — включается точечно.
	UPROPERTY(EditAnywhere, Category = "Asda") bool bInvertNodeRot = false;
	// ★ Отражение квада по ВЕРТИКАЛИ (V текстуры). Нужно вместе с bInvertNodeRot: инверсия знака поворота
	//   ставит квад на верную высоту, но переворачивает его «вверх ногами» (верх текстуры уходит вниз).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFlipQuadV = false;
	// ★ СТРОГАЯ ПОСАДКА ATTACH-КВАДА (0xB/0xC/0x18/0x13). ЭТАЛОН (effectQuad_updateAndDrawBillboard@0x120AEB0):
	//   мировая матрица квада = RotY(roll356)·RotX(364)·RotY(372)·RotZ(380)·N48·N520, а ТРАНСЛЯЦИЯ считается
	//   ОТДЕЛЬНО: basePos(0x3FC) + posChan(0x3E4)·(R·N48·N520) + дрейф узла. То есть roll крутит ГЕОМЕТРИЮ,
	//   а не центр, и никакого разворота на 180° в эталоне нет (та же схема, что в эталонной ветке колец).
	//   Исторический порт складывал pos-канал с УГЛАМИ квада ДО roll и добавлял π-«швартовку» для attach —
	//   под это откалиброван attach-квад башни alpen, поэтому ДЕФОЛТ false = как было.
	//   true ставит рантайм эффектов НА КОСТИ (свечение заточки): замер Tools/_probe_weapon_glow_attach.py,
	//   ohs_a_co_ma_03 при мешe оружия x[−9.5..3.5] y[−3.5..2.8] z[−12.5..99.6]:
	//     было  S_1st_Ground(2.2,0,90)→ok, S_Spak_01→(0,2.5,−60), Light_01→(0,0,−55), S_3rd_Up→(60,−2.2,0);
	//     стало все полотна на клинке: 90 / 60 / 55 / (0,−2.2,60) — координаты .jeb 1:1 совпадают с сырыми
	//     координатами МЕША ОРУЖИЯ (обе сущности живут в матрице одной кости 8_a1).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bStrictQuadAttach = false;
	// ★ Доп. множитель размера ЭФФЕКТ-эмиттеров (0xC кольца-сияние + 0xD искры), НЕ трогает меш-кристалл (=«глиф»).
	//   Для char-select глифов ставится <1 (запрос: уменьшить размер эффектов независимо от размера глифа).
	UPROPERTY(EditAnywhere, Category = "Asda") float EffectEmitterScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "Asda") float TimeSyncGate = 0.f;   // type-2: старт при anim-времени родителя >= (мс); 0 = сразу
	// ★ type-2 attach БЕЗ парентинга: слежение позицией за костью цели (движок S5: SetPos каждый кадр; ориентация/
	//   масштаб — авторские, НЕ трогаются). Парентинг к зеркальному скелету (scale.X<0) ломает FTransform-relative.
	UPROPERTY(EditAnywhere, Category = "Asda") TObjectPtr<AActor> FollowActor = nullptr;
	UPROPERTY(EditAnywhere, Category = "Asda") FName FollowBone;
	// Ориентация attached: R = liveКость · bindКость⁻¹ · authored (движок owner48·owner520). Кость≈bind (радуга) →
	// чистый authored; повёрнутая/анимированная голова (лампа tower) → доворот конуса за костью.
	UPROPERTY(EditAnywhere, Category = "Asda") FRotator FollowBindRot = FRotator::ZeroRotator;   // поворот сокета на бейке (bind)
	UPROPERTY(EditAnywhere, Category = "Asda") FRotator AuthoredRot = FRotator::ZeroRotator;     // авторский поворот эффекта
	// ★ Физика частиц по флагам эффекта (эталон 10_Tick.c:36-58, gated sub_91E460/sub_93C695 родителя;
	//   дисковые оффсеты флагов пока не реверснуты — дефолт ВЫКЛ, включается данными/редактором).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFloorBounce = false;   // отскок от пола: pos.z<actorZ−5·s → vel.z=−vel.z·0.5
	UPROPERTY(EditAnywhere, Category = "Asda") float AttractStrength = 0.f; // аттрактор к позиции эффекта: vel+=норм(цель−pos)·сила·dt
	// ★ Хиральная нормаль type-2 attach (game-оси; ZeroVector = выкл): носители с ЗЕРКАЛЬНЫМ скелетом
	//   (scale.X<0: дирижабль, башня-портал) требуют отражения ВСЕЙ сборки эффекта. Позиции (basePos/pos)
	//   отражает place-скрипт в данных узлов; ГЕОМЕТРИЮ колец/квадов (после канальных вращений) отражает
	//   рантайм этой нормалью — иначе конус и его офсеты рассинхронизированы (tower: «юбка ниже конуса»,
	//   cos(Δ,ось) менял знак). Радуга/фонарь — те же носители, та же модель.
	UPROPERTY(EditAnywhere, Category = "Asda") FVector ChiralNormal = FVector::ZeroVector;
	// ★ Плотность частиц (тюнинг, дефолт 1 = данные как есть): множитель vertex-альфы частиц c клампом 0..1.
	//   Введён по визуальному арбитражу (дым: честный альфа-марш 1→0 после включения VertexColor дал слишком
	//   прозрачный шлейф против оригинала; точная эталонная кривая дефолт-узлов 0xD — TODO дореверса).
	UPROPERTY(EditAnywhere, Category = "Asda") float ParticleAlphaScale = 1.f;
	// ═══ ★★★ [ПОРТ] РАНТАЙМ-МНОЖИТЕЛЬ АЛЬФЫ ВСЕГО ЭФФЕКТА (GM-«скрыться») ═══════════════════════
	//   ЗАЧЕМ ОТДЕЛЬНОЕ ПОЛЕ, А НЕ `ParticleAlphaScale`: тот — ТЮНИНГ плотности дыма, живёт в ассете
	//   и применяется ТОЛЬКО к частицам. Здесь нужен внешний, временный множитель на ВСЕ четыре
	//   канала записи вершинной альфы (частицы, кольцо, одиночный квад, .jem-меш/крылья), который
	//   гасится обратно в 1 и не пишется в ассет.
	//   ★★★ ПОЧЕМУ ВЕРШИННАЯ АЛЬФА, А НЕ МАТЕРИАЛ: у эффектов нет ни одного MID —
	//   `RebuildSection` в ветке Create ставит на секцию МАТЕРИАЛ ИЗ ДАННЫХ ЭМИТТЕРА (`E.Material`),
	//   снимая любой рантайм-экземпляр (этот файл, ~стр. 1614; та же грабля описана у CullDistance).
	//   Секции перестраиваются каждый кадр в `Tick`, поэтому множитель начинает действовать со
	//   следующего кадра и переживает любую пересборку — состояние живёт в АКТОРЕ, а не в материале.
	//   Не UPROPERTY(EditAnywhere): в ассет не сохраняется, это чисто сессионное состояние.
	UPROPERTY(Transient) float FxAlphaScale = 1.f;
	// Задать множитель (клампится в 0..1). Отдельный сеттер — чтобы точка входа была одна и логируемая.
	void SetFxAlphaScale(float InScale) { FxAlphaScale = FMath::Clamp(InScale, 0.f, 1.f); }
	// ★ Ручная правка в редакторе: сними галки — актор перестанет снапиться и двигается свободно;
	//   FollowOffset — мировой сдвиг ПОВЕРХ кости (следование сохраняется).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFollowPosition = false;   // дефолт ВЫКЛ: поставили 1 раз при импорте, двигается руками
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFollowRotation = false;
	// ★ Слежение ЗА YAW НОСИТЕЛЯ (устаревший костыль крыльев — заменён bFollowBoneFull; оставлен для
	//   эффектов, которым нужен только доворот по курсу носителя без ориентации кости).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFollowActorYaw = false;
	// ★ ПОЛНЫЙ ТРАНСФОРМ КОСТИ 1:1 с эталоном (attach-режим 10, effectAttachToCharSlot@0xF58F10:
	//   EffectWorld = liveBoneMatrix × charWorldMatrix). Позиция И ОРИЕНТАЦИЯ берутся из живой
	//   анимированной кости; авторские каналы эффекта (rotY=90 у крыла) домножаются поверх — именно так
	//   разворот кости «M» (yaw≈−87°) компенсирует канал и крыло ложится размахом поперёк спины.
	//   Масштаб кости НЕ наследуется (вершины меша уже несут scale-канал эффекта).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFollowBoneFull = false;
	// ★ ATTACH-РЕЖИМ 6 (effectNodeFollowPlayerBone @0xF58850) — шлейф транспорта. Вопреки имени, кость НЕ
	//   используется: хендлер КАЖДЫЙ КАДР пишет в ноду МИРОВУЮ МАТРИЦУ ПЕРСОНАЖА (char->worldMatrix), заменяя
	//   в ней только элемент [13] (высота) живой позицией. То есть эффект живёт в системе координат чара:
	//   локальный posBase эмиттера крутится вместе с курсом персонажа.
	//   Порт: тот же вывод, что у attach-10 (bFollowBoneFull), но БЕЗ множителя кости —
	//   World = FxToBone(swapXY×100) × ComponentTransform(меша). Сверено с формулой attach-10 (live.dmp §H).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFollowCharWorld = false;
	// ★ ATTACH-3/5/9/12 (`effectSyncPosToRenderEntity`/`effectNodeTrackTargetChar`/
	//   `effectSyncPosToOwnerEntity`/`effectUpdateEntityAttachPos`) — канал эффектов СОСТОЯНИЙ.
	//   Хендлер каждый кадр делает ТОЛЬКО `CEffect_SetPos(node+8, позиция носителя)`: матрица ноды
	//   (ориентация) остаётся такой, какой её собрал эффект — поэтому здесь НЕТ ни кости, ни поворота.
	//   FollowOffset — эталонная поправка высоты (+15 у attach-5, +25/45/60 у attach-12 по классу).
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFollowCharPos = false;
	// Компонент-носитель для bFollowCharWorld. Задаётся явно (НЕ FindComponentByClass): у персонажа в седле
	//   скелеталок несколько (тело, транспорт, флаг клана), а порядок обхода OwnedComponents не определён.
	UPROPERTY(EditAnywhere, Category = "Asda") TObjectPtr<USceneComponent> FollowComponent = nullptr;
	// ★ Скелетный компонент-носитель для bFollowBoneFull (attach-10). Дефолт nullptr = ПОВЕДЕНИЕ КАК БЫЛО
	//   (FindComponentByClass<USkeletalMeshComponent>). Задавать ЯВНО там, где эффект обязан жить в ТОЧНО
	//   ТОЙ ЖЕ скелетной позе, что и декорируемый МЕШ (свечение заточки ↔ меш оружия): у персонажа
	//   скелеталок много (тело BaseSkel + Equip_* с LeaderPose + транспорт + флаг клана), а порядок обхода
	//   OwnedComponents (TSet) НЕ определён — FindComponentByClass может вернуть чужую. Эталон таких
	//   разночтений не имеет: и модель оружия (sub_93DE55 → нода pModelPartArray+524*6, локальная матрица
	//   ЕДИНИЧНАЯ — сверено в live.dmp), и эффект (CCharacter_UpdateHeldItemEffect@0xA4C070) берут ОДНУ
	//   живую кость ОДНОГО скелета: M = boneLive × charWorld.
	UPROPERTY(EditAnywhere, Category = "Asda") TObjectPtr<USkeletalMeshComponent> FollowSkelComponent = nullptr;
	UPROPERTY(EditAnywhere, Category = "Asda") FVector FollowOffset = FVector::ZeroVector;
	// ★ ДОП. ЛОКАЛЬНАЯ МАТРИЦА В СЫРЫХ (D3D) ОСЯХ КОСТИ, вставляемая МЕЖДУ fx-пространством и костью:
	//   A = FxToBone · FollowBoneAttach · BoneCS · Comp. 1:1 эталонная attachMatrix из
	//   CCharacter_UpdateHeldItemEffect@0xA4C070 (entity_char.c:28503-28564): при «оружие убрано за спину»
	//   свечение получает те же поправки, что и меш (копьё/посох, арбалет, баллиста, луки). Identity = как было.
	//   Единицы — сырые игровые (FxToBone уже домножил на 100), поэтому литералы эталона идут как есть.
	FMatrix FollowBoneAttach = FMatrix::Identity;
	bool bWarnedNoFollowBone = false;   // одноразовый Warning «кость не найдена» (без спама per-tick)
	// ★ ПОРЯДОК ТИКОВ ДЛЯ ЭФФЕКТОВ НА ПЕРСОНАЖЕ (фикс «эффекты оружия дёргаются и отстают при беге»,
	//   2026-08-01). ПРИЧИНА: и павн (SetActorLocation в своём Tick), и скелетал (поза), и эффект-актор
	//   тикали в ОДНОЙ группе TG_PrePhysics, а порядок акторов внутри группы UE НЕ определяет. Эффект
	//   читал GetComponentSpaceTransforms()/GetComponentTransform() ДО того, как персонаж сдвинулся и
	//   параллельная анимация докрутила позу ⇒ геометрия эффекта строилась по позе ПРЕДЫДУЩЕГО кадра,
	//   а меш оружия рисовался по текущей: постоянное отставание на 1 кадр (при беге ~7 юнитов) плюс
	//   дрожание, когда порядок акторов в группе менялся.
	//   ЛЕЧЕНИЕ: у эффектов, привязанных к персонажу, тик переносится в TG_PostUpdateWork (после движения
	//   павна, после физики и после завершения параллельной оценки поз) + tick-prerequisite на компонент-
	//   носитель. ДЕФОЛТ ВЫКЛ = поведение как было ⇒ эффекты карт и всё, что не помечено, не меняются.
	UPROPERTY(EditAnywhere, Category = "Asda") bool bFollowTickAfterPose = false;
	// Приводит группу тика/зависимости в соответствие с bFollowTickAfterPose и cvar asda.FxFollowTickFix.
	// Идемпотентна и дёшева (сравнение указателей) — зовётся при спавне и в начале Tick.
	void ApplyFollowTickOrder();

	// ★★★ 19.09.2026: признак «эффект входит в группу свечения своего носителя» (пункты «Свечения» 1/3): группа
	//   эффекта = группа владельца-носителя, трафарет тот же (UAsdaVideoSettingsSubsystem::GlowGroupOf /
	//   IsGlowCarriedFx / ApplyGlowStencilToActor). Рантайм-признак, не UPROPERTY: в ассеты BP_Fx_* не сериализуется.
	//   ★★ 19.09.2026 (glow_fix5, С2 — реверс, два метода): НИ ОДИН эффект предмета порта в группу эталона НЕ входит,
	//   признак сейчас не ставит никто (ссылок-присваиваний в срезе нет).
	//     • Свечение заточки оружия — `CCharacter_UpdateHeldItemEffect@0xA4C070`: .jeb грузится в слот персонажа
	//       +0x33B18 (`CEffect_LoadJEB_Wrapper`), узел +0x33B1C кладётся в очередь 0x3537FA8 (`CRenderQueue_AddNode`
	//       @0xA4C656); щита — `CCharacter_UpdateEquipAuraEffect@0xA4DD60`: `Effect_SpawnByName` менеджера 0x349E860
	//       (@0xA4E4EA) + узел +12 в ту же очередь (@0xA4E568); крылья — `CCharacter_UpdateWingEffect@0xA4BAA0`
	//       (менеджер 0x349E860 + очередь); кольцо соулмейта — `playerUpdateSelectedItemEffect@0xA382D0` (слот
	//       +0x336C0 → очередь, ключ +150). Узлы менеджера 0x349E860 туда же кладёт `effectMgr_BuildDepthSortQueue`.
	//     • Очередь 0x3537FA8 рисуют только `renderShadowMeshQueue@0x97E870` / `clanShadowRenderQueue@0x97E0F0`
	//       (шаг «5.1.5» кадра `CRenderer_RenderWorldFrame@0x9773B0`, вызов @0x977B8B), `renderShadowMapPass@0x97EAB0`
	//       (шаг «6.1», тип 4) и `CRenderQueue_Flush` (только кадр логина @0x976188) — все ПОСЛЕ сведения свечения
	//       (шаг «5», `effectQuadRender_FC00@0xF5FC00` → `sceneRenderShadowLightPass@0xF5FCC0`). Девять объектов
	//       прохода свечения (@0xF5FD79…0xF5FDCE) эту очередь не обходят: второй метод — скан смещений .text
	//       (поля очереди 0x3537FAC/FB0/FB4/0x354B828 читают только три названные функции; слоты +0x33B18/+0x33B1C/
	//       +0x336BC/+0x336C0 и указатель +0x37360 — только их обновление, сброс, конструктор/деструктор и копирование).
	//     • Список 0x29080FC (= объект 0x29079E0 + 0x71C, `sub_976A60` → `renderListChildrenWithShadowPass`) в
	//       группе есть, но пишут в него загрузчики МОДЕЛЕЙ персонажа (части тела, экипировка, волосы, езда, стрела
	//       `Projectile_LoadArrowModel`) и полосы «совела» `itemSetupSowelGlow@0x9ED480` (частицы вставленного камня,
	//       `pbbBuildStripBatch`, узлы 0x29080FC @0x9ED64F) — их в порту нет. Адрес @0x9EDF51 (`push 0x29080FC`) —
	//       внутри функции 0x9EDE30 (декомпила нет; статических вызовов нет, кроме переходника 0x923587 без ссылок):
	//       она грузит МОДЕЛЬ груди по полу `ch1_{m,f}_00_chst001_00_00_0.jbm` (`meshLoad_BindSubsetTexturesWithState`
	//       с объектом 0x2907B98), не .jeb; прежняя подпись «загрузчик .jeb» была неверной.
	bool bGlowWithCarrier = false;

	// ★★★ Д1: ЕДИНАЯ точка настройки attach-10/attach-6 эффекта на носителе. Ставит ВЕСЬ обязательный
	//   набор флагов (в т.ч. bFollowPosition=true — гейт тика, забытый у хранителя = боевой баг «волна в
	//   сторону от взгляда») + ApplyFollowTickOrder(). Носитель-специфичные offset/attachMatrix — параметры.
	//   Skel == nullptr для attach-10 = поведение «как было» (FindComponentByClass) — так у крыльев.
	void ConfigureBoneAttach(AActor* InOwner, USkeletalMeshComponent* Skel, FName Bone,
		EAsdaFxAttachMode Mode, const FVector& Offset = FVector::ZeroVector,
		const FMatrix& AttachMatrix = FMatrix::Identity);

	// ★★★ Д3: ЕДИНАЯ ground-truth матрица посадки на живую кость (attach-10): A = FxToBone·FollowBoneAttach·
	//   BoneCS·Comp (со скейлом). И Tick, и зонды верификации (asda.FxFollowLagTest / asda.VerifyWeaponGlow)
	//   строят посадку ТОЛЬКО через неё — иначе копии в верификаторах молча начинают мерить не то.
	//   Возвращает false, если носитель/кость не разрешаются или режим не bFollowBoneFull.
	bool ComputeFollowBoneMatrix(FMatrix& OutA) const;
	// ★ Итоговая мировая точка+ориентация носителя (attach-6 charWorld ИЛИ attach-10 boneFull), БЕЗ
	//   FollowOffset (его прибавляет вызывающий). false = режим не применим/кость не найдена.
	bool ComputeFollowTargetWorld(FVector& OutLoc, FQuat& OutRot) const;

	// ★ Аудит №3 (A10): ЕДИНОЕ построение имени и класса эффект-BP по .jeb-базе
	//   («BP_Fx_» + Replace('-','_') → `/Game/AsdaAssets/Effects/<имя>.<имя>_C` → LoadClass).
	//   Отдельно от спавна — тем, кому нужен ранний гейт «BP собран?» (шлейф транспорта проверяет
	//   его в момент посадки, а спавнит раз в 2.5 с). nullptr = BP не собран или база пустая.
	static UClass* ResolveJebClass(const FString& JebBase);

	// ★ Аудит №3 (A10): спавн эффект-BP по ГОТОВОМУ имени ассета из `/Game/AsdaAssets/Effects/`.
	//   Нужен импактам боя: их BP собираются `Tools/asda_build_combat_impacts.py` под РУЧНЫМИ именами
	//   (`BP_ImpactMob`, `BP_ImpactBattk01..04`), а не по схеме `BP_Fx_<jeb>` — перевести их на
	//   `SpawnByJebName` без переименования пяти ассетов нельзя (см. отчёт волны).
	//   `SpawnByJebName` — обёртка над этим методом, так что параметры спавна у обоих одни.
	static AAsdaEffectActor* SpawnByBPName(UWorld* World, const FString& BPName,
		const FVector& Loc, const FRotator& Rot, AActor* Owner);

	// ★ Д4: ЕДИНЫЙ спавн эффект-BP по .jeb-базе («BP_Fx_» + Replace('-','_') → LoadClass →
	//   SpawnActor AlwaysSpawn). Возвращает эффект-актор или nullptr (BP не собран — логирует вызывающий).
	static AAsdaEffectActor* SpawnByJebName(UWorld* World, const FString& JebBase,
		const FVector& Loc, const FRotator& Rot, AActor* Owner);

	// ★★★ Аудит №3 (B5): ЕСТЕСТВЕННОЕ ВРЕМЯ ЖИЗНИ одноразового эффекта = самый долгий эмиттер.
	//   `life = max(StartDelay + Period)` по `Emitters`, затем `Clamp(life + Pad, MinS, MaxS)`.
	//   Тело было размножено по четырём файлам (шлейф и вспышка транспорта, эффект хранителя,
	//   подсистема эффектов состояний) — данные лежат ВНУТРИ актора, здесь им и место.
	//   Пустой список эмиттеров даёт `MinS` (эффект без эмиттеров живёт минимальный срок, как и раньше).
	//   Pad = запас после последней эмиссии на догорание уже выпущенных частиц.
	//   ★ 08.10.2026 (ЦИКЛ 1:1, МЕЛК-1 ревизии fx_loop): при `asda.Fx.Loop 1` и контейнере 0/2 (с учётом
	//   переопределения владельцем — ставить ДО вызова) `life = max(life, длительность контейнера)`: эталон
	//   держит эффект (и застывшие эмиттеры endMode 0) до конца КОНТЕЙНЕРА (`objAnimRotateUpdate@0x114E050`).
	float NaturalLifeSpan(float Pad = 0.5f, float MinS = 1.f, float MaxS = 30.f) const;

	// ★★★ Дедуп №2 В1-C (F12, 11.10.2026): ОДНОРАЗОВЫЙ ЭФФЕКТ НА НОСИТЕЛЕ — общая точка копий
	//   «`SpawnByJebName` → [`ConfigureBoneAttach`] → `SetLifeSpan(NaturalLifeSpan())`» (предмет, звание, пустышка,
	//   шлейф и вспышка транспорта, копка, итог копки, конец рыбалки ×2, удар хранителя). Срок — `NaturalLifeSpan()`
	//   с умолчаниями, как у всех прежних копий. nullptr = BP не собран (лог — у вызывающего, свой контекст).
	//   Флаги, которые вызывающий ставит ПОСЛЕ (например `bStrictQuadAttach`), срока не касаются.
	//   Особые спавны (эффект состояния с переопределением контейнера, корона без срока, крылья, каст навыка,
	//   эффекты с хэндлом) сюда НЕ идут — у них свой срок или его нет.
	//   Без привязки: эффект стоит в точке спавна (режим 0 эталона), `Carrier` — только владелец актора.
	static AAsdaEffectActor* SpawnOnCarrier(UWorld* World, const FString& JebBase, const FVector& At, AActor* Carrier,
		const FRotator& Rot = FRotator::ZeroRotator);
	//   С привязкой: сразу после спавна `ConfigureBoneAttach(Carrier, Skel, Bone, Mode, Offset)`.
	static AAsdaEffectActor* SpawnOnCarrier(UWorld* World, const FString& JebBase, const FVector& At, AActor* Carrier,
		USkeletalMeshComponent* Skel, FName Bone, EAsdaFxAttachMode Mode, const FVector& Offset = FVector::ZeroVector);

	// ★ Дедуп №2 В1-C (F12): ТАБЛИЦА «номер режима привязки эталона → режим эффект-актора + подъём по Z». Каноническое
	//   тело — бывший `switch` канала состояний (`UAsdaEffectsSubsystem`); его копии подъёма «+15» в каналах предметов
	//   и пустышки уже дважды терялись («дефект-в-копии»). Коды: 5 → позиция носителя + `TrackRiseZ`; 7 → кость +
	//   `TrackRiseZ`; 6/8 → мировая матрица носителя; 3/9 → позиция носителя; 12 → позиция + 25/45/60 по классу
	//   размера; 13 → позиция + 200/400/600. Неизвестный код → false, `CharPos` и 0 (кричит вызывающий).
	//   `TrackRiseZ` — поправка эталона `CEffect_SetPos(…, y + 15.0)`; у канала состояний это его свойство.
	static bool EtalonAttach(int32 Code, int32 SizeClass, float TrackRiseZ, EAsdaFxAttachMode& OutMode, float& OutRiseZ);
	// Поправка высоты эталонных привязок 5/7 (`flt_13998C0` = 15.0, игровая Y = UE Z): умолчание для `TrackRiseZ`.
	static constexpr float kEtalonTrackRiseZ = 15.f;

	// ★ Для зонда `asda.Fx.MotionProbe`: мировая точка эмиссии эмиттера в ТЕКУЩЕМ состоянии
	//   (актор + марш/вращение эмиттера). Никем, кроме зонда, не используется — читает то же
	//   состояние, что и `SpawnParticle`, чтобы зонд не мерил «своё» ([[false-negatives-method]]).
	FVector EmitterProbePos(int32 EmitterIdx) const;

	// ★ Для зонда: ЦЕНТРОИД реально построенной геометрии секции эмиттера (квад/кольцо/частицы).
	//   Точка эмиссии у квадов и колец не используется — их положение видно только по вершинам,
	//   поэтому «до/после» по π-швартовке и по вращению офсета кольца меряется именно так.
	//   false = секции нет (эмиттер ещё не стартовал / погашен / вырожден).
	bool EmitterSectionCentroid(int32 EmitterIdx, FVector& Out) const;

	// ★ Для зонда: РАДИУС построенной геометрии = максимум расстояния вершины до центроида.
	//   Центроид РАДИАЛЬНО-СИММЕТРИЧНОЙ фигуры (кольцо-лента 0xE) не меняется при росте радиуса —
	//   статичную «юбку» им не поймать ([[false-negatives-method]]: одного метода мало).
	//   Радиус растёт вместе с topR/botR/scale и с половинными экстентами квада ⇒ им и меряем
	//   «до/после» по каналам размера. −1 = секции нет.
	float EmitterSectionRadius(int32 EmitterIdx) const;

	// ★★★ ВОЛНА Ф4 (10.09.2026, свет по ВСЕМ эмиттерам): ЦЕНТРОИД + РАДИУС + ЧИСЛО ВЕРШИН секции ЗА ОДИН ВЫЗОВ.
	//   Заведено НЕ ради красоты, а ради цены: агрегирование эффект-света идёт по ВСЕМ эмиттерам каждый кадр,
	//   и пара `EmitterSectionCentroid` + `EmitterSectionRadius` стоила бы ЧЕТЫРЁХ проходов по вершинам на
	//   эмиттер (каждая из них внутри себя считает центроид заново). Здесь проходов ровно два, а
	//   `OutVerts` — то, чего у прежней пары не было вовсе: он нужен как ПРОКСИ ПЛОЩАДИ секции.
	//   ПОЧЕМУ ЧИСЛО ВЕРШИН = ПЛОЩАДЬ. Геометрия эмиттера — квады по 4 вершины со стороной 2·HalfExtent,
	//   поэтому суммарная излучающая площадь ≈ (Verts/4)·(2·Sx)·(2·Sy) = Verts·Sx·Sy. Считать площадь
	//   треугольниками честнее, но это третий проход по вершинам ради величины, которая входит в вес
	//   ЛИНЕЙНО и делится на сумму таких же весов, — точность там не окупается.
	//   false = секции нет (эмиттер ещё не стартовал / погашен / вырожден) — ровно как у пары выше.
	//   ★ 15.10.2026: `bCarrierFrame` — как у EmitterSectionFrame (облако мировых частиц в кадре носителя).
	bool EmitterSectionStats(int32 EmitterIdx, FVector& OutCentroid, float& OutRadius, int32& OutVerts, bool bCarrierFrame = false) const;

	// ★ ЖИВАЯ ФАКТИКА ДЛЯ ДИАГНОСТИКИ (`asda.Target.RingNearest`, `npc_target_ring_fix.md` §4):
	//   эмиттер уже погашен веткой `EndMode == 0` (`bStopped` + `ClearFxSection`)? Иначе «нет»
	//   у одноразового эффекта означало бы, что он всё ещё крутится. `OutLifeTimer` — таймер
	//   периода того же эмиттера (сек). false = такого эмиттера нет.
	//   ★ ПРИ `EndMode == 3` У ОДИНОЧНОГО КВАДА (кольцо выбора цели, `npc_target_ring_fix2.md`)
	//   `bStopped` ВСЕГДА false — фриз выражен НЕ остановкой, а тем, что `OutLifeTimer` упёрся
	//   в `Period` и больше не растёт. Читать эти два поля надо ВМЕСТЕ, иначе «не погашен»
	//   прочитается как «крутится» ([[false-negatives-method]]).
	//   `OutAlpha` / `OutHalfExtent` (необязательные) — значения ТЕКУЩЕГО кадра, снятые тем же
	//   `NodeAt`, которым строится геометрия: ими «кольцо видно и какого оно диаметра»
	//   доказывается ФАКТОМ, а не пересчётом по второй формуле.
	//   ★ ВОЛНА Ф2 (09.09.2026, эффект-света): `OutColor` — ЦВЕТ УЗЛА того же кадра (`FAsdaFxNode::Color`).
	//   Параметр ДОБАВЛЕН, а не заведён отдельным методом, ровно потому, что цвет обязан быть снят ТЕМ ЖЕ
	//   `NodeAt` и на ТОМ ЖЕ `LifeTimer`, что альфа и размер: свет от эффекта должен гаснуть и менять
	//   оттенок синхронно с картинкой, а вторая формула рано или поздно разойдётся с первой.
	//   Значение по умолчанию nullptr ⇒ у всех прежних вызывающих поведение не меняется.
	bool GetEmitterRuntime(int32 EmitterIdx, bool& bOutStopped, float& OutLifeTimer,
		float* OutAlpha = nullptr, float* OutHalfExtent = nullptr, FLinearColor* OutColor = nullptr) const;

	// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 2 ревизии: ЕСТЬ ЛИ У ЭМИТТЕРА ПОСТРОЕННАЯ СЕКЦИЯ — ЗА O(1).
	//   `EmitterSectionStats` отвечает на тот же вопрос, но ДВУМЯ проходами по буферу вершин: она
	//   считает ещё и центроид с радиусом. Отбору набора светов нужно только «есть ли геометрия
	//   вообще», и спрашивать это у эффекта из 160 эмиттеров полным разбором было бы платой ни за что.
	//   ★ Тело — в .cpp: `UProceduralMeshComponent` здесь только объявлен вперёд.
	int32 EmitterSectionVertCount(int32 EmitterIdx) const;

	// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 1 ревизии: УЗЕЛ ТЕКУЩЕГО КАДРА ЦЕЛИКОМ.
	//   ЗАЧЕМ ОТДЕЛЬНЫЙ ДОСТУП. `GetEmitterRuntime` отдаёт из узла три величины — альфу, цвет и
	//   `max(SizeX, SizeY)`. Для КОЛЬЦА-ленты (тип 0xE) этого не хватает принципиально: размер
	//   кольца лежит в каналах `RingTopR` / `RingBotR` / `RingScale` / `RingH`, а `SizeX`/`SizeY` у
	//   него задают ширину ЛЕНТЫ и бывают нулевыми при кольце в сотни юнитов. Диагностика, которая
	//   этого не видит, отвечает «эмиттер крошечный» там, где эмиттер большой, — то самое ложное
	//   «нет» (закон false-negatives-method).
	//   ★ Это ЧИСТОЕ ЧТЕНИЕ и ТА ЖЕ функция `NodeAt`, которой строится геометрия: второй формулы
	//     интерполяции здесь нет и быть не должно.
	bool GetEmitterNodeNow(int32 EmitterIdx, FAsdaFxNode& OutNode) const
	{
		if (!Emitters.IsValidIndex(EmitterIdx) || !States.IsValidIndex(EmitterIdx)) { return false; }
		NodeAt(Emitters[EmitterIdx], States[EmitterIdx].LifeTimer, OutNode);
		return true;
	}

	// ★ ВОЛНА Ф2: уровень значимости эффекта ТЕКУЩЕГО кадра (`FAsdaVisState::Level`, EAsdaVisLevel:
	//   0 = заморожен, 1 = через кадр, 2 = каждый кадр). ЧИСТОЕ ЧТЕНИЕ, поведения не меняет.
	//   Нужен эффект-свету: при уровне 0 симуляция эффекта стоит, и свет обязан погаснуть вместе с ней —
	//   иначе на карте остаётся «свет от невидимого огня», который никто не двигает.
	//   При выключенном гейте (`asda.Vis.Effects 0`) поле остаётся Near — свет горит как раньше.
	uint8 GetVisLevel() const { return VisState.Level; }

	// ═══ ★★★ ЭФФЕКТ НА ДВИЖУЩЕМСЯ НОСИТЕЛЕ (15.10.2026, `Reports/anticheat/wave_PORT_mount_fx.md`) ═══
	//   Жалоба владельца: «на велосипеде эффект подсветки отстаёт от транспорта, иногда не успевает догонять».
	//   [ОРИГИНАЛ] `objectMgr_TickEffects@0xF57990`: на КАЖДЫЙ узел эффекта в КАЖДОМ кадре сначала зовётся
	//   обработчик привязки (attach 6 = `effectNodeFollowPlayerBone@0xF58850`: матрица узла := мировая матрица
	//   персонажа), и сразу за ним — шаг эмиттеров (`objAnimRotateUpdateSpeed`). Кадров «без шага» у узла на
	//   носителе в оригинале не бывает: квады/кольца/меши строятся от матрицы узла этого кадра.
	//   [ПОРТ] Геометрия секций МИРОВАЯ и строится только в шаге (`SimulateStep`). Гейт значимости (волна
	//   CPU 07.09.2026) шагает эффект уровня 1 ЧЕРЕЗ КАДР и замораживает уровень 0 — у эффекта на движущемся
	//   носителе пропущенный кадр оставлял геометрию там, где носитель был кадр (или много кадров) назад.
	//   Признак «эффект следует за носителем» — ОДИН на актор (им же пользуется свет эффекта).
	bool IsCarrierFollower() const
	{
		return bFollowPosition && (bFollowCharWorld || bFollowCharPos || bFollowBoneFull || FollowActor != nullptr);
	}
	// ★ Эмиттер с частицами в МИРОВЫХ координатах (0x03/0x0D/0x0F/0x17/0x1B/0x20): частица живёт там, где
	//   родилась, и уходит от носителя по своей скорости — так и в оригинале (`CEmitter_SpawnParticle@0x12062A0`
	//   пишет мировую точку рождения, `CEmitter_Draw@0x1212790` рисует ровно её). Квад/кольцо/меш — жёсткая
	//   геометрия узла (едет с носителем). false — индекса нет.
	bool IsWorldSpaceParticleEmitter(int32 EmitterIdx) const
	{
		if (!Emitters.IsValidIndex(EmitterIdx)) { return false; }
		const FAsdaFxEmitter& E = Emitters[EmitterIdx];
		return !(E.bMeshEmitter || E.bSingleQuad || E.bRing);
	}
	// Диагностика (самотест `asda.Mount.FxSelfTest`): кадр (`GFrameCounter`) и трансформ актора, с которыми в
	//   последний раз построена геометрия секций; признак «геометрия сейчас перенесена вслед за носителем без шага».
	uint64 GetGeomBuildFrame() const { return GeomBuildFrame; }
	const FTransform& GetGeomBuildXf() const { return GeomBuildXf; }
	bool IsGeomCarried() const { return bGeomCarried; }
	// Мировой трансформ компонента геометрии (identity, пока геометрия не перенесена вслед за носителем).
	FTransform GetGeomComponentXf() const;
private:
	uint64 GeomBuildFrame = 0;                   // GFrameCounter последнего шага с построением секций
	FTransform GeomBuildXf = FTransform::Identity; // трансформ актора в этом шаге (от него строились вершины)
	bool bGeomCarried = false;                   // компонент геометрии сдвинут вслед за носителем (кадр без шага)
	// ★ ОБЛАКО МИРОВЫХ ЧАСТИЦ «В КАДРЕ НОСИТЕЛЯ» (только у эффекта, следующего за носителем; по секции эмиттера):
	//   вершины секции, где квад каждой частицы сдвинут на (текущий трансформ · трансформ рождения⁻¹ · Pos − Pos).
	//   Порядок и число — как у секции (паддинг не сдвигается). Картинку НЕ меняет: секция рисуется по-прежнему из
	//   мировых вершин (частицы тянутся шлейфом, как в оригинале); читает только свет эффекта (флаг bCarrierFrame).
	TArray<TArray<FVector>> RestVerts;
	// Вершины секции «в кадре носителя» для чтения (флаг выключен / облака нет / число вершин не совпало — nullptr).
	const TArray<FVector>* CarrierFrameVerts(int32 EmitterIdx, bool bCarrierFrame, int32 SectionVertNum) const;
	// Кадр без шага у эффекта на носителе: перенести уже построенную мировую геометрию жёстко вслед за актором
	//   (Mesh := GeomBuildXf⁻¹ · текущий трансформ). Следующий шаг вернёт компоненту identity.
	void CarryGeometryWithCarrier();
	// ★ Ревизия 15.10.2026 (review_PORT_mobpos_mountfx.md п. 4–6): полный темп и перенос — только у ДВИЖУЩЕГОСЯ носителя.
	//   Кадр носителя = компонент, от которого строится слежение (см. GetCarrierFrameXf в .cpp).
	FTransform CarrierXfAtStep = FTransform::Identity;   // кадр носителя на последнем шаге
	FTransform CarrierXfAtCarry = FTransform::Identity;  // кадр носителя на последнем переносе геометрии
	bool bHaveCarrierXfAtStep = false;                   // кадр носителя на шаге снят
	bool bRestFrameWanted = false;                       // строить RestVerts в этом шаге (есть свет эффекта и ключ)
	bool GetCarrierFrameXf(FTransform& Out) const;
	bool CarrierMovedSince(const FTransform& Ref) const;
	TWeakObjectPtr<USceneComponent> TickPrereqComp;   // на какой компонент уже повешен prerequisite
	bool bFollowTickOrderApplied = false;             // текущее состояние (для A/B по cvar на живой сцене)
public:
	// header-анимация холдера (.jeb header, аудит §12): world = emitterLocal × RotY(angle) × attachMat (+posDrift в мире)
	UPROPERTY(EditAnywhere, Category = "Asda") float HolderYawStart = 0.f;  // град
	UPROPERTY(EditAnywhere, Category = "Asda") float HolderYawSpeed = 0.f;  // град/с
	UPROPERTY(EditAnywhere, Category = "Asda") FVector HolderPosVel = FVector::ZeroVector;  // UE ед/с (дрейф)

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }  // анимировать в editor-вьюпорте
	virtual void OnConstruction(const FTransform& Xf) override;
	// ★ ВОЛНА Ф8: восстановление поэмиттерного свечения из долговечной копии. Именно PostLoad, а не
	//   BeginPlay: ассет BP_Fx_* после перепечки открывают В РЕДАКТОРЕ, и владелец обязан увидеть свои
	//   числа на месте, ни разу не запустив игру.
	virtual void PostLoad() override;
	// ★ ВОЛНА Ф8: снять копию поэмиттерного свечения ПЕРЕД записью ассета на диск. Одного
	//   PostEditChangeProperty мало: числа в эмиттер попадают и мимо панели деталей (скриптом),
	//   а сохранение — единственная точка, через которую проходит ЛЮБОЙ путь правки.
	//   ⚠ НЕ под `#if WITH_EDITOR`: сигнатура PreSave у UObject не редакторская, и расхождение
	//     объявления с базой дало бы «свою» функцию вместо переопределения — молча, без ошибки.
	virtual void PreSave(FObjectPreSaveContext Context) override;
#if WITH_EDITOR
	// ★ ЗАМОК «переопределено вручную» (волна Ф2-ВСЕ). Любая правка полей FxLight в панели деталей взводит
	//   FxLight.bManual — иначе следующий прогон генератора дефолтов (Tools/asda_apply_fx_light_defaults.py)
	//   молча затёр бы поштучную настройку владельца. Само поле «Переопределено вручную» из этого правила
	//   исключено: снять замок надо иметь возможность, не взводя его тем же движением.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;

	// ★★★ Ф15 (К-1): ПИШЕТ ЛИ В `Emitters` ПАНЕЛЬ ДЕТАЛЕЙ (рука владельца), а не скрипт-сборщик.
	//   true  — рука: запись авторитетна, долговечную копию чистим вместе с эмиттерами;
	//   false — скрипт (перепечка из .jeb): копию сперва РАЗДАЁМ обратно, запись неавторитетна.
	//   Разбор трёх признаков и ссылки на строки движка — в теле функции (AsdaEffectActor.cpp).
	//   Открыт наружу ради самопроверки редакторского плагина: она гоняет через него все формы
	//   события, которые панель деталей строит на каждый жест.
	static bool IsEmittersWriteFromDetailsPanel(const FPropertyChangedEvent& Event);

	// ★ 27.10.2026 (просьба владельца): КОПИРОВАТЬ / ВСТАВИТЬ СВЕТ ЭФФЕКТА — для групп одинаковых эффектов на картах
	//   (MapEffect_eff_chessspade_a_01_54/55/56 …). Копирование кладёт в буфер обмена ВСЁ, что касается света и
	//   свечения: структуру FxLight целиком и свечение каждого эмиттера (Emitters[i].Glow, с именем эмиттера).
	//   Вставка перезаписывает этим текущий эффект: FxLight целиком (+ замок bManual — это ручная настройка),
	//   свечение эмиттеров — по имени эмиттера (при разных именах, но равном числе — по порядку), с отменой Ctrl+Z.
	//   Кнопки зовутся на КАЖДОМ выделенном акторе — вставить можно сразу в группу.
	UFUNCTION(CallInEditor, Category = "Effect Glow", meta = (DisplayName = "Копировать свет"))
	void CopyGlowToClipboard();
	UFUNCTION(CallInEditor, Category = "Effect Glow", meta = (DisplayName = "Вставить свет"))
	void PasteGlowFromClipboard();

	// ═══ ★★★ ВОЛНА Ф15 (С-2, 20.09.2026): «ЭТОТ ЭФФЕКТ УЧАСТВОВАЛ В ОТМЕНЕ/ПОВТОРЕ» ══════════════
	//   ЗАЧЕМ. Конфигуратор свечения после Ctrl+Z раздаёт откат в размещённые экземпляры, иначе
	//   дельта «экземпляр ≠ архетип» впечатывается в .umap. Но он ловил ОБЩЕЕ событие редактора
	//   FEditorDelegates::PostUndoRedo, у которого нет ни одного признака того, ЧТО именно отменили,
	//   и потому обходил ВСЕ ассеты, когда-либо получавшие раздачу, — на любой Ctrl+Z, хоть на
	//   отмену передвижения актора. То есть писал в чужие акторы вне связи с отменяемым действием.
	//   ЧТО ДАЁТ ЭТО СОБЫТИЕ. Движок зовёт PostEditUndo у КАЖДОГО объекта отменяемой транзакции и
	//   только у них (EditorTransaction.cpp:928-957), в том числе у CDO блюпринта. Значит, подписка
	//   на него — точный ответ «наша ли это транзакция»: CDO эффекта попадает в транзакцию только
	//   если его правили мы (конфигуратор) или владелец в панели деталей.
	//   Раздачу при этом здесь НЕ делаем: PostEditUndo идёт ПОСРЕДИ применения транзакции.
	//   Подписчик копит имена и работает уже по PostUndoRedo, когда транзакция применена целиком.
	DECLARE_MULTICAST_DELEGATE_OneParam(FAsdaFxOnPostEditUndo, AAsdaEffectActor* /*Actor*/);
	static FAsdaFxOnPostEditUndo& OnPostEditUndo();
	virtual void PostEditUndo() override;
	virtual void PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation) override;
#endif
	// Перезапуск эмиттеров с нуля (эталон vtbl+4 Play эффекта): форджинг char-create играет eft_creation01 заново.
	void RestartEmitters();

	// ★★★ ЦИКЛ 1:1: ВЛАДЕЛЕЦ ПЕРЕОПРЕДЕЛЯЕТ endMode КОНТЕЙНЕРА после спавна — как эталонные владельцы
	//   пишут `node+0x2A8` (= контейнер +0x29C). ★ 08.10.2026 (ревизия fx_loop, КРИТ-1): писателей в бинаре
	//   НЕ три — скан `.text` (`mov [reg+0x2A8], imm/reg`) дал 44 функции эффектов, 55 мест:
	//     =1 (рестарт всего эффекта каждый период): корона `CharFx_UpdateKingCrown@0xA21110` ×2, оглушение,
	//        яд `dot301`, молчание, паника, `firtim`, `timbom` ×2, `briec` у игрока — 10 функций, 12 мест;
	//     =3 (вечный, пока владелец не снимет): заморозки, сон, кровотечение, `straim`, `flamec`, `defsr`,
	//        `cridn`, `evadw`, `attsdw`, `movdw`, `defdw2`, `defdwe`, `briec` у мобов, ледяные стрелы,
	//        `CChar_PlayEffectOddindex` ×10, аура щита `CCharacter_UpdateEquipAuraEffect@0xA4DD60`,
	//        `dr_ch_eff_n_idle_00` (`LocalPlayer_UpdateRender@0x9D4170`) — 31 функция, 40 мест;
	//     =0: `CEffectMgr_StopByOwner@0xF57370` и `objectMgrResetByHandle@0xF57450` пишут 0 ВМЕСТЕ с флагом
	//        удаления (`byte[+1] = 1`) — эффект удаляется в следующем тике, это мгновенный снос, а не
	//        «доиграть и кончиться» (порт уничтожает актор сразу — верно); капли дождя `fxWaterParticleUpdate@0xB5F510`
	//        (`eff_ches_water_a_02`, системы погоды в порту нет — реестр отложенного).
	//   Состояния (38 функций-обработчиков: 19 игрока + 19 мобов) едут данными `DT_BuffFx` (`FAsdaBuffFxSpawn::Loop`) → `UAsdaEffectsSubsystem::SpawnBuffFx`.
	//   −1 = как в шапке `.jeb` (DT_FxLoop).
	void SetLoopContainerEndOverride(int32 Mode);

	// ★★★ ЦИКЛ 1:1 (08.10.2026, МЕЛК-1 ревизии): длительность и endMode КОНТЕЙНЕРА этого эффекта из `DT_FxLoop`
	//   (с учётом переопределения владельцем) — ДО первого шага симуляции, без побочных эффектов. Нужна
	//   `NaturalLifeSpan`: время жизни одноразового эффекта не короче контейнера 0/2 (эталон гасит эффект
	//   по концу контейнера, а не по концу самого длинного эмиттера). false = ключ `asda.Fx.Loop` выключен
	//   или строки таблицы нет.
	bool PeekLoopContainer(float& OutDur, int32& OutEnd) const;

	// ★★★ ТОЛЬКО ДЛЯ ДИАГНОСТИКИ (`asda.Fx.LoopProbe`, 08.10.2026): ОДИН шаг симуляции мимо гейтов
	//   видимости/дистанции — тот же `SimulateStep`, что рисуется игроку, с наблюдателем из `SampleView`.
	void DebugLoopStep(float Dt);
	// ★★★ ДИАГНОСТИКА ЦИКЛА: режим конца периода эмиттера, таймеры и флаги — для зонда стыка (только чтение).
	bool GetLoopDebug(int32 EmitterIdx, int32& OutMode, int32& OutDiskType, int32& OutDiskEnd, float& OutLife,
		float& OutOver, int32& OutWrap, bool& bOutStopped, bool& bOutSeed) const;
	// ★★★ ДИАГНОСТИКА ЦИКЛА: таймер и параметры контейнера (только чтение).
	void GetLoopContainerDebug(bool& bOutKnown, bool& bOutHolder, float& OutDur, int32& OutEnd, float& OutTimer) const;
	// ★★★ ДИАГНОСТИКА ЦИКЛА: «как выглядит» построенная секция — центроид, радиус, средняя яркость вершин
	//   (альфа × max RGB), средний UV (кадр флипбука) и направление на первую вершину (поворот квада).
	//   Это ровно то, что ушло в рендер (вершины процедурного меша), а не пересчёт по второй формуле.
	bool EmitterSectionLook(int32 EmitterIdx, FVector& OutCentroid, float& OutRadius, float& OutIntensity,
		FVector2D& OutUV, FVector& OutDir0) const;

	// ★ ТОЛЬКО ДЛЯ ДИАГНОСТИКИ (`asda.Fx.MatDump ... recreate`, 09.09.2026). Сносит ВСЕ секции
	//   процедурного меша, как это делает дистанц-кулл: следующий тик соберёт их заново веткой
	//   Create и САМ поставит материал из данных эмиттера. Нужно там, где кулл до эффекта не
	//   дотягивается и «отъезд камеры» ничего не проверяет: у эффектов сцены логина CullDistance
	//   принудительно 1e9 (AsdaLoginGameMode), то есть отъехать за отсечку физически нельзя.
	//   Геймплей не трогает: то же самое происходит на каждом цикле кулла.
	void DebugClearAllSections();

	// ═══ ★★★ ВОЛНА Ф5 (10.09.2026): «СИЛА СВЕЧЕНИЯ» ЭФФЕКТА ПРЯМО НА АКТОРЕ ═══════════════════════
	//   ЗАДАЧА. Яркость самого эффекта (скаляр `EmissiveBoost` четырёх мастеров M_AsdaEffect*) до сих пор
	//   правилась ТОЛЬКО пересборкой таблицы Tools/asda_fx_boost.json: генератор создавал экземпляр
	//   материала `<имя>_fxb<буст×100>` и прописывал его эмиттеру. Владелец же хочет крутить её там же,
	//   где силу света, — в панели деталей.
	//
	//   ★★★ ПОЧЕМУ ЭТО НЕ ДЕЛАЕТСЯ «ОДИН РАЗ ПРИ СТАРТЕ» (грабля, на которой сгорела волна Ф1).
	//   Ветка Create в `RebuildSection` ставит на секцию материал ИЗ ДАННЫХ ЭМИТТЕРА (`E.Material`),
	//   то есть СНИМАЕТ любой назначенный ранее динамический экземпляр. Пересборка секции происходит
	//   при каждой смене топологии и после каждого цикла дистанц-кулла. При этом записи в снятый MID
	//   ПО-ПРЕЖНЕМУ ПРИНИМАЮТСЯ БЕЗ ОШИБОК — просто рисуется им уже не секция; на этом и строилась
	//   неверная проверка «значение записалось, значит работает».
	//   ЛЕЧЕНИЕ: материал секции проходит через `GlowMaterialFor()` В КАЖДОМ месте, где вызывается
	//   `SetMaterial` (их пять). Тогда значение применяется ПОСЛЕ КАЖДОЙ ПЕРЕСБОРКИ по построению,
	//   а не «однажды и с надеждой».
	//
	//   ★ ФЛАГ С ДЕФОЛТОМ «КАК БЫЛО» (закон этого файла, см. ~стр. 140). Механизм спит, пока в ассете
	//     не взведено `FxLight.bOverrideEmissive` (дефолт false у всех 1274 ассетов): `GlowMaterialFor`
	//     возвращает ровно `E.Material`, MID не создаётся, лишней памяти нет. Рантайм-выключатель на
	//     весь механизм — `asda.Fx.Glow.ActorOverride 0`.
	//
	//   Возвращает материал, который надо поставить на слот: сам ассет эмиттера либо MID поверх него
	//   с записанным `EmissiveBoost`. Слот = индекс секции (парная ModAdd-секция = Emitters.Num()+i).
	UMaterialInterface* GlowMaterialFor(int32 Slot, UMaterialInterface* Base);
	// Переставить материалы на ВСЕ уже созданные секции (правка поля в панели деталей: секцию никто
	// не пересобирал, а значение изменилось). Публичная — её же зовёт проверочная консольная команда.
	void RefreshGlowMaterials();

	// ═══ ★★★ 13.09.2026: КОНФИГУРАТОР СВЕЧЕНИЯ — ПРЕДМЕТ, РАДИ КОТОРОГО ЗАСПАВНЕН ЭФФЕКТ ══════════════
	//   Свечение заточки и блёстки дропа спавнятся в рантайме, а один ассет BP_Fx_* общий у многих
	//   предметов. Чтобы настройка «вот этого меча» не легла на все мечи той же буквы и редкости,
	//   эффект знает код своего предмета, и по паре (имя .jeb, код) ищется запись
	//   UAsdaFxLightSettings::Overrides. Transient: в ассет и на карту не пишется.
	// Код предмета item_list (0 = эффект не предметный: карта, навык, стенд без предмета)
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Asda")
	int32 SourceItemId = 0;
	// Поставить код предмета и сразу применить его переопределение: материалы секций и набор светов эффекта
	void SetSourceItemId(int32 ItemId);
	// Действующее переопределение (предмет → эффект) или nullptr; ключ отката asda.Fx.Glow.Overrides учтён. Кэш по ревизии таблиц
	const struct FAsdaFxGlowOverride* FindGlowOverride() const;
	// Действующая «сила свечения» по цепочке «предмет → эффект → поле ассета (Ф5) → семейство»:
	//   true и OutBoost, если её задаёт хоть один уровень; false — рисуется материал эмиттера как есть
	bool ResolveEmissiveOverride(float& OutBoost) const;

private:
	// ★ 13.09.2026: кэш поиска переопределения и правила семейства (ключ — ревизия таблиц, код предмета, число записей).
	//   GlowMaterialFor зовётся на каждой пересборке секции, а поиск идёт по имени .jeb — без кэша это строки и регулярки десятки раз в секунду
	mutable int32 GlowOvRevision = -1;
	mutable int32 GlowOvItem = 0;
	mutable int32 GlowOvNum = -1;
	mutable int32 GlowOvIndex = INDEX_NONE;
	mutable int32 GlowRuleRevision = -1;
	mutable int32 GlowRuleIndex = INDEX_NONE;
	// Имя эффекта (EffectName), под которое собраны оба кэша выше: смена имени .jeb на живом акторе обязана их сбросить.
	//   Сравнивается само поле, а не ResolveJebName — у пустого поля имя берётся из класса, а класс актора не меняется
	mutable FString GlowCacheEffectName;
	// ★ ВОЛНА Ф5: динамические экземпляры материалов «силы свечения», по одному на слот секции.
	//   Transient: в ассет не пишутся (в ассете живёт ЧИСЛО в FxLight.EmissiveBoost, а не экземпляр).
	//   UPROPERTY обязателен — иначе GC съест MID, на который ссылается только PMC-оверрайд.
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> GlowMIDs;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	TArray<FAsdaFxEmitterState> States;
	TArray<int32> SectionVerts;                        // сколько вершин в секции (для смены топологии)
	TArray<FTransform> SectionMeshXf;                  // трансформ актора при последней сборке .jem-меша (кэш: перестроить кристалл при движении с каруселью)
	float MeshSpinAngle = 0.f;                          // накопленный угол спина .jem-кристалла (рад)
	float MeshAnimFrame = 0.f;                          // ★ animFrame .jem-скина (эталон += 1 за кадр рендера)
	float LastFxDebugTime = -1.f;                       // троттл диагностики asda.FxDebug (сек)
	bool bSyncPassed = false;                          // timeSync-гейт пройден
	float ElapsedTime = 0.f;                           // elapsed эффекта (holder.float[69]): спин/дрейф/StartDelay

	// ★★★ ЦИКЛ 1:1 (08.10.2026): контейнер эффекта и холдер карты (шапка .jeb, `DT_FxLoop`).
	TArray<FAsdaFxLoopRt> LoopRt;                      // по эмиттеру: режим конца периода (см. ResolveLoopInfo)
	int32 LoopResolvedKey = -1;                        // снимок ключей asda.Fx.Loop*/End0Freeze, под который разрешено (−1 = ещё нет)
	bool bLoopHolder = false;                          // размещён в уровне карты ⇒ холдер поднимает его по концу контейнера
	bool bLoopContainer = false;                       // контейнер известен (строка таблицы найдена, длительность > 0)
	float LoopContainerDur = 0.f;                      // длительность контейнера, сек (шапка +0x08)
	int32 LoopContainerEnd = 3;                        // endMode контейнера (шапка +0x40)
	int32 LoopContainerEndOverride = -1;               // переопределение владельцем (node+0x2A8), −1 = нет
	float LoopContainerTimer = 0.f;                    // таймер контейнера (как lifeTimer +0x114 эталона)
	bool bLoopContainerDone = false;                   // контейнер окончен (0/2) и холдера нет — погашен ВЕСЬ эффект, шаг не идёт (ЦИКЛ-4, 08.10.2026)
	bool bSyncWaitCross = false;                       // холдер режима 2: контейнер кончился, ждать «анимация ≥ порога» (защёлка эталона +1072)
	float SyncPrevPosMs = -1.f;                        // прошлое anim-время носителя (мс) — только для лога/зонда (детектор пересечения снят 08.10, МЕЛК-3)
	float LoopAllFrozenTime = 0.f;                     // ★ диагностика (08.10): сколько сек ВСЕ видимые эмиттеры застыли подряд
	bool bLoopFrozenWarned = false;                    // ★ диагностика: предупреждение «застыл без владельца» уже выдано

	// ★ Волна CPU 07.09.2026: значимость эффекта (UAsdaVisibilitySubsystem). 2 — симуляция каждый кадр,
	//   1 — через кадр с НАКОПЛЕННЫМ Dt (фаза не рвётся), 0 — заморозка (Dt не копится вовсе).
	UPROPERTY() FAsdaVisState VisState;
	// Накопленный Dt для значимости 1 (шаг делается вдвое реже, но на удвоенное время — суммарное
	//   «время эффекта» ElapsedTime идёт РОВНО как при полной частоте, без скачка фазы).
	float VisPendingDt = 0.f;
	// Габаритный радиус эффекта для конуса значимости (кэш: считается один раз по узлам эмиттеров).
	float VisRadius = -1.f;

	static float RandPM() { return FMath::FRand() - FMath::FRand(); }   // ТРЕУГОЛЬНОЕ [-1,1] = (rand-rand)/32767
	void ResetStates();
	// ★ (Пере)инициализация движения эмиттера — 1:1 `CEmitter_InitReset@0x120F0E0` (см. bEmitterMotion).
	//   Зовётся на старте эмиттера и на КАЖДОМ перезапуске периода (endMode 1/2/3), как в эталоне.
	static void ResetEmitterMotion(const FAsdaFxEmitter& E, FAsdaFxEmitterState& S);
	// ★ Точка эмиссии в локальных UE-осях: EmitPos, повёрнутая марширующими rot-каналами эмиттера
	//   (game RotX·RotY·RotZ, затем RotY холдера) — эталон `CEmitter_Update_v3` (матрица R·N48).
	FVector EmitterLocalPos(const FAsdaFxEmitter& E, const FAsdaFxEmitterState& S, float HolderAngleDeg) const;
	void ClearFxSection(int32 SectionIdx);             // чистит секцию + парную ModAdd-секцию (MaterialB)
	// ★ СНИМОК ТОЧКИ НАБЛЮДАТЕЛЯ НА ШАГ АКТОРА (15.09.2026, «точка наблюдателя в BuildQuads»). До этого BuildQuads
	//   сам звал AsdaView::GetViewPoint (поворот частиц) на КАЖДЫЙ эмиттер и ещё GetViewLocation (биллборд 0xC),
	//   а в редакторе это перебор вьюпортов — на прогреве 90 шагов × эмиттеры. Теперь точка берётся ОДИН раз:
	//   в Tick (та же выборка, что у дистанц-кулла) и один раз на весь прогрев OnConstruction. Приоритет прежний:
	//   оверрайд превью (bUseViewOverride) → правило AsdaView. Разбор путей — Reports/dedup_audit/wave3_asdaview.md.
	struct FAsdaFxViewSample
	{
		FVector Location = FVector::ZeroVector;     // точка наблюдателя (ноль, если её нет)
		FRotator Rotation = FRotator::ZeroRotator;  // поворот наблюдателя (ноль, если его нет)
		bool bHasLocation = false;                  // точка есть (оверрайд превью или AsdaView вернул true)
		bool bHasRotation = false;                  // поворот есть (оверрайд превью или у источника AsdaView он есть)
	};
	// Буферы геометрии одной секции — только для стенда сверки `asda.Fx.ViewSampleCheck` (секции не трогаются)
	struct FAsdaFxQuadCapture
	{
		TArray<FVector> V;          // вершины
		TArray<int32> I;            // индексы (после разворота обхода у зеркального актора)
		TArray<FVector> Nrm;        // нормали
		TArray<FVector2D> UV;       // текстурные координаты
		TArray<FLinearColor> Col;   // цвет вершин
	};
	void SampleView(FAsdaFxViewSample& Out) const;   // выборка точки наблюдателя: оверрайд превью → AsdaView::GetViewPoint
	void SimulateStep(float Dt, const FVector* ViewLoc, const FAsdaFxViewSample& View);   // общий шаг (Tick и прогрев)
	// ★★★ ЦИКЛ 1:1: прочитать `DT_FxLoop` и вывести режимы эмиттеров/контейнер (повторно — при смене ключей).
	void ResolveLoopInfo();
	// ★★★ ЦИКЛ 1:1: каналы после конца периода — марш наклоном последнего сегмента / оборотами (только запись в Cur).
	void ApplyLoopChannels(const FAsdaFxEmitter& E, const FAsdaFxEmitterState& S, const FAsdaFxLoopRt& L, FAsdaFxNode& Cur) const;
	// ★★★ ЦИКЛ 1:1: рестарт ВСЕГО эффекта (контейнер endMode 1 / холдер карты) — `effectParticleReset@0x114D0F0`.
	void RestartLoopContainer();
	// ★★★ ЦИКЛ 1:1 (08.10.2026, СРЕДН-5 ревизии): ЕДИНАЯ выборка узла текущего кадра эмиттера — `NodeAt` на
	//   времени трека (зеркально на нечётном обороте пинг-понга) + марш каналов после конца (`ApplyLoopChannels`).
	//   Её зовут и сборка секции (квады/кольца/меши), и `GetEmitterRuntime` (свет эффекта) — вторая формула не нужна.
	void SampleEmitterNode(int32 EmitterIdx, FAsdaFxNode& Out) const;
	// ★★★ ЦИКЛ-3 (08.10.2026): эмиттер типа, чей draw эталона режет альфу > 1 с записью в канал
	//   (0x0B/0x18, 0x0C/0x19 — квад, 0x0E — кольцо); только при `asda.Fx.Loop 1` (прежний путь бит-в-бит).
	bool IsAlphaClampKind(const FAsdaFxEmitter& E) const;
	// ★★★ ЦИКЛ-3: ДО выборки кадра — сброс поправки альфы, если канал эталона в этом кадре снапается
	//   (сменился сегмент/оборот, период пошёл заново, застыл, удержание альфы).
	void PrepareAlphaClamp(int32 EmitterIdx);
	// ★★★ ЦИКЛ-3: ПОСЛЕ выборки — `if (alpha > 1) alpha = 1` с записью разницы в поправку канала.
	void CommitAlphaClamp(int32 EmitterIdx, FAsdaFxNode& Cur);
	// ★★★ ЦИКЛ 1:1 (08.10.2026): ключ строки `DT_FxLoop` этого актора (имя .jeb в нижнем регистре, `-`→`_`, без `.jeb`).
	FString LoopRowKey() const;
	// Стенд сверки (не Shipping): прежняя выборка внутри сборки против переданного снимка + побайтная сверка буферов
	void ViewSampleStandCheck(int32 SectionIdx, const FAsdaFxEmitter& E, FAsdaFxEmitterState& S,
	                          const FVector& ViewLoc, const FAsdaFxViewSample& View, const FAsdaFxNode& Cur);
	void NodeAt(const FAsdaFxEmitter& E, float T, FAsdaFxNode& Out) const;   // кусочно-линейный track-march
	// Значения СЛЕДУЮЩЕГО узла после времени T (эталон 02_SpawnParticle.c:115: rate=(next−cur)/life);
	// за последним узлом — последний. БЕЗ обёртки периода (Fmod давал цель из начала → «хлопок»).
	void NextNodeAfter(const FAsdaFxEmitter& E, float T, FAsdaFxNode& Out) const;
	// Индекс СЕГМЕНТА трека, внутри которого лежит T = эталонный `trackFrameIdx`
	// (`CEmitter_Update_Track@0x1204DA0`). Клампится в [0, N−2]: у эталона индекс не может дорасти
	// до последнего узла, потому что ветка инкремента живёт под `lifeTimer <= lifetime`, а время
	// последнего узла равно lifetime. Спавн частицы берёт значения узла `Seg`, цель марша — `Seg+1`.
	int32 SegIndexAt(const FAsdaFxEmitter& E, float T) const;
	void SpawnParticle(const FAsdaFxEmitter& E, FAsdaFxEmitterState& S, const FAsdaFxNode& Cur);
	bool GetViewLocation(FVector& Out) const;   // точка из SampleView (оверрайд превью → AsdaView); false — точки нет
	// View — снимок наблюдателя этого шага (поворот частиц, камера биллборда 0xC); Capture != nullptr — только
	// геометрия в буферы для стенда сверки, секции меша не создаются и не чистятся.
	void BuildQuads(int32 SectionIdx, const FAsdaFxEmitter& E, FAsdaFxEmitterState& S,
	                const FVector& ViewLoc, const FAsdaFxViewSample& View, const FAsdaFxNode& Cur,
	                FAsdaFxQuadCapture* Capture = nullptr);
};
