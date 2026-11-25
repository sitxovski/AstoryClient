// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightComponent_Shapes.cpp — ★ R2 10.11.2026 (разрез AsdaFxLightComponent.cpp, дословный перенос): формы: геометрия, пулы, кольцо/диск/трубка/прямоугольник, ApplyShapeFrame, дормантность.
//   Общее для частей — в AsdaFxLightComponentInternal.h; шапка и реверс — в AsdaFxLightComponent.cpp.
#include "World/AsdaFxLightComponent.h"
#include "World/AsdaFxLightSubsystem.h"
#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "World/AsdaLightBudgetSubsystem.h"
#include "World/AsdaRenderQualitySubsystemBase.h"   // ★ волна 3 п.12 (15.09.2026): множитель дальности светов и чтение уровня — общие
#include "UI/AsdaDevMenuSubsystem.h"
#include "UObject/Package.h"                 // ★ Волна Ф8: хранитель флага «пакет изменён» при заведении дочернего источника
#include "AsdaFxLightComponentInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaFxLightComponent*.cpp

// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВОЛНА Ф10 (11.09.2026): ТРУБКА, КОЛЬЦО И АВТО-ФОРМА ПО ГЕОМЕТРИИ ЭМИТТЕРА.
//   Требование владельца: «кольца должны кольцом испускать свет, столбы — столбом и т.д.».
//   Источник формы — ГЛАВНЫЕ ОСИ облака вершин секции эмиттера (AAsdaEffectActor::EmitterSectionFrame,
//   та же функция, что у прямоугольника Ф8) плюс ТИП эмиттера (кольцо 0xE, меш, развёрнутый к камере квад).
// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// Геометрия формы эмиттера для трубки и кольца — ОДНА функция на распознавание и на кадр света
struct FAsdaFxShapeGeom
{
	bool bOk = false;                    // секция есть и в ней не меньше трёх вершин
	FVector Major = FVector::UpVector;   // ось трубки
	float MajorHalf = 0.f;               // полу-длина вдоль неё, юн
	FVector RingN = FVector::UpVector;   // нормаль плоскости кольца
	float RingR = 0.f;                   // радиус кольца, юн
	float HA = 0.f, HB = 0.f, HN = 0.f;  // полу-габариты по главным осям (по убыванию разброса)
	FVector AxN = FVector::UpVector;     // наименьшая главная ось (нормаль плоскости секции)
};

static FAsdaFxShapeGeom AsdaShapeGeom(const AAsdaEffectActor* Fx, int32 Em)
{
	FAsdaFxShapeGeom G;
	if (!Fx || !Fx->Emitters.IsValidIndex(Em)) { return G; }
	FVector C, A, B, N, H;
	if (!Fx->EmitterSectionFrame(Em, C, A, B, N, H, AsdaFxLightCarrierFrame(Fx, Em))) { return G; }   // ★ 15.10.2026: в кадре носителя
	G.bOk = true;
	G.HA = (float)H.X; G.HB = (float)H.Y; G.HN = (float)H.Z; G.AxN = N;
	if (Fx->Emitters[Em].bRing)
	{
		// ★ У ЛЕНТЫ-КОЛЬЦА 0xE две оси окружности дают БЛИЗКИЕ полу-габариты (обе = радиус), третья — ось
		//   кольца. По наименьшему разбросу её не найти: у высокого цилиндра (RingH больше радиуса)
		//   наименьший разброс уже по оси окружности, и нормаль легла бы в плоскость кольца.
		const float dAB = FMath::Abs(G.HA - G.HB), dAN = FMath::Abs(G.HA - G.HN), dBN = FMath::Abs(G.HB - G.HN);
		if (dAB <= dAN && dAB <= dBN) { G.RingN = N; G.RingR = 0.5f * (G.HA + G.HB); G.Major = N; G.MajorHalf = G.HN; }
		else if (dAN <= dBN)          { G.RingN = B; G.RingR = 0.5f * (G.HA + G.HN); G.Major = B; G.MajorHalf = G.HB; }
		else                          { G.RingN = A; G.RingR = 0.5f * (G.HB + G.HN); G.Major = A; G.MajorHalf = G.HA; }
	}
	else
	{
		G.Major = A; G.MajorHalf = G.HA;
		G.RingN = N; G.RingR = 0.5f * (G.HA + G.HB);
	}
	return G;
}

EAsdaFxLightShape UAsdaFxLightComponent::ClassifyEmitterShape(const AAsdaEffectActor* Fx, int32 Em, const FAsdaFxLightSpec& Spec)
{
	const FAsdaFxShapeGeom G = AsdaShapeGeom(Fx, Em);
	if (!G.bOk) { return EAsdaFxLightShape::Point; }
	const FAsdaFxEmitter& E = Fx->Emitters[Em];
	// 1. Кольцо-лента 0xE: низкое — кольцо, высокое (ось длиннее полутора радиусов) — это уже столб
	if (E.bRing)
	{
		return (G.MajorHalf > 1.5f * FMath::Max(G.RingR, 1.f)) ? EAsdaFxLightShape::Tube : EAsdaFxLightShape::Ring;
	}
	const float HB = FMath::Max(G.HB, 1.f);
	const bool bLong = G.HA >= FMath::Max(Spec.AutoElongation, 1.f) * HB;
	// 2. ★ Квады и частицы, РАЗВЁРНУТЫЕ К КАМЕРЕ (BillboardMode 0), плоские только потому, что смотрят в
	//   объектив: их плоскость — не форма эффекта, а направление взгляда. Прямоугольник, обращённый к
	//   камере, светил бы в камеру, а не на пол. У них различаем лишь «вытянут» (язык пламени) и «нет».
	const bool bCamFacing = !E.bMeshEmitter && E.BillboardMode == 0;
	if (!bCamFacing && G.HN <= FMath::Clamp(Spec.AutoFlatness, 0.f, 1.f) * HB)
	{
		// 3. Плоский. Вытянутая полоса (луч, лента столба) — ТРУБКА вдоль неё: замер стенда 11.09 на
		//    ch1_a_4th_ac_04_1 — прямоугольником два луча столба сдвигали среднюю яркость кадра на +0.05
		//    (в шуме контроля), точкой — на +0.60. Горизонтальный примерно круглый (печать под ногами) —
		//    кольцо; прочая плоскость (вертикальная панель, стена портала) — прямоугольник.
		if (bLong) { return EAsdaFxLightShape::Tube; }
		const bool bHoriz = FMath::Abs(G.AxN.Z) >= 0.7f;
		// ★ Ф11: плоский горизонтальный круг отдаётся КОЛЬЦУ (как было) или БЛИНУ — по галочке владельца.
		//   Дефолт галочки false ⇒ распознавание волны Ф10 не меняется ни на одном эффекте.
		if (bHoriz && G.HA <= 1.6f * HB)
		{
			return Spec.bAutoFlatRoundAsDisc ? EAsdaFxLightShape::Disc : EAsdaFxLightShape::Ring;
		}
		return EAsdaFxLightShape::Rect;
	}
	// 4. Объёмный: вытянутый — столб (трубка), иначе — точка
	return bLong ? EAsdaFxLightShape::Tube : EAsdaFxLightShape::Point;
}

