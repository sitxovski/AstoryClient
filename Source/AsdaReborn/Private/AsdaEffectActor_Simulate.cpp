// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor_Simulate.cpp — ★ R2 10.11.2026 (разрез AsdaEffectActor.cpp, дословный перенос): рантайм: узлы/сегменты, частицы, вид, Tick, носитель, SimulateStep.
//   Общее для частей — в AsdaEffectActorInternal.h; шапка и реверс — в AsdaEffectActor.cpp.
#include "AsdaEffectActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "AsdaSelfTestBatch.h"   // ★ ревизия В2 дедупа №2 (11.10.2026): Quit — выход самотеста по итогу (перехватывается пакетом)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): имя параметра EmissiveBoost
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1): единый своп осей игра→UE
#include "ProceduralMeshComponent.h"
#include "AsdaFxNames.h"   // теги свечения заточки (цвет вершин)
#include "AsdaFxDeadHost.h"   // ★ R5-fix5 (13.11.2026): следящий эффект гаснет вместе с умершим мобом-носителем
#include "Npc/AsdaMobActor.h"  // ★ R5-fix5: признак «моб мёртв» (IsDead)
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"   // ★ Ф17 (С-1): уборка сирот обязана отменяться Ctrl+Z — разбор у DropOldestGlowOrphans
#endif
#include "Kismet/GameplayStatics.h"
// ★ Для консольной проверки яркости `asda.Fx.MatDump` (конец файла): чтение скалярного параметра
//   с ЭКЗЕМПЛЯРА материала на секции, отложенные шаги сценария и выполнение консольных команд.
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"   // FMaterialParameterInfo (MaterialTypes.h объявлен устаревшим)
#include "Materials/MaterialInstanceDynamic.h"  // ★ Ф5: MID «силы свечения» поверх материала эмиттера
#include "TimerManager.h"
#include "EngineUtils.h"   // TActorIterator — чтение живых эффектов мира (`asda.Fx.SectionDump`)
#include "UObject/UObjectIterator.h"   // ★ Ф16 (МЕЛК-4): TObjectIterator — CDO запечённых BP_Fx_* (в мире их нет)
#include "World/AsdaFxLightSettings.h"    // ★ 13.09.2026: переопределения свечения предмета/эффекта и сила свечения семейств
#include "World/AsdaFxLightSubsystem.h"   // ★ 13.09.2026: ключ отката asda.Fx.Glow.Overrides, имя .jeb, перезаведение света
#include "Data/AsdaFxLoopRows.h"          // ★ 08.10.2026: ЦИКЛ 1:1 — endMode диска, контейнер, сид-узлы карт (DT_FxLoop)
#include "Engine/DataTable.h"             // ★ 08.10.2026: чтение DT_FxLoop
#include "AsdaDataTableCache.h"            // ★ дедуп №2 В2-C (F3): AsdaDt::Cached — общая загрузка DT_FxLoop
#include "UI/AsdaEffectsSubsystem.h"        // ★ дедуп №2 В2-C (F3): удержание DT_FxLoop на сессию (FAsdaHeldTables)
#include "Engine/GameInstance.h"           // ★ дедуп №2 В2-C (F3): GameInstance эффекта — владелец удержания
#include "Components/SkeletalMeshComponent.h"   // ★ 08.10.2026: холдер режима 2 — порог анимации носителя («≥ порога» с защёлкой)
#include "World/AsdaWorldKind.h"              // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "World/AsdaFxLightComponent.h"        // ★ 15.10.2026: RestVerts строятся только при свете эффекта (IsCarrierAnchorOn)
#if WITH_EDITOR
#include "HAL/PlatformApplicationMisc.h"      // ★ 27.10.2026: буфер обмена кнопок «Копировать/Вставить свет»
#endif
#include "AsdaEffectActorInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaEffectActor*.cpp

// ★ Д5 + ★ Аудит №3 (B1): ЕДИНЫЙ закон осей game→UE = (gx, −gz, gy), т.е. компоненты (−Z, X, Y).
//   Своя копия `ToUeV` убрана — тот же своп теперь живёт в `AsdaCoord::GameToUe3D` вместе с
//   двухмерными вариантами, которыми пользуется весь мир (моб, игрок, контроллер, подсистемы).
// ★ Диагностика частиц/скина: `asda.FxDebug 1` в консоли — раз в секунду печатает по актору живые
//   частицы каждого эмиттера, кадр .jem-скина и мировую позицию. Дефолт 0 (в игре не спамит).
static ASDA_DEV_CVAR(int32, CVarAsdaFxDebug,
	TEXT("asda.FxDebug"), 0,
	TEXT("1 = раз в секунду логировать живые частицы/кадр скина эффектов Asda"), ECVF_Cheat);

// ★★★ ЭФФЕКТ НА ДВИЖУЩЕМСЯ НОСИТЕЛЕ — КАЖДЫЙ КАДР (15.10.2026, wave_PORT_mount_fx.md; довод — у IsCarrierFollower в .h).
//   1 = эффект, следующий за носителем (шлейф транспорта, свечение оружия, крылья, эффекты состояний), пока носитель
//       ДВИЖЕТСЯ, гейтом значимости НЕ прореживается: на уровне 1 шагает каждый кадр, как в оригинале; на уровне 0
//       (вне расширенного кадра) шаг по-прежнему стоит, но построенная геометрия ПЕРЕНОСИТСЯ вслед за носителем.
//       Стоящий носитель — прежняя экономия (ревизия 15.10.2026, review_PORT_mobpos_mountfx.md п. 4–5: цена шага
//       ≈3.4 мкс на эмиттер, у байка 42 эмиттера — полный темп всем стоящим свечениям/крыльям стоил бы даром);
//   0 = как было (уровень 1 — через кадр, уровень 0 — геометрия стоит на месте заморозки).
//   Эффектов БЕЗ слежения (карты, весь запечённый контент) ключ не касается.
static TAutoConsoleVariable<int32> CVarAsdaFxFollowFullRate(
	TEXT("asda.Fx.FollowFullRate"), 1,
	TEXT("1 = эффекты на движущемся носителе шагают каждый кадр и не отстают от него (как оригинал); 0 = как было (через кадр / замороженная геометрия)"),
	ECVF_Cheat);

// ★★★ endMode У МЕШ-ЭМИТТЕРОВ (0x11/0x12/0x1F). До 2026-08-10 ветка меша в `SimulateStep`
//   зацикливала период БЕЗУСЛОВНО (`Fmod`), поле `EndMode` там не читалось вообще — 77 из 78
//   меш-эмиттеров эффектов навыков (29 `.jeb`) имеют на диске `endMode = 0` и обязаны отыграть
//   ОДИН раз. Эталон `effectEmitter_tickUpdate@0x12293B0`, ветка `age > lifetime`:
//     case 0 → `effectInstanceResetToInitial` + `age = −1` + **return 0** (эмиттер снят);
//     case 1 → реинит (vtbl+4), case 2 → randomize+init, case 3 → `age = 0` + reset — ВСЕ ТРИ ЛУП.
//   (Заметь: у меша `3` — это перезапуск, а НЕ «фриз последнего кадра», как у квад/кольцо-ветки.)
//   `asda.Fx.MeshEndMode 0` возвращает прежнее «всегда зациклен» — на случай, если где-то
//   принятая картина была построена на старом поведении.
static TAutoConsoleVariable<int32> CVarAsdaFxMeshEndMode(
	TEXT("asda.Fx.MeshEndMode"), 1,
	TEXT("1 = меш-эмиттеры соблюдают endMode диска (0=стоп после первого проигрыша); 0 = всегда луп"), ECVF_Cheat);

// ★ ЧИСЛОВОЕ ДОКАЗАТЕЛЬСТВО ПО КАДРАМ (только чтение и печать; пусто = молчит, накладных нет).
//   Пишет ПО КАЖДОМУ КАДРУ тот самый путь, который рисуется игроку (закон
//   [[diagnostic-must-use-real-path]]): таймер периода, индекс сегмента, значения канала из `NodeAt`
//   (ими строится геометрия квада/кольца и питается свет эффекта) И значения, с которыми РЕАЛЬНО
//   рождается частица, плюс живые частицы слотов. Именно на стыке периода эти два набора расходились.
//   Запуск: `asda.Fx.TrackLog eff_mdel_alpen_smoke` — фильтр по части имени эффекта.
static ASDA_DEV_CVAR(FString, CVarAsdaFxTrackLog,
	TEXT("asda.Fx.TrackLog"), TEXT(""),
	TEXT("часть имени эффекта — печатать покадровый ряд каналов эмиттеров (только чтение); пусто = выкл"), ECVF_Cheat);

// ★ Живое состояние эмиттера для диагностики — см. шапку в заголовке.
bool AAsdaEffectActor::GetEmitterRuntime(int32 EmitterIdx, bool& bOutStopped, float& OutLifeTimer,
	float* OutAlpha, float* OutHalfExtent, FLinearColor* OutColor) const
{
	if (!States.IsValidIndex(EmitterIdx)) { return false; }
	bOutStopped = States[EmitterIdx].bStopped;
	OutLifeTimer = States[EmitterIdx].LifeTimer;
	// ★ ЖИВЫЕ ЗНАЧЕНИЯ ТЕКУЩЕГО КАДРА — тем же `NodeAt`, что и построение геометрии (не второй
	//   формулой): «кольцо видно» доказывается АЛЬФОЙ и ПОЛУ-ЭКСТЕНТОМ, а не словом «замерло».
	if ((OutAlpha || OutHalfExtent || OutColor) && Emitters.IsValidIndex(EmitterIdx))
	{
		// ★ 08.10.2026 (ЦИКЛ 1:1, СРЕДН-5 ревизии fx_loop): та же выборка, что у сборки секции, — с маршем каналов
		//   после конца периода и зеркалом пинг-понга. Раньше здесь был голый `NodeAt(LifeTimer)`: у `Hold` 0x0B
		//   радиус света стоял по КОНЦУ при марширующих полу-экстентах, у `WrapMarch` (луки, баллисты) альфа и
		//   цвет света не шли за картинкой. На прежнем пути (`asda.Fx.Loop 0`) — бит-в-бит как было.
		FAsdaFxNode Cur;
		SampleEmitterNode(EmitterIdx, Cur);
		// ★ ЦИКЛ-3: канал альфы этих типов у эталона не бывает > 1 (обрезка пишется в канал) — свет видит то же
		if (OutAlpha)      { *OutAlpha = (CVarAsdaFxLoop.GetValueOnGameThread() != 0 && IsAlphaClampKind(Emitters[EmitterIdx])) ? FMath::Min(Cur.Alpha, 1.f) : Cur.Alpha; }
		if (OutHalfExtent) { *OutHalfExtent = FMath::Max(Cur.SizeX, Cur.SizeY); }
		// ★ Фѐ2: цвет узла того же кадра — из него эффект-свет берёт свой оттенок
		if (OutColor)      { *OutColor = Cur.Color; }
	}
	return true;
}

void AAsdaEffectActor::NodeAt(const FAsdaFxEmitter& E, float T, FAsdaFxNode& Out) const
{
	// Кусочно-линейная интерполяция track-узлов (как Update_Track: param += dt*rate между узлами ==
	// линейный марш set-значений). Дефолт: 2 узла birth(t=0)/death(t=Period) — так строит рантайм при count=0.
	if (E.Nodes.Num() == 0) { Out = FAsdaFxNode(); return; }
	if (E.Nodes.Num() == 1 || T <= E.Nodes[0].Time) { Out = E.Nodes[0]; return; }
	for (int32 i = 1; i < E.Nodes.Num(); ++i)
	{
		const FAsdaFxNode& A = E.Nodes[i - 1]; const FAsdaFxNode& B = E.Nodes[i];
		if (T <= B.Time || i == E.Nodes.Num() - 1)
		{
			const float Den = FMath::Max(B.Time - A.Time, 1e-4f);
			const float K = FMath::Clamp((T - A.Time) / Den, 0.f, 1.f);
			Out.Time = T;
			Out.Rate      = FMath::Lerp(A.Rate, B.Rate, K);
			Out.SizeX     = FMath::Lerp(A.SizeX, B.SizeX, K);
			Out.SizeY     = FMath::Lerp(A.SizeY, B.SizeY, K);
			Out.Speed     = FMath::Lerp(A.Speed, B.Speed, K);
			Out.ConeAngle = FMath::Lerp(A.ConeAngle, B.ConeAngle, K);
			Out.Alpha     = FMath::Lerp(A.Alpha, B.Alpha, K);
			Out.Color     = FMath::Lerp(A.Color, B.Color, K);
			Out.Gravity   = FMath::Lerp(A.Gravity, B.Gravity, K);
			Out.ConeAxis  = FMath::Lerp(A.ConeAxis, B.ConeAxis, K);
			Out.RingScale = FMath::Lerp(A.RingScale, B.RingScale, K);
			Out.RingRoll  = FMath::Lerp(A.RingRoll, B.RingRoll, K);
			Out.RingRot   = FMath::Lerp(A.RingRot, B.RingRot, K);
			Out.RingH     = FMath::Lerp(A.RingH, B.RingH, K);
			Out.RingTopR  = FMath::Lerp(A.RingTopR, B.RingTopR, K);
			Out.RingBotR  = FMath::Lerp(A.RingBotR, B.RingBotR, K);
			Out.RingU     = FMath::Lerp(A.RingU, B.RingU, K);
			Out.RingV     = FMath::Lerp(A.RingV, B.RingV, K);
			Out.RingBasePos = FMath::Lerp(A.RingBasePos, B.RingBasePos, K);
			Out.RingPosOfs  = FMath::Lerp(A.RingPosOfs, B.RingPosOfs, K);
			return;
		}
	}
	Out = E.Nodes.Last();
}

void AAsdaEffectActor::NextNodeAfter(const FAsdaFxEmitter& E, float T, FAsdaFxNode& Out) const
{
	// ★ Эталон 02_SpawnParticle.c:115 (CONFIRMED аудитом 2026-07-27): march-цель частицы = значения
	//   СЛЕДУЮЩЕГО узла трека, rate=(next−cur)/life. БЕЗ обёртки периода: прежний Fmod(LifeTimer+Life)
	//   у частиц конца периода давал цель из НАЧАЛА трека → rate≈0 → исчезновение «хлопком» без затухания.
	if (E.Nodes.Num() == 0) { Out = FAsdaFxNode(); return; }
	for (const FAsdaFxNode& N : E.Nodes)
	{
		if (N.Time > T + KINDA_SMALL_NUMBER) { Out = N; return; }
	}
	Out = E.Nodes.Last();
}

