// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor_DevStands.cpp — ★ R2 10.11.2026 (разрез AsdaEffectActor.cpp, дословный перенос): дев-стенды/пробы: проверка вида, движение, петли, снимки, альфа-зажим, дамп секций/материалов.
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

#if !UE_BUILD_SHIPPING
static FAsdaFxViewCheckStats GAsdaFxViewCheck;

// ★ СТЕНД СВЕРКИ (ключ asda.Fx.ViewSampleCheck, шапка файла). Зовётся из SimulateStep ПЕРЕД боевой сборкой секции —
//   ровно в тот момент, где до 15.09.2026 BuildQuads сам спрашивал наблюдателя. Ничего не меняет: две сборки идут
//   в буферы (Capture), состояние эмиттера сборкой не пишется (только чтение S.Slots/S.LifeTimer/SectionVerts).
void AAsdaEffectActor::ViewSampleStandCheck(int32 SectionIdx, const FAsdaFxEmitter& E, FAsdaFxEmitterState& S,
                                            const FVector& ViewLoc, const FAsdaFxViewSample& View, const FAsdaFxNode& Cur)
{
	// ПРЕЖНИЙ ПУТЬ ВЫБОРКИ — символ в символ код BuildQuads до 15.09.2026 (_backup/2026-09-15_asdaview_quads):
	//   поворот: bUseViewOverride → ViewRotationOverride, иначе отдельный AsdaView::GetViewPoint с bHasRotation;
	//   точка биллборда 0xC: прежний GetViewLocation (bUseViewOverride → ViewLocationOverride, иначе AsdaView::GetViewLocation).
	FAsdaFxViewSample Old;
	if (bUseViewOverride)
	{
		Old.Rotation = ViewRotationOverride;
		Old.bHasRotation = true;
	}
	else
	{
		FAsdaViewPoint VP;
		if (AsdaView::GetViewPoint(GetWorld(), VP) && VP.bHasRotation)
		{
			Old.Rotation = VP.Rotation;
			Old.bHasRotation = true;
		}
	}
	FVector OldCamLoc = FVector::ZeroVector;
	const bool bOldCam = bUseViewOverride
		? (OldCamLoc = ViewLocationOverride, true)
		: AsdaView::GetViewLocation(GetWorld(), OldCamLoc);
	Old.Location = OldCamLoc;
	Old.bHasLocation = bOldCam;

	const bool bSampleSame = Old.bHasLocation == View.bHasLocation && Old.bHasRotation == View.bHasRotation
		&& FMemory::Memcmp(&Old.Location, &View.Location, sizeof(FVector)) == 0
		&& FMemory::Memcmp(&Old.Rotation, &View.Rotation, sizeof(FRotator)) == 0;

	FAsdaFxQuadCapture A, B;
	BuildQuads(SectionIdx, E, S, ViewLoc, Old, Cur, &A);    // геометрия с прежней выборкой
	BuildQuads(SectionIdx, E, S, ViewLoc, View, Cur, &B);   // геометрия со снимком шага
	auto SameBytes = [](const auto& X, const auto& Y)
	{
		return X.Num() == Y.Num()
			&& (X.Num() == 0 || FMemory::Memcmp(X.GetData(), Y.GetData(), (SIZE_T)X.Num() * sizeof(X[0])) == 0);
	};
	const bool bBufSame = SameBytes(A.V, B.V) && SameBytes(A.I, B.I) && SameBytes(A.Nrm, B.Nrm)
		&& SameBytes(A.UV, B.UV) && SameBytes(A.Col, B.Col);

	FAsdaFxViewCheckStats& St = GAsdaFxViewCheck;
	++St.Sections;
	St.Verts += (uint64)B.V.Num();
	if (View.bHasLocation) { ++St.WithLocation; }
	if (View.bHasRotation) { ++St.WithRotation; }
	if (bUseViewOverride) { ++St.Override; }
	if (E.bSingleQuad && E.BillboardMode != 0 && !bBaseAlreadyUE && View.bHasLocation) { ++St.Billboard; }
	if (const UWorld* W = GetWorld()) { if (W->IsGameWorld()) { ++St.GameWorld; } }
	if (!bSampleSame) { ++St.SampleMismatch; }
	if (!bBufSame) { ++St.BufferMismatch; }
	if ((!bSampleSame || !bBufSame) && St.SampleMismatch + St.BufferMismatch <= 20)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[наблюдатель-квады] РАСХОЖДЕНИЕ %s эм.%d тип 0x%X: снимок %s | прежняя точка=%s (есть=%d) поворот=%s (есть=%d) | снимок точка=%s (есть=%d) поворот=%s (есть=%d) | вершин %d/%d"),
			*GetName(), SectionIdx, E.JebType, bSampleSame ? TEXT("совпал") : TEXT("РАЗОШЁЛСЯ"),
			*Old.Location.ToString(), Old.bHasLocation ? 1 : 0, *Old.Rotation.ToString(), Old.bHasRotation ? 1 : 0,
			*View.Location.ToString(), View.bHasLocation ? 1 : 0, *View.Rotation.ToString(), View.bHasRotation ? 1 : 0,
			A.V.Num(), B.V.Num());
	}
}

// Печать итогов стенда (и сброс по аргументу reset)
static void AsdaFxViewCheckReport(const TCHAR* Tag, bool bReset)
{
	const FAsdaFxViewCheckStats& St = GAsdaFxViewCheck;
	UE_LOG(LogTemp, Warning,
		TEXT("[наблюдатель-квады] ИТОГ «%s»: сборок секций=%llu вершин=%llu | снимок≠прежней выборки=%llu | буферы разошлись=%llu | с точкой=%llu с поворотом=%llu оверрайд=%llu биллборд0xC=%llu игровой мир=%llu"),
		Tag, St.Sections, St.Verts, St.SampleMismatch, St.BufferMismatch,
		St.WithLocation, St.WithRotation, St.Override, St.Billboard, St.GameWorld);
	if (bReset) { GAsdaFxViewCheck = FAsdaFxViewCheckStats(); }
}

static FAutoConsoleCommand GAsdaFxViewSampleCheckReport(   // перегрузка с FConsoleCommandWithArgsDelegate
	TEXT("asda.Fx.ViewSampleCheck.Report"),
	TEXT("asda.Fx.ViewSampleCheck.Report [reset] — итоги стенда asda.Fx.ViewSampleCheck (сборок, расхождений снимка и буферов)"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A)
	{
		AsdaFxViewCheckReport(TEXT("отчёт"), A.Num() > 0 && A[0].Equals(TEXT("reset"), ESearchCase::IgnoreCase));
	}));

