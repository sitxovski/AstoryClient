// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightComponent_Emitters.cpp — ★ R2 10.11.2026 (разрез AsdaFxLightComponent.cpp, дословный перенос): донор/сбор эмиттеров, подготовка к эффекту, тени, оценка экрана, допуск, гашение.
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

int32 UAsdaFxLightComponent::PickDonorEmitter(const AAsdaEffectActor* Fx)
{
	// Донор выбирается ПО ДАННЫМ узлов, а не по геометрии: на момент заведения эффект ещё не построил ни одной
	// секции (StartDelay, первый кадр), и «самый яркий по вершинам» вернул бы случайный эмиттер.
	// Мера «светимости» эмиттера = max по узлам (яркость цвета × альфа × полу-экстент): так у костра донором
	// становится пламя, а не дым, а у портала — светящееся кольцо, а не искры.
	int32 Best = 0;
	double BestW = -1.0;
	for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
	{
		const FAsdaFxEmitter& E = Fx->Emitters[i];
		// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 3 ревизии: ПОГАШЕННЫЙ ЭМИТТЕР НЕ МОЖЕТ БЫТЬ ДОНОРОМ.
		//   Владелец ставит эмиттеру «Света у этого эмиттера нет» — и до этой волны у ОБЩЕГО света
		//   эффекта (охват «один на эффект», 11 семейств из 18) поле не читалось вовсе: погашенный
		//   эмиттер продолжал быть донором, то есть давал свету оттенок, позицию, оси формы и радиус.
		//   Гашение при этом выглядело как «ничего не произошло» — ровно та жалоба, ради которой
		//   поле и заводилось (памятка владельцу, п. 3).
		if (E.Glow.Source == EAsdaFxEmitterGlowSource::Off) { continue; }
		const double W = AsdaFxLight::EmitterWeight(E);   // ★ В1-C F15: общая формула веса (AsdaEffectActor.h)
		if (W > BestW) { BestW = W; Best = i; }
	}
	// ★ РЕВИЗИЯ «форма-2» (мелкое): ЕСЛИ ПОГАШЕНЫ ВСЕ, ДОНОРА НЕТ ВОВСЕ. Прежде цикл не находил ни
	//   одного кандидата, `Best` оставался нулём, и функция возвращала ПОГАШЕННЫЙ эмиттер #0 — а лог
	//   бодро рапортовал «донор перевыбран на #0». Возвращаем INDEX_NONE: «донора нет» читается всеми
	//   ветками как «не жив», и свет честно гаснет гейтом «свечение снято у ВСЕХ эмиттеров».
	return (BestW < 0.0) ? INDEX_NONE : Best;
}

// ★ Яркость цвета по Rec.709 — та же нормаль, что в PickDonorEmitter (одна формула на весь механизм)
//   ★ Дедуп №2 В1-C (F15): НЕ та же формула — здесь отрицательные каналы режутся (`Max(C, 0)`), а вес эмиттера
//   (`AsdaFxLight::EmitterWeight`) их не режет. Сводить = менять поведение (решение — В2), поэтому оставлена.
static FORCEINLINE double AsdaLum(const FLinearColor& C)
{
	return 0.2126 * FMath::Max(C.R, 0.f) + 0.7152 * FMath::Max(C.G, 0.f) + 0.0722 * FMath::Max(C.B, 0.f);
}

