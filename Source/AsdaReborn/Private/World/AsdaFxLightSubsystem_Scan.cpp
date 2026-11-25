// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightSubsystem_Scan.cpp — ★ R3-c 12.11.2026 (разрез AsdaFxLightSubsystem.cpp, дословный перенос): учёт эффектов: появление актора, обход мира, имя .jeb, сверка наборов, регистрация, перепроверка погасших.
//   Общее для частей — в AsdaFxLightSubsystem_Private.h; шапка и реверс — в AsdaFxLightSubsystem.cpp.
#include "World/AsdaFxLightSubsystem.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaFxLightComponent.h"
#include "World/AsdaFxLightSettings.h"
#include "World/AsdaVisibilitySubsystem.h"   // ★ Волна Ф9: ОБЩАЯ точка «объект попадает в кадр» (расширенный конус, гистерезис, предсказание по угловой скорости)
#include "UI/AsdaDevMenuSubsystem.h"
#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameUserSettings.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif
#include "AsdaFxLightSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaFxLightSubsystem*.cpp

// Уровневый множитель дальности светов (r.LightMaxDrawDistanceScale: 0.5 на Low/Medium) — ★ волна 3 п.12 (15.09.2026):
// UAsdaRenderQualitySubsystemBase::LightDrawDistanceScale, тот же читает UAsdaFxLightComponent: отбор обязан отсекать
// ровно там, где движок перестаёт рисовать свет.
// ★ ЗАЩИТА АССЕТА КАРТЫ ОТ РАНТАЙМ-СВЕТОВ. В окне редактора мы создаём компоненты на акторах открытой карты.
//   Компоненты RF_Transient и в уровень не сериализуются, но САМ ФАКТ добавления компонента к актору взводит
//   пакету уровня флаг «изменено» — и владелец получил бы карту со звёздочкой, ничего не тронув, а потом
//   сохранил бы её «на всякий случай». Хранитель возвращает флаг ровно в то состояние, в каком он был до нас
//   (если карта была изменена ДО — так и остаётся изменённой, чужие правки мы не глотаем).
struct FAsdaFxLightPackageDirtyGuard
{
	UPackage* Pkg = nullptr;
	bool bWasDirty = false;

	explicit FAsdaFxLightPackageDirtyGuard(const UObject* Obj)
	{
#if WITH_EDITOR
		if (GIsEditor && Obj)
		{
			Pkg = Obj->GetPackage();
			bWasDirty = Pkg && Pkg->IsDirty();
		}
#endif
	}

	~FAsdaFxLightPackageDirtyGuard()
	{
#if WITH_EDITOR
		if (Pkg && !bWasDirty && Pkg->IsDirty()) { Pkg->SetDirtyFlag(false); }
#endif
	}
};

// ─────────────── заведение кандидатов ───────────────
void UAsdaFxLightSubsystem::OnActorSpawned(AActor* A)
{
	if (AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(A)) { RegisterEffect(Fx); }
}

void UAsdaFxLightSubsystem::ScanWorld()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	bScanned = true;

	// ★ ПЕРЕПИСЬ КАРТЫ В ЛОГ. Нужна не для красоты: имя .jeb — ЕДИНСТВЕННОЕ, чем эффект опознаётся в таблице
	//   семейств, и если оно окажется пустым (или правило промахнётся), эффект получит не те дефолты, и это
	//   будет неотличимо от «так и задумано» ([[false-negatives-method]]: «нет» обязано быть доказано).
	TMap<FString, int32> Seen;
	TMap<FString, int32> Lit;
	for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
	{
		AAsdaEffectActor* Fx = *It;
		RegisterEffect(Fx);
		if (!IsValid(Fx)) { continue; }
		const FString Jeb = ResolveJebName(Fx);
		Seen.FindOrAdd(Jeb)++;
		if (Fx->FindComponentByClass<UAsdaFxLightComponent>()) { Lit.FindOrAdd(Jeb)++; }
	}
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	UE_LOG(LogAsdaFxLight, Log, TEXT("[эффект-света] перепись карты %s: %d уникальных имён .jeb, правил-семейств в таблице %d, кандидатов %d"),
		*W->GetName(), Seen.Num(), S->Families.Num(), Candidates.Num());
	for (const TPair<FString, int32>& P : Seen)
	{
		const FAsdaFxLightRule* R = S->FindRule(P.Key);
		UE_LOG(LogAsdaFxLight, Log, TEXT("   %-40s ×%-4d → %-22s %s"), *P.Key, P.Value,
			R ? *R->Id : TEXT("<правила нет>"),
			Lit.FindRef(P.Key) > 0 ? TEXT("СВЕТ") : TEXT("без света"));
	}
}

void UAsdaFxLightSubsystem::Rescan()
{
	// Компоненты живут на акторах эффектов — сносим их явно, иначе повторное заведение увидит «уже есть»
	for (TObjectPtr<UAsdaFxLightComponent>& C : Candidates)
	{
		if (IsValid(C))
		{
			FAsdaFxLightPackageDirtyGuard Guard(C->GetOwner());
			C->SetAdmitted(false);
			C->DestroyComponent();
		}
	}
	Candidates.Reset();
	LeaseAge.Reset();
	OffAge.Reset();
	// ★ fix16: список погасших наполняет заведение, а ScanWorld ниже заводит ВСЕХ заново — значит
	//   старый список к этому моменту уже ничего не описывает. Не сбросить его означало бы держать
	//   в нём эффекты, у которых свет только что появился, и тратить на них круговой обход.
	Extinguished.Reset();
	ExtinguishedCursor = 0;
	bScanned = false;
	SelectTimer = 1.e6f;
	ScanWorld();
}

FString UAsdaFxLightSubsystem::ResolveJebName(const AAsdaEffectActor* Fx)
{
	// Имя .jeb: поле актора; у ассетов, где его забыли проставить, — база имени класса BP_Fx_<jeb>
	FString Jeb = Fx ? Fx->EffectName : FString();
	if (Jeb.IsEmpty() && Fx)
	{
		// ★ ИСПРАВЛЕНИЕ D4 (волна 2 п. 7, 14.09.2026): было RemoveFromEnd("_C") + RemoveFromStart("BP_Fx_") — дефис имени .jeb
		//   («…-a_00») не возвращался, точное имя в переопределении не совпало бы. Теперь дефис — по таблице реальных имён
		//   (AsdaAssetPaths::JebFromFxClassName; имена без дефиса, не-BP_Fx_ и test-000 — как раньше).
		Jeb = AsdaAssetPaths::JebFromFxClassName(Fx->GetClass()->GetName());
	}
	return Jeb;
}