// ★★★ ИНДЕКС СЕГМЕНТА ТРЕКА = эталонный `trackFrameIdx` (`CEmitter_Update_Track@0x1204DA0`).
//   Сегмент [i, i+1] — тот, внутри которого лежит T. Индекс клампится в [0, N−2], и это НЕ
//   защитный кламп, а воспроизведение эталона: ветка снапа/инкремента там живёт под условием
//   `lifeTimer <= lifetime`, а время ПОСЛЕДНЕГО узла РАВНО lifetime ⇒ `trackFrameIdx` физически
//   не может дорасти до последнего узла. На конце периода эмиттер стоит на ПРЕДпоследнем узле,
//   а целью марша частицы остаётся последний. Именно поэтому эталонный «фриз» конца НЕ рождает
//   частицы с death-альфой 0 — грабля порта 2026-07-28 («дым стал одноразовым») была следствием
//   сэмплирования трека по абсолютному времени, а не самого фриза.
int32 AAsdaEffectActor::SegIndexAt(const FAsdaFxEmitter& E, float T) const
{
	const int32 N = E.Nodes.Num();
	if (N < 2) { return 0; }
	int32 Seg = 0;
	for (int32 i = 1; i < N; ++i)
	{
		if (T >= E.Nodes[i].Time) { Seg = i; } else { break; }
	}
	return FMath::Clamp(Seg, 0, N - 2);
}

void AAsdaEffectActor::SpawnParticle(const FAsdaFxEmitter& E, FAsdaFxEmitterState& S, const FAsdaFxNode& Cur)
{
	// CEmitter_SpawnParticle: все rand = rand_pm (ТРЕУГОЛЬНОЕ [-1,1]); march-дельты: цель = значения
	// СЛЕДУЮЩЕГО узла (движок пишет (next-v0)/life при спавне, 02_SpawnParticle.c:115).
	FAsdaFxParticle* Slot = nullptr;
	for (FAsdaFxParticle& P : S.Slots) if (P.Age < 0.f) { Slot = &P; break; }
	if (!Slot) return;                                  // все 100 заняты — как движок
	FAsdaFxParticle& P = *Slot;
	P.Age = 0.f;
	P.Life = FMath::Max(0.05f, E.LifeBase + RandPM() * E.LifeVar);
	// ★ mode2 (03_SpawnParticle_Mode2, аудит): жизнь клампится [0.1,10]
	if (E.SpawnMode == 2) { P.Life = FMath::Clamp(E.LifeBase + RandPM() * E.LifeVar, 0.1f, 10.f); }

	FAsdaFxNode Death; NextNodeAfter(E, S.LifeTimer, Death);

	// ★★★ [B] СПАВН-ЗНАЧЕНИЯ ЧАСТИЦЫ — ИЗ УЗЛА СЕГМЕНТА, А НЕ ИЗ ЛЕРПА ПО ВРЕМЕНИ.
	//   Эталон `CEmitter_SpawnParticle@0x12062A0`: `birthAttr = node[trackFrameIdx]` (значения УЗЛА),
	//   `deathAttr = node[trackFrameIdx+1]`, марш = (death−birth)/life. Внутри сегмента спавн-значения
	//   ПОСТОЯННЫ — у 2-узлового трека (подавляющее большинство 0x0D) они постоянны ВЕСЬ период,
	//   поэтому на стыке периода в оригинале меняться нечему. Порт же брал `Cur = NodeAt(LifeTimer)`
	//   ⇒ альфа/размер/цвет плавно уезжали к death и РАЗОМ прыгали к birth — «переход к первому кадру».
	//   Скорость/угол конуса/ось/spawnRate остаются на `Cur`: в эталоне это МАРШЕВЫЕ поля эмиттера
	//   (`emitVelHistA[2]`, `sizeRand`, `spawnRate`), их непрерывность верна.
	//   Ключ `asda.Fx.SpawnFromNode 0` возвращает прежний лерп.
	const bool bSegSpawn = CVarAsdaFxSpawnFromNode.GetValueOnGameThread() != 0 && E.Nodes.Num() >= 2;
	FAsdaFxNode SegBirth;
	if (bSegSpawn)
	{
		const int32 Seg = SegIndexAt(E, S.LifeTimer);
		SegBirth = E.Nodes[Seg];
		Death    = E.Nodes[Seg + 1];
	}

	// ★ 0x17 (grid-частицы «блёстки» крыльев) — ЭТАЛОН jemParticle_SpawnInit@0x1242880 + лоадер
	//   effectEmitterLoadFromFile_Type23@0x117AB00 (SINGLE-запись 0x1DC, БЕЗ track-узлов):
	//   size/color/alpha/gravity частицы при СПАВНЕ = ВСЕГДА дисковые START(+rand·VAR) (+0x4A4/+0x4C8/
	//   +0x4BC/+0x498), марш к END идёт ПЕР-ЧАСТИЧНО за её жизнь (rate=(END−START)/life). На уровне
	//   эмиттера за период маршируют ТОЛЬКО speed/coneAngle/повороты/дрейф/spawnRate
	//   (jemParticleSystemUpdate@0x1243500: sizeRand += dt·sizeRandVel и т.п.).
	//   Порт строил 0x17 псевдо-узлами birth/death и брал спавн-значения ИНТЕРПОЛЯЦИЕЙ по LifeTimer
	//   (модель 0xD) ⇒ к концу 5с-периода частицы рождались размером ~0 и белыми — «ОЧЕНЬ МАЛЕНЬКИЕ».
	//   Гейт по JebType==0x17 (только крылья): 0xD/треки не трогаем — их модель CONFIRMED дымами.
	const bool bT17 = (E.JebType == 0x17) && E.Nodes.Num() > 0;
	const FAsdaFxNode& Sp = bT17 ? E.Nodes[0] : (bSegSpawn ? SegBirth : Cur);

	const FTransform Xf = GetActorTransform();
	// ★ attached map-эффект (дым дирижаблей). Реверс CEffectMgr_scaleAll@0x114D7C0 + effectUpdateAttachedTargetPos@
	//   0x9954F0: attached-эффект наследует ТОЛЬКО ПОЗИЦИЮ кости (не ориентацию/scale). Масштаб s (=actorScale) идёт в
	//   матрицу+0x208 (→скорость ×s, геометрия кольца/квада ×s), но НЕ в РАЗМЕР частицы и НЕ в posBase-смещение.
	//   Порт же гнал ЖИВОЙ наклон+scale кости через Xf → дым наклонялся с дирижаблем; posBase ×s раздувал разброс.
	//   ★ ИСКЛЮЧЕНИЕ (2026-07-30): эффекты НА ПЕРСОНАЖЕ (bFollowBoneFull, attach-10) живут в ЧИСТОМ
	//   единичном трансформе кости — там правило «только позиция» неверно: эталон эмитит частицы из
	//   мировой позиции эмиттера (= нода + локальный pos-канал ЧЕРЕЗ матрицу ноды) и вращает ось конуса
	//   той же матрицей. Без этого блёстки крыла сыпались из точки кости в мировых осях (мимо крыла).
	//   (bFollowCharWorld = attach-6, шлейф транспорта — та же логика: чистый трансформ чара, posBase через него)
	const bool bAttachedMap = !bBaseAlreadyUE && !bFollowBoneFull && !bFollowCharWorld &&
		(FollowActor != nullptr || (GetRootComponent() && GetRootComponent()->GetAttachParent() != nullptr));   // attached = маркер follow_actor ИЛИ парентинг
	const float ScaleMag = FMath::Abs(GetActorScale3D().X);
	// позиция: центр эмиттера + локальный разброс PosBase±PosRange (повёрнутый трансформом)
	// ★ При bEmitterMotion точку эмиссии даёт ЖИВОЕ состояние эмиттера (марш posBase→posTarget +
	//   канальные вращения) — ровно как в эталоне, где разброс posRange разыгрывается ОДИН раз на
	//   старте эмиттера, а не на каждую частицу (`CEmitter_InitReset`). Без флага — прежняя ветка.
	FVector Local(P.Pos);
	if (E.bEmitterMotion)
	{
		Local = EmitterLocalPos(E, S, HolderYawStart + HolderYawSpeed * ElapsedTime);
	}
	else
	{
		Local.X = E.PosBase.X + RandPM() * E.PosRange.X;
		Local.Y = E.PosBase.Y + RandPM() * E.PosRange.Y;
		Local.Z = E.PosBase.Z + RandPM() * E.PosRange.Z;
	}
	// ★ 0x17 на персонаже (bFollowBoneFull): эталон вращает локальный pos-канал и ось конуса ТОЛЬКО
	//   собственными rot-каналами эмиттера (у крыльев нули) × RotY(yaw эффекта = charDir при спавне) —
	//   jemParticleSystemUpdate@0x1243500: worldPos = effectPos + M(rotКаналы·RotY(+82))·posLocal.
	//   Матрица КОСТИ на офсеты частиц НЕ действует (в отличие от меша 0x12!). Порт гнал офсеты через
	//   ПОЛНУЮ матрицу кости M (Y-up бинд-поворот) ⇒ «вверх» (0,10,0) уезжал в бок/вперёд — блёстки
	//   рождались ВНУТРИ тела, а у 3-эмиттерных крыльев (big/mid/ms: posBase ±40..100 вбок) боковые
	//   офсеты уезжали не к крыльям. Yaw носителя = живой yaw актора-персонажа (FollowActor).
	const bool bT17CharFrame = bT17 && bFollowBoneFull && FollowActor != nullptr;
	const FQuat CharYawQ = bT17CharFrame
		? FRotator(0.f, FollowActor->GetActorRotation().Yaw, 0.f).Quaternion()
		: FQuat::Identity;
	// attached: центр = позиция кости (Xf.location), posBase-смещение БЕЗ scale/наклона (мир); иначе полный трансформ
	P.Pos = bAttachedMap ? (Xf.GetLocation() + Local)
	     : bT17CharFrame ? (Xf.GetLocation() + CharYawQ.RotateVector(Local))
	     : Xf.TransformPosition(Local);
	// ★ 15.10.2026: трансформ рождения — для облака «в кадре носителя» у света эффекта (RestVerts); картинку не трогает.
	P.BirthLoc = Xf.GetLocation();
	P.BirthRot = Xf.GetRotation();
	// ★ Спавн-объём моды 5 (КРУГ 3, live cdb-захват wingcap_original.log + дизасм моды-5 @0x1213340):
	//   точка эмиссии = база + rand_pm(треугольный)·R по КАЖДОЙ МИРОВОЙ оси (куб ±R, новая точка каждый
	//   кадр; замер оригинала: разброс ±35 типичный/±50 макс при R=50 — облако вокруг персонажа =
	//   «блёстки у крыльев»). Дефолт SpawnPosMode=0 — прочие эмиттеры БЕЗ изменений.
	if (E.SpawnPosMode == 5)
	{
		P.Pos += FVector(RandPM(), RandPM(), RandPM()) * E.SpawnPosRadius;
	}

	{
		// ★ КОНУС ДЛЯ ВСЕХ РЕЖИМОВ (аудит 2026-07-27: 03_SpawnParticle_Mode2 = ТОТ ЖЕ конус, НЕ покомпонентный
		//   velBase-вектор — прежняя mode2-ветка порта была неверна). Азимут 2π, отклонение ±ConeAngle° от оси.
		const float Az = RandPM() * PI;                             // rand_pm*2π/2 → полный круг с треуг.распр.
		const float Cone = FMath::DegreesToRadians(RandPM() * Cur.ConeAngle);
		FVector Axis = Cur.ConeAxis; const float AxisLen = FMath::Max(Axis.Size(), 1e-4f);
		Axis /= AxisLen;
		// ортобазис вокруг оси
		FVector T1, T2; Axis.FindBestAxisVectors(T1, T2);
		FVector Dir = Axis * FMath::Cos(Cone) + (T1 * FMath::Cos(Az * 2.f) + T2 * FMath::Sin(Az * 2.f)) * FMath::Sin(Cone);
		const float Speed = Cur.Speed + RandPM() * E.SpeedVar;
		// ★ Скорость attached: ×ScaleMag (авторская матрица содержит масштаб ~58 — TransformCoord движка).
		//   Дирижабль ВИСИТ (анимация = вертикальное покачивание) → хвост-шлейф = скорость струи по СТАТИЧНОЙ
		//   дисковой оси (thetaSpread, запечена авторской матрицей); 20×58=1160 u/s × жизнь 5с = длинный плюмаж.
		P.Vel = bAttachedMap
			? Dir * AxisLen * Speed * ScaleMag
			: bT17CharFrame
				? CharYawQ.RotateVector(Dir * AxisLen * Speed)   // ★ 0x17: ось конуса БЕЗ матрицы кости (см. выше)
				: Xf.TransformVector(Dir * AxisLen * Speed);
	}

	// цвет: VAR — ПОЛОЖИТЕЛЬНЫЙ рандом (ResetVar2: cur = Min + rand01×Var, НЕ ±): выхлоп START=(1,1,1) VAR=(1,1,1)
	// → кламп 1.0 = ЧИСТО БЕЛЫЙ (rand_pm давал серые частицы — юзер: «серый, а должен быть белый»).
	// ★ Sp = START-узел для 0x17 (пер-частичные каналы, эталон SpawnInit), иначе Cur (маршевые 0xD-треки).
	P.RGB.X = FMath::Clamp(Sp.Color.R + FMath::FRand() * E.ColorVar.X, 0.f, 1.f);
	P.RGB.Y = FMath::Clamp(Sp.Color.G + FMath::FRand() * E.ColorVar.Y, 0.f, 1.f);
	P.RGB.Z = FMath::Clamp(Sp.Color.B + FMath::FRand() * E.ColorVar.Z, 0.f, 1.f);
	P.RGBRate = (FVector(Death.Color.R, Death.Color.G, Death.Color.B) - P.RGB) / P.Life;
	P.Alpha = FMath::Clamp(Sp.Alpha + RandPM() * E.AlphaVar, 0.f, 1.f);
	P.AlphaRate = (Death.Alpha - P.Alpha) / P.Life;
	P.SX = Sp.SizeX + RandPM() * E.SizeXVar;  P.SXRate = (Death.SizeX - P.SX) / P.Life;
	P.SY = Sp.SizeY + RandPM() * E.SizeYVar;  P.SYRate = (Death.SizeY - P.SY) / P.Life;
	P.Grav = (Sp.Gravity + RandPM() * E.GravityVar) * 9.8f;
	P.GravRate = (Death.Gravity * 9.8f - P.Grav) / P.Life;
}

