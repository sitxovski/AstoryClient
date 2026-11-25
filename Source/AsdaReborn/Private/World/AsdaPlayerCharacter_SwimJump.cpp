// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacter_SwimJump.cpp — ★ R3-c 12.11.2026 (разрез AsdaPlayerCharacter_Move.cpp, дословный перенос): прыжок и плавание: ключи, высота стояния, состояние воды, прыжок, брызги.
//   Общее для частей — в AsdaPlayerCharacter_Move_Private.h; шапка и реверс — в AsdaPlayerCharacter_Move.cpp.
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"   // ★ ритм ударов задаёт тик анимации (OnSwingCycleCompleted)
#include "Misc/App.h"   // FApp::GetDeltaTime — оценка кадра (точка отправки за кадр до дедлайна)
#include "Materials/MaterialInterface.h"
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1/B2/B9): единый перевод координат игра↔UE
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"   // LocalPlayerActor — «свой» персонаж (звук 2D против 3D)
#include "Engine/GameInstance.h"         // ★ В5fix: GetGameInstance()->GetSubsystem (сборка без unity)
#include "AsdaEffectActor.h"            // шлейф езды = AAsdaEffectActor (attach-режим 6)
#include "World/AsdaWalkGrid.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"   // поза трупа: перемотка клипа death01 в последний кадр
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"                 // ★ 10.10.2026 (копание): меш инструмента на `8_a1`
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "World/AsdaFishingSubsystem.h"      // ★ В5: конец клипа своего действия рыбалки → автомат; Fish_data (модель рыбы)
#include "TimerManager.h"                  // ★ В5: таймер конца клипа действия рыбалки
#include "World/AsdaCombatTrace.h"         // ★ 18.10.2026: журнал боя — замах, кадры удара, стойка, смерть персонажа
#include "Npc/AsdaMobActor.h"              // ★ 18.10.2026: журнал боя — имя и rid цели разворота в замахе
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)
#include "AsdaNetSubsystem.h"              // ★ 23.10.2026 (прыжок донора): 4024 код 131
#include "World/AsdaCrouchStand.h"         // ★ 05.11.2026: присед стоя — поза и 4024 132/133
#include "UI/AsdaChatWindowSubsystem.h"    // ★ 23.10.2026 (прыжок): набор в строке чата — Пробел не прыжок
#include "UI/AsdaSelfStoreSubsystem.h"     // ★ … фокус в чате личной лавки
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ … сцена премиум-гачи
#include "UI/AsdaWindowSubsystem.h"        // ★ … замок ввода модалкой
#include "World/AsdaAdminSubsystem.h"      // ★ … свободная камера админа (Пробел = подъём, команда 87)
#include "Framework/Application/SlateApplication.h"   // ★ … фокус в любом поле ввода
#include "Resources/AsdaGameUserSettings.h"           // ★ 25.10.2026 (присед): режим Ctrl «Нажатие/Зажатие» из окна настроек
#include "EngineUtils.h"                              // ★ 28.10.2026: обход объёмов пост-обработки карты (экспозиция)
#include "AsdaPlayerCharacter_Move_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPlayerCharacter_Move*.cpp