// ═══ ★★★ ВОЛНА fix13 (КРИТ-1 ревизии fix12, 20.09.2026): ЗЕРКАЛО ДЕЙСТВУЮЩИХ ЧИСЕЛ В ПОЛЯ АССЕТА
//
//   ЧТО БЫЛО НЕ ТАК. Тело этой функции стояло прямо в RegisterEffect под `#if WITH_EDITOR` и
//   работало ТОЛЬКО в окне редактора. Стенд `asda.Fx.Light.Stand` живёт в игровом мире, зеркала не
//   получал, и его правка (f13_stand.py: `bManual = True` плюс поля формы) ложилась поверх
//   C++-ЗАГЛУШКИ полей ассета, где охват — «один свет на эффект». Отсюда и расхождение, ради
//   которого заведена вся волна: при заведении эффект получал 8 поэмиттерных светов ПО СЕМЕЙСТВУ,
//   а после первого пересканирования — 1 свет по ЗАМКУ АССЕТА, потому что к этому моменту стенд
//   уже успел взвести замок. Замер «поставка per_emitter против одного на эффект» мерил при этом
//   не поставку, а заглушку (закон diagnostic-must-use-real-path).
//
//   ЗАЧЕМ ЗЕРКАЛО ВООБЩЕ. У эффект-акторов, РАЗМЕЩЁННЫХ на картах, поля «Свет эффекта» генератором
//   дефолтов не проставлены (карты намеренно не переписываются: над теми же .umap параллельно идёт
//   калибровка bloom). Без зеркала владелец открыл бы фонарь карты и увидел C++-заглушку «7.5 кд,
//   свет включён» вместо действующих 43.2 кд — панель врала бы и про яркость, и про то, светит ли
//   эффект вообще (у дыма заглушка сказала бы «включён», хотя семейство света ему не даёт).
//   Поэтому зеркалим ДО проверки «включён» и для ВСЕХ эффектов, а флаг «изменено» пакета возвращает
//   хранитель: карта остаётся чистой, числа в панели — настоящие. Как только владелец что-то правит,
//   PostEditChangeProperty взводит bManual, и зеркало больше не срабатывает.
bool UAsdaFxLightSubsystem::MirrorSpecToAsset(AAsdaEffectActor* Fx, const FAsdaFxLightRule* Rule)
{
	if (!IsValid(Fx) || !Rule || Fx->FxLight.bManual) { return false; }
	// Хранитель флага «изменено» — на случай вызова из окна редактора (в игре ничего не делает)
	FAsdaFxLightPackageDirtyGuard Guard(Fx);
	Fx->FxLight = Rule->Spec;
	// ★ Ф10: в мире нестандартного масштаба (логин ×100) панель показывает УЖЕ УМНОЖЕННЫЕ дефолты —
	//   ровно те, с которыми свет горит (ResolveSpec масштабирует дефолты той же функцией). Правка
	//   владельца начинается с них и дальше живёт в юнитах этого мира (ручные числа не масштабируются).
	UAsdaFxLightComponent::ScaleSpecToWorld(Fx->FxLight, WorldLengthScale(GetWorld()));
	Fx->FxLight.FamilyId = Rule->Id;
	Fx->FxLight.bManual = false;
	return true;
}