// ★ Прогон стенда на заспавненном эффекте ТЕКУЩЕГО мира: спавн (в неигровом мире — прогрев OnConstruction),
//   N тиков без оверрайда (правило AsdaView), затем оверрайд превью + RerunConstructionScripts (прогрев с
//   оверрайдом, как у браузера предметов) и ещё N тиков. Кулл снят, как у asda.Fx.MotionProbe. Эффект удаляется.
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxViewSampleCheckStand(
	TEXT("asda.Fx.ViewSampleCheck.Stand"),
	TEXT("asda.Fx.ViewSampleCheck.Stand <база .jeb | BP_имя> [тиков=120] — сверка точки наблюдателя в сборке квадов: прогрев, тики без оверрайда и с оверрайдом превью"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W || A.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[наблюдатель-квады] использование: asda.Fx.ViewSampleCheck.Stand <база .jeb | BP_имя> [тиков]"));
			return;
		}
		const int32 Ticks = (A.Num() > 1) ? FMath::Clamp(FCString::Atoi(*A[1]), 1, 3600) : 120;
		const int32 PrevKey = GAsdaFxViewSampleCheck;
		GAsdaFxViewSampleCheck = 1;
		AsdaFxViewCheckReport(TEXT("до стенда (сброс)"), true);
		const bool bRawBP = A[0].StartsWith(TEXT("BP_"));
		AAsdaEffectActor* Fx = bRawBP
			? AAsdaEffectActor::SpawnByBPName(W, A[0], FVector::ZeroVector, FRotator::ZeroRotator, nullptr)
			: AAsdaEffectActor::SpawnByJebName(W, A[0], FVector::ZeroVector, FRotator::ZeroRotator, nullptr);
		if (!Fx)
		{
			GAsdaFxViewSampleCheck = PrevKey;
			UE_LOG(LogTemp, Warning, TEXT("[наблюдатель-квады] эффект %s не найден"), *A[0]);
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("[наблюдатель-квады] стенд %s мир=%s игровой=%d эмиттеров=%d"),
			*Fx->GetName(), *W->GetName(), W->IsGameWorld() ? 1 : 0, Fx->Emitters.Num());
		AsdaFxViewCheckReport(TEXT("прогрев при спавне"), true);
		Fx->CullDistance = 1.0e9f;
		for (int32 t = 0; t < Ticks; ++t) { Fx->Tick(1.f / 60.f); }
		AsdaFxViewCheckReport(TEXT("тики без оверрайда (AsdaView)"), true);
		Fx->bUseViewOverride = true;
		Fx->ViewLocationOverride = FVector(0.f, -1000.f, 300.f);
		Fx->ViewRotationOverride = FRotator(0.f, 90.f, 0.f);
#if WITH_EDITOR
		// ★ Ревизия 15.09: RerunConstructionScripts есть только в редакторской сборке (Actor.h, WITH_EDITOR) —
		//   без охраны не компилировалась игровая цель Development/Test
		Fx->RerunConstructionScripts();
		AsdaFxViewCheckReport(TEXT("прогрев с оверрайдом"), true);
#else
		UE_LOG(LogTemp, Warning, TEXT("[наблюдатель-квады] прогрев с оверрайдом пропущен: RerunConstructionScripts только в редакторе"));
#endif
		Fx->CullDistance = 1.0e9f;
		for (int32 t = 0; t < Ticks; ++t) { Fx->Tick(1.f / 60.f); }
		AsdaFxViewCheckReport(TEXT("тики с оверрайдом"), true);
		Fx->Destroy();
		GAsdaFxViewSampleCheck = PrevKey;
		UE_LOG(LogTemp, Warning, TEXT("[наблюдатель-квады] СТЕНД ЗАВЕРШЁН %s"), *A[0]);
	}));
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ЗОНД ДВИЖЕНИЯ ЭМИТТЕРОВ (второй метод к структурной сверке, закон `false-negatives-method`).
//   «Поля в BP лежат» ещё не значит «в рантайме что-то поехало»: зонд СПАВНИТ настоящий BP,
//   гоняет НАСТОЯЩИЙ `Tick` (60 Гц) и печатает мировую точку эмиссии каждого эмиттера на
//   нескольких моментах времени. Если точка на всех отсчётах одна — эмиттер стоит (это и был баг).
//   Запуск: `asda.Fx.MotionProbe eff_0_char_99_pmbuff_a_06 [секунды=2] [strict 0/1]`.
//   ★ Третий аргумент — акторный `bStrictQuadAttach` (эталонная ветка квада/кольца, `asda.Item.StrictFx`):
//     прогнав зонд с 0 и с 1, получаем числа «до/после» по геометрии, а не «включим и посмотрим».
//   ★ Кроме точки эмиссии печатается ЦЕНТРОИД построенной секции — у квадов и колец точка эмиссии
//     не используется вовсе, их сдвиг виден только по вершинам.
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxMotionProbe(
	TEXT("asda.Fx.MotionProbe"),
	TEXT("asda.Fx.MotionProbe <база .jeb> [секунды=2] [strict 0/1] [масштаб актора=1] — заспавнить "
	     "BP_Fx_<база>, прогнать Tick 60 Гц и напечатать по каждому эмиттеру точку эмиссии, центроид "
	     "и РАДИУС геометрии на 5 отсчётах"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W || A.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[зонд-движения] использование: asda.Fx.MotionProbe <база .jeb> [сек] [strict]"));
			return;
		}
		const float Secs = (A.Num() > 1) ? FMath::Clamp(FCString::Atof(*A[1]), 0.1f, 30.f) : 2.f;
		const bool  bStrict = (A.Num() > 2) ? (FCString::Atoi(*A[2]) != 0) : true;
		// ★ ЧЕТВЁРТЫЙ АРГУМЕНТ — МАСШТАБ АКТОРА (2026-08-14). Эталонный `CEffectMgr_scaleAll` в порту =
		//   `SetActorScale3D`, и прогон зонда с двумя значениями даёт числа «до/после» ЖИВЫМ тиком,
		//   а не арифметикой на бумаге (`asda.Fx.MotionProbe <jeb> 0.5 1 1` против `… 0.5 1 2`).
		const float ActorScale = (A.Num() > 3) ? FMath::Clamp(FCString::Atof(*A[3]), 0.01f, 100.f) : 1.f;
		// ★ Аргумент — либо база `.jeb` (обычный случай, ассет `BP_Fx_<база>`), либо ГОТОВОЕ имя BP.
		//   Второе нужно импактам боя: они собираются `asda_build_combat_impacts.py` под РУЧНЫМИ
		//   именами (`BP_ImpactMob`, `BP_ImpactBattk01..04`), схема `BP_Fx_<jeb>` к ним неприменима.
		const bool bRawBP = A[0].StartsWith(TEXT("BP_"));
		AAsdaEffectActor* Fx = bRawBP
			? AAsdaEffectActor::SpawnByBPName(W, A[0], FVector::ZeroVector, FRotator::ZeroRotator, nullptr)
			: AAsdaEffectActor::SpawnByJebName(W, A[0], FVector::ZeroVector, FRotator::ZeroRotator, nullptr);
		if (!Fx)
		{
			UE_LOG(LogTemp, Warning, TEXT("[зонд-движения] %s не найден"),
				bRawBP ? *A[0] : *AsdaAssetPaths::FxBpName(A[0]));   // ★ волна 2 п. 7
			return;
		}
		// Кулл по расстоянию отключаем: зонд не про камеру, а про арифметику движения.
		Fx->CullDistance = 1.0e9f;
		Fx->bStrictQuadAttach = bStrict;
		if (!FMath::IsNearlyEqual(ActorScale, 1.f)) { Fx->SetActorScale3D(FVector(ActorScale)); }
		// ★ ЭМУЛЯЦИЯ ПРИВЯЗКИ. Ветки `bAttachedQuad` (π-швартовка) и `bAttachedMap` (частицы в мировых
		//   осях) гейтятся наличием НОСИТЕЛЯ (`FollowActor != nullptr`). Без него зонд мерил бы код,
		//   который в игре у эффектов предметов не исполняется. `bFollowPosition` остаётся false ⇒
		//   слежение позицией в Tick не включается и трансформ актора остаётся единичным.
		Fx->FollowActor = Fx;
		// ★ Камера-переопределение ОБЯЗАТЕЛЬНО: без неё биллборд-ветка квада (116 из 169 квадов
		//   канала предметов) в headless не исполняется вовсе и зонд мерил бы не тот код.
		Fx->bUseViewOverride = true;
		Fx->ViewLocationOverride = FVector(0.f, -1000.f, 300.f);
		Fx->ViewRotationOverride = FRotator(0.f, 90.f, 0.f);
		// ★ В РЕДАКТОРСКОМ мире `OnConstruction` делает ПРОГРЕВ 90×1/30 = 3 с (чтобы геометрия была
		//   видна без Realtime-вьюпорта). Для зонда это исказило бы отсчёт t0 — сбрасываем на ноль.
		Fx->RestartEmitters();
		const int32 Steps = FMath::Max(1, FMath::RoundToInt(Secs * 60.f));
		const int32 Marks = 5;
		TArray<TArray<FVector>> Track, Geom;   // [отсчёт][эмиттер]
		// ★ РАДИУС секции на каждом отсчёте: центроид радиально-симметричного кольца при росте
		//   радиуса НЕ движется, поэтому «юбка растёт / стоит» видно только по этому числу.
		TArray<TArray<float>> Rad;
		Track.SetNum(Marks); Geom.SetNum(Marks); Rad.SetNum(Marks);
		for (int32 s = 0; s <= Steps; ++s)
		{
			const int32 M = (s * (Marks - 1)) / FMath::Max(Steps, 1);
			if (Track[M].Num() == 0)
			{
				for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
				{
					Track[M].Add(Fx->EmitterProbePos(i));
					FVector C; Geom[M].Add(Fx->EmitterSectionCentroid(i, C) ? C : FVector(FLT_MAX));
					Rad[M].Add(Fx->EmitterSectionRadius(i));
				}
			}
			Fx->Tick(1.f / 60.f);
		}
		for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
		{
			const FAsdaFxEmitter& E = Fx->Emitters[i];
			FString Line, GLine, RLine;
			FVector First = FVector::ZeroVector; float MaxD = 0.f;
			FVector GFirst(FLT_MAX); float GMaxD = 0.f;
			float RMin = FLT_MAX, RMax = -1.f;
			for (int32 m = 0; m < Marks; ++m)
			{
				const FVector P = Track[m].IsValidIndex(i) ? Track[m][i] : FVector::ZeroVector;
				if (m == 0) { First = P; } else { MaxD = FMath::Max(MaxD, FVector::Dist(First, P)); }
				Line += FString::Printf(TEXT("t%d=%s "), m, *P.ToCompactString());
				const FVector G = Geom[m].IsValidIndex(i) ? Geom[m][i] : FVector(FLT_MAX);
				if (G.X != FLT_MAX)
				{
					if (GFirst.X == FLT_MAX) { GFirst = G; }
					else { GMaxD = FMath::Max(GMaxD, FVector::Dist(GFirst, G)); }
					GLine += FString::Printf(TEXT("g%d=%s "), m, *G.ToCompactString());
				}
				const float R = Rad[m].IsValidIndex(i) ? Rad[m][i] : -1.f;
				if (R >= 0.f)
				{
					RMin = FMath::Min(RMin, R); RMax = FMath::Max(RMax, R);
					RLine += FString::Printf(TEXT("r%d=%.1f "), m, R);
				}
			}
			UE_LOG(LogTemp, Warning,
				TEXT("[зонд-движения] strict=%d %s эм.%02d «%s» тип 0x%X движение=%s разброс=%.1f: %s| ГЕОМ разброс=%.1f: %s| РАДИУС %.1f→%.1f (дельта %.1f): %s"),
				bStrict ? 1 : 0, *A[0], i, *E.Name, E.JebType,
				E.bEmitterMotion ? TEXT("ВКЛ") : TEXT("выкл"), MaxD, *Line, GMaxD,
				GLine.IsEmpty() ? TEXT("(секции нет)") : *GLine,
				(RMax < 0.f) ? 0.f : RMin, (RMax < 0.f) ? 0.f : RMax,
				(RMax < 0.f) ? 0.f : (RMax - RMin), RLine.IsEmpty() ? TEXT("(секции нет)") : *RLine);
		}
		Fx->Destroy();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ЗОНД СТЫКА ЦИКЛА (08.10.2026, задача «эффекты резко зациклены», `Reports/owner_1008/fx_loop.md`).
//   Гоняет ТОТ ЖЕ SimulateStep (60 Гц, мимо гейтов видимости) и на каждом кадре читает то, что ушло в
//   рендер: вершины секции эмиттера (центроид, радиус, средняя яркость альфа×RGB, средний UV, поворот).
//   Кадр помечается «стыком», если таймер периода пошёл назад, провернулся оборот или перезапустился
//   контейнер. На выходе по эмиттеру: наибольший скачок НА СТЫКАХ против КОНТРОЛЯ — наибольшего и 95-го
//   перцентиля шага ВНУТРИ периода того же эмиттера (контрольная пара: сигнал сравнивается с
//   собственным разбросом, а не с общим «шумом» — закон pixel-metric-needs-control-pairs).
//   Цели: акторы мира, чьё имя .jeb содержит подстроку (по одному на имя), иначе — заспавненный BP.
//   Запуск: `asda.Fx.LoopProbe <часть имени .jeb | BP_…> [секунды=10] [акторов=8]`. Только чтение
//   (кроме самого шага симуляции у выбранных акторов).
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxLoopProbe(
	TEXT("asda.Fx.LoopProbe"),
	TEXT("asda.Fx.LoopProbe <часть имени .jeb | BP_…> [сек=10] [акторов=8] — скачок секции НА СТЫКЕ цикла против шага внутри периода, по эмиттерам"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W || A.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[петля] использование: asda.Fx.LoopProbe <часть имени> [сек] [акторов]"));
			return;
		}
		const FString Filter = A[0];
		const float Secs = (A.Num() > 1) ? FMath::Clamp(FCString::Atof(*A[1]), 0.5f, 120.f) : 10.f;
		const int32 MaxActors = (A.Num() > 2) ? FMath::Clamp(FCString::Atoi(*A[2]), 1, 64) : 8;
		TArray<AAsdaEffectActor*> Targets;
		TSet<FString> SeenNames;
		for (TActorIterator<AAsdaEffectActor> It(W); It && Targets.Num() < MaxActors; ++It)
		{
			AAsdaEffectActor* Fx = *It;
			if (!Fx || Fx->Emitters.Num() == 0) { continue; }
			const FString Nm = Fx->EffectName.IsEmpty() ? Fx->GetName() : Fx->EffectName;
			if (!Nm.Contains(Filter, ESearchCase::IgnoreCase)) { continue; }
			if (SeenNames.Contains(Nm)) { continue; }
			SeenNames.Add(Nm);
			Targets.Add(Fx);
		}
		bool bSpawned = false;
		if (Targets.Num() == 0)
		{
			const bool bRawBP = Filter.StartsWith(TEXT("BP_"));
			AAsdaEffectActor* Fx = bRawBP
				? AAsdaEffectActor::SpawnByBPName(W, Filter, FVector::ZeroVector, FRotator::ZeroRotator, nullptr)
				: AAsdaEffectActor::SpawnByJebName(W, Filter, FVector::ZeroVector, FRotator::ZeroRotator, nullptr);
			if (!Fx) { UE_LOG(LogTemp, Warning, TEXT("[петля] «%s»: ни актора в мире, ни BP"), *Filter); return; }
			Fx->CullDistance = 1.0e9f;
			Fx->bUseViewOverride = true;
			Fx->ViewLocationOverride = FVector(0.f, -1000.f, 300.f);
			Fx->ViewRotationOverride = FRotator(0.f, 90.f, 0.f);
			Targets.Add(Fx);
			bSpawned = true;
		}
		const float Dt = 1.f / 60.f;
		const int32 Steps = FMath::Max(1, FMath::RoundToInt(Secs / Dt));
		for (AAsdaEffectActor* Fx : Targets)
		{
			Fx->RestartEmitters();
			const int32 NE = Fx->Emitters.Num();
			// по эмиттеру: прошлое состояние и накопители
			struct FAcc
			{
				bool bHave = false; FVector C; float R = 0, I = 0; FVector2D UV; FVector D0;
				float PrevLife = 0; int32 PrevWrap = 0;
				int32 Seams = 0;
				int32 PopIns = 0;   // стыков-«появлений»: конец цикла невидим, начало видно
				float SeamI = 0, SeamR = 0, SeamC = 0, SeamUV = 0, SeamAng = 0;
				TArray<float> CtlI, CtlR, CtlC, CtlUV, CtlAng;
			};
			TArray<FAcc> Acc; Acc.SetNum(NE);
			float PrevCont = 0.f;
			for (int32 s = 0; s < Steps; ++s)
			{
				Fx->DebugLoopStep(Dt);
				bool bK, bH; float CD, CT; int32 CE;
				Fx->GetLoopContainerDebug(bK, bH, CD, CE, CT);
				const bool bContRestart = (CT + 1e-4f < PrevCont);
				PrevCont = CT;
				for (int32 i = 0; i < NE; ++i)
				{
					FAcc& X = Acc[i];
					int32 Mode, DT_, DE; float Life, Over; int32 Wrap; bool bStop, bSeed;
					if (!Fx->GetLoopDebug(i, Mode, DT_, DE, Life, Over, Wrap, bStop, bSeed)) { continue; }
					FVector C, D0; float R, I; FVector2D UV;
					const bool bSec = Fx->EmitterSectionLook(i, C, R, I, UV, D0);
					const bool bSeam = (Life + 1e-4f < X.PrevLife) || (Wrap != X.PrevWrap) || bContRestart;
					// Видимость стороны: яркость вершин > 0.03 (у additive чёрный/прозрачный квад не виден).
					//   Нулевой полу-экстент (радиус секции < 0.5 юн) — тоже невидим, хоть цвет и белый.
					const bool bVisNow = bSec && I > 0.03f && R > 0.5f, bVisPrev = X.bHave && X.I > 0.03f && X.R > 0.5f;
					if (bSec && X.bHave)
					{
						const float dI = FMath::Abs(I - X.I);
						// Геометрия меняется ЗАМЕТНО только когда квад виден на ОБОИХ кадрах.
						const bool bBoth = bVisNow && bVisPrev;
						const float dR = bBoth ? FMath::Abs(R - X.R) / FMath::Max3(R, X.R, 1.f) : 0.f;
						const float dC = bBoth ? (float)FVector::Dist(C, X.C) : 0.f;
						// UV: целый сдвиг невидим (текстуры эффектов TA_WRAP) — берём дробную часть разности.
						const FVector2D DUv = UV - X.UV;
						const float dUV = bBoth ? (float)FVector2D(DUv.X - FMath::RoundToDouble(DUv.X), DUv.Y - FMath::RoundToDouble(DUv.Y)).Size() : 0.f;
						float dAng = bBoth ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)FVector::DotProduct(D0, X.D0), -1.f, 1.f))) : 0.f;
						// Кольцо: рисунок повторяется через 360°/U-повторов — поворот на целый период невидим.
						if (Fx->Emitters[i].bRing && Fx->Emitters[i].RingUTile > 1.f)
						{
							const float P = 360.f / Fx->Emitters[i].RingUTile;
							const float M = FMath::Fmod(dAng, P);
							dAng = FMath::Min(M, P - M);
						}
						if (bSeam)
						{
							++X.Seams;
							if (bVisPrev)
							{
								// ОБРЫВ: конец цикла был виден — его смена на начало и есть «резкий стык»
								X.SeamI = FMath::Max(X.SeamI, dI); X.SeamR = FMath::Max(X.SeamR, dR); X.SeamC = FMath::Max(X.SeamC, dC);
								X.SeamUV = FMath::Max(X.SeamUV, dUV); X.SeamAng = FMath::Max(X.SeamAng, dAng);
							}
							else if (bVisNow) { ++X.PopIns; }   // ПОЯВЛЕНИЕ: конец невидим, новый цикл возник из ничего
						}
						else
						{
							X.CtlI.Add(dI); X.CtlR.Add(dR); X.CtlC.Add(dC); X.CtlUV.Add(dUV); X.CtlAng.Add(dAng);
						}
					}
					else if (bSeam && (bSec != X.bHave))
					{
						++X.Seams;
						if (bVisPrev) { X.SeamI = FMath::Max(X.SeamI, X.I); }   // видимая секция пропала на стыке
						else if (bVisNow) { ++X.PopIns; }
					}
					X.bHave = bSec; if (bSec) { X.C = C; X.R = R; X.I = I; X.UV = UV; X.D0 = D0; }
					X.PrevLife = Life; X.PrevWrap = Wrap;
				}
			}
			auto P95 = [](TArray<float>& V) { if (V.Num() == 0) { return 0.f; } V.Sort(); return V[FMath::Clamp(FMath::FloorToInt(V.Num() * 0.95f), 0, V.Num() - 1)]; };
			auto Mx = [](const TArray<float>& V) { float M = 0.f; for (float x : V) { M = FMath::Max(M, x); } return M; };
			bool bK, bH; float CD, CT; int32 CE;
			Fx->GetLoopContainerDebug(bK, bH, CD, CE, CT);
			UE_LOG(LogTemp, Warning, TEXT("[петля] АКТОР %s (%s) контейнер: известен=%d холдер=%d длит=%.3f end=%d | шагов %d по %.4f с"),
				*Fx->EffectName, *Fx->GetName(), bK ? 1 : 0, bH ? 1 : 0, CD, CE, Steps, Dt);
			for (int32 i = 0; i < NE; ++i)
			{
				FAcc& X = Acc[i];
				int32 Mode, DT_, DE; float Life, Over; int32 Wrap; bool bStop, bSeed;
				Fx->GetLoopDebug(i, Mode, DT_, DE, Life, Over, Wrap, bStop, bSeed);
				const FAsdaFxEmitter& E = Fx->Emitters[i];
				const float mI = Mx(X.CtlI), mR = Mx(X.CtlR), mC = Mx(X.CtlC), mUV = Mx(X.CtlUV), mAng = Mx(X.CtlAng);
				UE_LOG(LogTemp, Warning,
					TEXT("[петля]\t%s\t%02d\t%s\tассет=0x%X/%d\tдиск=0x%X/%d\tрежим=%d\tсид=%d\tпериод=%.3f\tстоп=%d\tшвов=%d\tпоявлений=%d\tшов:dI=%.4f\tdR=%.4f\tdC=%.2f\tdUV=%.4f\tdAng=%.2f\tконтроль_max:dI=%.4f\tdR=%.4f\tdC=%.2f\tdUV=%.4f\tdAng=%.2f\tконтроль_p95:dI=%.4f\tdR=%.4f\tdC=%.2f\tdUV=%.4f\tdAng=%.2f"),
					*Fx->EffectName, i, *E.Name, E.JebType, E.EndMode, DT_, DE, Mode, bSeed ? 1 : 0, E.Period, bStop ? 1 : 0,
					X.Seams, X.PopIns, X.SeamI, X.SeamR, X.SeamC, X.SeamUV, X.SeamAng, mI, mR, mC, mUV, mAng,
					P95(X.CtlI), P95(X.CtlR), P95(X.CtlC), P95(X.CtlUV), P95(X.CtlAng));
			}
			if (bSpawned) { Fx->Destroy(); }
		}
		UE_LOG(LogTemp, Warning, TEXT("[петля] ГОТОВО «%s»: акторов %d"), *Filter, Targets.Num());
	}));
