// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor_Quads.cpp — ★ R2 10.11.2026 (разрез AsdaEffectActor.cpp, дословный перенос): построение квадов BuildQuads.
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

// ★★★ МИНИМАЛЬНЫЙ РАЗМЕР (порог) — вместо ЖЁСТКО ЗАШИТЫХ полов 1.0 (квад) и 0.5 (частица).
//   ЭТАЛОН ПОЛА НЕ ИМЕЕТ: `effectQuad_updateAndDrawBillboard@0x120AEB0` строит вершины прямо из
//   каналов (`spawnPosCur[0] = −halfW`, `[2] = −halfH`, симметрично «+»), кольцо
//   (`effectTrailRender@0x1215C10`) множит цилиндр на `scale` как есть — ни клампа, ни минимума.
//   ДЕФОЛТ 0 (= точно как оригинал) выбран ПО ЗАМЕРУ, а не на глаз (`fx_scale_timing_audit.md` §15):
//   во всём архиве (2261 `.jeb`) значений размера строго между 0 и текущим полом — ЧЕТЫРЕ штуки
//   (один несобранный эмиттер); ВСЁ остальное «мелкое» — это РОВНО 0, то есть КОНЦЫ рампы
//   «вырасти из нуля / истаять в ноль». Любой порог > 0 искажает именно амплитуду роста, ради
//   которой и делался аудит, и ничего при этом не спасает: квад, у которого ВСЕ размеры равны 0
//   (то есть невидим целиком), в архиве ОДИН и он не собран ни в один BP.
//   Вырожденная геометрия безопасна: нормали здесь константные (`Fwd` у частиц, `UpVector` у
//   одиночного квада) — деления на длину нет, NaN взяться неоткуда; более того, мёртвые слоты
//   частиц ПО ПРОЕКТУ паддятся вырожденными квадами (4 совпадающие вершины) ради стабильной
//   топологии секции — то есть нулевой квад в этом рендерере штатное состояние.
//   Порог оставлен параметром на случай, если на приёмке где-то захочется «не терять» тонкую нить.
static TAutoConsoleVariable<float> CVarAsdaFxMinQuadSize(
	TEXT("asda.Fx.MinQuadSize"), 0.f,
	TEXT("Минимальный полу-экстент квада (0 = как в оригинале; было жёстко 1.0)"), ECVF_Cheat);
static TAutoConsoleVariable<float> CVarAsdaFxMinParticleSize(
	TEXT("asda.Fx.MinParticleSize"), 0.f,
	TEXT("Минимальный полу-экстент частицы (0 = как в оригинале; было жёстко 0.5)"), ECVF_Cheat);