void UAsdaFxLightComponent::GatherEmitters(const AAsdaEffectActor* Fx, const FAsdaFxLightSpec& Spec, int32 DonorIdx, FAsdaFxLightAgg& Out, int32 OnlyEmitter)
{
	Out = FAsdaFxLightAgg();
	if (!Fx) { return; }

	// Один эмиттер = одна запись. TInlineAllocator: у эффектов архива эмиттеров единицы, кучу трогать не за чем
	struct FEntry { double W; double Lum; FVector C; float R; FLinearColor Chroma; };
	TArray<FEntry, TInlineAllocator<8>> Ent;

	const int32 N = Fx->Emitters.Num();
	double WSum = 0.0, GSum = 0.0, GMaxLocal = 0.0;
	FVector Acc = FVector::ZeroVector;
	// ★ Ф7: КОРОБКА объединения секций (AABB), собирается ТЕМ ЖЕ проходом — второй проход по эмиттерам
	//   рано или поздно разойдётся с первым (закон diagnostic-must-use-real-path).
	FVector BoxMin(TNumericLimits<double>::Max()), BoxMax(-TNumericLimits<double>::Max());

	for (int32 i = 0; i < N; ++i)
	{
		// ★★★ ВОЛНА Ф8: свет ОДНОГО эмиттера — все прочие в агрегат не входят вовсе. Отдельной
		//   функции «собрать один» намеренно НЕТ: вес, площадь и оттенок обязаны считаться ТОЙ ЖЕ
		//   формулой, иначе поэмиттерный свет и общий свет того же эффекта разъедутся по яркости.
		if (OnlyEmitter >= 0 && i != OnlyEmitter) { continue; }
		// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 3 ревизии: «СВЕТА У ЭТОГО ЭМИТТЕРА НЕТ» ДЕЙСТВУЕТ
		//   ПРИ ЛЮБОМ ОХВАТЕ. До этой волны поле читалось ровно в одном месте — ComputeDesiredSet,
		//   то есть ТОЛЬКО при охвате «свой свет у каждого эмиттера» (7 семейств из 18, свет включён
		//   у трёх). У остальных одиннадцати (огонь карт, фонари, порталы, столбы, оружие, транспорт,
		//   дроп, интерьер, метки, дым, «прочее») владелец гасил эмиттер — и не менялось РОВНО
		//   НИЧЕГО: погашенный эмиттер продолжал давать общему свету вес, оттенок, центр тяжести,
		//   коробку и габарит. Замер прежней волны это и показал: «погасили все 83 — 2 источника,
		//   25.14 кд, свет горит как горел» (памятка владельцу, таблица в п. 3).
		//   ★ Правило стоит ЗДЕСЬ, в единственном проходе по эмиттерам, а не второй проверкой у
		//     каждого потребителя: второй проход рано или поздно разойдётся с первым
		//     (закон diagnostic-must-use-real-path), и «цвет учёл погашенный, а радиус нет» ловилось
		//     бы месяцами. Поэмиттерного света это не касается вовсе: его у погашенного эмиттера
		//     просто нет (ComputeDesiredSet такого не заказывает), и OnlyEmitter сюда не придёт.
		if (Fx->Emitters.IsValidIndex(i) && Fx->Emitters[i].Glow.Source == EAsdaFxEmitterGlowSource::Off)
		{
			++Out.OffByOwner;
			continue;
		}
		bool bStopped = false;
		float LifeTimer = 0.f, NodeAlpha = 1.f, HalfExtent = 0.f;
		FLinearColor NodeColor = FLinearColor::White;
		if (!Fx->GetEmitterRuntime(i, bStopped, LifeTimer, &NodeAlpha, &HalfExtent, &NodeColor) || bStopped) { continue; }
		++Out.WithRuntime;

		FVector C; float R = 0.f; int32 Verts = 0;
		// ★ 15.10.2026: эффект на движущемся носителе — облако мировых частиц «в кадре носителя» (центр И габарит),
		//   а не оставшееся позади (см. GAsdaFxLightCarrierAnchor в шапке файла).
		if (!Fx->EmitterSectionStats(i, C, R, Verts, AsdaFxLightCarrierFrame(Fx, i))) { continue; }
		// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 1 ревизии: ВЫРОЖДЕННАЯ СЕКЦИЯ — ЭТО НЕ СВЕТ.
		//   Секция построена, но ВСЕ её вершины в одной точке (радиус ≤ 1 юн) И полу-экстент узла
		//   нулевой ⇒ эффект в этом месте НЕ РИСУЕТ НИЧЕГО. Прежде такой эмиттер всё равно входил в
		//   агрегат: вес считается как `вершины × max(полу-экстент, 1)²`, и этот зажим единицей
		//   ПРОИЗВОДИЛ площадь из ничего. Для поэмиттерного света это давало ровно то, на что
		//   жаловался владелец: источник радиусом «нижний зажим» (160…192 юн) СВЕТИТ ИЗ ТОЧКИ,
		//   а картинки под ним нет вовсе.
		//   Замер: `ch1_a_4th_mg_02_2`, эмиттер #5 `Bil_002` — SizeX = SizeY = 0 и радиус секции
		//   0.000 юн во ВСЕХ десяти снятых моментах периода, и при этом «ГОРИТ» (10 строк дампа из
		//   60 в каждом из пяти прогонов).
		//   ★ Проверяются ОБА признака сразу: у живого квада, который сейчас схлопнут анимацией,
		//     полу-экстент узла остаётся ненулевым, и его удержание габарита лечит правильно
		//     (см. GeomHoldSec). Гасить такого нельзя — он светит и будет светить.
		if (R <= 1.f && HalfExtent <= KINDA_SMALL_NUMBER)
		{
			++Out.DegenerateSection;
			continue;
		}
		++Out.WithSection;

		const float A = FMath::Clamp(NodeAlpha, 0.f, 1.f);

		// ★ ДОНОР СНИМАЕТСЯ ТЕМ ЖЕ ПРОХОДОМ. Иначе откатные режимы читали бы эмиттеры второй раз —
		//   и, что хуже, второй формулой: расхождение «свет откатили, а он другой» ловилось бы месяцами
		if (i == DonorIdx)
		{
			Out.bDonorLive = true;
			Out.DonorCentroid = C;
			// Секция вырождена (один квад в точке) → полу-экстент узла: он всегда > 0 (поведение Ф2 сохранено)
			Out.DonorRadius = R > 1.f ? R : FMath::Max(HalfExtent, 1.f);
			Out.DonorAlpha = A;
			Out.DonorColor = NodeColor;
		}

		if (A <= KINDA_SMALL_NUMBER) { continue; }

		// ВЕС: площадь × яркость (g) и он же × альфа (w). См. шапку World/AsdaFxLightSpec.h
		const double Area = (double)Verts * (double)FMath::Max(HalfExtent, 1.f) * (double)FMath::Max(HalfExtent, 1.f);
		const double Lum = AsdaLum(NodeColor);
		const double G = Area * Lum;
		const double W = G * (double)A;
		if (!(W > 0.0)) { continue; }   // чёрный узел ничего не излучает — в агрегат не входит

		++Out.Live;
		GSum += G; WSum += W; GMaxLocal = FMath::Max(GMaxLocal, G);
		Acc += C * W;
		// Секция входит в коробку как шар C ± R: сами вершины здесь не читаются (их читал бы второй проход)
		{
			const double Rr = (double)(R > 1.f ? R : FMath::Max(HalfExtent, 1.f));
			BoxMin = FVector(FMath::Min(BoxMin.X, C.X - Rr), FMath::Min(BoxMin.Y, C.Y - Rr), FMath::Min(BoxMin.Z, C.Z - Rr));
			BoxMax = FVector(FMath::Max(BoxMax.X, C.X + Rr), FMath::Max(BoxMax.Y, C.Y + Rr), FMath::Max(BoxMax.Z, C.Z + Rr));
		}
		Ent.Add({ W, Lum, C, R > 1.f ? R : FMath::Max(HalfExtent, 1.f), AsdaChroma(NodeColor) });
	}

	if (Out.Live == 0) { return; }

	Out.WSum = WSum; Out.GSum = GSum;
	// ── ПОЗИЦИЯ: центр тяжести ПО ВКЛАДУ ──────────────────────────────────────────────────────────────
	Out.Centroid = Acc / WSum;
	// ── ПОЗИЦИЯ-2 (Ф7): середина КОРОБКИ, без весов ───────────────────────────────────────────────────
	Out.BoxCenter = (BoxMin + BoxMax) * 0.5;
	// ── АЛЬФА: ПОТОК, НОРМИРОВАННЫЙ НА САМЫЙ СИЛЬНЫЙ ЭМИТТЕР ─────────────────────────────────────────
	// α = Σ w_i / max_i g_i, зажатая единицей.
	//   ЗНАМЕНАТЕЛЬ БЕЗ АЛЬФЫ — иначе вышло бы α², и полупрозрачный эмиттер тянул бы свет к нулю быстрее,
	//   чем гаснет картинка. ★ А ВОТ НОРМИРОВКА НА Σ g_i (первая редакция Ф4) БЫЛА ОШИБКОЙ: это СРЕДНЯЯ
	//   альфа, и она делала многоэмиттерный эффект ТЕМНЕЕ одноэмиттерного — ровно то, чего владелец просил
	//   не допустить. Замер на стенде 10.09: навык defminb03 (45 живых эмиттеров) — донор Ф2 6.40 кд,
	//   средняя 2.14 кд, освещённость пола 40.6 → 14.4. Нормировка на СИЛЬНЕЙШИЙ эмиттер даёт: у эффекта
	//   с одним эмиттером α = α_1 (то есть ровно Ф2, и принятые числа не могут сдвинуться), у эффекта из
	//   многих — «во сколько раз суммарный поток превосходит поток сильнейшего», обрезанное единицей:
	//   добавка эмиттеров может ДОВЕСТИ свет до полной силы, но никогда не опустит его ниже сильнейшего.
	Out.GMax = GMaxLocal;
	Out.Alpha = (GMaxLocal > 0.0) ? (float)FMath::Clamp(WSum / GMaxLocal, 0.0, 1.0) : 0.f;

	// ── РАДИУС: габарит ОБЪЕДИНЕНИЯ секций вокруг центра тяжести ──────────────────────────────────────
	// R = max_i(|P − C_i| + R_i) — охватывающая сфера объединения, посчитанная БЕЗ второго прохода по
	// вершинам. ★ У ОДНОГО эмиттера P = C_1 и R = R_1 ТОЧНО, поэтому принятые числа не могут сдвинуться.
	auto MeasureAround = [&Ent](const FVector& P, float& OutR, float& OutSpread)
	{
		OutR = 0.f; OutSpread = 0.f;
		for (const FEntry& E : Ent)
		{
			const float D = (float)FVector::Dist(P, E.C);
			OutR = FMath::Max(OutR, D + E.R);
			OutSpread = FMath::Max(OutSpread, D);
		}
	};
	MeasureAround(Out.Centroid, Out.GeomRadius, Out.Spread);

	// ★★★ ПОВОДОК: ЦЕНТР ТЯЖЕСТИ НЕ МОЖЕТ УЕХАТЬ ОТ САМОГО ЭФФЕКТА ДАЛЬШЕ ЕГО СОБСТВЕННОГО ГАБАРИТА.
	//   ЗАЧЕМ. Вес эмиттера — площадь×яркость×альфа, и у эффекта, чья масса свечения ушла вверх или вбок
	//   (шлейф метеора, столб навыка 4-й профессии), центр тяжести уезжает от места, ГДЕ ЭФФЕКТ ЖИВЁТ.
	//   Замер на стенде 10.09: у ch1_a_4th_ac_04_1 центр тяжести встал на 934 юн над полом при радиусе 700 —
	//   сфера отсечения до пола не доставала, и освещённость пола была РОВНО НОЛЬ при 10 кд силы (у донора
	//   Ф2 — 10.3). У eff_1_char_06_bigmeob03 центр уехал на 1300 юн ВБОК от актора.
	//   ПОЧЕМУ ИМЕННО ГАБАРИТ. Это единственная величина того же порядка, что и сам эффект, и она уже
	//   посчитана: «свет стоит внутри эффекта» — инвариант, который нельзя нарушать, а насколько высоко
	//   внутри — решает вес. У компактных эффектов поводок не срабатывает вовсе (|P − актор| < габарита),
	//   поэтому принятые числа столбов и фонаря сдвинуться не могут.
	const FVector ActorLoc = Fx->GetActorLocation();
	const FVector Off = Out.Centroid - ActorLoc;
	const float OffLen = (float)Off.Size();
	// ★★★ ВОЛНА Ф8: У ПОЭМИТТЕРНОГО СВЕТА ПОВОДКА НЕТ. Поводок сдерживает центр ТЯЖЕСТИ, который у
	//   разлетающегося эффекта уезжает в никуда; но эмиттер, стоящий В СТОРОНЕ от пивота (крыло башни,
	//   конец радуги), уезжает туда ПО ЗАМЫСЛУ — притянуть его к пивоту значило бы вернуть ровно ту
	//   «лампочку в центре», ради избавления от которой волна и затевалась.
	if (OnlyEmitter < 0 && OffLen > Out.GeomRadius && OffLen > KINDA_SMALL_NUMBER)
	{
		Out.bLeashed = true;
		Out.Centroid = ActorLoc + Off * (Out.GeomRadius / OffLen);
		// Габарит пересчитываем ВОКРУГ НОВОЙ ТОЧКИ — иначе радиус перестал бы накрывать секции, ради
		// которых он и считался (та же ошибка, что «пересчитать второй формулой» в дампе)
		MeasureAround(Out.Centroid, Out.GeomRadius, Out.Spread);
	}

	// ★ Ф7: КОРОБКА ДЕРЖИТСЯ НА ТОМ ЖЕ ПОВОДКЕ — и проверяется ОТДЕЛЬНО от центра тяжести.
	//   Середина коробки уезжает от актора не там же, где центр тяжести (у шлейфа масса свечения внизу,
	//   а коробка растянута наверх), поэтому проверку нельзя вкладывать внутрь «если центр уехал»:
	//   режим «центр габаритов» обошёл бы защиту, ради которой поводок и написан.
	{
		const FVector BOff = Out.BoxCenter - ActorLoc;
		const float BLen = (float)BOff.Size();
		if (OnlyEmitter < 0 && BLen > Out.GeomRadius && BLen > KINDA_SMALL_NUMBER)
		{
			Out.BoxCenter = ActorLoc + BOff * (Out.GeomRadius / BLen);
		}
	}

	// ── ЦВЕТ ──────────────────────────────────────────────────────────────────────────────────────────
	if (Spec.ColorMode == EAsdaFxLightColorMode::Median)
	{
		// ВЗВЕШЕННАЯ МЕДИАНА: упорядочиваем эмиттеры по ЯРКОСТИ их узла и берём цвет того, на котором
		// накопленный ВКЛАД переходит половину. Возвращается цвет РЕАЛЬНОГО эмиттера — покомпонентная
		// медиана у трёх эмиттеров R/G/B дала бы (0,0,0), то есть чёрный свет, которого нет ни у кого.
		Ent.Sort([](const FEntry& A, const FEntry& B) { return A.Lum < B.Lum; });
		double Run = 0.0; const double Half = WSum * 0.5;
		Out.Color = Ent.Last().Chroma;
		for (const FEntry& E : Ent) { Run += E.W; if (Run >= Half) { Out.Color = E.Chroma; break; } }
	}
	else
	{
		// СРЕДНЕВЗВЕШЕННЫЙ ПО ВКЛАДУ. Усредняются ОТТЕНКИ (уже нормированные), а не сырые цвета: яркость
		// эмиттера учтена в весе, и второй раз ей входить нельзя — иначе один яркий узел давал бы двойной вес.
		FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
		for (const FEntry& E : Ent) { Sum += E.Chroma * (float)(E.W / WSum); }
		Out.Color = Sum;
	}
	Out.Color = AsdaChroma(Out.Color);   // средний оттенок тоже нормируем: калибровка канделы обязана уцелеть
}

