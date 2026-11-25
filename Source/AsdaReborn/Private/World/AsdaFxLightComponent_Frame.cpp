// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightComponent_Frame.cpp — ★ R2 10.11.2026 (разрез AsdaFxLightComponent.cpp, дословный перенос): ApplyFrame (49 КБ), дочерние/активные огни, гашение форм.
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

void UAsdaFxLightComponent::ApplyFrame(float DeltaTime)
{
	AAsdaEffectActor* Fx = Effect.Get();
	// ГЕЙТ 2: эффект умер — компонент больше не о чем ведёт, снимаем себя (подсистема вычистит ссылку)
	if (!IsValid(Fx))
	{
		// Компонент живёт НА акторе эффекта, поэтому сюда попадаем только в кадр его разрушения:
		// гасим и замолкаем, а ссылку вычистит ближайший отбор подсистемы (DestroyComponent из
		// собственного тика — лишний риск на ровном месте).
		Extinguish(EAsdaFxLightGate::NoEffect);
		SetComponentTickEnabled(false);
		bAdmitted = false;
		return;
	}

	// ★ Ревизия Ф11 (М4) → ★★★ РЕВИЗИЯ fix3 (С-5): УБОРКА ЛИШНИХ ДОЧЕРНИХ ИСТОЧНИКОВ ОТСЮДА УБРАНА.
	//   Ревизия Ф11 поставила её в начало кадра света, но кадр света считается ТОЛЬКО у кандидата
	//   с местом в бюджете: у вытесненного тик компонента выключен (SetAdmitted → SetComponentTickEnabled),
	//   и подсистема зовёт ApplyFrame только для admitted. То есть уборка не доходила ровно до
	//   главного случая накопления — эффекта, который перебрали формами и вытеснили из бюджета.
	//   Теперь TrimShapePools зовёт подсистема по ВСЕМ кандидатам (см. UAsdaFxLightSubsystem::Select).

	// ★ ЧИСЛА ЭТОГО КАДРА. Перечитываются каждый тик — именно поэтому правка поля в панели деталей
	//   меняет свет во вьюпорте немедленно, без перезапуска и без пересоздания компонента.
	const FAsdaFxLightSpec Spec = ResolveSpec();

	// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 3 ревизии: ДОНОР, КОТОРОМУ СНЯЛИ СВЕЧЕНИЕ, ПЕРЕВЫБИРАЕТСЯ.
	//   Донор фиксируется ОДИН раз в SetupForEffect (иначе свет прыгал бы между эмиттерами), а сверка
	//   набора при охвате «один на эффект» сравнивать его не может вовсе: набор там — один элемент
	//   «общий», и он не меняется, что бы владелец ни погасил. Без этой строки гашение ДОНОРА у
	//   одиннадцати семейств давало бы худший из исходов: сам донор выбыл из агрегата (см.
	//   GatherEmitters), а откатные режимы «цвет из узла донора» / «радиус из геометрии донора» /
	//   «позиция центроида донора» молча гасили бы свет гейтом «донор не жив».
	//   ★ Перевыбор стоит РОВНО НА ОДНО УСЛОВИЕ: текущий донор помечен «Света у этого эмиттера нет».
	//     В обычной жизни (никто ничего не гасил) ветка не исполняется ни разу — проверка стоит один
	//     доступ к массиву. Поэмиттерного света это не касается: там донор — сам эмиттер, и его
	//     собственное гашение сносит весь компонент.
	if (EmitterIndex < 0 && Fx->Emitters.IsValidIndex(DonorEmitter)
		&& Fx->Emitters[DonorEmitter].Glow.Source == EAsdaFxEmitterGlowSource::Off)
	{
		const int32 NewDonor = PickDonorEmitter(Fx);
		if (NewDonor != DonorEmitter)
		{
			UE_LOG(LogAsdaFxLightComp, Log,
				TEXT("[донор] %s: эмиттеру #%d снято свечение владельцем — %s"),
				*GetNameSafe(Fx), DonorEmitter,
				NewDonor == INDEX_NONE
					? TEXT("незагашенных эмиттеров не осталось, донора нет — свет погаснет")
					: *FString::Printf(TEXT("донор перевыбран на #%d"), NewDonor));
			DonorEmitter = NewDonor;
		}
	}

	// ГЕЙТ 1, 3 и 8: общий откат / место в бюджете не выдано / свет выключен этому эффекту
	if (!Spec.bEnabled || !bAdmitted || !UAsdaFxLightSubsystem::IsFeatureEnabled())
	{
		Extinguish(!Spec.bEnabled ? EAsdaFxLightGate::Disabled
			: (!UAsdaFxLightSubsystem::IsFeatureEnabled() ? EAsdaFxLightGate::FeatureOff : EAsdaFxLightGate::NotAdmitted));
		return;
	}

	// ГЕЙТ 4: ОБЩАЯ дальность, домноженная тем же уровневым множителем, которым движок гасит свет
	UAsdaFxLightSubsystem* Sub = UAsdaFxLightSubsystem::Get(this);
	FVector View;
	if (!Sub || !Sub->GetViewLocation(View)) { Extinguish(EAsdaFxLightGate::NoView); return; }
	const float DrawDist = ResolveDrawDistance(Spec);
	// ★ Ф6: дистанция считается ОДИН раз и переиспользуется гейтом тени («Тень видна до»): второй вызов
	//   FVector::Dist по тем же точкам — это тот самый второй способ считать одно и то же, который потом расходится.
	const float DistToView = (float)FVector::Dist(View, Fx->GetActorLocation());
	// ★★★ ВОЛНА Ф9: В РЕЖИМЕ ОТБОРА ПО КАДРУ ЭТОГО ГЕЙТА НЕТ. Попадание в кадр проверено подсистемой
	//   в ОТБОРЕ (Select), и до сюда доходит только тот, кому выдано место: тик компонента включается
	//   ровно вместе с местом (SetAdmitted). Второй проверки здесь быть не должно — она обязана была бы
	//   повторить весь расширенный конус с гистерезисом и предсказанием и рано или поздно разошлась бы
	//   с первым (закон port-shared-points). `DistToView` остаётся: он нужен гейту ТЕНИ, у которой
	//   дальность своя и по-прежнему дистанционная.
	if (!UAsdaFxLightSubsystem::IsFrustumCull() && DistToView > DrawDist * UAsdaRenderQualitySubsystemBase::LightDrawDistanceScale()) { Extinguish(EAsdaFxLightGate::TooFar); return; }

	// ГЕЙТ 5: гейт значимости эффекта. Уровень 0 = симуляция заморожена (Dt не копится, геометрия стоит) —
	//   свет обязан погаснуть вместе с ней, иначе на карте останется «свет от невидимого огня».
	if (Fx->GetVisLevel() == 0) { Extinguish(EAsdaFxLightGate::VisLevel0); return; }

	// ★★★ ВОЛНА Ф4: ОДИН ПРОХОД ПО ВСЕМ ЭМИТТЕРАМ — из него и позиция, и радиус, и цвет, и альфа.
	//   Донорские числа снимаются ТЕМ ЖЕ проходом, поэтому откатные режимы не читают эффект второй раз.
	FAsdaFxLightAgg Agg;
	// ★★★ ВОЛНА Ф8: поэмиттерный свет собирает ТОЛЬКО свой эмиттер (EmitterIndex >= 0).
	GatherEmitters(Fx, Spec, DonorEmitter, Agg, EmitterIndex);

	// bAggregate = хотя бы одна из трёх величин считается по ВСЕМ эмиттерам. Ровно от него зависит,
	// чью альфу брать: агрегатную (догоревший эмиттер не гасит живых) или донорскую (точное поведение Ф2).
	// Один флаг вместо трёх развилок — иначе «цвет средний, а гаснет по донору» стало бы необъяснимым.
	// ★★★ ВОЛНА Ф8: у ПОЭМИТТЕРНОГО света агрегат — всегда, и это не оговорка: собран он по ОДНОМУ
	//   эмиттеру, поэтому «средний по всем живым» и «донорский» дают одно и то же число, а ветка
	//   агрегата не требует, чтобы донор был жив ОТДЕЛЬНО от него самого.
	const bool bAggregate = EmitterIndex >= 0 ||
		Spec.ColorMode == EAsdaFxLightColorMode::Mean || Spec.ColorMode == EAsdaFxLightColorMode::Median ||
		Spec.RadiusMode == EAsdaFxLightRadiusMode::Bounds ||
		Spec.Position == EAsdaFxLightPosMode::Bounds || Spec.Position == EAsdaFxLightPosMode::BoundsBox;

	// ГЕЙТ 6: светить не от чего. Секции нет (StartDelay не прошёл, эффект дистанц-culled и почистил
	//   геометрию, EndMode == 0 остановил эмиттер). ★ Причину называем ТОЧНО: «ни один эмиттер не жив» и
	//   «живы, но геометрии нет» — разные поломки, и путать их нельзя (закон Ф3 «гейт обязан назвать себя»).
	if (bAggregate)
	{
		if (Agg.Live == 0)
		{
			LastProbeAlpha = 0.f; ProbeAge = 0.f;   // ★ Ф10-испр. v2: отбор увидит «тёмен» без второго прохода
			// ★ Волна «форма-2»: если НИ ОДНОГО эмиттера не осталось вовсе и все выбывшие сняты
			//   владельцем — так и говорим. Иначе владелец, погасивший все эмиттеры, читал бы
			//   «эмиттер-донор стоит или не запущен» и искал бы поломку в механизме эффекта.
			// ★★★ РЕВИЗИЯ «форма-2» (СРЕДН-6): ГЕЙТ НАЗЫВАЕТСЯ ТОЛЬКО ТОГДА, КОГДА ОН И ЕСТЬ ПРИЧИНА.
			//   Первая редакция ставила `AllEmittersOff` при `OffByOwner > 0 && WithRuntime == 0`, то
			//   есть достаточно было ОДНОГО погашенного эмиттера при том, что остальные просто не
			//   запущены, — и владелец читал «я всё погасил» там, где он погасил один дым.
			//   То же с `DegenerateGeom`: одна вырожденная секция при непостроенных остальных.
			//   Теперь причина называется, когда выбывшие ЭТОЙ причиной — ВСЕ:
			//     `AllEmittersOff`  — погашенных владельцем ровно столько, сколько эмиттеров у эффекта;
			//     `DegenerateGeom`  — все, у кого рантайм есть, выбыли вырождением.
			const int32 EmittersNum = Fx->Emitters.Num();
			Extinguish(
				(Agg.OffByOwner > 0 && Agg.OffByOwner >= EmittersNum) ? EAsdaFxLightGate::AllEmittersOff
				: (Agg.WithRuntime == 0 ? EAsdaFxLightGate::NoRuntime
				: (Agg.WithSection == 0
					? ((Agg.DegenerateSection > 0 && Agg.DegenerateSection >= Agg.WithRuntime)
						? EAsdaFxLightGate::DegenerateGeom : EAsdaFxLightGate::NoSection)
					: EAsdaFxLightGate::AlphaZero)));
			return;
		}
	}
	else if (!Agg.bDonorLive)
	{
		LastProbeAlpha = 0.f; ProbeAge = 0.f;       // ★ Ф10-испр. v2: донор не жив — эффект тёмен
		// Донорский путь: эмиттер стоит/не запущен, либо у него нет построенной геометрии
		bool bStopped = false; float LT = 0.f;
		Extinguish(Fx->GetEmitterRuntime(DonorEmitter, bStopped, LT) && !bStopped
			? EAsdaFxLightGate::NoSection : EAsdaFxLightGate::NoRuntime);
		return;
	}

	// ГЕЙТ 7: эффект догорел — альфа этого кадра в нуле
	const float A = bAggregate ? Agg.Alpha : Agg.DonorAlpha;
	// ★ Ф10-испр. v2: альфа этого кадра — для сна в отборе (та же, что гасит свет ниже, × скрытие оператора)
	LastProbeAlpha = FMath::Clamp(A * Fx->FxAlphaScale, 0.f, 1.f);
	ProbeAge = 0.f;
	if (A <= KINDA_SMALL_NUMBER) { Extinguish(EAsdaFxLightGate::AlphaZero); return; }

	// ── ПОЗИЦИЯ ─────────────────────────────────────────────────────────────────────────────────────────
	//   BoundsBox — СЕРЕДИНА КОРОБКИ объединения секций, БЕЗ весов (волна Ф7): у столбовых эффектов
	//             она выше центра тяжести, и источник перестаёт прятаться внутри каменного основания —
	//             тень расходится кольцом, а не клином (претензия владельца 10.09, замер Reports/fx_shadow2/);
	//   Bounds  — ЦЕНТР ТЯЖЕСТИ ПО ВКЛАДУ всех живых эмиттеров (дефолт Ф4): у костра с дымом середина
	//             коробки уехала бы в дым (он выше и шире), а центр тяжести остаётся в пламени;
	//   Section — центроид геометрии ОДНОГО донора (поведение Ф2);
	//   Actor   — позиция эффекта + смещение (эффекты, чья геометрия расползается: дождь искр, шлейф).
	//   Вершины эффекта МИРОВЫЕ (AsdaEffectActor.cpp: построение секции), пересчёт не нужен.
	FVector Loc;
	switch (Spec.Position)
	{
	case EAsdaFxLightPosMode::Bounds:  Loc = Agg.Centroid; break;
	case EAsdaFxLightPosMode::BoundsBox: Loc = Agg.BoxCenter; break;
	case EAsdaFxLightPosMode::Section: Loc = Agg.bDonorLive ? Agg.DonorCentroid : Agg.Centroid; break;
	default:                           Loc = Fx->GetActorLocation(); break;
	}
	Loc += Spec.Offset;
	// ★ Ф6: у ТЕНЕВОГО света положение прищёлкивается к сетке — иначе дрожание центра тяжести на доли юнита
	//   каждый кадр сбрасывает кэш кубической карты и вся статика перерисовывается в шесть граней заново.
	//   Гистерезис (сдвинулись дальше шага → пересчитали узел сетки) вместо голого GridSnap: на границе ячейки
	//   голый снап щёлкал бы туда-сюда и сбрасывал кэш ровно так же, как несглаженная позиция.
	const bool bWantShadow = WantsShadow(Spec, DistToView);
	if (bWantShadow && GAsdaFxShadowSnap > 0.f)
	{
		if (!bHaveSnap || FVector::Dist(Loc, SnappedLoc) > GAsdaFxShadowSnap)
		{
			SnappedLoc = FVector(AsdaSnap((float)Loc.X, GAsdaFxShadowSnap), AsdaSnap((float)Loc.Y, GAsdaFxShadowSnap), AsdaSnap((float)Loc.Z, GAsdaFxShadowSnap));
			bHaveSnap = true;
		}
		Loc = SnappedLoc;
	}
	else { bHaveSnap = false; }
	// ★ ВОЛНА Ф8: позицию ставит ApplyShapeFrame — у ТОГО источника, который светит (у прямоугольника
	//   и конуса вместе с поворотом, которого у точки нет вовсе).

	// ── РАДИУС ──────────────────────────────────────────────────────────────────────────────────────────
	// ★ ТРЕБОВАНИЕ ВЛАДЕЛЬЦА «чем больше эффект, тем больше радиус»: источник размера — ГАБАРИТ
	//   ОБЪЕДИНЕНИЯ секций живых эмиттеров, а не радиус секции одного донора.
	float Radius = Spec.RadiusMax;
	float Geom = 0.f;
	// ═══ ★★★ УДЕРЖАНИЕ ГАБАРИТА — ДВА НЕЗАВИСИМЫХ ЧИСЛА (ревизия «форма-2», СРЕДН-1) ═══════════════
	//
	//   ЧТО БЫЛО НЕ ТАК В ПЕРВОЙ РЕДАКЦИИ ВОЛНЫ. Множитель `ShapeHoldK` выводился из источника
	//   РАДИУСА, а умножался на полу-габариты формы ОДНОГО эмиттера. При охвате «свой свет у каждого
	//   эмиттера» (3 семейства, на которых волна и мерила) это одна и та же секция, и множитель
	//   когерентен. Но при охвате «один на эффект» (11 семейств из 18, и `radius_mode` у всех них —
	//   `bounds`) источник радиуса — ОХВАТЫВАЮЩАЯ СФЕРА ОБЪЕДИНЕНИЯ секций ВСЕХ эмиттеров, а форму
	//   меряет секция донора. Эти две величины дышат по-разному: объединение может стоять, пока
	//   секция донора схлопывается (тогда K ≈ 1 и форма схлопнется ВСЁ РАВНО — правка не работает),
	//   и наоборот (тогда форма раздуется без причины). То есть у одиннадцати семейств из
	//   восемнадцати правка первой редакции не делала того, что обещала.
	//
	//   ЛЕЧЕНИЕ: каждая величина удерживает СЕБЯ.
	//     `HeldGeomRaw`  — источник РАДИУСА (агрегат или донор, как решит `bFromBounds`);
	//     `HeldShapeRaw` — радиус секции ТОГО ЭМИТТЕРА, чьи оси и полу-габариты дают форму
	//                      (`Agg.DonorRadius`: `GatherEmitters` снимает донора тем же проходом, а у
	//                      поэмиттерного света донор — сам эмиттер, см. `SetupForEffect`).
	//   При охвате «по эмиттерам» обе величины по-прежнему об одной секции, и числа замеров волны
	//   сдвинуться не могут.
	//
	//   ★ СТАРЕНИЕ СТОИТ ЗДЕСЬ, ДО ВСЕХ ВЕТВЛЕНИЙ, И ЭКСПОНЕНТОЙ (ревизия, мелкое):
	//     1. первая редакция считала спад Эйлером `H -= H·dt/T` — он зависит от частоты кадров, а при
	//        `dt >= T` МОЛЧА ВЫКЛЮЧАЛ удержание на просадке кадра (именно тогда, когда оно и нужно);
	//     2. спад выполняется ДО гейтов, поэтому память стареет и пока свет погашен. Иначе эффект,
	//        вернувшийся из темноты, на кадр брал бы устаревший раздутый габарит.
	//   `exp(-dt/T)`: за T значение падает втрое (0.368) при любой частоте кадров.
	{
		const float HoldSec = UAsdaFxLightSubsystem::ResolveGeomHoldSec(Spec.GeomHoldSec);
		if (HoldSec > KINDA_SMALL_NUMBER)
		{
			const float Decay = FMath::Exp(-FMath::Max(DeltaTime, 0.f) / HoldSec);
			HeldGeomRaw *= Decay;
			HeldShapeRaw *= Decay;
		}
		else { HeldGeomRaw = 0.f; HeldShapeRaw = 0.f; }   // удержание выключено ⇒ памяти нет вовсе
	}
	// Радиус секции ЭМИТТЕРА ФОРМЫ этого кадра. Донора нет (не жив) — формы всё равно не будет,
	// и удерживать нечего: берём ноль, множитель выйдет 1.
	const float ShapeNow = Agg.bDonorLive ? FMath::Max(Agg.DonorRadius, 0.f) : 0.f;
	HeldShapeRaw = FMath::Max(HeldShapeRaw, ShapeNow);
	// ★ ВО СКОЛЬКО РАЗ УДЕРЖАНИЕ ПОДНИМАЕТ РАЗМЕР ФОРМЫ. Потолок 64 — чтобы секция, схлопнувшаяся
	//   В НОЛЬ, не дала бесконечного множителя.
	ShapeHoldK = (ShapeNow > KINDA_SMALL_NUMBER)
		? FMath::Clamp(HeldShapeRaw / ShapeNow, 1.f, 64.f)
		: 1.f;
	if (Spec.RadiusMode != EAsdaFxLightRadiusMode::Fixed)
	{
		// ★ У КАЖДОГО РЕЖИМА СВОЙ МНОЖИТЕЛЬ. Габарит объединения на порядок больше радиуса секции донора
		//   (замер 10.09: 52 юн против 1003 у навыка 4-й профессии), и общий множитель означал бы, что
		//   один из двух режимов заведомо расколиброван. См. FAsdaFxLightSpec::RadiusBoundsScale.
		const bool bFromBounds = (Spec.RadiusMode == EAsdaFxLightRadiusMode::Bounds) || !Agg.bDonorLive;
		const float SrcNow = bFromBounds ? Agg.GeomRadius : Agg.DonorRadius;
		HeldGeomRaw = FMath::Max(HeldGeomRaw, SrcNow);
		Geom = HeldGeomRaw * (bFromBounds ? Spec.RadiusBoundsScale : Spec.RadiusScale);
		Radius = FMath::Clamp(Geom, Spec.RadiusMin, Spec.RadiusMax);
	}
	// ★ ЧЕМ РАДИУС СРЕЗАЛО — записываем ЗДЕСЬ, а не пересчитываем в дампе: пересчёт второй формулой
	//   и есть тот способ, которым «потолок не режет» доказывают ложно (закон false-negatives-method).
	LastRadiusGeom = Geom;
	LastClip = (Spec.RadiusMode == EAsdaFxLightRadiusMode::Fixed) ? 0
		: (Geom > Spec.RadiusMax ? 2 : (Geom < Spec.RadiusMin ? 1 : 0));
	// Отладочный множитель радиуса (asda.Fx.Light.RadiusScale) — поверх зажима чисел: замер цены по радиусу
	Radius *= UAsdaFxLightSubsystem::GlobalRadiusScale();
	// ★ УРОВНЕВЫЙ ПОТОЛОК РАДИУСА ([AsdaFxLight@N] Light.FxRadiusCap). Цена бестеневого света растёт с
	//   площадью его экранного следа, поэтому на слабых уровнях ограничивать надо в первую очередь радиус,
	//   а не количество (замер волны Ф2 §4: удвоение радиуса двух светов = цена сотни светов обычного).
	// ★ Ф10: потолок — в юнитах ЭТОГО мира (логин ×100), та же величина, что в отборе подсистемы
	const float LvlCap = UAsdaFxLightSubsystem::ResolveRadiusCap() * UAsdaFxLightSubsystem::WorldLengthScale(GetWorld());
	if (Radius > LvlCap) { Radius = LvlCap; LastClip = 3; }

	// ── ЦВЕТ ────────────────────────────────────────────────────────────────────────────────────────────
	FLinearColor Col = FLinearColor::White;
	if (Spec.ColorMode == EAsdaFxLightColorMode::Fixed)
	{
		Col = FLinearColor(Spec.ColorSRGB);
	}
	else
	{
		// Mean/Median — агрегат по всем живым эмиттерам (уже нормирован по максимальной компоненте);
		// Node — оттенок узла ОДНОГО донора (поведение Ф2), нормируется здесь той же функцией.
		Col = (Spec.ColorMode == EAsdaFxLightColorMode::Node && Agg.bDonorLive) ? AsdaChroma(Agg.DonorColor) : Agg.Color;
		// Насыщенность: подмешиваем белый (у фонаря плашка насыщенно-жёлтая, а свет фонаря почти белый)
		const float S = FMath::Clamp(Spec.ColorSaturation, 0.f, 1.f);
		Col = FMath::Lerp(FLinearColor::White, Col, S);
	}

	// ── СИЛА ────────────────────────────────────────────────────────────────────────────────────────────
	// ★ АБСОЛЮТНАЯ КАНДЕЛА ИЗ ПОЛЯ. Ни солнца карты, ни времени суток здесь больше нет: костёр светит
	//   одинаково днём и ночью, а разницу картинки даёт сама сцена (днём его вклад тонет в солнце,
	//   ночью читается) — это и есть «правильно», а не «одинаково заметно».
	float I = FMath::Max(Spec.IntensityCd, 0.f) * UAsdaFxLightSubsystem::GlobalIntensityScale();
	// Гашение по альфе узла: свет живёт и умирает вместе с картинкой эффекта
	if (Spec.AlphaPower > 0.f) { I *= FMath::Pow(A, Spec.AlphaPower); }
	// ★★★ [ПОРТ] ВОЛНА GM-«СКРЫТЬСЯ» (10.09.2026). `FxAlphaScale` — рантайм-множитель прозрачности
	//   ВСЕГО эффекта (см. `AsdaEffectActor.h`): под скрытием оператора ауры и свечение оружия
	//   притухают до `asda.Gm.HideFxAlpha`. Свет обязан притухнуть ВМЕСТЕ С НИМИ: он берётся из
	//   альфы УЗЛА эмиттера (`GetEmitterRuntime`), а не из вершинной альфы, которую множитель
	//   правит, — без этой строки полупрозрачная аура продолжала бы освещать сцену в полную силу
	//   и выдавала бы положение скрытого GM. Дефолт `FxAlphaScale` = 1 ⇒ на всех прочих эффектах
	//   строка не меняет ровным счётом ничего.
	//   ⚠ Категория `Effect Glow` этой строкой не затрагивается: правится только итоговая сила `I`.
	I *= Fx->FxAlphaScale;
	// Мерцание: две несоизмеримые синусоиды — ровный «живой» разброс без периода, слышимого глазом,
	// и без ассета кривой (AAsdaLocalLightActor берёт кривую, но там её ставит владелец руками)
	if (Spec.FlickerHz > 0.f && Spec.FlickerAmplitude > 0.f)
	{
		FlickerPhase = FMath::Fmod(FlickerPhase + DeltaTime * Spec.FlickerHz, 1000.f);
		const float W = 0.6f * FMath::Sin(FlickerPhase * 2.f * PI) + 0.4f * FMath::Sin(FlickerPhase * 2.f * PI * 1.7f);
		I *= FMath::Max(0.f, 1.f + Spec.FlickerAmplitude * W);
	}

	// ── ★★★ ВОЛНА Ф9: ХОД СИЛЫ НА ГРАНИЦЕ КАДРА ────────────────────────────────────────────────────────
	//   Отбор по кадру двоичен: кандидат либо в расширенном конусе камеры, либо нет. Без хода свет,
	//   влетевший в кадр при повороте, включался бы разом на полной яркости — та самая вспышка, которой
	//   быть не должно. Ход множит ИТОГОВУЮ силу (после альфы и мерцания), поэтому он одинаково виден у
	//   всех трёх форм источника и ни с одной из них не спорит.
	//   ★ ГАСНЕМ ТОЛЬКО ДОЙДЯ ДО НУЛЯ. Цель 0 ставит подсистема, когда кандидат вышел из кадра, и всё
	//     время отсрочки (asda.Fx.Light.GraceSec) он ДЕРЖИТ место — значит успевает догореть. Вернулась
	//     камера раньше — цель снова 1, и ход разворачивается с той доли, где его застали: при быстром
	//     вращении «туда-обратно» не случается ни вспышки, ни провала.
	//   ★ Ход выключается вместе с режимом (asda.Fx.Light.Cull 0) и нулём секунд: тогда доля мгновенно
	//     равна цели, то есть поведение волн Ф2…Ф8 воспроизводится ровно.
	{
		const bool bUp = (FadeTarget > LitRamp);
		const float Sec = bUp ? UAsdaFxLightSubsystem::FadeInSec() : UAsdaFxLightSubsystem::FadeOutSec();
		if (!UAsdaFxLightSubsystem::IsFrustumCull() || Sec <= KINDA_SMALL_NUMBER) { LitRamp = FadeTarget; }
		else { LitRamp = FMath::Clamp(LitRamp + (bUp ? DeltaTime / Sec : -DeltaTime / Sec), 0.f, 1.f); }
	}
	if (LitRamp <= 0.f && FadeTarget <= 0.f) { Extinguish(EAsdaFxLightGate::OffScreen); return; }
	I *= LitRamp;

	// ── ПРИМЕНЕНИЕ ──────────────────────────────────────────────────────────────────────────────────────
	// Поля формы и дальности тоже перечитываются каждый кадр: владелец крутит их в панели деталей,
	// и «применилось только после перезапуска» тут было бы неотличимо от «не работает».
	// ★ Ф6: радиус теневого света тоже прищёлкиваем — он входит в инициализатор кэшируемой тени
	// ★★★ РЕВИЗИЯ fix5 (СРЕДН-Д, 20.09.2026): РАДИУС ПРИЩЁЛКИВАЕТСЯ И У БЕСТЕНЕВОЙ РАЗЛОЖЕННОЙ ФОРМЫ.
	//   ЗАМЕР, КОТОРЫЙ ЭТО ПОКАЗАЛ. Сначала к сетке привели РАЗМЕР ФОРМЫ (он считается из ковариации
	//   облака вершин и дышит анимацией) — и Σ R² по 40 кадрам не изменилась ВОВСЕ: 12 разных значений
	//   с размахом ×2.03 при шаге 0 против 11 значений и ×2.07 при шаге 32 (прогон d1, шаги
	//   C2_snap0_a / E4_snap32). Значит дрожал не размер формы, а САМ РАДИУС ЗАТУХАНИЯ: он берётся из
	//   габаритов эффекта каждый кадр, а прищёлкивался ТОЛЬКО у теневых светов — у блина и кольца тени
	//   нет никогда, поэтому их радиус не прищёлкивался ни разу. Радиус входит в раскладку дважды
	//   (нижняя граница радиуса куска и его потолок), поэтому его дрожание и переносило места в
	//   связанном экранном бюджете владельца (63.96 из 64.00) от эффекта к эффекту каждый кадр.
	//   ★ Шаг берётся тот же, что у теневых (asda.Fx.Shadow.RadiusSnap): одна сетка на обе цели.
	const EAsdaFxLightShape ShNow = (Spec.Shape == EAsdaFxLightShape::Auto)
		? (bAutoHave ? AutoShape : EAsdaFxLightShape::Point) : Spec.Shape;
	const bool bSplitShape = (ShNow == EAsdaFxLightShape::Disc || ShNow == EAsdaFxLightShape::Ring
		|| ShNow == EAsdaFxLightShape::Tube
		|| (ShNow == EAsdaFxLightShape::Rect && FMath::Clamp(Spec.ShapeSegments, 1, 16) > 1));
	const bool bSnapRadius = (bWantShadow || bSplitShape) && GAsdaFxShadowRadiusSnap > 0.f;
	if (bSnapRadius)
	{
		if (SnappedRadius <= 0.f || FMath::Abs(Radius - SnappedRadius) > GAsdaFxShadowRadiusSnap)
		{
			SnappedRadius = FMath::Max(AsdaSnap(Radius, GAsdaFxShadowRadiusSnap), GAsdaFxShadowRadiusSnap);
		}
		Radius = SnappedRadius;
	}
	else { SnappedRadius = 0.f; }

	// ★★★ ВОЛНА Ф8: ВСЁ ПРИМЕНЕНИЕ УШЛО В ApplyShapeFrame. Раньше числа ставились прямо здесь, на
	//   `this`, потому что источник был всегда один и всегда точечный. Теперь форму выбирает поле
	//   «6.01. Форма света», и класс источника у каждой формы СВОЙ — держать три ветки посреди
	//   ApplyFrame значило бы, что «дальность видимости» и «свечение в тумане» кто-нибудь однажды
	//   поставит двум формам из трёх (и это будет незаметно ровно до жалобы владельца).
	//   `GeomEmitter` — чью секцию мерить для формы: свой эмиттер у поэмиттерного света, донор у общего.
	const int32 GeomEmitter = (EmitterIndex >= 0) ? EmitterIndex : DonorEmitter;
	// ★★★ ВОЛНА Ф10: «АВТО» ПРЕВРАЩАЕТСЯ В КОНКРЕТНУЮ ФОРМУ ЗДЕСЬ, до применения: дальше весь код видит
	//   обычную точку/трубку/кольцо/прямоугольник и второй развилки «а если авто» нигде не нужно.
	FAsdaFxLightSpec ShapeSpec = Spec;
	float ShapeI = I;
	if (Spec.Shape == EAsdaFxLightShape::Auto)
	{
		ShapeSpec.Shape = ResolveAutoShape(Spec, GeomEmitter, DeltaTime);
		// ★ АВТО-ПРЯМОУГОЛЬНИК — ДВУСТОРОННИЙ И С СИЛОЙ ×2. Прямоугольник движка излучает в ПОЛУСФЕРУ, и
		//   при тех же канделах его поток π·I против 4π·I у точки. Две стороны × 2 = поток точки: смена
		//   формы меняет ФОРМУ пятна, а не общую яркость (тот же закон, что у кольца, делящего силу на дуги).
		if (ShapeSpec.Shape == EAsdaFxLightShape::Rect) { ShapeSpec.bRectTwoSided = true; ShapeI = I * 2.f; }
	}
	// ★ Ф10-испр.: ФОРМА НЕ ДОРОЖЕ ВЫДАННОГО. Отбор мог посчитать кандидата точкой (у «авто» до первого
	//   распознавания), а форма вышла кольцом — такой кадр обязан уложиться в выданное число источников.
	//
	//   ★★★ РЕВИЗИЯ Ф11 (С4, 20.09.2026): РАНЬШЕ ЗДЕСЬ БЫЛ СБРОС В ТОЧКУ ЦЕЛИКОМ, и вместе с отбором,
	//   который не пускал кандидата, не влезающего ПОЛНОЙ ценой, это давало провал ЯРКОСТИ СЦЕНЫ.
	//   Замер ревизии: тот же эффект, охват «свой свет у каждого эмиттера», бюджет количества 64 —
	//   точкой горит 45 источников на 101 кд, трубкой ×6 горит 60 источников, но всего на 48.6 кд:
	//   бюджет пустил вшестеро меньше ЭМИТТЕРОВ, и половина эффекта осталась неосвещённой. Владелец
	//   при этом не менял ни силу, ни радиус — он менял ФОРМУ, а потемнела вся сцена.
	//   ЛЕЧЕНИЕ (пара с правкой отбора в UAsdaFxLightSubsystem::Select): отбор выдаёт кандидату
	//   СТОЛЬКО источников, сколько осталось в бюджете (не меньше одного), а кадр УКОРАЧИВАЕТ РАСКЛАДКУ
	//   под выданное вместо того, чтобы выбрасывать форму. Форма при нехватке бюджета становится грубее
	//   (полос меньше), но эффект продолжает светить своей полной силой — суммарный поток раскладки
	//   равен потоку точки при любом числе кусков.
	//
	//   ★★★ РЕВИЗИЯ fix3 (КРИТ-1 и КРИТ-2, 20.09.2026): ПРАВИЛО ВЫРОЖДЕНИЯ ПЕРЕЕХАЛО В ОБЩУЮ
	//   ФУНКЦИЮ ResolveGiveShape — её ЖЕ спрашивает экранный бюджет. Пока правило жило здесь одно,
	//   бюджет списывал цену ЗАЯВЛЕННОЙ формы: у вырожденного в точку блина на экране стоял один
	//   источник ПОЛНОГО радиуса, а списывалась доля 0.7² от него. И второе: блин больше не
	//   вырождается в точку — при одном выданном месте он остаётся блином из ОДНОЙ полосы-хорды,
	//   а она по построению проходит через центр диска и светит во весь диаметр (см. ApplyDiscFrame).
	// ★★★ ВОЛНА fix9 (КРИТ-1 ревизии fix8, 20.09.2026): ОПОРНАЯ СИСТЕМА ФОРМЫ СЧИТАЕТСЯ ЗДЕСЬ, ДО
	//   РЕШЕНИЯ О ЧИСЛЕ КУСКОВ. Раньше её считал ApplyShapeFrame — то есть уже ПОСЛЕ того, как число
	//   полос названо, — и решению неоткуда было взять размер формы этого кадра. Теперь базис один на
	//   кадр: по нему меряется размер формы для правила, и он же уходит в применение.
	const FAsdaFxShapeBasis FrameBasis = ResolveShapeBasis(Spec, Agg, GeomEmitter, Loc);
	{
		const int32 Give = FMath::Max(AdmittedCost, 1);
		// ★ fix5 (КРИТ-А): правилу вырождения нужна ГЕОМЕТРИЯ — сколько плиток требует полоса.
		// ★★★ fix9 (КРИТ-1): ГЕОМЕТРИЯ — ЭТОГО КАДРА, А НЕ ПРОШЛОГО. Прежде здесь стоял
		//   ShapePredictGeom (размер и радиус ПРОШЛОГО кадра), а куски строились по ТЕКУЩЕМУ радиусу;
		//   у живого эффекта радиус дышит (у стенда 588 ↔ 840+ юн), и правило отвечало про чужой кадр.
		//   Замер, показавший это: f8_polnoB_9x_f8.json при бюджете количества 512 — 48 источников,
		//   то есть 8 полос при базе 587.6, тогда как правило при том же кадре просит 10.
		//   Защёлку порога пишет ИМЕННО ЭТОТ вызов (bCommit = true): у него числа свежие.
		float PSize = 0.f, PBase = 0.f, PCap = 0.f;
		ShapeFrameGeom(Spec, ShapeSpec.Shape, FrameBasis, GeomEmitter, Radius, PSize, PBase, PCap);
		// ★ fix10 (СРЕДН-1 ревизии fix9): решение «блин мельче своего света» спрашивается ОДИН раз
		//   за кадр — оно же идёт в правило, оно же в LastRuleParents, оно же в цену полной
		//   раскладки. Прежде вопрос задавался ДВАЖДЫ (второй раз ради LastRuleParents, с
		//   bCommit = false): защёлку второй вызов не писал, но два ответа на один вопрос в одном
		//   кадре — это ровно та развилка, из которой вырос КРИТ-1 волны fix9.
		const bool bDiscPointNow = DiscAsPoint(Spec, ShapeSpec.Shape, PSize, PBase, /*bCommit=*/true);
		const FAsdaFxGiveShape G = ResolveGiveShape(Spec, ShapeSpec.Shape, Give, PSize, PBase, PCap, bDiscPointNow);
		// ★★★ fix9 (КРИТ-1): ЗАПОМИНАЕМ ЧИСЛА РЕШЕНИЯ — их печатает дамп, и по ним гарантия «при
		//   полной выдаче построено то, что просит правило» проверяется ПРЯМО, а не восстановлением
		//   числа полос из радиусов источников (так её пришлось проверять ревизии fix8).
		LastRuleSizeR = PSize;
		LastRuleBaseR = PBase;
		// ★★★ РЕВИЗИЯ «форма-2» (КРИТ-2): ЗНАМЕНАТЕЛЬ КОЛОНКИ «НАКРЫТ» — ПОЛУ-ГАБАРИТ ФОРМЫ.
		//   `MeasureShapeSizeR` возвращает РАЗНЫЕ величины по формам: у блина и кольца это радиус,
		//   у трубки — полу-длина, а у ПРЯМОУГОЛЬНИКА — ПОЛНАЯ ширина (см. её тело). Приводим всё к
		//   «полу-габариту от центра формы», иначе плоскость получала бы вдвое заниженное накрытие.
		//   У точки, шара и конуса собственной формы нет — там 0, и дамп печатает «—».
		LastShapeHalf = (ShapeSpec.Shape == EAsdaFxLightShape::Rect) ? PSize * 0.5f
			: ((ShapeSpec.Shape == EAsdaFxLightShape::Disc || ShapeSpec.Shape == EAsdaFxLightShape::Ring
				|| ShapeSpec.Shape == EAsdaFxLightShape::Tube) ? PSize : 0.f);
		LastRuleParents = (ShapeSpec.Shape == EAsdaFxLightShape::Disc)
			? (bDiscPointNow ? 1 : AsdaFxDiscStripsFor(Spec, PSize, PBase))
			: (ShapeSpec.Shape == EAsdaFxLightShape::Ring) ? FMath::Clamp(Spec.RingSegments, 3, 8)
			: (ShapeSpec.Shape == EAsdaFxLightShape::Tube || ShapeSpec.Shape == EAsdaFxLightShape::Rect)
				? FMath::Clamp(Spec.ShapeSegments, 1, 16) : 1;
		// ═══ ★★★ ВОЛНА fix10 (СРЕДН-1 ревизии fix9, 20.09.2026): ЦЕНА ПОЛНОЙ РАСКЛАДКИ ЭТОГО КАДРА ═══
		//
		//   ЧТО БЫЛО НЕ ТАК. Метка «← РАСХОЖДЕНИЕ» в дампе ставилась по ОДНОМУ сравнению «правило
		//   просит N — кадр построил M» и кричала о дефекте там, где его нет. При нехватке МЕСТ
		//   ResolveGiveShape укорачивает раскладку ЗАКОННО (ветка деградации под бюджетом, fix8), и
		//   M < N там — это работа бюджета, а не поломка. Живой прогон ревизии fix9 при бюджете
		//   количества 30 (scratchpad/rev_fxfix9/rv9_bud_9x.log): «правило просит 10, построено 6,
		//   источников 28, мест выдано 30 ← РАСХОЖДЕНИЕ» — кадр отработал правильно, а дамп назвал
		//   его дефектом. Ложная тревога в дампе стоит дороже отсутствующей: по ней ищут поломку.
		//
		//   ЧЕГО НЕ ХВАТАЛО. Третьего числа — СКОЛЬКО МЕСТ СТОИТ ПОЛНАЯ РАСКЛАДКА при геометрии
		//   ЭТОГО кадра. Только с ним видно, был ли у кадра выбор: если выданных мест хватало на
		//   полную раскладку, а построено меньше — это дефект; если не хватало — это срез.
		//   Считается теми же функциями и теми же тремя числами, которыми считает себя сама
		//   раскладка (SplitSourceCount при том же потолке плиток и той же доле радиуса), иначе
		//   порог сравнения разошёлся бы с кадром — ровно так, как расходились радиусы до fix9.
		//   ★ У вырожденного в точку блина полная раскладка — один источник (ResolveGiveShape,
		//     ветка bDiscAsPoint), и SplitSourceCount тут спрашивать нельзя: он посчитал бы плитки
		//     полосы, которой в этом кадре нет.
		//   ★ У ПРЯМОУГОЛЬНИКА цена вдвое больше числа кусков, когда он двусторонний: у каждой
		//     плитки два источника (так же считает и GetLightCost — иначе порог сравнения разошёлся
		//     бы с бюджетом ровно вдвое и двусторонняя плоскость под срезом печаталась бы дефектом).
		const int32 RectSides = (ShapeSpec.Shape == EAsdaFxLightShape::Rect && ShapeSpec.bRectTwoSided) ? 2 : 1;
		// ★ fix11 (пункт 2): вместе с ценой запоминаем, ЧЬЯ она — заявленную форму этого кадра.
		//   Счёт мест берёт цену нижней границей запроса только при совпадении формы (GetLightCost).
		LastRuleShape = ShapeSpec.Shape;
		LastRuleFullCost = bDiscPointNow
			? 1
			: RectSides * SplitSourceCount(ShapeSpec.Shape, FMath::Max(LastRuleParents, 1), PSize, PBase,
				(ShapeSpec.Shape == EAsdaFxLightShape::Disc) ? AsdaFxDiscShareFor(Spec, PSize, PBase)
					: ShapeRadiusShare(Spec, ShapeSpec.Shape),
				PCap, AsdaFxTileMaxFor(Spec, ShapeSpec.Shape, PSize, PBase));
		// ★ fix5: потолок плиток, который бюджет разрешил этому кадру (полное покрытие или срезанное)
		ShapeSpec.ShapeCoverTiles = FMath::Clamp(G.Tiles, 1, 8);
		switch (ShapeSpec.Shape)
		{
		case EAsdaFxLightShape::Ring:
			if (G.Shape == EAsdaFxLightShape::Ring) { ShapeSpec.RingSegments = G.Segments; }
			else { ShapeSpec.Shape = G.Shape; }       // мест меньше трёх — обода не собрать
			break;
		case EAsdaFxLightShape::Disc:
			ShapeSpec.DiscStrips = G.Segments;        // ★ КРИТ-2: минимум 1 полоса, а не точка
			// ★★★ fix7: ДОЛЯ РАДИУСА ПОЛОСЫ ТОЖЕ ПРИХОДИТ ИЗ ОБЩЕГО ПРАВИЛА. У блина мельче своего
			//   света оно отдаёт ОДНУ полосу при доле 1.0 — один источник полного радиуса. Без этой
			//   строки кадр построил бы ту же полосу при доле из поля (0.7), а экранный бюджет
			//   предсказал бы полный радиус: два бюджета разошлись бы вдвое (ровно КРИТ-1 fix3).
			//   В обычном случае G.RadiusShare равен полю, и ни одно прежнее число не меняется.
			ShapeSpec.DiscStripRadiusScale = FMath::Clamp(G.RadiusShare, 0.1f, 2.f);
			break;
		case EAsdaFxLightShape::Tube:
			ShapeSpec.ShapeSegments = G.Segments;
			break;
		case EAsdaFxLightShape::Rect:
			ShapeSpec.ShapeSegments = G.Segments;
			// Обратная сторона погашена — сила ВОЗВРАЩАЕТСЯ к исходной: её удвоение было платой
			// ровно за вторую полусферу (см. выше)
			if (ShapeSpec.bRectTwoSided && !G.bTwoSided) { ShapeSpec.bRectTwoSided = false; ShapeI = I; }
			break;
		default: break;   // точка, шар, конус — один источник, укорачивать нечего
		}
		// С каким числом мест посчитан ЭТОТ кадр: экранный бюджет берёт измеренный след только
		// тогда, когда спрашивает про то же самое число (иначе картинка была бы другой)
		LastGiveApplied = Give;
	}
	// ★ Ф10-испр.: трубка согласует свой радиус с длиной — ДО применения, чтобы LastRadius (экранный бюджет) был честным
	// ★ Ревизия Ф11 (С4): числа берутся из ShapeSpec, а не из Spec — раскладку мог укоротить бюджет,
	//   и PrepareTube обязан считать полу-длину куска по ФАКТИЧЕСКОМУ числу кусков
	if (ShapeSpec.Shape == EAsdaFxLightShape::Tube) { PrepareTube(ShapeSpec, FrameBasis, GeomEmitter, LvlCap, bWantShadow, Radius); }
	ApplyShapeFrame(ShapeSpec, Agg, GeomEmitter, Loc, FrameBasis, Radius, Col, ShapeI, bWantShadow);
	LastShape = ShapeSpec.Shape;

	LastIntensityCd = I;
	LastRadius = Radius;
	// ★ РЕВИЗИЯ (СРЕДН-6): КОЛОНКА «ЭМИТ» В ДОНОРСКОМ РЕЖИМЕ БЫЛА КОНСТАНТОЙ 1 — она печатала не
	//   «сколько эмиттеров дали свет», а «мы в донорском режиме». Печатаем живых ВСЕГДА: в донорском
	//   режиме их тоже посчитал тот же проход, и число «живых 12, а свет берёт одного» — полезное.
	LastLive = Agg.Live;
	LastLeashed = bAggregate && Agg.bLeashed;
	LastSpread = Agg.Spread;
	bLitNow = true;
	LastGate = EAsdaFxLightGate::Lit;
}