// ══════════ ★★★ ПРЫЖОК И ПЛАВАНИЕ — ПЕРЕНОС ИЗ ДОНОРА AsdaFableClassic (23.10.2026, решение владельца) ══════════
//   [ПОРТ] В эталоне AsdaStory этих механик нет. Источник — статический реверс `Asda2.exe` донора (сводка — MEMANTO
//   «ДОНОР AsdaFableClassic, Asda2.exe», отчёт `Reports/fable_mounts/FABLE_JUMP_SWIM.md`). Числа ниже — константы донора.
//   ПРЫЖОК (`0x40E280` ввод, `0x4AF1A0` свой, `0x4AE120` чужой): чисто визуальный — высоту даёт корень клипа, физики нет;
//     стоит → idlejump01 (слот 24), движется → runjump01 (слот 37), ход не прерывается; переход покой↔ход во время
//     прыжка переносит время клипа; удержание клавиши = цепочка (новый прыжок, когда время idlejump > 868 мс / runjump > 800 мс);
//     верхом и мёртвым — нельзя; сеть — 4024 код 131 (у соседей 4025 → тот же запуск).
//   ПЛАВАНИЕ (`0x46C6E0` свой, `0x46D230` чужой — то же условие, ЛОКАЛЬНО по позиции): без гистерезиса,
//     `ground + 100 < waterY` → плывёт (верхом — «транспорт в воде»); высота корня `waterY − 20` (на суше `ground + 70`),
//     то есть ноги на 90 ниже поверхности; покой → swimloop01, ход → swim01; скорость и оружие не меняются.
//   БРЫЗГИ (`0x4D33E0`): mode 0 swimeffet01 ×1.5 (держится на воде, окно времени swimloop 1.2…1.3 с), mode 1 swimeffet ×10
//     (плывёт, окно swim01 0.858…0.9 с); транспорт в воде: стоя mode 0 ×1.5 раз в 1000 мс, в движении mode 1 ×20 раз в 100 мс.
static TAutoConsoleVariable<int32> CVarAsdaJumpEnable(
	TEXT("asda.Jump.Enable"), 1,
	TEXT("[ПОРТ] Прыжок из донора AsdaFableClassic (Пробел, команда 95 «EXT JUMP»; 4024/4025 код 131). 0 — прыжка нет (как эталон)."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarAsdaSwimEnable(
	TEXT("asda.Swim.Enable"), 1,
	TEXT("[ПОРТ] Плавание из донора AsdaFableClassic (глубина > 100: клипы swim, высота у поверхности, брызги). 0 — как эталон."),
	ECVF_Default);
// ★ 28.10.2026 [ПОРТ] (решение владельца): в воду можно войти. Маска `UAsdaWalkGrid::SwimBits` (генератор
//   `Tools/asda_water_swim.py`; тот же список тайлов снят с бита 0 `.sim` GameServer) делает воду и берег-спуск проходимыми
//   для клика, клавиатуры, автопути и миникарты. 0 — вода снова блок, как в эталоне (сервер при этом всё равно пускает).
static TAutoConsoleVariable<int32> CVarAsdaSwimOpenWater(
	TEXT("asda.Swim.OpenWater"), 1,
	TEXT("[ПОРТ] Открытая вода (DA_WalkGrid.SwimBits) проходима: 1 — можно войти в воду и плыть; 0 — вода непроходима, как эталон."),
	ECVF_Default);
bool AsdaSwimOpenWaterOn() { return CVarAsdaSwimOpenWater.GetValueOnAnyThread() != 0; }
// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 18, баг 9: «игроки проходят сквозь запретные зоны — невидимые стены», Альпен x106 y384 и
//   x54 y403). Маска `SwimBits` открывала блок-тайлы .TER, до которых от суши дойти можно только сквозь стену: обрыв в провал
//   (центр Альпена — под башней) и котловины под обрывами (Санни-Кост), кромка города (ровные блок-тайлы безопасной зоны на уровне
//   мостовой — Альпен, Силалис). Такие тайлы — `UAsdaWalkGrid::SwimWallBits` (правило `Tools/asda_water_swim.py::wall_tiles`);
//   при 1 они непроходимы для клика, клавиатуры, автопути, мини-карты и хода (верхом тоже — общая `IsTileWalkable`).
//   0 — как до правки (стена открыта).
static TAutoConsoleVariable<int32> CVarAsdaSwimWallGuard(
	TEXT("asda.Swim.WallGuard"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Тайлы маски плавания за стеной (DA_WalkGrid.SwimWallBits: обрыв > 230 или кромка города) непроходимы: 1 — "
	     "блок, как у эталона; 0 — открыты, как до правки 08.11.2026."),
	ECVF_Default);
bool AsdaSwimWallGuardOn() { return CVarAsdaSwimWallGuard.GetValueOnAnyThread() != 0; }
// ★ 09.11.2026 [ИСПРАВЛЕНИЕ] (пачка 20, баг 2: «плывёт назад спиной, тем же клипом, что вперёд»). Разбор донора AsdaFableClassic
//   (Asda2.exe, статика): флаг «плывёт» `+219489` (`0x46C6E0`) читают только выбор клипа — состояние «идёт» ставит слот 39 swim01
//   ВМЕСТО бега и «пятки» (`0x4AF1A0` case 1) — высота корня, брызги и сброс; тик хода `0x484D10` его не читает, поэтому курс
//   «пятясь — лицом вперёд» (эталонная пятка [208], на суше с клипом back01) в воде сохраняется, а клип — плавание вперёд:
//   пловец скользит спиной вперёд. Клипа «плыть назад» у донора нет (только swim01/swimloop01). Ключ 1: плывущий (не верхом)
//   смотрит по направлению хода при любом ходе — назад (S), вбок, по диагонали, клик/автопуть (у них курс и так по ходу);
//   клип swim01 и скорость прежние; на выходе из воды «пятка» снова лицом вперёд с back01. 0 — как донор (спиной).
static TAutoConsoleVariable<int32> CVarAsdaSwimFaceMoveDir(
	TEXT("asda.Swim.FaceMoveDir"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Плывущий смотрит по направлению хода и при ходе назад (S): 1 — разворот к ходу, клип swim01; "
	     "0 — как донор: пятясь в воде лицом вперёд (плывёт спиной)."),
	ECVF_Default);
bool AAsdaPlayerCharacter::SwimFacesMoveDir() const
{
	return bSwimming && !bMounted && CVarAsdaSwimFaceMoveDir.GetValueOnGameThread() != 0;
}
// ★ 04.11.2026 [ИСПРАВЛЕНИЕ] (баг владельца: портал Fireway Forest → Flabis не сработал, персонаж ушёл под карту): тайлы
//   `UAsdaWalkGrid::ClosedBits` непроходимы для клика, клавиатуры, автопути и хода. Источник — `Tools/asda_portal_pockets.py`:
//   карман за зоной портала под домиком-порталом и тайлы, открытые в .TER, но закрытые в .sim сервера (паритет хода).
//   0 — проходимость как у эталона (.TER).
static TAutoConsoleVariable<int32> CVarAsdaWalkClosedTiles(
	TEXT("asda.Walk.ClosedTiles"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Тайлы DA_WalkGrid.ClosedBits непроходимы (карман за порталом Fireway Forest → Flabis, паритет с .sim "
	     "сервера): 1 — закрыты; 0 — проходимость как у эталона (.TER)."),
	ECVF_Default);
bool AsdaWalkClosedTilesOn() { return CVarAsdaWalkClosedTiles.GetValueOnAnyThread() != 0; }
// ★ 28.10.2026 [ПОРТ] (решение владельца): пока свой персонаж ПЛЫВЁТ (глубже порога плавания, не верхом) — только
//   перемещение и предметы (кроме транспорта). Запрещены навыки (все пути: ряд, клавиши, окно, Хранитель), обычная атака
//   и PvP, эмоции и сидение (окно действий, клик, клавиши), прыжок, рыбалка, копание лопатой, транспорт из сумки.
//   Отказ МОЛЧА: нотиса «в воде» у оригинала нет (в оригинале нет плавания). 0 — прежнее поведение (всё разрешено).
static TAutoConsoleVariable<int32> CVarAsdaSwimActionGate(
	TEXT("asda.Swim.ActionGate"), 1,
	TEXT("[ПОРТ] Плывущему — только ход и предметы: навыки, атака, эмоции, сидение, прыжок, рыбалка, лопата и транспорт запрещены (1). 0 — без запретов."),
	ECVF_Default);
// ★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (решение владельца: «проигравший в дуэли ОБЯЗАН проиграть анимацию до конца»). Оригинал проигравшего
//   НЕ держит (`Reports/duel/DUEL_LOSER_RE.md`: 4304 → `CCharacter_SetAction(51)` ставит только трек 26 и `bActionBusy`; гейтов ввода
//   на 51 нет ни в клиенте — 402 обращения к +0x2E6FC проверены, — ни в сервере: `CharMove` на ход во время жеста отвечает 4007 code 1),
//   и жест обрывается первым же кликом/клавишей/навыком. [ДЕФЕКТ ОРИГИНАЛА]: пока СВОЙ персонаж в позе 51 (жест поражения, 4.267 с,
//   до конца клипа) — ни хода (общая точка остановки `AAsdaPlayerController::IsStunHardStopped`), ни действий (этот гейт — те же
//   входы, что у плывущего: навыки, атака и PvP, эмоции, сидение, прыжок, рыбалка, лопата, транспорт), ни предметов (гейт входов
//   `UAsdaInventorySubsystem::UseConsumable/UsePremiumItem/UseGachaItem`). Серверные события (страх, снап 4009, смерть, портал) не
//   гейтятся. 0 — как оригинал (жест прерываемый).
static TAutoConsoleVariable<int32> CVarAsdaDuelLoserLock(
	TEXT("asda.Duel.LoserLock"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Проигравший дуэль доигрывает жест поражения (состояние 51) до конца: ход, навыки, атака, эмоции и предметы ждут конца "
	     "клипа (1). 0 — как оригинал: жест прерывается любым действием."),
	ECVF_Default);
bool AsdaDuelLoserLocked(const AAsdaPlayerCharacter* Ch)
{
	return Ch && Ch->IsLocalPlayerCharacter() && Ch->GetCampPoseState() == 51 && CVarAsdaDuelLoserLock.GetValueOnGameThread() != 0;
}
bool AsdaSwimActionLocked(const AAsdaPlayerCharacter* Ch)
{
	// ★ 31.10.2026: тот же набор входов закрыт и на время жеста поражения дуэли (`AsdaDuelLoserLocked`, разбор выше).
	if (AsdaDuelLoserLocked(Ch)) { return true; }
	return Ch && Ch->IsSwimming() && CVarAsdaSwimActionGate.GetValueOnGameThread() != 0;
}
// ★ 28.10.2026 [ИСПРАВЛЕНИЕ]: труп в глубокой воде всплывает к поверхности (донор топит его на 90 под воду — тела не видно).
// ★ 28.10.2026 [ИСПРАВЛЕНИЕ] (баг владельца: «плывущий сидит слишком низко — должна торчать голова, руки на глади»): высота
//   пловца по ЗАМЕРУ костей поз swimloop01/swim01 (самотест `asda.SelfTest.SwimOpenWater`, «поза»), а не «ноги = вода − 90».
//   0 — прежняя постановка (корень донора вода − 20 при точке актора = ноги + 70).
static TAutoConsoleVariable<int32> CVarAsdaSwimHeightFix(
	TEXT("asda.Swim.HeightFix"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Пловец по замеру поз: голова и плечи над водой, кисти на глади (1). 0 — ноги на 90 под водой, как прежде."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarAsdaSwimDeadFloat(
	TEXT("asda.Swim.DeadFloat"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Труп в глубокой воде у поверхности (1). 0 — как донор: тело на 90 ниже поверхности."),
	ECVF_Default);

// ★ Ревизия 24.10 (п.4): присед переключён, пока прежний 4006 ещё без ответа (бит «запрос хода в пути»), — новый статус
//   дошлётся, как только ответ придёт (`TickSwimAndJump` своего). Признак только для своего персонажа — одна ссылка на файл.
TWeakObjectPtr<AAsdaPlayerCharacter> GAsdaCrouchResendPending;

FAsdaCrouchNetSent GAsdaCrouchNetSent;

// Отметка отправленного 4006 своего персонажа (вызов — `UAsdaNetSubsystem::SendMove4006` после успешной отправки/перехвата).
void AsdaCrouchNote4006Sent(const AAsdaPlayerCharacter* Me, int32 Status)
{
	GAsdaCrouchNetSent.Who = Me;
	GAsdaCrouchNetSent.Status = Status;
	GAsdaCrouchNetSent.TimeS = FPlatformTime::Seconds();
}

namespace AsdaFableMove {
	constexpr float kSwimDepth        = 100.f;   // flt_C7501C: ground + 100 < waterY → в воде
	constexpr float kRootAboveGround  = 70.f;    // корень на суше: ground + 70 (0x46CE64)
	constexpr float kRootBelowWater   = 20.f;    // корень в воде: waterY − 20 (у транспорта — поле таблицы +0x104 = −20 у всех)
	constexpr float kCorpseBelowWater = 15.f;    // [ИСПРАВЛЕНИЕ] труп в глубокой воде: точка актора на 15 под поверхностью (тело видно)
	// ★ 28.10.2026 [ИСПРАВЛЕНИЕ] точка актора пловца относительно поверхности (масштаб 1; умножается на масштаб актора по Z).
	//   Замер (средние высоты костей над точкой актора, муж./жен.; Rein): swimloop01 (238) — макушка 64/63, плечи 29/28, кисти
	//   11/4; swim01 (237) — макушка 37/34, плечи 10/8, кисти 3/−9. Цель: кисти на глади, плечи и голова над водой.
	//   ★ Владелец 28.10 по кадрам: «высоковато, нужно ниже» → 1:1 донор (статика `0x46CF5D`/`0x46E0A2` → `SetPosition@0xA39480`):
	//   ноги модели = поверхность − 20 у своего и чужих, для swimloop01 и swim01 одинаково. Порт прежде ошибочно вычитал ещё
	//   «+70» (блок `0x46CE64` заблокированного тайла — в воде его перезаписывает `SetPosition`): было −90.
	//   Стоя −20 → макушка +44/+43, плечи +9/+8, кисти −9/−16; в ходе −20 → макушка +17/+14, плечи −10/−12.
	constexpr float kSwimFeetIdle = -20.f;       // swimloop01: точка актора (ноги) на 20 под поверхностью (донор)
	constexpr float kSwimFeetMove = -20.f;       // swim01: так же (донор не различает клипы)
	constexpr float kSwimLiftRate = 120.f;       // смена позы стоя↔ход: высота едет плавно, ед./с (без скачка на 15)
	constexpr float kChainIdleS       = 0.868f;  // удержание: новый прыжок, когда время idlejump > 868
	constexpr float kChainRunS        = 0.800f;  // … runjump > 800
	constexpr float kSwimEvalPeriodS  = 0.2f;    // [ПОРТ] стоящего пересчитываем 5 раз в с (донор — каждый кадр; позиция та же)
	constexpr float kSplashIdleLoS = 1.200f;     // окно swimloop01 (mode 0): начало
	constexpr float kSplashIdleHiS = 1.300f;     // … конец
	constexpr float kSplashMoveLoS = 0.858f;     // окно swim01 (mode 1): начало
	constexpr float kSplashMoveHiS = 0.900f;     // … конец
	constexpr float kSplashForward = 80.f;       // mode 1: 80 ед. вперёд по курсу
}

// ★ 28.10.2026 [ИСПРАВЛЕНИЕ] высота пловца: текущий подъём точки актора над поверхностью (плавно к цели позы) и последняя
//   поверхность под ним. Своя запись у каждого персонажа (свой и соседи), без полей в заголовке; снимается на суше.
struct FAsdaSwimLift
{
	float Cur = 0.f;        // текущий подъём точки актора над поверхностью, ед.
	float Water = 0.f;      // последняя поверхность воды под персонажем (UE Z)
	bool bValid = false;    // подъём задан (первый вход в воду ставит его сразу, без «всплытия»)
};
static TMap<TWeakObjectPtr<const AAsdaPlayerCharacter>, FAsdaSwimLift> GSwimLift;

// Цель подъёма по позе: ход — swim01, иначе swimloop01; масштаб актора по Z.
static float SwimLiftTarget(const AAsdaPlayerCharacter* C)
{
	using namespace AsdaFableMove;
	const float Sc = C ? FMath::Max(0.01f, (float)C->GetActorScale3D().Z) : 1.f;
	return ((C && C->AnimState == EAsdaPlayerState::Move) ? kSwimFeetMove : kSwimFeetIdle) * Sc;
}

float AAsdaPlayerCharacter::StandZ(float UeX, float UeY)
{
	using namespace AsdaFableMove;
	const float Ground = TerrainZ(UeX, UeY);
	float Water = 0.f;
	bool bDeep = false;
	// ★ Ревизия СРЕДН-2: трейс воды (до двух сложных лучей по мешу воды) — только над тайлом, который сетка карты помечает
	//   водой (`WaterBits` из `.WAT`, тот же флаг у шагов `TickFootsteps`); сетки без воды/нет сетки — трейс как раньше.
	const FVector2D Pg = AsdaCoord::UeToGame2D(FVector(UeX, UeY, 0.f));
	const bool bTileMayBeWater = !WalkGrid || WalkGrid->WaterBits.Num() == 0 || WalkGrid->IsWorldOverWater((float)Pg.X, (float)Pg.Y);
	if (CVarAsdaSwimEnable.GetValueOnGameThread() != 0 && GetWorld() && bTileMayBeWater)
	{
		const float W = AsdaGround::WaterSurfaceZ(GetWorld(), UeX, UeY);   // NaN — над точкой воды нет
		if (!FMath::IsNaN(W) && Ground + kSwimDepth < W) { bDeep = true; Water = W; }
	}
	SwimDepth = bDeep ? (Water - Ground) : 0.f;
	SwimEvalAtGame = AsdaCoord::UeToGame2D(FVector(UeX, UeY, 0.f));
	SwimEvalAccum = 0.f;
	// ★ 28.10.2026 (решение владельца): плавание в приоритете — перебивает и полёт на крыльях (премиум-зелье): в глубокой воде
	//   с крыльями персонаж плывёт (swim01/swimloop01 вместо взмахов, `MoveClip`), как у донора.
	const bool bNewSwim = bDeep && !bMounted;
	const bool bNewMountWater = bDeep && bMounted;
	if (bNewSwim != bSwimming || bNewMountWater != bMountInWater)
	{
		const bool bSwimChanged = (bNewSwim != bSwimming);
		bSwimming = bNewSwim;
		bMountInWater = bNewMountWater;
		bSwimSplashLatch = false;
		SwimSplashAccumMs = 0.f;
		UE_LOG(LogAsdaPlayer, Log, TEXT("[плавание] %s: %s (глубина %.0f)"), *GetName(),
			bSwimming ? TEXT("плывёт") : (bMountInWater ? TEXT("транспорт в воде") : TEXT("на суше")), SwimDepth);
		// Клип меняем только там, где играет клип покоя/хода (эмоция, замах, навык, сидение, действие — свои).
		if (bSwimChanged && !bMounted && (AnimState == EAsdaPlayerState::Idle || AnimState == EAsdaPlayerState::Move)
			&& !IsEtalonActionBusy() && !IsAttackAnimActive() && !IsSkillAnimActive() && !IsEmotionAnimActive() && SitPhase == 0)
		{
			IdleTimer = 0.f; FidgetRemaining = 0.f;
			if (bJumpActive && !bSwimming) { ShowJumpClipForState(); }   // вышел из воды посреди прыжка — прыжок снова виден
			else { JumpClipShown = -1; PlayWorldClip(AnimState == EAsdaPlayerState::Move ? MoveClip() : IdleClipIdx(), true); }
		}
	}
	// В глубокой воде корень на 20 ниже поверхности; у порта точка актора — ноги (на суше = земля, корень донора = +70).
	// ★ 28.10.2026 [ИСПРАВЛЕНИЕ] (смерть в воде): донор кладёт и труп на «корень = вода − 20», то есть лежащее тело на 90 ниже
	//   поверхности — его не видно вовсе (у донора в воду почти не попадали). Труп всплывает: тело на kCorpseBelowWater под
	//   поверхностью. Ключ `asda.Swim.DeadFloat` 0 — как донор.
	if (bDeep && AnimState == EAsdaPlayerState::Dead && CVarAsdaSwimDeadFloat.GetValueOnGameThread() != 0) { return Water - kCorpseBelowWater; }
	// ★ 28.10.2026 [ИСПРАВЛЕНИЕ] плывущий (не верхом) — по замеру поз, с плавной сменой стоя↔ход (`TickSwimAndJump`).
	if (bDeep && bSwimming && AnimState != EAsdaPlayerState::Dead && CVarAsdaSwimHeightFix.GetValueOnGameThread() != 0)
	{
		FAsdaSwimLift& L = GSwimLift.FindOrAdd(this);
		const float Target = SwimLiftTarget(this);
		if (!L.bValid) { L.Cur = Target; L.bValid = true; }
		L.Water = Water;
		return Water + L.Cur;
	}
	if (!bSwimming) { GSwimLift.Remove(this); }
	return bDeep ? (Water - kRootBelowWater - kRootAboveGround) : Ground;
}

void AAsdaPlayerCharacter::UpdateSwimState(bool bForce)
{
	const FVector L = GetActorLocation();
	const bool bWas = bSwimming || bMountInWater;
	const float Z = StandZ(L.X, L.Y);
	// Стоящего ставим на высоту сразу; идущего ставит его же шаг хода (`SetActorLocation` со `StandZ`).
	if ((bForce || bWas || bSwimming || bMountInWater) && !bMoving && !FMath::IsNearlyEqual(L.Z, Z, 0.5f))
	{
		SetActorLocation(FVector(L.X, L.Y, Z));
	}
}

bool AAsdaPlayerCharacter::StartJump(bool bFromNet)
{
	LastJumpRefusal.Reset();
	if (CVarAsdaJumpEnable.GetValueOnGameThread() == 0) { LastJumpRefusal = TEXT("ключ asda.Jump.Enable 0"); return false; }
	// Донор: верхом и мёртвым прыжок не начинается (0x40E374…0x40E3AF; у чужого 0x4AE120 — «верхом не играет»).
	if (bMounted) { LastJumpRefusal = TEXT("верхом"); return false; }
	if (AnimState == EAsdaPlayerState::Dead) { LastJumpRefusal = TEXT("мёртв"); return false; }
	// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущий не прыгает (свой; у соседа прыжок в воде запретил его клиент).
	if (!bFromNet && AsdaSwimActionLocked(this)) { LastJumpRefusal = TEXT("плывёт"); return false; }
	if (!bFromNet)
	{
		// [ПОРТ] Свой прыжок шлёт 4024, а старый сервер пишет код действия в CUser+0x54E4, который читают копание (P66: 110/114
		//   на конце копки), сидение (108), рыбалка. Донор флаг ставит в любом состоянии и отыгрывает его по возврату в покой/ход;
		//   у нас свой прыжок разрешён ТОЛЬКО в покое/ходе без занятого действия — иначе 4024 сорвал бы серверное состояние.
		if (AnimState != EAsdaPlayerState::Idle && AnimState != EAsdaPlayerState::Move) { LastJumpRefusal = TEXT("не покой и не ход"); return false; }
		if (SitPhase != 0) { LastJumpRefusal = TEXT("сидит"); return false; }
		if (IsEtalonActionBusy()) { LastJumpRefusal = TEXT("занят действием (эмоция/рыбалка/копание)"); return false; }
		if (IsAttackAnimActive() || IsSkillAnimActive()) { LastJumpRefusal = TEXT("замах/навык"); return false; }
	}
	// ★ 24.10.2026: нулевая отметка подъёма «шапки» — высота корня в покое/ходе перед прыжком. У цепочки (новый прыжок
	//   посреди прежнего) отметку не переснимаем: корень ещё в воздухе.
	if (!bJumpActive)
	{
		const float H = ReadRootBoneHeight();
		bJumpRestValid = !FMath::IsNaN(H);
		JumpRestRootZ = bJumpRestValid ? H : 0.f;
	}
	CancelJump(TEXT("новый прыжок"));
	bJumpActive = true;
	JumpStartS = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!bFromNet)
	{
		// Донор `0x8C0690(131)` → 4024 тем же путём, что эмоции (тело: свой userIdx, код, курс взгляда).
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			const FVector2D F = GetFacingDirGame();
			Net->SendCharStatePos4024(kJumpActionCode, (float)F.X, (float)F.Y);
		}
	}
	ShowJumpClipForState();
	UE_LOG(LogAsdaPlayer, Log, TEXT("[прыжок] %s: %s, %s, клип %d"), *GetName(), bFromNet ? TEXT("чужой (4025 код 131)") : TEXT("свой (4024 код 131)"),
		AnimState == EAsdaPlayerState::Move ? TEXT("с бега") : TEXT("с места"), JumpClipShown);
	return true;
}

void AAsdaPlayerCharacter::ShowJumpClipForState()
{
	if (!bJumpActive || !GetWorld()) { return; }
	const double Elapsed = GetWorld()->GetTimeSeconds() - JumpStartS;   // время прыжка идёт и тогда, когда клип не виден (донор)
	// Клип прыжка виден только в покое/ходе, не в воде (ветка плавания донора перебивает слот в том же кадре) и не поверх
	//   эмоции/замаха/навыка/сидения/действия.
	const bool bCanShow = (AnimState == EAsdaPlayerState::Idle || AnimState == EAsdaPlayerState::Move) && !bSwimming && !bMounted
		&& !IsEtalonActionBusy() && !IsAttackAnimActive() && !IsSkillAnimActive() && !IsEmotionAnimActive() && SitPhase == 0;
	const int32 Want = (AnimState == EAsdaPlayerState::Move) ? kClipJumpRun : kClipJumpIdle;
	if (!bCanShow)
	{
		JumpClipShown = -1;
		// Конец прыжка без показа — по длине клипа, который был бы виден (донор держит время слота и снимает флаг по концу).
		UAnimSequence* A = UAsdaModelAnimSubsystem::LoadClipChecked(FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), Want));
		const float Len = A ? A->GetPlayLength() : 1.5f;
		const float Left = FMath::Max(0.01f, Len - (float)Elapsed);
		GetWorld()->GetTimerManager().SetTimer(JumpTimer, this, &AAsdaPlayerCharacter::OnJumpClipEnd, Left, false);
		return;
	}
	const float Len = PlayWorldClip(Want, false);
	if (Len <= 0.f) { CancelJump(TEXT("клипа прыжка нет")); return; }
	if (Elapsed >= Len) { JumpClipShown = Want; OnJumpClipEnd(); return; }
	// Перенос времени из прежнего клипа прыжка (донор: время слота 37 переносится в 24 и наоборот, второй обнуляется).
	if (Elapsed > 0.0 && BaseSkel)
	{
		if (UAnimSingleNodeInstance* SN = BaseSkel->GetSingleNodeInstance()) { SN->SetPosition((float)Elapsed, false); }
	}
	JumpClipShown = Want;
	GetWorld()->GetTimerManager().SetTimer(JumpTimer, this, &AAsdaPlayerCharacter::OnJumpClipEnd, Len - (float)Elapsed, false);
}

void AAsdaPlayerCharacter::OnJumpClipEnd()
{
	if (!bJumpActive) { return; }
	const int32 Shown = JumpClipShown;
	bJumpActive = false;
	JumpClipShown = -1;
	// Донор: «возврата как отдельного действия нет» — со следующего кадра играет обычный клип покоя/хода. Возвращаем его,
	//   только если на экране всё ещё наш клип прыжка (его не перебили замах/эмоция/смерть/вода).
	if (Shown >= 0 && GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), Shown))
		&& (AnimState == EAsdaPlayerState::Idle || AnimState == EAsdaPlayerState::Move))
	{
		PlayWorldClip(AnimState == EAsdaPlayerState::Move ? MoveClip() : IdleClipIdx(), true);
		IdleTimer = 0.f; FidgetRemaining = 0.f;
	}
}

