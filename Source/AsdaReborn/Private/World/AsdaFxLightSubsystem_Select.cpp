// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightSubsystem_Select.cpp — ★ R3-c 12.11.2026 (разрез AsdaFxLightSubsystem.cpp, дословный перенос): кадр: точка наблюдателя, Tick, отбор огней Select.
//   Общее для частей — в AsdaFxLightSubsystem_Private.h; шапка и реверс — в AsdaFxLightSubsystem.cpp.
#include "World/AsdaFxLightSubsystem.h"
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "World/AsdaFxLightComponent.h"
#include "World/AsdaFxLightSettings.h"
#include "World/AsdaVisibilitySubsystem.h"   // ★ Волна Ф9: ОБЩАЯ точка «объект попадает в кадр» (расширенный конус, гистерезис, предсказание по угловой скорости)
#include "UI/AsdaDevMenuSubsystem.h"
#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif
#include "AsdaFxLightSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaFxLightSubsystem*.cpp

// ─────────────── отбор ───────────────
void UAsdaFxLightSubsystem::UpdateViewLocation()
{
	// ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя — AsdaView (правило — AsdaView.h). Эффект
	//   (AAsdaEffectActor::GetViewLocation) зовёт ТУ ЖЕ функцию — дистанцию геометрии и света теперь действительно считают
	//   от одной точки. ★ ИСПРАВЛЕНИЕ E7: прежний комментарий здесь утверждал «ровно тот же выбор, что делает сам эффект»,
	//   но эффект мир вьюпорта не проверял. Для самого света правило прежнее (камера → павн; в редакторе перспективный
	//   вьюпорт ЭТОГО мира) плюс последний отрисованный вид этого мира, когда их нет (первые кадры PIE без контроллера):
	//   раньше в этом окне свет гас как «нет наблюдателя».
	FVector V;
	bHaveView = AsdaView::GetViewLocation(GetWorld(), V);
	if (bHaveView) { ViewLoc = V; }
}

void UAsdaFxLightSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ★ У РЕДАКТОРСКОГО МИРА OnWorldBeginPlay НЕ ЗОВЁТСЯ ВОВСЕ — перепись делается на первом тике.
	//   В игре сюда не попадаем: там ScanWorld уже отработал в OnWorldBeginPlay.
	if (!bScanned) { ScanWorld(); }
	// Отложенная пересборка (гаситель дребезга правок свойств в редакторе — см. поле bRescanRequested)
	if (bRescanRequested) { bRescanRequested = false; Rescan(); }

	UpdateViewLocation();

	// ★★★ fix13 (КРИТ-1): набор светов — ПЕРЕД отбором. Иначе отбор этого кадра раздал бы места
	//   набору, который уже не соответствует спецификации, и кадр съёмки поймал бы ровно то
	//   неустойчивое состояние, на котором сняты все кадры волны fix12.
	//   ★ fix14 (МЕЛК-4): это КАЖДЫЙ КАДР, а не «каждый отбор» — отбор строкой ниже ходит под
	//   таймером SelectIntervalSec. Сверка нарочно чаще: она решает, ЧТО отбирать.
	//   ★ fix14 (С-3): флаг принуждения взводит смена уровня качества — гасим его ЗДЕСЬ, сразу
	//   после того, как сверка им воспользовалась, иначе принуждение осталось бы навсегда.
	// ★ 29.10.2026 (волна производительности, Reports/perf_opt_2026-10-29/REPORT.md): в ИГРОВОМ мире сверка —
	//   с частотой asda.Fx.Light.ReconcileHz (попарно на alpen: 0.22 мс игрового потока за кадр). Принуждение (смена
	//   уровня качества) и редакторский мир (правки свойств руками) — как прежде, сразу и каждый кадр.
	ReconcileAccumSec += GetWorld() ? GetWorld()->GetDeltaSeconds() : DeltaTime;
	const float ReconcilePeriod = (GAsdaFxLightReconcileHz > 0.f) ? 1.f / GAsdaFxLightReconcileHz : 0.f;
	const bool bGameWorld = GetWorld() && GetWorld()->IsGameWorld();
	if (bReconcileForced || !bGameWorld || ReconcilePeriod <= 0.f || ReconcileAccumSec >= ReconcilePeriod)
	{
		ReconcileDt = ReconcileAccumSec;
		ReconcileAccumSec = 0.f;
		ReconcileSets(bReconcileForced);
		bReconcileForced = false;
	}

	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	SelectTimer += DeltaTime;
	if (SelectTimer >= S->SelectIntervalSec) { Select(DeltaTime); SelectTimer = 0.f; }

	// ★★★ ВОЛНА Ф9: покадровый след (asda.Fx.Light.Watch N). Печатается ПОСЛЕ отбора и после того,
	//   как компоненты посчитали свой кадр, — иначе «ход силы» отставал бы на кадр и строка врала бы
	//   ровно в тот момент, ради которого её и читают (вход света в кадр).
	if (GAsdaFxLightWatch > 0)
	{
		--GAsdaFxLightWatch;
		float RMin = 2.f, RMax = -1.f;
		int32 Lit = 0;
		for (UAsdaFxLightComponent* C : Candidates)
		{
			if (!IsValid(C) || !C->IsLit()) { continue; }
			++Lit;
			RMin = FMath::Min(RMin, C->GetLitRamp());
			RMax = FMath::Max(RMax, C->GetLitRamp());
		}
		UE_LOG(LogAsdaFxLight, Log, TEXT("[след] кандидатов %d | прошли %d | вне кадра %d | догорают %d | мест %d | ГОРИТ %d | экран %.2f | ход %.2f..%.2f | dt %.1f мс"),
			LastCandidates, LastEligible, LastOffScreen, LastFading, LastAdmitted, Lit,
			LastScreenUsed, RMax < 0.f ? 0.f : RMin, RMax < 0.f ? 0.f : RMax, DeltaTime * 1000.f);
	}
}