ULocalLightComponent* UAsdaFxLightComponent::ActiveLight(EAsdaFxLightShape Shape)
{
	if (Shape == EAsdaFxLightShape::Point) { return this; }
	AActor* Owner = GetOwner();
	if (!Owner) { return this; }

	// ★ Хранитель флага «пакет изменён»: заведение компонента не должно помечать карту грязной —
	//   тот же закон, что и в UAsdaFxLightSubsystem::RegisterEffect. Иначе достаточно было бы
	//   пролететь мимо эффекта с прямоугольной формой, чтобы редактор попросил сохранить уровень.
#if WITH_EDITOR
	UPackage* Pkg = GIsEditor ? Owner->GetPackage() : nullptr;
	const bool bWasDirty = Pkg && Pkg->IsDirty();
#endif
	ULocalLightComponent* Result = this;

	if (Shape == EAsdaFxLightShape::Rect)
	{
		if (!RectChild)
		{
			RectChild = NewObject<URectLightComponent>(Owner, NAME_None, RF_Transient);
			if (RectChild)
			{
				AsdaInitFxChildLight(RectChild);
				RectChild->SetupAttachment(this);
				RectChild->RegisterComponent();
			}
		}
		Result = RectChild ? (ULocalLightComponent*)RectChild.Get() : (ULocalLightComponent*)this;
	}
	else if (Shape == EAsdaFxLightShape::Spot)
	{
		if (!SpotChild)
		{
			SpotChild = NewObject<USpotLightComponent>(Owner, NAME_None, RF_Transient);
			if (SpotChild)
			{
				AsdaInitFxChildLight(SpotChild);
				SpotChild->SetupAttachment(this);
				SpotChild->RegisterComponent();
			}
		}
		Result = SpotChild ? (ULocalLightComponent*)SpotChild.Get() : (ULocalLightComponent*)this;
	}

#if WITH_EDITOR
	if (Pkg && !bWasDirty && Pkg->IsDirty()) { Pkg->SetDirtyFlag(false); }
#endif
	return Result;
}

void UAsdaFxLightComponent::ExtinguishShapes()
{
	AsdaDouseLight(RectChild.Get());
	AsdaDouseLight(RectChildBack.Get());
	AsdaDouseLight(SpotChild.Get());
	// ★ Ф10: дуги кольца гаснут вместе с эффектом; ★ Ф11: вместе с ними — куски трубки и полосы блина
	//   (общий пул) и плитки разложенного прямоугольника
	for (UPointLightComponent* P : PointChildren) { AsdaDouseLight(P); }
	for (URectLightComponent* R : RectChildren) { AsdaDouseLight(R); }
	// ★ Ревизия Ф11 (М4): погашенный свет не занимает НИ ОДНОГО дочернего источника — значит все они
	//   лишние и через отсрочку будут убраны (★ fix3, С-5: уборку зовёт подсистема по всем кандидатам,
	//   в том числе по вытесненным из бюджета, у которых тик выключен)
	LastPointUsed = 0;
	LastRectUsed = 0;
	// ★ Ревизия fix3 (КРИТ-1): погасший свет не занимает и НИ ОДНОГО экранного тайла
	LastLitSumR2 = 0.0;
	LastLitSources = 0;
}