void AAsdaEffectActor::SampleView(FAsdaFxViewSample& Out) const
{
	Out = FAsdaFxViewSample();
	// ★ Явно заданная камера превью-сцены (редакторские панели) имеет ПРИОРИТЕТ: свой FPreviewScene
	//   движку неизвестен, а вьюпорт УРОВНЯ смотрит совсем в другое место (кулл + ориентация квадов).
	//   Точка И поворот — из оверрайда (прежде: GetViewLocation и ветка bUseViewOverride в BuildQuads).
	if (bUseViewOverride)
	{
		Out.Location = ViewLocationOverride;
		Out.bHasLocation = true;
		Out.Rotation = ViewRotationOverride;
		Out.bHasRotation = true;
		return;
	}
	// ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя — AsdaView (правило и перечень прежних копий — AsdaView.h).
	//   ★ ИСПРАВЛЕНИЕ E7: своя копия брала камеру игрока, иначе ПЕРВЫЙ перспективный вьюпорт уровня БЕЗ проверки мира.
	//   Последствия: (1) превью-сцена без оверрайда отсекалась от вьюпорта ЧУЖОГО мира — теперь от своего отрисованного
	//   вида; (2) игровой мир без контроллера (первые кадры PIE) отсекался от вьюпорта редактора (другой мир) — теперь от
	//   павна / отрисованного вида PIE, иначе кулла нет; (3) свет эффектов (UAsdaFxLightSubsystem) зовёт ТУ ЖЕ функцию —
	//   дистанцию геометрии и света считают от одной точки. В игре с камерой и в окне редактора уровня — без изменений.
	// ★ 15.09.2026: один вызов AsdaView::GetViewPoint даёт и точку (≡ AsdaView::GetViewLocation — это её обёртка),
	//   и поворот (прежде BuildQuads звал GetViewPoint отдельно). Поворот есть только у камеры и вьюпорта.
	FAsdaViewPoint VP;
	if (AsdaView::GetViewPoint(GetWorld(), VP))
	{
		Out.Location = VP.Location;
		Out.bHasLocation = true;
		if (VP.bHasRotation)
		{
			Out.Rotation = VP.Rotation;
			Out.bHasRotation = true;
		}
	}
}

bool AAsdaEffectActor::GetViewLocation(FVector& Out) const
{
	FAsdaFxViewSample VS;
	SampleView(VS);
	if (!VS.bHasLocation) { return false; }   // точки нет — Out не трогается, как у прежней функции
	Out = VS.Location;
	return true;
}

// ★★★ [ПОРТ] Носитель эффекта под GM-скрытием? Смотрим цепочку родителей по attach И носителя слежения
//   (эффекты канала состояний attach-3/5/9/12 к персонажу НЕ приаттачены — они лишь следуют за его
//   позицией через FollowActor). Носитель «скрыт», если он сам спрятан и помечен меткой
//   `kAsdaGmHiddenTagName` — либо спрятан и его ВЛАДЕЛЕЦ помечен (хранитель души: отдельный актор,
//   Owner = персонаж). Свой персонаж оператора под скрытием полупрозрачен и НЕ спрятан — его эффекты
//   остаются видимыми самому оператору, как требовал владелец (asda.Gm.HideTranslucent).
//   ★ 24.10.2026: «скрыт» — ЛЮБОЙ системой: GM-скрытие или невидимость навыка (`AsdaHostHiddenByTag`, обе метки).
static bool AsdaFxHostIsGmHidden(const AActor* Host)
{
	for (int32 Depth = 0; Host && Depth < 8; ++Depth, Host = Host->GetAttachParentActor())
	{
		if (!Host->IsHidden()) { continue; }
		if (AsdaHostHiddenByTag(Host)) { return true; }
		if (const AActor* Own = Host->GetOwner()) { if (AsdaHostHiddenByTag(Own)) { return true; } }
	}
	return false;
}

#if !UE_BUILD_SHIPPING
// ★ 15.10.2026: счётчики бенча (объявление — AsdaEffectActor.h). Один поток (игровой) — атомики не нужны.
static uint64 GAsdaFxBenchTickCycles = 0, GAsdaFxBenchTicks = 0, GAsdaFxBenchSteps = 0;
struct FAsdaFxBenchTickScope
{
	uint64 T0 = FPlatformTime::Cycles64();
	~FAsdaFxBenchTickScope() { GAsdaFxBenchTickCycles += FPlatformTime::Cycles64() - T0; ++GAsdaFxBenchTicks; }
};
#endif // !UE_BUILD_SHIPPING

void AsdaFxBenchCounters(uint64& OutTickCycles, uint64& OutTicks, uint64& OutSteps)
{
#if !UE_BUILD_SHIPPING
	OutTickCycles = GAsdaFxBenchTickCycles; OutTicks = GAsdaFxBenchTicks; OutSteps = GAsdaFxBenchSteps;
#else
	OutTickCycles = 0; OutTicks = 0; OutSteps = 0;
#endif
}

// ★ R5-fix5 (13.11.2026) [ОРИГИНАЛ]: ключ гашения следящего эффекта на умершем мобе (разбор — AsdaFxDeadHost.h).
static ASDA_DEV_CVAR(int32, CVarAsdaFxDieWithDeadMob,
	TEXT("asda.Fx.DieWithDeadMob"), 1,
	TEXT("[ОРИГИНАЛ 13.11.2026] 1 = следящий эффект, рождённый не веткой удара (эффект состояния игрока на его цели-мобе — состояние 296; ")
	TEXT("записи фазы каста навыков 756 и 908 на цели-мобе), гаснет, когда моб-носитель умер, как эталон (обработчики привязки 3/9/12/13: ")
	TEXT("состояние носителя 10 → эффект удаляется); 0 = прежнее: эффект живёт над трупом до своего срока. Эффекты ветки удара не затронуты."),
	ECVF_Default);

bool AsdaFxDeadHost::IsOn()
{
	return CVarAsdaFxDieWithDeadMob.GetValueOnGameThread() != 0;
}

bool AsdaFxDeadHost::ShouldDie(const AActor* Fx, const AActor* Host)
{
	if (!Fx || !Host || !IsOn() || !Fx->Tags.Contains(Tag())) { return false; }
	const AAsdaMobActor* Mob = Cast<AAsdaMobActor>(Host);
	return Mob && Mob->IsDead();
}