// ★★★ 05.11.2026 [ОРИГИНАЛ] V ТЕКСТУРЫ ПЛОСКОГО КВАДА НА КОСТИ (жалоба игроков «розовая аура Stalker's Blade/Tachyon Blade
//   сдвинута вдоль клинка, выходит за острие и уходит в землю»). Эталон: таблица вершин `CEmitter_Type1_ctor@0x1208B40`
//   (stride 24 = pos·color·uv) — v0 (−1,0,−1) uv(0,0), v1 (−1,0,+1) uv(0,1), v2 (+1,0,−1) uv(1,0), v3 (+1,0,+1) uv(1,1);
//   `effectQuad_updateAndDrawBillboard@0x120AEB0` переписывает только x=±size.x и z=±size.y ⇒ край z=−size — ВЕРХ текстуры
//   (v=0), край z=+size — НИЗ (v=1). Порт ставил наоборот (z=−size ↔ v=1): геометрия совпадала с .jeb до единицы (замер
//   `asda.SelfTest.WeaponGlowFit`), но картинка лежала вверх ногами — у полотен свечения с roll 180° яркое основание пламени
//   уезжало с клинка за острие (в землю у двуручного в покое), а у кисти оставался гаснущий хвост.
//   Охват: акторы эталонной посадки на кость (`bStrictQuadAttach` — свечение заточки оружия и щита, эффекты Хранителя и
//   навыков на кости владельца). Плоские квады карт не трогаются: их вид подбирался вместе с π-«швартовкой».
//   1 — как эталон; 0 — как было.
// ★★★ 06.11.2026 [ОРИГИНАЛ] U ТЕКСТУРЫ БИЛЛБОРДА (надписи WIN/LOSE итога дуэли выходили зеркально). Эталон
//   `spriteBuildTransformAndDraw@0x120CF10` (render_core.c:49326): при режиме 1/3 ориентация — транспонированный вид, локальный +x =
//   экранное право, а вершина x = −size имеет U = 0 (таблица `CEmitter_Type1_ctor`). Порт строил `Rgt = Cross(Up, ToCam)`, в левой
//   системе UE это экранное ЛЕВО, и U = 0 оказывалась справа. Симметричные вспышки этого не показывали, надпись — показала
//   (текстура `win` прямая). Меняется только U: геометрия и вращение квада те же. 1 — как эталон; 0 — как было.
static ASDA_DEV_CVAR(int32, CVarAsdaFxBillboardUvEtalon,
	TEXT("asda.Fx.BillboardUvEtalon"), 1,
	TEXT("[ОРИГИНАЛ] Биллборд 0xC: 1 = U слева направо по экрану, как эталон; 0 = прежний порт (картинка зеркальна)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarAsdaFxAttachQuadUvEtalon,
	TEXT("asda.Fx.AttachQuadUvEtalon"), 1,
	TEXT("[ОРИГИНАЛ] 1 — у плоских квадов эффектов на кости (свечение заточки оружия/щита, эффекты на кости владельца) край "
	     "z=−size получает верх текстуры, как в оригинале; 0 — как было (текстура вверх ногами: свечение уезжает за острие)."),
	ECVF_Default);
// ★★★ 05.11.2026 [ОРИГИНАЛ] ТО ЖЕ ДЛЯ ОСТАЛЬНЫХ ПЛОСКИХ КВАДОВ 0xB/0x18 (навыки игрока и мобов, эффекты предметов/дропа, NPC, карт).
//   Локальная раскладка вершин у эталона одна на всех (0x1208B40 + 0x120AEB0), различается только матрица узла, поэтому
//   переворот V у порта был общим. π-«швартовка» attach-квадов (`RY(G, π)`) — поворот ГЕОМЕТРИИ вместе с текстурой (подобран
//   ради соосности квада с кольцами башни alpen), V она не исправляла: относительно своей геометрии текстура и там стояла
//   вверх ногами, швартовка остаётся как есть. Не трогаются: квады глифов выбора персонажа (`bBaseAlreadyUE`, своя плоскость
//   XY) и метка клика (`bFlipQuadV` — её переворот уже подобран вручную вместе с `bInvertNodeRot`). Картинка меняется только
//   у текстур, несимметричных по вертикали (у симметричных переворот незаметен). 1 — как эталон; 0 — как было.
static ASDA_DEV_CVAR(int32, CVarAsdaFxFlatQuadUvEtalon,
	TEXT("asda.Fx.FlatQuadUvEtalon"), 1,
	TEXT("[ОРИГИНАЛ] 1 — у остальных плоских квадов эффектов (навыки, предметы, NPC, карты) край z=−size получает верх текстуры, "
	     "как в оригинале; 0 — как было (текстура вверх ногами). Глифы выбора персонажа и метка клика не меняются."),
	ECVF_Default);

void AAsdaEffectActor::BuildQuads(int32 SectionIdx, const FAsdaFxEmitter& E, FAsdaFxEmitterState& S,
                                  const FVector& ViewLoc, const FAsdaFxViewSample& View, const FAsdaFxNode& Cur,
                                  FAsdaFxQuadCapture* Capture)
{
	const float HolderAngleQ = HolderYawStart + HolderYawSpeed * ElapsedTime;
	const FVector HolderOfsQ = HolderPosVel * ElapsedTime;
	// ★ Эталонный билборд частиц (mathBuildTransposedXZMatrixWithPos@0x125A1F0, верификация 2026-07-27):
	//   ВЕРТИКАЛЬНО-СТАБИЛИЗИРОВАННАЯ ЭКРАННАЯ ПЛОСКОСТЬ — транспонируется только XZ-блок VIEW, вертикаль
	//   остаётся мировой. Т.е. right = ГОРИЗОНТАЛЬНАЯ проекция камерного right (по НАПРАВЛЕНИЮ камеры, одна
	//   на все частицы), up = мировой. НЕ «на точку частицы» и НЕ полный сферический базис.
	FVector CamRightHoriz = FVector::ZeroVector; bool bHaveCamRot = false;
	// ★ 15.09.2026: поворот — из снимка наблюдателя шага (SampleView: оверрайд превью → AsdaView::GetViewPoint), а не
	//   из своего вызова на каждый эмиттер. Прежние две ветки сведены в одну выборку с тем же приоритетом:
	//   превью-панель редактора — поворот камеры своей сцены (bUseViewOverride); иначе AsdaView.
	//   ★ волна 3 п.11 / ИСПРАВЛЕНИЕ E7: своя копия брала поворот ТОЛЬКО у камеры игрока; в окне редактора его не было,
	//   и частицы строились «цилиндром на точку», хотя позиция уже бралась от вьюпорта. Теперь в редакторе — поворот
	//   перспективного вьюпорта этого мира: частицы в окне редактора стоят в плоскости экрана, как в игре.
	//   В игре (камера) — без изменений; у павна и отрисованного вида поворота нет — фолбэк ниже, как было.
	if (View.bHasRotation)
	{
		CamRightHoriz = FRotationMatrix(View.Rotation).GetScaledAxis(EAxis::Y);
		CamRightHoriz.Z = 0.f;
		bHaveCamRot = CamRightHoriz.Normalize();
	}
	TArray<FVector> V; TArray<int32> I; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FLinearColor> Col;
	auto AddQuad = [&](const FVector& C, float HX, float HY, const FLinearColor& RGBA,
	                   const FVector2D& UvMin = FVector2D(0.f, 0.f), const FVector2D& UvMax = FVector2D(1.f, 1.f))
	{
		FVector Fwd = (ViewLoc - C); Fwd.Normalize();
		FVector Up = FVector::UpVector;
		FVector Right;
		if (bHaveCamRot)
		{
			Right = CamRightHoriz;   // эталон: экранная плоскость, вертикаль мировая (все режимы частиц)
		}
		else
		{
			// фолбэк без поворота наблюдателя (headless, павн, отрисованный вид): цилиндр «на точку»
			Right = FVector::CrossProduct(Up, Fwd);
			if (!Right.Normalize()) Right = FVector::RightVector;
		}
		const int32 B = V.Num();
		V.Add(C - Right * HX - Up * HY); V.Add(C + Right * HX - Up * HY);
		V.Add(C + Right * HX + Up * HY); V.Add(C - Right * HX + Up * HY);
		for (int k = 0; k < 4; ++k) { Nrm.Add(Fwd); Col.Add(RGBA); }
		// UV-регион (атлас 0x17: ячейка кадра; дефолт [0..1]). Порядок как исходный (0,1)(1,1)(1,0)(0,0).
		UV.Add(FVector2D(UvMin.X, UvMax.Y)); UV.Add(FVector2D(UvMax.X, UvMax.Y));
		UV.Add(FVector2D(UvMax.X, UvMin.Y)); UV.Add(FVector2D(UvMin.X, UvMin.Y));
		I.Add(B); I.Add(B + 2); I.Add(B + 1); I.Add(B); I.Add(B + 3); I.Add(B + 2);
	};

	if (E.bSingleQuad)
	{
		// 0xB/0xC/0x18: ОДИН МИРОВОЙ КВАД (НЕ camera-billboard! аудит: движок @0x120AEB0 использует ту же
		// матрицу, что кольцо: Эйлер-каналы узла + POS/BASEPOS-офсет; камера в коде отсутствует).
		const FTransform Xf = GetActorTransform();
		const float DegR = PI / 180.f;
		auto RX=[](const FVector& P,float A){float c=FMath::Cos(A),s=FMath::Sin(A);return FVector(P.X,P.Y*c+P.Z*s,-P.Y*s+P.Z*c);};
		auto RY=[](const FVector& P,float A){float c=FMath::Cos(A),s=FMath::Sin(A);return FVector(P.X*c-P.Z*s,P.Y,P.X*s+P.Z*c);};
		auto RZ=[](const FVector& P,float A){float c=FMath::Cos(A),s=FMath::Sin(A);return FVector(P.X*c+P.Y*s,-P.X*s+P.Y*c,P.Z);};
		// ★ Порог размера — параметр `asda.Fx.MinQuadSize` (дефолт 0 = как эталон); раньше здесь стоял
		//   жёсткий пол 1.0, см. шапку файла у CVarAsdaFxMinQuadSize.
		const float MinQ = FMath::Max(CVarAsdaFxMinQuadSize.GetValueOnGameThread(), 0.f);
		const float SX=FMath::Max(Cur.SizeX,MinQ)*EffectEmitterScale, SY=FMath::Max(Cur.SizeY,MinQ)*EffectEmitterScale;
		// ★ Отображение game→UE углов/basePos ЗАВИСИТ от того, что квад "лицом к холдеру":
		//   * Карты (bBaseAlreadyUE=false, верифиц. 48/48): стандартный своп UE=(-Gz,Gx,Gy), basePos добавляется в МИРЕ.
		//   * Char-select глифы (bBaseAlreadyUE=true): база = КОСТЬ пьедестала шкатулки (замер geom_query.log). Скелет-импорт
		//     кладёт локальные оси кости как: game-Y(верх)→кость-Z, game-Z(нормаль/на камеру)→кость-Y, game-X(гориз)→кость-X.
		//     ⇒ UE_local=(Gx,Gz,Gy). Тогда нормаль квада = кость-Y, а для центр. CH_002 кость-Y=(0.3,-0.9,-0.2)=НА камеру
		//     (LoginCamera fwd=(0,1,0)) → глиф смотрит на игрока (было: нормаль на кость-X=вбок=ребром=невидим).
		const FVector BPue = bBaseAlreadyUE
			? FVector(Cur.RingBasePos.X, Cur.RingBasePos.Z, Cur.RingBasePos.Y)   // game→кость-локаль (своп Y↔Z)
			: AsdaCoord::GameToUe3D(Cur.RingBasePos); // ★ Аудит №3 (B1); карты: стандартный своп
		// ★ FxAlphaScale — рантайм-множитель всего эффекта (GM-«скрыться»), дефолт 1 = данные как есть.
		const FLinearColor QC(Cur.Color.R, Cur.Color.G, Cur.Color.B, FMath::Clamp(Cur.Alpha * FxAlphaScale, 0.f, 1.f));
		// ★ АТЛАС-ФЛИПБУК ОДИНОЧНОГО КВАДА (типы 0x13/0x14/0x18/0x19; реверс weapon_glow_re.md §D:
		//   quadEmitterBuildUVFrames@0x123DE50 — total=cols·rows, frameDur=life/total, кадр=elapsed/frameDur,
		//   col=кадр%cols, row=кадр/cols; UV ячейки = (col·1/cols … (col+1)·1/cols) × то же по V).
		//   Cols=Rows=1 (дефолт и ВСЕ ранее собранные квады 0xB/0xC/0x18) → полный UV [0..1], поведение
		//   прежних эффектов НЕ меняется.
		const int32 QAC = FMath::Max(E.AtlasCols, 1), QAR = FMath::Max(E.AtlasRows, 1);
		const int32 QFrames = QAC * QAR;
		FVector2D QUvMin(0.f, 0.f), QUvMax(1.f, 1.f);
		if (QFrames > 1)
		{
			const float QPer = FMath::Max(E.Period, 1e-3f);
			const float QPh = FMath::Fmod(FMath::Max(S.FlipTime, 0.f), QPer) / QPer;   // ★ ЦИКЛ 1:1: время флипбука (== LifeTimer на прежнем пути)
			const int32 QFrame = FMath::Clamp(FMath::FloorToInt(QPh * QFrames), 0, QFrames - 1);
			const int32 QCol = QFrame % QAC, QRow = QFrame / QAC;
			QUvMin = FVector2D((float)QCol / QAC, (float)QRow / QAR);
			QUvMax = FVector2D((float)(QCol + 1) / QAC, (float)(QRow + 1) / QAR);
		}
		// ★ 0xB карты = ПЛОСКИЙ квад в плоскости ЗЕМЛИ (game XZ, нормаль +Y=up), CONFIRMED дизасм 0x120AEB0
		//   (V=(±scaleX,0,±scaleY), y=0). Порт раньше делал XY (вертикаль) → квад повёрнут на 90° = неверная форма.
		//   Глиф char-select (bBaseAlreadyUE) = билборд к пьедесталу — плоскость XY (не трогаем).
		FVector Corners[4];
		if (bBaseAlreadyUE) {
			Corners[0]=FVector(-SX,-SY,0); Corners[1]=FVector(SX,-SY,0); Corners[2]=FVector(SX,SY,0); Corners[3]=FVector(-SX,SY,0);
		} else {
			// bFlipQuadV: меняем знак вертикальной полуоси — квад отражается по V (см. заголовок).
			const float VS = bFlipQuadV ? -SY : SY;
			Corners[0]=FVector(-SX,0,-VS); Corners[1]=FVector(SX,0,-VS); Corners[2]=FVector(SX,0,VS); Corners[3]=FVector(-SX,0,VS);
		}
		// ★ 0xC = линейный keyframe-трек (НЕ синус): вращение/alpha/размер маршируются между узлами (START/ключ/END,
		//   loop за period). Синус-«качание» убран — движение задаёт трек-канал rot (Cur.RingRot, node idx28/30/32),
		//   мерцание — трек-канал alpha (Cur.Alpha, node idx13: 1→0→1). Эталон effectQuad_updateAndDrawBillboard@0x120AEB0.
		const int32 B=V.Num();
		// ★ КАРТЫ, ДВА офсет-канала (лоадер-трейс effect_loaders_pertype.md, CONFIRMED):
		//   pos-канал (RingPosOfs, диск 0x150/0x15C) — ДО вращений, через Xf → вращается канальными вращениями
		//     (орбита flooer: s==E=(300,48,0) конст + спин 0..360) и авторской матрицей узла (дрейф волны
		//     «наружу» от СВОЕГО динамика). Лерп s→E = дрейф (wave вверх 75→300, pado к берегу 0→-600Z).
		//   basePos-канал (RingBasePos, диск 0x12C/0x138) — СЫРОЙ МИР (дизасм: перезапись трансляции M._41-43;
		//     не вращается ничем) ×|actorScale|: разнос пятен flooer em2-5 (±200 по game-X).
		const float BpScale = FMath::Abs(Xf.GetScale3D().X);
		const FVector BPraw = Cur.RingBasePos;
		const FVector BPrawUE = bBaseAlreadyUE ? FVector::ZeroVector
			: AsdaCoord::GameToUe3D(BPraw) * BpScale;
		// ★ 0xC billboardMode (лоадер: +0x8C; 1/3=сферический биллборд, 2=lookAt up=(0,1,0)). Камера из
		//   точка наблюдателя GetViewLocation (оверрайд превью → AsdaView); без наблюдателя (headless) — фолбэк плоскость земли.
		bool bBillboarded = false;
		if (!bBaseAlreadyUE && E.BillboardMode != 0)
		{
			// ★ волна 3 п.11 (15.09.2026): та же точка, что у кулла и прочих частиц (GetViewLocation: оверрайд превью → AsdaView).
			//   ★ ИСПРАВЛЕНИЕ E7: своя копия в окне редактора брала последний отрисованный вид (кадр назад), а прочие частицы
			//   того же эффекта — вьюпорт уровня; теперь одна точка — перспективный вьюпорт этого мира. В игре — камера, как
			//   было; в мире без камеры и без вьюпорта этого мира — последний отрисованный вид, как было.
			//   ★ 15.09.2026: точка — из снимка наблюдателя шага (тот же SampleView, что у кулла), без своего вызова.
			const FVector CamLoc = View.Location;
			const bool bCam = View.bHasLocation;
			if (bCam)
			{
				FVector PoUE = AsdaCoord::GameToUe3D(Cur.RingPosOfs);   // ★ Аудит №3 (B1)
				// ★ bStrictQuadAttach в BILLBOARD-ветке (ФИКС: раньше флаг здесь НЕ учитывался вообще).
				//   Эталон spriteBuildTransformAndDraw@0x120CF10: при billboardMode!=0 ОРИЕНТАЦИЯ квада
				//   берётся от камеры (rt328: mode1/3 — транспонированная VIEW, mode2 — lookAt с up=(0,1,0)),
				//   но ТРАНСЛЯЦИЯ считается ровно так же, как у не-биллборда (M._41-43 = gap_234[224..232]
				//   = basePos + pos-канал через R·N48·N520 + позиция холдера) — т.е. КАНАЛЬНЫЕ ВРАЩЕНИЯ
				//   RotX/RotY/RotZ участвуют ПОЛНОСТЬЮ. Порт крутил центр только rotY вокруг МИРОВОЙ
				//   вертикали ⇒ у эмиттеров с rotX/rotZ≠0 спрайт садился мимо (у свечения оружия это
				//   Bil_01: rot=(0,0,300°/900°) при pos=(0,12,30)/(0,7,90) — промах ~12 юнитов).
				//   ВАЖНО: эта ветка активна ТОЛЬКО когда есть камера ⇒ в headless-замерах прошлых кругов
				//   она не исполнялась НИ РАЗУ, а в игре исполняется всегда.
				//   Дефолт bStrictQuadAttach = false ⇒ карты/башня alpen/глифы не затронуты.
				//   ★★★ bStrictQuadPivot (2026-09-10) — тот же эталонный расчёт центра, включаемый печкой
				//   карт поэмиттерно. Дефолт false ⇒ прежние карт-биллборды не тронуты.
				if (bStrictQuadAttach || E.bStrictQuadPivot)
				{
					const float ax = Cur.RingRot.X * DegR, ay = Cur.RingRot.Y * DegR, az = Cur.RingRot.Z * DegR;
					auto RXs = [](const FVector& P, float A) { float c = FMath::Cos(A), s = FMath::Sin(A); return FVector(P.X, P.Y*c - P.Z*s, P.Y*s + P.Z*c); };
					auto RYs = [](const FVector& P, float A) { float c = FMath::Cos(A), s = FMath::Sin(A); return FVector(P.X*c + P.Z*s, P.Y, -P.X*s + P.Z*c); };
					auto RZs = [](const FVector& P, float A) { float c = FMath::Cos(A), s = FMath::Sin(A); return FVector(P.X*c - P.Y*s, P.X*s + P.Y*c, P.Z); };
					// счёт в GAME-осях (как в плоской строгой ветке), затем один раз game→UE
					FVector Cg = RZs(RYs(RXs(Cur.RingPosOfs, ax), ay), az);
					Cg = RYs(Cg, HolderAngleQ * DegR);                       // N48 (holder)
					PoUE = AsdaCoord::GameToUe3D(Cg);
				}
				// ★ bStrictQuadOrbit: pos-канал вращается канальным rotY вокруг вертикали (D3DX RotY в ue-осях,
				//   §H6: x'=x·c+y·s, y'=−x·s+y·c) ⇒ ОРБИТА (кольцо соулмейта: 8 квадов, pos=(50,0,0), rotY 0→360).
				//   Внутриплоскостной спин тогда берёт только roll/holder — иначе rotY учтётся дважды.
				else if (E.bStrictQuadOrbit)
				{
					const float Ao = Cur.RingRot.Y * DegR;
					const float co = FMath::Cos(Ao), so = FMath::Sin(Ao);
					PoUE = FVector(PoUE.X * co + PoUE.Y * so, -PoUE.X * so + PoUE.Y * co, PoUE.Z);
				}
				const FVector Center = Xf.TransformPosition(PoUE) + BPrawUE + HolderOfsQ;
				FVector ToCam = (CamLoc - Center).GetSafeNormal();
				FVector Rgt, Up;
				if (E.BillboardMode == 2)
				{	// цилиндрический lookAt: up = мир-вертикаль
					Up = FVector::UpVector;
					Rgt = FVector::CrossProduct(Up, ToCam).GetSafeNormal();
					if (Rgt.IsNearlyZero()) Rgt = FVector::RightVector;
				}
				else
				{	// сферический биллборд
					Up = FVector::UpVector;
					Rgt = FVector::CrossProduct(Up, ToCam).GetSafeNormal();
					if (Rgt.IsNearlyZero()) Rgt = FVector::RightVector;
					Up = FVector::CrossProduct(ToCam, Rgt).GetSafeNormal();
				}
				// in-plane спин: при строгой орбите/строгой посадке rotY уже ушёл в поворот pos-канала (не двоим).
				// ★ Эталон 0x120CF10: при billboardMode!=0 итоговая матрица = RotY(spin)·rt328, где spin =
				//   gap_234[356] = КАНАЛ ROLL. Канальные RotX/RotY/RotZ в ориентацию биллборда НЕ входят
				//   (их матрица R·N48·N520 перезаписывается) — только в трансляцию.
				const float Spin = (Cur.RingRoll + ((E.bStrictQuadOrbit || bStrictQuadAttach || E.bStrictQuadPivot) ? 0.f : Cur.RingRot.Y) + HolderAngleQ) * DegR;
				const float cs=FMath::Cos(Spin), sn=FMath::Sin(Spin);
				const float SXw=SX*BpScale, SYw=SY*BpScale;
				const int32 B2=V.Num();
				for (int k=0;k<4;++k)
				{
					const float lx = (k==0||k==3)?-SXw:SXw;
					const float ly = (k<2)?-SYw:SYw;
					const float rx = lx*cs - ly*sn, ry2 = lx*sn + ly*cs;
					V.Add(Center + Rgt*rx + Up*ry2);
					Nrm.Add(FVector::UpVector); Col.Add(QC);
				}
				// ★ 06.11.2026 [ОРИГИНАЛ]: `Rgt = Cross(Up, ToCam)` в левой системе UE — экранное ЛЕВО, поэтому U шла справа налево
				//   (разбор у `asda.Fx.BillboardUvEtalon`).
				const bool bUvEtalon = CVarAsdaFxBillboardUvEtalon.GetValueOnGameThread() != 0;
				const float ULeft = bUvEtalon ? QUvMax.X : QUvMin.X, URight = bUvEtalon ? QUvMin.X : QUvMax.X;
				UV.Add(FVector2D(ULeft,QUvMax.Y)); UV.Add(FVector2D(URight,QUvMax.Y));
				UV.Add(FVector2D(URight,QUvMin.Y)); UV.Add(FVector2D(ULeft,QUvMin.Y));
				I.Add(B2); I.Add(B2+2); I.Add(B2+1); I.Add(B2); I.Add(B2+3); I.Add(B2+2);
				bBillboarded = true;
			}
		}
		// ★ ATTACH-квад (type-2, tower): кольца того же эффекта проходят RotYv(π)-«швартовку», квад — нет →
		//   наклонный конус-луч и его квад-юбка расходились на 180° по азимуту («восьмёрка», alpen tower;
		//   всплыло после оживления vertex-альфы — раньше квад был невидим). Согласуем: тот же π-разворот
		//   для attach-квадов. Карты (flooer/wave, не-attach) и глифы (bBaseAlreadyUE) не затронуты.
		const bool bAttachedQuad = !bBaseAlreadyUE &&
			(FollowActor != nullptr || (GetRootComponent() && GetRootComponent()->GetAttachParent() != nullptr));
		if (!bBillboarded)
		for (int k=0;k<4;++k)
		{
			FVector G=Corners[k];
			// ★ bStrictQuadAttach (ЭТАЛОН, дефолт ВЫКЛ): pos-канал НЕ участвует в roll-повороте квада и
			//   не «швартуется» на π — ровно как в реконструированной эталонной ветке КОЛЕЦ, где
			//   геометрия идёт через RotY(roll)·R·N48, а центр (pos-канал) — только через R·N48.
			//   Здесь исторически pos прибавлялся к УГЛАМ до roll ⇒ roll крутил и центр (у свечения
			//   заточки roll=180/90 у половины полотен) и π добивал остальное: половина полотен
			//   улетала за рукоять (z −55…−90), половина вбок (x ±60) — «эффект утоплен в модель».
			// ★★★ bStrictQuadPivot (2026-09-10, «эмиттер облетает эффект»): та же эталонная развязка
			//   «углы отдельно, центр отдельно», но включаемая ПЕЧКОЙ КАРТ поэмиттерно (см. шапку флага
			//   в AsdaEffectActor.h). Швартовка на π остаётся привязана к bStrictQuadAttach — квад башни
			//   alpen (attach) её не теряет.
			const bool bStrictAtt = bStrictQuadAttach || E.bStrictQuadPivot;
			if (!bBaseAlreadyUE && !bStrictAtt) G += Cur.RingPosOfs;
			G=RY(G, Cur.RingRoll*DegR);
			FVector C = (!bBaseAlreadyUE && bStrictAtt) ? Cur.RingPosOfs : FVector::ZeroVector;
			{
				// Знак углов узла: RH-игра vs наша формула (см. bInvertNodeRot).
				const float RS = bInvertNodeRot ? -1.f : 1.f;
				G=RX(G, Cur.RingRot.X*DegR*RS); G=RY(G, Cur.RingRot.Y*DegR*RS); G=RZ(G, Cur.RingRot.Z*DegR*RS);
				// ★ Центр (pos-канал) при строгой посадке крутится ЭТАЛОННЫМИ D3DX row-vector ротациями
				//   (дизасм D3DXMatrixRotationX/Y/Z @0xFF4ADD/0xFF4BDA/0xFF4CED) — как в эталонной ветке колец.
				//   ПРОВЕРКА геометрией (лук bow_a_co_ma_03, эмиттер sp_3: pos=(10,0,40), rotY=−20°):
				//   D3DX даёт центр (−4.3, 0, 41.0), а сечение меша лука при z∈[30,50] лежит при x∈[−6.6..2.3]
				//   (среднее −3.8) — попадание в плечо лука; историч. транспонированная формула давала
				//   (+23.1, 0, 34.2) = мимо модели на 27 юнитов.
				const float ax=Cur.RingRot.X*DegR, ay=Cur.RingRot.Y*DegR, az=Cur.RingRot.Z*DegR;
				auto RXd=[](const FVector& P,float A){float c=FMath::Cos(A),s=FMath::Sin(A);return FVector(P.X,P.Y*c-P.Z*s,P.Y*s+P.Z*c);};
				auto RYd=[](const FVector& P,float A){float c=FMath::Cos(A),s=FMath::Sin(A);return FVector(P.X*c+P.Z*s,P.Y,-P.X*s+P.Z*c);};
				auto RZd=[](const FVector& P,float A){float c=FMath::Cos(A),s=FMath::Sin(A);return FVector(P.X*c-P.Y*s,P.X*s+P.Y*c,P.Z);};
				C=RZd(RYd(RXd(C, ax), ay), az);
				C=RYd(C, HolderAngleQ*DegR);                         // N48 (holder)
			}
			// ★ швартовка на π привязана ИМЕННО к акторному bStrictQuadAttach (не к bStrictAtt): у квада
			//   башни alpen (attach-размещение) она подобрана визуально и снятие пивот-флагом её не трогает.
			if (bAttachedQuad && !bStrictQuadAttach) G=RY(G, PI);    // швартовка = как у колец (соосность пары)
			G=RY(G, HolderAngleQ*DegR);                              // header-спин холдера
			G += C;                                                  // центр квада (pos-канал) — без roll/π
			// ★ хиральное отражение геометрии (см. ring-ветку): зеркальный носитель type-2
			if (!ChiralNormal.IsNearlyZero())
			{
				G = G - 2.f * FVector::DotProduct(G, ChiralNormal) * ChiralNormal;
			}
			if (bBaseAlreadyUE)
			{
				// кость пьедестала уже UE: локаль=(Gx,Gz,Gy), basePos локальным офсетом, Xf несёт скейл+ориентацию.
				// (глиф char-select ПОДТВЕРЖДЁН — путь не меняем: офсет после вращений)
				const FVector UE = FVector(G.X, G.Z, G.Y);
				V.Add(Xf.TransformPosition(UE + BPue) + HolderOfsQ);
			}
			else
			{
				const FVector UE = AsdaCoord::GameToUe3D(G);
				V.Add(Xf.TransformPosition(UE) + BPrawUE + HolderOfsQ);
			}
			Nrm.Add(FVector::UpVector); Col.Add(QC);
		}
		// UV-оффсет single-quad = ЛЕРП узловых значений (статичный сид; у flooer 1.0≡0 wrap-но-оп). НЕ аккумулятор.
		if (!bBillboarded)
		{
			const float Uo = Cur.RingU, Vo = Cur.RingV;
			// ★ 05.11.2026 (`asda.Fx.AttachQuadUvEtalon`): у эффектов на кости углы 0/1 (z=−size) — верх текстуры (эталон 0x1208B40).
			//   Остальные плоские квады — ключ `asda.Fx.FlatQuadUvEtalon` (кроме метки клика с ручным `bFlipQuadV`).
			const bool bEtalonV = bStrictQuadAttach
				? CVarAsdaFxAttachQuadUvEtalon.GetValueOnGameThread() != 0
				: (!bFlipQuadV && !bBaseAlreadyUE && CVarAsdaFxFlatQuadUvEtalon.GetValueOnGameThread() != 0);
			const float VLo = bEtalonV ? QUvMin.Y : QUvMax.Y;   // V углов 0/1 (z=−size)
			const float VHi = bEtalonV ? QUvMax.Y : QUvMin.Y;   // V углов 2/3 (z=+size)
			UV.Add(FVector2D(QUvMin.X+Uo,VLo+Vo)); UV.Add(FVector2D(QUvMax.X+Uo,VLo+Vo));
			UV.Add(FVector2D(QUvMax.X+Uo,VHi+Vo)); UV.Add(FVector2D(QUvMin.X+Uo,VHi+Vo));
			I.Add(B); I.Add(B+2); I.Add(B+1); I.Add(B); I.Add(B+3); I.Add(B+2);
		}
	}
	else
	{
		// ★ Размер частицы МАСШТАБИРУЕТСЯ трансформом актора (как квад глифа): позиция P.Pos уже прошла
		//   Xf.TransformPosition (×actorScale ≈100 для char-select сцены), а размер — нет → частицы были в ~100×
		//   мельче глифа и невидимы. Домножаем на |actorScale| (в оригинале глиф и частица в одной world-матрице).
		// ОТКАТ 2026-07-21: ×|actorScale| для карт → дым ×58 = пол-карты (масштаб кости дирижабля не должен идти в
		//   размер частицы). Возврат к ×1 для карт до реверса истинного scaleAll s (агент a6d65edacb0dc6cea).
		const float PScale = EffectEmitterScale * (bBaseAlreadyUE ? FMath::Abs(GetActorScale3D().X) : 1.f);
		const float MinP = FMath::Max(CVarAsdaFxMinParticleSize.GetValueOnGameThread(), 0.f);   // порог размера частицы
		// ★ Атлас-раскадровка UV (тип 0x17 «блёстки» / 0x1B / 0x13): кадр = Age/(Life/(Cols·Rows)) — один
		//   проход атласа за жизнь частицы; col=кадр%Cols, row=кадр/Cols. Cols=Rows=1 → полный UV (0xD не тронут).
		const int32 AC = FMath::Max(E.AtlasCols, 1), AR = FMath::Max(E.AtlasRows, 1);
		const int32 Frames = AC * AR;
		const bool bAtlas = Frames > 1;
		const float DU = 1.f / AC, DV = 1.f / AR;
		// ★ 15.10.2026: облако «в кадре носителя» (RestVerts, см. .h) — только у эффекта, следующего за носителем;
		//   квад частицы сдвигается на (текущий · рождения⁻¹ · Pos − Pos). Стенд сверки (Capture) не пишет.
		const bool bRest = !Capture && bRestFrameWanted;
		TArray<FVector>* RestOut = nullptr;
		if (bRest)
		{
			if (RestVerts.Num() < Emitters.Num()) { RestVerts.SetNum(Emitters.Num()); }
			if (RestVerts.IsValidIndex(SectionIdx)) { RestOut = &RestVerts[SectionIdx]; RestOut->Reset(); }
		}
		else if (!Capture && RestVerts.IsValidIndex(SectionIdx)) { RestVerts[SectionIdx].Reset(); }
		const FTransform NowXf = GetActorTransform();
		for (const FAsdaFxParticle& P : S.Slots)
		{
			if (P.Age < 0.f) continue;
			const int32 QuadBase = V.Num();   // ★ 15.10.2026: первая вершина квада этой частицы (для RestVerts)
			FVector2D UvMin(0.f, 0.f), UvMax(1.f, 1.f);
			if (bAtlas)
			{
				int32 Frame = FMath::FloorToInt((P.Age / FMath::Max(P.Life, 1e-4f)) * Frames);
				Frame = FMath::Clamp(Frame, 0, Frames - 1);
				const int32 CCol = Frame % AC, CRow = Frame / AC;
				UvMin = FVector2D(CCol * DU, CRow * DV);
				UvMax = FVector2D((CCol + 1) * DU, (CRow + 1) * DV);
			}
			// ★ Порог размера частицы — параметр `asda.Fx.MinParticleSize` (дефолт 0 = как эталон);
			//   раньше здесь стоял жёсткий пол 0.5, см. шапку файла у CVarAsdaFxMinParticleSize.
			//   Считаем один раз на секцию — значение читается выше, у объявления MinP.
			AddQuad(P.Pos, FMath::Max(P.SX, MinP) * PScale, FMath::Max(P.SY, MinP) * PScale,
			        FLinearColor(FMath::Clamp(P.RGB.X, 0.f, 1.f), FMath::Clamp(P.RGB.Y, 0.f, 1.f),
			                     FMath::Clamp(P.RGB.Z, 0.f, 1.f),
			                     FMath::Clamp(P.Alpha * ParticleAlphaScale * FxAlphaScale, 0.f, 1.f)),   // плотность (тюнинг) × рантайм-множитель (GM-hide)
			        UvMin, UvMax);
			if (RestOut)
			{
				// Где была бы эта частица, если бы носитель стоял: текущий трансформ · (трансформ рождения)⁻¹ · Pos.
				const FTransform BirthXf(P.BirthRot, P.BirthLoc, NowXf.GetScale3D());
				const FVector D = NowXf.TransformPosition(BirthXf.InverseTransformPosition(P.Pos)) - P.Pos;
				for (int32 k = QuadBase; k < V.Num(); ++k) { RestOut->Add(V[k] + D); }
			}
		}
		// ★ СТАБИЛЬНАЯ ТОПОЛОГИЯ (фикс «весь дым мерцает», 2026-07-28): число вершин менялось на каждом
		//   спавне/смерти частицы → CreateMeshSection вместо Update → пересоздание рендер-прокси PMC
		//   20-30 раз/с = кадровое мерцание дыма. Оригинал стримит вершины (DrawPrimitiveUP), объект не
		//   пересоздаёт. Паддинг мёртвых слотов вырожденными квадами (4 совпадающие вершины в позиции
		//   актора, VC=0: additive +0, modulate 1−0=1 → нейтрально, alpha op=0) до high-water секции:
		//   после прогрева count постоянен → всегда UpdateMeshSection, прокси живёт.
		//   Ёмкость = максимум живых частиц (max node-rate × life, кламп kSlots) — топология постоянна с
		//   ПЕРВОГО коммита (без Create-серий в фазе роста после загрузки уровня).
		int32 CapQuads = 0;
		for (const FAsdaFxNode& Nd : E.Nodes)
			CapQuads = FMath::Max(CapQuads, FMath::CeilToInt(Nd.Rate * FMath::Max(E.LifeBase + E.LifeVar, 0.05f)));
		CapQuads = FMath::Clamp(CapQuads + 1, 1, kSlots);
		const int32 Committed = SectionVerts.IsValidIndex(SectionIdx) ? SectionVerts[SectionIdx] : -1;
		const int32 PadTarget = FMath::Max(Committed, CapQuads * 4);
		const FVector PadPos = GetActorLocation();
		while (V.Num() < PadTarget)
		{
			const int32 B = V.Num();
			for (int k = 0; k < 4; ++k) { V.Add(PadPos); Nrm.Add(FVector::UpVector); Col.Add(FLinearColor(0, 0, 0, 0)); }
			UV.Add(FVector2D(0, 1)); UV.Add(FVector2D(1, 1)); UV.Add(FVector2D(1, 0)); UV.Add(FVector2D(0, 0));
			I.Add(B); I.Add(B + 2); I.Add(B + 1); I.Add(B); I.Add(B + 3); I.Add(B + 2);
			if (RestOut) { for (int k = 0; k < 4; ++k) { RestOut->Add(PadPos); } }   // паддинг стоит у актора — он и так едет с носителем
		}
	}

	if (Capture)
	{
		// Стенд сверки: только буферы геометрии (с тем же разворотом обхода), секции меша не трогаем.
		if (V.Num() > 0) { FlipWindingIfMirrored(I, GetActorTransform().GetScale3D()); }
		Capture->V = MoveTemp(V); Capture->I = MoveTemp(I); Capture->Nrm = MoveTemp(Nrm);
		Capture->UV = MoveTemp(UV); Capture->Col = MoveTemp(Col);
		return;
	}
	if (V.Num() == 0)
	{
		ClearFxSection(SectionIdx);
		return;
	}
	FlipWindingIfMirrored(I, GetActorTransform().GetScale3D());
	if (SectionVerts.IsValidIndex(SectionIdx) && SectionVerts[SectionIdx] == V.Num())
	{
		AsdaFxUpdateSection(Mesh, SectionIdx, V, Nrm, UV, Col);
	}
	else
	{
		AsdaFxCreateSection(Mesh, SectionIdx, V, I, Nrm, UV, Col);
		if (E.Material) Mesh->SetMaterial(SectionIdx, GlowMaterialFor(SectionIdx, E.Material));   // ★ Ф5: через «силу свечения»
		if (SectionVerts.IsValidIndex(SectionIdx)) SectionVerts[SectionIdx] = V.Num();
	}
	// ★ Парная секция ModAdd (D3D 6:4, декомпозиция dst×(1−srcC) + srcC×(1−srcA)): та же геометрия/цвет,
	//   материал MaterialB (additive-добавка). Секции PMC рисуются по индексу → Modulate (i) раньше Add (N+i),
	//   как в однопроходном D3D-бленде. Формулы разводит материал (VC общий).
	if (E.MaterialB)
	{
		const int32 Pair = Emitters.Num() + SectionIdx;
		if (SectionVerts.IsValidIndex(Pair) && SectionVerts[Pair] == V.Num())
		{
			AsdaFxUpdateSection(Mesh, Pair, V, Nrm, UV, Col);
		}
		else
		{
			AsdaFxCreateSection(Mesh, Pair, V, I, Nrm, UV, Col);
			Mesh->SetMaterial(Pair, GlowMaterialFor(Pair, E.MaterialB));   // ★ Ф5: то же для парной ModAdd-секции
			if (SectionVerts.IsValidIndex(Pair)) SectionVerts[Pair] = V.Num();
		}
	}
}
