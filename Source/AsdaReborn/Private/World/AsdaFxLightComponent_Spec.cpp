// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightComponent_Spec.cpp — ★ R2 10.11.2026 (разрез AsdaFxLightComponent.cpp, дословный перенос): спецификация света: форсированная форма, масштаб, разбиение, стоимость, ResolveGiveShape.
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

//   МЕЛК-4. ПОТОЛОК МАССИВА НЕ ДОЛЖЕН ГАСИТЬ ПОСЛЕДНИЕ КУСКИ. Массив раскладки держит
//   AsdaFxMaxSplit = 64 куска, а ПОЛЯ позволяют заказать больше: «Раскладка: источников по форме»
//   до 16 (AsdaFxLightSpec.h:535) × «Покрытие: плиток вдоль куска» до 8 = 128 у трубки. Прежде
//   укладка просто обрывалась на 64-м куске (условие `S.N < AsdaFxMaxSplit` в Emit) — последние
//   восемь капсул трубки оставались БЕЗ СВЕТА, и заметить это было нечем: ни дампа, ни строки в лог.
//   Лечим ПЕРЕСЧЁТОМ, а не обрывом: потолок плиток опускается так, чтобы КАЖДЫЙ родительский кусок
//   получил свою долю (16 × 4 = 64 вместо «16 × 8, из них видно 8 капсул»).
//   ★ У блина и кольца 8 полос × 8 плиток = 64 — впритык, и это сочетание не меняется ни на плитку.
static int32 AsdaFxTileCapForParents(int32 Parents, int32 TileMax, int32 MaxSplit)
{
	const int32 P = FMath::Max(Parents, 1);
	const int32 Room = FMath::Max(MaxSplit / P, 1);          // сколько плиток помещается на один кусок
	return FMath::Clamp(FMath::Min(TileMax, Room), 1, 8);
}

// Предупреждение о клампе печатается ОДИН раз за запуск: владельцу его видеть нужно (поле «плиток»
//   выше потолка не даёт ничего), а печатать его каждый кадр отбора нельзя — отбор идёт покадрово.
static void AsdaFxWarnTileCap(int32 Parents, int32 Asked, int32 Given, int32 MaxSplit)
{
	static bool bWarned = false;
	if (bWarned || Asked <= Given) { return; }
	bWarned = true;
	UE_LOG(LogAsdaFxLightComp, Warning,
		TEXT("[раскладка] заказано кусков %d × плиток %d = %d, потолок массива %d; плиток на кусок опущено до %d, чтобы ни один кусок не остался без света"),
		Parents, Asked, Parents * Asked, MaxSplit, Given);
}

// ★ п.2 ревизии: блин МЕЛЬЧЕ своего света кладётся одним источником полного радиуса.
//   ★ Трубки это правило НЕ касается сознательно: одиночная капсула и так стоит ровно один источник
//     полного радиуса (ShapeRadiusShare отдаёт ей 1.0), то есть выигрывать нечего, а вытянутость
//     формы была бы потеряна. Кольцо в этой волне не тронуто — довод в отчёте fix7.
//   ★★★ fix8 (С-1): правило РАЗДЕЛЕНО НА ДВЕ ЧАСТИ. Здесь — то, что не зависит от памяти
//     (применимо ли правило к этой форме вообще); само решение с гистерезисом — в
//     UAsdaFxLightComponent::DiscAsPoint, потому что у него есть защёлка.
static FORCEINLINE bool AsdaFxDiscPointRuleOn(const FAsdaFxLightSpec& Spec, EAsdaFxLightShape Sh)
{
	return Sh == EAsdaFxLightShape::Disc && AsdaFxLayoutAutoOn(Spec) && GAsdaFxShapePointRatio > 0.f;
}

// ★★★ fix8 (К-2): СКОЛЬКО ПЛИТОК ПРОСИТ САМАЯ ДЛИННАЯ ПОЛОСА при полном покрытии.
//   Это знаменатель «недобора плиток» в правиле деградации под бюджетом. Считается ТОЙ ЖЕ
//   функцией AsdaFxTilesFor, что и сама раскладка (закон port-shared-points): иначе недобор
//   мерился бы одной формулой, а платился по другой.
static FORCEINLINE int32 AsdaFxDiscTilesNeeded(float SizeR, float BaseR, float Share, float Cap, int32 TileMax)
{
	const float R0 = FMath::Max(BaseR * FMath::Clamp(Share, 0.1f, 2.f), 1.f);
	const float CapR = FMath::Max(FMath::Min(FMath::Max(Cap, 1.f), FMath::Max(FMath::Max(BaseR, 1.f), R0)), 1.f);
	float Rk = 0.f;
	return AsdaFxTilesFor(FMath::Max(SizeR, 1.f), R0, CapR, FMath::Clamp(TileMax, 1, 8), Rk);
}

// ★★★ ВОЛНА Ф8: ЗАМЕРНЫЕ КЛЮЧИ ФОРМЫ И УРОВНЯ. Отдельная функция, а не строчки в каждой ветке
//   ResolveSpec: веток возврата там три (эмиттер выключен / эмиттер сам / числа эффекта), и ключ,
//   применённый в двух из трёх, дал бы замер, в котором часть сцены осталась старой формы —
//   то есть цифру, не означающую ничего.
static FORCEINLINE void AsdaApplyForcedShapeScope(FAsdaFxLightSpec& Out)
{
	int32 FS = -1, FSc = -1, FSeg = -1;
	UAsdaFxLightSubsystem::ForcedShapeScope(FS, FSc, &FSeg);
	// ★ Ф11: раскладка ключом — у трубки и прямоугольника это «источников по форме», у блина — полосы.
	//   Оба поля ставим сразу: форму меняет соседний ключ, и разбираться, какое из полей ему подходит,
	//   пришлось бы в двух местах.
	// ★★★ fix7: ЗАМЕРНЫЙ КЛЮЧ — ЭТО ЯВНОЕ ПЕРЕОПРЕДЕЛЕНИЕ, И ОН СНИМАЕТ АВТОМАТИКУ.
	//   Без этой строки asda.Fx.Light.ForceSegments у блина перестал бы значить что-либо вовсе:
	//   в авто-режиме число полос считается из отношения, а поле DiscStrips не читается. Замер
	//   «блин при 2/3/4/6/8 полосах» показал бы одну и ту же картинку пять раз (закон
	//   false-negatives-method: ключ, который молча ничего не делает, даёт ложное «разницы нет»).
	if (FSeg >= 1)
	{
		Out.ShapeSegments = FMath::Clamp(FSeg, 1, 16);
		// ★★★ fix8 (МЕЛК-5 ревизии fix7): ЗАЖИМ БЫЛ (2, 8) — И «ForceSegments 1» У БЛИНА МОЛЧА ДАВАЛ
		//   ДВЕ ПОЛОСЫ. Это ровно та ловушка, которую fix7 чинил у кольца (пункт С-3): замерный ключ,
		//   который молча ставит не то, что попросили, даёт ложное «разницы нет»
		//   (закон false-negatives-method). Одна полоса — законная раскладка блина (GetMinLightCost
		//   отдаёт ему минимум 1), а потолок поднят до десяти вместе с правилом (пункт С-2).
		Out.DiscStrips = FMath::Clamp(FSeg, 1, AsdaFxDiscStripsMax);
		// ★★★ fix7 (пункт С-3 ревизии, НАСТОЯЩАЯ ПРИЧИНА): КОЛЬЦО ЭТИМ КЛЮЧОМ НЕ ТРОГАЛОСЬ ВОВСЕ.
		//   У кольца число дуг живёт в СВОЁМ поле RingSegments, и ключ его не писал. Поэтому кадры
		//   fix6 «кольцо 4 дуги» и «кольцо 8 дуг» вышли ОДИНАКОВЫМИ (4 источника при форме 400 юн,
		//   8 при 1200): оба прогона строили ЧЕТЫРЕ дуги из умолчания поля. Объяснение отчёта fix6
		//   («4 дуги × 2 плитки и 8 дуг × 1 плитка дают одни и те же 8 капсул») к 400 юн не подходит
		//   вовсе — там вышло 4 источника, а не 8. Замер ключом, который молча ничего не делает,
		//   даёт ложное «разницы нет» — ровно закон false-negatives-method.
		Out.RingSegments = FMath::Clamp(FSeg, 3, 8);
		Out.bShapeLayoutAuto = false;
	}
	// ★ fix5 (КРИТ-А): потолок плиток вдоль куска — тем же порядком, одним ключом на всю карту
	const int32 FTiles = UAsdaFxLightSubsystem::ForcedShapeTiles();
	if (FTiles >= 1) { Out.ShapeCoverTiles = FMath::Clamp(FTiles, 1, 8); Out.bShapeLayoutAuto = false; }
	// ★ Ф11: РАЗМЕР формы ключом — все четыре режима размера переводятся в ручной РАЗОМ. По отдельности
	//   их переводить нельзя: замер «форма 1200 при радиусе 400» обязан значить одно и то же число
	//   для трубки, прямоугольника, блина, кольца и шара, иначе формы сравнивались бы с разными длинами.
	const float FSz = UAsdaFxLightSubsystem::ForcedShapeSize();
	if (FSz > 0.f)
	{
		Out.TubeLengthMode = EAsdaFxRectSizeMode::Fixed;   Out.TubeLength = FSz;
		Out.RectSizeMode   = EAsdaFxRectSizeMode::Fixed;   Out.RectWidth = FSz;   Out.RectHeight = FMath::Max(FSz * 0.1f, 2.f);
		Out.DiscSizeMode   = EAsdaFxRectSizeMode::Fixed;   Out.DiscRadius = 0.5f * FSz;
		Out.RingSizeMode   = EAsdaFxRectSizeMode::Fixed;   Out.RingRadiusUU = 0.5f * FSz;
		Out.SphereSizeMode = EAsdaFxRectSizeMode::Fixed;   Out.SphereRadius = 0.5f * FSz;
	}
	// ★ Ф11: доворот формы ключом — складывается с доворотом поля, как и любой другой замерный ключ
	const float FPitch = UAsdaFxLightSubsystem::ForcedShapePitch();
	if (!FMath::IsNearlyZero(FPitch)) { Out.ShapeRotation.Pitch += FPitch; }
	// ★ Ф10: верхняя граница 5 — дописаны трубка (3), кольцо (4) и авто (5); зажим 0..2 молча превращал бы их в конус
	// ★ Ф11: граница поднята до 7 — дописаны блин (6) и шар (7). Число здесь ОБЯЗАНО расти вместе с
	//   перечислением: зажим по старой границе молча подменял бы новую форму прежней, и замер «блин
	//   против точки» показал бы точку против точки (закон false-negatives-method)
	if (FS >= 0)  { Out.Shape = (EAsdaFxLightShape)(uint8)FMath::Clamp(FS, 0, 7); }
	if (FSc >= 0) { Out.Scope = (EAsdaFxLightScope)(uint8)FMath::Clamp(FSc, 0, 1); }
}