const TCHAR* UAsdaFxLightComponent::GetClipName() const
{
	switch (LastClip)
	{
	case 1: return TEXT("нижний зажим");
	// ★★★ РЕВИЗИЯ «форма-2» (СРЕДН-6): ИСТОЧНИК ЗАЖИМА НЕ ЗАШИВАЕТСЯ СЛОВОМ.
	//   «ПОТОЛОК СЕМЕЙСТВА» печаталось всегда, хотя `RadiusMax` мог прийти не от семейства, а с
	//   замка ассета (`FxLight.bManual`) или из конфигуратора свечения; «уровневый потолок» —
	//   и тогда, когда потолок задан ключом `asda.Fx.Light.RadiusCap`, а не уровнем качества.
	//   Ветку, давшую числа, компонент уже знает (`LastSpecSrc`), а происхождение потолка —
	//   та же развилка, что у `ResolveRadiusCap`.
	case 2: return LastLeashed ? TEXT("верхний зажим+поводок") : TEXT("верхний зажим (см. «откуда»)");
	case 3: return LastLeashed
		? (UAsdaFxLightSubsystem::RadiusCapFromKey() ? TEXT("потолок ключа+поводок") : TEXT("потолок уровня+поводок"))
		: (UAsdaFxLightSubsystem::RadiusCapFromKey() ? TEXT("ПОТОЛОК КЛЮЧА") : TEXT("ПОТОЛОК УРОВНЯ"));
	}
	return LastLeashed ? TEXT("поводок") : TEXT("—");
}