// ★ 24.10.2026 [ПОРТ] (владелец: при прыжке поднималось только тело, «шапка» над головой оставалась на месте). Ключ отката —
//   0: подъёма нет, всё над головой висит на высоте актора, как было 23.10.
static TAutoConsoleVariable<int32> CVarAsdaJumpOverheadFollow(
	TEXT("asda.Jump.OverheadFollow"), 1,
	TEXT("[ПОРТ] 1 — плашка (HP, ник, звания, клан, значки), облачко чата, надпись и сам хранитель души, корона поднимаются ")
	TEXT("вместе с телом в прыжке (по кости корня клипа). 0 — остаются на высоте актора (как 23.10)."),
	ECVF_Default);

float AAsdaPlayerCharacter::ReadRootBoneHeight() const
{
	if (!BaseSkel) { return NAN; }
	// Кость корня тела: M у мужской модели, G у женской (та же, что меряет `asda.SelfTest.JumpSwim`).
	for (const TCHAR* Bone : { TEXT("M"), TEXT("G") })
	{
		const FName B(Bone);
		if (BaseSkel->GetBoneIndex(B) != INDEX_NONE)
		{
			// «Хвост» сетевой поправки (смещение MeshRoot) вычитаем: он к прыжку не относится и у своего уже несёт плашку.
			return (float)(BaseSkel->GetBoneLocation(B).Z - GetActorLocation().Z - VisualTail.Z);
		}
	}
	return NAN;
}