// ★ fix13: имя победившей ветки разрешения чисел — одно на лог, панель и дамп
const TCHAR* UAsdaFxLightComponent::SpecSourceName(EAsdaFxSpecSource S)
{
	switch (S)
	{
	case EAsdaFxSpecSource::EmitterOff:    return TEXT("эмиттер:выкл");
	case EAsdaFxSpecSource::EmitterManual: return TEXT("эмиттер:вручную");
	case EAsdaFxSpecSource::GlowOverride:  return TEXT("конфигуратор");
	case EAsdaFxSpecSource::AssetManual:   return TEXT("ассет:замок");
	case EAsdaFxSpecSource::Family:        return TEXT("семейство");
	case EAsdaFxSpecSource::AssetField:    return TEXT("ассет:поля");
	default:                               return TEXT("эффекта нет");
	}
}

FAsdaFxLightSpec UAsdaFxLightComponent::ResolveSpec(const AAsdaEffectActor* Fx, const FAsdaFxLightRule* Rule, int32 EmitterIdx,
	EAsdaFxSpecSource* OutSource)
{
	// ★ fix13: ветка по умолчанию — «эффекта нет»; каждая ветка ниже называет себя сама
	auto Say = [OutSource](EAsdaFxSpecSource S) { if (OutSource) { *OutSource = S; } };
	Say(EAsdaFxSpecSource::NoEffect);
	// ★★★ ВОЛНА Ф8 (10.09.2026): ЦЕПОЧКА НАСЛЕДОВАНИЯ «ЭМИТТЕР → ЭФФЕКТ».
	//   Эмиттер, переключённый в «задано вручную», отвечает за свои числа ЦЕЛИКОМ — и это
	//   осознанный выбор против «переопределить отдельные поля»: частичное переопределение
	//   потребовало бы на КАЖДОЕ из 40 полей отдельной галочки «а это моё», то есть удвоило бы
	//   панель ради случая, которого владелец не просил. Числа эффекта в момент переключения
	//   ПОДХВАТЫВАЮТСЯ (AAsdaEffectActor::PostEditChangeProperty), поэтому «своё целиком»
	//   начинается ровно с того, что было унаследовано, и разъезжается только там, где правили.
	if (Fx && Fx->Emitters.IsValidIndex(EmitterIdx))
	{
		const FAsdaFxEmitterGlow& G = Fx->Emitters[EmitterIdx].Glow;
		if (G.Source == EAsdaFxEmitterGlowSource::Off)
		{
			// «Света у этого эмиттера нет» — возвращаем ВЫКЛЮЧЕННЫЙ набор, а не «как у эффекта»:
			// гейт `bEnabled` в ApplyFrame и в подсистеме уже умеет его читать, второй развилки не нужно.
			FAsdaFxLightSpec Off = G.Spec;
			Off.bEnabled = false;
			AsdaApplyForcedShapeScope(Off);
			Say(EAsdaFxSpecSource::EmitterOff);
			return Off;
		}
		if (G.Source == EAsdaFxEmitterGlowSource::Manual)
		{
			FAsdaFxLightSpec Own = G.Spec;
			int32 FC0 = -1, FR0 = -1, FP0 = -1;
			UAsdaFxLightSubsystem::ForcedModes(FC0, FR0, FP0);
			if (FC0 >= 0) { Own.ColorMode = (EAsdaFxLightColorMode)(uint8)FMath::Clamp(FC0, 0, 3); }
			if (FR0 >= 0) { Own.RadiusMode = (EAsdaFxLightRadiusMode)(uint8)FMath::Clamp(FR0, 0, 2); }
			if (FP0 >= 0) { Own.Position = (EAsdaFxLightPosMode)(uint8)FMath::Clamp(FP0, 0, 3); }
			AsdaApplyForcedShapeScope(Own);
			Say(EAsdaFxSpecSource::EmitterManual);
			return Own;
		}
		// FromEffect — падаем ниже, в общую ветку «числа эффекта»
	}
	// ★ ЕДИНОЕ ПРАВИЛО РАЗРЕШЕНИЯ ЧИСЕЛ (одно на всю систему: заведение, тик, отбор, дамп):
	//   1. владелец правил эффект руками (FxLight.bManual) → его числа, и точка. Дефолты семейства
	//      такой эффект больше не касаются — ни в редакторе, ни в игре;
	//   2. правило семейства найдено → его дефолты. Это норма для 1274 ассетов BP_Fx_*, куда те же самые
	//      числа проставлены генератором: рантайм и ассет совпадают, и расхождение невозможно по построению;
	//   3. правила нет (таблица не дотянулась) → числа с эффекта как есть. Хуже дефолта не будет:
	//      в CDO структуры лежат безопасные значения (7.5 кд, радиус 150…800).
	FAsdaFxLightSpec Out;
	// ★ Ф10: ДЕФОЛТНЫЕ числа (семейство / необработанное поле ассета) переводятся в юниты мира;
	//   РУЧНЫЕ — нет: владелец правит их уже в юнитах этого мира (шапка FAsdaFxLightWorldScale).
	//   ★ 12.09.2026: единственным нестандартным миром был логин (×100) — он переведён в ×1, и
	//   множитель у него теперь 1. Механизм оставлен на случай новых миров.
	bool bDefaults = false;
	// ★★★ 13.09.2026 (конфигуратор свечения): ПЕРЕОПРЕДЕЛЕНИЕ «ПРЕДМЕТ → ЭФФЕКТ» СТОИТ ВЫШЕ ЗАМКА АССЕТА.
	//   Запись заводит владелец в конфигураторе явно и именно для этого эффекта/предмета — это самая
	//   свежая и самая узкая настройка, и «поправил, а ничего не поменялось из-за старого замка ассета»
	//   было бы худшим исходом. Запись без bLight (правка одной только силы свечения) свет не трогает.
	//   Числа записи сведены генератором из семейства — поэтому масштаб мира к ним применяется как к дефолтам.
	const FAsdaFxGlowOverride* Ov = Fx ? Fx->FindGlowOverride() : nullptr;
	if (!Fx) { Out = Rule ? Rule->Spec : FAsdaFxLightSpec(); }
	else if (Ov && Ov->bLight) { Out = Ov->Spec; bDefaults = true; Say(EAsdaFxSpecSource::GlowOverride); }
	else if (Fx->FxLight.bManual) { Out = Fx->FxLight; Say(EAsdaFxSpecSource::AssetManual); }
	else if (Rule)
	{
		Out = Rule->Spec;
		Out.FamilyId = Rule->Id;      // в дампе и в панели деталей видно, откуда числа
		bDefaults = true;
		Say(EAsdaFxSpecSource::Family);
	}
	else { Out = Fx->FxLight; bDefaults = true; Say(EAsdaFxSpecSource::AssetField); }
	if (bDefaults) { ScaleSpecToWorld(Out, UAsdaFxLightSubsystem::WorldLengthScale(Fx->GetWorld())); }

	// ★ Ф4, ПОСЛЕДНИМ ШАГОМ: отладочный перевод в заданные режимы (asda.Fx.Light.Force*Mode).
	//   Именно ПОСЛЕ разрешения чисел, а не вместо него: ключ меняет ТОЛЬКО «откуда берётся величина»,
	//   а сами числа (кандела, зажимы, насыщенность) остаются теми, что у эффекта. Иначе сравнение
	//   «Ф2 против Ф4» мерило бы заодно и разницу яркостей, и вывод был бы ни о чём.
	int32 FC = -1, FR = -1, FP = -1;
	UAsdaFxLightSubsystem::ForcedModes(FC, FR, FP);
	if (FC >= 0) { Out.ColorMode = (EAsdaFxLightColorMode)(uint8)FMath::Clamp(FC, 0, 3); }
	if (FR >= 0) { Out.RadiusMode = (EAsdaFxLightRadiusMode)(uint8)FMath::Clamp(FR, 0, 2); }
	// ★ Ф7: верхняя граница 3 — добавился режим BoundsBox («центр габаритов»). Зажим 0..2 молча
	//   превращал бы «3» в «2», и ключ отладки врал бы про режим (тот же класс, что миснейм в дампе).
	if (FP >= 0) { Out.Position = (EAsdaFxLightPosMode)(uint8)FMath::Clamp(FP, 0, 3); }
	AsdaApplyForcedShapeScope(Out);
	return Out;
}