#endif // ASDA_DEV_CONSOLE

// ★ СТЕНД СНИМКОВ (08.10.2026, fx_parity3): эффект перед камерой игрока — для снимков «до/после» путём игрока
//   (обычный Tick актора, гейты видимости, рендер; `HighResShot` следующей командой через `asda.UI.Seq`).
//   Только диагностика: актор без владельца, срок жизни задаёт команда; `clear` — снести все стенды.
//   Запуск: `asda.Fx.ShotStand <часть .jeb | BP_…> [дистанция=500] [жизнь=30|auto]` / `asda.Fx.ShotStand clear`
//   (`auto` — срок `NaturalLifeSpan`, как у одноразовых эффектов игры).
static TArray<TWeakObjectPtr<AAsdaEffectActor>> GAsdaFxShotStands;   // заспавненные стенды (для clear)

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxShotStand(
	TEXT("asda.Fx.ShotStand"),
	TEXT("asda.Fx.ShotStand <часть .jeb | BP_…> [дист=500] [жизнь=30] | clear — эффект перед камерой (снимки до/после)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W || A.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("[стенд-снимок] использование: asda.Fx.ShotStand <имя> [дист] [жизнь] | clear")); return; }
		if (A[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			for (TWeakObjectPtr<AAsdaEffectActor>& P : GAsdaFxShotStands) { if (P.IsValid()) { P->Destroy(); } }
			GAsdaFxShotStands.Reset();
			UE_LOG(LogTemp, Warning, TEXT("[стенд-снимок] стенды снесены"));
			return;
		}
		// ★ Дедуп №2 В1-C (F4): камера — общей точкой наблюдателя `AsdaView` (в игре — та же камера первого игрока, те же
		//   GetCameraLocation/Rotation; в окне редактора — вьюпорт ЭТОГО мира вместо отказа). Нужен поворот: источник
		//   без поворота (павн, отрисованный вид) — по-прежнему «нет камеры игрока».
		FAsdaViewPoint VP;
		if (!AsdaView::GetViewPoint(W, VP) || !VP.bHasRotation) { UE_LOG(LogTemp, Warning, TEXT("[стенд-снимок] нет камеры игрока")); return; }
		const float Dist = (A.Num() > 1) ? FMath::Clamp(FCString::Atof(*A[1]), 50.f, 5000.f) : 500.f;   // см перед камерой
		const bool bAutoLife = (A.Num() > 2) && A[2].Equals(TEXT("auto"), ESearchCase::IgnoreCase);   // срок как у одноразовых спавнеров
		float Life = (A.Num() > 2 && !bAutoLife) ? FMath::Clamp(FCString::Atof(*A[2]), 0.5f, 600.f) : 30.f;    // срок жизни стенда, с
		const FVector CamLoc = VP.Location;
		const FRotator CamRot = VP.Rotation;
		const FVector Loc = CamLoc + CamRot.Vector() * Dist;
		AAsdaEffectActor* Fx = A[0].StartsWith(TEXT("BP_"))
			? AAsdaEffectActor::SpawnByBPName(W, A[0], Loc, FRotator::ZeroRotator, nullptr)
			: AAsdaEffectActor::SpawnByJebName(W, A[0], Loc, FRotator::ZeroRotator, nullptr);
		if (!Fx) { UE_LOG(LogTemp, Warning, TEXT("[стенд-снимок] «%s»: BP не найден"), *A[0]); return; }
		Fx->CullDistance = 1.0e9f;
		if (bAutoLife) { Life = Fx->NaturalLifeSpan(); }   // тот же срок, что у SpawnBuffFx/ударов (контейнер 0/2 учтён)
		Fx->SetLifeSpan(Life);
		GAsdaFxShotStands.Add(Fx);
		UE_LOG(LogTemp, Warning, TEXT("[стенд-снимок] %s (%s) в %s, камера %s → %s, жизнь %.1f с; SpawnFromNode=%d LoopHold=%d EndTail=%d Loop=%d"),
			*Fx->EffectName, *Fx->GetName(), *Loc.ToCompactString(), *CamLoc.ToCompactString(), *CamRot.ToCompactString(), Life,
			CVarAsdaFxSpawnFromNode.GetValueOnGameThread(), CVarAsdaFxLoopHold.GetValueOnGameThread(),
			CVarAsdaFxEndTail.GetValueOnGameThread(), CVarAsdaFxLoop.GetValueOnGameThread());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ САМОПРОВЕРКА ЦИКЛА-3 (08.10.2026, fx_parity3): обрезка альфы > 1 С ЗАПИСЬЮ в канал против модели эталона.
//   В текущих ассетах альфа > 1 есть только у кольца `eff_neoalpenshop_a_00` Object2 (через сид-узлы DT_FxLoop), и там
//   отрезок возрастающий — картинка совпадает с обрезкой при выводе. Расходящийся случай (`mon_hiskaiyanormal_bo_00`
//   Object1, 0x19: альфа 39.2 → 0 за 0.5 с, endMode 1) в порту не собран, поэтому стенд строит такой эмиттер в памяти
//   и гоняет тот же SimulateStep (DebugLoopStep). Модель эталона (E:92512 марш `alpha += dt·rate`, E:92539 снап узла,
//   draw E:97289 `if (alpha > 1) alpha = 1` в канал, endMode 1 → реинит) считается здесь же покадрово.
//   Печатает альфу порта (GetEmitterRuntime) против канала эталона, отрицательный канал сравнивается как 0 (вывод порта
//   режет к 0, заворот байта эталона не воспроизводится — отдельный пункт). Только диагностика, актор сносится.
//   Запуск: `asda.Fx.AlphaClampSelfTest [альфа_старт=39.2] [альфа_конец=0] [период=0.5] [тип=0x19]`.
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxAlphaClampSelfTest(
	TEXT("asda.Fx.AlphaClampSelfTest"),
	TEXT("asda.Fx.AlphaClampSelfTest [A0=39.2] [A1=0] [период=0.5] [тип=25] — ЦИКЛ-3: альфа > 1 режется с записью в канал (против модели эталона)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& AIn, UWorld* W)
	{
		if (!W) { return; }
		// ★ Ревизия В2 дедупа №2 (11.10.2026): `quit` — не число, а «выйти по итогу» (пакет `asda.SelfTest.Batch` дописывает его
		//   последним; прежде он становился A0 = `Atof("quit")` = 0 и выхода не было). Числа — среди аргументов БЕЗ `quit`.
		bool bQuit = false;   // выйти после итога
		TArray<FString> A;    // позиционные числовые аргументы
		for (const FString& Arg : AIn) { if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } else { A.Add(Arg); } }
		const float A0 = (A.Num() > 0) ? FCString::Atof(*A[0]) : 39.2f;          // альфа узла 0 (СТАРТ)
		const float A1 = (A.Num() > 1) ? FCString::Atof(*A[1]) : 0.f;            // альфа узла 1 (КОНЕЦ)
		const float Per = (A.Num() > 2) ? FMath::Max(FCString::Atof(*A[2]), 0.1f) : 0.5f;   // период, с
		const int32 Ty = (A.Num() > 3) ? FCString::Atoi(*A[3]) : 0x19;           // тип записи .jeb
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AAsdaEffectActor* Fx = W->SpawnActor<AAsdaEffectActor>(AAsdaEffectActor::StaticClass(), FVector(0.f, 0.f, -100000.f), FRotator::ZeroRotator, SP);
		if (!Fx) { UE_LOG(LogTemp, Warning, TEXT("[альфа>1] не заспавнился стенд")); return; }
		Fx->EffectName = TEXT("zz_alphaclamp_selftest");   // строки в DT_FxLoop нет: режим Legacy, контейнера нет
		FAsdaFxEmitter E;
		E.Name = TEXT("Object1"); E.JebType = Ty; E.bSingleQuad = (Ty != 0x0E); E.bRing = (Ty == 0x0E);
		E.Period = Per; E.EndMode = 1;
		FAsdaFxNode N0; N0.Time = 0.f; N0.Alpha = A0; N0.SizeX = N0.SizeY = 50.f;
		FAsdaFxNode N1 = N0; N1.Time = Per; N1.Alpha = A1;
		E.Nodes = { N0, N1 };
		Fx->Emitters = { E };
		const float Dt = 1.f / 60.f;
		const float Rate = (A1 - A0) / Per;   // наклон канала эталона (node[rate])
		float C = A0, Life = 0.f;             // канал и таймер модели эталона
		float MaxDiff = 0.f; int32 NOver = 0, NDiffOld = 0;
		for (int32 k = 1; k <= FMath::RoundToInt(2.f * Per / Dt) + 2; ++k)
		{
			Fx->DebugLoopStep(Dt);
			// эталон: марш, затем конец периода (endMode 1 → реинит на СТАРТ), затем draw с обрезкой в канал
			Life += Dt; C += Dt * Rate;
			if (Life > Per) { Life = FMath::Fmod(Life, Per); C = A0 + Life * Rate; }
			if (C > 1.f) { C = 1.f; }
			bool bSt = false; float LT = 0.f, Port = -1.f;
			Fx->GetEmitterRuntime(0, bSt, LT, &Port, nullptr, nullptr);
			const float PortOut = FMath::Clamp(Port, 0.f, 1.f), EtaOut = FMath::Clamp(C, 0.f, 1.f);   // как уходит в вершину
			const float OldOut = FMath::Clamp(A0 + (A1 - A0) * (LT / Per), 0.f, 1.f);                    // прежний порт: обрезка только при выводе
			MaxDiff = FMath::Max(MaxDiff, FMath::Abs(PortOut - EtaOut));
			if (Port > 1.f + 1e-4f) { ++NOver; }
			if (FMath::Abs(OldOut - EtaOut) > 1e-3f) { ++NDiffOld; }
			if (k <= 4 || k % 10 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[альфа>1]\tкадр=%d\tlife=%.4f\tпорт=%.4f\tэталон_канал=%.4f\tвывод порт/эталон/прежний=%.4f/%.4f/%.4f"),
					k, LT, Port, C, PortOut, EtaOut, OldOut);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[альфа>1] ИТОГ тип=0x%X A=%.3f→%.3f период=%.2f Loop=%d: макс. |вывод порт − эталон| = %.5f; кадров с альфой порта > 1: %d; кадров, где прежняя обрезка при выводе расходилась с эталоном: %d"),
			Ty, A0, A1, Per, CVarAsdaFxLoop.GetValueOnGameThread(), MaxDiff, NOver, NDiffOld);
		Fx->Destroy();
		if (bQuit) { AsdaSelfTestBatch::Quit(W); }   // ★ ревизия В2 дедупа №2: конец самотеста для пакета / выход
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ЦЕНА ЦИКЛА 1:1 (08.10.2026): время симуляции ВСЕХ эффектов мира, мс на кадр. Гоняет тот же
//   SimulateStep (DebugLoopStep, мимо гейтов видимости — то есть худший случай «всё в кадре»), N шагов
//   по 1/60 с. Сравнивать прогоны с `asda.Fx.Loop 0` и `1` ПАРАМИ (контроль собственного разброса).
//   Запуск: `asda.Fx.LoopBench [шагов=120]`.
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxLoopBench(
	TEXT("asda.Fx.LoopBench"),
	TEXT("asda.Fx.LoopBench [шагов=120] — мс на кадр симуляции всех эффектов мира (тот же SimulateStep)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W) { return; }
		const int32 Steps = (A.Num() > 0) ? FMath::Clamp(FCString::Atoi(*A[0]), 1, 100000) : 120;
		TArray<AAsdaEffectActor*> All;
		int32 NEm = 0;
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It) { if (*It) { All.Add(*It); NEm += It->Emitters.Num(); } }
		// прогрев: первый шаг разрешает DT_FxLoop и строит секции — в замер не входит
		for (AAsdaEffectActor* Fx : All) { Fx->DebugLoopStep(1.f / 60.f); }
		const double T0 = FPlatformTime::Seconds();
		for (int32 s = 0; s < Steps; ++s) { for (AAsdaEffectActor* Fx : All) { Fx->DebugLoopStep(1.f / 60.f); } }
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0 / Steps;
		UE_LOG(LogTemp, Warning, TEXT("[петля-цена] asda.Fx.Loop=%d акторов=%d эмиттеров=%d шагов=%d: %.4f мс/кадр"),
			IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fx.Loop"))->GetInt(), All.Num(), NEm, Steps, Ms);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ЧИСЛА «ДО/ПОСЛЕ» ПО РАЗМЕЩЁННЫМ ЭФФЕКТАМ КАРТЫ (2026-09-10, задача «смещение эмиттеров»).
//   `asda.Fx.MotionProbe` спавнит СВОЙ двойник из BP — им НЕЛЬЗЯ мерить акторы, уже лежащие в уровне
//   (у них свой трансформ, своя привязка к кости и своя хиральная нормаль). Эта команда ЧИТАЕТ
//   ЖИВЫЕ акторы мира и печатает по каждому эмиттеру МИРОВОЙ ЦЕНТРОИД и РАДИУС построенной секции —
//   то самое «где эмиттер ОКАЗАЛСЯ», сравнимое между прогонами без глазомера (закон
//   [[diagnostic-must-use-real-path]]: мерим тот же путь, который рисуется игроку).
//   НИЧЕГО НЕ МЕНЯЕТ — только читает и печатает.
//   Запуск: `asda.Fx.SectionDump [часть имени .jeb или ярлыка]` (без аргумента — все эффекты мира).
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxSectionDump(
	TEXT("asda.Fx.SectionDump"),
	TEXT("asda.Fx.SectionDump [часть имени] — напечатать мировой центроид/радиус секции каждого "
	     "эмиттера у РАЗМЕЩЁННЫХ эффектов мира (только читает)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W) { return; }
		const FString Filter = (A.Num() > 0) ? A[0] : FString();
		int32 Shown = 0;
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			AAsdaEffectActor* Fx = *It;
			if (!Fx) { continue; }
			const FString Nm = Fx->EffectName.IsEmpty() ? Fx->GetName() : Fx->EffectName;
			if (!Filter.IsEmpty() && !Nm.Contains(Filter) && !Fx->GetName().Contains(Filter)) { continue; }
			++Shown;
			UE_LOG(LogTemp, Warning, TEXT("[секции] АКТОР %s (%s) лок=%s масштаб=%.4f эмиттеров=%d"),
				*Nm, *Fx->GetName(), *Fx->GetActorLocation().ToCompactString(),
				Fx->GetActorScale3D().X, Fx->Emitters.Num());
			for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
			{
				const FAsdaFxEmitter& E = Fx->Emitters[i];
				FVector C; float R = -1.f; int32 V = 0;
				const bool bOk = Fx->EmitterSectionStats(i, C, R, V);
				UE_LOG(LogTemp, Warning,
					TEXT("[секции]   эм.%02d «%s» тип 0x%X no_chiral=%d центроид=%s радиус=%.1f вершин=%d"),
					i, *E.Name, E.JebType, E.bRingNoChiral ? 1 : 0,
					bOk ? *C.ToCompactString() : TEXT("(секции нет)"), R, V);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[секции] ИТОГО акторов напечатано: %d (фильтр «%s»)"), Shown, *Filter);
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ПРОВЕРКА ЯРКОСТИ ЭФФЕКТОВ В УПАКОВАННОМ БИЛДЕ (волна Ф1, 09.09.2026).
//   В упакованной игре нет Python и нет редакторских библиотек, то есть прочитать «что реально
//   стоит на секции» нечем. Команда читает значение ПРЯМО С `UProceduralMeshComponent` — того
//   самого экземпляра материала, которым рисуется секция.
//   ★ СЦЕНАРИЙ ЦЕЛИКОМ ВНУТРИ КОМАНДЫ, потому что `-ExecCmds` выполняет все свои пункты СРАЗУ,
//     одним кадром, и развести «навёлся — отъехал — вернулся» по времени снаружи нечем.
//   ★ ОТЪЕЗД ОБЯЗАТЕЛЕН: дальше `CullDistance` секции чистятся, на возврате создаются заново, и
//     актор сам ставит на них материал из данных эмиттера (`SetMaterial`, ветка Create этого же
//     файла). Именно на этом сгорела первая попытка волны Ф1: рантайм-MID снимался с секции,
//     а лог рапортовал успех.
//   Ничего не меняет: только читает и печатает. В Shipping консольных команд нет вовсе.
//   Запуск: `asda.Fx.MatDump <часть имени .jeb> [x y z pitch yaw]`
void AAsdaEffectActor::DebugClearAllSections()
{
	if (Mesh) { Mesh->ClearAllMeshSections(); }
	for (int32& SV : SectionVerts) { SV = 0; }
	SectionMeshXf.Reset();   // кэш трансформа .jem-меша: иначе меш-секция не пересоберётся (актор не двигался)
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxMatDump(
	TEXT("asda.Fx.MatDump"),
	TEXT("asda.Fx.MatDump <часть имени .jeb> [x y z pitch yaw | cycle | recreate] — напечатать материал и "
	     "EmissiveBoost каждой секции эффект-акторов; с координатами (или словом cycle, который берёт их у "
	     "первого подошедшего актора) — полный цикл «навёлся / отъехал за отсечку / вернулся»; recreate — "
	     "тот же цикл пересборки секций без отъезда (для эффектов, до которых кулл не дотягивается)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W) { return; }
		const FString Filter = (A.Num() > 0) ? A[0] : FString();
		// ★ РЕЖИМ `recreate` (09.09.2026). Проверяет РОВНО ТО ЖЕ, что «отъезд за отсечку и возврат»:
		//   секции сносятся, следующий тик собирает их заново веткой Create и сам ставит материал из
		//   данных эмиттера. Нужен там, где отъездом ничего не проверить: у эффектов сцены логина
		//   CullDistance принудительно 1e9 (AsdaLoginGameMode) — за отсечку не отъехать в принципе,
		//   а на самой сцене логина игрового пешехода для BugItGo может не быть вовсе.
		if (A.Num() == 2 && A[1].Equals(TEXT("recreate"), ESearchCase::IgnoreCase))
		{
			TWeakObjectPtr<UWorld> WR(W);
			auto AtR = [WR](float Secs, TFunction<void()> Fn)
			{
				if (!WR.IsValid()) { return; }
				FTimerHandle H;
				WR->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Fn]() { Fn(); }), Secs, false);
			};
			AtR(4.0f, [WR, Filter]() { if (WR.IsValid()) { AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("НА МЕСТЕ")); } });
			AtR(5.0f, [WR, Filter]()
			{
				if (!WR.IsValid()) { return; }
				TArray<AActor*> Acts;
				UGameplayStatics::GetAllActorsOfClass(WR.Get(), AAsdaEffectActor::StaticClass(), Acts);
				int32 Cleared = 0;
				for (AActor* AA : Acts)
				{
					AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(AA);
					if (!Fx || (!Filter.IsEmpty() && !Fx->EffectName.Contains(Filter))) { continue; }
					Fx->DebugClearAllSections();
					++Cleared;
				}
				UE_LOG(LogTemp, Warning, TEXT("[FxMatDump] СЕКЦИИ СНЕСЕНЫ у %d эффект-акторов (эмуляция цикла кулла)"), Cleared);
			});
			AtR(5.2f, [WR, Filter]() { if (WR.IsValid()) { AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("СРАЗУ ПОСЛЕ СНОСА")); } });
			AtR(11.0f, [WR, Filter]() { if (WR.IsValid()) { AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("ПОСЛЕ ПЕРЕСБОРКИ")); } });
			AtR(12.0f, []() { UE_LOG(LogTemp, Warning, TEXT("FX-MATDUMP ЗАВЕРШЕНО")); });
			return;
		}
		// ★ РЕЖИМ `cycle` (добавлен 09.09.2026 при раскате на ВСЕ карты). Раньше цикл «навёлся —
		//   отъехал за отсечку — вернулся» требовал готовых координат камеры, а они существовали
		//   только для двух карт волны Ф1 (fx_f1_<карта>.json). Для остальных 18 карт и для сцены
		//   логина их нет, а выдумывать координаты нельзя. `cycle` берёт якорь ИЗ МИРА: позиция
		//   ПЕРВОГО эффект-актора, подошедшего под фильтр, минус 1200 юн по X и плюс 400 по Z
		//   (камера смотрит на него сбоку-сверху). Ничего не меняет — только читает и телепортирует
		//   камеру отладочной командой BugItGo, как и вариант с явными координатами.
		double X = 0.0, Y = 0.0, Z = 0.0, P = -10.0, Yaw = 0.0;
		if (A.Num() == 2 && A[1].Equals(TEXT("cycle"), ESearchCase::IgnoreCase))
		{
			TArray<AActor*> Anchors;
			UGameplayStatics::GetAllActorsOfClass(W, AAsdaEffectActor::StaticClass(), Anchors);
			const AAsdaEffectActor* Anchor = nullptr;
			for (AActor* AA : Anchors)
			{
				const AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(AA);
				if (Fx && (Filter.IsEmpty() || Fx->EffectName.Contains(Filter))) { Anchor = Fx; break; }
			}
			if (!Anchor)
			{
				UE_LOG(LogTemp, Warning, TEXT("[FxMatDump] якорь не найден: нет эффект-актора по фильтру «%s» (всего в мире %d)"),
					*Filter, Anchors.Num());
				return;
			}
			const FVector L = Anchor->GetActorLocation();
			X = L.X - 1200.0; Y = L.Y; Z = L.Z + 400.0;
			UE_LOG(LogTemp, Warning, TEXT("[FxMatDump] якорь=%s (%s) поз=%s → камера (%.1f %.1f %.1f)"),
				*Anchor->GetName(), *Anchor->EffectName, *L.ToCompactString(), X, Y, Z);
		}
		else if (A.Num() < 6)
		{
			AsdaFxMatDumpOnce(W, Filter, TEXT("СЕЙЧАС"));
			return;
		}
		else
		{
			X = FCString::Atod(*A[1]); Y = FCString::Atod(*A[2]); Z = FCString::Atod(*A[3]);
			P = FCString::Atod(*A[4]); Yaw = FCString::Atod(*A[5]);
		}
		// Отъезд на 30 000 юн — заведомо больше `CullDistance` (10 000).
		const FString Here = FString::Printf(TEXT("BugItGo %.1f %.1f %.1f %.3f %.3f 0"), X, Y, Z, P, Yaw);
		const FString Away = FString::Printf(TEXT("BugItGo %.1f %.1f %.1f %.3f %.3f 0"),
			X + 30000.0, Y + 30000.0, Z + 3000.0, P, Yaw);
		TWeakObjectPtr<UWorld> WW(W);
		auto Cmd = [WW](const FString& C)
		{
			if (WW.IsValid()) { UKismetSystemLibrary::ExecuteConsoleCommand(WW.Get(), C, nullptr); }
		};
		auto At = [WW](float Secs, TFunction<void()> Fn)
		{
			if (!WW.IsValid()) { return; }
			FTimerHandle H;   // отменять нечего: сценарий одноразовый, хендл нужен только API таймера
			WW->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Fn]() { Fn(); }), Secs, false);
		};
		At(0.5f, [Cmd, Here]() { Cmd(Here); });
		At(6.0f, [WW, Filter]() { if (WW.IsValid()) { AsdaFxMatDumpOnce(WW.Get(), Filter, TEXT("НА МЕСТЕ")); } });
		At(7.0f, [Cmd, Away]() { Cmd(Away); });
		At(12.0f, [Cmd, Here]() { Cmd(Here); });
		At(18.0f, [WW, Filter]() { if (WW.IsValid()) { AsdaFxMatDumpOnce(WW.Get(), Filter, TEXT("ПОСЛЕ ВОЗВРАТА")); } });
		At(19.0f, []() { UE_LOG(LogTemp, Warning, TEXT("FX-MATDUMP ЗАВЕРШЕНО")); });
	}));
#endif // ASDA_DEV_CONSOLE