void UAsdaFxLightComponent::SetupForEffect(AAsdaEffectActor* InFx, int32 InRuleIndex, int32 InEmitterIndex)
{
	Effect = InFx;
	RuleIndex = InRuleIndex;
	// ★★★ ВОЛНА Ф8: чей это свет. Ставится ДО первого ResolveSpec — иначе поэмиттерный компонент
	//   на самом первом кадре взял бы числа эффекта и на кадр мигнул бы чужой яркостью.
	EmitterIndex = (InFx && InFx->Emitters.IsValidIndex(InEmitterIndex)) ? InEmitterIndex : INDEX_NONE;
	if (!InFx) { return; }

	const FAsdaFxLightSpec Spec = ResolveSpec();

	// Донор фиксируется ОДИН раз: иначе свет прыгал бы между эмиттерами внутри одного эффекта.
	// Правка поля «Эмиттер-донор» в редакторе применится при следующем заведении кандидата (подсистема
	// пересобирает кандидатов на изменение свойств актора — см. UAsdaFxLightSubsystem::OnObjectChanged).
	DonorEmitter = (Spec.Emitter >= 0 && InFx->Emitters.IsValidIndex(Spec.Emitter)) ? Spec.Emitter : PickDonorEmitter(InFx);
	// ★★★ ВОЛНА Ф8: у поэмиттерного света донор — САМ ЭМИТТЕР. Иначе донорские режимы (цвет «из узла
	//   одного эмиттера», радиус «из геометрии одного эмиттера») читали бы ЧУЖОЙ эмиттер, которого в
	//   собранном агрегате этого света вообще нет, и молча гасли бы гейтом «донор не жив».
	if (EmitterIndex >= 0) { DonorEmitter = EmitterIndex; }

	// ★ «форма-2», п. 1: удержание габарита — это ПАМЯТЬ ОДНОГО СВЕТА. При перепривязке компонента к
	//   другому эффекту или эмиттеру она обязана обнулиться, иначе новый свет на кадр взял бы чужой
	//   габарит (и, что хуже, чужой множитель формы).
	HeldGeomRaw = 0.f;
	HeldShapeRaw = 0.f;
	ShapeHoldK = 1.f;
	LastShapeHalf = 0.f;

	// Форма и дальность — стартовые значения; каждый тик они перечитываются заново из действующих чисел
	SetSourceRadius(Spec.SourceRadius);
	SetSoftSourceRadius(Spec.SoftSourceRadius);
	SetAttenuationRadius(FMath::Max(Spec.RadiusMin, 1.f));
	SetMaxDrawDistance(ResolveDrawDistance(Spec));
	SetMaxDistanceFadeRange(Spec.FadeRange);
	SetVolumetricScatteringIntensity(Spec.VolumetricScattering * UAsdaDevMenuSubsystem::FogVolumetricLightScale(GetWorld()));
	SetAffectGlobalIllumination(Spec.bAffectGI);

	// ★ ПОРЯДОК ТИКА: prerequisite на актор эффекта. Эффекты на персонаже сами живут в TG_PostUpdateWork
	//   (bFollowTickAfterPose — после движения павна и оценки позы); без зависимости мы читали бы геометрию
	//   ПРЕДЫДУЩЕГО кадра и пятно света отставало бы от пламени ровно на кадр (тот же класс бага, что чинил
	//   ApplyFollowTickOrder у самих эффектов).
	PrimaryComponentTick.AddPrerequisite(InFx, InFx->PrimaryActorTick);

	// Фаза мерцания своя у каждого света: иначе все костры карты мигали бы синхронно, как гирлянда
	FlickerPhase = FMath::FRand();

	// Позиция стартовая — актор эффекта (до первого тика геометрии ещё нет)
	SetWorldLocation(InFx->GetActorLocation() + Spec.Offset);
}