FAsdaFxLightSpec UAsdaFxLightComponent::ResolveSpec() const
{
	return ResolveSpec(Effect.Get(), GetRule(), EmitterIndex);
}

void UAsdaFxLightComponent::ScaleSpecToWorld(FAsdaFxLightSpec& S, float K)
{
	if (!(K > 0.f) || FMath::IsNearlyEqual(K, 1.f)) { return; }
	// СИЛА — квадратом: E = I/d², при d → K·d та же освещённость требует K²·I
	S.IntensityCd *= K * K;
	// ДЛИНЫ — первой степенью. Множители геометрии (RadiusScale, RadiusBoundsScale, RectSizeScale,
	//   TubeLengthScale, доли кольца) НЕ трогаем: они умножают габарит эффекта, который в мире ×K уже ×K.
	S.RadiusMin *= K;          S.RadiusMax *= K;
	S.MaxDrawDistance *= K;    S.FadeRange *= K;
	S.SourceRadius *= K;       S.SoftSourceRadius *= K;
	S.ShadowMaxDistance *= K;  S.ContactShadowLength *= K;
	S.Offset *= K;
	S.RectWidth *= K;          S.RectHeight *= K;          S.BarnDoorLength *= K;
	// ★ Ф11: ручные размеры и локальное смещение — такие же длины, что и всё выше. Доли и множители
	//   (DiscSizeScale, SphereSizeScale, DiscStripRadiusScale, ShapeSegmentRadiusScale) не трогаем —
	//   они умножают габарит, который в мире ×K уже ×K; углы доворота масштаба не имеют вовсе.
	S.OffsetLocal *= K;
	S.DiscRadius *= K;         S.TubeLength *= K;          S.RingRadiusUU *= K;
	S.SphereRadius *= K;
}

// ★★★ РЕВИЗИЯ fix5 (КРИТ-А): СКОЛЬКО ИСТОЧНИКОВ ДВИЖКА ЗАЙМЁТ РАСКЛАДКА — БЕЗ ПОСТРОЕНИЯ САМОЙ
//   РАСКЛАДКИ. Отбор (UAsdaFxLightSubsystem::Select) идёт КАЖДЫЙ КАДР (SelectIntervalSec = 0) и
//   спрашивает цену по многу раз, а полная FAsdaFxSplit — килобайт на стеке. Считаем то же самое
//   число теми же формулами, но без массивов. Расхождение с SplitGeometry невозможно: обе ветки
//   пользуются одним правилом «наименьшее m, при котором полу-длина ≤ 0.66 · радиус плитки»
//   (закон port-shared-points — правило одно, применений два, и второе обязано быть буквальным).
int32 UAsdaFxLightComponent::SplitSourceCount(
	EAsdaFxLightShape Sh, int32 N, float SizeR, float BaseR, float Share, float Cap, int32 TileMax)
{
	const int32 Parents = FMath::Clamp(N, 1, (int32)AsdaFxMaxSplit);
	if (Sh == EAsdaFxLightShape::Rect) { return Parents; }                  // у плоскости плиток вдоль плитки нет
	if (Sh != EAsdaFxLightShape::Disc && Sh != EAsdaFxLightShape::Ring && Sh != EAsdaFxLightShape::Tube) { return 1; }
	if (!(SizeR > 0.f)) { return Parents; }                                 // геометрия ещё не измерена
	const float R0 = FMath::Max(BaseR * FMath::Clamp(Share, 0.1f, 2.f), 1.f);
	const float CapR = FMath::Max(FMath::Min(FMath::Max(Cap, 1.f), FMath::Max(FMath::Max(BaseR, 1.f), R0)), 1.f);
	// ★ fix6 (МЕЛК-4): потолок плиток опускается под вместимость массива ТЕМ ЖЕ расчётом, что в
	//   SplitGeometry, — иначе счёт цены обещал бы источников больше, чем раскладка кладёт.
	const int32 ParentsForCap = (Sh == EAsdaFxLightShape::Ring) ? FMath::Max(Parents, 3) : Parents;
	const int32 TileCap = AsdaFxTileCapForParents(ParentsForCap, TileMax, (int32)AsdaFxMaxSplit);
	auto Tiles = [&](float HalfChord) -> int32
	{
		float Rk = 0.f;                                   // радиус здесь не нужен — нужно только число
		return AsdaFxTilesFor(HalfChord, R0, CapR, TileCap, Rk);
	};
	int32 Total = 0;
	if (Sh == EAsdaFxLightShape::Disc)
	{
		const float DR = FMath::Max(SizeR, 1.f);
		for (int32 k = 0; k < Parents; ++k)
		{
			const float b = DR * (-1.f + (2.f * (float)k + 1.f) / (float)Parents);
			Total += Tiles(FMath::Sqrt(FMath::Max(DR * DR - b * b, 0.f)));
		}
	}
	else if (Sh == EAsdaFxLightShape::Ring)
	{
		const int32 NN = FMath::Max(Parents, 3);
		Total = NN * Tiles(FMath::Max(SizeR, 1.f) * FMath::Sin(PI / (float)NN));
	}
	else   // Tube
	{
		Total = Parents * Tiles(FMath::Max(FMath::Max(SizeR, 0.5f) / (float)Parents, 0.5f));
	}
	return FMath::Clamp(Total, 1, (int32)AsdaFxMaxSplit);
}

// ★ fix5 (СРЕДН-Д): прищёлкнуть размер формы к сетке с тем же гистерезисом, что у позиции и радиуса.
//   Узел пересчитывается, только когда размер ушёл дальше шага, — иначе на границе ячейки снап щёлкал
//   бы туда-сюда и дрожание вернулось бы в том же виде. Slot — своя ячейка на каждую величину.
float UAsdaFxLightComponent::SnapShapeSize(float V, int32 Slot)
{
	if (Slot < 0 || Slot >= AsdaFxShapeSizeSlots) { return V; }
	const float Step = GAsdaFxShapeSizeSnap;
	if (!(Step > 0.f)) { bHaveShapeSizeSnap[Slot] = false; return V; }
	if (!bHaveShapeSizeSnap[Slot] || FMath::Abs(V - SnappedShapeSize[Slot]) > Step)
	{
		SnappedShapeSize[Slot] = FMath::Max(AsdaSnap(V, Step), Step);
		bHaveShapeSizeSnap[Slot] = true;
	}
	return SnappedShapeSize[Slot];
}

// ★ fix5: доля радиуса куска у формы — одно место на все ветки (кадр, бюджет, счёт цены)
float UAsdaFxLightComponent::ShapeRadiusShare(const FAsdaFxLightSpec& Spec, EAsdaFxLightShape Sh)
{
	switch (Sh)
	{
	case EAsdaFxLightShape::Ring: return FMath::Clamp(Spec.RingRadiusScale, 0.1f, 2.f);
	case EAsdaFxLightShape::Disc: return FMath::Clamp(Spec.DiscStripRadiusScale, 0.1f, 2.f);
	case EAsdaFxLightShape::Tube: return (FMath::Clamp(Spec.ShapeSegments, 1, 16) > 1)
		? FMath::Clamp(Spec.ShapeSegmentRadiusScale, 0.1f, 2.f) : 1.f;
	default: return 1.f;
	}
}