EAsdaFxLightShape UAsdaFxLightComponent::ResolveAutoShape(const FAsdaFxLightSpec& Spec, int32 GeomEmitter, float DeltaTime)
{
	AutoTimer += DeltaTime;
	if (!bAutoHave || AutoTimer >= 0.25f)
	{
		AutoTimer = 0.f;
		const EAsdaFxLightShape Now = ClassifyEmitterShape(Effect.Get(), GeomEmitter, Spec);
		if (!bAutoHave)             { AutoShape = AutoPending = Now; bAutoHave = true; }
		else if (Now == AutoPending) { AutoShape = Now; }   // второй замер подряд подтвердил — меняем
		else                         { AutoPending = Now; } // первый замер «за» — ждём подтверждения
	}
	return AutoShape;
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВОЛНА Ф11 (20.09.2026): РАЗМЕЩЕНИЕ ФОРМЫ И РАСКЛАДКА ФОРМЫ НА ИСТОЧНИКИ.
//
//   ЧТО ЛЕЧИМ И ПОЧЕМУ ИМЕННО ТАК (доказательство — шапка блока полей в World/AsdaFxLightSpec.h):
//   маска затухания локального света считается от ОДНОЙ точки
//   (Engine/Shaders/Private/DeferredLightingCommon.ush:252, :260), и итог собирается как
//   `CombinedAttenuation = Attenuation · LightMask` (:550) — то есть площадное распределение
//   ДОМНОЖАЕТСЯ на сферу из центра. Объём влияния на CPU — тоже сфера вокруг точки
//   (Engine/Source/Runtime/Engine/Private/Components/LocalLightComponent.cpp:78-81).
//   Отсюда единственное честное лечение: РАСКЛАДКА ФОРМЫ НА НЕСКОЛЬКО ИСТОЧНИКОВ, у каждого своя сфера.
//   ★ Диффузная составляющая капсулы у движка и вовсе НЕ интегрируется по площади: это одно среднее
//     направление плюс wrap-NoL (Engine/Shaders/Private/CapsuleLightIntegrate.ush:47-69), поэтому
//     «длинная лампа» без раскладки не даёт даже равномерной засветки вдоль себя.
// ═══════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaFxLightComponent::EnsurePointChildren(int32 Num)
{
	if (PointChildren.Num() >= Num) { return; }
	AActor* Owner = GetOwner();
	if (!Owner) { return; }
	// Флаг «пакет изменён» у карты возвращается как был (закон ActiveLight): пролёт камеры мимо эффекта
	// не должен заставлять редактор просить сохранить уровень
#if WITH_EDITOR
	UPackage* Pkg = GIsEditor ? Owner->GetPackage() : nullptr;
	const bool bWasDirty = Pkg && Pkg->IsDirty();
#endif
	while (PointChildren.Num() < Num)
	{
		UPointLightComponent* P = NewObject<UPointLightComponent>(Owner, NAME_None, RF_Transient);
		if (!P) { break; }
		AsdaInitFxChildLight(P);
		P->bUseInverseSquaredFalloff = true;    // та же физика, что у нашей точки (кандела, обратные квадраты)
		P->SetupAttachment(this);
		P->RegisterComponent();
		PointChildren.Add(P);
	}
#if WITH_EDITOR
	if (Pkg && !bWasDirty && Pkg->IsDirty()) { Pkg->SetDirtyFlag(false); }
#endif
}

void UAsdaFxLightComponent::EnsureRectChildren(int32 Num)
{
	if (RectChildren.Num() >= Num) { return; }
	AActor* Owner = GetOwner();
	if (!Owner) { return; }
#if WITH_EDITOR
	UPackage* Pkg = GIsEditor ? Owner->GetPackage() : nullptr;
	const bool bWasDirty = Pkg && Pkg->IsDirty();
#endif
	while (RectChildren.Num() < Num)
	{
		URectLightComponent* R = NewObject<URectLightComponent>(Owner, NAME_None, RF_Transient);
		if (!R) { break; }
		AsdaInitFxChildLight(R);
		R->SetupAttachment(this);
		R->RegisterComponent();
		RectChildren.Add(R);
	}
#if WITH_EDITOR
	if (Pkg && !bWasDirty && Pkg->IsDirty()) { Pkg->SetDirtyFlag(false); }
#endif
}

// ★★★ РЕВИЗИЯ Ф11 (М4, 20.09.2026): УБОРКА ЛИШНИХ ДОЧЕРНИХ ИСТОЧНИКОВ.
//   Довод — в шапке объявления (World/AsdaFxLightComponent.h). Короче: пулы только росли, и перебор
//   форм в панели деталей оставлял у эффекта десятки погашенных, но ЗАРЕГИСТРИРОВАННЫХ компонентов.
//   Отсрочка нужна против дрожания: бюджет на границе то даёт шесть кусков, то пять, и удалять
//   шестой в каждом таком кадре значило бы каждый кадр же его заводить заново.
static float GAsdaFxPoolIdleSec = 5.f;
static FAutoConsoleVariableRef CVarAsdaFxPoolIdleSec(
	TEXT("asda.Fx.Light.PoolIdleSec"),
	GAsdaFxPoolIdleSec,
	TEXT("Через сколько секунд простоя удаляются лишние дочерние источники формы (пулы точечных и прямоугольных). 0 = удалять сразу, отрицательное = не удалять вовсе (поведение волны Ф11)."),
	ECVF_Default);

void UAsdaFxLightComponent::TrimShapePools(float DeltaTime)
{
	if (GAsdaFxPoolIdleSec < 0.f) { return; }
	const int32 KeepP = FMath::Clamp(LastPointUsed, 0, PointChildren.Num());
	const int32 KeepR = FMath::Clamp(LastRectUsed, 0, RectChildren.Num());
	if (PointChildren.Num() <= KeepP && RectChildren.Num() <= KeepR) { ShapePoolIdle = 0.f; return; }
	ShapePoolIdle += FMath::Max(DeltaTime, 0.f);
	if (ShapePoolIdle < GAsdaFxPoolIdleSec) { return; }
	ShapePoolIdle = 0.f;
	AActor* Owner = GetOwner();
	// Флаг «пакет изменён» у карты возвращается как был — тот же закон, что у ActiveLight и EnsurePointChildren:
	// уборка мёртвых источников не повод просить владельца сохранить уровень
#if WITH_EDITOR
	UPackage* Pkg = (GIsEditor && Owner) ? Owner->GetPackage() : nullptr;
	const bool bWasDirty = Pkg && Pkg->IsDirty();
#endif
	auto Kill = [](UActorComponent* C) { if (IsValid(C) && !C->IsBeingDestroyed()) { C->DestroyComponent(); } };
	for (int32 k = PointChildren.Num() - 1; k >= KeepP; --k) { Kill(PointChildren[k]); PointChildren.RemoveAt(k); }
	for (int32 k = RectChildren.Num() - 1; k >= KeepR; --k) { Kill(RectChildren[k]); RectChildren.RemoveAt(k); }
#if WITH_EDITOR
	if (Pkg && !bWasDirty && Pkg->IsDirty()) { Pkg->SetDirtyFlag(false); }
#endif
}

UAsdaFxLightComponent::FAsdaFxShapeBasis UAsdaFxLightComponent::ResolveShapeBasis(
	const FAsdaFxLightSpec& Spec, const FAsdaFxLightAgg& Agg, int32 GeomEmitter, const FVector& Loc) const
{
	FAsdaFxShapeBasis Bs;
	const AAsdaEffectActor* Fx = Effect.Get();
	FVector C = Loc, A = FVector::ForwardVector, B = FVector::RightVector, N = FVector::UpVector, H = FVector::ZeroVector;
	// ★ 15.10.2026: секция мировых частиц эффекта на движущемся носителе — «в кадре носителя» (то же правило, что в
	//   GatherEmitters): центр, оси и полу-габариты формы — как у стоящего носителя.
	Bs.bHaveFrame = Fx && Fx->EmitterSectionFrame(GeomEmitter, C, A, B, N, H, AsdaFxLightCarrierFrame(Fx, GeomEmitter));

	// ── ПРИВЯЗКА. `Section` (дефолт с ревизии Ф11, С3) = центр той самой секции, по которой мерены
	//   оси формы; `Light` = точка источника, то есть поведение волн Ф8…Ф10 без отличий.
	//   Прочим привязкам МИРОВОЕ смещение владельца добавляется здесь: в `Loc` оно уже входит,
	//   и без этой строки одно и то же поле работало бы при одной привязке и молчало при другой.
	//   ★ Ревизия Ф11 (С3): ОТКАТ «геометрии нет» — это `Loc`, и смещение к нему НЕ прибавляется
	//   второй раз (в `Loc` оно уже учтено в ApplyFrame). Прежняя запись `(bHaveFrame ? C : Loc) + Offset`
	//   давала на первом кадре и на дистанц-культе ДВОЙНОЕ смещение — незаметное ровно до тех пор,
	//   пока привязка не стала умолчанием.
	switch (Spec.ShapeAnchor)
	{
	case EAsdaFxShapeAnchor::Section:   Bs.C = Bs.bHaveFrame ? (C + Spec.Offset) : Loc; break;
	case EAsdaFxShapeAnchor::Centroid:  Bs.C = Agg.Centroid + Spec.Offset; break;
	case EAsdaFxShapeAnchor::BoundsBox: Bs.C = Agg.BoxCenter + Spec.Offset; break;
	case EAsdaFxShapeAnchor::Actor:     Bs.C = Fx ? (Fx->GetActorLocation() + Spec.Offset) : Loc; break;
	default:                            Bs.C = Loc; break;
	}

	// ── ДОВОРОТ ОСЕЙ. Крутим ИМЕННО оси, а не готовый FRotator источника: у блина и кольца источников
	//   несколько, и «повернуть каждый» и «повернуть систему координат» — разные вещи; вторая и нужна.
	if (!Spec.ShapeRotation.IsNearlyZero())
	{
		const FQuat Q = Spec.ShapeRotation.Quaternion();
		A = Q.RotateVector(A); B = Q.RotateVector(B); N = Q.RotateVector(N);
	}
	Bs.A = A.GetSafeNormal(); Bs.B = B.GetSafeNormal(); Bs.N = N.GetSafeNormal();
	if (Bs.A.IsNearlyZero()) { Bs.A = FVector::ForwardVector; }
	if (Bs.B.IsNearlyZero()) { Bs.B = FVector::RightVector; }
	if (Bs.N.IsNearlyZero()) { Bs.N = FVector::UpVector; }
	Bs.Half = H;

	// ── ЛОКАЛЬНОЕ СМЕЩЕНИЕ. ПОСЛЕ доворота: владелец двигает форму по ТЕМ осям, которые видит на экране
	//   после поворота, а не по тем, что были до него.
	Bs.C += Bs.A * Spec.OffsetLocal.X + Bs.B * Spec.OffsetLocal.Y + Bs.N * Spec.OffsetLocal.Z;
	return Bs;
}

// ═══ ★★★ ВОЛНА fix9 (КРИТ-1 ревизии fix8, 20.09.2026): РАЗМЕР ФОРМЫ — ОДНА ФОРМУЛА НА КАДР ═══════
//   Довод целиком — у объявления в заголовке. Короче: до этой волны число полос блина и долю радиуса
//   полосы называли по размеру и радиусу ПРОШЛОГО кадра, а куски строили по ТЕКУЩЕМУ; радиус живого
//   эффекта дышит, и правило отвечало про чужой кадр. Здесь размер формы меряется ОДИН раз, и то же
//   число уходит и в правило, и в SplitGeometry.
//   ★ Формулы не новые: каждая ПЕРЕНЕСЕНА из своего кадра формы (ApplyDiscFrame, ApplyRingFrame,
//     PrepareTube, ApplyShapeFrame) буква в букву, и те теперь спрашивают её здесь. Второго расчёта
//     тех же чисел не остаётся ни одного (закон port-shared-points).
//   ★ Ячейки сетки SnapShapeSize у каждой величины свои (0 блин, 1 кольцо, 2 трубка, 3 ширина
//     плоскости) — как и было; повторный вызов в том же кадре сетку не двигает, поэтому «померить
//     до решения и померить при постройке» даёт одно и то же число.
float UAsdaFxLightComponent::MeasureShapeSizeR(const FAsdaFxLightSpec& Spec, EAsdaFxLightShape Sh,
	const FAsdaFxShapeBasis& Bs, int32 GeomEmitter, float Radius)
{
	switch (Sh)
	{
	case EAsdaFxLightShape::Disc:
		// РАДИУС ДИСКА: полусумма двух длинных полу-осей секции (настоящий размер печати) или руками.
		//   ★ Ревизия Ф11 (М3): геометрии может ещё не быть (первый кадр, StartDelay, дистанц-кулл
		//   почистил её) — тогда берётся тот же запасной радиус, что у кольца (Radius · 0.25).
		//   ★★★ «форма-2», п. 1: РАЗМЕР ФОРМЫ ПОДНИМАЕТСЯ ТЕМ ЖЕ УДЕРЖАНИЕМ, ЧТО И РАДИУС.
		//   `ShapeHoldK` = «удержанный габарит ÷ габарит этого кадра» (считается в ApplyFrame, там же
		//   и довод). Секция дышит целиком: и её радиус, и полу-габариты главных осей сжимаются ОДНИМ
		//   множителем, поэтому один и тот же K восстанавливает и то, и другое. Удерживать радиус, но
		//   не форму, значило бы получить «широкий свет из схлопнувшегося блина» — полдефекта.
		//   ★ Режимы «Постоянные» (число владельца) удержание НЕ трогает: там габарита нет вовсе.
		return (Spec.DiscSizeMode == EAsdaFxRectSizeMode::Fixed)
			? FMath::Max(Spec.DiscRadius, 1.f)
			: (Bs.bHaveFrame
				? FMath::Max(SnapShapeSize(0.5f * ((float)Bs.Half.X + (float)Bs.Half.Y) * ShapeHoldK * FMath::Max(Spec.DiscSizeScale, 0.05f), 0), 1.f)
				: FMath::Max(Radius * 0.25f, 1.f));
	case EAsdaFxLightShape::Ring:
	{
		// РАДИУС ОБОДА: из геометрии (ось кольца ищется отдельно, см. AsdaShapeGeom) или руками
		if (Spec.RingSizeMode == EAsdaFxRectSizeMode::Fixed) { return FMath::Max(Spec.RingRadiusUU, 1.f); }
		const FAsdaFxShapeGeom G = AsdaShapeGeom(Effect.Get(), GeomEmitter);
		return G.bOk ? FMath::Max(SnapShapeSize(G.RingR * ShapeHoldK, 1), 1.f) : FMath::Max(Radius * 0.25f, 1.f);
	}
	case EAsdaFxLightShape::Tube:
	{
		// ПОЛУ-ДЛИНА СТОЛБА: доля габарита вдоль главной оси или заданная длина пополам.
		//   «Длина от габарита» в ручном режиме не применяется: это ДОЛЯ ГАБАРИТА, а габарит в ручном
		//   режиме не спрашивается вовсе.
		if (Spec.TubeLengthMode == EAsdaFxRectSizeMode::Fixed) { return 0.5f * FMath::Max(Spec.TubeLength, 1.f); }
		const FAsdaFxShapeGeom G = AsdaShapeGeom(Effect.Get(), GeomEmitter);
		return G.bOk ? SnapShapeSize(G.MajorHalf * ShapeHoldK * FMath::Max(Spec.TubeLengthScale, 0.f), 2) : 0.f;
	}
	case EAsdaFxLightShape::Rect:
	{
		// ШИРИНА ПЛОСКОСТИ: удвоенный полу-габарит по САМОЙ ДЛИННОЙ оси секции или заданная руками.
		//   Нижний зажим 2 юнита — вырожденный прямоугольник движок трактует как точку
		//   (RectLightComponent.cpp:330-331), и лента толщиной в один квад стала бы «лампочкой».
		if (Spec.RectSizeMode == EAsdaFxRectSizeMode::Section && Bs.bHaveFrame)
		{
			return FMath::Max(SnapShapeSize((float)Bs.Half.X * 2.f * ShapeHoldK * FMath::Max(Spec.RectSizeScale, 0.05f), 3), 2.f);
		}
		return FMath::Max(Spec.RectWidth, 1.f);
	}
	default:
		// Точка, шар, конус: собственного размера кусков у них нет — раскладки тоже.
		return 0.f;
	}
}

void UAsdaFxLightComponent::ShapeFrameGeom(const FAsdaFxLightSpec& Spec, EAsdaFxLightShape Sh,
	const FAsdaFxShapeBasis& Bs, int32 GeomEmitter, float Radius, float& OutSizeR, float& OutBaseR, float& OutCap)
{
	// Потолок — тот же, что читают кадр формы и бюджет (в юнитах ЭТОГО мира: логин ×100)
	OutCap = FMath::Max(UAsdaFxLightSubsystem::ResolveRadiusCap() * UAsdaFxLightSubsystem::WorldLengthScale(GetWorld()), 1.f);
	// ★★★ КРИТ-1: базовый радиус — РАДИУС ЭТОГО КАДРА, уже прищёлкнутый и уже под уровневым потолком
	//   (ApplyFrame срезает его до LvlCap выше по тексту). Ровно это же число уходит в SplitGeometry
	//   аргументом BaseR, поэтому «сколько полос просит правило» и «сколько их построено» — про один
	//   и тот же кадр. Зажим снизу единицей — тот же, что у ShapeBaseRadius.
	OutBaseR = FMath::Min(FMath::Max(Radius, 1.f), OutCap);
	OutSizeR = MeasureShapeSizeR(Spec, Sh, Bs, GeomEmitter, Radius);
}

// ★ R5 (12.11.2026, дубли): НАСТРОЙКА ОДНОГО КУСКА составного света — общая для кольца (`ApplyRingFrame`), блина
//   (`ApplyDiscFrame`) и трубки (`ApplyTubeSegments`). До R5 эти десять вызовов стояли тремя дословными копиями; порядок
//   вызовов и выражения перенесены без изменений (масштаб объёмного рассеяния по-прежнему спрашивается на каждый кусок).
//   P — точечный источник куска; DrawDist — дальность отрисовки (`ResolveDrawDistance`); R — радиус затухания куска;
//   Len — светящаяся длина куска; SumLen — сумма длин всех кусков; IntensityCd — сила всего света, кд.
static void AsdaApplyLightPiece(UPointLightComponent* P, const FAsdaFxLightSpec& Spec, UWorld* World, float DrawDist,
	const FLinearColor& Col, float R, float Len, float SumLen, float IntensityCd)
{
	P->SetMaxDrawDistance(DrawDist);
	P->SetMaxDistanceFadeRange(Spec.FadeRange);
	P->SetVolumetricScatteringIntensity(Spec.VolumetricScattering * UAsdaDevMenuSubsystem::FogVolumetricLightScale(World));
	P->SetAffectGlobalIllumination(Spec.bAffectGI);
	P->SetLightColor(Col, /*bSRGB=*/false);
	P->SetAttenuationRadius(R);
	// ★ Ревизия Ф11 (С2), правило 1: доля силы = доля ФАКТИЧЕСКОЙ светящейся длины.
	//   Суммарный поток при этом сохраняется точно (Σ долей = 1), то есть «точка ↔ блин»
	//   по-прежнему меняет форму пятна, а не общую яркость.
	// ★ fix5: сила по ФАКТИЧЕСКОЙ светящейся длине — одно правило на кольцо, блин и трубку
	P->SetIntensity(IntensityCd * Len / SumLen);
	P->SetSourceRadius(Spec.SourceRadius);
	P->SetSoftSourceRadius(Spec.SoftSourceRadius);
	P->SetSourceLength(Len);
}

void UAsdaFxLightComponent::ApplyRingFrame(const FAsdaFxLightSpec& Spec, int32 GeomEmitter, const FAsdaFxShapeBasis& Bs,
	float Radius, const FLinearColor& Col, float IntensityCd)
{
	const FVector Loc = Bs.C;
	// Прочие формы гаснут в ТОТ ЖЕ кадр (довод — в ApplyShapeFrame: два пятна читались бы как удвоенная яркость)
	if (bAffectsWorld) { bAffectsWorld = false; SetVisibility(false); MarkRenderStateDirty(); }
	AsdaDouseLight(RectChild.Get());
	AsdaDouseLight(RectChildBack.Get());
	AsdaDouseLight(SpotChild.Get());
	for (URectLightComponent* R : RectChildren) { AsdaDouseLight(R); }
	// ★ ТЕНИ У КОЛЬЦА НЕТ НИКОГДА: N кубических карт на один эффект навыка — ровно та цена, за которую
	//   навыкам запрещено выходить из бюджета теней. Слот, если его держала прежняя форма, освобождается.
	ApplyShadowFrame(Spec, /*bWant*/false, ShadowTarget.IsValid() ? ShadowTarget.Get() : this);

	const FAsdaFxShapeGeom G = AsdaShapeGeom(Effect.Get(), GeomEmitter);
	// ★ Ф11: нормаль кольца доворачивается вместе со всей системой координат формы. Разность «ось кольца
	//   по геометрии» и «нормаль секции» сохранена: у ленты 0xE они РАЗНЫЕ (см. AsdaShapeGeom).
	FVector N = G.bOk ? G.RingN.GetSafeNormal() : FVector::UpVector;
	if (!Spec.ShapeRotation.IsNearlyZero()) { N = Spec.ShapeRotation.Quaternion().RotateVector(N).GetSafeNormal(); }
	// ★ Ф11: радиус обода — из геометрии (как было) или руками
	//   ★ fix5 (СРЕДН-Д): размер из геометрии — через сетку (довод общий, см. ApplyDiscFrame)
	//   ★★★ fix9 (КРИТ-1): формула переехала в MeasureShapeSizeR — её ЖЕ спрашивает решение о числе
	//     дуг в ApplyFrame, и обе стороны видят размер ЭТОГО кадра, а не прошлого.
	const float RingR = MeasureShapeSizeR(Spec, EAsdaFxLightShape::Ring, Bs, GeomEmitter, Radius);
	const int32 NSeg = FMath::Clamp(Spec.RingSegments, 3, 8);

	// Базис плоскости кольца
	const FVector Ref = (FMath::Abs(N.Z) < 0.9f) ? FVector::UpVector : FVector::ForwardVector;
	const FVector U = FVector::CrossProduct(N, Ref).GetSafeNormal();
	const FVector V = FVector::CrossProduct(N, U).GetSafeNormal();
	// Дуга k — хорда вписанного N-угольника: середина на расстоянии R·cos(π/N) от центра, длина 2R·sin(π/N).
	//   Трубки стыкуются концами, и на полу получается сплошное кольцо, а не N отдельных пятен.
	//   ★ fix5: длина хорды и удаление её середины от центра обода считаются в SplitGeometry (Sp.Len,
	//   Sp.Pos) — здесь остаётся только угол дуги.
	const float HalfStep = PI / (float)NSeg;
	// ★ Ф10-испр.: радиус дуги не меньше её полу-хорды: затухание движка меряется от ЦЕНТРА источника,
	//   и при радиусе дуги меньше полу-хорды концы дуг (стыки обода) оставались без света.
	//   Под уровневым потолком; если потолок режет — укорачивается сама дуга.
	// ★★★ РЕВИЗИЯ fix3 (М-2, 20.09.2026): ПРАВИЛО «ПОЛУ-ДЛИНА ≤ 0.66 РАДИУСА» — ОДНО НА ВСЕ ТРИ
	//   РАЗЛОЖЕННЫЕ ФОРМЫ. У кольца стояла несогласованная пара: радиус считался как «полу-хорда +
	//   полрадиуса», а длина резалась по доле 0.8 — то есть «хватило» по одной формуле и «не хватило»
	//   по другой, и дуга укорачивалась даже там, где потолок не режет. Ревизия Ф11 привела к доле
	//   0.66 только блин, оставив кольцо и трубку при прежних числах (0.8 и «+1 юн»).
	//   Выбрана доля БЛИНА (0.66): она с запасом в треть радиуса держит концы куска внутри его же
	//   сферы затухания, и ниже неё стыки кусков темнеют (закон волны Ф10-испр.).
	const float RingCap = FMath::Max(UAsdaFxLightSubsystem::ResolveRadiusCap() * UAsdaFxLightSubsystem::WorldLengthScale(GetWorld()), 1.f);
	// ★ Ревизия fix3 (КРИТ-1): размеры дуг считает ОБЩАЯ SplitGeometry — та же, которую спрашивает
	//   экранный бюджет, когда предсказывает цену другого числа дуг
	// ★★★ РЕВИЗИЯ fix5 (КРИТ-А): ДУГА ТОЖЕ СКЛАДЫВАЕТСЯ ИЗ ПЛИТОК ВСТЫК. До неё кольцо радиусом 600
	//   юн при свете 400 давало дугу 528 юн вместо хорды 1039 — обод рвался на три коротких отрезка
	//   с чёрными промежутками, хотя в дампе значилось «кольцо x3/3».
	const FAsdaFxSplit Sp = SplitGeometry(EAsdaFxLightShape::Ring, NSeg, RingR, Radius,
		FMath::Clamp(Spec.RingRadiusScale, 0.1f, 2.f), RingCap, FMath::Clamp(Spec.ShapeCoverTiles, 1, 8));
	const int32 NSrcTotal = Sp.N;

	// Дуги заводятся лениво; флаг «пакет изменён» у карты возвращается как был (закон ActiveLight)
	EnsurePointChildren(NSrcTotal);

	// ★ Ревизия fix3 (КРИТ-1): ИЗМЕРЕННЫЙ экранный след — сумма квадратов радиусов зажжённых источников
	LastLitSumR2 = Sp.SumR2;
	LastLitSources = NSrcTotal;
	LastSplitSizeR = RingR;   // ★ fix3: размер формы — им бюджет предсказывает цену другого числа дуг
	LastSplitParents = Sp.Parents;   // ★ fix9 (КРИТ-1): построенных дуг
	LastPointUsed = NSrcTotal;
	LastRectUsed = 0;
	// ★ Сила делится ПРОПОРЦИОНАЛЬНО светящейся длине куска (fix5): у кольца все плитки равны, поэтому
	//   числа совпадают с прежним «поровну» до последнего знака, но правило деления теперь ОДНО на все
	//   три разложенные формы — вторая формула того же и есть тот способ, которым они расходятся.
	float SumLen = 0.f;
	for (int32 k = 0; k < NSrcTotal; ++k) { SumLen += Sp.Len[k]; }
	if (!(SumLen > 0.f)) { SumLen = 1.f; }
	const float DrawDist = ResolveDrawDistance(Spec);
	for (int32 k = 0; k < PointChildren.Num(); ++k)
	{
		UPointLightComponent* P = PointChildren[k];
		if (!P) { continue; }
		if (k >= NSrcTotal) { AsdaDouseLight(P); continue; }
		// Угол берётся по НОМЕРУ ДУГИ, а не по номеру источника: плитки одной дуги лежат под одним углом
		const float Ang = 2.f * PI * (float)Sp.Idx[k] / (float)NSeg + HalfStep;
		const FVector Dir = U * FMath::Cos(Ang) + V * FMath::Sin(Ang);
		const FVector Tan = (V * FMath::Cos(Ang) - U * FMath::Sin(Ang)).GetSafeNormal();
		AsdaApplyLightPiece(P, Spec, GetWorld(), DrawDist, Col, Sp.R[k], Sp.Len[k], SumLen, IntensityCd);   // ★ R5: общая настройка куска
		// Трубка точечного света движка лежит вдоль ЛОКАЛЬНОЙ Z (PointLightSceneProxy.cpp: Tangent = столбец Z).
		//   ★ fix5: плитка сдвинута вдоль хорды (Sp.Along), середина дуги — на Sp.Pos от центра обода.
		P->SetWorldLocationAndRotation(Loc + Dir * Sp.Pos[k] + Tan * Sp.Along[k], FRotationMatrix::MakeFromZ(Tan).Rotator());
		if (!P->bAffectsWorld)
		{
			P->bAffectsWorld = true;
			P->SetVisibility(true);
			P->MarkRenderStateDirty();
		}
	}
}

// ═══ ★★★ Ф11: «БЛИН» — ПЛОСКИЙ ДИСК, СВЕТЯЩИЙ ВСЕЙ ПЛОСКОСТЬЮ ═════════════════════════════════════
//
//   КАК СОБРАН. N параллельных КАПСУЛ (точечный свет движка с `SourceLength`), уложенных хордами
//   поперёк диска: полоса k стоит на расстоянии b_k от центра вдоль оси B, её длина — длина хорды
//   на этом расстоянии, 2·√(R² − b_k²). Сила делится ПРОПОРЦИОНАЛЬНО ФАКТИЧЕСКОЙ ДЛИНЕ полосы
//   (★ ревизия Ф11, С2 — раньше делилась по номинальной хорде, см. довод в теле функции): у равных
//   сил короткие крайние полосы светили бы так же, как длинная средняя, и края блина были бы ЯРЧЕ
//   середины на единицу длины, а при срезающем потолке радиуса — наоборот, ярче была бы середина.
//   ПОЧЕМУ НЕ ОДИН ПРЯМОУГОЛЬНИК ДВИЖКА. (1) он излучает только в ПОЛУСФЕРУ — всё за его плоскостью
//   гасится наглухо (DeferredLightingCommon.ush:279-283), а блин под ногами обязан подсвечивать и
//   персонажа над собой, и пол под собой; (2) затухание у него всё равно от одной точки (:252, :260),
//   то есть края «блина» радиусом 600 при радиусе света 400 остались бы чёрными; (3) круг ему можно
//   задать только текстурой источника, и та читается ТОЛЬКО в RGB (RectLight.ush:49) и не меняет
//   геометрию интеграла — полигон остаётся прямоугольником (RectLight.ush:354-357).
//   ТЕНИ НЕТ НИКОГДА — по тому же доводу, что у кольца: N кубических карт на один эффект.
void UAsdaFxLightComponent::ApplyDiscFrame(const FAsdaFxLightSpec& Spec, const FAsdaFxShapeBasis& Bs, int32 GeomEmitter,
	float Radius, const FLinearColor& Col, float IntensityCd)
{
	// Прочие формы гаснут в ТОТ ЖЕ кадр
	if (bAffectsWorld) { bAffectsWorld = false; SetVisibility(false); MarkRenderStateDirty(); }
	AsdaDouseLight(RectChild.Get());
	AsdaDouseLight(RectChildBack.Get());
	AsdaDouseLight(SpotChild.Get());
	for (URectLightComponent* R : RectChildren) { AsdaDouseLight(R); }
	ApplyShadowFrame(Spec, /*bWant*/false, ShadowTarget.IsValid() ? ShadowTarget.Get() : this);

	// РАДИУС ДИСКА: полусумма двух длинных полу-осей секции (настоящий размер печати) или руками.
	// ★ Ревизия Ф11 (М3): ГЕОМЕТРИЯ СЕКЦИИ МОЖЕТ БЫТЬ ЕЩЁ НЕ ПРОЧИТАНА (первый кадр, StartDelay,
	//   дистанц-кулл почистил её). Тогда `Bs.Half` — нули, и прежний зажим давал блин радиусом
	//   в ОДИН ЮНИТ, то есть восемь ламп в одной точке вместо круга. У кольца на этот случай давно
	//   стоит запасной радиус (`Radius · 0.25`, см. ApplyRingFrame) — берём ровно его же, чтобы два
	//   разложенных круга не вели себя по-разному в одной и той же ситуации.
	//   ★ fix5 (СРЕДН-Д): размер ИЗ ГЕОМЕТРИИ прищёлкивается к сетке — он дышит вместе с анимацией
	//   эмиттера, и его дрожание перекладывало места в связанном экранном бюджете каждый кадр.
	//   ★★★ fix9 (КРИТ-1): САМА ФОРМУЛА ПЕРЕЕХАЛА В MeasureShapeSizeR. Её ЖЕ спрашивает решение о
	//   числе полос (ApplyFrame → ShapeFrameGeom), поэтому число полос и сами полосы считаются от
	//   ОДНОГО размера и ОДНОГО радиуса — того, что у этого кадра. До правки полос просили по памяти
	//   о прошлом кадре, и на 7× и 9× игра строила 8 полос там, где правило просит 9 и 10.
	const float DiscR = MeasureShapeSizeR(Spec, EAsdaFxLightShape::Disc, Bs, GeomEmitter, Radius);
	// ПЛОСКОСТЬ ДИСКА. Оси A и B секции — она и есть; «вручную» задаёт мировой поворот, у которого
	//   ось +X — нормаль диска (тот же договор, что у прямоугольника, чтобы не заводить второго).
	FVector AxA = Bs.A, AxB = Bs.B;
	if (Spec.DiscNormalMode == EAsdaFxRectNormalMode::Explicit)
	{
		const FRotationMatrix M(Spec.RectRotation + Spec.ShapeRotation);
		AxA = M.GetScaledAxis(EAxis::Y).GetSafeNormal();
		AxB = M.GetScaledAxis(EAxis::Z).GetSafeNormal();
	}

	// ★★★ РЕВИЗИЯ fix3 (КРИТ-2): НИЖНЯЯ ГРАНИЦА ЗДЕСЬ — ОДНА ПОЛОСА, А НЕ ДВЕ. Поле владельца
	//   по-прежнему начинается с двух (одна полоса как НАСТРОЙКА бессмысленна), но бюджет вправе
	//   срезать подробность до одной: при N = 1 центр полосы b₀ = 0, то есть она проходит через
	//   середину диска и светит во весь его диаметр. Это грубый блин, а не точка — ради этого
	//   КРИТ-2 и писан. Прежний зажим снизу двойкой молча превращал «выдали одно место» в шарик.
	// ═══ ★★★ ВОЛНА fix9 (КРИТ-1, 20.09.2026): ВЕРХНИЙ ЗАЖИМ БЫЛ ВОСЕМЬЮ — И ЭТО И ЕСТЬ «ДЕСЯТЬ
	//   ПОЛОС НЕ ПОЯВИЛИСЬ НИ В ОДНОМ ПРОГОНЕ». Восьмёрка осталась здесь с ревизии fix3, когда
	//   потолок полос и был восемь; fix8 подняла потолок правила до десяти В ТРЁХ МЕСТАХ (правило,
	//   поле, ключ) и ЭТУ строку не тронула. Дальше всё сходилось само собой: правило просило 10,
	//   ResolveGiveShape отдавал 10, бюджет выдавал места под 10 — а кадр молча строил ВОСЕМЬ.
	//   ★ ПОЧЕМУ ЭТОГО НЕ УВИДЕЛИ. В дампе печаталось только число ИСТОЧНИКОВ (48), а 48 источников
	//     даёт и «8 полос при радиусе 608», и «10 полос при радиусе прошлого кадра 840» — ревизия
	//     fix8 восстанавливала число полос перебором и получила первое объяснение из двух возможных.
	//     Поэтому та же волна заводит в дамп ЧИСЛО ПОЛОС и то, сколько просит правило: две величины,
	//     которые нельзя перепутать (см. GetLastRuleParents).
	//   ★ ВТОРАЯ ПРИЧИНА ОСТАЁТСЯ НАСТОЯЩЕЙ: радиус решения и радиус кусков расходились на кадр
	//     (правка выше по тексту). Зажим и рассинхронизация — ДВА РАЗНЫХ дефекта, и виден был один.
	//   Потолок берётся ОТТУДА ЖЕ, откуда его берёт правило (AsdaFxDiscStripsMax), — второе число
	//   того же смысла разошлось бы с первым ровно так, как разошлось теперь.
	const int32 NSrc = FMath::Clamp(Spec.DiscStrips, 1, AsdaFxDiscStripsMax);

	const float Cap = FMath::Max(UAsdaFxLightSubsystem::ResolveRadiusCap() * UAsdaFxLightSubsystem::WorldLengthScale(GetWorld()), 1.f);
	// ★ fix10 (МЕЛК-5 ревизии fix9): ОТСЮДА УБРАН МЁРТВЫЙ ОСТАТОК `const float StripR0` — радиус
	//   полосы с долей из поля. Он был опорой прежнего, до-fix3 расчёта размеров полос ЗДЕСЬ; после
	//   переезда расчёта в общую SplitGeometry (ревизия fix3, КРИТ-1) его не читала ни одна строка,
	//   и оставленное объявление обещало читателю второй источник того же числа. Долю радиуса теперь
	//   спрашивают только SplitGeometry и SplitSourceCount — из ОДНОГО места (закон port-shared-points).
	const float DrawDist = ResolveDrawDistance(Spec);

	// ═══ ★★★ РЕВИЗИЯ Ф11 (С2, 20.09.2026): РАВНОМЕРНОСТЬ ПРИ ЛЮБОМ ПОТОЛКЕ РАДИУСА ══════════════
	//
	//   ЧТО БЫЛО НЕ ТАК. Полоса k получала силу ПРОПОРЦИОНАЛЬНО СВОЕЙ ХОРДЕ, а светила отрезком
	//   `min(2·полу-хорда, 2·R_полосы·0.66)`. Пока потолок радиуса не режет, это одно и то же и всё
	//   честно. Но как только потолок срезает (а на Low он 1000 юн при блине в 600 юн радиусом — то
	//   есть постоянно), ВСЕ внутренние полосы получают ОДИНАКОВУЮ длину и РАЗНУЮ силу: в прогонах
	//   волны Ф11 (`f11_s2.json`, `f11_s3.json`) у восьми полос `source_length` = 528.0 у всех при
	//   силах 0.47…0.97. Яркость на единицу длины у средних полос оказывалась вдвое выше, чем у
	//   крайних, — то есть середина блина светлее краёв, ровно то, от чего блин и делался. Числом
	//   это видно в прогоне `f11_s3`: край/центр 2 полосы 0.251 → 4 полосы 0.166 → 8 полос 0.121,
	//   то есть чем БОЛЬШЕ полос, тем ХУЖЕ — а должно быть наоборот.
	//
	//   КАК ЛЕЧИМ. Раскладка остаётся прежней — N параллельных полос-хорд (она для диска и правильна:
	//   капсула бесплатно тянется вдоль всей хорды, и другой формы покрытия круга у движка нет), —
	//   а ПРАВИЛО ДЕЛЕНИЯ СИЛЫ меняется: доля полосы считается по ЕЁ ФАКТИЧЕСКОЙ СВЕТЯЩЕЙСЯ ДЛИНЕ,
	//   а не по номинальной хорде. Тогда яркость на единицу длины одинакова у всех полос ПО
	//   ПОСТРОЕНИЮ, при любом потолке; пока потолок не режет, фактическая длина равна хорде, и
	//   числа волны Ф11 не сдвигаются ни на единицу.
	//   ★ Почему НЕ дробить хорду на куски (пробовали в ходе ревизии и отказались): при жёстком
	//     потолке это укладывает те же N источников в решётку 2×2, у которой центры удалены от
	//     середины диска на ≈0.99 радиуса затухания — и СЕРЕДИНА БЛИНА ГАСНЕТ НАСОВСЕМ (замер:
	//     внутреннее кольцо 0.0 против 7.7 у полос). Полосы-хорды такой дыры не дают никогда.
	//   ★ Ревизия fix3 (КРИТ-1): САМ РАСЧЁТ РАЗМЕРОВ ПОЛОС ПЕРЕЕХАЛ В ОБЩУЮ SplitGeometry — её же
	//     спрашивает экранный бюджет, когда предсказывает цену другого числа полос. Здесь остаётся
	//     только применение: где стоит полоса, какой силы и куда смотрит.
	//   ★★★ РЕВИЗИЯ fix5 (КРИТ-А, 20.09.2026): ПОЛОСА КЛАДЁТСЯ ПЛИТКАМИ ВСТЫК, А НЕ ОДНОЙ КАПСУЛОЙ.
	//     Довод и числа — в шапке SplitGeometry. Здесь важно одно: правило «сила по ФАКТИЧЕСКОЙ длине»
	//     (С2) от этого не меняется вовсе — плитки одной полосы равны, и вместе они несут ровно ту же
	//     долю силы, что несла бы целая полоса. Именно поэтому решётка fix5 не повторяет провал fix2:
	//     там силу делили ПОРОВНУ между кусками разной длины, и середина гасла.
	const FAsdaFxSplit Sp = SplitGeometry(EAsdaFxLightShape::Disc, NSrc, DiscR, Radius,
		FMath::Clamp(Spec.DiscStripRadiusScale, 0.1f, 2.f), Cap, FMath::Clamp(Spec.ShapeCoverTiles, 1, 8));
	float SumLen = 0.f;
	const int32 n = Sp.N;
	for (int32 k = 0; k < n; ++k) { SumLen += Sp.Len[k]; }
	if (!(SumLen > 0.f)) { SumLen = 1.f; }
	EnsurePointChildren(n);

	for (int32 k = 0; k < PointChildren.Num(); ++k)
	{
		UPointLightComponent* P = PointChildren[k];
		if (!P) { continue; }
		if (k >= n) { AsdaDouseLight(P); continue; }
		AsdaApplyLightPiece(P, Spec, GetWorld(), DrawDist, Col, Sp.R[k], Sp.Len[k], SumLen, IntensityCd);   // ★ R5: общая настройка куска
		// Трубка точечного света движка лежит вдоль ЛОКАЛЬНОЙ Z (PointLightSceneProxy.cpp: Tangent = столбец Z).
		//   ★ fix5: Sp.Pos — поперёк полос (ось B), Sp.Along — вдоль хорды (ось A, она же ось капсулы).
		P->SetWorldLocationAndRotation(Bs.C + AxB * Sp.Pos[k] + AxA * Sp.Along[k], FRotationMatrix::MakeFromZ(AxA).Rotator());
		if (!P->bAffectsWorld)
		{
			P->bAffectsWorld = true;
			P->SetVisibility(true);
			P->MarkRenderStateDirty();
		}
	}
	// ★ Ревизия Ф11 (С1) → fix3 (КРИТ-1): экранному бюджету нужен ФАКТИЧЕСКИЙ след, а он у блина
	//   НЕ выражается одним радиусом куска: полосы-хорды имеют разные радиусы (крайние короче
	//   средних), и прежняя запись «радиус самой большой полосы» завышала след крайних полос.
	//   Сумма квадратов — ровно то, чем меряется число задетых тайлов light grid.
	LastLitSumR2 = Sp.SumR2;
	LastLitSources = n;
	LastSplitSizeR = DiscR;   // ★ fix3: бюджету — размер формы, чтобы предсказать цену другого числа полос
	LastSplitParents = Sp.Parents;   // ★ fix9 (КРИТ-1): сколько полос кадр ПОСТРОИЛ — для дампа и проверки гарантии
	LastPointUsed = n;
	LastRectUsed = 0;
}

// ═══ ★★★ Ф11: РАЗЛОЖЕННАЯ ТРУБКА — N КАПСУЛ ВСТЫК ВДОЛЬ ОСИ ═══════════════════════════════════════
//   Один источник с `SourceLength` даёт форму блика и среднее направление, но энергия всё равно
//   гаснет сферой от центра (см. шапку блока). N капсул встык, у каждой своя сфера, — единственный
//   способ осветить ОСНОВАНИЕ столба той же силой, что и его середину.
void UAsdaFxLightComponent::ApplyTubeSegments(const FAsdaFxLightSpec& Spec, const FAsdaFxShapeBasis& Bs, float Radius,
	const FLinearColor& Col, float IntensityCd)
{
	if (bAffectsWorld) { bAffectsWorld = false; SetVisibility(false); MarkRenderStateDirty(); }
	AsdaDouseLight(RectChild.Get());
	AsdaDouseLight(RectChildBack.Get());
	AsdaDouseLight(SpotChild.Get());
	for (URectLightComponent* R : RectChildren) { AsdaDouseLight(R); }
	ApplyShadowFrame(Spec, /*bWant*/false, ShadowTarget.IsValid() ? ShadowTarget.Get() : this);

	// ★ fix5 (СРЕДН-Г): сюда приходит и ОДИНОЧНАЯ капсула — когда её длину надо крыть плитками.
	//   Нижняя граница 1, а не 2: правило «кусок не светит дальше источника» теперь одно на все формы,
	//   и длину трубки покрывают плитки, а не раздутый радиус (см. PrepareTube).
	const int32 NSeg = FMath::Clamp(Spec.ShapeSegments, 1, 16);

	const FVector Axis = TubeAxis.IsNearlyZero() ? FVector::UpVector : TubeAxis;
	const float Cap = FMath::Max(UAsdaFxLightSubsystem::ResolveRadiusCap() * UAsdaFxLightSubsystem::WorldLengthScale(GetWorld()), 1.f);
	// ★★★ РЕВИЗИЯ fix3 (М-2): та же доля 0.66, что у блина и кольца. Было «радиус ≥ полу-длина + 1 юн»,
	//   то есть конец капсулы лежал ровно на границе своей сферы затухания, где освещённость уже ноль, —
	//   и стыки кусков трубки темнели тем сильнее, чем короче кусок. Длина куска теперь тоже режется
	//   потолком (раньше SourceLength ставилась полной независимо от того, достаёт ли до неё радиус).
	// ★★★ РЕВИЗИЯ fix5 (КРИТ-А): и КАЖДАЯ капсула, в свою очередь, складывается из плиток встык, если
	//   до своей доли длины не дотягивается.
	const FAsdaFxSplit Sp = SplitGeometry(EAsdaFxLightShape::Tube, NSeg, TubeHalfLen, Radius,
		FMath::Clamp(Spec.ShapeSegmentRadiusScale, 0.1f, 2.f), Cap, FMath::Clamp(Spec.ShapeCoverTiles, 1, 8));
	const int32 NSrcTotal = Sp.N;
	EnsurePointChildren(NSrcTotal);
	float SumLen = 0.f;
	for (int32 k = 0; k < NSrcTotal; ++k) { SumLen += Sp.Len[k]; }
	if (!(SumLen > 0.f)) { SumLen = 1.f; }
	const float DrawDist = ResolveDrawDistance(Spec);
	const FRotator SegRot = FRotationMatrix::MakeFromZ(Axis).Rotator();
	// ★ Ревизия Ф11 (С1) → fix3 (КРИТ-1): экранный бюджет видит РАСКЛАДКУ трубки, а не один источник,
	//   и видит её ИЗМЕРЕННОЙ (радиус куска подтянут вверх под его длину и вниз под потолок)
	LastLitSumR2 = Sp.SumR2;
	LastLitSources = NSrcTotal;
	LastSplitSizeR = TubeHalfLen;   // ★ fix3: полу-длина трубки — размер формы для бюджета
	LastSplitParents = Sp.Parents;   // ★ fix9 (КРИТ-1): построенных капсул
	LastPointUsed = NSrcTotal;
	LastRectUsed = 0;
	for (int32 k = 0; k < PointChildren.Num(); ++k)
	{
		UPointLightComponent* P = PointChildren[k];
		if (!P) { continue; }
		if (k >= NSrcTotal) { AsdaDouseLight(P); continue; }
		// Центр куска k: от −L/2 до +L/2, середина своего отрезка (плитки уже сложены в Sp.Pos)
		const float t = Sp.Pos[k];
		AsdaApplyLightPiece(P, Spec, GetWorld(), DrawDist, Col, Sp.R[k], Sp.Len[k], SumLen, IntensityCd);   // ★ R5: общая настройка куска
		P->SetWorldLocationAndRotation(Bs.C + Axis * t, SegRot);
		if (!P->bAffectsWorld)
		{
			P->bAffectsWorld = true;
			P->SetVisibility(true);
			P->MarkRenderStateDirty();
		}
	}
}

// ═══ ★★★ Ф11: РАЗЛОЖЕННЫЙ ПРЯМОУГОЛЬНИК — N ПЛИТОК ВДОЛЬ ДЛИННОЙ СТОРОНЫ ══════════════════════════
//   Прямоугольник движка — единственная форма с НАСТОЯЩЕЙ площадной математикой (аналитический
//   ламберт по четырём углам + LTC-блик, RectLight.ush:340-405), но его энергия так же гасится
//   сферой от центра. Плитки дают ленте длиной в пятнадцать метров ровную полосу света вдоль себя.
//   Ширина плитки = W/N, высота та же; сила каждой = I/N (тот же договор, что у кольца и блина).
void UAsdaFxLightComponent::ApplyRectSegments(const FAsdaFxLightSpec& Spec, const FAsdaFxShapeBasis& Bs, float Radius,
	const FLinearColor& Col, float IntensityCd, float W, float H, const FRotator& Rot)
{
	if (bAffectsWorld) { bAffectsWorld = false; SetVisibility(false); MarkRenderStateDirty(); }
	AsdaDouseLight(RectChild.Get());
	AsdaDouseLight(RectChildBack.Get());
	AsdaDouseLight(SpotChild.Get());
	for (UPointLightComponent* P : PointChildren) { AsdaDouseLight(P); }
	ApplyShadowFrame(Spec, /*bWant*/false, ShadowTarget.IsValid() ? ShadowTarget.Get() : this);

	const int32 NSeg = FMath::Clamp(Spec.ShapeSegments, 2, 16);
	const bool bBoth = Spec.bRectTwoSided;
	const int32 Need = NSeg * (bBoth ? 2 : 1);
	EnsureRectChildren(Need);

	const float TileW = FMath::Max(W / (float)NSeg, 1.f);
	const float Cap = FMath::Max(UAsdaFxLightSubsystem::ResolveRadiusCap() * UAsdaFxLightSubsystem::WorldLengthScale(GetWorld()), 1.f);
	// ★ fix3 (М-2, КРИТ-1): размеры плиток — из ОБЩЕЙ SplitGeometry и по тому же правилу 0.66, что у
	//   блина, кольца и трубки. Было «полу-ширина + 1 юн» — четвёртое правило из четырёх возможных.
	const FAsdaFxSplit Sp = SplitGeometry(EAsdaFxLightShape::Rect, NSeg, W, Radius,
		FMath::Clamp(Spec.ShapeSegmentRadiusScale, 0.1f, 2.f), Cap);
	const float TileR = Sp.R[0];
	const float TileI = IntensityCd / (float)NSeg;
	const float DrawDist = ResolveDrawDistance(Spec);
	// Ширина прямоугольника движка лежит вдоль его оси Y (см. ApplyShapeFrame), поэтому плитки
	//   расставляются по оси Y ЭТОГО ЖЕ поворота, а не по осям секции — иначе «задать поворот
	//   вручную» рассыпало бы ряд плиток вбок.
	const FVector AxisW = FRotationMatrix(Rot).GetScaledAxis(EAxis::Y).GetSafeNormal();
	const FQuat QBack = Rot.Quaternion() * FQuat(FVector::UpVector, PI);
	// ★ Ревизия Ф11 (С1) → fix3 (КРИТ-1): экранный бюджет видит ВСЕ плитки (и обратные стороны тоже),
	//   каждую — её измеренным радиусом
	LastLitSumR2 = Sp.SumR2 * (bBoth ? 2.0 : 1.0);   // обратные стороны стоят там же и стоят столько же
	LastLitSources = Need;
	LastSplitSizeR = W;   // ★ fix3: ширина плоскости — размер формы для бюджета
	LastSplitParents = Sp.Parents;   // ★ fix9 (КРИТ-1): построенных плиток
	LastPointUsed = 0;
	LastRectUsed = Need;
	for (int32 k = 0; k < RectChildren.Num(); ++k)
	{
		URectLightComponent* R = RectChildren[k];
		if (!R) { continue; }
		if (k >= Need) { AsdaDouseLight(R); continue; }
		const int32 Tile = k % NSeg;
		const bool bBack = (k >= NSeg);
		const float t = Sp.Pos[Tile];
		R->SetSourceWidth(TileW);
		R->SetSourceHeight(FMath::Max(H, 1.f));
		R->SetBarnDoorAngle(Spec.BarnDoorAngle);
		R->SetBarnDoorLength(Spec.BarnDoorLength);
		R->SetMaxDrawDistance(DrawDist);
		R->SetMaxDistanceFadeRange(Spec.FadeRange);
		R->SetVolumetricScatteringIntensity(Spec.VolumetricScattering * UAsdaDevMenuSubsystem::FogVolumetricLightScale(GetWorld()));
		R->SetAffectGlobalIllumination(Spec.bAffectGI);
		R->SetLightColor(Col, /*bSRGB=*/false);
		R->SetAttenuationRadius(TileR);
		R->SetIntensity(TileI);
		R->SetWorldLocationAndRotation(Bs.C + AxisW * t, bBack ? QBack.Rotator() : Rot);
		if (!R->bAffectsWorld)
		{
			R->bAffectsWorld = true;
			R->SetVisibility(true);
			R->MarkRenderStateDirty();
		}
	}
}

void UAsdaFxLightComponent::ApplyShapeFrame(const FAsdaFxLightSpec& Spec, const FAsdaFxLightAgg& Agg, int32 GeomEmitter,
	const FVector& Loc, const FAsdaFxShapeBasis& InBs, float Radius, const FLinearColor& Col,
	float IntensityCd, bool bWantShadow)
{
	AAsdaEffectActor* Fx = Effect.Get();
	// ★★★ Ф11: ОПОРНАЯ СИСТЕМА ФОРМЫ считается ОДИН раз на кадр и ОДНА на все формы: привязка,
	//   доворот и локальное смещение обязаны работать одинаково у ленты, столба, кольца и блина.
	// ★★★ fix9 (КРИТ-1): считает её ApplyFrame и передаёт сюда — решению о числе кусков она нужна
	//   РАНЬШЕ, чем начинается применение. Копия своя: ниже центр прищёлкивается к сетке теней.
	FAsdaFxShapeBasis Bs = InBs;

	// ★★★ РЕВИЗИЯ Ф11 (С3): ЦЕНТР ФОРМЫ ПРИЩЁЛКИВАЕТСЯ К СЕТКЕ ТАК ЖЕ, КАК ПОЗИЦИЯ ЛАМПЫ.
	//   Позиция источника (`Loc`) прищёлкивается в ApplyFrame ради кэша кубической карты теней
	//   (asda.Fx.Shadow.Snap: любой сдвиг источника сбрасывает кэш и вся статика перерисовывается
	//   в шесть граней). Центр ФОРМЫ с привязкой «центр секции» — ДРУГАЯ точка, и она дрожит своим
	//   дрожанием (центр облака вершин пересчитывается каждый кадр). Без этой врезки смена умолчания
	//   привязки на `Section` сделала бы тень прямоугольника и конуса дороже, чем была.
	//   Тот же гистерезис, что у `SnappedLoc`: узел сетки пересчитывается, только когда центр ушёл
	//   дальше шага — иначе на границе ячейки снап щёлкал бы туда-сюда и сбрасывал кэш так же.
	if (bWantShadow && GAsdaFxShadowSnap > 0.f)
	{
		if (!bHaveShapeSnap || FVector::Dist(Bs.C, SnappedShapeLoc) > GAsdaFxShadowSnap)
		{
			SnappedShapeLoc = FVector(AsdaSnap((float)Bs.C.X, GAsdaFxShadowSnap),
				AsdaSnap((float)Bs.C.Y, GAsdaFxShadowSnap), AsdaSnap((float)Bs.C.Z, GAsdaFxShadowSnap));
			bHaveShapeSnap = true;
		}
		Bs.C = SnappedShapeLoc;
	}
	else { bHaveShapeSnap = false; }

	// ★ Ф10: кольцо — отдельный кадр (N дочерних дуг вместо одного источника); прочие формы гасят дуги
	if (Spec.Shape == EAsdaFxLightShape::Ring)
	{
		ApplyRingFrame(Spec, GeomEmitter, Bs, Radius, Col, IntensityCd);
		return;
	}
	// ★ Ф11: блин — N параллельных капсул-хорд в плоскости диска
	if (Spec.Shape == EAsdaFxLightShape::Disc)
	{
		ApplyDiscFrame(Spec, Bs, GeomEmitter, Radius, Col, IntensityCd);
		return;
	}
	// ★ Ф11: разложенная трубка — N капсул встык (при одном источнике идём обычным путём ниже)
	//   ★ fix5 (СРЕДН-Г): решает не поле, а ФАКТИЧЕСКОЕ число кусков (TubePieces, считает PrepareTube):
	//   трубка с одной капсулой по полю всё равно раскладывается, когда её длина не укладывается в
	//   собственный радиус. Одиночная капсула по-прежнему горит самим компонентом и может нести тень.
	if (Spec.Shape == EAsdaFxLightShape::Tube && TubePieces > 1)
	{
		ApplyTubeSegments(Spec, Bs, Radius, Col, IntensityCd);
		return;
	}
	// ★★★ Ф11: РАЗМЕРЫ И ПОВОРОТ ПРЯМОУГОЛЬНИКА СЧИТАЮТСЯ ЗДЕСЬ, ДО ВЫБОРА ИСТОЧНИКА: те же числа
	//   нужны и одиночной плоскости, и раскладке на плитки, а вторая формула тех же чисел рано или
	//   поздно разошлась бы с первой (закон diagnostic-must-use-real-path).
	float RectW = Spec.RectWidth, RectH = Spec.RectHeight;
	FRotator RectRot = FRotator::ZeroRotator;
	if (Spec.Shape == EAsdaFxLightShape::Rect)
	{
		// РАЗМЕРЫ. «Из габаритов секции» — удвоенные полу-габариты по двум ДЛИННЫМ осям: ширина по
		//   самой длинной (вдоль ленты), высота по второй. Нижний зажим в 2 юнита — вырожденный
		//   прямоугольник движок трактует как точку (RectLightComponent.cpp:330-331 зажимает 1 юнитом),
		//   и лента толщиной в один квад молча превратилась бы обратно в «лампочку».
		// ★★★ fix9 (КРИТ-1): ШИРИНА — ИЗ ОБЩЕЙ MeasureShapeSizeR (её же спрашивает решение о числе
		//   плиток), ВЫСОТА остаётся здесь: в раскладку она не входит и вторым числом нигде не нужна.
		RectW = MeasureShapeSizeR(Spec, EAsdaFxLightShape::Rect, Bs, GeomEmitter, Radius);
		if (Spec.RectSizeMode == EAsdaFxRectSizeMode::Section && Bs.bHaveFrame)
		{
			// ★ fix5 (СРЕДН-Д): стороны из габарита — через сетку (довод общий, см. ApplyDiscFrame)
			// ★★★ РЕВИЗИЯ ВОЛНЫ «форма-2» (КРИТ-1, 21.09.2026): ВЫСОТА УДЕРЖИВАЕТСЯ ТЕМ ЖЕ `ShapeHoldK`,
			//   ЧТО И ШИРИНА. Ширина ушла в общую `MeasureShapeSizeR` (там множитель уже стоял), а
			//   высота осталась считаться ЗДЕСЬ — и первая редакция волны про неё забыла. Итог был
			//   ровно тот дефект, который волна и чинила, только на одну ось: на дне вдоха секции
			//   плоскость становилась широкой лентой НУЛЕВОЙ ВЫСОТЫ. Обе стороны берутся из одного
			//   дышащего облака вершин (`Bs.Half`), значит и восстанавливаться обязаны одним числом.
			const float K = ShapeHoldK * FMath::Max(Spec.RectSizeScale, 0.05f);
			RectH = FMath::Max(SnapShapeSize((float)Bs.Half.Y * 2.f * K, 4), 2.f);
		}
		RectW = FMath::Max(RectW, 1.f);
		RectH = FMath::Max(RectH, 1.f);
		// ПОВОРОТ. У прямоугольника движка: +X — куда светит, Y — ширина, Z — высота. Ширину кладём
		//   на САМУЮ ДЛИННУЮ ось секции (A), высоту — на вторую (B), значит нормаль (N) и есть
		//   направление излучения. ★ Ф11: оси уже довёрнуты в ResolveShapeBasis, поэтому «доворот
		//   формы» действует и здесь; у режима «вручную» он складывается с заданным углом.
		switch (Spec.RectNormalMode)
		{
		case EAsdaFxRectNormalMode::Explicit: RectRot = Spec.RectRotation + Spec.ShapeRotation; break;
		case EAsdaFxRectNormalMode::AutoFlip: RectRot = FMatrix(-Bs.N, -Bs.A, Bs.B, FVector::ZeroVector).Rotator(); break;
		default:                              RectRot = FMatrix( Bs.N,  Bs.A, Bs.B, FVector::ZeroVector).Rotator(); break;
		}
		// ★ Ф11: разложенная плоскость — N плиток вдоль длинной стороны (при одном источнике идём ниже)
		if (Spec.ShapeSegments > 1)
		{
			ApplyRectSegments(Spec, Bs, Radius, Col, IntensityCd, RectW, RectH, RectRot);
			return;
		}
	}
	for (UPointLightComponent* P : PointChildren) { AsdaDouseLight(P); }
	for (URectLightComponent* R : RectChildren) { AsdaDouseLight(R); }
	// ★ Ревизия Ф11 (С1, М4) → ★★★ fix3 (КРИТ-1): форма НЕ разложена — ГОРИТ ОДИН ИСТОЧНИК ПОЛНОГО
	//   РАДИУСА, и ровно это и записывается в измеренный след. Прежняя запись «множителя нет, поле в
	//   ноль» была верна только для настоящей точки: вырожденный под бюджетом блин приходил сюда же,
	//   след обнулялся, и экранный бюджет навсегда возвращался к доле из поля (0.7² = вдвое дешевле
	//   того, что стоит на сцене). Дочерние пулы при этом не заняты ни одним источником и подлежат
	//   уборке (TrimShapePools).
	// ★★★ РЕВИЗИЯ fix4 (КРИТ-1, 20.09.2026): ИСТОЧНИКОВ ЗДЕСЬ НЕ ВСЕГДА ОДИН.
	//   Запись «один источник полного радиуса» верна для точки, шара, конуса и ОДНОСТОРОННЕЙ
	//   плоскости. Но ДВУСТОРОННЯЯ плоскость из одной плитки зажигает ниже ДВА прямоугольных
	//   источника того же радиуса (RectChild и RectChildBack, см. ветку Rect), и след её вдвое
	//   больше записанного. Регрессия жила на умолчании: «авто» у плоских эмиттеров даёт именно
	//   двусторонний прямоугольник с одной плиткой, и в прогоне при умолчаниях 40 из 45 горящих
	//   были им (f3_rd1.log). Бюджет КОЛИЧЕСТВА при этом считал верно (GetLightCost = Seg×2),
	//   а экранный брал измеренный след и списывал ровно половину — два бюджета расходились.
	//   ★ Дамп печатает это же число (x2/2 вместо прежнего x1/2) — владелец видит, что на сцене
	//     стоят две плоскости, а не одна.
	const bool bBackLitNow = (Spec.Shape == EAsdaFxLightShape::Rect && Spec.bRectTwoSided);
	const int32 NonSplitSources = bBackLitNow ? 2 : 1;
	LastLitSumR2 = (double)Radius * (double)Radius * (double)NonSplitSources;
	LastLitSources = NonSplitSources;
	// ★ fix4 (МЕЛК-а): РАЗМЕР ФОРМЫ ИЗВЕСТЕН И У НЕРАЗЛОЖЕННОЙ. Прежний сброс в ноль обнулял его
	//   каждый кадр, и когда бюджет спрашивал цену ДРУГОГО числа кусков (Ask ≠ выданного), раскладка
	//   предсказывалась по форме нулевого размера — то есть по доле из поля, а не по геометрии.
	//   Для плоскости размер — её ширина, для трубки — полу-длина (ровно то, что кладёт в SizeR
	//   раскладка этих же форм). У точки, шара и конуса размера кусков нет вовсе.
	LastSplitSizeR = (Spec.Shape == EAsdaFxLightShape::Rect) ? RectW
		: (Spec.Shape == EAsdaFxLightShape::Tube) ? TubeHalfLen : 0.f;
	LastSplitParents = 1;   // ★ fix9 (КРИТ-1): форма не разложена — один кусок
	LastPointUsed = 0;
	LastRectUsed = 0;
	// ★ Ф11: шар рисует ТОТ ЖЕ точечный источник движка — отличается только «размер лампы»
	ULocalLightComponent* L = ActiveLight(Spec.Shape == EAsdaFxLightShape::Sphere ? EAsdaFxLightShape::Point : Spec.Shape);

	// ── ГАСИМ ЧУЖИЕ ФОРМЫ ───────────────────────────────────────────────────────────────────────
	//   Форму переключают на живой сцене, и прежний источник обязан уйти из сцены В ТОТ ЖЕ КАДР:
	//   иначе после «точка → прямоугольник» на полу осталось бы ДВА пятна, и это выглядело бы
	//   не как забытый источник, а как удвоенная яркость (то есть чинили бы не то).
	if (L != this && bAffectsWorld)
	{
		bAffectsWorld = false;
		SetVisibility(false);
		MarkRenderStateDirty();
	}
	if (L != (ULocalLightComponent*)RectChild.Get())     { AsdaDouseLight(RectChild.Get()); }
	if (Spec.Shape != EAsdaFxLightShape::Rect || !Spec.bRectTwoSided) { AsdaDouseLight(RectChildBack.Get()); }
	if (L != (ULocalLightComponent*)SpotChild.Get())     { AsdaDouseLight(SpotChild.Get()); }

	// ── ОБЩИЕ ЧИСЛА (одинаковы у всех трёх форм) ────────────────────────────────────────────────
	const float DrawDist = ResolveDrawDistance(Spec);
	L->SetMaxDrawDistance(DrawDist);
	L->SetMaxDistanceFadeRange(Spec.FadeRange);
	L->SetVolumetricScatteringIntensity(Spec.VolumetricScattering * UAsdaDevMenuSubsystem::FogVolumetricLightScale(GetWorld()));
	L->SetAffectGlobalIllumination(Spec.bAffectGI);
	L->SetLightColor(Col, /*bSRGB=*/false);
	L->SetAttenuationRadius(Radius);
	L->SetIntensity(IntensityCd);

	// ── ГЕОМЕТРИЯ ФОРМЫ ─────────────────────────────────────────────────────────────────────────
	if (Spec.Shape == EAsdaFxLightShape::Point)
	{
		// Точка: «размер лампы» — те же два радиуса, что и во всех прежних волнах (мягкость блика).
		SetSourceRadius(Spec.SourceRadius);
		SetSoftSourceRadius(Spec.SoftSourceRadius);
		SetSourceLength(0.f);   // ★ Ф10: после формы «трубка» у точки длины источника быть не должно
		SetWorldLocation(Loc);
	}
	else if (Spec.Shape == EAsdaFxLightShape::Sphere)
	{
		// ★★★ Ф11: ШАР — тот же точечный источник, но «размер лампы» берётся из габарита секции.
		//   Движок у источника с ненулевым SourceRadius считает wrap-освещение по сфере
		//   (CapsuleLightIntegrate.ush:62-67) и расширяет лепесток блика (:98), поэтому крупный
		//   сгусток перестаёт выглядеть уколом света. Цена та же, что у точки: один источник.
		// ★ «форма-2», п. 1: «размер лампы» шара — та же дышащая секция, и удержание ему нужно так же
		//   (иначе шар пульсировал бы из сгустка в укол света вместе с вдохом эмиттера).
		const float SR = (Spec.SphereSizeMode == EAsdaFxRectSizeMode::Fixed)
			? FMath::Max(Spec.SphereRadius, 0.f)
			: FMath::Max(0.5f * ((float)Bs.Half.X + (float)Bs.Half.Y) * ShapeHoldK * FMath::Max(Spec.SphereSizeScale, 0.05f), 0.f);
		// Движок зажимает «размер лампы» десятью тысячами юнитов (PointLightComponent.h:44) — зажимаем сами,
		//   чтобы в панели деталей стояло ровно то число, которым свет и светит
		SetSourceRadius(FMath::Min(SR, 10000.f));
		SetSoftSourceRadius(Spec.SoftSourceRadius);
		SetSourceLength(0.f);
		SetWorldLocation(Bs.C);
	}
	else if (Spec.Shape == EAsdaFxLightShape::Tube)
	{
		// ★★★ Ф10: ТРУБКА — наш же точечный источник с «длиной источника» вдоль главной оси эмиттера.
		//   Столб светит вдоль всей своей высоты, а не из одной точки посередине: пол у основания и
		//   персонаж рядом со столбом получают свет, которого точка в центре тяжести не давала.
		// ★ Ф10-испр.: ось и длина уже согласованы с радиусом в PrepareTube (основание столба в зоне света)
		const FVector Axis = TubeAxis;
		const float Len = 2.f * TubeHalfLen;
		SetSourceRadius(Spec.SourceRadius);
		SetSoftSourceRadius(Spec.SoftSourceRadius);
		SetSourceLength(Len);
		// Трубка точечного света движка лежит вдоль ЛОКАЛЬНОЙ Z (PointLightSceneProxy.cpp: Tangent = столбец Z)
		SetWorldLocationAndRotation(Bs.C, FRotationMatrix::MakeFromZ(Axis.IsNearlyZero() ? FVector::UpVector : Axis).Rotator());
	}
	else
	{
		// ★ ОСИ СЕКЦИИ уже посчитаны в ResolveShapeBasis (Якоби по ковариации — EmitterSectionFrame)
		//   и там же довёрнуты полем «доворот формы». Второй раз геометрию не читаем.
		//   Прямоугольнику нужны все три (они уже вошли в RectRot выше), конусу — только главная.
		const FVector AxA = Bs.A;

		if (Spec.Shape == EAsdaFxLightShape::Rect)
		{
			URectLightComponent* R = Cast<URectLightComponent>(L);
			if (R)
			{
				const float W = RectW, H = RectH;
				const FRotator Rot = RectRot;
				R->SetSourceWidth(W);
				R->SetSourceHeight(H);
				R->SetBarnDoorAngle(Spec.BarnDoorAngle);
				R->SetBarnDoorLength(Spec.BarnDoorLength);

				// ★ ПОЗИЦИЯ — ИЗ ОПОРНОЙ СИСТЕМЫ ФОРМЫ. При привязке «там же, где источник» (дефолт)
				//   `Bs.C` тождественно равен `Loc`, то есть поведение волн Ф8…Ф10 не меняется;
				//   при прочих привязках плоскость ложится туда, куда её велел положить владелец.
				R->SetWorldLocationAndRotation(Bs.C, Rot);

				// «Светит с обеих сторон» — вторая плоскость, развёрнутая на 180° по своей ширине.
				if (Spec.bRectTwoSided)
				{
					if (!RectChildBack)
					{
						if (AActor* Owner = GetOwner())
						{
							RectChildBack = NewObject<URectLightComponent>(Owner, NAME_None, RF_Transient);
							if (RectChildBack)
							{
								AsdaInitFxChildLight(RectChildBack);
								RectChildBack->SetupAttachment(this);
								RectChildBack->RegisterComponent();
							}
						}
					}
					if (RectChildBack)
					{
						RectChildBack->SetSourceWidth(R->SourceWidth);
						RectChildBack->SetSourceHeight(R->SourceHeight);
						RectChildBack->SetBarnDoorAngle(Spec.BarnDoorAngle);
						RectChildBack->SetBarnDoorLength(Spec.BarnDoorLength);
						RectChildBack->SetMaxDrawDistance(DrawDist);
						RectChildBack->SetMaxDistanceFadeRange(Spec.FadeRange);
						RectChildBack->SetVolumetricScatteringIntensity(Spec.VolumetricScattering * UAsdaDevMenuSubsystem::FogVolumetricLightScale(GetWorld()));
						RectChildBack->SetAffectGlobalIllumination(Spec.bAffectGI);
						RectChildBack->SetLightColor(Col, /*bSRGB=*/false);
						RectChildBack->SetAttenuationRadius(Radius);
						RectChildBack->SetIntensity(IntensityCd);
						RectChildBack->SetWorldLocationAndRotation(Bs.C, (Rot.Quaternion() * FQuat(FVector::UpVector, PI)).Rotator());
						if (!RectChildBack->bAffectsWorld)
						{
							RectChildBack->bAffectsWorld = true;
							RectChildBack->SetVisibility(true);
							RectChildBack->MarkRenderStateDirty();
						}
					}
				}
			}
		}
		else   // Spot
		{
			USpotLightComponent* Sp = Cast<USpotLightComponent>(L);
			if (Sp)
			{
				// ★ УГЛЫ. Внутренний зажимается внешним: движок при inner > outer рисует «вывернутый»
				//   конус, и это выглядит как случайно погасший свет, а не как ошибка настройки.
				const float Outer = FMath::Clamp(Spec.SpotOuterCone, 1.f, 80.f);
				Sp->SetOuterConeAngle(Outer);
				Sp->SetInnerConeAngle(FMath::Clamp(Spec.SpotInnerCone, 0.f, Outer));
				Sp->SetSourceRadius(Spec.SourceRadius);
				Sp->SetSoftSourceRadius(Spec.SoftSourceRadius);

				// НАПРАВЛЕНИЕ. +X компонента — ось конуса.
				FRotator Rot;
				switch (Spec.SpotDirMode)
				{
				// ★ Ф11: «доворот формы» складывается с любым из режимов — у осей секции он уже
				//   учтён (ResolveShapeBasis), у мировых направлений добавляется здесь.
				case EAsdaFxSpotDirMode::Explicit: Rot = Spec.SpotRotation + Spec.ShapeRotation; break;
				case EAsdaFxSpotDirMode::ActorFwd: Rot = (Fx ? Fx->GetActorRotation() : FRotator::ZeroRotator) + Spec.ShapeRotation; break;
				case EAsdaFxSpotDirMode::Section:  Rot = Bs.bHaveFrame ? AxA.Rotation() : FRotator(-90.f, 0.f, 0.f) + Spec.ShapeRotation; break;
				default:                           Rot = FRotator(-90.f, 0.f, 0.f) + Spec.ShapeRotation; break;   // вниз
				}
				Sp->SetWorldLocationAndRotation(Bs.C, Rot);
			}
		}
	}

	// ── ТЕНЬ ────────────────────────────────────────────────────────────────────────────────────
	ApplyShadowFrame(Spec, bWantShadow, L);

	// ── ЗАЖИГАНИЕ ───────────────────────────────────────────────────────────────────────────────
	if (!L->bAffectsWorld)
	{
		L->bAffectsWorld = true;
		L->SetVisibility(true);
		L->MarkRenderStateDirty();
	}
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ Ф10-испр. (ревизия 11.09.2026): СОН НЕВИДИМОГО, ТРУБКА ПО РАДИУСУ, УБОРКА ДОЧЕРНИХ.
// ═══════════════════════════════════════════════════════════════════════════════════════════════════
float UAsdaFxLightComponent::ProbeAlpha(float DeltaTime)
{
	// Свежий замер есть (кадр света этого кадра или зонд моложе 0.1 с) — берём его
	ProbeAge += FMath::Max(DeltaTime, 0.f);
	if (ProbeAge < 0.1f) { return LastProbeAlpha; }
	ProbeAge = 0.f;
	const AAsdaEffectActor* Fx = Effect.Get();
	if (!Fx) { LastProbeAlpha = 0.f; return 0.f; }
	// ★ ТОТ ЖЕ проход и та же развилка «агрегат / донор», что в ApplyFrame (закон diagnostic-must-use-real-path)
	const FAsdaFxLightSpec Spec = ResolveSpec();
	FAsdaFxLightAgg Agg;
	GatherEmitters(Fx, Spec, DonorEmitter, Agg, EmitterIndex);
	const bool bAggregate = EmitterIndex >= 0 ||
		Spec.ColorMode == EAsdaFxLightColorMode::Mean || Spec.ColorMode == EAsdaFxLightColorMode::Median ||
		Spec.RadiusMode == EAsdaFxLightRadiusMode::Bounds ||
		Spec.Position == EAsdaFxLightPosMode::Bounds || Spec.Position == EAsdaFxLightPosMode::BoundsBox;
	const float A = bAggregate ? (Agg.Live > 0 ? Agg.Alpha : 0.f) : (Agg.bDonorLive ? Agg.DonorAlpha : 0.f);
	// ★★★ ВОЛНА «форма-2», п. 6 ревизии (миснеймы в дампе): ЗОНД ЗАПОМИНАЕТ ПРИЧИНУ ТЕМНОТЫ.
	//   Отбор снимает тёмного кандидата в спящие и писал в дамп ОДНО слово — «альфа узла в нуле:
	//   эффект догорел», — каким бы ни была настоящая причина. У эмиттера с вырожденной геометрией
	//   (`ch1_a_4th_mg_02_2` #5 `Bil_002`: размер узла 0 во всех моментах периода) это прямой
	//   миснейм: он не догорел, ему нечем светить. Тот же класс, что «бюджет (ключ)» в шапке дампа.
	// ★ Ревизия (СРЕДН-6): развилка ТА ЖЕ, что в ApplyFrame — оба места отвечают одинаково
	const int32 EmittersNum = Fx->Emitters.Num();
	DormantGate = (Agg.OffByOwner > 0 && Agg.OffByOwner >= EmittersNum) ? EAsdaFxLightGate::AllEmittersOff
		: (Agg.WithRuntime == 0 ? EAsdaFxLightGate::NoRuntime
		: (Agg.WithSection == 0
			? ((Agg.DegenerateSection > 0 && Agg.DegenerateSection >= Agg.WithRuntime)
				? EAsdaFxLightGate::DegenerateGeom : EAsdaFxLightGate::NoSection)
			: EAsdaFxLightGate::AlphaZero));
	LastProbeAlpha = FMath::Clamp(A * Fx->FxAlphaScale, 0.f, 1.f);
	return LastProbeAlpha;
}

bool UAsdaFxLightComponent::UpdateDormancy(float Alpha, float DeltaTime)
{
	if (Alpha > KINDA_SMALL_NUMBER) { DarkTime = 0.f; bDormant = false; return false; }
	DarkTime += FMath::Max(DeltaTime, 0.f);
	if (!bLitNow && LitRamp <= 0.f) { bDormant = true; }       // не светил — гасить нечего, спит сразу
	else if (DarkTime >= 0.25f)      { bDormant = true; }       // светил — ждём, пока темнота не устоится
	return bDormant;
}

void UAsdaFxLightComponent::PrepareTube(const FAsdaFxLightSpec& Spec, const FAsdaFxShapeBasis& Bs, int32 GeomEmitter,
	float LvlCap, bool bSnap, float& InOutRadius)
{
	const FAsdaFxShapeGeom G = AsdaShapeGeom(Effect.Get(), GeomEmitter);
	// ★ Ф11: длина — из габарита (как было) или задана руками.
	//   ★ fix5 (СРЕДН-Д): полу-длина из габарита — через сетку (довод общий, см. ApplyDiscFrame)
	//   ★★★ fix9 (КРИТ-1): формула переехала в MeasureShapeSizeR — её ЖЕ спрашивает решение о числе
	//     капсул, и обе стороны видят полу-длину ЭТОГО кадра.
	const float Half = MeasureShapeSizeR(Spec, EAsdaFxLightShape::Tube, Bs, GeomEmitter, InOutRadius);
	TubeAxis = (G.bOk ? G.Major : FVector::UpVector).GetSafeNormal();
	// ★ Ф11: доворот формы крутит и ось трубки — иначе «доворот» работал бы у ленты и молчал у столба
	if (!Spec.ShapeRotation.IsNearlyZero()) { TubeAxis = Spec.ShapeRotation.Quaternion().RotateVector(TubeAxis).GetSafeNormal(); }
	if (TubeAxis.IsNearlyZero()) { TubeAxis = FVector::UpVector; }

	// ★★★ Ф11: У РАЗЛОЖЕННОЙ ТРУБКИ РАДИУС РАСТЯГИВАТЬ НЕ НАДО. Требование «конец трубки в зоне
	//   собственного света» решается длиной КУСКА, а не общим радиусом: при N кусках полу-длина куска
	//   равна Half/N, и каждый кусок укладывается в свою сферу сам. Именно поэтому раскладка снимает
	//   и жалобу «свет из центра», и вынужденное раздувание радиуса, которым до Ф11 её лечили
	//   (а радиус — самая дорогая величина механизма, см. поле «Радиус макс»).
	// ★★★ РЕВИЗИЯ fix5 (СРЕДН-Г, 20.09.2026): РАЗДУВАНИЕ РАДИУСА У НЕРАЗЛОЖЕННОЙ ТРУБКИ УБРАНО.
	//   ЧТО БЫЛО НЕ ТАК. Правило fix4 «кусок не светит дальше, чем светил бы весь источник» применили
	//   ко ВСЕМ разложенным формам, но одиночная капсула шла мимо него: строка `max(R, Half + ½R)`
	//   поднимала радиус ПОД ДЛИНУ — замер ревизии: tube1 получал R = 800 юн при собственных 400, то
	//   есть экранный след ВЧЕТВЕРО больше точки, и бюджет это лишь ПРЕДСКАЗЫВАЛ (врезка в
	//   EstimateScreenArea), а не зажимал. Одна форма из шести жила по своему закону.
	//   КАК СТАЛО. Радиус трубки — её собственный, как у всех. Длину кроют ПЛИТКИ ВСТЫК (КРИТ-А):
	//   капсула, которая до своей доли длины не дотягивается, раскладывается на несколько. Если
	//   плитки запрещены полем (потолок 1), длина режется тем же правилом 0.66, что и раньше, —
	//   то есть прежнее поведение остаётся ровно там, где владелец его попросит явно.
	const int32 NSeg = FMath::Clamp(Spec.ShapeSegments, 1, 16);
	float R = FMath::Min(InOutRadius, FMath::Max(LvlCap, 1.f));
	if (bSnap && GAsdaFxShadowRadiusSnap > 0.f) { R = FMath::Max(AsdaSnap(R, GAsdaFxShadowRadiusSnap), GAsdaFxShadowRadiusSnap); }
	// Сколько кусков выйдет и какой длины — спрашиваем У ТОЙ ЖЕ раскладки, что построит кадр света.
	//   Второй расчёт тех же чисел разошёлся бы с первым (закон port-shared-points).
	const FAsdaFxSplit Sp = SplitGeometry(EAsdaFxLightShape::Tube, NSeg, Half, R,
		ShapeRadiusShare(Spec, EAsdaFxLightShape::Tube), FMath::Max(LvlCap, 1.f),
		FMath::Clamp(Spec.ShapeCoverTiles, 1, 8));
	TubePieces = Sp.N;
	// Один кусок — трубка горит САМИМ компонентом (и может нести тень), и её длина берётся из
	//   раскладки: та уже срезала её правилом 0.66, если плитки запрещены. Кусков больше одного —
	//   длина остаётся полной, куски режет ApplyTubeSegments.
	TubeHalfLen = (Sp.N <= 1) ? 0.5f * Sp.Len[0] : Half;
	InOutRadius = R;
}

void UAsdaFxLightComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// Слот в бюджете теней отпускаем явно — дочерний источник мог его держать (форма не точечная)
	UWorld* W = GetWorld();
	if (bShadowWanted && W && !W->bIsTearingDown)
	{
		ApplyShadowFrame(FAsdaFxLightSpec(), /*bWant*/false, ShadowTarget.IsValid() ? ShadowTarget.Get() : this);
	}
	auto Kill = [](UActorComponent* C) { if (IsValid(C) && !C->IsBeingDestroyed()) { C->DestroyComponent(); } };
	Kill(RectChild.Get());
	Kill(RectChildBack.Get());
	Kill(SpotChild.Get());
	for (UPointLightComponent* P : PointChildren) { Kill(P); }
	for (URectLightComponent* R : RectChildren) { Kill(R); }
	RectChild = nullptr; RectChildBack = nullptr; SpotChild = nullptr;
	PointChildren.Reset();
	RectChildren.Reset();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