// ═══ ★★★ ВОЛНА fix13 (КРИТ-1): НАБОР СВЕТОВ ОБЯЗАН СЛЕДОВАТЬ ЗА ДЕЙСТВУЮЩЕЙ СПЕЦИФИКАЦИЕЙ ════════
//
//   РАЗДЕЛЕНИЕ, ИЗ КОТОРОГО ВЫРОСЛА БОЛЕЗНЬ. ЧИСЛА света (сила, радиус, цвет, форма) компонент
//   перечитывает КАЖДЫЙ ТИК сам — они не устаревают никогда. А НАБОР светов (один на эффект против
//   своего у каждого эмиттера, и сколько именно эмиттеров) строит ТОЛЬКО RegisterEffect, то есть
//   ровно один раз при заведении. Пока спецификацию правили через панель деталей, это сходилось:
//   подписка OnObjectPropertyChanged звала RegisterEffect заново. Но подписка стоит под
//   `WITH_EDITOR` И ТОЛЬКО ДЛЯ МИРА РЕДАКТОРА, а писать в поля эффекта умеют и другие:
//   UE-Python `set_editor_property` (наш же стенд), Blueprint, конфигуратор свечения в PIE.
//   В игровом мире набор оставался от прежней спецификации — и состояние эффекта зависело от того,
//   заводился он только что или пережил пересканирование. Это и видела ревизия fix12 как
//   «8 поэмиттерных светов при заведении, 1 после Rescan».
//
//   ПОЧЕМУ СВЕРКА, А НЕ «ЗВАТЬ RegisterEffect НА КАЖДУЮ ПРАВКУ». Писателя может не быть вовсе
//   (правка приходит из скрипта мимо всех уведомлений), а искать всех писателей — это тот самый
//   список, который уже оказался неполон. Поэтому сверяем результат.
//
//   ★★★ ЧТО ИМЕННО СВЕРЯЕТСЯ — ИСПРАВЛЕНО В fix14 (С-3 и С-4 ревизии fix13). До fix14 здесь стояли
//   ТРИ СКАЛЯРА уровня эффекта: «свет включён», охват, кошелёк. Формулировка в комментарии обещала
//   «три величины, которые и решают состав набора», но состав решают не они:
//     * С-4. Состав зависит и от спецификаций ОТДЕЛЬНЫХ эмиттеров: RegisterEffect выкидывает
//       эмиттеры с выключенным свечением и поднимает ручные. Владелец гасит свечение одному
//       эмиттеру — набор обязан стать на один свет короче, а все три скаляра остаются теми же,
//       и расхождения сверка не видела. В редакторе это ловила подписка на правку свойств; в
//       СОБРАННОЙ игре подписки нет вовсе (весь блок под `#if WITH_EDITOR`), то есть не ловил никто.
//     * С-3. Скаляр кошелька ловил только УМЕНЬШЕНИЕ (`HavePerEmitter > Cap`) — и УМЕНЬШЕНИЕ,
//       значит, чинилось само и до fix14. Сломано было ровно ПОВЫШЕНИЕ: игрок, поднявший
//       «Эффекты» с Low до самого верха, увеличивает кошелёк 1 → 8 (★ fix15, МЕЛК-1: 8 — это
//       Cine, то есть уровень 4; Epic даёт 4, см. `[AsdaFxLight@N] Light.FxPerEffect` = 1/2/3/4/8),
//       а набор из одного света под условие не попадал и оставался урезанным. (Саму смену уровня
//       теперь ещё и ЗОВЁТ сверку принудительно: OnQualityLevelsChanged взводит флаг, Tick зовёт
//       ReconcileSets(true) — см. заголовок.)
//       ★ fix15 (СРЕДН-1): охват дефекта — ТОЛЬКО эффекты семейств с охватом «свой свет у каждого
//       эмиттера»; у охвата «один на эффект» кошелёк не читается вовсе. В таблице 18 семейств
//       per_emitter стоит у 7, включены три: char_skill, char_skill_4th, mob_skill.
//   Теперь сверяется САМ ЗАКАЗ: ComputeDesiredSet строит тот же список номеров эмиттеров, который
//   построил бы RegisterEffect, и он сравнивается с фактическим ПОЭЛЕМЕНТНО. Обе стороны берут
//   список из ОДНОЙ функции — иначе сверка и заведение спорили бы между собой каждый кадр.
//
//   ЦЕНА. Один ResolveSpec на эффект плюс по одному на эмиттер, когда охват «по эмиттерам»;
//   перебор узлов (веса отбора) делается ТОЛЬКО когда подходящих эмиттеров больше кошелька —
//   при равенстве берутся все, и вес ни на что не влияет.
//   ★ МЕЛК-4 ревизии: зовётся из Tick, то есть КАЖДЫЙ КАДР. Прежняя подпись говорила «каждый
//   отбор» — отбор ходит под таймером SelectIntervalSec, и совпадает это только потому, что
//   поставочный SelectIntervalSec = 0. Стоит владельцу поставить период — и подпись стала бы ложью.
//   Откат — `asda.Fx.Light.AutoReconcile 0` (поведение ровно как до fix13); bForce его перебивает,
//   и так зовёт сверку смена уровня качества (см. OnQualityLevelsChanged в заголовке).
void UAsdaFxLightSubsystem::ReconcileSets(bool bForce)
{
	if (GAsdaFxLightAutoReconcile == 0 && !bForce) { return; }
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	if (!S->bEnabled) { return; }
	// ★ «форма-2», п. 2: шаг времени для счётчика темноты набора. Берётся у мира, а не копится
	//   своим таймером: сверка зовётся из Tick, и второй источник времени тут разошёлся бы с первым.
	//   ★ 29.10.2026: при сверке с частотой asda.Fx.Light.ReconcileHz шаг — накопленный с прошлой сверки (ReconcileDt
	//   выставляет Tick), иначе счётчик темноты шёл бы в 6–14 раз медленнее. Время по-прежнему мировое.
	const float Dt = ReconcileDt;
	LastDarkRepicks = 0;

	// Эффекты, у которых есть хоть один кандидат, — по одному разу каждый
	TSet<AAsdaEffectActor*, DefaultKeyFuncs<AAsdaEffectActor*>, TInlineSetAllocator<64>> Seen;
	// ★ fix14: рядом с расходящимся эффектом — САМИ СПИСКИ, а не только их длины. Главный случай
	//   С-4 («погасили свечение одному эмиттеру») меняет СОСТАВ набора, не меняя его РАЗМЕРА:
	//   выбывший эмиттер тут же заменяется следующим по весу. По одним числам «заказано 8, было 8»
	//   расхождение было бы неотличимо от ложного срабатывания — поэтому печатаем номера.
	struct FStale { AAsdaEffectActor* Fx; FString Want; FString Have; };
	// ★ fix15 (СРЕДН-2): сколько эффектов недособраны ИМЕННО из-за предела кандидатов — считаем
	//   этим обходом, чтобы сообщение ниже называло число, а не «какие-то»
	int32 CeilingShortNum = 0;
	auto ListOf = [](const TArray<int32, TInlineAllocator<8>>& A) -> FString
	{
		if (A.Num() == 0) { return TEXT("<пусто>"); }
		FString S;
		for (int32 V : A) { S += (V == INDEX_NONE) ? TEXT("общий ") : FString::Printf(TEXT("#%d "), V); }
		return S.TrimEnd();
	};
	TArray<FStale, TInlineAllocator<16>> Stale;
	for (const TObjectPtr<UAsdaFxLightComponent>& C : Candidates)
	{
		if (!IsValid(C)) { continue; }
		AAsdaEffectActor* Fx = C->GetEffect();
		if (!IsValid(Fx)) { continue; }
		bool bAlready = false;
		Seen.Add(Fx, &bAlready);
		if (bAlready) { continue; }

		const FAsdaFxLightRule* R = C->GetRule();
		const FAsdaFxLightSpec Spec = UAsdaFxLightComponent::ResolveSpec(Fx, R);
		// ЗАКАЗАНО действующей спецификацией (та же функция, что строит набор при заведении)
		TArray<int32, TInlineAllocator<8>> Want;
		ComputeDesiredSet(Fx, R, Spec, Want);
		// ЕСТЬ сейчас у этого эффекта — номера эмиттеров в том же устойчивом порядке
		// ★ МЕЛК-4 ревизии: инлайн-аллокатор, как у соседних Seen/Stale (обход идёт каждый кадр)
		TArray<UAsdaFxLightComponent*, TInlineAllocator<16>> Have;
		Fx->GetComponents<UAsdaFxLightComponent>(Have);
		TArray<int32, TInlineAllocator<8>> HaveIdx;
		bool bAnyLit = false;
		for (const UAsdaFxLightComponent* H : Have)
		{
			if (!IsValid(H)) { continue; }
			HaveIdx.Add(H->GetEmitterIndex());
			if (H->IsLit()) { bAnyLit = true; }
		}
		HaveIdx.Sort();

		// ═══ ★★★ ВОЛНА «форма-2» (21.09.2026), п. 2 ревизии: ПЕРЕВЫБОР ЦЕЛИКОМ ТЁМНОГО НАБОРА ═════
		//   Довод и замер — у ключа asda.Fx.Light.DarkRepickSec выше по файлу. Коротко: статический
		//   отбор по узлам у эффекта из одинаковых эмиттеров даёт произвольную восьмёрку, и она
		//   может целиком не уметь светить (нет секции) или целиком сидеть в тёмной фазе. Здесь
		//   это ЛОВИТСЯ («весь набор тёмен дольше отсрочки») и ЛЕЧИТСЯ один раз — живым отбором.
		//   ★ Только поэмиттерный охват: у общего света набор из одного элемента, перевыбирать нечего.
		if (Spec.Scope == EAsdaFxLightScope::PerEmitter && DarkRepickSec() > 0.f && HaveIdx.Num() > 0)
		{
			FAsdaFxDarkState& DS = DarkStates.FindOrAdd(FObjectKey(Fx));
			if (bAnyLit) { DS.DarkSec = 0.f; DS.Backoff = 0; }
			else         { DS.DarkSec += Dt; }
			if (DS.DarkSec >= DarkRepickSec() * (float)(DS.Backoff + 1))
			{
				TArray<int32, TInlineAllocator<8>> Live;
				ComputeDesiredSet(Fx, R, Spec, Live, /*bPreferLive*/ true);
				if (Live.Num() > 0 && Live != HaveIdx)
				{
					DS.Pinned = Live;
					DS.PinnedCap = ResolvePerEffect();
					Want = Live;                 // заведение пойдёт по НЁМ (шпилька уже стоит)
					++LastDarkRepicks;
					if (GAsdaFxLightTraceRegister != 0)
					{
						UE_LOG(LogAsdaFxLight, Log,
							TEXT("[перевыбор набора] %-38s весь набор тёмен %.2f с — пересобираю по ЖИВЫМ эмиттерам: было [%s], стало [%s]"),
							*ResolveJebName(Fx), DS.DarkSec, *ListOf(HaveIdx), *ListOf(Live));
					}
				}
				// ★★★ РЕВИЗИЯ (СРЕДН-2): «ПЕРЕСОБИРАЕТСЯ ОДИН РАЗ» БЫЛО НЕВЕРНО И ПО КОДУ, И ПО
				//   ДАННЫМ. Сброс счётчика стоял БЕЗУСЛОВНО, поэтому у вечно тёмного эффекта
				//   перевыбор повторялся каждые DarkRepickSec (в данных ревизии `wholicb03`
				//   перевыбран ДВАЖДЫ за прогон). Моргания это не даёт — ветка работает, только
				//   когда не горит НИЧЕГО, — но перезаведение компонентов до двух раз в секунду
				//   никто не мерил, и обещать «один раз» нельзя.
				//   Теперь пауза РАСТЁТ: каждый безрезультатный или повторный перевыбор удваивает
				//   отсрочку до потолка в 16 попыток (≈8 с при поставке 0.5 с). Эффект, которому
				//   живой отбор помог, гаснуть перестаёт — и счётчик обнуляется сам по `bAnyLit`
				//   вместе с паузой.
				DS.DarkSec = 0.f;
				DS.Backoff = (int32)FMath::Min(DS.Backoff * 2 + 1, 15);
			}
		}
		else if (Spec.Scope != EAsdaFxLightScope::PerEmitter)
		{
			DarkStates.Remove(FObjectKey(Fx));
		}

		if (HaveIdx != Want)
		{
			// ★★★ fix15 (СРЕДН-2 ревизии fix14): РАСХОЖДЕНИЕ, КОТОРОЕ ЗАВЕДЕНИЕ НЕ МОЖЕТ ЗАКРЫТЬ.
			//   RegisterEffect заводит недостающие света, пока есть место в предохранителе
			//   `Предел кандидатов`, и при упоре делает `break` (см. там же). На карте, где потолок
			//   достигнут, у части эффектов набор НАВСЕГДА короче заказа — а сверка объявляла бы это
			//   расхождением КАЖДЫЙ КАДР: заведение по недособранным эффектам плюс SelectTimer = 1.e6f,
			//   то есть полный отбор каждым кадром. Прежнее условие fix14 (три скаляра) такой петли не
			//   давало: «мало светов» оно не ловило вовсе, ловило только «ни одного» и «больше кошелька».
			//   ★ ПРОПУСКАЕМ ТОЛЬКО ЧИСТУЮ НЕДОСТАЧУ. Если у эффекта есть ЛИШНИЙ свет (номер, которого
			//   в заказе нет — например, владелец погасил свечение эмиттеру), заведение ему всё ещё
			//   нужно: оно этот свет снесёт, и место в предохранителе освободится для соседа. Поэтому
			//   условие — «всё, что есть, заказано» И «заказано больше, чем есть» И «мест больше нет».
			bool bExtra = false;
			for (const int32 I : HaveIdx)
			{
				if (!Want.Contains(I)) { bExtra = true; break; }
			}
			const bool bShortByCeiling = GAsdaFxLightCeilingSkip != 0
				&& !bExtra
				&& HaveIdx.Num() < Want.Num()
				&& Candidates.Num() >= ResolveMaxCandidates();
			if (bShortByCeiling)
			{
				++CeilingShortNum;
			}
			else
			{
				Stale.Add({ Fx, GAsdaFxLightTraceRegister != 0 ? ListOf(Want) : FString(),
				                GAsdaFxLightTraceRegister != 0 ? ListOf(HaveIdx) : FString() });
			}
		}
	}
	DarkRepicksSum += LastDarkRepicks;
	// ★ «форма-2», п. 2: ПАМЯТЬ ПЕРЕВЫБОРА НЕ КОПИТСЯ. Эффекты умирают, ключи остаются — и на карте
	//   со сменой сотен эффектов карта DarkStates росла бы без предела. Чистим, когда записей стало
	//   заметно больше, чем эффектов у кандидатов: обход редкий и по мёртвым ключам, не по кадру.
	if (DarkStates.Num() > FMath::Max(64, Candidates.Num()))
	{
		for (auto It = DarkStates.CreateIterator(); It; ++It)
		{
			if (!Seen.Contains(Cast<AAsdaEffectActor>(It.Key().ResolveObjectPtr()))) { It.RemoveCurrent(); }
		}
	}
	// ★ fix15: ОДНО СЛОВО ВЛАДЕЛЬЦУ, А НЕ МОЛЧАНИЕ И НЕ ПОТОК. Пропуск выше делает петлю невидимой —
	//   но «часть эффектов светит не полностью» владелец обязан узнать, иначе он будет искать причину
	//   в числах семейства. Сообщение выдаётся ОДИН РАЗ на подъём: пока потолок держится, счётчик
	//   ненулевой и повтора нет; как только он опустится до нуля, признак снимается, и следующий
	//   упор в потолок снова будет новостью.
	if (CeilingShortNum > 0 && !bCeilingShortReported)
	{
		bCeilingShortReported = true;
		UE_LOG(LogAsdaFxLight, Warning,
			TEXT("[эффект-света] ПРЕДЕЛ КАНДИДАТОВ ДОСТИГНУТ (%d из %d): у %d эффект(ов) светов меньше заказанного, и завести недостающие негде. Это не дефект настроек — поднимите «Предел кандидатов» (UAsdaFxLightSettings::MaxCandidates) или уменьшите кошелёк на эффект."),
			Candidates.Num(), ResolveMaxCandidates(), CeilingShortNum);
	}
	else if (CeilingShortNum == 0)
	{
		bCeilingShortReported = false;
	}
	// Заведение правит массив Candidates — поэтому ПОСЛЕ обхода, а не внутри него
	for (const FStale& St : Stale)
	{
		if (GAsdaFxLightTraceRegister != 0)
		{
			UE_LOG(LogAsdaFxLight, Log,
				TEXT("[сверка набора] %-38s набор светов разошёлся с действующей спецификацией: заказано [%s], было [%s]%s — завожу заново"),
				*ResolveJebName(St.Fx), *St.Want, *St.Have,
				bForce ? TEXT(" (принудительно: сменился уровень качества)") : TEXT(""));
		}
		RegisterEffect(St.Fx);
		SelectTimer = 1.e6f;   // место в бюджете надо раздать заново уже этим кадром
	}

	// ★★★ fix16: ПОГАСШИЕ — ЗДЕСЬ ЖЕ, НО ОТДЕЛЬНЫМ ОБХОДОМ. Обход выше идёт по Candidates и
	//   эффект без единого света не видит в принципе; второй обход идёт по списку Extinguished.
	//   Вызов ПОСЛЕ заведения расходящихся: оно правит Candidates, а проверка погасших читает их
	//   число (упор в предохранитель). Свой ключ отката — `asda.Fx.Light.EmptyRecheck 0`.
	RecheckExtinguished();
}