// ★ fix5: геометрия для ПРЕДСКАЗАНИЯ раскладки — размер формы и радиус ПОСЛЕДНЕГО кадра плюс
//   уровневый потолок. Ровно те же три числа кладёт в SplitGeometry кадр света; пока кадра не было,
//   размер 0 — и раскладка считается по числу кусков из полей (грубая оценка на один кадр).
// ★★★ fix8 (пункт С-6 ревизии fix7): ОДНА ФОРМУЛА БАЗОВОГО РАДИУСА НА ОБА БЮДЖЕТА.
//   ЧТО БЫЛО НЕ ТАК. ShapePredictGeom (его читает счёт МЕСТ через GetLightCost) брал
//   `LastRadius > 0 ? LastRadius : Spec.RadiusMax` БЕЗ условия «горит сейчас», а EstimateScreenArea
//   (ЭКРАННЫЙ бюджет) — `bLitNow && LastRadius > 0 ? LastRadius : Spec.RadiusMax`. У кандидата,
//   который погас, но помнит радиус прошлого кадра, это ДВА РАЗНЫХ BaseR. До fix7 от BaseR зависели
//   только радиусы кусков, а с fix7 от него зависит ЧИСЛО ПОЛОС — значит счёт мест и экранный
//   бюджет считали РАЗНЫЕ раскладки одного кандидата (закон port-shared-points).
//   Берётся ПЕССИМИСТИЧНЫЙ вариант экранного бюджета: у не горящего — потолок его чисел, чтобы
//   бюджет не пробивался «залпом» при зажигании.
float UAsdaFxLightComponent::ShapeBaseRadius(const FAsdaFxLightSpec& Spec, float Cap) const
{
	return FMath::Min(FMath::Max((bLitNow && LastRadius > 0.f) ? LastRadius : Spec.RadiusMax, 1.f), FMath::Max(Cap, 1.f));
}

void UAsdaFxLightComponent::ShapePredictGeom(const FAsdaFxLightSpec& Spec, float& OutSizeR, float& OutBaseR, float& OutCap) const
{
	OutCap = FMath::Max(UAsdaFxLightSubsystem::ResolveRadiusCap() * UAsdaFxLightSubsystem::WorldLengthScale(GetWorld()), 1.f);
	OutBaseR = ShapeBaseRadius(Spec, OutCap);
	OutSizeR = LastSplitSizeR;
}

// ★★★ fix8 (пункт С-1 ревизии fix7): ПОРОГ «БЛИН МЕЛЬЧЕ СВОЕГО СВЕТА» С ГИСТЕРЕЗИСОМ.
//   ЧТО БЫЛО НЕ ТАК. Порог был голым сравнением: отношение чуть ниже — одна полоса полного радиуса,
//   чуть выше — три полосы доли 0.7. У живого эффекта радиус света ДЫШИТ, и кандидат, стоящий на
//   самой границе, менял раскладку покадрово. Замер разрыва — у объявления ключа LayoutRampR.
//   ★ ЧЕСТНО: гистерезис лечит ДРЕБЕЗГ у границы, но не сам разрыв — растущий всплеск границу
//     перейдёт обязательно. Величину разрыва уменьшает ВТОРОЕ средство, разбег доли (AsdaFxDiscShareFor).
// ★★★ ВОЛНА fix9 (пункты С-2 и КРИТ-1 ревизии fix8, 20.09.2026). ДВЕ ПРАВКИ, ДОВОД — У ОБЪЯВЛЕНИЯ:
//   1. Полоса гистерезиса лежит ПОД порогом (верхняя кромка равна порогу), а не вокруг него: иначе
//      одиночная полоса держалась до отношения порог·(1+полоса) = 1.035, где она даёт 3.19 % тусклой
//      площади, и перекалибровка порога 1.0 → 0.9 не давала ничего.
//   2. Защёлку пишет ТОЛЬКО кадр света (bCommit): у бюджета своя, более старая геометрия, и его
//      запись стирала память, ради которой гистерезис и заводился.
bool UAsdaFxLightComponent::DiscAsPoint(const FAsdaFxLightSpec& Spec, EAsdaFxLightShape Sh, float SizeR, float BaseR,
	bool bCommit) const
{
	if (!AsdaFxDiscPointRuleOn(Spec, Sh)) { if (bCommit) { bDiscPointLatch = false; } return false; }
	const float Ratio = AsdaFxShapeToLightRatio(SizeR, BaseR);
	if (!(Ratio > 0.f)) { return bDiscPointLatch; }      // геометрии ещё нет — держим прежнее решение
	// Полуширина полосы задаётся ключом как доля порога; полная ширина полосы — вдвое больше, и она
	//   отложена ВНИЗ от порога. Зажим 0.49 держит нижнюю кромку положительной при любом ключе.
	const float H = FMath::Clamp(GAsdaFxShapePointHyst, 0.f, 0.49f);
	const float Up = GAsdaFxShapePointRatio;                       // выше порога одной полосы НЕ БЫВАЕТ
	const float Down = GAsdaFxShapePointRatio * (1.f - 2.f * H);   // вход в одиночную полосу
	bool bOut = bDiscPointLatch;
	if (Ratio <= Down) { bOut = true; }
	else if (Ratio > Up) { bOut = false; }
	// между Down и Up решение НЕ меняется — в этом и весь гистерезис
	if (bCommit) { bDiscPointLatch = bOut; }
	return bOut;
}

int32 UAsdaFxLightComponent::GetLightCost(const FAsdaFxLightSpec& Spec) const
{
	EAsdaFxLightShape Sh = Spec.Shape;
	if (Sh == EAsdaFxLightShape::Auto) { Sh = bAutoHave ? AutoShape : EAsdaFxLightShape::Point; }
	// ★★★ РЕВИЗИЯ fix5 (КРИТ-А): ПОЛНАЯ ЦЕНА ФОРМЫ — ЭТО ПОЛОСЫ ×  ПЛИТКИ, А НЕ ОДНИ ПОЛОСЫ.
	//   Иначе бюджет выдавал бы кандидату ровно столько мест, сколько у него полос, и решётка
	//   никогда не собиралась бы: покрытие осталось бы прежним (44 % диаметра), а поле «плиток
	//   вдоль куска» молча не работало бы. Число плиток зависит от ГЕОМЕТРИИ (хорда против радиуса),
	//   поэтому спрашивается у той же формулы, которой его посчитает кадр света.
	if (Sh == EAsdaFxLightShape::Ring || Sh == EAsdaFxLightShape::Disc || Sh == EAsdaFxLightShape::Tube)
	{
		// ★★★ fix7: ГЕОМЕТРИЯ СПРАШИВАЕТСЯ ПЕРВОЙ. От отношения «диаметр формы ÷ радиус света»
		//   зависят теперь и число полос, и вырождение блина в один источник, — значит, узнать
		//   размер надо ДО того, как назван их счёт. Цена и кадр по-прежнему считаются одними и
		//   теми же функциями (DiscAsPoint / AsdaFxDiscStripsFor / AsdaFxTileMaxFor зовёт и
		//   ResolveGiveShape), иначе бюджет списывал бы не то, что горит (закон port-shared-points).
		// ★ fix9 (КРИТ-1): ЗДЕСЬ — ПРЕДСКАЗАНИЕ, И ТОЛЬКО ОНО. Счёт цены идёт ДО кадра (его зовёт
		//   отбор подсистемы), свежей геометрии в этот момент не существует ни у кого. Защёлку порога
		//   этот вызов НЕ пишет (bCommit = false): её хозяин — кадр света, у которого числа свежие.
		float SizeR = 0.f, BaseR = 0.f, CapV = 0.f;
		ShapePredictGeom(Spec, SizeR, BaseR, CapV);
		if (DiscAsPoint(Spec, Sh, SizeR, BaseR, /*bCommit=*/false)) { return 1; }
		const int32 Parents = (Sh == EAsdaFxLightShape::Ring) ? FMath::Clamp(Spec.RingSegments, 3, 8)
			: (Sh == EAsdaFxLightShape::Disc) ? AsdaFxDiscStripsFor(Spec, SizeR, BaseR)
			: FMath::Clamp(Spec.ShapeSegments, 1, 16);
		// ★ fix8 (С-1): доля радиуса у блина в авто-режиме — с разбегом над порогом, и счёт цены
		//   обязан спрашивать ЕЁ ЖЕ: иначе плиток насчиталось бы по одной доле, а построилось по другой.
		const float ShareC = (Sh == EAsdaFxLightShape::Disc) ? AsdaFxDiscShareFor(Spec, SizeR, BaseR)
			: ShapeRadiusShare(Spec, Sh);
		const int32 Predicted = SplitSourceCount(Sh, Parents, SizeR, BaseR, ShareC, CapV,
			AsdaFxTileMaxFor(Spec, Sh, SizeR, BaseR));
		// ═══ ★★★ ВОЛНА fix11 (пункт 2 ревизии fix10, 20.09.2026): ЗАПРОС НЕ МЕНЬШЕ ТОГО, ЧТО КАДР
		//     УЖЕ НАСЧИТАЛ СЕБЕ САМ ═════════════════════════════════════════════════════════════
		//
		//   ЧТО БЫЛО НЕ ТАК. Предсказание выше считается по размеру формы и радиусу ПРОШЛОГО кадра
		//   (ShapePredictGeom), а раскладку кадр строит по радиусу ТЕКУЩЕГО. У живого эффекта радиус
		//   дышит, и два числа расходятся — кандидат ПРОСИТ меньше, чем стоит его же полная раскладка,
		//   а бюджет больше запрошенного не даёт ни при каком остатке. Замер: f10_kadr5x.log, тик 698,
		//   бюджет количества 512 и экранный 1.84 из 4096 — «правило просит 6 кусков, построено 5,
		//   источников 13, мест выдано 13, полная раскладка стоит 18». Резать было НЕЧЕГО: бюджет
		//   свободен, а блин всё равно вышел на полосу меньше правила. То есть гарантия «при свободном
		//   бюджете построено ровно то, что просит правило» держалась только при неподвижном радиусе.
		//
		//   ЧТО ДЕЛАЕТСЯ. Кадр света уже посчитал СВОИМИ, свежими числами, во сколько мест обходится
		//   его полная раскладка (LastRuleFullCost, заведено волной fix10 ради метки в дампе). Здесь
		//   оно берётся как НИЖНЯЯ ГРАНИЦА запроса. Тот же приём пессимизма, что у ShapeBaseRadius:
		//   бюджет вправе ошибиться в сторону «попросил больше», но не в сторону «сам себя урезал».
		//   Сходимость — за один кадр: кадр N меряет 18, отбор кадра N+1 просит 18, кадр N+1 строит
		//   шесть полос. Храповика тут нет: LastRuleFullCost перезаписывается КАЖДЫМ кадром, поэтому
		//   у сжимающейся формы граница едет вниз вместе с ней.
		//   ★ Число берётся только у ГОРЯЩЕГО кандидата и только для ТОЙ ЖЕ заявленной формы
		//     (LastRuleShape): у погашенного оно протухшее, а у сменившего форму — про чужую раскладку.
		//   ★ Вырожденный в точку блин сюда не доходит (ветка DiscAsPoint выше), поэтому подменить
		//     единицу пятьюдесятью шестью это не может.
		const bool bFrameKnows = (GAsdaFxAskFullCost != 0) && bLitNow && LastRuleFullCost > 0 && LastRuleShape == Sh;
		return bFrameKnows ? FMath::Max(Predicted, LastRuleFullCost) : Predicted;
	}
	// ★ Ф11: раскладка прямоугольника на N источников. Дефолт 1 ⇒ прежние числа не меняются
	const int32 Seg = FMath::Clamp(Spec.ShapeSegments, 1, 16);
	// ★ Авто-прямоугольник всегда двусторонний (см. ApplyFrame) — в бюджете он тоже за два
	if (Sh == EAsdaFxLightShape::Rect) { return Seg * ((Spec.bRectTwoSided || Spec.Shape == EAsdaFxLightShape::Auto) ? 2 : 1); }
	return 1;   // точка, шар, конус — один источник движка
}