float UAsdaFxLightComponent::ResolveDrawDistance(const FAsdaFxLightSpec& Spec) const
{
	// ★★★ ВОЛНА Ф9 (10.09.2026): В РЕЖИМЕ ОТБОРА ПО КАДРУ ДАЛЬНОСТИ НЕТ ВОВСЕ.
	//   0 для ULocalLightComponent::MaxDrawDistance означает «не гасить никогда», и это ЕДИНСТВЕННОЕ
	//   место, где такое отключение действует: дальность читает сам рендер (FLightSceneInfo), поэтому
	//   снять её только в наших гейтах было бы бесполезно — движок продолжил бы гасить свет по числу
	//   из поля. Здесь же перестаёт иметь значение и уровневый множитель r.LightMaxDrawDistanceScale
	//   (на Low/Medium он 0.5): при MaxDrawDistance = 0 движок отсечку не делает вовсе, и умножать нечего.
	if (UAsdaFxLightSubsystem::IsFrustumCull()) { return 0.f; }
	const AAsdaEffectActor* Fx = Effect.Get();
	// ★ ОБЩАЯ ФОРМУЛА ДАЛЬНОСТЕЙ (аудит эффектов, препятствие 9): эффект чистит геометрию на своём CullDistance
	//   (по умолчанию 10000), а свет по своим числам гаснет раньше. Берём МИНИМУМ: свет не может пережить
	//   геометрию, из которой берёт позицию, и не может светить дальше, чем разрешают его числа.
	return FMath::Min(Spec.MaxDrawDistance, Fx ? FMath::Max(Fx->CullDistance, 1.f) : Spec.MaxDrawDistance);
}

// ═══════════════ ВОЛНА Ф6: ТЕНЬ ЭФФЕКТ-СВЕТА ═══════════════
float UAsdaFxLightComponent::ResolveShadowResScale(float SpecScale)
{
	// Ключ замера важнее всего: им снимается лестница «качество против цены» без правки 1274 ассетов
	if (GAsdaFxShadowRes >= 0.f)  { return GAsdaFxShadowRes; }
	if (GAsdaFxShadowRes <= -2.f) { return FMath::Max(SpecScale, 0.f); }
	// Уровневый ПОТОЛОК (не замена!): слабая машина не обязана тянуть 0.5, но и поднимать чужие 0.25 незачем.
	// Индекс — sg.ShadowQuality, тот же, что у бюджета теней: обе величины про одно и то же (цену теней).
	const float Cap = UAsdaRenderQualitySubsystemBase::LevelFloat(TEXT("AsdaShadow"), UAsdaRenderQualitySubsystemBase::ShadowQualityLevel(), TEXT("Light.FxShadowResScale"), 1.f);   // ★ волна 3 п.12 (15.09.2026)
	return FMath::Clamp(FMath::Min(FMath::Max(SpecScale, 0.f), Cap > 0.f ? Cap : 1.f), 0.f, 8.f);
}

bool UAsdaFxLightComponent::WantsShadow(const FAsdaFxLightSpec& Spec, float DistToView) const
{
	if (GAsdaFxShadow == 0) { return false; }
	// ★ Значение 2 — ТОЛЬКО ДЛЯ ЗАМЕРА: даёт тень всем, кто её просить не должен. Так снимается лестница
	//   «1 / 2 / 4 / 8 теней» на карте, где теневых семейств столько не набирается (закон: цену мерить на
	//   том же пути, которым ходит игрок, но количество задавать управляемо)
	if (GAsdaFxShadow < 2 && !Spec.bCastShadow) { return false; }
	// Нулевое качество движок трактует как «карту теней не строить» (r.Shadow.ResolutionScaleZeroDisablesSm=1) —
	// значит и в бюджет такому свету становиться незачем: место занял бы, а тени не дал
	if (ResolveShadowResScale(Spec.ShadowResolutionScale) <= 0.f) { return false; }
	// ★ ДАЛЬНОСТЬ ТЕНИ — СВОЯ, короче дальности света. Шесть граней куба стоят одинаково на любой дистанции,
	//   а видно тень только вблизи. Домножаем тем же уровневым множителем дальности, что и свет: иначе на Low
	//   (где он 0.5) свет гас бы раньше собственной тени
	if (Spec.ShadowMaxDistance > 0.f && DistToView > Spec.ShadowMaxDistance * UAsdaRenderQualitySubsystemBase::LightDrawDistanceScale()) { return false; }
	return true;
}