void AAsdaPlayerCharacter::TickJumpLift(float DeltaSeconds)
{
	// Пока на экране клип прыжка — подъём из живой позы (кость корня минус отметка покоя; присед на старте не опускает
	//   «шапку» ниже обычного — на земле всё как раньше). Клип сменился/кончился/ключ 0 — подъём гаснет за ~0,1 с
	//   (на конце клипа корень уже у земли, остаток — единицы разницы поз покоя и прыжка).
	bool bLive = false;
	float Target = 0.f;
	if (CVarAsdaJumpOverheadFollow.GetValueOnGameThread() != 0 && bJumpActive && JumpClipShown >= 0 && bJumpRestValid
		&& GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), JumpClipShown)))
	{
		const float H = ReadRootBoneHeight();
		if (!FMath::IsNaN(H)) { bLive = true; Target = FMath::Max(0.f, H - JumpRestRootZ); }
	}
	if (bLive) { JumpLiftZ = Target; return; }
	if (JumpLiftZ != 0.f)
	{
		constexpr float kLiftFadeTauS = 0.04f;   // постоянная времени гашения остатка
		JumpLiftZ *= FMath::Exp(-FMath::Max(0.f, DeltaSeconds) / kLiftFadeTauS);
		if (JumpLiftZ < 0.5f || CVarAsdaJumpOverheadFollow.GetValueOnGameThread() == 0) { JumpLiftZ = 0.f; }
	}
}