int32 UAsdaFxLightComponent::GetMinLightCost(const FAsdaFxLightSpec& Spec) const
{
	EAsdaFxLightShape Sh = Spec.Shape;
	if (Sh == EAsdaFxLightShape::Auto) { Sh = bAutoHave ? AutoShape : EAsdaFxLightShape::Point; }
	// ★★★ РЕВИЗИЯ fix3 (КРИТ-2): НИЖЕ ЭТОГО ЧИСЛА ФОРМА ПЕРЕСТАЁТ БЫТЬ СОБОЙ.
	//   Кольцо: обод собирается из хорд вписанного N-угольника, и при N < 3 «хорды» вырождаются в
	//     диаметры — обода не получается ни при каком радиусе дуги. Минимум честный: три.
	//   Блин: полосы-хорды кладутся поперёк диска; при ОДНОЙ полосе она проходит ЧЕРЕЗ ЦЕНТР и имеет
	//     длину полного диаметра (b₀ = 0, см. ApplyDiscFrame). Это уже не «шарик в середине», а
	//     плоская светящаяся черта во всю форму — то есть блин грубее, но всё ещё блин. Минимум 1.
	//   Трубка: одна капсула с длиной — законная форма (так она и жила до раскладки). Минимум 1.
	//   Прямоугольник: одна плитка; двусторонность — плата за вторую полусферу, и при нехватке мест
	//     обратная сторона гасится, а сила возвращается к исходной (см. ResolveGiveShape). Минимум 1.
	if (Sh == EAsdaFxLightShape::Ring) { return FMath::Min(FMath::Clamp(Spec.RingSegments, 3, 8), 3); }
	return 1;
}