void UAsdaFxLightComponent::ApplyShadowFrame(const FAsdaFxLightSpec& Spec, bool bWant, ULocalLightComponent* Target)
{
	// ★★★ ВОЛНА Ф8: числа тени ставятся ТОМУ источнику, который реально светит (у нетоочечных
	//   форм это дочерний компонент). Без этого прямоугольный источник просил бы тень у себя,
	//   а бюджет теней вёл бы учёт по нашему ПОГАШЕННОМУ точечному — тень не появилась бы нигде,
	//   и это был бы ровно тот случай, когда «не работает» неотличимо от «выключено».
	if (!Target) { Target = this; }
	if (bWant)
	{
		// Числа тени перечитываются каждый кадр — ровно по той же причине, что цвет и радиус: правка поля
		// в панели деталей обязана быть видна сразу, а не «после перезапуска»
		const float Res = ResolveShadowResScale(Spec.ShadowResolutionScale);
		if (!FMath::IsNearlyEqual(Target->ShadowResolutionScale, Res))
		{
			// У ShadowResolutionScale нет сеттера в ULightComponent (в отличие от ShadowBias) — пишем поле и
			// помечаем состояние рендера грязным сами
			Target->ShadowResolutionScale = Res;
			Target->MarkRenderStateDirty();
		}
		if (!FMath::IsNearlyEqual(Target->ShadowBias, Spec.ShadowBias))          { Target->SetShadowBias(Spec.ShadowBias); }
		if (!FMath::IsNearlyEqual(Target->ShadowSlopeBias, Spec.ShadowSlopeBias)) { Target->SetShadowSlopeBias(Spec.ShadowSlopeBias); }
		if (!FMath::IsNearlyEqual(Target->ShadowSharpen, Spec.ShadowSharpen))     { Target->ShadowSharpen = Spec.ShadowSharpen; Target->MarkRenderStateDirty(); }
	}
	// Контактная тень живёт ОТДЕЛЬНО от карты теней (экранный луч по глубине) и потому ставится всегда,
	// даже когда бюджет отобрал у света настоящую тень: это и есть «дешёвая добавка у основания»
	const float CS = FMath::Max(Spec.ContactShadowLength, 0.f);
	if (!FMath::IsNearlyEqual(Target->ContactShadowLength, CS))
	{
		Target->ContactShadowLength = CS;
		Target->ContactShadowLengthInWS = CS > 0.f ? 1 : 0;   // юниты мира, а не доли экрана: поле подписано в юнитах
		Target->MarkRenderStateDirty();
	}

	// ★ ВОЛНА Ф8: гейт «состояние не менялось» учитывает и СМЕНУ ЦЕЛИ. Владелец переключает форму
	//   на живой сцене, и без этого прежний источник навсегда остался бы в бюджете теней с чужим слотом.
	if (bShadowWanted == bWant && ShadowTarget == Target) { return; }
	if (ShadowTarget.IsValid() && ShadowTarget.Get() != Target)
	{
		UWorld* WPrev = GetWorld();
		if (UAsdaLightBudgetSubsystem* BPrev = WPrev ? WPrev->GetSubsystem<UAsdaLightBudgetSubsystem>() : nullptr)
		{
			BPrev->SetAuthoredCastShadows(ShadowTarget.Get(), false);
		}
		else if (ShadowTarget->CastShadows) { ShadowTarget->SetCastShadows(false); }
	}
	ShadowTarget = Target;
	bShadowWanted = bWant;
	// ★ РЕШАЕТ БЮДЖЕТ, А НЕ МЫ. Компонент лишь объявляет авторское значение; сколько теней разрешено —
	//   знает UAsdaLightBudgetSubsystem (он же держит бюджет фонарей карты и не даст эффектам их вытеснить).
	UWorld* W = GetWorld();
	UAsdaLightBudgetSubsystem* B = W ? W->GetSubsystem<UAsdaLightBudgetSubsystem>() : nullptr;
	// ★ ПРОСЬБА ОБЯЗАНА БЫТЬ ВИДНА В ЛОГЕ. Первый же прогон Ф6 дал ровно ту картину, ради которой писан
	//   закон false-negatives-method: свет тень получил, а бюджет о нём не знал — и по одному «0 кандидатов»
	//   было не отличить «просьбы не было» от «просьба пришла, но мимо подсистемы». Строка редкая (только на смене).
	UE_LOG(LogAsdaFxLightComp, Log, TEXT("[тень] %s: %s (подсистема бюджета %s)"),
		*GetOwner()->GetActorNameOrLabel(), bWant ? TEXT("ПРОШУ тень") : TEXT("отказываюсь от тени"),
		B ? TEXT("есть") : TEXT("НЕТ — ставлю сам"));
	if (B)
	{
		B->SetAuthoredCastShadows(Target, bWant);
		return;
	}
	// Подсистемы нет (окно редактора: она создаётся только для игровых миров) — ставим сами, чтобы
	// владелец видел тень во вьюпорте ровно так же, как увидит в игре
	if ((Target->CastShadows != 0) != bWant) { Target->SetCastShadows(bWant); }
}

float UAsdaFxLightComponent::ComputeScore(const FVector& InViewLoc, const FAsdaFxLightSpec& Spec) const
{
	const AAsdaEffectActor* Fx = Effect.Get();
	if (!Fx) { return -1.f; }
	// «Ближе к камере и ярче — важнее»: освещённость, которую этот свет создаёт в точке камеры (I/d²),
	// взвешенная приоритетом семейства. Именно освещённость, а не голая дистанция: тусклая искра в двух метрах
	// не должна вытеснять фонарь в десяти.
	const double D2 = FMath::Max(FVector::DistSquared(InViewLoc, Fx->GetActorLocation()), 1.0);
	return (float)(Spec.Priority * FMath::Max(Spec.IntensityCd, KINDA_SMALL_NUMBER) / D2);
}