void AAsdaPlayerCharacter::CancelJump(const TCHAR* Why)
{
	if (!bJumpActive) { return; }
	bJumpActive = false;
	JumpClipShown = -1;
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(JumpTimer); }
	UE_LOG(LogAsdaPlayer, Verbose, TEXT("[прыжок] %s: снят (%s)"), *GetName(), Why);
}

void AAsdaPlayerCharacter::TickJumpInput(bool bKeyDown, bool bInputAllowed)
{
	using namespace AsdaFableMove;
	// Донор `0x40E280`: клавиша не нажата (или нет фокуса) → «отпущена»; иначе фронт → прыжок, удержание → цепочка у конца
	//   клипа (idlejump > 868 мс в покое, runjump > 800 мс в ходе).
	if (!bKeyDown || !bInputAllowed) { bJumpKeyReleased = true; return; }
	if (!bJumpActive)
	{
		if (bJumpKeyReleased && StartJump(false)) { bJumpKeyReleased = false; }
		return;
	}
	const double Elapsed = GetWorld() ? (GetWorld()->GetTimeSeconds() - JumpStartS) : 0.0;
	const float Chain = (AnimState == EAsdaPlayerState::Move) ? kChainRunS : kChainIdleS;
	if (Elapsed > Chain && StartJump(false)) { bJumpKeyReleased = false; }
}