UAsdaFxLightComponent::FAsdaFxSplit UAsdaFxLightComponent::SplitGeometry(
	EAsdaFxLightShape Sh, int32 N, float SizeR, float BaseR, float Share, float Cap, int32 TileMax)
{
	// ★★★ РЕВИЗИЯ fix3 (КРИТ-1, 20.09.2026): ЕДИНСТВЕННОЕ МЕСТО, ГДЕ СЧИТАЮТСЯ РАЗМЕРЫ КУСКОВ.
	//   Довод — в шапке объявления. Правило согласования длины и радиуса тут одно на все формы
	//   (ASDA_FX_SPLIT_HALFLEN_SHARE, ревизия М-2): полу-длина куска ≤ ДОЛЯ · радиус куска.
	FAsdaFxSplit S;
	S.N = FMath::Clamp(N, 1, (int32)AsdaFxMaxSplit);
	const float R0 = FMath::Max(BaseR * FMath::Clamp(Share, 0.1f, 2.f), 1.f);
	// ★★★ РЕВИЗИЯ fix4 (КРИТ-2, 20.09.2026): КУСОК НЕ СВЕТИТ ДАЛЬШЕ, ЧЕМ СВЕТИЛ БЫ ВЕСЬ ИСТОЧНИК.
	//   ЧТО БЫЛО НЕ ТАК. Правило «полу-длина ≤ 0.66 · радиус» тянуло радиус куска ВВЕРХ под его
	//   хорду, а верхней границей стоял УРОВНЕВЫЙ потолок радиуса (Light.FxRadiusCap, 1000…3000 юн),
	//   то есть величина, к самому источнику отношения не имеющая. У света радиусом 400 юн и формы
	//   диаметром 1200 полоса получала радиус 800…909 — след ОДНОЙ такой полосы (R/d)² выходил
	//   вчетверо больше следа всей точки, а у четырёх полос — в шестнадцать раз. Отсюда КРИТ-2:
	//   при рабочем экранном бюджете (12…32) первый же проход раздачи «каждому по одному источнику»
	//   не вмещал почти никого, и включение блина ГАСИЛО эмиттеры (замер ревизии f3_rb2.log:
	//   бюджет 32 — горит 6 из 45, бюджет 12 — 2 из 45).
	//   ПОЧЕМУ ЛЕЧИМ ИМЕННО ТАК. Радиус затухания — это ДИСТАНЦИЯ ОТСЕЧЕНИЯ источника, свойство
	//   эффекта (его чисел и габаритов), а не раскладки. Раскладка вправе поделить длину и силу
	//   между кусками, но не вправе сделать так, чтобы свет доставал ДАЛЬШЕ, чем задумано: это
	//   меняет картинку сцены и цену, а владелец менял только форму. Поэтому потолок куска —
	//   собственный радиус источника (или прямо заданная владельцем доля, если он просил БОЛЬШЕ
	//   через множитель радиуса куска), и уровневый потолок остаётся внешней границей сверху.
	//   СЛЕДСТВИЕ 1: след раскладки ≤ N · след точки, то есть экранный бюджет списывает ровно во
	//   столько раз больше, во сколько раз больше источников списывает бюджет КОЛИЧЕСТВА — два
	//   бюджета перестают противоречить друг другу.
	//   СЛЕДСТВИЕ 2: предсказание цены совпадает с измерением у НЕРАЗЛОЖЕННОЙ плоскости (она горит
	//   одним источником полного радиуса, а SplitGeometry раньше предсказывала ей растянутый).
	//   ЦЕНА РЕШЕНИЯ, честно: у формы КРУПНЕЕ собственного радиуса кусок больше не дотягивается до
	//   своей хорды — длина режется (ниже, тем же правилом 0.66), и такая форма светит не всей
	//   плоскостью, а полосой по середине. Это неизбежно: источник с радиусом 400 юн физически не
	//   достаёт до края формы в 1200 юн ни при какой раскладке; раньше «доставал» только потому,
	//   что молча увеличивал себе дальность впятеро дороже.
	const float CapR = FMath::Max(FMath::Min(FMath::Max(Cap, 1.f), FMath::Max(FMath::Max(BaseR, 1.f), R0)), 1.f);
	auto Fit = [&](float HalfLen) -> float { return FMath::Min(FMath::Max(R0, HalfLen / ASDA_FX_SPLIT_HALFLEN_SHARE), CapR); };

	// ═══ ★★★ РЕВИЗИЯ fix5 (КРИТ-А, 20.09.2026): ФОРМА СВЕТИТ ВСЕЙ ПЛОСКОСТЬЮ ПРИ ЛЮБОМ РАЗМЕРЕ ═══
	//
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ fix4. Потолок радиуса куска (собственный радиус света) — правильный,
	//   но вместе с правилом «полу-длина ≤ 0.66 · радиус» он резал ДЛИНУ: у блина радиусом 600 юн при
	//   свете 400 все полосы получали R = 400 и длину 528 при настоящих хордах 794 и 1162 — покрытие
	//   диаметра 1200 выходило 44 %, кольцо из трёх дуг давало дугу 528 вместо 1039. Правило звучало
	//   как «форма крупнее 0.66 · радиуса света ⇒ светит полосой по середине» и прямо противоречило
	//   задаче владельца («блин должен светиться по ВСЕЙ плоскости»).
	//
	//   КАК ЛЕЧИМ. Длину кладём НЕ ОДНОЙ капсулой, а НЕСКОЛЬКИМИ ПЛИТКАМИ ВСТЫК: полоса делится на m
	//   равных частей, каждая укладывается в свою сферу затухания сама, стыков без света нет по
	//   построению. m — НАИМЕНЬШЕЕ, при котором плитка укладывается; когда одной хватает (а хватает
	//   ей всегда, пока форма мельче радиуса света), числа совпадают с fix4 ЮНИТ В ЮНИТ.
	//
	//   ★ ПОЧЕМУ ЭТО НЕ ТА «РЕШЁТКА», КОТОРУЮ ОТВЕРГЛИ В fix2. Там на решётку раскладывали ТЕ ЖЕ N
	//     источников (4 полосы → сетка 2×2): центры разъезжались к краям диска, и СЕРЕДИНА ГАСЛА
	//     (замер той ревизии: внутреннее кольцо 0.0 против 7.7 у полос). Здесь число полос не
	//     меняется вовсе — дробится ДЛИНА КАЖДОЙ, то есть источников становится БОЛЬШЕ, а не иначе
	//     расставленных. Середина при этом остаётся накрытой той же полосой, что и была.
	//   ★ СИЛА КАЖДОЙ ПЛИТКИ ПРОПОРЦИОНАЛЬНА ЕЁ ДЛИНЕ (делит её вызывающий, по S.Len): плитки одной
	//     полосы равны между собой, а полосы разной длины получают разную силу — яркость на единицу
	//     длины одинакова у всех по построению. Ровно это и гасило середину в fix2, где силу делили
	//     поровну между кусками разной длины.
	//   ★ ЦЕНА. Радиус плитки падает вместе с её длиной, поэтому Σ R² растёт МЕДЛЕННЕЕ числа плиток:
	//     аналитический замер (an_model.py, уровень владельца) для блина 600 юн при свете 400 и
	//     четырёх полосах — 4 источника и Σ R² = 0.64 млн у fix4 против 10 источников и 0.88 млн у
	//     решётки, то есть экранная цена +37 % при покрытии 44 % → 96.8 %.
	// ★ fix6 (МЕЛК-4): потолок плиток опускается под вместимость массива ДО укладки, а не обрывом
	//   в Emit. Число родительских кусков здесь уже известно: это S.N, каким он пришёл (см. Want).
	//   У кольца дуг не меньше трёх — то же поднятие, что ниже в ветке Ring, иначе потолок считался
	//   бы по заниженному числу кусков.
	const int32 ParentsAsked = (Sh == EAsdaFxLightShape::Ring) ? FMath::Max(S.N, 3) : S.N;
	const int32 TileCap = AsdaFxTileCapForParents(ParentsAsked, TileMax, (int32)AsdaFxMaxSplit);
	AsdaFxWarnTileCap(ParentsAsked, FMath::Clamp(TileMax, 1, 8), TileCap, (int32)AsdaFxMaxSplit);
	// Наименьшее число плиток, при котором ПЛИТКА УКЛАДЫВАЕТСЯ В СВОЮ СФЕРУ, и её радиус.
	//   HalfChord — полу-длина ВСЕГО куска (полосы, дуги, капсулы). Правило — общее (AsdaFxTilesFor).
	auto FitTiles = [&](float HalfChord, int32& OutTiles, float& OutR)
	{
		OutTiles = AsdaFxTilesFor(HalfChord, R0, CapR, TileCap, OutR);
	};
	// Положить плитки одного куска. Возвращает, сколько легло.
	//   ★ fix6 (МЕЛК-4): после пересчёта потолка выше обрыв по месту в массиве СТАТЬ НЕ МОЖЕТ —
	//     родителей × TileCap ≤ AsdaFxMaxSplit по построению, а плиток у куска не больше TileCap.
	//     Условие `S.N < AsdaFxMaxSplit` оставлено страховкой на случай правки формул выше.
	auto Emit = [&](int32 Parent, int32 Tiles, float Rk, float HalfChord, float Across, bool bAlong) -> int32
	{
		const float TileLen = FMath::Min(2.f * HalfChord / (float)Tiles, 2.f * Rk * ASDA_FX_SPLIT_HALFLEN_SHARE);
		int32 Put = 0;
		for (int32 t = 0; t < Tiles && S.N < (int32)AsdaFxMaxSplit; ++t)
		{
			// Центр плитки t: от −полу-длины до +полу-длины, середина своей доли
			const float Off = -HalfChord + (HalfChord / (float)Tiles) * (2.f * (float)t + 1.f);
			S.R[S.N] = Rk;
			S.Len[S.N] = TileLen;
			S.Pos[S.N] = bAlong ? (Across + Off) : Across;   // у трубки и плитки смещение идёт по ТОЙ ЖЕ оси
			S.Along[S.N] = bAlong ? 0.f : Off;
			S.Idx[S.N] = Parent;
			++S.N; ++Put;
		}
		return Put;
	};

	const int32 Want = S.N;   // сколько РОДИТЕЛЬСКИХ кусков заказано (полос, дуг, капсул, плиток)
	S.N = 0;
	switch (Sh)
	{
	case EAsdaFxLightShape::Disc:
	{
		// Полосы-хорды: центр полосы k на b_k вдоль поперечной оси, полу-длина = полу-хорда на этом b
		const float DR = FMath::Max(SizeR, 1.f);
		S.Parents = Want;
		for (int32 k = 0; k < Want; ++k)
		{
			const float b = DR * (-1.f + (2.f * (float)k + 1.f) / (float)Want);
			const float hc = FMath::Sqrt(FMath::Max(DR * DR - b * b, 0.f));
			int32 m = 1; float Rk = 0.f;
			FitTiles(hc, m, Rk);
			Emit(k, m, Rk, hc, b, /*bAlong=*/false);
		}
		break;
	}
	case EAsdaFxLightShape::Ring:
	{
		// Дуги — хорды вписанного N-угольника: середина на R·cos(π/N), длина 2R·sin(π/N)
		const int32 NN = FMath::Max(Want, 3);
		const float RR = FMath::Max(SizeR, 1.f);
		const float Half = PI / (float)NN;
		const float HalfChord = RR * FMath::Sin(Half);
		const float Inner = RR * FMath::Cos(Half);   // удаление середины дуги от центра обода
		int32 m = 1; float Rk = 0.f;
		FitTiles(HalfChord, m, Rk);
		S.Parents = NN;
		for (int32 k = 0; k < NN; ++k)
		{
			Emit(k, m, Rk, HalfChord, Inner, /*bAlong=*/false);
		}
		break;
	}
	case EAsdaFxLightShape::Tube:
	{
		// Капсулы встык вдоль оси: SizeR — ПОЛУ-длина трубки
		const float HalfLen = FMath::Max(SizeR, 0.5f);
		const float SegHalf = FMath::Max(HalfLen / (float)Want, 0.5f);
		int32 m = 1; float Rk = 0.f;
		FitTiles(SegHalf, m, Rk);
		S.Parents = Want;
		for (int32 k = 0; k < Want; ++k)
		{
			// Плитки капсулы едут по ТОЙ ЖЕ оси, что и сама капсула, поэтому смещение складывается
			Emit(k, m, Rk, SegHalf, -HalfLen + SegHalf * (2.f * (float)k + 1.f), /*bAlong=*/true);
		}
		break;
	}
	case EAsdaFxLightShape::Rect:
	{
		// Плитки вдоль длинной стороны: SizeR — полная ШИРИНА прямоугольника.
		//   ★ fix5: у прямоугольника ПЛИТОК ВДОЛЬ ПЛИТКИ НЕ БЫВАЕТ. Источник движка здесь —
		//   URectLightComponent: его светящаяся поверхность задана ШИРИНОЙ и ВЫСОТОЙ, а не длиной
		//   капсулы, и она НЕ обязана укладываться в сферу затухания — за край сферы уходит не
		//   «тёмный конец лампы», а просто неосвещённая часть сцены. Дробить тут нечего.
		const float W = FMath::Max(SizeR, 1.f);
		const float TileW = FMath::Max(W / (float)Want, 1.f);
		const float Rk = Fit(0.5f * TileW);
		S.Parents = Want;
		for (int32 k = 0; k < Want && S.N < (int32)AsdaFxMaxSplit; ++k)
		{
			S.R[S.N] = Rk;
			S.Len[S.N] = TileW;
			S.Pos[S.N] = -0.5f * W + TileW * ((float)k + 0.5f);
			S.Along[S.N] = 0.f;
			S.Idx[S.N] = k;
			++S.N;
		}
		break;
	}
	default:
		// Точка, шар, конус и любая неразложенная форма — ОДИН источник полного радиуса
		S.N = 1;
		S.Parents = 1;
		S.R[0] = FMath::Min(FMath::Max(BaseR, 1.f), CapR);
		S.Len[0] = 0.f;
		S.Pos[0] = 0.f;
		S.Along[0] = 0.f;
		S.Idx[0] = 0;
		break;
	}
	if (S.N <= 0) { S.N = 1; S.Parents = 1; S.R[0] = FMath::Min(FMath::Max(BaseR, 1.f), CapR); }
	for (int32 k = 0; k < S.N; ++k) { S.SumR2 += (double)S.R[k] * (double)S.R[k]; }
	return S;
}