float UAsdaFxLightComponent::EstimateScreenArea(const FVector& InViewLoc, const FAsdaFxLightSpec& Spec, float RadiusCap, int32 Give) const
{
	const AAsdaEffectActor* Fx = Effect.Get();
	if (!Fx) { return 0.f; }
	// Верхняя оценка радиуса: у уже светящего — фактический радиус прошлого кадра (он же и стоил),
	// у нового кандидата — потолок его чисел (пессимистично, чтобы бюджет не пробивался «залпом»).
	// ★★★ fix8 (пункт С-6 ревизии fix7): ЧИСЛО БЕРЁТСЯ ИЗ ОБЩЕЙ ФУНКЦИИ ShapeBaseRadius — той же,
	//   что читает счёт МЕСТ через ShapePredictGeom. Прежде здесь стояло своё выражение с условием
	//   bLitNow, а там — без него, и у погашенного, но помнящего радиус кандидата два бюджета
	//   считали РАЗНЫЕ раскладки (с fix7 от BaseR зависит и число полос). Закон port-shared-points.
	const float R = ShapeBaseRadius(Spec, RadiusCap);
	const double D = FMath::Max(FVector::Dist(InViewLoc, Fx->GetActorLocation()), 1.0);
	const double T = R / D;
	if (!(R > 0.f)) { return 0.f; }
	// ★ Ф10: кольцо — N дуг радиусом (доля · R) каждая, двусторонний прямоугольник — две плоскости:
	//   экранный бюджет обязан видеть ВСЕ источники кандидата, а не один.
	// ★★★ РЕВИЗИЯ Ф11 (С1, 20.09.2026): ДО НЕЁ МНОЖИТЕЛЬ ЗНАЛИ ТОЛЬКО КОЛЬЦО И ДВУСТОРОННИЙ
	//   ПРЯМОУГОЛЬНИК. Волна Ф11 добавила блин (2…8 полос) и раскладку трубки и прямоугольника
	//   (до 16 кусков, у прямоугольника ×2 при двух сторонах), но сюда их не вписала — и у всех
	//   трёх K оставался единицей.
	// ★★★ РЕВИЗИЯ fix3 (КРИТ-1, 20.09.2026): СПИСЫВАЕТСЯ ТО, ЧТО РЕАЛЬНО ГОРИТ, А НЕ ТО, ЧТО ЗАЯВЛЕНО.
	//   Правка Ф11 была верна только для формы, получившей ПОЛНОЕ число мест. Под бюджетом форма
	//   вырождается (блин и кольцо — в точку полного радиуса), а множитель по-прежнему считался по
	//   полям: у одинаковой на экране картинки след получался разный (замер ревизора при бюджете 7:
	//   точка 4.91, блин и кольцо по 2.40 = 4.91·0.7²). Занижение было ПОСТОЯННЫМ, а не разовым:
	//   хвост ApplyShapeFrame сбрасывал фактический радиус куска в 0, и доля навсегда возвращалась
	//   к числу из поля. Теперь:
	//     1. форма, которая ЗАГОРИТСЯ при выданных местах, берётся из ОБЩЕГО правила ResolveGiveShape
	//        (его же исполняет кадр света) — поэтому «одинаковая картинка ⇒ одинаковый след»;
	//     2. у УЖЕ ГОРЯЩЕГО с тем же числом мест берутся ИЗМЕРЕННЫЕ радиусы прошлого кадра
	//        (Σ R_k², LastLitSumR2): у блина полосы-хорды имеют РАЗНЫЕ радиусы, и «N · доля²» для
	//        него неверно даже при полной подробности.
	const EAsdaFxLightShape Sh = (Spec.Shape == EAsdaFxLightShape::Auto) ? (bAutoHave ? AutoShape : EAsdaFxLightShape::Point) : Spec.Shape;
	// Give <= 0 = «полная подробность» (так функцию зовут отбор при первой оценке и дамп до раздачи)
	const int32 Ask = (Give > 0) ? Give : GetLightCost(Spec);
	// ★ fix5 (КРИТ-А): ту же геометрию, что уйдёт в SplitGeometry, получает и правило вырождения —
	//   иначе оно не смогло бы ответить, сколько плиток нужно полосе, и обещало бы число источников,
	//   которого кадр света не построит.
	const float CapG = FMath::Max(RadiusCap, 1.f);
	// ★ fix9 (КРИТ-1): экранный бюджет — тоже ПРЕДСКАЗАНИЕ по прошлому кадру, защёлку не пишет
	const FAsdaFxGiveShape G = ResolveGiveShape(Spec, Sh, Ask, LastSplitSizeR, R, CapG,
		DiscAsPoint(Spec, Sh, LastSplitSizeR, R, /*bCommit=*/false));
	double K;
	if (bLitNow && LastLitSources > 0 && LastGiveApplied == Ask && LastLitSumR2 > 0.0)
	{
		// ИЗМЕРЕНО: сумма квадратов фактических радиусов зажжённых источников, приведённая к R²
		K = LastLitSumR2 / ((double)R * (double)R);
	}
	else
	{
		// ПРЕДСКАЗАНО ПО ТОЙ ЖЕ ГЕОМЕТРИИ, какую применит кадр света: радиус куска тянется вверх под
		// его длину и вниз под потолок (SplitGeometry). Размер формы берётся из последнего кадра;
		// если кадра ещё не было, SizeR = 0 и куски считаются по доле из поля — это первая, грубая
		// оценка нового кандидата, и она живёт ровно один кадр.
		// ★★★ РЕВИЗИЯ fix5 (СРЕДН-Г, 20.09.2026): ВРЕЗКА «У НЕРАЗЛОЖЕННОЙ ТРУБКИ РАДИУС РАСТЁТ ПОД
		//   ЕЁ ДЛИНУ» УБРАНА ВМЕСТЕ С САМИМ РАЗДУВАНИЕМ (см. PrepareTube). Она была ЕДИНСТВЕННЫМ
		//   исключением из правила fix4 «кусок не светит дальше, чем светил бы весь источник»:
		//   замер ревизии — tube1 получал R = 800 при собственных 400, то есть след ×4, и бюджет
		//   это ПРЕДСКАЗЫВАЛ, а не ЗАЖИМАЛ. Теперь длину трубки покрывают плитки встык (КРИТ-А),
		//   радиус остаётся своим, и правило у всех форм одно.
		const FAsdaFxSplit Sp = SplitGeometry(G.Shape, G.Segments, LastSplitSizeR, R, G.RadiusShare, CapG, G.Tiles);
		// У двустороннего прямоугольника плитки считаются дважды (обратные стороны стоят там же)
		const double Sides = (G.Shape == EAsdaFxLightShape::Rect && G.bTwoSided) ? 2.0 : 1.0;
		K = Sp.SumR2 * Sides / ((double)R * (double)R);
	}
	return (float)(T * T * K);
}