void UAsdaFxLightSubsystem::Select(float DeltaTime)
{
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	const UWorld* W = GetWorld();
	const bool bEditorWorld = W && !W->IsGameWorld();

	// Мёртвые кандидаты (эффект уничтожен вместе с компонентом) — вычистить
	// ★ fix17 (МЕЛК-2): ВТОРАЯ ДВЕРЬ ИЗ Candidates МИМО RegisterEffect. Условие снимает кандидата
	//   и когда МЁРТВ ТОЛЬКО КОМПОНЕНТ, а сам эффект жив (`!IsValid(C)` при живом `C->GetEffect()`).
	//   Список погасших наполняет ОДИН RegisterEffect, значит уведённый отсюда последний свет
	//   живого эффекта в Extinguished не попадёт — и это та же дыра, которую закрывала fix16, но
	//   с другого входа. Сегодня она недостижима: компонент света уничтожается только из
	//   RegisterEffect (ветка «Лишние — снести», там же чистится Candidates) и вместе с актором
	//   (OnComponentDestroyed убирает дочерние). Чужого писателя, который снёс бы компонент в
	//   обход, в срезе нет. Правку не делаю: закрывать недостижимое — менять поведение без
	//   доказательства. ★ Раздел 8 отчёта fix16 эту точку НЕ СМОТРЕЛ; прогоном не проверялась.
	Candidates.RemoveAll([](const TObjectPtr<UAsdaFxLightComponent>& C) { return !IsValid(C) || !IsValid(C->GetEffect()); });
	// Сроки аренды умерших светов: без этой чистки карта росла бы весь сеанс на каждом одноразовом
	// эффекте навыка (эффект живёт секунды, а ключ-слабая-ссылка оставался бы навсегда).
	for (auto It = LeaseAge.CreateIterator(); It; ++It) { if (!It->Key.IsValid()) { It.RemoveCurrent(); } }
	LastCandidates = Candidates.Num();
	LastBudget = ResolveBudget();
	// Уровневые числа читаются ОДИН раз за отбор: каждое лезет в Scalability-ini через GConfig,
	// а кандидатов на людной карте сотни
	// ★ Ф10: уровневый потолок радиуса — в юнитах ЭТОГО мира (логин ×100): иначе 1000…3000 юн резали бы
	//   свет кристалла размером 28 000 юн до пятнышка внутри него самого
	const float RadiusCap = ResolveRadiusCap() * WorldLengthScale(W);
	const float ScreenBudget = ResolveScreenBudget();

	// Общий откат / показ в редакторе выключен / нет наблюдателя: гасим всё разом
	if (!IsFeatureEnabled() || !bHaveView || (bEditorWorld && !IsEditorPreviewEnabled()))
	{
		for (UAsdaFxLightComponent* C : Candidates)
		{
			C->SetAdmitted(false);
			C->SetGate(IsFeatureEnabled() ? (bHaveView ? EAsdaFxLightGate::FeatureOff : EAsdaFxLightGate::NoView) : EAsdaFxLightGate::FeatureOff);
			// ★ Ревизия fix3 (С-5): уборка пулов идёт и на этой ветке — «свет выключили совсем» тем
			//   более не повод держать у акторов десятки мёртвых зарегистрированных компонентов
			C->TrimShapePools(DeltaTime);
		}
		LeaseAge.Reset();
		OffAge.Reset();
		DetailHold.Reset();
		LastEligible = 0;
		LastLit = 0;
		LastAdmitted = 0;
		LastScreenUsed = 0.f;
		LastCutByScreen = 0;
		LastOffScreen = 0;
		LastFading = 0;
		return;
	}

	// ★★★ ВОЛНА Ф9: ГЕЙТ ОТБОРА. Два режима, ключ asda.Fx.Light.Cull.
	//   1 (по умолчанию) — ПО КАДРУ: кандидат проходит, если его СФЕРА (позиция + радиус этого кадра)
	//     попадает в расширенный конус камеры. Считает общая точка значимости
	//     UAsdaVisibilitySubsystem::Evaluate — та же, которой гейтятся сами эффекты, плашки и облака.
	//     ПОЧЕМУ ЕЮ, А НЕ СВОИМ КОНУСОМ: у неё уже есть запас по углу, гистерезис включения/выключения,
	//     ПРЕДСКАЗАНИЕ по угловой скорости камеры и пересчёт всех объектов на рывке — свой второй конус
	//     означал бы вторую формулу того же (закон port-shared-points), и первым же расхождением стало бы
	//     «свет есть, а эффекта нет» (эффект заморожен уровнем 0, свет считает себя видимым).
	//     Угловой размер источника берётся ИМЕННО от радиуса света, а не от габарита эффекта: свет за
	//     краем кадра обязан гореть, пока его пятно достаёт до видимой земли.
	//   0 — ПО ДИСТАНЦИИ: та же формула, что была у света в тике, min(числа, CullDistance эффекта)
	//     × r.LightMaxDrawDistanceScale (иначе далёкий свет занимал бы место, будучи погашенным движком).
	const bool bFrustum = IsFrustumCull();
	UAsdaVisibilitySubsystem* Vis = bFrustum ? UAsdaVisibilitySubsystem::Get(this) : nullptr;
	const float Grace = GraceSec();

	// ★ Ф10: Cost — сколько источников движка стоит кандидат (кольцо = число дуг): бюджет количества
	//   считает ИСТОЧНИКИ, а не кандидатов, иначе кольцо из четырёх дуг проходило бы за один свет
	// ★★★ РЕВИЗИЯ fix3 (КРИТ-1, КРИТ-2): вместо ОДНОГО числа «площадь при полной подробности» у
	//   кандидата хранится СПЕКА и минимальная цена формы: площадь при выданных N мест спрашивается
	//   у самого компонента (EstimateScreenArea(..., Give)), который считает её по тому же правилу
	//   вырождения, каким потом зажжёт свет. Прежняя запись «Area / Cost» молча предполагала, что
	//   все источники раскладки одинаковы и что при нехватке мест форма дешевеет пропорционально, —
	//   и то и другое неверно (вырожденный блин горит ТОЧКОЙ ПОЛНОГО радиуса).
	struct FCand { UAsdaFxLightComponent* C; FAsdaFxLightSpec Spec; float Score; bool bWasLit; float Age; bool bFading; int32 Cost; int32 MinCost; };
	TArray<FCand> Cands;
	Cands.Reserve(Candidates.Num());
	int32 OffScreenCut = 0, Fading = 0, Dormant = 0;
	for (UAsdaFxLightComponent* C : Candidates)
	{
		const AAsdaEffectActor* Fx = C->GetEffect();
		const FAsdaFxLightSpec Spec = C->ResolveSpec();
		// Свет этому эффекту выключен полем — кандидат молчит и место не занимает
		if (!Spec.bEnabled) { C->SetAdmitted(false); C->SetGate(EAsdaFxLightGate::Disabled); LeaseAge.Remove(C); OffAge.Remove(C); continue; }
		const float Score = C->ComputeScore(ViewLoc, Spec);
		if (Score <= 0.f) { C->SetAdmitted(false); C->SetGate(EAsdaFxLightGate::Disabled); continue; }

		const bool bWas = C->IsAdmitted();
		bool bFadingOut = false;
		if (bFrustum)
		{
			// ★★★ ЕДИНСТВЕННЫЙ ОСТАВШИЙСЯ ПОТОЛОК ДАЛЬНОСТИ — И ОН НЕ СВЕТА, А ЭФФЕКТА.
			//   Дальше своего CullDistance (по умолчанию 10 000 юн) эффект ЧИСТИТ ГЕОМЕТРИЮ
			//   (AsdaEffectActor.cpp, дистанц-кулл «как движок»): его самого уже не видно, а свет берёт
			//   из этой геометрии и позицию, и радиус, и цвет, и альфу — светить становится нечем и не от чего.
			//   Такой кандидат и раньше не загорался (гейт «у донора нет построенной геометрии»), но
			//   ЗАНИМАЛ МЕСТО в бюджете количества: замер 10.09 на nightvalley при вращении камеры —
			//   17 мест выдано, ГОРИТ 0. Отсекаем здесь, чтобы числа бюджета означали то, что написано.
			//   ★ Это НЕ возврат дистанционного гейта света: убирается ровно то, что физически не светит.
			//     Хотите свет дальше 10 000 юн — поднимать надо CullDistance ЭФФЕКТА (вместе с его картинкой).
			if (Fx && FVector::Dist(ViewLoc, Fx->GetActorLocation()) > FMath::Max(Fx->CullDistance, 1.f))
			{
				C->SetAdmitted(false); C->SetGate(EAsdaFxLightGate::TooFar);
				LeaseAge.Remove(C); OffAge.Remove(C); continue;
			}
			// Сфера кандидата: у уже светящего — фактический радиус прошлого кадра, у нового — потолок
			// его чисел под уровневым потолком (та же верхняя оценка, что у экранного бюджета).
			// ★★★ fix9 (МЕЛК-3 ревизии fix8): ФОРМУЛА НЕ ВЫПИСЫВАЕТСЯ ЗДЕСЬ ВТОРОЙ РАЗ. Прежняя
			//   ручная копия отличалась от общей ShapeBaseRadius двумя мелочами — не было зажима
			//   снизу единицей и стояло IsLit() вместо bLitNow (сейчас это одно и то же, но связь
			//   держалась на совпадении, а не на общем коде). Свели: одна функция на все три
			//   потребителя — счёт мест, экранный бюджет и отбор по видимости.
			const float SphereR = C->ShapeBaseRadius(Spec, RadiusCap);
			// ★ Подсистемы значимости нет (редакторский мир, загрузка, ключ asda.Vis.Enable 0) — считаем
			//   кандидата видимым: «не знаю» обязано означать «свети», а не «гасни». Обратное дало бы
			//   ровно то ложное «света нет», от которого предостерегает закон false-negatives-method.
			const int32 Level = Vis ? Vis->Evaluate(C->GetVisState(), Fx->GetActorLocation(), SphereR) : (int32)EAsdaVisLevel::Near;
			if (Level == (int32)EAsdaVisLevel::Off)
			{
				// ОТСРОЧКА. Ушедший из кадра светящий ещё держит место и гаснет ходом (см. asda.Fx.Light.GraceSec):
				// при рывке камеры «туда-обратно» его не снимают и не заводят заново — ход просто разворачивается.
				float& Off = OffAge.FindOrAdd(C);
				Off += DeltaTime;
				if (!bWas || Off > Grace || C->GetLitRamp() <= 0.f)
				{
					C->SetAdmitted(false); C->SetGate(EAsdaFxLightGate::OffScreen);
					LeaseAge.Remove(C); OffAge.Remove(C); ++OffScreenCut; continue;
				}
				bFadingOut = true;
				++Fading;
			}
			else { OffAge.Remove(C); }
		}
		else
		{
			const float Lim = C->ResolveDrawDistance(Spec) * LightDrawDistanceScale();
			if (FVector::Dist(ViewLoc, Fx->GetActorLocation()) > Lim) { C->SetAdmitted(false); C->SetGate(EAsdaFxLightGate::TooFar); LeaseAge.Remove(C); continue; }
		}

		// ★★★ Ф10-испр. (ревизия 11.09.2026): НЕВИДИМЫЙ ЭФФЕКТ МЕСТ НЕ ЗАНИМАЕТ. Проверка стоит ПОСЛЕ отсечки
		//   по кадру: зонд гоняет геометрию, и кандидатам за кадром он не нужен. Альфа — та же, что гасит свет
		//   в ApplyFrame; спящий снимается без места, проснувшись — сразу идёт в отбор. Раньше кольцо навыка
		//   с альфой 0 держало до 8 мест и на Low гасило фонари карты.
		const float ProbeA = C->ProbeAlpha(DeltaTime);
		if (C->UpdateDormancy(ProbeA, DeltaTime))
		{
			// ★ «форма-2», п. 6: причину называет ЗОНД (он один её и знает), а не зашитое слово
			C->SetAdmitted(false); C->SetAdmittedCost(0); C->SetGate(C->GetDormantGate());
			LeaseAge.Remove(C); OffAge.Remove(C); ++Dormant;
			continue;
		}
		// ★ Счёт домножается на альфу (не ниже 0.1): догорающий эффект уступает место фонарю, а не держит его до конца
		const float ScoreA = Score * FMath::Max(ProbeA, 0.1f);
		float* AgePtr = LeaseAge.Find(C);
		const float Age = AgePtr ? *AgePtr : 0.f;
		Cands.Add({ C, Spec, ScoreA, bWas, Age, bFadingOut, C->GetLightCost(Spec), C->GetMinLightCost(Spec) });
	}
	LastEligible = Cands.Num();
	LastOffScreen = OffScreenCut;
	LastFading = Fading;
	LastDormant = Dormant;
	// Сроки отсрочки умерших кандидатов — та же чистка, что у LeaseAge (иначе карта росла бы весь сеанс)
	for (auto It = OffAge.CreateIterator(); It; ++It) { if (!It->Key.IsValid()) { It.RemoveCurrent(); } }

	// ★ ГИСТЕРЕЗИС АРЕНДЫ. Уже светящий кандидат идёт в сортировку с УСИЛЕННЫМ счётом, пока не прожил
	//   LeaseMinSec: без этого два костра по разные стороны границы бюджета менялись бы местами каждый кадр
	//   (свет добавляется/удаляется из сцены — видимое мигание и лишняя работа рендера).
	//   После LeaseMinSec фора остаётся, но всего в LeaseHysteresis раз — «ближе и ярче» всё-таки побеждает.
	const float Hyst = FMath::Max(S->LeaseHysteresis, 1.f);
	for (FCand& X : Cands)
	{
		if (X.bWasLit) { X.Score *= (X.Age < S->LeaseMinSec) ? (Hyst * 1000.f) : Hyst; }
	}
	// ★ РЕВИЗИЯ (СРЕДН-6): СЧЁТ, ПО КОТОРОМУ РЕАЛЬНО СОРТИРОВАЛИ, ЗАПОМИНАЕТСЯ НА КАНДИДАТЕ.
	//   Дамп печатал его ПЕРЕСЧЁТОМ через ComputeScore — без множителя альфы и без гистерезиса
	//   аренды, то есть не то число, по которому шёл отбор (закон diagnostic-must-use-real-path).
	for (FCand& X : Cands) { if (IsValid(X.C)) { X.C->SetLastScore(X.Score); } }
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Score > B.Score; });

	// ★ РАЗДАЧА МЕСТ ПО ДВУМ БЮДЖЕТАМ. Экранный бюджет — главный: он ограничивает то, что реально стоит
	//   (площадь следа в тайлах light grid). Счёт количества — грубый предохранитель поверх него.
	//   Кандидат, не влезший в ОСТАТОК экранного бюджета, пропускается, а перебор продолжается: следующий
	//   за ним может быть мелким и влезть. Так десять искр под ногами не вытесняются одним далёким заревом.
	float ScreenUsed = 0.f;
	int32 Admitted = 0, CutByScreen = 0;
	// ★★★ РЕВИЗИЯ Ф11 (С4, 20.09.2026): РАЗДАЧА В ДВА ПРОХОДА — «СНАЧАЛА ВСЕМ ПО ОДНОМУ».
	//
	//   ЧТО БЫЛО НЕ ТАК. Кандидат, чья ПОЛНАЯ стоимость не влезала в остаток бюджета количества,
	//   отбрасывался целиком. У эффекта с охватом «свой свет у каждого эмиттера» это означало, что
	//   смена формы на разложенную ГАСИТ БОЛЬШУЮ ЧАСТЬ ЭМИТТЕРОВ: замер ревизии (бюджет 64, 45
	//   эмиттеров) — точкой горит 45 источников на 99.7 кд, трубкой ×6 горит 64 источника, но всего
	//   у 11 эмиттеров и на 51.1 кд. Владелец менял ФОРМУ, а проваливалась ЯРКОСТЬ ВСЕЙ СЦЕНЫ —
	//   то есть бюджет наказывал за настройку, которой сам же обещал «суммарный поток не меняется».
	//   Одной лишь «выдачей остатка» это не лечится: остатка хватает ровно одному кандидату на
	//   границе, а следующие 34 по-прежнему не получают ничего.
	//
	//   КАК СТАЛО. Два прохода в порядке счёта:
	//     1. КАЖДОМУ кандидату выдаётся ОДИН источник — минимальная форма (точка), но полная сила.
	//        Пока бюджет не кончился, ни один эмиттер не остаётся неосвещённым.
	//     2. Остаток бюджета раздаётся на ПОДРОБНОСТЬ формы: кандидаты по очереди счёта добираются
	//        до своей полной цены (6 кусков трубки, 8 полос блина).
	//   Кадр света укорачивает раскладку под выданное (UAsdaFxLightComponent::ApplyFrame, блок С4),
	//   а суммарный поток раскладки равен потоку точки при любом числе кусков — значит яркость
	//   сцены не зависит от того, хватило бюджета на подробность или нет.
	//
	//   ★★★ РЕВИЗИЯ fix3 (КРИТ-1, КРИТ-2, С-4, 20.09.2026): ЛЕСТНИЦА ИЗ ЧЕТЫРЁХ ПРОХОДОВ.
	//
	//   ЧТО БЫЛО НЕ ТАК — ТРИ ВЕЩИ СРАЗУ.
	//   (1) Цена списывалась как «площадь полной формы ÷ число её кусков» независимо от того,
	//       сколько мест кандидату выдали. У блина и кольца, вырожденных выдачей в ОДИН источник,
	//       на сцене стояла точка ПОЛНОГО радиуса, а списывалась доля 0.7² от неё: одинаковая
	//       картинка — вдвое разный след (замер ревизора при бюджете 7: 4.91 у точки, 2.40 у блина).
	//   (2) «Каждому по одному» вырождало блин в точку у всех, кому не хватило полной цены:
	//       при бюджете 64 и 45 эмиттерах форму получали 7, а 38 горели точкой. То есть вырождалась
	//       ровно та форма, ради которой волна затевалась.
	//   (3) Подробность не имела гистерезиса вовсе — кандидат на границе моргал формой покадрово.
	//
	//   КАК СТАЛО. Четыре прохода в порядке счёта, и на каждом шаге цена спрашивается у САМОГО
	//   кандидата за ТО число мест, которое ему сейчас дают (EstimateScreenArea(..., Give)) — то
	//   есть считается по той же раскладке, какой потом загорится свет:
	//     A. КАЖДОМУ по одному источнику — ни один эмиттер не остаётся неосвещённым (закон С4 Ф11);
	//     B. МИНИМУМ ФОРМЫ: кандидат добирается до числа, ниже которого форма перестаёт быть собой
	//        (кольцу — три дуги; блину после КРИТ-2 хватает и одной полосы, поэтому он сюда не идёт);
	//     C. ПОЛНАЯ ПОДРОБНОСТЬ: остаток раздаётся по очереди счёта — это «натуральная» раздача;
	//     D. УДЕРЖАНИЕ (С-4): кому натуральная раздача дала МЕНЬШЕ, чем он держал, возвращают
	//        разницу — из остатка бюджета, а если остатка нет, ОТНИМАЯ у самого слабого по счёту.
	//        ★ Удержание стоит ПОСЛЕДНИМ сознательно: поставленное перед раздачей подробности, оно
	//        намертво замораживало случайную раскладку первых кадров (замер прогона B: 19 кандидатов
	//        держали по 2 полосы, и шесть ближайших к игроку не могли получить свои четыре НИКОГДА,
	//        потому что срок удержания обнулялся каждый кадр — им же и выданной подробностью).
	//        Теперь срок копится ровно тогда, когда удержание ПОНАДОБИЛОСЬ, и по его истечении
	//        раскладка становится натуральной.
	//   ★ Шаг цены СЧИТАЕТСЯ РАЗНИЦЕЙ Area(g+1) − Area(g), а не «площадью одного источника»: у блина
	//     переход «1 полоса → 2 полосы» может быть и ДЕШЕВЛЕ нуля (одна полоса светит на полный
	//     радиус, две — на 0.7 каждая), и бюджет обязан это видеть, а не делать вид, что все шаги
	//     одинаковы.
	const float HoldSec = FMath::Max(GAsdaFxLightDetailHold, 0.f);
	TArray<int32> Give;
	Give.SetNumZeroed(Cands.Num());
	// ★ С-4: кому подробность досталась ТОЛЬКО удержанием (натуральная раздача дала меньше). Ровно у
	//   них идёт срок удержания: у прочих он обязан стоять, иначе «срок» отсчитывался бы у всех подряд.
	TArray<bool> bHeld;
	bHeld.Init(false, Cands.Num());
	// Площадь кандидата при выданных g источниках (g >= 1). Одна лямбда на все четыре прохода.
	auto AreaAt = [&](const FCand& X, int32 g) { return X.C->EstimateScreenArea(ViewLoc, X.Spec, RadiusCap, FMath::Max(g, 1)); };
	// Поднять кандидату подробность до Target, пока хватает обоих бюджетов. Возвращает, поднялась ли
	auto RaiseTo = [&](int32 i, int32 Target) -> bool
	{
		const FCand& X = Cands[i];
		bool bAny = false;
		while (Give[i] < Target && Admitted + 1 <= LastBudget)
		{
			const float dA = AreaAt(X, Give[i] + 1) - AreaAt(X, Give[i]);
			if (ScreenUsed + dA > ScreenBudget) { break; }
			++Give[i]; ++Admitted; ScreenUsed += dA; bAny = true;
		}
		return bAny;
	};
	// A. Каждому по одному
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		const FCand& X = Cands[i];
		const float A1 = AreaAt(X, 1);
		if (Admitted + 1 > LastBudget) { continue; }
		if (ScreenUsed + A1 > ScreenBudget) { ++CutByScreen; continue; }
		Give[i] = 1;
		++Admitted;
		ScreenUsed += A1;
	}
	// B. Минимум формы (кольцу — три дуги: двумя обод не собрать)
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		if (Give[i] <= 0) { continue; }
		RaiseTo(i, FMath::Min(FMath::Max(Cands[i].MinCost, 1), FMath::Max(Cands[i].Cost, 1)));
	}
	// ★★★ fix9 (С-4): ПОТОЛОК МЕСТ НА ОДНОГО КАНДИДАТА. Довод и числа — у объявления ключа
	//   asda.Fx.Light.PerCandMax. При нуле (умолчание) выражение тождественно прежнему.
	auto CapGive = [&](int32 i, int32 Target) -> int32
	{
		if (GAsdaFxLightPerCandMax <= 0) { return Target; }
		// Ниже минимума формы не опускаемся: там режется уже не подробность, а сама форма
		const int32 Floor = FMath::Clamp(Cands[i].MinCost, 1, FMath::Max(Cands[i].Cost, 1));
		return FMath::Max(FMath::Min(Target, GAsdaFxLightPerCandMax), Floor);
	};
	// C. Полная подробность — «натуральная» раздача
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		if (Give[i] <= 0) { continue; }
		RaiseTo(i, CapGive(i, FMath::Max(Cands[i].Cost, 1)));
	}
	// D. Удержание подробности (гистерезис С-4). Кандидату, которому натуральная раздача дала меньше,
	//    чем он держал, разницу ВОЗВРАЩАЕМ — из ОСТАТКА бюджета.
	//    ★ ОТНИМАТЬ У ДРУГИХ — ПРОБОВАЛИ И ОТКАЗАЛИСЬ. Вариант «нет остатка — забрать подробность у
	//      самого слабого по счёту» замерен (прогон C, трубка ×6, 150 кадров, бюджет на границе):
	//      смен раскладки 5 без гистерезиса против 13 с ним. Отъём сам превращался в дребезг —
	//      кандидат i отнимал у j, на следующем кадре j отнимал обратно. То есть средство от
	//      мелькания мелькание же и создавало. Удержание оставлено БЕЗОПАСНЫМ: оно может только
	//      не дать подробности упасть, пока в бюджете есть место, и не может отобрать чужое.
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		if (Give[i] <= 0) { continue; }
		const FAsdaFxDetailHold* H = DetailHold.Find(Cands[i].C);
		if (!H || H->Age >= HoldSec) { continue; }
		// ★ fix9 (С-4): удержание тоже под потолком — иначе он обходился бы памятью о прошлом кадре
		const int32 Want = CapGive(i, FMath::Min(H->Give, FMath::Max(Cands[i].Cost, 1)));
		if (Want <= Give[i]) { continue; }   // натуральная раздача и так не меньше — держать нечего
		bHeld[i] = true;
		RaiseTo(i, Want);
	}
	// ★ С-4: ведём сроки удержания. Выдали столько же или больше — «спокойный» кадр, срок сброшен;
	//   выдали меньше, чем держали, — срок копится, и по истечении DetailHoldSec понижение принимается.
	//   ★ Срок копится ИМЕННО ЗДЕСЬ, после удержания: иначе он обнулялся бы той подробностью, которую
	//     само удержание и выдало, и раскладка первых кадров осталась бы навсегда (см. довод выше).
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		if (Give[i] <= 0) { DetailHold.Remove(Cands[i].C); continue; }
		FAsdaFxDetailHold& H = DetailHold.FindOrAdd(Cands[i].C);
		if (Give[i] > H.Give)       { H.Give = Give[i]; H.Age = 0.f; }   // подробность выросла — держим новую
		else if (Give[i] == H.Give && !bHeld[i]) { H.Age = 0.f; }        // держится сама собой — срок не идёт
		else { H.Age += DeltaTime; if (H.Age >= HoldSec) { H.Give = Give[i]; H.Age = 0.f; } }
	}
	for (auto It = DetailHold.CreateIterator(); It; ++It) { if (!It->Key.IsValid()) { It.RemoveCurrent(); } }
	// ★ С-4: СЧИТАЕМ МЕЛЬКАНИЕ ЧИСЛОМ. Сколько кандидатов получили ДРУГУЮ подробность, чем в прошлом
	//   кадре, — это и есть мелькание формы; копится до ближайшего дампа (он печатает и обнуляет).
	LastDetailChanges = 0;
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		if (Cands[i].C->GetAdmittedCost() != Give[i]) { ++LastDetailChanges; }
	}
	DetailChangesSum += LastDetailChanges;
	++DetailFrames;
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		const FCand& X = Cands[i];
		const bool bAdmit = (Give[i] > 0);
		X.C->SetAdmitted(bAdmit);
		X.C->SetAdmittedCost(Give[i]);   // ★ Ф10-испр.: кадр света не зажжёт форму дороже выданного
		// ★★★ fix11 (пункт 3 ревизии fix10): ЗАПОМИНАЕМ И ЗАПРОС. Дамп обязан различать «бюджет не
		//   дал столько, сколько просили» и «попросили меньше, чем стоит собственная раскладка»:
		//   первое — работа бюджета, второе — дефект (ровно он и был пунктом 2 этой волны). Число
		//   берётся ЗДЕСЬ, у того же отбора, который раздаёт места, а не пересчитывается дампом.
		X.C->SetRequestedCost(FMath::Max(X.Cost, 1));
		// ★ Ф9: ЦЕЛЬ ХОДА СИЛЫ. Место есть и кандидат в кадре — разгораться; место есть, но он вышел
		//   из кадра (доживает отсрочку) — гаснуть ходом; места нет — цель не важна, свет и так погашен,
		//   но её всё равно сбрасываем в 0, чтобы вернувшийся кандидат разгорелся, а не вспыхнул.
		X.C->SetFadeTarget((bAdmit && !X.bFading) ? 1.f : 0.f);
		if (bAdmit)
		{
			LeaseAge.Add(X.C, X.Age + DeltaTime);
		}
		else
		{
			X.C->SetGate(EAsdaFxLightGate::NotAdmitted);
			LeaseAge.Remove(X.C);
		}
	}
	LastAdmitted = Admitted;
	LastScreenUsed = ScreenUsed;
	LastCutByScreen = CutByScreen;

	// ★ В НЕИГРОВОМ МИРЕ КАДР СВЕТА ДВИГАЕМ САМИ. Тик компонента в редакторском мире идёт только при
	//   Realtime-вьюпорте (по RDP он выключен всегда), а подсистема тикает как FTickableGameObject
	//   независимо от него. Без этого показ в редакторе выглядел бы как «место есть, гейт погасил»:
	//   подсистема раздала места, а свет так и остался с конструкторскими значениями (проверено 09.09.2026).
	//   В игре сюда не заходим: там кадр света двигает TickComponent в TG_PostUpdateWork с prerequisite
	//   на эффект, и вызывать то же самое дважды за кадр незачем.
	if (bEditorWorld)
	{
		for (const FCand& X : Cands)
		{
			if (X.C->IsAdmitted()) { X.C->ApplyFrame(DeltaTime); }
		}
	}

	// Сколько ФАКТИЧЕСКИ горит после всех гейтов (место в бюджете ≠ горит: эффект мог не построить геометрию,
	// догореть по альфе или уйти под гейт значимости). Считаем здесь, чтобы дамп не врал.
	// ★★★ РЕВИЗИЯ fix3 (С-5, 20.09.2026): ЗДЕСЬ ЖЕ УБИРАЕМ ЛИШНИЕ ДОЧЕРНИЕ ИСТОЧНИКИ ФОРМЫ — ПО ВСЕМ
	//   КАНДИДАТАМ, А НЕ ПО ГОРЯЩИМ. Ревизия Ф11 звала уборку из кадра света (ApplyFrame), но кадр
	//   света считается ТОЛЬКО у кандидата с местом: у вытесненного бюджетом тик компонента выключен
	//   (SetAdmitted → SetComponentTickEnabled(false)), а сюда, в редакторском мире, ApplyFrame зовётся
	//   тоже только для admitted. То есть уборка обходила стороной ровно тот случай, ради которого
	//   написана: эффект, которому перебрали формы и который затем вытеснили из бюджета, держал
	//   16…32 мёртвых зарегистрированных компонента ДО СВОЕГО ВОЗВРАЩЕНИЯ В БЮДЖЕТ — а вернуться он
	//   мог и никогда. Проход по Candidates идёт в этом же цикле: второго обхода списка заводить
	//   незачем, а уборка сама себя гасит отсрочкой (asda.Fx.Light.PoolIdleSec).
	LastLit = 0;
	for (UAsdaFxLightComponent* C : Candidates)
	{
		if (!IsValid(C)) { continue; }
		if (C->IsLit()) { ++LastLit; }
		C->TrimShapePools(DeltaTime);
	}
}