void AAsdaEffectActor::Tick(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
	FAsdaFxBenchTickScope BenchScope;   // ★ 15.10.2026: цена Tick для `asda.Mount.FxBench`
#endif
	Super::Tick(DeltaSeconds);
	// ★★★ [ПОРТ] GM-СКРЫТИЕ НОСИТЕЛЯ (доводка P39, 27.09.2026): эффект, появившийся на скрытом персонаже
	//   после момента скрытия, гасит себя сам; при показе носителя — возвращается (только если гасили мы:
	//   собственная метка, чтобы не «проявить» эффект, спрятанный другой системой).
	{
		static const FName kSelfTag(TEXT("AsdaGmHiddenByHost"));
		const AActor* FollowHost = FollowActor.Get();
		if (!FollowHost && FollowComponent) { FollowHost = FollowComponent->GetOwner(); }
		const bool bHostHidden = AsdaFxHostIsGmHidden(GetAttachParentActor()) || AsdaFxHostIsGmHidden(FollowHost);
		if (bHostHidden)
		{
			if (!IsHidden()) { SetActorHiddenInGame(true); Tags.AddUnique(kSelfTag); }
		}
		else if (Tags.Contains(kSelfTag))
		{
			Tags.Remove(kSelfTag);
			SetActorHiddenInGame(false);
		}
	}
	// ★ Порядок тика для эффектов на персонаже: носитель (FollowSkelComponent) назначается спавнером
	//   ПОСЛЕ SpawnActor, а cvar можно щёлкнуть на живой сцене — поэтому сверяем каждый кадр (сравнение
	//   указателей, выход сразу). У эффектов без bFollowTickAfterPose — no-op.
	if (bFollowTickAfterPose) { ApplyFollowTickOrder(); }
	if (Emitters.Num() == 0) return;
	if (States.Num() != Emitters.Num()) ResetStates();

	// ★ type-2 слежение БЕЗ парентинга (движок S5: SetPos позиции цели каждый кадр; ориентация/масштаб авторские).
	//   Парентинг к зеркальному скелету (scale.X<0) ломал FTransform-relative (масштаб −100→1, поворот перекручен).
	// ★ БЕЗ пер-тик слежения: трансформ выставлен ОДИН раз при импорте (позиция кости + boneW-ориентация),
	//   рантайм его НЕ трогает → эффект свободно двигается руками в редакторе. (Носители alpen висят на месте,
	//   покачивание кости незаметно; если нужно живое следование за движущимся носителем — включить опционально
	//   через bFollowPosition в редакторе, дефолт выключен.)
	// ★ attach-6 (шлейф транспорта): нода = МИРОВАЯ МАТРИЦА ПЕРСОНАЖА, без кости.
	//   Та же цепочка, что у attach-10, но с BoneCS = единицей. МАТЕМАТИКА — в ComputeFollowTargetWorld (Д3).
	if (bFollowPosition && bFollowCharWorld && FollowComponent)
	{
		FVector Loc; FQuat Q;
		if (ComputeFollowTargetWorld(Loc, Q))
		{
			SetActorLocationAndRotation(Loc + Q.RotateVector(FollowOffset), Q);
		}
	}
	// ★ attach-3/5/9/12 (канал состояний): эталон делает РОВНО `CEffect_SetPos(node+8, поз. носителя)`
	//   — позиция мировая, ориентация ноды НЕ трогается. Носитель умер/удалён ⇒ эталон помечает ноду
	//   `dead` (`*(node+1) = 1`); у нас это гашение актора (эффект не должен висеть над пустотой).
	else if (bFollowPosition && bFollowCharPos)
	{
		if (const AActor* Host = FollowActor.Get())
		{
			// ★ R5-fix5 (13.11.2026) [ОРИГИНАЛ]: помеченный эффект (не ветка удара) гаснет, когда носитель-моб умер — эталон
			//   `[120] == 10 → *(node+1) = 1` у обработчиков 3/9/12/13. Разбор и ключ `asda.Fx.DieWithDeadMob` — AsdaFxDeadHost.h.
			if (AsdaFxDeadHost::ShouldDie(this, Host))
			{
				Destroy();
				return;
			}
			SetActorLocation(Host->GetActorLocation() + FollowOffset);
		}
		else
		{
			Destroy();
			return;
		}
	}
	else if (bFollowPosition && FollowActor)
	{
		// ★ Носитель: ЯВНО заданный компонент (FollowSkelComponent) имеет приоритет; nullptr = как было
		//   (FindComponentByClass). У персонажа скелетных компонентов много (BaseSkel + Equip_* LeaderPose
		//   + MountMesh + ClanFlagMesh), порядок обхода OwnedComponents (TSet) не определён — эффект мог
		//   читать позу НЕ того компонента, к которому прикреплён декорируемый меш (оружие → BaseSkel).
		const USkeletalMeshComponent* FSk = FollowSkelComponent
			? ToRawPtr(FollowSkelComponent)
			: FollowActor->FindComponentByClass<USkeletalMeshComponent>();
		if (FSk)
		{
			// ★ ПОЛНЫЙ трансформ кости 1:1 эталон attach-10 (effectAttachToCharSlot@0xF58F10:
			//   EffectWorld = boneLive × charWorld; ground truth live.dmp §H). МАТРИЦА строится ЕДИНОЙ
			//   точкой ComputeFollowBoneMatrix/ComputeFollowTargetWorld (Д3) — ею же пользуются зонды
			//   верификации, чтобы копии в верификаторах не «мерили не то».
			//   ПОЧЕМУ НЕ GetSocketTransform: FTransform-композиция при отриц. скейле канонизирует
			//   зеркало (SetFromMatrix) — socket-кватернион НЕ представляет матрицу кости, ориентация
			//   мусорная → «у кого в ногах, у кого поперёк».
			if (bFollowBoneFull)
			{
				FVector Loc; FQuat Q;
				if (ComputeFollowTargetWorld(Loc, Q))
				{
					// FollowOffset — в пространстве кости (локальная подстройка посадки)
					SetActorLocationAndRotation(Loc + Q.RotateVector(FollowOffset), Q);
				}
				else if (!bWarnedNoFollowBone)
				{
					bWarnedNoFollowBone = true;
					UE_LOG(LogTemp, Warning, TEXT("[Fx] bFollowBoneFull: кость '%s' НЕ найдена в скелете '%s' — эффект замер в точке спавна!"),
						*FollowBone.ToString(), *GetNameSafe(FSk->GetSkeletalMeshAsset()));
				}
			}
			else
			{
				if (FSk->DoesSocketExist(FollowBone))
				{
					SetActorLocation(FSk->GetSocketLocation(FollowBone) + FollowOffset);
					if (bFollowRotation)
					{
						const FQuat SockNow = FSk->GetSocketQuaternion(FollowBone);
						SetActorRotation((SockNow * FollowBindRot.Quaternion().Inverse()) * AuthoredRot.Quaternion());
					}
				}
				// ★ Устаревший yaw-костыль (не для крыльев — см. bFollowBoneFull).
				else if (bFollowActorYaw)
				{
					const float SkYaw = FSk->GetComponentRotation().Yaw;
					SetActorRotation(FRotator(0.f, SkYaw + AuthoredRot.Yaw, 0.f));
				}
			}
		}
	}

	// timeSync-гейт (type-2 one-shot, аудит №13): эффект стартует, когда anim-время родителя >= TimeSyncGate.
	// loopMode=3 (бессмертные карт-эффекты) имеют ts=0 → гейт пройден сразу.
	if (TimeSyncGate > 0.f && !bSyncPassed)
	{
		if (AActor* Par = FollowActor ? FollowActor.Get() : GetAttachParentActor())
			if (const USkeletalMeshComponent* SkC = Par->FindComponentByClass<USkeletalMeshComponent>())
			{
				const float PosMs = SkC->GetPosition() * 1000.f;   // anim-время в МС (как timeSync)
				if (bSyncWaitCross)
				{
					// ★★★ ЦИКЛ 1:1, холдер режима 2 (`sceneComposite_UpdateGridsAndEffects@0x11DD150`,
					//   render_core.c:49009–49023). ★ ПОПРАВЛЕНО 08.10.2026 (МЕЛК-3 ревизии fx_loop): условие
					//   эталона — «≥ порога» С ЗАЩЁЛКОЙ, а не пересечение: по концу контейнера защёлка `+1072 = 0`,
					//   затем при `защёлка == 0 && anim(+13) >= порог(+0x324)` — рестарт и защёлка 1. Если анимация
					//   уже выше порога, рестарт НЕМЕДЛЕННЫЙ. Защёлка = `bSyncWaitCross` (сбрасывается при рестарте).
					//   Сейчас не проявляется: у всех 2655 размещений карт `timeSync = 0`.
					SyncPrevPosMs = PosMs;
					if (PosMs >= TimeSyncGate) { bSyncPassed = true; bSyncWaitCross = false; }
				}
				else if (PosMs >= TimeSyncGate) { bSyncPassed = true; }
			}
		if (!bSyncPassed) { if (Mesh) Mesh->ClearAllMeshSections(); for (int32& sv : SectionVerts) sv = 0; return; }
	}

	// ★ 15.09.2026: снимок наблюдателя — ОДИН на шаг: им пользуются кулл ниже и сборка квадов ВСЕХ эмиттеров
	//   (поворот частиц, камера биллборда 0xC). Прежде сборка переспрашивала AsdaView на каждый эмиттер; между этой
	//   строкой и сборкой актор не трогает ни камеру/контроллер/павн, ни вьюпорты, ни отрисованные виды, ни оверрайд
	//   превью — выборка в прежний момент дала бы те же биты (разбор и стенд — wave3_asdaview.md, раздел 15.09).
	FAsdaFxViewSample ViewSample;
	SampleView(ViewSample);
	const bool bHaveView = ViewSample.bHasLocation;
	const FVector View = ViewSample.Location;
	// дистанц-кулл как движок (~9000-10000): далеко — не симулируем и чистим геометрию
	if (bHaveView && FVector::Dist(View, GetActorLocation()) > CullDistance)
	{
		for (int32 i = 0; i < Emitters.Num(); ++i)
			if (SectionVerts.IsValidIndex(i) && SectionVerts[i] != 0) ClearFxSection(i);
		return;
	}
	// ★ ВОЛНА CPU 07.09.2026 (cpu_visibility_report.md): гейт значимости поверх дистанц-кулла.
	//   SimulateStep — это CPU-симуляция частиц И пересборка секций UProceduralMeshComponent
	//   (UpdateMeshSection_LinearColor, TArray V/N/UV/Col + загрузка на поток рендера) КАЖДЫЙ кадр
	//   у всех эффектов в 100 м. Вне расширенного конуса камеры это чистая потеря.
	//     значимость 2 — как раньше, каждый кадр;
	//     значимость 1 — шаг через кадр, но с НАКОПЛЕННЫМ Dt: ElapsedTime/MeshAnimFrame идут ровно как
	//                    при полной частоте, поэтому при подходе к камере фаза не «прыгает»;
	//     значимость 0 — заморозка: Dt НЕ копится, симуляция стоит, геометрия НЕ чистится.
	//   Почему не чистим секции (в отличие от дистанц-кулла выше): очистка + повторная сборка на возврате —
	//   ровно та работа, которую мы убираем, а вне кадра геометрия всё равно отсекается рендером.
	//   Почему заморозка, а не «догнать время на возврате»: догон дал бы разовый скачок фазы (взрыв частиц
	//   на кадре появления). Замороженный эффект продолжает с того же места — это «пауза», глазом не ловится
	//   у карт-эффектов (loopMode=3, бесконечные циклы), а одноразовые каскады рядом с игроком всегда в зоне 2.
	//   Откат: asda.Vis.Effects 0 (или общий asda.Vis.Enable 0).
	float SimDt = DeltaSeconds;
	if (UAsdaVisibilitySubsystem::EffectsEnabled())
	{
		if (UAsdaVisibilitySubsystem* Vis = UAsdaVisibilitySubsystem::Get(this))
		{
			if (VisRadius < 0.f)
			{
				// Габарит эффекта — один раз: по границам актора, но не меньше 1000 юн (10 м).
				// Нижний предел важен для «плоских» эффектов: он даёт им угловой запас у края кадра.
				FVector Origin, Extent;
				GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
				VisRadius = FMath::Max((float)Extent.Size(), 1000.f);
			}
			const int32 Level = Vis->Evaluate(VisState, GetActorLocation(), VisRadius);
			// ★ 15.10.2026: эффект на движущемся носителе (см. CVarAsdaFxFollowFullRate в шапке файла).
			//   ★ Ревизия 15.10.2026 (review_PORT_mobpos_mountfx.md п. 4–5): полный темп и перенос — ТОЛЬКО пока носитель
			//   ДВИЖЕТСЯ (его кадр сдвинулся/повернулся с прошлого шага). Стоящий носитель (свечение оружия, крылья,
			//   баффы стоящего игрока) — прежняя экономия: через кадр на уровне 1, без переноса на уровне 0.
			const bool bCarrierFullRate = IsCarrierFollower() && CVarAsdaFxFollowFullRate.GetValueOnGameThread() != 0;
			if (Level == (int32)EAsdaVisLevel::Off)
			{
				VisPendingDt = 0.f;   // заморозка: время эффекта не идёт
				// Шага нет, но геометрия носителя не должна остаться висеть на месте заморозки (только если он сдвинулся).
				if (bCarrierFullRate) { CarryGeometryWithCarrier(); }
				return;
			}
			if (Level == (int32)EAsdaVisLevel::Far)
			{
				VisPendingDt += DeltaSeconds;
				// Шаг делаем, когда накопилось не меньше двух кадров (через кадр при стабильном fps);
				//   движущийся носитель — каждый кадр (иначе геометрия отстаёт на путь за кадр).
				const bool bMovingCarrier = bCarrierFullRate && CarrierMovedSince(CarrierXfAtStep);
				if (!bMovingCarrier && VisPendingDt < DeltaSeconds * 1.5f) { return; }
				SimDt = VisPendingDt;
				VisPendingDt = 0.f;
			}
			else
			{
				VisPendingDt = 0.f;
			}
		}
	}
	SimulateStep(FMath::Min(SimDt * TimeScale, 0.1f), bHaveView ? &View : nullptr, ViewSample);
#if !UE_BUILD_SHIPPING
	++GAsdaFxBenchSteps;
#endif
	// ★ 15.10.2026: с чем построена геометрия этого кадра (самотест `asda.Mount.FxSelfTest` и перенос вслед за носителем).
	GeomBuildFrame = GFrameCounter;
	GeomBuildXf = GetActorTransform();
	if (IsCarrierFollower()) { bHaveCarrierXfAtStep = GetCarrierFrameXf(CarrierXfAtStep); }
}

// ★ Ревизия 15.10.2026: кадр носителя — тот компонент, от которого слежение строит трансформ эффекта (явный скелет →
//   явный компонент → первый скелет носителя → корень носителя). Кость внутри скелета сюда не входит намеренно:
//   покачивание кости на месте (покой) — не «движение носителя», его экономия прежняя.
bool AAsdaEffectActor::GetCarrierFrameXf(FTransform& Out) const
{
	const USceneComponent* C = FollowSkelComponent ? static_cast<const USceneComponent*>(ToRawPtr(FollowSkelComponent))
		: (FollowComponent ? ToRawPtr(FollowComponent) : nullptr);
	if (!C && FollowActor)
	{
		C = FollowActor->FindComponentByClass<USkeletalMeshComponent>();
		if (!C) { C = FollowActor->GetRootComponent(); }
	}
	if (!C) { return false; }
	Out = C->GetComponentTransform();
	return true;
}

// ★ Ревизия 15.10.2026: сдвинулся ли кадр носителя относительно `Ref` (0.01 юн / 1e-4 рад). Нет кадра — «да» (как было).
bool AAsdaEffectActor::CarrierMovedSince(const FTransform& Ref) const
{
	FTransform Now;
	if (!bHaveCarrierXfAtStep || !GetCarrierFrameXf(Now)) { return true; }
	return FVector::DistSquared(Now.GetLocation(), Ref.GetLocation()) > 1.0e-4
		|| Now.GetRotation().AngularDistance(Ref.GetRotation()) > 1.0e-4;
}

// ★ 15.10.2026: перенос построенной мировой геометрии вслед за носителем в кадр без шага (см. объявление в .h).
//   Вершины секций построены от трансформа актора `GeomBuildXf`; компонент меша получает GeomBuildXf⁻¹ · текущий —
//   жёсткая геометрия узла (квады/кольца/меши) встаёт ровно туда, где её построил бы шаг этого кадра. Мировые частицы
//   едут вместе с ней только до ближайшего шага (уровень 0 значимости = эффект вне расширенного кадра).
void AAsdaEffectActor::CarryGeometryWithCarrier()
{
	if (!Mesh || GeomBuildFrame == 0) { return; }
	// ★ Ревизия 15.10.2026 (п. 5): носитель не сдвинулся с прошлого шага/переноса — компонент уже там, где надо.
	if (!CarrierMovedSince(bGeomCarried ? CarrierXfAtCarry : CarrierXfAtStep)) { return; }
	Mesh->SetWorldTransform(GeomBuildXf.Inverse() * GetActorTransform());
	bGeomCarried = true;
	GetCarrierFrameXf(CarrierXfAtCarry);
}

FTransform AAsdaEffectActor::GetGeomComponentXf() const
{
	return Mesh ? Mesh->GetComponentTransform() : FTransform::Identity;
}