bool AAsdaPlayerCharacter::IsJumpInputAllowed() const
{
	// ★ Ревизия КРИТ-1/СРЕДН-1 (23.10.2026): у UE буква, не обработанная полем ввода, ВСПЛЫВАЕТ до вьюпорта — уровень клавиши у
	//   контроллера взводится и при наборе текста (прежний комментарий «Slate отдаёт клавишу виджету» был неверен). Поэтому те же
	//   явные гейты, что глушат движение в `AAsdaPlayerController::PollKeys` (`AsdaPlayerController_Keys.cpp`): строка чата, чат
	//   личной лавки, сцена премиум-гачи, рыбалка, замок модалкой — плюс свободная камера админа (там Пробел — подъём, команда 87)
	//   и фокус в ЛЮБОМ поле ввода (имена, письма, поиск — приём `AsdaDevMenuSubsystem`). Донор (`0x40E280`) прыгает только при
	//   фокусе окна игры и «готовом мире» (`sub_5DE320 && sub_796AE0`).
	const UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	if (const UAsdaChatWindowSubsystem* Cw = GI->GetSubsystem<UAsdaChatWindowSubsystem>()) { if (Cw->IsInputActive()) { return false; } }
	if (const UAsdaSelfStoreSubsystem* St = GI->GetSubsystem<UAsdaSelfStoreSubsystem>()) { if (St->IsChatInputFocused()) { return false; } }
	if (const UAsdaPremiumGachaSubsystem* Pg = GI->GetSubsystem<UAsdaPremiumGachaSubsystem>()) { if (Pg->IsSceneOpen()) { return false; } }
	if (UAsdaFishingSubsystem::IsFishingNow(this)) { return false; }
	if (const UAsdaWindowSubsystem* Wm = GI->GetSubsystem<UAsdaWindowSubsystem>()) { if (Wm->IsAnyModalOpen()) { return false; } }
	if (const UAsdaAdminSubsystem* Ad = GI->GetSubsystem<UAsdaAdminSubsystem>()) { if (Ad->IsFreeCamActive()) { return false; } }
	if (FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (Focused.IsValid() && Focused->GetTypeAsString().Contains(TEXT("EditableText"))) { return false; }
	}
	return true;
}