// ═══ ★★★ fix14 (С-3/С-4 ревизии fix13): КАКОЙ НАБОР СВЕТОВ ЗАКАЗАН ДЕЙСТВУЮЩЕЙ СПЕЦИФИКАЦИЕЙ ═════
//
//   Тело перенесено из RegisterEffect ДОСЛОВНО (плюс одна оптимизация, помеченная ниже) — ровно
//   затем, чтобы сверка набора (ReconcileSets) спрашивала «что должно быть» ТОЙ ЖЕ функцией,
//   которой заведение строит набор. Разойдись эти две — и сверка стала бы каждый кадр «чинить»
//   набор в свою сторону, а заведение возвращать обратно (закон port-shared-points: правило одно,
//   применений два, и второе обязано быть буквальным).
//
//   ВЫХОД: номера эмиттеров, которым положен свой свет; ровно один элемент INDEX_NONE = один общий
//   свет на эффект; пустой список = света у эффекта нет. Порядок — возрастание номера, всегда.
void UAsdaFxLightSubsystem::ComputeDesiredSet(const AAsdaEffectActor* Fx, const FAsdaFxLightRule* Rule,
	const FAsdaFxLightSpec& Spec, TArray<int32, TInlineAllocator<8>>& OutWant, bool bPreferLive) const
{
	OutWant.Reset();
	if (!IsValid(Fx) || !Spec.bEnabled) { return; }
	// ★ Светит ГЕОМЕТРИЯ эмиттеров — нет эмиттеров, нет и света, при любом охвате. Правило здесь,
	//   а не отдельным выходом в RegisterEffect: заведение и сверка обязаны отвечать одинаково,
	//   иначе сверка каждый кадр требовала бы свет, которого заведение не создаёт (вечный дребезг).
	if (Fx->Emitters.Num() == 0) { return; }
	if (Spec.Scope != EAsdaFxLightScope::PerEmitter)
	{
		OutWant.Add(INDEX_NONE);
		return;
	}

	// ★ ОТБОР ЭМИТТЕРОВ ПО ВКЛАДУ, а не по номеру. Вклад — тот же СТАТИЧЕСКИЙ вес, которым
	//   PickDonorEmitter выбирает донора (яркость × альфа × размер по узлам), потому что
	//   решение принимается ДО первого кадра, когда живой геометрии ещё нет ни у кого.
	//   Порог отбора и порядок обязаны быть УСТОЙЧИВЫМИ: иначе набор светов менялся бы от
	//   пересканирования к пересканированию, и «мигает при подходе» стало бы штатным.
	struct FCand { int32 Idx; double W; };
	TArray<FCand, TInlineAllocator<8>> Cands;
	// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 1 ревизии: ЭМИТТЕР НУЛЕВОГО РАЗМЕРА СВЕТИТЬ НЕ МОЖЕТ.
	//   Светит ГЕОМЕТРИЯ. У квада, у которого ВО ВСЕХ узлах трека размер нулевой, секция строится
	//   вырожденной — четыре вершины в одной точке, — и свет от неё получается «нижний зажим радиуса
	//   ИЗ ТОЧКИ» при отсутствующей картинке. Замер: `ch1_a_4th_mg_02_2`, эмиттер #5 `Bil_002`,
	//   SizeX = SizeY = 0 во ВСЕХ десяти снятых моментах периода — он занимал место в кошельке и
	//   давал ровно такой свет (10 вырожденных строк дампа из 60 в КАЖДОМ из пяти прогонов).
	//   ★ КОЛЬЦО (0xE) И МЕШ ИСКЛЮЧЕНЫ ИЗ ПРОВЕРКИ НАРОЧНО: у ленты-кольца размер лежит в каналах
	//     RingTopR/RingBotR/RingScale, а не в SizeX/SizeY (у `mon_5mareadamstun` кольца имеют
	//     SizeX 50 и RingTopR 0 — проверять их этим полем значило бы врать), у меш-эмиттера размер
	//     задаёт сам меш. Правило смотрит ТОЛЬКО на тот случай, где оно доказуемо.
	//   ★ СТРАХОВКА: если фильтр не оставил никого, берём нефильтрованный список — свет «какой-то»
	//     лучше, чем молчание (и ровно так же ведёт себя эффект до этой волны).
	auto CanEverLight = [](const FAsdaFxEmitter& E) -> bool
	{
		if (E.bRing || E.bMeshEmitter || E.Nodes.Num() == 0) { return true; }
		for (const FAsdaFxNode& N : E.Nodes)
		{
			if (FMath::Max(N.SizeX, N.SizeY) > KINDA_SMALL_NUMBER) { return true; }
		}
		return false;
	};
	int32 SkippedZeroSize = 0;
	for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
	{
		const FAsdaFxLightSpec ES = UAsdaFxLightComponent::ResolveSpec(Fx, Rule, i);
		if (!ES.bEnabled) { continue; }              // «света у этого эмиттера нет» / выключен свой набор
		if (!CanEverLight(Fx->Emitters[i])) { ++SkippedZeroSize; continue; }
		Cands.Add({ i, 0.0 });
	}
	if (Cands.Num() == 0 && SkippedZeroSize > 0)
	{
		for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
		{
			const FAsdaFxLightSpec ES = UAsdaFxLightComponent::ResolveSpec(Fx, Rule, i);
			if (ES.bEnabled) { Cands.Add({ i, 0.0 }); }
		}
	}
	const int32 Cap = ResolvePerEffect();

	// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 2 ревизии: ПРИШПИЛЕННЫЙ СОСТАВ ОТВЕЧАЕТ ПЕРВЫМ.
	//   Если набор уже пересобирали по живым эмиттерам, заказом считается ИМЕННО ОН — иначе сверка
	//   следующего кадра вернула бы статическую восьмёрку и дребезг пошёл бы по кругу (см. шапку
	//   FAsdaFxDarkState). Шпилька перестаёт действовать сама, когда сменился кошелёк уровня или
	//   когда пришпиленный эмиттер потерял право на свет.
	if (!bPreferLive)
	{
		if (const FAsdaFxDarkState* P = DarkStates.Find(FObjectKey(Fx)))
		{
			if (P->Pinned.Num() > 0 && P->PinnedCap == Cap)
			{
				bool bOk = true;
				for (const int32 I : P->Pinned)
				{
					if (!Cands.ContainsByPredicate([I](const FCand& C) { return C.Idx == I; })) { bOk = false; break; }
				}
				if (bOk)
				{
					OutWant = P->Pinned;
					OutWant.Sort();
					return;
				}
			}
		}
	}

	// ★ fix14, единственное отличие от прежнего тела: ВЕСА СЧИТАЮТСЯ, ТОЛЬКО ЕСЛИ ОНИ РЕШАЮТ.
	//   Подходящих эмиттеров не больше кошелька — берутся ВСЕ, и порядок сортировки ни на что не
	//   влияет (итог всё равно сортируется по номеру). Перебор узлов каждого эмиттера при этом
	//   пропускается целиком — а ради сверки каждый кадр (fix14) это и есть основная цена.
	if (Cands.Num() > Cap)
	{
		for (FCand& C : Cands)
		{
			double W = AsdaFxLight::EmitterWeight(Fx->Emitters[C.Idx]);   // ★ В1-C F15: общая формула веса (AsdaEffectActor.h)
			// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 2 ревизии: ЖИВАЯ ПРИБАВКА ПРИ ПЕРЕВЫБОРЕ.
			//   Статический вес считается ПО УЗЛАМ, то есть по данным ассета, и у эффектов из
			//   десятков одинаковых эмиттеров он у всех РАВЕН: `ch1_a_4th_ft_06_1` — 24 эмиттера
			//   S_001…S_024 с SizeX 60 и альфой 1 у каждого. Кошелёк тогда достаётся произвольной
			//   восьмёрке, и замер показал, чем это кончается: пятеро из восьми не построили секцию
			//   НИ В ОДНОМ из пяти замеров периода — они занимали места, не умея светить вовсе.
			//   Прибавка ставится ТОЛЬКО на перевыборе (bPreferLive) и по ДВУМ живым признакам
			//   сразу: секция построена И альфа узла ненулевая. Порядок величин разнесён нарочно:
			//   ручной эмиттер (1e12) > живой (1e9) > статический вес — иначе живая прибавка
			//   выбила бы из набора эмиттер, который владелец настроил руками.
			if (bPreferLive)
			{
				bool bStopped = false; float LT = 0.f, A = 0.f;
				const bool bRt = Fx->GetEmitterRuntime(C.Idx, bStopped, LT, &A);
				if (bRt && !bStopped && A > KINDA_SMALL_NUMBER && Fx->EmitterSectionVertCount(C.Idx) > 0)
				{
					W += 1.e9;
				}
			}
			// ★ Эмиттер, у которого владелец ЯВНО задал свои числа, в отборе не проигрывает
			//   вообще никому: он настраивал его руками именно ради этого света.
			if (Fx->Emitters[C.Idx].Glow.Source == EAsdaFxEmitterGlowSource::Manual) { W += 1.e12; }
			C.W = W;
		}
		Cands.Sort([](const FCand& A, const FCand& B) { return A.W > B.W; });
	}
	for (int32 k = 0; k < Cands.Num() && k < Cap; ++k) { OutWant.Add(Cands[k].Idx); }
	// Устойчивый порядок номеров: набор не должен зависеть от того, как сортировка разложила равные веса
	OutWant.Sort();
}