UAsdaFxLightComponent::FAsdaFxGiveShape UAsdaFxLightComponent::ResolveGiveShape(
	const FAsdaFxLightSpec& Spec, EAsdaFxLightShape Sh, int32 Give, float SizeR, float BaseR, float Cap,
	bool bDiscAsPoint)
{
	// ★★★ РЕВИЗИЯ fix3 (КРИТ-1, КРИТ-2): ЕДИНСТВЕННОЕ ПРАВИЛО ВЫРОЖДЕНИЯ — И ДЛЯ КАДРА, И ДЛЯ БЮДЖЕТА.
	//   Раньше оно жило только в ApplyFrame, а бюджет считал цену ЗАЯВЛЕННОЙ формы. Замер ревизора:
	//   при бюджете 7 все четыре формы горели ОДНИМ источником полного радиуса (картинка одинакова),
	//   а списывалось 4.91 у точки против 2.40 у блина и кольца — ровно в 1/0.7² раз меньше, потому
	//   что доля радиуса полосы бралась из поля, а полос на сцене не было вовсе.
	// ★★★ РЕВИЗИЯ fix5 (КРИТ-А): У ФОРМЫ ТЕПЕРЬ ДВЕ ПОДРОБНОСТИ, И РЕЖУТСЯ ОНИ В РАЗНОМ ПОРЯДКЕ.
	//   ПОЛОСЫ (дуги, капсулы) — это подробность формы; ПЛИТКИ вдоль полосы — это её ПОКРЫТИЕ, то
	//   есть ровно то, ради чего волна и затевалась («свет по всей плоскости»). Поэтому при нехватке
	//   мест сначала уменьшается ЧИСЛО ПОЛОС при полном покрытии каждой, и лишь когда мест не хватает
	//   даже ОДНОЙ покрытой полосе — режутся плитки. Обратный порядок вернул бы картину fix4
	//   (много коротких черт по середине формы) при том же числе источников.
	FAsdaFxGiveShape Out;
	Out.Shape = Sh;
	const int32 N = FMath::Max(Give, 1);
	const int32 Seg = FMath::Clamp(Spec.ShapeSegments, 1, 16);
	// ★ fix7: у БЛИНА в авто-режиме потолок плиток поднят до предела поля — довод у AsdaFxTileMaxFor
	const int32 TileMax = AsdaFxTileMaxFor(Spec, Sh, SizeR, BaseR);
	// ★ fix8 (С-1): у блина доля радиуса в авто-режиме идёт с разбегом над порогом (AsdaFxDiscShareFor);
	//   у прочих форм — как было. Одно число на все ветки этой функции и на счёт цены.
	const float Share = (Sh == EAsdaFxLightShape::Disc) ? AsdaFxDiscShareFor(Spec, SizeR, BaseR)
		: ShapeRadiusShare(Spec, Sh);
	// Наибольшее число ПОЛОС (не меньше MinParents), которое влезает в N мест при полном покрытии.
	//   Возвращает 0, если не влезает даже MinParents.
	auto FitParents = [&](int32 WantParents, int32 MinParents, int32& OutSources) -> int32
	{
		for (int32 s = WantParents; s >= MinParents; --s)
		{
			const int32 Src = SplitSourceCount(Sh, s, SizeR, BaseR, Share, Cap, TileMax);
			if (Src <= N) { OutSources = Src; return s; }
		}
		OutSources = 0;
		return 0;
	};
	// Мест не хватает даже на полное покрытие MinParents полос — режем ПЛИТКИ у них же.
	auto FitTilesDown = [&](int32 Parents, int32& OutSources) -> int32
	{
		for (int32 t = TileMax - 1; t >= 1; --t)
		{
			const int32 Src = SplitSourceCount(Sh, Parents, SizeR, BaseR, Share, Cap, t);
			if (Src <= N) { OutSources = Src; return t; }
		}
		OutSources = 0;
		return 0;
	};
	switch (Sh)
	{
	case EAsdaFxLightShape::Ring:
	{
		const int32 Want = FMath::Clamp(Spec.RingSegments, 3, 8);
		int32 Src = 0;
		const int32 Arcs = FitParents(Want, 3, Src);
		if (Arcs >= 3)
		{
			Out.Segments = Arcs; Out.Sources = Src; Out.Tiles = TileMax; Out.RadiusShare = Share;
		}
		else if (N >= 3)
		{
			// Обод собирается, но на полное покрытие дуг мест нет — режем плитки, оставляя три дуги
			const int32 T = FitTilesDown(3, Src);
			Out.Segments = 3; Out.Tiles = FMath::Max(T, 1); Out.Sources = (Src > 0) ? Src : 3;
			Out.RadiusShare = Share;
		}
		else
		{
			// Двумя дугами обод не собрать — честно вырождаемся в точку ПОЛНОГО радиуса
			Out.Shape = EAsdaFxLightShape::Point;
			Out.Segments = 1; Out.Sources = 1; Out.Tiles = 1; Out.RadiusShare = 1.f;
		}
		break;
	}
	case EAsdaFxLightShape::Disc:
	{
		// ★★★ fix7 (п.2 ревизии): БЛИН МЕЛЬЧЕ СВОЕГО СВЕТА — ОДИН ИСТОЧНИК ПОЛНОГО РАДИУСА.
		//   Это НЕ отмена КРИТ-2: формой остаётся всё та же полоса-хорда через центр во всю ширину
		//   блина, ей только возвращается ПОЛНЫЙ радиус (доля 1.0 вместо 0.7). Шаг именно такой, а
		//   не «Out.Shape = Point», по двум причинам: (1) кадр ТОЧКИ обнуляет измеренный размер формы
		//   (ApplyShapeFrame, хвост: LastSplitSizeR = 0 у всякой формы, кроме прямоугольника, блина,
		//   кольца и трубки) — отношение стало бы неизвестным, и следующий кадр снова построил бы
		//   блин: форма мигала бы КАЖДЫЙ КАДР; (2) растущий всплеск так навсегда остался бы точкой.
		//   Здесь же размер меряется каждый кадр, и блин сам станет блином, как только перерастёт
		//   свой свет. Замер (радиус света 288, блин 0.7×): Σ R² 0.083 млн против 0.163 у четырёх
		//   полос, источник 1 против 4, средняя по площади 0.000151 против 0.000126.
		//   ★ fix8 (МЕЛК-1 ревизии fix7): функции «ApplyPointFrame» не существует и не существовало —
		//     обнуление лежит в ApplyShapeFrame. Миснейм исправлен.
		//   ★ fix8 (С-1): решение приходит АРГУМЕНТОМ, потому что у порога появилась память
		//     (гистерезис, UAsdaFxLightComponent::DiscAsPoint).
		if (bDiscAsPoint)
		{
			Out.Segments = 1; Out.Sources = 1; Out.Tiles = 1; Out.RadiusShare = 1.f;
			break;
		}
		// ★ КРИТ-2: блин НИКОГДА не вырождается в точку. Одна полоса — это хорда через центр диска
		//   длиной в полный диаметр; подробность режется до черты, а не до шарика.
		// ★ fix7 (п.1 ревизии): сколько полос ПРОСИТ форма — из отношения «диаметр ÷ радиус света»,
		//   а не из постоянного числа в поле (довод и таблица — у AsdaFxDiscStripsFor).
		const int32 Want = AsdaFxDiscStripsFor(Spec, SizeR, BaseR);
		int32 Src = 0;
		const int32 Strips = FitParents(Want, 1, Src);
		if (Strips >= Want && Src > 0)
		{
			// Мест хватило на ПОЛНУЮ раскладку — резать нечего
			Out.Segments = Strips; Out.Sources = Src; Out.Tiles = TileMax;
			Out.RadiusShare = Share;
			break;
		}
		// ═══ ★★★ ВОЛНА fix8 (пункт К-2 ревизии fix7): ДЕГРАДАЦИЯ ПОД БЮДЖЕТОМ ═══════════════════
		//
		//   ЧТО БЫЛО НЕ ТАК. Гарантия «тьмы нет» относилась к ПОЛНОЙ выдаче, а живой прогон fix7
		//   (слежение за 200 кадрами, форма 5× радиуса) шёл ниже неё в 38 % кадров: мест по кадрам
		//   22 (125 кадров), 20 (6), 18 (9), 14 (9), 13 (14), 10 (37). При десяти местах прежний
		//   порядок («режем ПОЛОСЫ при полном покрытии каждой») оставлял ТРИ полосы доли 0.7 —
		//   4.04 % тьмы и 23.89 % тусклой площади, то есть ровно тот дефект, ради которого волна
		//   затевалась. Худший кадр автоматики равнялся худшему кадру прежнего умолчания.
		//
		//   ЧТО ПОКАЗАЛ ЗАМЕР (an8_degrade.py: при каждом числе мест перебраны ВСЕ раскладки
		//   «полос × плиток × доля» и найдена лучшая по площади). Ни один односторонний порядок
		//   не годится:
		//     * срезанные ПОЛОСЫ дают тьму ПОЛОСАМИ между ними (форма 7×, 22 места: 4 полосы по
		//       8 плиток = 20 источников, тьма 14.2 %, а 6 полос по 4 плитки = 22 источника — 0.4 %);
		//     * срезанные ПЛИТКИ дают тьму КОЛЬЦОМ по ободу — хорда не достаёт до края (форма 5×,
		//       14 мест: 7 полос по 2 плитки = 14 источников, тьма 12.8 %, а 4 полосы полного
		//       покрытия = те же 14 источников — 0.35 %).
		//       ★ fix9 (МЕЛК-4 ревизии fix8): ЗДЕСЬ СТОЯЛО «0.0 %», И ЭТО БЫЛА ЧУЖАЯ СТРОКА.
		//         При доле радиуса ИЗ ПОЛЯ (0.7) четыре полосы полного покрытия дают 0.35 % тьмы и
		//         7.98 % тусклой площади; ноль получается только ПОСЛЕ подъёма доли до 1.0 (тогда
		//         0.00 / 2.16 %) — то есть от ВТОРОГО средства этой же ветки, а не от порядка среза.
		//         Довод «резать надо оба» числа не теряет: 0.35 против 12.80 % — та же разница.
		//   Значит резать надо ОБА, и в измеренной пропорции. Недобор полос = s / правило, недобор
		//   плиток = t / нужно самой длинной полосе; берём раскладку с наибольшим МЕНЬШИМ из них,
		//   вес полос — ключ BudgetBias. Вес подобран перебором (an8_degrade3.py, 490 строк замера:
		//   7 размеров формы × 5 конфигураций × 14 бюджетов): при 1.2…1.4 отставание от лучшей
		//   раскладки наименьшее — 0.09 п.п. тьмы и 0.06 п.п. тусклой площади против 7.75 и 7.64
		//   у прежнего порядка; худшая строка 5.6 п.п. против 40.7.
		//   ★ ВТОРОЕ: при ЛЮБОМ срезе доля радиуса поднимается до 1.0 (кусок светит ровно так же
		//     далеко, как светил бы весь источник, — выше нельзя, КРИТ-2 fix4). Замер: те же места,
		//     тьма и тусклая площадь строго меньше (форма 5×, 22 места: 0.00/0.92 → 0.00/0.00;
		//     18 мест: 0.00/1.84 → 0.00/0.14; 14 мест: 0.35/7.98 → 0.00/2.16). Плиток при большем
		//     радиусе нужно МЕНЬШЕ, поэтому подъём доли не стоит ни одного лишнего источника.
		//   ★ ЧЕСТНО: это не отменяет предела. При десяти местах блин 5× остаётся с 1.42 % тьмы и
		//     21.20 % тусклой площади (было 4.04 / 23.89): десятью источниками форму вдесятеро
		//     крупнее света не покрыть ничем.
		const float CutShare = (GAsdaFxDiscBudgetBias > 0.f) ? 1.f : Share;
		const int32 TNeed = FMath::Max(AsdaFxDiscTilesNeeded(SizeR, BaseR, CutShare, Cap, TileMax), 1);
		int32 BestS = 0, BestT = 1, BestSrc = 0;
		float BestScore = -1.f;
		for (int32 s = 1; s <= Want; ++s)
		{
			int32 TFit = 0, SrcFit = 0;
			for (int32 t = 1; t <= TileMax; ++t)
			{
				// Число источников не убывает с ростом плиток — первый непоместившийся обрывает перебор
				const int32 SrcT = SplitSourceCount(Sh, s, SizeR, BaseR, CutShare, Cap, t);
				if (SrcT > N) { break; }
				TFit = t; SrcFit = SrcT;
			}
			if (TFit <= 0) { break; }            // даже одна плитка не влезла — дальше полос только больше
			const float StripPart = FMath::Min(FMath::Max(GAsdaFxDiscBudgetBias, 0.f) * (float)s / (float)Want, 1.f);
			const float TilePart = FMath::Min((float)TFit / (float)TNeed, 1.f);
			const float Score = FMath::Min(StripPart, TilePart);
			// При равном счёте берём БОЛЬШЕ полос: тьма полосами заметнее глазу, чем недосвеченный обод
			if (Score > BestScore + KINDA_SMALL_NUMBER || (Score > BestScore - KINDA_SMALL_NUMBER && s > BestS))
			{
				BestScore = Score; BestS = s; BestT = TFit; BestSrc = SrcFit;
			}
		}
		if (BestS >= 1 && BestSrc > 0 && GAsdaFxDiscBudgetBias > 0.f)
		{
			Out.Segments = BestS; Out.Tiles = BestT; Out.Sources = BestSrc; Out.RadiusShare = CutShare;
			break;
		}
		// Откат к порядку fix5/fix7 (ключ BudgetBias 0) и страховка «не нашлось ничего»
		if (Strips >= 1)
		{
			Out.Segments = Strips; Out.Sources = Src; Out.Tiles = TileMax;
		}
		else
		{
			const int32 T = FitTilesDown(1, Src);
			Out.Segments = 1; Out.Tiles = FMath::Max(T, 1); Out.Sources = (Src > 0) ? Src : 1;
		}
		Out.RadiusShare = Share;
		break;
	}
	case EAsdaFxLightShape::Tube:
	{
		int32 Src = 0;
		const int32 Caps = FitParents(Seg, 1, Src);
		if (Caps >= 1)
		{
			Out.Segments = Caps; Out.Sources = Src; Out.Tiles = TileMax;
		}
		else
		{
			const int32 T = FitTilesDown(1, Src);
			Out.Segments = 1; Out.Tiles = FMath::Max(T, 1); Out.Sources = (Src > 0) ? Src : 1;
		}
		// Одна капсула светит на ПОЛНЫЙ радиус света (доля кусков к ней не относится)
		Out.RadiusShare = (Out.Segments > 1) ? FMath::Clamp(Spec.ShapeSegmentRadiusScale, 0.1f, 2.f) : 1.f;
		// ★ fix5: ЧИСЛО ИСТОЧНИКОВ ПЕРЕСЧИТЫВАЕТСЯ ПРИ ИТОГОВОЙ ДОЛЕ РАДИУСА. Доля влияет на то,
		//   сколько плиток нужно полосе; у трубки, которую бюджет срезал до одной капсулы, итоговая
		//   доля становится 1.0 вместо заданной, и без пересчёта обещанное бюджету число источников
		//   разошлось бы с тем, что построит кадр света (закон fix3, КРИТ-1).
		Out.Sources = SplitSourceCount(Sh, Out.Segments, SizeR, BaseR, Out.RadiusShare, Cap, Out.Tiles);
		break;
	}
	case EAsdaFxLightShape::Rect:
	{
		const bool bWantBoth = (Spec.bRectTwoSided || Spec.Shape == EAsdaFxLightShape::Auto);
		const int32 Sides = bWantBoth ? 2 : 1;
		if (N >= Seg * Sides)
		{
			Out.Segments = Seg; Out.bTwoSided = bWantBoth;
		}
		else if (N >= Sides)
		{
			Out.Segments = FMath::Max(N / Sides, 1); Out.bTwoSided = bWantBoth;
		}
		else
		{
			// На две стороны не хватает даже одного места: гасим обратную (и сила вернётся к исходной)
			Out.Segments = 1; Out.bTwoSided = false;
		}
		Out.Sources = Out.Segments * (Out.bTwoSided ? 2 : 1);
		Out.RadiusShare = (Out.Segments > 1) ? FMath::Clamp(Spec.ShapeSegmentRadiusScale, 0.1f, 2.f) : 1.f;
		break;
	}
	default:
		Out.Segments = 1; Out.Sources = 1; Out.RadiusShare = 1.f;   // точка, шар, конус
		break;
	}
	return Out;
}