void AAsdaEffectActor::SimulateStep(float Dt, const FVector* ViewLocPtr, const FAsdaFxViewSample& View)
{
	// ★ 15.10.2026: шаг строит вершины в МИРЕ заново — перенос вслед за носителем снимается (компонент снова identity).
	if (bGeomCarried && Mesh) { Mesh->SetWorldTransform(FTransform::Identity); bGeomCarried = false; }
	// ★ Ревизия 15.10.2026 (п. 6): облако «в кадре носителя» читает только свет эффекта — строим, только если свет есть
	//   и его правило включено (`asda.Fx.Light.CarrierAnchor`). Один поиск компонента на шаг, лишь у эффектов на носителе.
	bRestFrameWanted = IsCarrierFollower() && UAsdaFxLightComponent::IsCarrierAnchorOn()
		&& FindComponentByClass<UAsdaFxLightComponent>() != nullptr;
	if (Emitters.Num() == 0) return;
	if (States.Num() != Emitters.Num()) ResetStates();
	// ★★★ ЦИКЛ 1:1 (см. блок ключей `asda.Fx.Loop*` в шапке файла): данные DT_FxLoop — один раз и
	//   повторно при смене ключей; затем КОНТЕЙНЕР — `objAnimRotateUpdate@0x114E050`:
	//   `lifeTimer += dt`, по концу длительности switch(endMode контейнера):
	//     1   → vtbl[1] `effectParticleReset` — рестарт всего эффекта;
	//     0/2 → эффект окончен; на карте холдер `sceneComposite_UpdateGridsAndEffects@0x11DD150`
	//           поднимает его заново (режим 2 — «анимация ≥ порога» с защёлкой, у нас
	//           `TimeSyncGate`); вне карты гаснет ВЕСЬ эффект (ЦИКЛ-4, 08.10.2026) и больше не тикает;
	//     3   → вечный, ничего.
	const bool bLoopOn = CVarAsdaFxLoop.GetValueOnGameThread() != 0;
	{
		const int32 LKey = (bLoopOn ? 1 : 0) | ((bLoopOn && CVarAsdaFxLoopSeeds.GetValueOnGameThread() != 0) ? 2 : 0)
			| (CVarAsdaFxEnd0Freeze.GetValueOnGameThread() != 0 ? 4 : 0);
		if (LKey != LoopResolvedKey || LoopRt.Num() != Emitters.Num()) { ResolveLoopInfo(); }
	}
	if (bLoopOn && bLoopContainer && !bLoopContainerDone && LoopContainerEnd >= 0 && LoopContainerEnd <= 2)
	{
		LoopContainerTimer += Dt;
		if (LoopContainerTimer > LoopContainerDur)
		{
			const bool bEnded = (LoopContainerEnd == 0 || LoopContainerEnd == 2);
			if (LoopContainerEnd == 1 || (bEnded && bLoopHolder))
			{
				RestartLoopContainer();
				if (bEnded && TimeSyncGate > 0.f)
				{
					// холдер режима 2: защёлка снята — поднять, как только анимация носителя ≥ порога (Tick-гейт; 08.10 МЕЛК-3)
					bSyncPassed = false;
					bSyncWaitCross = true;
					SyncPrevPosMs = -1.f;   // прошлое anim-время — с чистого листа (только лог)
					return;
				}
			}
			else if (bEnded)
			{
				// ★★★ 08.10.2026 (ЦИКЛ-4, решение владельца): по концу контейнера 0/2 у эффекта НЕ на карте гаснет
				//   ВЕСЬ эффект, как у эталона. `objAnimRotateUpdate@0x114E050` case 0/2 → `sub_920F49` →
				//   `emitterRecordsResetTimers@0x114DC20` (E:77121–77147): `container.lifeTimer(+0x114) = −1.0`,
				//   у каждой записи `emitter+276 = −2.0`. Рендер `objListRenderInRange@0x114E470` (render_core.c:44171)
				//   рисует запись только при `container.lifeTimer >= rec.delay` (задержки ≥ 0) ⇒ с −1 не рисуется НИ ОДИН
				//   эмиттер любого типа — квады, кольца, меши и частицы (их живые слоты тоже). Сам контейнер дальше
				//   не тикает (`if (lifeTimer == −1) return 0`), поднять его может только холдер карты (у нас
				//   `bLoopHolder` → RestartLoopContainer выше) или новый спавн. Раньше [ПОРТ] гасил только застывшие
				//   эмиттеры, а Hold/Restart/марш (471 эмиттер с диском 1/2/3 в контейнерах 0/2) оставались видимыми
				//   до сноса актора владельцем.
				bLoopContainerDone = true;
				int32 NLive = 0;   // сколько эмиттеров было видимо и не застыло — их гасит именно эта правка (для лога)
				for (int32 k = 0; k < States.Num(); ++k)
				{
					FAsdaFxEmitterState& St = States[k];
					if (!St.bStopped && St.bStarted && !St.bFrozen) { ++NLive; }
					St.bStopped = true;
					for (FAsdaFxParticle& P : St.Slots) { P.Age = -1.f; }   // живые частицы тоже не рисуются (−2 → реинит)
					ClearFxSection(k);
				}
				if (NLive > 0)
				{
					UE_LOG(LogTemp, Log, TEXT("[петля] контейнер %s (%s) окончен (%.2f с, режим %d): погашен весь эффект, в т.ч. %d незастывших эмиттеров"),
						*EffectName, *GetName(), LoopContainerDur, LoopContainerEnd, NLive);
				}
			}
		}
	}
	// ★★★ ЦИКЛ-4: контейнер окончен, холдера нет — эталон эффект больше не обновляет и не рисует (см. выше).
	//   Ни один эмиттер не стартует по StartDelay и не рождает частиц, пока эффект не перезапустят
	//   (RestartEmitters / RestartLoopContainer / ResolveLoopInfo при смене ключей сбрасывают признак).
	if (bLoopOn && bLoopContainerDone && !bLoopHolder) { return; }
	// ★ Спин .jem-кристалла (глиф): накопление угла вокруг вертикали (эталон 6°/кадр ≈ ~180°/с; берём ~90°/с калм).
	if (bSpinMeshEmitters) { MeshSpinAngle += Dt * FMath::DegreesToRadians(90.f); if (MeshSpinAngle > 2.f*PI) MeshSpinAngle -= 2.f*PI; }
	// ★ animFrame .jem-скина: эталон += 1.0 за КАДР РЕНДЕРА (effects_particles.c:103512) → у нас Dt×Fps.
	MeshAnimFrame += Dt * FMath::Max(MeshAnimFps, 0.f);
	if (MeshAnimFrame > 1.0e6f) { MeshAnimFrame = 0.f; }
	ElapsedTime += Dt;                                  // holder elapsed (float[69]): спин/дрейф/StartDelay
	const float HolderAngle = HolderYawStart + HolderYawSpeed * ElapsedTime;   // град
	const FVector HolderOfs = HolderPosVel * ElapsedTime;                      // мировой дрейф

	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		const FAsdaFxEmitter& E = Emitters[i];
		FAsdaFxEmitterState& S = States[i];

		// StartDelay (rec+0x20): эмиттер не живёт, пока elapsed < delay (каскады звуковых колец!)
		if (!S.bStarted)
		{
			if (ElapsedTime < E.StartDelay)
			{
				ClearFxSection(i);
				continue;
			}
			S.bStarted = true; S.LifeTimer = 0.f;        // старт с нуля периода
			ResetEmitterMotion(E, S);                    // ★ марш точки эмиссии считается от старта
		}

		if (E.bMeshEmitter)
		{
			// ★ Мерцание co-planar пары (bFlickerAlt): слои с одинаковой геометрией видны ПОПЕРЕМЕННО —
			//   эмуляция оригинала (ZWRITE ON у мешей + нестабильная сортировка равных дистанций => кадровое
			//   чередование слоёв). Смена каждые 2 кадра; фаза по индексу эмиттера (пара в противофазе).
			if (E.bFlickerAlt && ((GFrameCounter / 2 + (uint64)i) & 1))
			{
				if (SectionVerts.IsValidIndex(i) && SectionVerts[i] != 0) { Mesh->ClearMeshSection(i); SectionVerts[i] = 0; }
				continue;
			}
			const FTransform CurXf = GetActorTransform();
			// ★ 0x12/0x11/0x1F с узлами-каналами (лоадер-трейс 0x1178530): W=Scale·RotX·RotY·RotZ·parent;
			//   трансляция += offsetWorld (сырой мир ×s) + parent; локальный pos вращается матрицей (добавка в локале).
			//   Узел: ConeAxis=scale3(game sx,sy,sz), RingRot=(rotX,rotY,rotZ game), RingPosOfs=posLocal, RingBasePos=offsetWorld.
			//   Оси game→UE: RotX(gameX)→RotUE_Y, RotY(gameY=up)→RotUE_Z, RotZ(gameZ)→RotUE_-X; scale UE=(sz,sx,sy).
			if (S.bStopped) { continue; }        // endMode 0 у меша: отыграл один раз и погашен
			if (E.Nodes.Num() >= 2 && E.MeshVerts.Num() >= 3 && SectionVerts.IsValidIndex(i))
			{
				S.LifeTimer += Dt;
				const float Period = FMath::Max(E.Period, 0.1f);
				// ★★★ endMode У МЕША ТЕПЕРЬ СОБЛЮДАЕТСЯ — обоснование и эталон см. у
				//   `CVarAsdaFxMeshEndMode` в шапке файла. У меша ЛУП дают 1/2/3, стоп — только 0.
				// ★★★ ЦИКЛ 1:1 у меша (`effectAnimTickKeyframe@0x1239250`): 0 = `age = −1` + снап КОНЦА — меш
				//   застывает видимым (Freeze) либо, у одноразового канала, гасится (Stop); 1 = реинит.
				const EAsdaFxLoopMode MeshLoop = (bLoopOn && LoopRt.IsValidIndex(i)) ? LoopRt[i].Mode : EAsdaFxLoopMode::Legacy;
				if (S.LifeTimer > Period)
				{
					if (MeshLoop == EAsdaFxLoopMode::Freeze)
					{
						S.bFrozen = true;
						S.LifeTimer = Period;
						// ★ 08.10.2026 (ревизия fx_parity3 МЕЛК-3): прежняя ветка «застыл после конца контейнера → погасить»
						//   (СРЕДН-1 ревизии fx_loop) убрана как НЕДОСТИЖИМАЯ: с ЦИКЛ-4 при `bLoopContainerDone && !bLoopHolder`
						//   шаг выходит раньше (ранний `return` после блока контейнера), а сам блок контейнера гасит ВСЕ эмиттеры.
					}
					else if (MeshLoop == EAsdaFxLoopMode::Hold || MeshLoop == EAsdaFxLoopMode::MarchAll)
					{
						S.OverTime += S.LifeTimer - Period;   // марш дальше конца (см. ApplyLoopChannels)
						S.LifeTimer = Period;
					}
					else if (MeshLoop == EAsdaFxLoopMode::SawFromEnd || MeshLoop == EAsdaFxLoopMode::PingPong)
					{
						S.WrapCount += FMath::Max(1, FMath::FloorToInt(S.LifeTimer / Period));
						S.LifeTimer = FMath::Fmod(S.LifeTimer, Period);
					}
					else if (MeshLoop == EAsdaFxLoopMode::Stop
						|| (MeshLoop == EAsdaFxLoopMode::Legacy && E.EndMode == 0 && CVarAsdaFxMeshEndMode.GetValueOnGameThread() != 0))
					{
						S.bStopped = true;
						S.LifeTimer = Period;
						ClearFxSection(i);
						continue;
					}
					else
					{
						S.LifeTimer = FMath::Fmod(S.LifeTimer, Period);
					}
				}
				// ★★★ ЦИКЛ 1:1 у меша: пинг-понг — зеркальная выборка на нечётном обороте; удержание/пила —
				//   марш каналов поверх узла (ApplyLoopChannels). На прежнем пути — как было.
				//   ★ 08.10.2026 (СРЕДН-5): одной функцией с квадами и светом эффекта (`SampleEmitterNode`).
				FAsdaFxNode Cur; SampleEmitterNode(i, Cur);
				const float DegR = PI/180.f;
				const float ax=Cur.ConeAxis.X, ay=Cur.ConeAxis.Y, az=Cur.ConeAxis.Z;   // game sx,sy,sz
				const FVector SclUE(az?az:1.f, ax?ax:1.f, ay?ay:1.f);
				const float cx=FMath::Cos(Cur.RingRot.X*DegR), sx2=FMath::Sin(Cur.RingRot.X*DegR);
				const float cy=FMath::Cos(Cur.RingRot.Y*DegR), sy2=FMath::Sin(Cur.RingRot.Y*DegR);
				const float cz2=FMath::Cos(-Cur.RingRot.Z*DegR), sz3=FMath::Sin(-Cur.RingRot.Z*DegR);
				// ★ ЗНАК ВРАЩЕНИЙ (фикс 2026-07-30, «крылья на груди»): каналы задают D3DX-матрицы
				//   RotationX/Y/Z в GAME-осях (row-vector, v'=v·M). Перевод в stored-UE = сопряжение
				//   P⁻¹·R·P, где P: game→ue = (−gz, gx, gy). Вывод (см. отчёт §H6):
				//     rotX: x' = x·c − z·s ; z' = x·s + z·c
				//     rotY: x' = x·c + y·s ; y' = −x·s + y·c
				//     rotZ: y' = y·c − z·s ; z' = y·s + z·c      (эта уже была верна)
				//   Прежние rotX/rotY были ТРАНСПОНИРОВАНЫ (знак наоборот) ⇒ у крыла rotY=90° давал
				//   разворот на 180° вокруг вертикали = крыло переезжало со спины НА ГРУДЬ.
				//   ★ ФЛАГ bStrictChannelRot: строгий знак применяется ТОЛЬКО там, где он сверен с дампом
				//   (крылья). Карты (фонари/радуга/дирижабли) калибровались под ИСТОРИЧЕСКИЙ знак —
				//   для них поведение остаётся прежним (иначе разворот на 180°, регрессия 2026-07-30).
				const bool bStrict = E.bStrictChannelRot;
				auto RotChain=[&](FVector P){
					if (bStrict)
					{
						P = FVector(P.X*cx - P.Z*sx2, P.Y, P.X*sx2 + P.Z*cx);         // D3DX RotX(game) в ue
						P = FVector(P.X*cy + P.Y*sy2, -P.X*sy2 + P.Y*cy, P.Z);        // D3DX RotY(game) в ue
					}
					else
					{
						P = FVector(P.X*cx + P.Z*sx2, P.Y, -P.X*sx2 + P.Z*cx);        // историч. RotUE_Y(rotX game)
						P = FVector(P.X*cy - P.Y*sy2, P.X*sy2 + P.Y*cy, P.Z);         // историч. RotUE_Z(rotY game)
					}
					P = FVector(P.X, P.Y*cz2 + P.Z*sz3, -P.Y*sz3 + P.Z*cz2);          // rotZ — общий (был верен)
					return P;
				};
				// ★ pos-канал идёт через ПОЛНУЮ W (эталон 0x1229810: d3dxTransformDispatch3f(matTilt,pos) —
				//   pos трансформируется Scale·Rot·parent; трансляция = offsetWorld + pos·W + parentPos).
				//   Ground truth live.dmp (крыло, §H): pos(0,2.5,0)·W = +25 мировых см вверх (2.5×scale 10) ✓.
				//   Раньше pos добавлялся сырым ПОСЛЕ RotChain (без Scale/Rot канала) — крыло на 22 см ниже.
				const FVector PosLocalUE = AsdaCoord::GameToUe3D(Cur.RingPosOfs);   // ★ Аудит №3 (B1)
				const float ScaleMag = FMath::Abs(CurXf.GetScale3D().X);
				const FVector OwUE = AsdaCoord::GameToUe3D(Cur.RingBasePos) * ScaleMag;   // ★ Аудит №3 (B1)
				// ★ FxAlphaScale — рантайм-множитель всего эффекта (GM-«скрыться»), дефолт 1 = данные как есть.
				const FLinearColor MC(Cur.Color.R, Cur.Color.G, Cur.Color.B, FMath::Clamp(Cur.Alpha * FxAlphaScale, 0.f, 1.f));
				TArray<FVector> V; TArray<int32> I; TArray<FVector> Nn; TArray<FVector2D> UV; TArray<FLinearColor> Col;
				V.Reserve(E.MeshVerts.Num());
				// ★ Скин .jem: кадр анимации = animFrame (марш 1/кадр рендера у эталона; у нас MeshAnimFps),
				//   заворот на длину трека. Кость вершины — из палитры (MeshVertBone).
				const int32 SkinBn = E.SkinBones;
				const int32 SkinNF = (SkinBn > 0) ? (E.SkinFrames.Num() / SkinBn) : 0;
				const int32 SkinF  = (SkinNF > 0) ? (((int32)MeshAnimFrame % SkinNF + SkinNF) % SkinNF) : 0;
				const bool  bSkin  = (SkinNF > 0) && (E.MeshVertBone.Num() == E.MeshVerts.Num());
				for (int32 k = 0; k < E.MeshVerts.Num(); ++k)
				{
					FVector L = E.MeshVerts[k];
					FVector N = E.MeshNormals.IsValidIndex(k) ? E.MeshNormals[k] : FVector::UpVector;
					if (bSkin)
					{
						const int32 Bn = E.MeshVertBone[k];
						if (Bn >= 0 && Bn < SkinBn)
						{
							const FTransform& SkT = E.SkinFrames[SkinF * SkinBn + Bn];
							L = SkT.TransformPosition(L);
							N = SkT.TransformVectorNoScale(N);
						}
					}
						// ★ ФЛАГ bStrictMeshPos (дефолт false = ИСТОРИЧЕСКОЕ поведение карт): при false pos-канал
						//   прибавляется СЫРЫМ ПОСЛЕ вращений — так были откалиброваны меш-эффекты карт;
						//   при true (крылья/транспорт, сверено с live.dmp §H3) pos идёт через ПОЛНУЮ W: (v+pos)·S·R.
						const FVector Ls = E.bStrictMeshPos
							? RotChain((L + PosLocalUE) * SclUE)
							: (RotChain(L * SclUE) + PosLocalUE);
					V.Add(CurXf.TransformPosition(Ls) + OwUE);
					Nn.Add(CurXf.TransformVectorNoScale(RotChain(N)));
					UV.Add(E.MeshUV0.IsValidIndex(k) ? E.MeshUV0[k] : FVector2D::ZeroVector);
					Col.Add(MC);
					I.Add(k);
				}
				FlipWindingIfMirrored(I, CurXf.GetScale3D());
				if (SectionVerts[i] == V.Num())
					AsdaFxUpdateSection(Mesh, i, V, Nn, UV, Col);
				else
				{
					AsdaFxCreateSection(Mesh, i, V, I, Nn, UV, Col);
					// ★ Цвет/альфа узла идут ВЕРШИННЫМ diffuse (эталон FVF 0x142) — мастера эффектов теперь
					//   умножают на VertexColor (asda_add_vertexcolor_effects.py); FadeIn-MID-обход удалён.
					if (E.Material) Mesh->SetMaterial(i, GlowMaterialFor(i, E.Material));   // ★ Ф5: через «силу свечения» (см. GlowMaterialFor)
					SectionVerts[i] = V.Num();
				}
				continue;
			}
			// .jem-меш без каналов (кристалл soul-well глифа char-select). База = ТРАНСФОРМ АКТОРА.
			//   Эталон jemEffect_UpdateModelEmitter@0xF11D60: спин вокруг вертикали (+6°/кадр game-Y=UE-Z).
			const bool bXfChanged = !SectionMeshXf.IsValidIndex(i) || !SectionMeshXf[i].Equals(CurXf, 0.01f);
			const bool bRebuild = bSpinMeshEmitters || SectionVerts[i] != E.MeshVerts.Num() || bXfChanged;
			if (SectionVerts.IsValidIndex(i) && bRebuild && E.MeshVerts.Num() >= 3)
			{
				if (!SectionMeshXf.IsValidIndex(i)) { SectionMeshXf.SetNum(SectionVerts.Num()); }
				SectionMeshXf[i] = CurXf;
				const FTransform Xf = CurXf;
				const float Ang = bSpinMeshEmitters ? MeshSpinAngle : 0.f;
				const float cz = FMath::Cos(Ang), sz = FMath::Sin(Ang);
				TArray<FVector> V; TArray<int32> I; TArray<FVector> Nn; TArray<FVector2D> UV; TArray<FLinearColor> Col;
				V.Reserve(E.MeshVerts.Num());
				for (int32 k = 0; k < E.MeshVerts.Num(); ++k)
				{
					const FVector& L = E.MeshVerts[k];
					const FVector Ls(L.X * cz - L.Y * sz, L.X * sz + L.Y * cz, L.Z);   // RotZ (спин вокруг вертикали)
					V.Add(Xf.TransformPosition(Ls));
					FVector N = E.MeshNormals.IsValidIndex(k) ? E.MeshNormals[k] : FVector::UpVector;
					N = FVector(N.X * cz - N.Y * sz, N.X * sz + N.Y * cz, N.Z);
					Nn.Add(Xf.TransformVectorNoScale(N));
					UV.Add(E.MeshUV0.IsValidIndex(k) ? E.MeshUV0[k] : FVector2D::ZeroVector);
					Col.Add(FLinearColor::White);
					I.Add(k);
				}
				FlipWindingIfMirrored(I, GetActorTransform().GetScale3D());
				AsdaFxCreateSection(Mesh, i, V, I, Nn, UV, Col);
				if (E.Material) Mesh->SetMaterial(i, GlowMaterialFor(i, E.Material));   // ★ Ф5: через «силу свечения» (см. GlowMaterialFor)
				SectionVerts[i] = E.MeshVerts.Num();
			}
			continue;
		}

		// ★ таймер периода + endMode (аудит 2026-07-27, 07_Update.c:231-239 / 08_Mesh_Update):
		//   0 = СТОП: эмиссия выкл + ВСЕ живые частицы гаснут НЕМЕДЛЕННО (particleInitState ×100), секция чистится;
		//   3 = БЕЗ сброса каналов: фриз трека на END-значениях (луп с узла 0 давал видимый скачок раз в период);
		//   1/2 = restart (Fmod, как прежде).
		//   ⚠ УТОЧНЕНО 2026-09-10 по телам эталонных функций (см. блок ключей `asda.Fx.*` в шапке файла):
		//     «частицы гаснут немедленно» у endMode 0 — НЕВЕРНО для партикл-ветки (эталон пишет только
		//     `lifeTimer = −1.0`, живые частицы доигрывают), а endMode 3 у партикл-ветки — НЕ рестарт,
		//     а удержание последнего сегмента. Обе поправки живут под ключами `asda.Fx.EndTail` и
		//     `asda.Fx.LoopHold`; квад/кольцо/меш описаны здесь верно и не меняются.
		S.LifeTimer += Dt;
		const float Period = FMath::Max(E.Period, 0.05f);
		// ★★★ МАРШ САМОГО ЭМИТТЕРА (эталон `CEmitter_Update_v3@0x1211C20`, строки
		//   `emitPos[i] += dt·emitPosVel[i]` и `rotXCur/rotYCur/rotZCur += dt·rot*Vel`).
		//   Идёт ДО обработки endMode — ровно как в оригинале, где марш стоит выше проверки
		//   `lifeTimer > lifetime`. При bEmitterMotion == false (все ранее собранные BP и все карты)
		//   векторы скоростей нулевые — арифметика no-op, поведение бит-в-бит прежнее.
		//   ★ Погашённый эмиттер (endMode 0) НЕ марширует: в эталоне `Update_v3` при `lifeTimer <= −1.0`
		//     выходит ПЕРВОЙ строкой, до всех `+= dt·vel` — иначе точка эмиссии уезжает бесконечно.
		if (!S.bStopped)
		{
			S.EmitPos += S.EmitPosVel * Dt;
			S.EmitRot += S.EmitRotVel * Dt;
		}
		// ★★★ [D] ХВОСТ ЗАТУХАНИЯ У endMode 0 — только ПАРТИКЛ-ветка. Эталон `CEmitter_Update_v3@0x1211C20`
		//   и `CEmitter_Update_Track@0x1204DA0`, `case 0`: пишется ТОЛЬКО `lifeTimer = −1.0`; цикл
		//   пер-частичного тика в обеих функциях стоит ВЫШЕ проверки `lifeTimer <= −1.0` ⇒ уже рождённые
		//   частицы доигрывают свою жизнь и гаснут по собственным рампам. Квад/кольцо гасим как прежде:
		//   у них `case 0` в эталоне снапает каналы на БАЗОВЫЕ СИДЫ и ставит `lifeTimer = −1.0`
		//   (`effectUpdateKeyframeTrack@0x11F7B10`, ветка `spawnSpread == 0`), под что в порту
		//   откалибрована очистка секции. Ключ `asda.Fx.EndTail 0` возвращает прежнее «гасить сразу».
		const bool bEndTail = CVarAsdaFxEndTail.GetValueOnGameThread() != 0 && !E.bSingleQuad && !E.bRing;
		// ★★★ ЦИКЛ 1:1 — режим конца периода квада/кольца по эталону (таблица в шапке блока «ЦИКЛ 1:1»).
		//   nullptr = прежний путь (частицы, данных нет, ключ выключен, осознанная правка ассета).
		const FAsdaFxLoopRt* LR = (bLoopOn && (E.bSingleQuad || E.bRing) && LoopRt.IsValidIndex(i)
			&& LoopRt[i].Mode != EAsdaFxLoopMode::Legacy) ? &LoopRt[i] : nullptr;
		if (LR && S.LifeTimer > Period)
		{
			switch (LR->Mode)
			{
			case EAsdaFxLoopMode::Restart:
				// 1: снап КОНЦА + реинит — новый период с узла 0 (как прежний Fmod; счёт оборотов — для пинг-понга/зонда)
				S.WrapCount += FMath::Max(1, FMath::FloorToInt(S.LifeTimer / Period));
				S.LifeTimer = FMath::Fmod(S.LifeTimer, Period);
				ResetEmitterMotion(E, S);
				break;
			case EAsdaFxLoopMode::Stop:
				if (!S.bStopped) { S.bStopped = true; ClearFxSection(i); }
				S.LifeTimer = Period;
				break;
			case EAsdaFxLoopMode::Freeze:
				// 0 эталона: снап КОНЦА, `lifeTimer = −1` — update больше не идёт, рисуется застывшим
				S.bFrozen = true;
				S.LifeTimer = Period;
				// ★ 08.10.2026 (ревизия fx_parity3 МЕЛК-3): прежнее гашение «застыл после конца контейнера» убрано как
				//   НЕДОСТИЖИМОЕ — при `bLoopContainerDone && !bLoopHolder` шаг выходит раньше (ЦИКЛ-4), эмиттеры уже погашены.
				break;
			case EAsdaFxLoopMode::Hold:
			case EAsdaFxLoopMode::MarchAll:
				// 3 / 2: таймер эталона растёт дальше; держим время трека на конце, а «сверх конца» копим
				S.OverTime += S.LifeTimer - Period;
				S.LifeTimer = Period;
				break;
			case EAsdaFxLoopMode::WrapMarch:
			case EAsdaFxLoopMode::PingPong:
				// 3 у 0x13/0x14: `lifeTimer = 0` без реинита; 2 у 0x01/0x02/0x04: свап СТАРТ↔КОНЕЦ
				S.WrapCount += FMath::Max(1, FMath::FloorToInt(S.LifeTimer / Period));
				S.LifeTimer = FMath::Fmod(S.LifeTimer, Period);
				break;
			default:
				break;
			}
		}
		else if (S.LifeTimer > Period)
		{
			switch (E.EndMode)
			{
			case 0:
				if (!S.bStopped)
				{
					S.bStopped = true;
					if (!bEndTail)
					{
						for (FAsdaFxParticle& P : S.Slots) { P.Age = -1.f; }
						ClearFxSection(i);
					}
				}
				S.LifeTimer = Period;
				break;
			case 3:
				// ★ endMode 3 ПО ТИПУ ВЕТКИ (аудит + регресс дыма 2026-07-28): спрайт-квад/кольцо-трек
				//   (0x11F7B10) = ФРИЗ на END-значениях (луп с узла 0 давал скачок); ЧАСТИЦЫ-эмиттер (v3,
				//   04_Update_v3) = перезапуск цикла с продолжением дрейфа — фриз рождал частицы с
				//   death-альфой 0.0 (невидимки) → «дым одноразовый».
				// ★★★ [C] ЭТАЛОН `CEmitter_Update_Track@0x1204DA0` / `CEmitter_Update_v3@0x1211C20`, `case 3`:
				//   пишется `lifeTimer = 0.0` и БОЛЬШЕ НИЧЕГО — `trackFrameIdx` НЕ сбрасывается, `InitReset`
				//   НЕ зовётся ⇒ эмиттер навсегда остаётся на ПОСЛЕДНЕМ сегменте трека, а маршевые каналы
				//   (emitPos/углы/velSpawn/spawnRate) продолжают идти без разрыва. Прежняя ветка порта
				//   (`Fmod` + `ResetEmitterMotion`) была полным РЕСТАРТОМ — раз в период трек прыгал на узел 0.
				//   Держим `LifeTimer = Period`: у нас трек сэмплируется по времени, и «время конца» даёт
				//   ровно последний сегмент — то же состояние, что у эталона (см. `SegIndexAt`).
				//   Старая грабля «фриз рождает частицы с death-альфой 0» снята правкой [B]: спавн берётся
				//   из НАЧАЛА сегмента (birth), а не из значения в точке конца.
				//   Ключ `asda.Fx.LoopHold 0` возвращает прежний рестарт.
				if (E.bSingleQuad || E.bRing || CVarAsdaFxLoopHold.GetValueOnGameThread() != 0)
				{
					S.LifeTimer = Period;
				}
				else { S.LifeTimer = FMath::Fmod(S.LifeTimer, Period); ResetEmitterMotion(E, S); }
				break;
			default:
				S.LifeTimer = FMath::Fmod(S.LifeTimer, Period);
				// ★ endMode 1/2 = `CEmitter_InitReset` / «randomize+init» — оба переигрывают марш
				//   точки эмиссии и углов с нуля (эталон `CEmitter_Update_v3`, ветки case 1/2).
				ResetEmitterMotion(E, S);
				break;
			}
		}
		// endMode 0: эмиссии больше нет (гейт `!S.bStopped` ниже). При включённом хвосте партикл-ветка
		// продолжает тикать и рисовать УЖЕ ЖИВЫЕ частицы, пока последняя не истечёт; когда слоты пусты —
		// секция гасится (идемпотентно) и эмиттер выходит из шага, как раньше. Квады/кольца — как было.
		if (S.bStopped)
		{
			const bool bTailAlive = bEndTail
				&& S.Slots.ContainsByPredicate([](const FAsdaFxParticle& P) { return P.Age >= 0.f; });
			if (!bTailAlive) { ClearFxSection(i); continue; }
		}
		// ★★★ ЦИКЛ 1:1: время выборки трека (пинг-понг — зеркально на нечётном обороте), марш каналов сверх
		//   конца и время кадра флипбука. На прежнем пути FlipTime == LifeTimer — поведение бит-в-бит.
		//   ★ 08.10.2026 (СРЕДН-5): одной функцией с мешами и светом эффекта (`SampleEmitterNode`).
		PrepareAlphaClamp(i);                            // ★ ЦИКЛ-3: снап канала альфы этого кадра (до выборки)
		FAsdaFxNode Cur; SampleEmitterNode(i, Cur);
		CommitAlphaClamp(i, Cur);                        // ★ ЦИКЛ-3: альфа > 1 → 1 с записью в канал (квады/кольца)
		if (LR && LR->Mode == EAsdaFxLoopMode::Freeze)
		{
			S.FlipTime = Period * 0.9999f;                       // застывший кадр = последний кадр атласа
		}
		else if (LR && (LR->Mode == EAsdaFxLoopMode::Hold || LR->Mode == EAsdaFxLoopMode::MarchAll))
		{
			S.FlipTime = S.LifeTimer + S.OverTime;               // кадр флипбука идёт дальше (V заворачивается TA_WRAP)
		}
		else
		{
			S.FlipTime = S.LifeTimer;
		}

		// ★ UV single-quad: БЕЗ аккумуляции! Эталон 92507: марш U/V (rotAngle[2]+=dt·node[+148]) идёт ТОЛЬКО при
		//   живом track-node (if gap_30[60]); у flooer/wave узлов НЕТ → uScroll=1.0 остаётся СТАТИЧНЫМ сидом
		//   (1.0≡0 при wrap = no-op). Аккумулятор гонял пятно по кваду → резалось на шве («потеряли форму», полосы).
		if (E.bRing)
		{
			// КОЛЬЦО-лента (0xE). ★★★ КАРТ-ПУТЬ ПЕРЕВЕДЁН НА ЭТАЛОН (дизасм ringDraw@0x1215C10, 2026-07-31,
			// отчёт Reports/ring0xE_etalon_re.md). Эталонная цепочка (row-vector D3DX, game-оси Y-вверх):
			//   v_rot = v · RotY(spin 0x398) · S(0x380) · RotX(0x3A0) · RotY(0x3A8) · RotZ(0x3B0)
			//           · N48(=RotY(node.angle324), holder) · N520(=attach кость×группа×place-scale)   [0x1215FE0..0x1216271]
			//   T     = basePos(0x3FC, СЫРОЙ МИР, без scale/rot) + (posChan(0x3E4)·R)·(R·N48·N520)      [0x1216271..0x12164DA]
			//           + node.pos756(attach) + node.pos368(дрейф узла);  R=Rx·Ry·Rz применяется ДВАЖДЫ (причуда оригинала).
			//   RotY(node+324) на 0x1216297 вычисляется и ПЕРЕЗАПИСЫВАЕТСЯ (мёртв); RotY(180°) в эталоне НЕТ.
			// Соответствие данных порта (live-дамп asda.dmp, эмиттер 0x38065C58 + диск eff_rainbow_a_00.jeb):
			//   Cur.RingBasePos == эталонный POS-КАНАЛ 0x3E4 (диск rec+0x144; парсер порта НЕГИРОВАЛ z: диск +1.75, порт −1.75);
			//   Cur.RingPosOfs  == эталонный basePos 0x3FC (у карт = 0);  актор rot+scale == N520 (place-scale 58.35 в матрице);
			//   HolderAngle == node.angle324 (карты: 0), HolderOfs == node.pos368-дрейф.
			TArray<FVector> V; TArray<int32> I; TArray<FVector> Nn; TArray<FVector2D> UV; TArray<FLinearColor> Col;
			const FTransform Xf = GetActorTransform();
			const float DegR = PI / 180.f;
			// ИСТОРИЧЕСКИЕ лямбды (ТРАНСПОНИРОВАНЫ против D3DX) — только для соулмейт (bBaseAlreadyUE)
			// и char/mount (bStrictRingBasePos) путей, откалиброванных на них. НЕ для карт.
			auto RotYv = [](const FVector& P, float A){ const float c=FMath::Cos(A), s=FMath::Sin(A); return FVector(P.X*c - P.Z*s, P.Y, P.X*s + P.Z*c); };
			auto RotXv = [](const FVector& P, float A){ const float c=FMath::Cos(A), s=FMath::Sin(A); return FVector(P.X, P.Y*c + P.Z*s, -P.Y*s + P.Z*c); };
			auto RotZv = [](const FVector& P, float A){ const float c=FMath::Cos(A), s=FMath::Sin(A); return FVector(P.X*c + P.Y*s, -P.X*s + P.Y*c, P.Z); };
			// ★ ЭТАЛОННЫЕ D3DX row-vector ротации (дизасм D3DXMatrixRotationX/Y/Z @0xFF4ADD/0xFF4BDA/0xFF4CED):
			//   RotX: y'=y·c−z·s, z'=y·s+z·c;  RotY: x'=x·c+z·s, z'=−x·s+z·c;  RotZ: x'=x·c−y·s, y'=x·s+y·c.
			auto RotXd = [](const FVector& P, float A){ const float c=FMath::Cos(A), s=FMath::Sin(A); return FVector(P.X, P.Y*c - P.Z*s, P.Y*s + P.Z*c); };
			auto RotYd = [](const FVector& P, float A){ const float c=FMath::Cos(A), s=FMath::Sin(A); return FVector(P.X*c + P.Z*s, P.Y, -P.X*s + P.Z*c); };
			auto RotZd = [](const FVector& P, float A){ const float c=FMath::Cos(A), s=FMath::Sin(A); return FVector(P.X*c - P.Y*s, P.X*s + P.Y*c, P.Z); };
			// ★ ЭТАЛОННУЮ ветку получают карты И СВЕЧЕНИЕ ЗАТОЧКИ (2026-07-31, круг 7).
			//   Историческая char/mount-ветка (bStrictRingBasePos) НЕ применяет к офсету R² — а по КАНОНУ 0xE
			//   (дизасм ring-Draw 0x1215C10, Docs/Port/systems/EFFECTS_CONVERSION_MODEL.md) центр кольца =
			//   basePos + (posChan·R)·(R·N48·N520), т.е. канальные вращения действуют на pos-канал ДВАЖДЫ.
			//   У колец соулмейта/маунта канальные вращения нулевые ⇒ разницы не было и дефект не всплывал.
			//   У свечения оружия они НЕ нулевые: у баллисты bali_e_*_ma_03 все ccyl имеют rotZ=90° ⇒
			//   R² = RotZ(180°) переносит якорь (70,−5,19) → (−70,+5,19) и (30,−12,12) → (−30,+12,12) —
			//   РОВНО на те станции, где лежат проверенные квады того же .jeb (bil1/bil_2 x=−70, sp_3 x=−20).
			//   Без R² кольца оставались на +X, т.е. по другую сторону от оружия.
			//   Гейт — АКТОРНЫЙ bStrictQuadAttach (ставит только ApplyWeaponGlowToActor для оружия/щита) ⇒
			//   карты, соулмейт, char-fx, маунт и крылья не затронуты.
			//   ★★★ [ПОРТ] 2026-09-11: третье слагаемое — `E.bRingEtalonLayout`, ПРИЗНАК ДАННЫХ
			//   («офсеты этого кольца уже уложены эталонной раскладкой», см. шапку флага в
			//   `AsdaEffectActor.h`). Без него ветка зависела от того, КТО заспавнил актор:
			//   навык (`asda.Skill.StrictFx`) читал эталонной, а стенд `asda.Fx.Light.Stand` и
			//   эффекты состояний — исторической, хотя ассет один и тот же. Дефолт false ⇒
			//   ни один ранее собранный эффект не меняется.
			const bool bEtalonMapPath = !bBaseAlreadyUE
				&& (!E.bStrictRingBasePos || bStrictQuadAttach || E.bRingEtalonLayout);
			// ★★★ [ПОРТ] Хиральное зеркало кольца — под флагом эмиттера (см. `AsdaEffectActor.h`,
			//   `bRingNoChiral`). Дефолт false ⇒ `RingChiral == ChiralNormal` ⇒ ВСЁ КАК БЫЛО.
			const FVector RingChiral = E.bRingNoChiral ? FVector::ZeroVector : ChiralNormal;
			// ★ pos-канал карт-пути (эталон 0x1216297..0x12163CF): posChan·R·R·N48(holder)·[chiral≈зеркало N520],
			//   затем через Xf (актор = N520 rot+place-scale). rScale канала на posChan НЕ действует.
			FVector PosWGame = FVector::ZeroVector;
			if (bEtalonMapPath)
			{
				const FVector PC(Cur.RingBasePos.X, Cur.RingBasePos.Y, -Cur.RingBasePos.Z);   // компенсация z-негации парсера (диск +z)
				FVector P1 = RotZd(RotYd(RotXd(PC, Cur.RingRot.X*DegR), Cur.RingRot.Y*DegR), Cur.RingRot.Z*DegR);
				P1 = RotZd(RotYd(RotXd(P1, Cur.RingRot.X*DegR), Cur.RingRot.Y*DegR), Cur.RingRot.Z*DegR);   // R дважды — как в эталоне
				P1 = RotYd(P1, HolderAngle * DegR);                                            // N48
				if (!RingChiral.IsNearlyZero())
				{
					P1 = P1 - 2.f * FVector::DotProduct(P1, RingChiral) * RingChiral;          // зеркальный носитель (часть N520-модели)
				}
				PosWGame = P1;
			}
			// ★ FxAlphaScale — рантайм-множитель всего эффекта (GM-«скрыться»), дефолт 1 = данные как есть.
			const FLinearColor RGBA(Cur.Color.R, Cur.Color.G, Cur.Color.B, FMath::Clamp(Cur.Alpha * FxAlphaScale, 0.f, 1.f));
			// ★★★ ФЛИПБУК КОЛЬЦА — типы 0x15 (12 эмиттеров) и 0x1A (1). Реверс 2026-08-08 по ДИЗАСМУ
			//   (не только Hex-Rays), отчёт Reports/jeb_emitters_0x14_0x19.md §9.
			//   Оригинал: update-функции 0x1240250 (0x15) и 0x1247CE0 (0x1A) — ЕДИНСТВЕННЫЕ писатели UV
			//   (по 4 записи в +0xBEC/+0xBF0, ни draw 0x1240860, ни ring-draw 0x1215C10 UV не трогают,
			//   текстурной матрицы нет: SetTransform(256) = D3DTS_WORLD). Формула:
			//     frameDur = life/(cols·rows);  frame = lifeTimer/frameDur;  row = frame/cols; col = frame%cols;
			//     u(i) = col/cols + (i/29)/cols ;  v[2i]=(row+1)/rows (низ, y=0) ;  v[2i+1]=row/rows (верх, y=H).
			//   ⇒ ячейка атласа растягивается по U на ВСЮ окружность, по V — на всю высоту цилиндра;
			//   uTile/vA/uScroll/vScroll у этих типов оригиналом НЕ применяются (на диске мусор).
			//   Сверка с простым 0xE (ctor 0x12151A0): там u=i/29, v=0.99(низ)/0.01(верх). При cols=rows=1
			//   формула даёт u=i/29, v=1.0/0.0 — та же развёртка без «инсета» 0.01 от растекания краёв;
			//   расхождение неважно, т.к. ветка гейтится cols·rows>1 и при 1×1 никогда не берётся.
			// ★ ДВА ДЕФЕКТА ОРИГИНАЛА ИСПРАВЛЕНЫ (закон fix-original-defects, §9.5 отчёта):
			//   (1) в оригинале стоит (uint32)(col−1) — при col==0 беззнаковое переполнение в 4294967295,
			//       во float32 весь член (i/29)/cols теряется и по кольцу размазывается один столбец текселей;
			//       на прочих кадрах U съезжает на ячейку влево. Берём col (V-часть той же формулы 0-based и верна).
			//   (2) делитель сегмента у 0x15 в update = 30.0, а у 0x1A и у init 0x15 = 29.0; геометрия кольца
			//       строится по θ=i·2π/29 ⇒ 30.0 — опечатка (шов не сходится). Берём 29.0 для обоих.
			// ★ Гейт cols·rows>1: у 0xE и типа 4 полей атласа на диске НЕТ ⇒ 1×1 ⇒ ветка не активна и
			//   поведение всех принятых карт-эффектов бит-в-бит прежнее (закон «дефолт = как было»).
			const int32 RAC = FMath::Max(E.AtlasCols, 1), RAR = FMath::Max(E.AtlasRows, 1);
			const int32 RFrames = RAC * RAR;
			const bool bRingAtlas = RFrames > 1;
			float RingU0 = 0.f, RingVBot = 1.f, RingVTop = 0.f;
			if (bRingAtlas)
			{
				const float RPer = FMath::Max(E.Period, 1e-3f);
				const float RPh  = FMath::Fmod(FMath::Max(S.FlipTime, 0.f), RPer) / RPer;   // ★ ЦИКЛ 1:1: время флипбука (== LifeTimer на прежнем пути)
				const int32 RFrame = FMath::Clamp(FMath::FloorToInt(RPh * RFrames), 0, RFrames - 1);
				const int32 RCol = RFrame % RAC, RRow = RFrame / RAC;
				RingU0   = (float)RCol / (float)RAC;
				RingVBot = (float)(RRow + 1) / (float)RAR;
				RingVTop = (float)RRow / (float)RAR;
			}
			for (int32 Seg = 0; Seg < 30; ++Seg)
			{
				const float Ang = 2.f * PI * (float)Seg / 29.f;
				const float U = bRingAtlas
					? (RingU0 + (float)Seg / 29.f / (float)RAC)                                  // флипбук 0x15/0x1A: ячейка на всю окружность
					: ((float)Seg / 29.f * FMath::Max(E.RingUTile, 0.01f) + Cur.RingU);          // без fmod: wrap делает семплер (движок: текстурная матрица)
				// game-пара: A=низ (y=0, botR), B=верх (y=height, topR)
				for (int32 tb = 0; tb < 2; ++tb)
				{
					const float R0 = tb ? Cur.RingTopR : Cur.RingBotR;
					FVector G(-FMath::Sin(Ang) * R0, tb ? Cur.RingH : 0.f, FMath::Cos(Ang) * R0);   // эталон ctor 0x12151A0: x=−sinθ, z=cosθ, ось=локальный +Y
					if (bEtalonMapPath)
					{
						// ★ ЭТАЛОН (0x1215C10): RotY(spin)→S→Rx→Ry→Rz→N48(holder); RotY(180°) НЕТ.
						G = RotYd(G, Cur.RingRoll * DegR) * Cur.RingScale;
						G = RotXd(G, Cur.RingRot.X * DegR); G = RotYd(G, Cur.RingRot.Y * DegR); G = RotZd(G, Cur.RingRot.Z * DegR);
						G = RotYd(G, HolderAngle * DegR);                // N48 = RotY(node.angle324)
					}
					else
					{
						G = RotYv(G, Cur.RingRoll * DegR) * Cur.RingScale;
						G = RotXv(G, Cur.RingRot.X * DegR); G = RotYv(G, Cur.RingRot.Y * DegR); G = RotZv(G, Cur.RingRot.Z * DegR);
						G = RotYv(G, PI);                                    // +180° локальная Y (историч. соулмейт/char-mount пути)
						G = RotYv(G, HolderAngle * DegR);                    // header-спин холдера (до attach-матрицы)
					}
					// ★ хиральное отражение ГЕОМЕТРИИ (зеркальный носитель type-2): та же нормаль, что у
					//   позиций place-данных — иначе конус и офсеты рассинхронизированы (tower «юбка ниже»)
					if (!RingChiral.IsNearlyZero())
					{
						G = G - 2.f * FVector::DotProduct(G, RingChiral) * RingChiral;
					}
					// ★ bBaseAlreadyUE: база (кость шкатулки) уже в UE-осях → пропустить game→UE своп (иначе двойной = «вниз»);
					//   basePos/holderOfs ×актор-скейл (×100-сцена). Иначе — как раньше (карт-эффекты в игр.осях).
					// ★★★ РЕГРЕССИЯ №2 (2026-07-30, «rainbow/adsalight опять сбиты»): в ходе работ по кольцу
					//   соулмейта сюда — в ОБЩУЮ ветку БЕЗ ФЛАГА — внесли «basePos В ЛОКАЛЕ ДО Xf» (эталонная
					//   формула MatrixTranslate(pos+basePos)·boneW). Для колец соулмейта это неотличимо (их актор
					//   identity), но КАРТЫ откалиброваны под ИСТОРИЧЕСКОЕ «basePos В МИРЕ, без scale/rot»:
					//   с локальной схемой basePos помножился на актор-scale и повернулся актор-ориентацией —
					//   rainbow ×3 уехали на 338 юн (из них +321 вверх, scale 58!), rainbow2 на 131, фонари
					//   adsalight на 77–182, tower на 58–195 (замер fx_dump_alpen_PRE30, дифф формул).
					//   Перезаливы уровня были НИ ПРИ ЧЁМ — данные акторов бит-в-бит; регрессию нёс ЭТОТ код.
					//   Дефолт = историческое поведение; строгая формула — ТОЛЬКО под флагом bStrictRingBasePos
					//   (ставит генератор char/mount-эффектов). ЗАКОН: общий код эффектов — только под флагом.
					const bool bLocalBP = !bBaseAlreadyUE && E.bStrictRingBasePos;
					// ★★★ КАРТ-ПУТЬ = ЭТАЛОН (2026-07-31, дизасм 0x1215C10; ПРЕЖНИЕ реконструкции №1-№3 ОПРОВЕРГНУТЫ):
					//   RingBasePos — это НЕ «basePos сырой мир», а POS-КАНАЛ 0x3E4 (live-дамп: эмиттер радуги 0x38065C58
					//   posChan=(5.5,0,+1.75), basePos 0x3FC=0; диск rec+0x144=+1.75 — парсер порта негировал z).
					//   Эталон: центр = attachPos + posChan·R²·N48·N520; N520=актор (rot+place-scale) ⇒ канал идёт
					//   ЧЕРЕЗ Xf.TransformPosition ВМЕСТЕ с геометрией (PosWGame, посчитан до цикла). «Сырой мир»
					//   у эталона — только канал 0x3FC (= Cur.RingPosOfs, у карт 0) БЕЗ ×scale + дрейф узла (HolderOfs).
					//   Историч. «BP×BS» поднимал радугу на +321 по UE-Y (ВБОК!), эталон даёт +322 по UE-Z (ВВЕРХ);
					//   расхождение центров ~530 юн — те самые «кольцо сбоку дирижабля» и «ромбы не на лампах».
					const float BS = FMath::Abs(GetActorScale3D().X);
					if (bEtalonMapPath)
					{
						const FVector GP = G + PosWGame;                        // pos-канал в той же (game) системе, до N520
						V.Add(Xf.TransformPosition(AsdaCoord::GameToUe3D(GP))  // N520 (актор rot+scale) + attachPos (loc)
							+ AsdaCoord::GameToUe3D(Cur.RingPosOfs)   // ★ Аудит №3 (B1); эталонный basePos 0x3FC: сырой мир, БЕЗ scale
							+ HolderOfs);                                       // node.pos368-дрейф (сырой мир, эталон objAnimRotateUpdate)
					}
					else
					{
					const FVector Gl = bLocalBP ? (G + Cur.RingBasePos) : G;
					const FVector UE = bBaseAlreadyUE ? Gl : AsdaCoord::GameToUe3D(Gl);
					const FVector BP = bBaseAlreadyUE ? Cur.RingBasePos
						: (bLocalBP ? FVector::ZeroVector
						            : AsdaCoord::GameToUe3D(Cur.RingBasePos));   // ★ Аудит №3 (B1)
					V.Add(Xf.TransformPosition(UE) + BP * BS + HolderOfs * BS);
					}
					Nn.Add(FVector::UpVector);
					// tb=0 = НИЗ (y=0) = чётная вершина 2i (эталон: v=0.99 / флипбук (row+1)/rows);
					// tb=1 = ВЕРХ (y=H) = нечётная вершина 2i+1 (эталон: v=0.01 / флипбук row/rows).
					UV.Add(FVector2D(U, bRingAtlas ? (tb ? RingVTop : RingVBot)
					                               : (tb ? (0.01f + Cur.RingV) : (E.RingVA + Cur.RingV))));
					Col.Add(RGBA);
				}
			}
			// strip 60 вершин → 58 треугольников (материал двусторонний)
			for (int32 k = 0; k + 2 < V.Num(); ++k) { I.Add(k); I.Add(k + 1); I.Add(k + 2); }
			FlipWindingIfMirrored(I, GetActorTransform().GetScale3D());
			if (SectionVerts.IsValidIndex(i) && SectionVerts[i] == V.Num())
				AsdaFxUpdateSection(Mesh, i, V, Nn, UV, Col);
			else
			{
				AsdaFxCreateSection(Mesh, i, V, I, Nn, UV, Col);
				if (E.Material) Mesh->SetMaterial(i, GlowMaterialFor(i, E.Material));   // ★ Ф5: через «силу свечения» (см. GlowMaterialFor)
				if (SectionVerts.IsValidIndex(i)) SectionVerts[i] = V.Num();
			}
			continue;
		}

		if (!E.bSingleQuad && !S.bStopped)
		{
			// эмиссия: accum += rate*dt; n=floor; кламп v3 (n>rate → 1, accum=0)
			S.EmitAccum += Cur.Rate * Dt;
			int32 N = FMath::FloorToInt(S.EmitAccum);
			if (N > 0)
			{
				if ((float)N <= Cur.Rate) S.EmitAccum -= (float)N;
				else { N = 1; S.EmitAccum = 0.f; }
				for (int32 k = 0; k < N; ++k) SpawnParticle(E, S, Cur);
			}
		}
		if (!E.bSingleQuad)
		{
			// тик частиц: pos+=vel·dt; vel+=(0,0,-10)·grav·dt (game Y-вверх → UE Z-вверх); линейный марш
			const FVector ActorLoc = GetActorLocation();
			const float FloorZ = ActorLoc.Z - 5.f * FMath::Abs(GetActorScale3D().X);
			for (FAsdaFxParticle& P : S.Slots)
			{
				if (P.Age < 0.f) continue;
				if (P.Age + Dt >= P.Life) { P.Age = -1.f; continue; }
				P.Age += Dt;
				P.Pos += P.Vel * Dt;
				// ★ floor-bounce (эталон 10_Tick.c:36-40, gated флагом эффекта; дефолт выкл — оффсет флага не реверснут)
				if (bFloorBounce && P.Pos.Z < FloorZ) { P.Pos.Z = FloorZ; P.Vel.Z = -P.Vel.Z * 0.5f; }
				P.Vel += FVector(0, 0, -10.f) * P.Grav * Dt;
				// ★ аттрактор (эталон 10_Tick.c:47-58, gated): vel += норм(цель−pos)·сила·dt; цель = позиция эффекта
				if (AttractStrength > 0.f) { P.Vel += (ActorLoc - P.Pos).GetSafeNormal() * AttractStrength * Dt; }
				P.RGB += P.RGBRate * Dt;
				P.Alpha = FMath::Clamp(P.Alpha + P.AlphaRate * Dt, 0.f, 1.f);
				P.SX += P.SXRate * Dt; P.SY += P.SYRate * Dt;
				P.Grav += P.GravRate * Dt;
			}
		}
		const FVector QuadViewLoc = ViewLocPtr ? *ViewLocPtr : GetActorLocation() + FVector(0, -1000, 500);
#if !UE_BUILD_SHIPPING
		if (GAsdaFxViewSampleCheck != 0) { ViewSampleStandCheck(i, E, S, QuadViewLoc, View, Cur); }   // стенд: только чтение
#endif
		BuildQuads(i, E, S, QuadViewLoc, View, Cur);

		// ★ Покадровый ряд каналов (`asda.Fx.TrackLog <часть имени>`) — только чтение и печать.
		{
			const FString TrackFilter = CVarAsdaFxTrackLog.GetValueOnGameThread();
			if (!TrackFilter.IsEmpty() && (EffectName.Contains(TrackFilter) || GetName().Contains(TrackFilter)))
			{
				const int32 Seg = SegIndexAt(E, S.LifeTimer);
				const FAsdaFxNode& SegN = E.Nodes.IsValidIndex(Seg) ? E.Nodes[Seg] : Cur;
				int32 Alive = 0; float SumA = 0.f, SumSX = 0.f;
				for (const FAsdaFxParticle& P : S.Slots)
				{
					if (P.Age < 0.f) { continue; }
					++Alive; SumA += P.Alpha; SumSX += P.SX;
				}
				// ★ ЦИКЛ 1:1 (08.10.2026): в хвост ряда — режим конца периода, «сверх конца», обороты,
				//   roll/rotZ/posZ канала и время флипбука: стык цикла виден числами.
				UE_LOG(LogTemp, Warning,
					TEXT("[fxtrack]\t%s\t%d\t0x%X\tt=%.3f\tlife=%.3f\tstop=%d\tseg=%d\tcurA=%.4f\tcurSX=%.2f\tcurRGB=%.3f,%.3f,%.3f\tspawnA=%.4f\tspawnSX=%.2f\tspawnRGB=%.3f,%.3f,%.3f\tживых=%d\tсрА=%.4f\tсрSX=%.2f\tрежим=%d\tсверх=%.3f\tоборот=%d\troll=%.2f\trotZ=%.2f\tposZ=%.2f\tflip=%.3f\tконт=%.3f"),
					*EffectName, i, E.JebType, ElapsedTime, S.LifeTimer, S.bStopped ? 1 : 0, Seg,
					Cur.Alpha, Cur.SizeX, Cur.Color.R, Cur.Color.G, Cur.Color.B,
					SegN.Alpha, SegN.SizeX, SegN.Color.R, SegN.Color.G, SegN.Color.B,
					Alive, Alive ? SumA / Alive : 0.f, Alive ? SumSX / Alive : 0.f,
					LR ? (int32)LR->Mode : 0, S.OverTime, S.WrapCount, Cur.RingRoll, Cur.RingRot.Z, Cur.RingPosOfs.Z,
					S.FlipTime, LoopContainerTimer);
			}
		}
	}

	// ★★★ 08.10.2026 (ЦИКЛ 1:1, рецепт включения `asda.Fx.End0Freeze` в ревизии fx_loop, п. 6.4): ДИАГНОСТИКА
	//   «застыл без владельца». ТОЛЬКО ЛОГ — ничего не гасит (не рантайм-фикс): если ВСЕ видимые эмиттеры
	//   застыли (endMode 0, Freeze) дольше max(контейнер, 30 с), а погасить эффект некому — нет срока жизни,
	//   не холдер карты, контейнер не вечный по воле владельца (переопределение 3) и (при вечном контейнере
	//   с диска) нет актора-владельца, — пишем одно предупреждение на актор. Ловит будущие «вечные кадры».
	if (bLoopOn && !bLoopFrozenWarned)
	{
		int32 NVis = 0, NFrozen = 0;
		for (int32 k = 0; k < States.Num(); ++k)
		{
			if (!States[k].bStarted || States[k].bStopped) { continue; }
			++NVis;
			if (States[k].bFrozen) { ++NFrozen; }
		}
		LoopAllFrozenTime = (NVis > 0 && NFrozen == NVis) ? LoopAllFrozenTime + Dt : 0.f;
		const float Limit = FMath::Max(bLoopContainer ? LoopContainerDur : 0.f, 30.f);
		if (LoopAllFrozenTime > Limit && !bLoopHolder && GetLifeSpan() <= 0.f && LoopContainerEndOverride != 3
			&& (LoopContainerEnd != 3 || GetOwner() == nullptr))
		{
			bLoopFrozenWarned = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[петля] застыл без владельца: %s (%s) — все %d видимых эмиттеров застыли %.1f с; срока жизни нет, контейнер %.2f с/режим %d, переопределение %d, владелец %s. Только диагностика."),
				*EffectName, *GetName(), NVis, LoopAllFrozenTime, LoopContainerDur, LoopContainerEnd,
				LoopContainerEndOverride, GetOwner() ? *GetOwner()->GetName() : TEXT("нет"));
		}
	}

	// ★ Диагностика (asda.FxDebug 1): живые частицы по эмиттерам + кадр .jem-скина, раз в секунду.
	if (CVarAsdaFxDebug.GetValueOnGameThread() != 0 && ElapsedTime - LastFxDebugTime >= 1.f)
	{
		LastFxDebugTime = ElapsedTime;
		FString Line;
		for (int32 i = 0; i < Emitters.Num(); ++i)
		{
			int32 Alive = 0;
			if (States.IsValidIndex(i)) { for (const FAsdaFxParticle& P : States[i].Slots) { if (P.Age >= 0.f) ++Alive; } }
			Line += FString::Printf(TEXT("[%d тип 0x%X %s жив=%d верт=%d] "), i, Emitters[i].JebType,
				Emitters[i].bMeshEmitter ? TEXT("меш") : TEXT("част"), Alive,
				SectionVerts.IsValidIndex(i) ? SectionVerts[i] : -1);
		}
		UE_LOG(LogTemp, Warning, TEXT("[FxDebug] %s поз=%s скин-кадр=%.0f %s"), *EffectName,
			*GetActorLocation().ToCompactString(), MeshAnimFrame, *Line);
	}
}