void UAsdaFxLightSubsystem::RegisterEffect(AAsdaEffectActor* Fx)
{
	if (!IsValid(Fx)) { return; }
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	if (!S->bEnabled) { return; }

	const FString Jeb = ResolveJebName(Fx);
	// Правило-семейство: первое подходящее СВЕРХУ ВНИЗ. Последнее правило таблицы — всеобщее `.`,
	// поэтому «правила нет» бывает только у эффекта с ПУСТЫМ именем .jeb
	const FAsdaFxLightRule* R = Jeb.IsEmpty() ? nullptr : S->FindRule(Jeb);
	const int32 RuleIdx = R ? (int32)(R - S->Families.GetData()) : INDEX_NONE;

	// ★ Ни одно наше действие не должно пометить ассет карты изменённым (см. хранитель выше).
	//   Хранитель заводится РАНЬШЕ зеркала и живёт до конца функции.
	FAsdaFxLightPackageDirtyGuard Guard(Fx);

#if WITH_EDITOR
	// ★ ЗЕРКАЛО ДЕЙСТВУЮЩИХ ЧИСЕЛ В ПАНЕЛЬ ДЕТАЛЕЙ — только окно редактора и только пока не правили
	//   руками. Тело вынесено в MirrorSpecToAsset (fix13): тем же зеркалом пользуется стенд, и
	//   расходиться этим двум применениям нельзя (закон port-shared-points).
	if (GIsEditor && GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		MirrorSpecToAsset(Fx, R);
	}
#endif

	// ★★★ fix14 (МЕЛК-7 ревизии fix13): ВЫХОДА «У ЭФФЕКТА НЕТ ЭМИТТЕРОВ» ЗДЕСЬ БОЛЬШЕ НЕТ.
	//   Он стоял ПЕРВОЙ строкой функции, то есть раньше зеркала, — и эффект без эмиттеров показывал
	//   владельцу в панели деталей C++-заглушку структуры (7.5 кд, точка, один на эффект) вместо
	//   действующих чисел семейства. Теперь зеркало отрабатывает выше, а «света у такого эффекта
	//   быть не может» перенесено в ComputeDesiredSet: заказ выходит пустым, набор приводится к
	//   пустому (лишние компоненты сносятся), новых не заводится. Заведение и сверка отвечают
	//   одинаково, и вечного дребезга «сверка требует — заведение не создаёт» не возникает.
	//   ★ Случай «правила семейства нет» зеркалом не лечится вовсе: брать числа неоткуда, и
	//   ResolveSpec тогда честно возвращает поля самого ассета (ветка «ассет:поле») — панель
	//   показывает ровно то, что действует, а не заглушку постороннего семейства.

	// ★ ДЕЙСТВУЮЩИЕ числа решают, быть ли кандидату: ручные поля эффекта важнее дефолтов семейства
	//   (владелец мог включить свет дыму или выключить фонарю).
	// ★ fix13: рядом с числами — ВЕТКА, которая их дала (см. EAsdaFxSpecSource)
	UAsdaFxLightComponent::EAsdaFxSpecSource SpecSrc = UAsdaFxLightComponent::EAsdaFxSpecSource::NoEffect;
	const FAsdaFxLightSpec Spec = UAsdaFxLightComponent::ResolveSpec(Fx, R, INDEX_NONE, &SpecSrc);

	// ═══ ★★★ ВОЛНА Ф8 (10.09.2026): У ЭФФЕКТА МОЖЕТ БЫТЬ НЕ ОДИН СВЕТ, А ПО ОДНОМУ НА ЭМИТТЕР ═══
	//   Раньше здесь работала пара «нашёлся компонент — выходим / не нашёлся — заводим». Теперь надо
	//   привести НАБОР компонентов к нужному: у режима «один на эффект» это ровно один компонент с
	//   EmitterIndex = INDEX_NONE, у режима «свой у каждого» — по компоненту на отобранный эмиттер.
	//   Приведение, а не пересоздание: владелец крутит переключатель на живой сцене, и снос всех
	//   светов эффекта на каждой правке гасил бы картинку на кадр (а на людной карте — заметно).

	// Кого хотим видеть (номера эмиттеров; INDEX_NONE = общий свет эффекта).
	// ★ fix14: тело вынесено в ComputeDesiredSet — тем же списком сверяется набор в ReconcileSets
	TArray<int32, TInlineAllocator<8>> Want;
	ComputeDesiredSet(Fx, R, Spec, Want);

	// Что уже есть
	TArray<UAsdaFxLightComponent*, TInlineAllocator<16>> Have;
	Fx->GetComponents<UAsdaFxLightComponent>(Have);

	// ★ fix13: след заведения (asda.Fx.Light.TraceRegister 1). Печатается ДО правки набора — видно
	//   и то, что было, и то, что заказано: «8 → 1» на одном и том же ассете ищется именно так.
	if (GAsdaFxLightTraceRegister != 0)
	{
		UE_LOG(LogAsdaFxLight, Log,
			TEXT("[заведение] %-38s правило=%-16s числа ИЗ: %-16s замок=%s вкл=%d охват=%s кошелёк=%d ⇒ заказано светов %d (было %d)"),
			*Jeb, R ? *R->Id : TEXT("<нет>"), UAsdaFxLightComponent::SpecSourceName(SpecSrc),
			Fx->FxLight.bManual ? TEXT("да") : TEXT("—"), (int32)Spec.bEnabled,
			Spec.Scope == EAsdaFxLightScope::PerEmitter ? TEXT("по эмиттерам") : TEXT("один на эффект"),
			ResolvePerEffect(), Want.Num(), Have.Num());
	}

	// ★ fix16: СКОЛЬКО СВЕТОВ ОСТАНЕТСЯ У ЭФФЕКТА ПОСЛЕ ЭТОГО ЗАВЕДЕНИЯ. Считаем здесь, а не
	//   перечитываем компоненты актора в конце: только что снесённый компонент ещё может числиться
	//   у владельца, и «ноль светов» оказалось бы неотличимо от «один, но мёртвый». Число нужно
	//   ровно затем, чтобы положить эффект в список погасших (см. Extinguished в заголовке).
	int32 LightsLeft = 0;

	// Лишние — снести
	for (UAsdaFxLightComponent* H : Have)
	{
		if (!IsValid(H)) { continue; }
		if (Want.Contains(H->GetEmitterIndex())) { ++LightsLeft; continue; }
		H->SetAdmitted(false);
		H->DestroyComponent();
		Candidates.RemoveAll([H](const TObjectPtr<UAsdaFxLightComponent>& C) { return C == H; });
		LeaseAge.Remove(H);
	}

	// Недостающие — завести
	for (int32 EmIdx : Want)
	{
		bool bFound = false;
		for (UAsdaFxLightComponent* H : Have)
		{
			if (IsValid(H) && H->GetEmitterIndex() == EmIdx) { bFound = true; break; }
		}
		if (bFound) { continue; }
		// Предохранитель на карту. ★ fix15 (СРЕДН-2): с этого `break` набор эффекта остаётся КОРОЧЕ
		//   заказа, и закрыть недостачу заведение уже не может ничем — поэтому поэлементная сверка
		//   (ReconcileSets) такую недостачу расхождением НЕ считает, иначе получилась бы покадровая
		//   петля «сверка требует → заведение упирается → сверка требует снова».
		if (Candidates.Num() >= ResolveMaxCandidates()) { break; }

		UAsdaFxLightComponent* C = NewObject<UAsdaFxLightComponent>(Fx, UAsdaFxLightComponent::StaticClass(), NAME_None, RF_Transient);
		if (!C) { break; }
		C->SetupAttachment(Fx->GetRootComponent());
		C->RegisterComponent();
		C->SetupForEffect(Fx, RuleIdx, EmIdx);
		Candidates.Add(C);
		++LightsLeft;
	}

	// ═══ ★★★ fix16: ЭФФЕКТ БЕЗ ЕДИНОГО СВЕТА — В СПИСОК ПОГАСШИХ ══════════════════════════════
	//   Ровно здесь, в конце единственного места, где набор строится, и по ФАКТУ («светов ноль»),
	//   а не по причине. Причин остаться без света три, и все три ведут к одной дыре:
	//     * заказ пуст (погашено свечение всем эмиттерам, снят «свет включён», нет эмиттеров);
	//     * заказ непуст, но завести нечего — упёрся предохранитель `Предел кандидатов`;
	//     * заказ непуст, а NewObject не дал объекта.
	//   Во всех трёх эффект вычеркнут из Candidates, то есть невидим для сверки набора. Список
	//   Extinguished — единственное, что о нём ещё помнит; RecheckExtinguished спрашивает по кругу,
	//   не пора ли ему светить снова.
	//   ★ Симметрия обязательна: как только свет появился, эффект из списка УХОДИТ. Иначе список
	//   рос бы до размера карты, и потолок проверок на кадр перестал бы означать «за кадр проверены
	//   все погасшие».
	if (LightsLeft <= 0)
	{
		Extinguished.AddUnique(Fx);
	}
	else
	{
		Extinguished.RemoveSingleSwap(Fx);
	}
}