void AAsdaPlayerCharacter::TickSwimAndJump(float DeltaSeconds)
{
	using namespace AsdaFableMove;
	// Свой персонаж: опрос команды 95 «EXT JUMP» (Пробел) — только когда игровой ввод не заглушён (`IsJumpInputAllowed`).
	if (IsLocalPlayerCharacter())
	{
		const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetController());
		const bool bInputAllowed = PC != nullptr && IsJumpInputAllowed();
		TickJumpInput(PC && PC->IsCmdKeyDown(95), bInputAllowed);
		// ★ 24.10.2026 присед по Ctrl (левый или правый): одиночное нажатие. «Новое нажатие другой клавиши/кнопки мыши, пока
		//   Ctrl зажат» — сочетание (Ctrl+N/Y/W/P/C/S чата, Ctrl+клик): не переключаем. Клавиши, зажатые ДО Ctrl (ход W/S),
		//   сочетанием не считаются. Опрос остальных клавиш — только пока Ctrl зажат.
		const bool bCtrl = PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
		bool bNewOther = false;
		// ★ Ревизия 24.10: ложные переключения. (1) AltGr = LCtrl + RAlt в ОДНОМ кадре — новые клавиши кадра нажатия тоже считаются
		//   (снимок прошлого кадра ведётся всегда, пока Ctrl зажат или только что зажат); любой Alt при зажатом Ctrl — сочетание.
		//   (2) Ctrl+клик по окну UMG — кнопка мыши у Slate (контроллер её не видит). (3) Окно игры потеряло фокус с зажатым
		//   Ctrl (Alt+Tab, Win) — сброс клавиш движком выглядит отпусканием: не переключаем.
		if (bCtrl)
		{
			if (PC->IsInputKeyDown(EKeys::LeftAlt) || PC->IsInputKeyDown(EKeys::RightAlt)) { bNewOther = true; }
			if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() > 0) { bNewOther = true; }
			// ★ 08.11.2026 (пачка 15, баг 3) [ИСПРАВЛЕНИЕ] Shift и Win, зажатые ДО Ctrl, — тоже сочетание (Shift+Ctrl — смена раскладки
			//   Windows): прежде «зажатое до Ctrl» сочетанием не считалось (правило для клавиш хода), и отпускание Ctrl сажало в присед.
			if (AsdaCrouchStrayGuardOn() && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)
				|| PC->IsInputKeyDown(EKeys::LeftCommand) || PC->IsInputKeyDown(EKeys::RightCommand)))
			{
				bNewOther = true;
			}
		}
		// ★ 08.11.2026 (пачка 15, баг 3) [ИСПРАВЛЕНИЕ] нажатия, которых контроллер не видит: окно UMG съело клавишу/клик/колесо
		//   (Ctrl+колесо над списком, Ctrl+клик по окну). Счётчик ведёт перехватчик Slate (до виджетов) — новое событие при
		//   зажатом Ctrl = сочетание. Счётчик читается каждый кадр (событие до нажатия Ctrl сочетанием не станет).
		if (AsdaCrouchSlateEventsSince() && bCtrl && AsdaCrouchStrayGuardOn()) { bNewOther = true; }
		{
			// Фокус окна на момент нажатия Ctrl; сменился за время удержания (Alt+Tab, Win) — сочетание. Окно без фокуса всё время
			//   (офлайн-прогон -RenderOffscreen) переключению не мешает.
			static int32 GCrouchCtrlPressFg = -1;   // -1 — Ctrl не зажат; 0/1 — фокус в кадре нажатия
			const int32 Fg = (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive()) ? 1 : 0;
			if (bCtrl && GCrouchCtrlPressFg < 0) { GCrouchCtrlPressFg = Fg; }
			if (GCrouchCtrlPressFg >= 0 && Fg != GCrouchCtrlPressFg) { bNewOther = true; }
			if (!bCtrl) { GCrouchCtrlPressFg = -1; }
		}
		if (PC)
		{
			static TArray<FKey> GCrouchWatchKeys;   // клавиатура и кнопки мыши (без Ctrl, геймпада, осей, касаний)
			if (GCrouchWatchKeys.Num() == 0)
			{
				TArray<FKey> All; EKeys::GetAllKeys(All);
				for (const FKey& K : All)
				{
					if (K.IsGamepadKey() || K.IsTouch() || K.IsAxis1D() || K.IsAxis2D() || K.IsAxis3D() || K.IsAnalog()) { continue; }
					if (K == EKeys::LeftControl || K == EKeys::RightControl || K == EKeys::AnyKey) { continue; }   // AnyKey — «любая», не клавиша
					GCrouchWatchKeys.Add(K);
				}
			}
			TArray<FKey> Down;
			// ★ 08.11.2026 (пачка 15, баг 3) [ИСПРАВЛЕНИЕ] уровень клавиши (`IsInputKeyDown`) не видит нажатие и отпускание в ОДНОМ кадре
			//   (`UPlayerInput`: PressDelta 0 → прежний bDown): колесо мыши так приходит всегда, быстрое нажатие — при низком FPS.
			//   Ctrl+колесо / Ctrl+быстрая клавиша считались одиночным Ctrl → присед «сам». События кадра (`WasInputKeyJustPressed`)
			//   ловят и их. Ключ `asda.Crouch.StrayGuard` (0 — прежнее: только уровень).
			const bool bEvents = bCtrl && AsdaCrouchStrayGuardOn();
			for (const FKey& K : GCrouchWatchKeys)
			{
				if (PC->IsInputKeyDown(K)) { Down.Add(K); if (bCtrl && !CrouchPrevKeysDown.Contains(K)) { bNewOther = true; } }
				else if (bEvents && PC->WasInputKeyJustPressed(K)) { bNewOther = true; }
			}
			CrouchPrevKeysDown = MoveTemp(Down);
		}
		// ★ 25.10.2026 «Зажатие»: Ctrl считается зажатым и по состоянию клавиш платформы, пока окно игры активно. После смены карты
		//   новый контроллер не знает о Ctrl, зажатом ДО портала (автоповтор Windows приходит «повтором», а повтор `PlayerInput`
		//   нажатием не считает), — без этого игрок с зажатым Ctrl вставал бы на новой карте. Чужое окно активно — платформу не
		//   спрашиваем. В «Нажатии» — только ввод контроллера, как прежде.
		bool bCtrlCrouch = bCtrl;
		if (!bCtrlCrouch && AsdaCrouchHoldModeOn() && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive())
		{
			bCtrlCrouch = FSlateApplication::Get().GetModifierKeys().IsControlDown();
		}
		TickCrouchInput(bCtrlCrouch, bNewOther, bInputAllowed);
		AsdaCrouchStand::TickRelay(this);   // ★ 05.11.2026: досылка 4024 132/133 (не чаще 250 мс)
		// Досылка статуса приседа после ответа на прежний 4006 (см. GAsdaCrouchResendPending).
		if (GAsdaCrouchResendPending.Get() == this)
		{
			UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!Net || !Net->bInGameConnected || !bMoving || !IsMovingToPoint()) { GAsdaCrouchResendPending.Reset(); }
			else if (AsdaCrouchNetCoalesceOn()) { AsdaSyncCrouchStatusToServer(this, Net); }   // ★ 25.10.2026: без повторов и не чаще 250 мс
			else if (!Net->IsMoveRequestInFlight())
			{
				GAsdaCrouchResendPending.Reset();
				const FVector2D Cur = GetGamePosXZ();
				if (FVector2D::Distance(Cur, MoveDstGame) > 1.f)
				{
					Net->SendMove4006((float)Cur.X, (float)Cur.Y, (float)MoveDstGame.X, (float)MoveDstGame.Y, LastSent4006TargetRid);
					UE_LOG(LogAsdaPlayer, Log, TEXT("[присед] %s: статус хода %d дослан после ответа на прежний 4006 (цель rid %d)"), *GetName(),
						GetMoveStatusFor4006(), LastSent4006TargetRid);
				}
			}
		}
	}
	// ★ 24.10.2026: темп клипа приседа идёт за скоростью (грант 4007 / 6071 меняют её посреди хода) и за знаком (пятясь — назад;
	//   смена направления без смены пятки, например курс соседа или клавиатурный грант, — тоже здесь).
	if (BaseSkel && GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), kClipCrouchWalk)))
	{
		// ★ 05.11.2026 [ПОРТ] стоя — поза приседа: кадр anim_239 удерживается (темп 0, кадр ставит `PlayWorldClip`).
		const float R = AsdaCrouchStand::Shown(this) ? 0.f : CrouchClipRate();
		if (!FMath::IsNearlyEqual(BaseSkel->GetPlayRate(), R, 0.01f)) { BaseSkel->SetPlayRate(R); }
	}
	// Вода у стоящего: позиция та же, а седло/вода могли смениться (посадка, спавн, телепорт без хода).
	// ★ 28.10.2026 [ИСПРАВЛЕНИЕ] высота пловца плавно к цели позы (стоя ↔ ход); стоящего двигаем здесь же.
	if (bSwimming && AnimState != EAsdaPlayerState::Dead && CVarAsdaSwimHeightFix.GetValueOnGameThread() != 0)
	{
		if (FAsdaSwimLift* L = GSwimLift.Find(this))
		{
			const float Target = SwimLiftTarget(this);
			if (!FMath::IsNearlyEqual(L->Cur, Target, 0.01f))
			{
				L->Cur = FMath::FInterpConstantTo(L->Cur, Target, DeltaSeconds, AsdaFableMove::kSwimLiftRate);
				if (!bMoving)
				{
					const FVector Lc = GetActorLocation();
					SetActorLocation(FVector(Lc.X, Lc.Y, L->Water + L->Cur));
				}
			}
		}
	}
	SwimEvalAccum += DeltaSeconds;
	if (SwimEvalAccum >= kSwimEvalPeriodS || FVector2D::Distance(GetGamePosXZ(), SwimEvalAtGame) > 1.f) { UpdateSwimState(false); }

	if (bSwimming)
	{
		const FString Clip = GetPlayingClipName();
		const bool bMoveClip = Clip.EndsWith(FString::Printf(TEXT("anim_%d"), kClipSwimMove));
		const bool bIdleClip = Clip.EndsWith(FString::Printf(TEXT("anim_%d"), kClipSwimIdle));
		if (bMoveClip || bIdleClip)
		{
			const float T = GetAttackClipPositionSec();   // позиция текущего клипа (общий геттер)
			const float Lo = bMoveClip ? kSplashMoveLoS : kSplashIdleLoS;
			const float Hi = bMoveClip ? kSplashMoveHiS : kSplashIdleHiS;
			if (T > Lo && T < Hi)
			{
				// [ИСПРАВЛЕНИЕ] у донора защёлка глобальная, а у чужих (`0x4AE120`) не взводится вовсе — брызги каждый кадр окна.
				//   Здесь защёлка своя у каждого персонажа: одни брызги на проход клипа.
				if (!bSwimSplashLatch) { bSwimSplashLatch = true; SpawnSwimSplash(bMoveClip ? 1 : 0, bMoveClip ? 10.f : 1.5f); }
			}
			else { bSwimSplashLatch = false; }
		}
	}
	else if (bMountInWater)
	{
		SwimSplashAccumMs += DeltaSeconds * 1000.f;
		const float Period = bMoving ? 100.f : 1000.f;
		if (SwimSplashAccumMs >= Period) { SwimSplashAccumMs = 0.f; SpawnSwimSplash(bMoving ? 1 : 0, bMoving ? 20.f : 1.5f); }
	}
}