void UAsdaFxLightComponent::SetAdmitted(bool bIn)
{
	if (bAdmitted == bIn) { return; }
	bAdmitted = bIn;
	// Тик у не получившего место выключен полностью: 468 эффектов карты не должны каждый кадр считать центроиды.
	// ★ В НЕИГРОВОМ (редакторском) МИРЕ тик компонента не включаем ВОВСЕ: там кадр света двигает подсистема
	//   (см. ApplyFrame). Иначе при Realtime-вьюпорте кадр применился бы ДВАЖДЫ и фаза мерцания бежала бы вдвое.
	const UWorld* W = GetWorld();
	SetComponentTickEnabled(bAdmitted && W && W->IsGameWorld());
	if (!bAdmitted) { Extinguish(EAsdaFxLightGate::NotAdmitted); }
}

const TCHAR* UAsdaFxLightComponent::GetGateName() const
{
	// ★ ВОЛНА Ф3: словами, а не номером — дамп читает владелец, а не отладчик
	switch (LastGate)
	{
	case EAsdaFxLightGate::Lit:         return TEXT("ГОРИТ");
	case EAsdaFxLightGate::NoEffect:    return TEXT("эффект умер");
	case EAsdaFxLightGate::Disabled:    return TEXT("свет выключен полем");
	case EAsdaFxLightGate::NotAdmitted: return TEXT("вне бюджета");
	case EAsdaFxLightGate::FeatureOff:  return TEXT("механизм выключен");
	case EAsdaFxLightGate::NoView:      return TEXT("нет наблюдателя");
	case EAsdaFxLightGate::TooFar:      return TEXT("дальше гейта дальности");
	case EAsdaFxLightGate::OffScreen:   return TEXT("вне кадра");
	case EAsdaFxLightGate::VisLevel0:   return TEXT("гейт значимости: эффект заморожен");
	case EAsdaFxLightGate::NoRuntime:   return TEXT("эмиттер-донор стоит или не запущен");
	case EAsdaFxLightGate::NoSection:   return TEXT("у донора нет построенной геометрии");
	case EAsdaFxLightGate::AlphaZero:   return TEXT("альфа узла в нуле: эффект догорел");
	case EAsdaFxLightGate::NeverTicked: return TEXT("кадр света ни разу не считался");
	// ★ Ревизия КРИТ-4(в): подпись поля ЦИТИРУЕТСЯ ТОЧНО. Прежняя — «1. Свечение этого эмиттера» —
	//   в панели деталей не существует, и владелец искал бы её глазами по списку (AsdaFxLightSpec.h,
	//   DisplayName поля FAsdaFxEmitterGlow::Source).
	case EAsdaFxLightGate::AllEmittersOff: return TEXT("свечение снято у ВСЕХ эмиттеров (поле «1. Свечение эмиттера: откуда числа» = «Света нет»)");
	case EAsdaFxLightGate::DegenerateGeom: return TEXT("геометрия вырождена в точку (размер узла ноль) — светить нечему");
	}
	return TEXT("?");
}

void UAsdaFxLightComponent::Extinguish(EAsdaFxLightGate Gate)
{
	LastGate = Gate;
	bLitNow = false;
	// ★ РЕВИЗИЯ «форма-2» (мелкое): ЧИСЛА ПОГАСШЕГО СВЕТА НЕ ОСТАЮТСЯ ОТ ЖИВОГО. Прежде после
	//   гашения ВСЕХ эмиттеров дамп продолжал печатать «эмит 7» и полу-габарит формы от прошлого
	//   кадра — то есть описывал состояние, которого уже нет (тот же класс, что миснейм в шапке).
	LastLive = 0;
	LastShapeHalf = 0.f;
	// ★ Ф9: погасший свет начинает разгораться С НУЛЯ. Иначе кандидат, снятый бюджетом посреди хода,
	//   вернувшись, вспыхнул бы с той доли, на которой его сняли — а это ровно та вспышка, ради которой
	//   ход и написан. Отдельно от FadeTarget: цель ставит подсистема, доля — состояние самого света.
	LitRamp = 0.f;
	// ★ Ф6: погашенный свет обязан ОСВОБОДИТЬ место в бюджете теней немедленно, а не ждать своего тика —
	//   иначе догоревший костёр держал бы слот, и ближайший к игроку эффект тени не получил бы
	ApplyShadowFrame(FAsdaFxLightSpec(), /*bWant*/false, ShadowTarget.IsValid() ? ShadowTarget.Get() : this);
	// ★ ВОЛНА Ф8: дочерние источники формы гаснут вместе с нами — иначе прямоугольник продолжал бы
	//   светить после того, как эффект догорел, и «свет от ничего» стал бы штатным состоянием.
	ExtinguishShapes();
	bHaveSnap = false;
	SnappedRadius = 0.f;
	if (bAffectsWorld)
	{
		bAffectsWorld = false;
		SetVisibility(false);
		MarkRenderStateDirty();
	}
}

void UAsdaFxLightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyFrame(DeltaTime);
}