// ═══ ★★★ ВОЛНА fix16 (20.09.2026): ПОГАСШИЕ ЭФФЕКТЫ — ПО КРУГУ, С ПОТОЛКОМ НА КАДР ════════════
//
//   ЧТО ЛЕЧИМ. Сверка набора (ReconcileSets) ходит по Candidates, то есть по компонентам света.
//   Эффект, у которого светов не осталось, в этом обходе не участвует — и вернуть себе свет не
//   может ничем, кроме полного пересканирования мира (asda.Fx.Light.Rescan) или собственного
//   переспавна. Замер до правки на alpen: погасили свечение всем 83 эмиттерам — 8 светов стали 0;
//   вернули свечение всем — 0 осталось нулём; Rescan — снова 8. То есть данные были целы всё
//   время, мешала ровно слепота обхода.
//
//   ПОЧЕМУ КРУГ И ПОТОЛОК, А НЕ «ПРОВЕРИТЬ ВСЕХ КАЖДЫЙ КАДР». Проверка одного погасшего — это
//   поиск правила по имени .jeb плюс ResolveSpec, а даёт она почти всегда «нет, светить
//   по-прежнему нечему».
//   ★ fix17 (МЕЛК-6): ПРЕЖНЕЕ ОБОСНОВАНИЕ ПОТОЛКА БЫЛО НЕТОЧНЫМ. Оно говорило «а у охвата „по
//   эмиттерам“ ещё и ResolveSpec на каждый эмиттер (у defminb03 их 83)» так, будто это цена
//   КАЖДОЙ проверки списка. На деле ComputeDesiredSet выходит ДО цикла по эмиттерам в трёх
//   случаях из четырёх — выключен свет у самого эффекта (`Spec.bEnabled` = 0, то есть семейство
//   без света), эмиттеров нет вовсе, охват «один на эффект», — и такому погасшему проверка стоит
//   ровно FindRule + один ResolveSpec. Цикл по всем 83 эмиттерам бывает в ОДНОМ случае: свет у
//   эффекта включён, охват «по эмиттерам», а погашены все эмиттеры — то есть ровно в том, ради
//   которого механизм и заведён, и ровно у того, кого владелец правит руками. Потолок нужен
//   поэтому ХУДШИМ случаем, а не типичным; на типичном он просто не мешает.
//   ★ Состав списка по этим ветвям (на alpen их 64) прогоном не разбирался — НЕ ПРОВЕРЕНО.
//   За кадр проверяется не больше GAsdaFxLightEmptyRecheck эффектов, и следующий кадр продолжает
//   с того места, где кончил предыдущий: цена ровная (не зависит от длины списка), а очередь
//   доходит до каждого.
//   ★ fix17 (МЕЛК-1): «ПОЛНЫЙ КРУГ ЗА N/ПОТОЛОК КАДРОВ» — ОЦЕНКА СНИЗУ, А НЕ РАВЕНСТВО. И
//   RemoveSingleSwap в RegisterEffect, и RemoveAtSwap здесь переставляют ХВОСТОВОЙ элемент на
//   освободившееся место, а оно может оказаться уже пройденным курсором — тогда переставленный
//   эффект в этом круге не проверяется и ждёт следующего. Голодания это не даёт: курсор идёт
//   монотонно и круг всегда замыкается, так что верхняя оценка задержки — ДВА круга, а не один.
//
//   ПОЧЕМУ НЕ «ПОДПИСАТЬСЯ НА ПРАВКУ». Ровно по тому же доводу, по которому fix13 завела сверку
//   вместо подписки: писателей полей много (UE-Python `set_editor_property`, Blueprint, наш стенд,
//   конфигуратор свечения), подписка на правку свойств есть только под `#if WITH_EDITOR` и только
//   для мира редактора, а в собранном клиенте её нет вовсе. Сверяем результат, а не ловим писателя.
void UAsdaFxLightSubsystem::RecheckExtinguished()
{
	if (GAsdaFxLightEmptyRecheck <= 0 || Extinguished.Num() == 0) { return; }
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();

	// ★ Упор в предохранитель: заводить всё равно негде, и звать заведение по кругу значило бы
	//   жечь кадр впустую. Эффекты остаются в списке — как только предел поднимут, очередь дойдёт.
	//   Условие то же, что у пропуска недостачи в ReconcileSets (закон port-shared-points).
	const bool bCeilingFull = GAsdaFxLightCeilingSkip != 0 && Candidates.Num() >= ResolveMaxCandidates();

	// Заведение правит и Candidates, и сам список Extinguished — поэтому сначала собираем, кого
	// заводить, и только потом заводим (тот же приём, что в ReconcileSets)
	TArray<AAsdaEffectActor*, TInlineAllocator<8>> Wake;
	const int32 Budget = FMath::Min(GAsdaFxLightEmptyRecheck, Extinguished.Num());
	for (int32 n = 0; n < Budget; ++n)
	{
		if (Extinguished.Num() == 0) { break; }
		if (ExtinguishedCursor >= Extinguished.Num()) { ExtinguishedCursor = 0; }
		const int32 At = ExtinguishedCursor;
		AAsdaEffectActor* Fx = Extinguished[At].Get();
		if (!IsValid(Fx))
		{
			// Актор умер (эффект навыка живёт секунду) — вычищаем тем же обходом, курсор остаётся
			// на месте: следующим будет тот, кто занял освободившийся номер
			Extinguished.RemoveAtSwap(At, EAllowShrinking::No);
			continue;
		}
		++ExtinguishedCursor;
		if (bCeilingFull) { continue; }

		// ТОТ ЖЕ заказ, которым строит набор заведение и сверяется сверка
		const FAsdaFxLightRule* R = nullptr;
		{
			const FString Jeb = ResolveJebName(Fx);
			R = Jeb.IsEmpty() ? nullptr : S->FindRule(Jeb);
		}
		const FAsdaFxLightSpec Spec = UAsdaFxLightComponent::ResolveSpec(Fx, R);
		TArray<int32, TInlineAllocator<8>> Want;
		ComputeDesiredSet(Fx, R, Spec, Want);
		if (Want.Num() > 0) { Wake.Add(Fx); }
	}

	for (AAsdaEffectActor* Fx : Wake)
	{
		if (GAsdaFxLightTraceRegister != 0)
		{
			UE_LOG(LogAsdaFxLight, Log,
				TEXT("[возврат погасшего] %-38s условие света снова выполнено (светов было 0) — завожу набор заново"),
				*ResolveJebName(Fx));
		}
		RegisterEffect(Fx);          // он же вычеркнет эффект из Extinguished, если свет появился
		// ★★★ fix17 (МЕЛК-1): ПОЛНЫЙ ОТБОР — ТОЛЬКО ЕСЛИ СВЕТ ДЕЙСТВИТЕЛЬНО ПОЯВИЛСЯ.
		//   ТРЕТИЙ ПУТЬ В ДЫРУ, который условие bCeilingFull выше не ловит: `NewObject` не дал
		//   объекта (RegisterEffect, ветка «Недостающие — завести», там же `break`). Тогда заказ у
		//   эффекта НЕПУСТ, светов по-прежнему ноль, эффект остаётся в Extinguished — и прежний
		//   БЕЗУСЛОВНЫЙ SelectTimer = 1.e6f взводил бы полный отбор кадром на КАЖДОМ круге, то есть
		//   ровно ту петлю, от которой fix15 избавлялась в ReconcileSets. Предохранитель здесь ни
		//   при чём (мест в нём сколько угодно), поэтому пропуск по bCeilingFull мимо.
		//   Признак «свет появился» — УХОД эффекта из списка: его ставит и снимает сам
		//   RegisterEffect по факту «светов ноль». Спрашиваем тот же признак, которым список живёт,
		//   а не заводим второй (закон port-shared-points).
		//   ★ Повторная попытка при этом сохранена нарочно: следующий круг снова позовёт заведение,
		//   и как только объект создастся, свет вернётся сам. Цена попытки ограничена тем же
		//   потолком GAsdaFxLightEmptyRecheck, что и у всех остальных погасших.
		//   ★ Путь практически недостижим: `NewObject` с RF_Transient без внешних ограничений не
		//   возвращает nullptr иначе как при исчерпании памяти, и ЗАМЕРА на нём нет — НЕ ПРОВЕРЕНО
		//   прогоном, закрыто чтением кода.
		if (!Extinguished.Contains(TWeakObjectPtr<AAsdaEffectActor>(Fx)))
		{
			SelectTimer = 1.e6f;     // место в бюджете надо раздать заново уже этим кадром
		}
	}
}