namespace AsdaCrouchStand {
	static constexpr double kRelayGapS = 0.25;      // не чаще: быстрые нажатия Ctrl не плодят 4024
	static constexpr double kRelaySettleS = 0.12;   // код уходит, когда переключение отстоялось (вкл→выкл подряд — ничего)

	// Отправить код сейчас (свой персонаж стоит, связь есть). Возврат — ушёл ли.
	static bool SendNow(AAsdaPlayerCharacter* Me, int32 Code, const TCHAR* Why)
	{
		UGameInstance* GI = Me->GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return false; }
		const FVector2D F = Me->GetFacingDirGame();   // тело 4024 — курс взгляда (как у 108/131)
		const bool bOk = Net->SendCharStatePos4024(Code, (float)F.X, (float)F.Y);
		if (bOk)
		{
			GRelay.Who = Me; GRelay.SentCode = Code; GRelay.SentS = FPlatformTime::Seconds(); GRelay.PendingCode = -1; ++GRelay.SentCount;
		}
		UE_LOG(LogAsdaPlayer, Log, TEXT("[присед] стоя — 4024 код %d (%s): %s"), Code, Why, bOk ? TEXT("ушёл") : TEXT("не ушёл (нет связи / гейт)"));
		return bOk;
	}

	void TickRelay(AAsdaPlayerCharacter* Me)
	{
		if (!Me || GRelay.Who.Get() != Me || GRelay.PendingCode < 0) { return; }
		if (Me->IsMoving() || CVarAsdaCrouchStandRelay.GetValueOnGameThread() == 0) { GRelay.PendingCode = -1; return; }
		const double Now = FPlatformTime::Seconds();
		if (Now - GRelay.PendingS < kRelaySettleS) { return; }
		if (GRelay.SentS >= 0.0 && Now - GRelay.SentS < kRelayGapS) { return; }
		const int32 Code = GRelay.PendingCode;
		if (Code == GRelay.SentCode) { GRelay.PendingCode = -1; return; }   // за время ожидания вернулись к отправленному
		SendNow(Me, Code, TEXT("досылка"));
	}
}

void AAsdaPlayerCharacter::SpawnSwimSplash(int32 Mode, float Scale)
{
	using namespace AsdaFableMove;
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FVector L = GetActorLocation();
	// Донор: точка — позиция персонажа (корень = waterY − 20); mode 0 — +20 (на поверхности), mode 1 — 80 вперёд по курсу.
	//   [ПОРТ] у mode 1 донор прибавляет к высоте «2 + глубина» от корня — это выше поверхности на всю глубину; ставим
	//   на поверхность + 2.
	const float Surface = L.Z + kRootAboveGround + kRootBelowWater;   // ноги + 90 = поверхность воды
	FVector At(L.X, L.Y, Mode == 0 ? Surface : Surface + 2.f);
	if (Mode == 1)
	{
		const FVector UeFw(FMath::Cos(FMath::DegreesToRadians(GetFacingYawUE())), FMath::Sin(FMath::DegreesToRadians(GetFacingYawUE())), 0.f);
		At += UeFw * kSplashForward;
	}
	const TCHAR* Jeb = (Mode == 0) ? TEXT("swimeffet01") : TEXT("swimeffet");
	// Донор: поворот π (`sub_94118F(…, 3.14159)`) и равномерный масштаб `sub_A44960(scale)` (матрица (s, s, s)).
	if (AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, Jeb, At, this, FRotator(0.f, 180.f, 0.f)))
	{
		Fx->SetActorScale3D(FVector(Scale));
	}
	else
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[плавание] BP_Fx_%s не собран (Tools/asda_fable_mounts_fx.py)"), Jeb);
	}
}
