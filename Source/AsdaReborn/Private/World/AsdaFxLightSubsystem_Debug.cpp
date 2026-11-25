// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaFxLightSubsystem.cpp, дословный перенос): дев-команды: дамп, поэмиттерный дамп, стенд, наблюдение; DumpGates, Dump.
//   Общее для частей — в AsdaFxLightSubsystem_Private.h; шапка и реверс — в AsdaFxLightSubsystem.cpp.
#include "World/AsdaFxLightSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaFxLightComponent.h"
#include "World/AsdaFxLightSettings.h"
#include "World/AsdaVisibilitySubsystem.h"   // ★ Волна Ф9: ОБЩАЯ точка «объект попадает в кадр» (расширенный конус, гистерезис, предсказание по угловой скорости)
#include "UI/AsdaDevMenuSubsystem.h"
#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"     // ★ Ф4: стенд asda.Fx.Light.Stand строит площадку и стенку кубами движка
#include "HAL/IConsoleManager.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif
#include "AsdaFxLightSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaFxLightSubsystem*.cpp

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// Дамп таблицы (по образцу asda.Light.Dump).
static FAutoConsoleCommand CmdAsdaFxLightDump(
	TEXT("asda.Fx.Light.Dump"),
	TEXT("Таблица эффект-светов мира в лог: кандидат, семейство, дистанция, счёт, сила, радиус, экранный след, горит ли."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEngine) { return; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (UWorld* W = Ctx.World())
			{
				if (UAsdaFxLightSubsystem* S = W->GetSubsystem<UAsdaFxLightSubsystem>()) { S->Dump(); }
			}
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 1 ревизии: ПОЭМИТТЕРНЫЙ ДАМП ГЕОМЕТРИИ.
//
//   ЗАЧЕМ. Таблица `asda.Fx.Light.Dump` печатает ИТОГ кадра света: «геом 2 юн при радиусе 160».
//   По нему нельзя отличить ДВЕ РАЗНЫЕ БОЛЕЗНИ, и обе объясняют одно и то же число:
//     (а) эмиттер и правда крошечный — светить по форме нечему;
//     (б) в снятый момент у эмиттера не было живой секции (или она схлопнулась в точку), и свет
//         взял нижний зажим радиуса из точки — ровно то, от чего владелец просил уйти.
//   Различает их только сырьё: сколько у секции вершин, каков её радиус, каков полу-экстент УЗЛА,
//   и — для кольца 0xE — каковы RingTopR/RingBotR/RingScale, которых `max(SizeX, SizeY)` не видит
//   вовсе. Эта команда печатает сырьё; второго расчёта в ней нет ни одного — и секция, и рантайм
//   берутся ТЕМИ ЖЕ методами эффекта, которыми их читает кадр света (закон
//   diagnostic-must-use-real-path).
//
//   asda.Fx.Light.Emitters [подстрока имени .jeb]  — без аргумента печатает ВСЕ эффекты мира.
// ═══════════════════════════════════════════════════════════════════════════════════════════════════
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaFxLightEmitters(
	TEXT("asda.Fx.Light.Emitters"),
	TEXT("Сырьё геометрии по КАЖДОМУ эмиттеру эффекта: вершин секции, радиус секции, полу-габариты "
	     "главных осей, полу-экстент и альфа узла, каналы кольца, распознанная форма, состояние "
	     "поля «1. Свечение эмиттера: откуда числа». asda.Fx.Light.Emitters [подстрока имени .jeb "
	     "или имени актора; регистр НЕ важен, звёздочек нет]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* W)
	{
		if (!W) { return; }
		const FString Filter = InArgs.Num() > 0 ? InArgs[0] : FString();
		const UAsdaFxLightSettings* Settings = UAsdaFxLightSettings::Get();
		int32 Shown = 0;
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			AAsdaEffectActor* Fx = *It;
			if (!IsValid(Fx)) { continue; }
			const FString Jeb = UAsdaFxLightSubsystem::ResolveJebName(Fx);
			if (!Filter.IsEmpty() && !Jeb.Contains(Filter) && !Fx->GetName().Contains(Filter)) { continue; }
			++Shown;
			const FAsdaFxLightRule* R = (Settings && !Jeb.IsEmpty()) ? Settings->FindRule(Jeb) : nullptr;
			const FAsdaFxLightSpec Base = UAsdaFxLightComponent::ResolveSpec(Fx, R, INDEX_NONE);
			// Какие номера эмиттеров реально несут свет ПРЯМО СЕЙЧАС — берём с живых компонентов,
			// а не пересчитываем заказ: заказ и факт расходятся ровно в тех случаях, ради которых
			// команда и заведена.
			TArray<UAsdaFxLightComponent*> Have;
			Fx->GetComponents<UAsdaFxLightComponent>(Have);
			TSet<int32> LitIdx, SetIdx;
			for (const UAsdaFxLightComponent* H : Have)
			{
				if (!IsValid(H)) { continue; }
				SetIdx.Add(H->GetEmitterIndex());
				if (H->IsLit()) { LitIdx.Add(H->GetEmitterIndex()); }
			}
			UE_LOG(LogAsdaFxLight, Log,
				TEXT("=== эмиттеры %s (%s): всего %d, охват %s, светов у эффекта %d (горит %d) ==="),
				*Jeb, *Fx->GetName(), Fx->Emitters.Num(),
				Base.Scope == EAsdaFxLightScope::PerEmitter ? TEXT("свой у каждого эмиттера") : TEXT("один на эффект"),
				Have.Num(), LitIdx.Num());
			UE_LOG(LogAsdaFxLight, Log,
				TEXT("  %4s %-22s %6s %5s %6s %7s %8s %8s %9s %9s %9s %9s %9s %-10s %-14s %s"),
				TEXT("#"), TEXT("имя"), TEXT("тип"), TEXT("коль"), TEXT("стоп"), TEXT("t,с"),
				TEXT("альфа"), TEXT("SizeX"), TEXT("SizeY"), TEXT("вершин"), TEXT("R секц"),
				TEXT("полуA"), TEXT("полуB"), TEXT("полуN/кольцо"), TEXT("форма"), TEXT("свет эмиттера"));
			for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
			{
				const FAsdaFxEmitter& E = Fx->Emitters[i];
				bool bStopped = false;
				float LifeTimer = 0.f, NodeAlpha = 0.f, HalfExt = 0.f;
				FLinearColor NodeCol = FLinearColor::White;
				const bool bRt = Fx->GetEmitterRuntime(i, bStopped, LifeTimer, &NodeAlpha, &HalfExt, &NodeCol);
				FVector C; float SecR = -1.f; int32 Verts = 0;
				const bool bSec = Fx->EmitterSectionStats(i, C, SecR, Verts);
				FVector FC, FA, FB, FN, FH;
				const bool bFrame = Fx->EmitterSectionFrame(i, FC, FA, FB, FN, FH);
				// Узел ТОГО ЖЕ момента — те же SizeX/SizeY, из которых считается полу-экстент,
				// плюс каналы кольца: у ленты 0xE размер лежит в них, а НЕ в SizeX/SizeY.
				FAsdaFxNode Cur;
				Fx->GetEmitterNodeNow(i, Cur);
				const FAsdaFxLightSpec ES = UAsdaFxLightComponent::ResolveSpec(Fx, R, i);
				const EAsdaFxLightShape Sh = UAsdaFxLightComponent::ClassifyEmitterShape(Fx, i, ES);
				const FString RingWord = E.bRing
					? FString::Printf(TEXT("R%.0f/%.0f×%.2f h%.0f"), Cur.RingTopR, Cur.RingBotR, Cur.RingScale, Cur.RingH)
					: (bFrame ? FString::Printf(TEXT("%.1f"), FH.Z) : FString(TEXT("—")));
				const TCHAR* GlowWord =
					E.Glow.Source == EAsdaFxEmitterGlowSource::Off ? TEXT("СНЯТО владельцем")
					: E.Glow.Source == EAsdaFxEmitterGlowSource::Manual ? TEXT("задано вручную")
					: TEXT("как у эффекта");
				UE_LOG(LogAsdaFxLight, Log,
					TEXT("  %4d %-22s %6d %5s %6s %7.2f %8.3f %8.1f %9.1f %9d %9.1f %9.1f %9.1f %-10s %-14s %s%s%s"),
					i, *E.Name.Left(22), E.JebType, E.bRing ? TEXT("да") : TEXT("—"),
					(!bRt || bStopped) ? TEXT("ДА") : TEXT("—"), LifeTimer, NodeAlpha,
					Cur.SizeX, Cur.SizeY, Verts, SecR,
					bFrame ? (float)FH.X : -1.f, bFrame ? (float)FH.Y : -1.f, *RingWord,
					*UEnum::GetDisplayValueAsText(Sh).ToString(), GlowWord,
					SetIdx.Contains(i) ? TEXT("  [в наборе]") : TEXT(""),
					LitIdx.Contains(i) ? TEXT(" ГОРИТ") : TEXT(""));
				// ★ ВЫРОЖДЕНИЕ НАЗЫВАЕТСЯ ВСЛУХ: «секция есть, но она в точку» — это НЕ то же самое,
				//   что «секции нет». Первое даёт свету нижний зажим радиуса ИЗ ТОЧКИ при живой
				//   картинке эффекта, второе гасит свет честным гейтом.
				// ★★★ РЕВИЗИЯ (мелкое): ПРЕДУПРЕЖДЕНИЕ ОБЯЗАНО СЧИТАТЬ ТЕМИ ЖЕ ДВУМЯ ПРИЗНАКАМИ,
				//   ЧТО И ГЕЙТ. Первая редакция срабатывала по ОДНОМУ (радиус секции ≤ 1) и обещала
				//   исход «свет уйдёт в нижний зажим из ТОЧКИ», который та же волна отменила:
				//   гасится эмиттер только когда И секция в точку, И полу-экстент узла ноль
				//   (`GatherEmitters`). При живом полу-экстенте исход другой — удержание габарита.
				//   Обещать отменённый исход — тот же класс, что миснейм в дампе.
				if (bSec && Verts > 0 && SecR <= 1.f)
				{
					const bool bDead = HalfExt <= KINDA_SMALL_NUMBER;
					UE_LOG(LogAsdaFxLight, Warning,
						TEXT("      ★ СЕКЦИЯ В ТОЧКУ: вершин %d, радиус %.3f юн (≤1), полу-экстент узла %.2f, альфа %.3f — %s%s"),
						Verts, SecR, HalfExt, NodeAlpha,
						bDead
							? TEXT("РИСОВАТЬ НЕЧЕГО (размер узла тоже ноль): света этому эмиттеру не выдаётся, гейт «геометрия вырождена в точку»")
							: TEXT("это ДНО ВДОХА живого эмиттера (размер узла ненулевой): габарит и форму держит поле «6.40. Размер формы: удержание габарита, с»"),
						E.bRing ? TEXT(". Кольцо: размер лежит в RingTopR/RingBotR, а не в SizeX/SizeY") : TEXT(""));
				}
			}
		}
		UE_LOG(LogAsdaFxLight, Log, TEXT("[эмиттеры] эффектов напечатано: %d%s"), Shown,
			Filter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (фильтр «%s»)"), *Filter));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ВОЛНА Ф4 (10.09.2026): СТЕНД ДЛЯ ЭФФЕКТОВ, КОТОРЫЕ ОФЛАЙН НЕ ЗАПУСКАЮТСЯ.
//
//   ЗАЧЕМ. Навыки и свечение оружия — единственные семейства, которые НЕЛЬЗЯ увидеть, просто открыв карту:
//   у офлайн-павна оружия нет вовсе (модель приезжает пакетом 5001), а навык требует сервера. Проверять их
//   по таблице семейств — это ровно тот «второй метод», который врёт (закон false-negatives-method):
//   таблица говорит «правило есть», а горит ли свет и светлеет ли от него ПОЛ — не говорит.
//
//   ЧТО ДЕЛАЕТ. Строит перед камерой серую площадку со стенкой (кубы движка, дефолтный материал) и спавнит
//   на ней ТОТ ЖЕ САМЫЙ ассет BP_Fx_<jeb>, который в игре спавнят навык и свечение оружия. Дальше это обычный
//   эффект-актор: подсистема заводит ему кандидата, семейство определяется тем же именем .jeb, кадр света
//   считается тем же ApplyFrame. Ничего «тестового» в пути света нет — стенд подставляет только ПОВЕРХНОСТЬ,
//   которой у эффекта в воздухе не было.
//
//   asda.Fx.Light.Stand <база .jeb | BP_имя> [дистанция=700] [пол ниже на=150]
//                       [item=<код предмета>] [plate=<сторона площадки, юн>] [wall=0|1]
//   ★ fix10: plate=0 отменяет и площадку, и стенку — эффект встаёт над ПОЛОМ САМОЙ КАРТЫ.
//     Без этого крупный блин (9× при свете 608 юн — ширина 5472 юн) светил на площадку 1600 юн
//     и на пустоту вокруг неё, и кадра «свет по всей плоскости» не существовало ни в одной волне.
//   asda.Fx.Light.StandClear — убрать стенд
static TArray<TWeakObjectPtr<AActor>> GAsdaFxStandActors;

static void AsdaFxStandClear()
{
	int32 N = 0;
	for (TWeakObjectPtr<AActor>& A : GAsdaFxStandActors) { if (A.IsValid()) { A->Destroy(); ++N; } }
	GAsdaFxStandActors.Reset();
	UE_LOG(LogAsdaFxLight, Log, TEXT("[стенд] убрано акторов: %d"), N);
}

// Куб движка нужного размера с дефолтным материалом. Масштаб в кубах: базовый куб — 100×100×100.
static AActor* AsdaFxStandBox(UWorld* W, const FVector& Center, const FVector& SizeUU)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) { return nullptr; }
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Center, FRotator::ZeroRotator, P);
	if (!A) { return nullptr; }
	// Mobility обязателен Movable: у Static-актора SetActorScale3D в игре просто не применится
	A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	A->GetStaticMeshComponent()->SetStaticMesh(Cube);
	A->SetActorScale3D(SizeUU / 100.f);
	GAsdaFxStandActors.Add(A);
	return A;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaFxLightStand(
	TEXT("asda.Fx.Light.Stand"),
	TEXT("Стенд для эффекта: серая площадка со стенкой перед камерой + спавн BP_Fx_<база .jeb> на ней. "
	     "asda.Fx.Light.Stand <база .jeb | BP_имя> [дистанция=700] [пол ниже на=150] [item=<код предмета>] "
	     "[plate=<сторона площадки, юн; 1600 по умолчанию, 0 = не строить, светить на пол карты>] [wall=0|1] "
	     "[seed=0|1 — засеять поля эффекта действующими числами перед правкой; 1 по умолчанию]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* W)
	{
		// ★ 13.09.2026 (конфигуратор свечения): необязательный аргумент item=<код предмета> в любом месте —
		//   эффект стенда получает код предмета, и на нём действует переопределение «этот предмет + эффект».
		//   Позиционные аргументы (имя, дистанция, опускание пола) считаются БЕЗ него.
		int32 StandItem = 0;
		// ═══ ★★★ ВОЛНА fix10 (пункт 1 ревизии fix9, 20.09.2026): ПЛОЩАДКА СТЕНДА — ПАРАМЕТР ═══════
		//
		//   ЧТО БЫЛО НЕ ТАК. Площадка стенда всегда 1600×1600 юн, а блин, ради которого волны и
		//   идут, при 9× имеет ширину 5472 юн: свет ложился на 1600 юн посередине и на ПУСТОТУ
		//   вокруг. Кадр «блин светит плоскостью» поэтому не мог получиться ни у одной волны —
		//   плоскости под блином не было. Стенка 40×1600×600 при этом стоит в 420 юн перед
		//   эффектом, то есть ВНУТРИ крупного блина, и в съёмке сверху перечёркивает кадр.
		//   ЛЕЧЕНИЕ: и площадка, и стенка теперь управляются именованными аргументами.
		//     plate=<юн>  сторона площадки (1600 по умолчанию); 0 — НЕ строить её вовсе, тогда
		//                 свет ложится на СОБСТВЕННЫЙ пол карты (ради этого пункт и заведён).
		//     wall=0|1    строить ли стенку позади эффекта (1 по умолчанию, как было).
		//   Умолчания равны прежнему поведению — ни один прежний прогон не меняется.
		float PlateSide = 1600.f;
		bool bWall = true;
		// ★★★ fix13 (КРИТ-1): ЗЕРКАЛИТЬ ЛИ ДЕЙСТВУЮЩИЕ ЧИСЛА В ПОЛЯ ЭФФЕКТА ПЕРЕД ПРАВКОЙ.
		//   1 (умолчание) — да: стенд обязан начинать с тех же чисел, с каких начнёт владелец в
		//   панели деталей (там это делает зеркало RegisterEffect). Иначе скрипт стенда пишет свою
		//   спецификацию поверх C++-заглушки, у которой охват «один свет на эффект», — и замер
		//   «поставка per_emitter» мерит не поставку (ровно это и вышло у волны fix12).
		//   seed=0 возвращает прежнее поведение для сравнения со старыми прогонами.
		bool bSeedSpec = true;
		TArray<FString> A;
		for (const FString& S : InArgs)
		{
			if (S.StartsWith(TEXT("item="), ESearchCase::IgnoreCase)) { StandItem = FCString::Atoi(*S.Mid(5)); }
			else if (S.StartsWith(TEXT("plate="), ESearchCase::IgnoreCase)) { PlateSide = FMath::Clamp(FCString::Atof(*S.Mid(6)), 0.f, 60000.f); }
			else if (S.StartsWith(TEXT("wall="), ESearchCase::IgnoreCase)) { bWall = (FCString::Atoi(*S.Mid(5)) != 0); }
			else if (S.StartsWith(TEXT("seed="), ESearchCase::IgnoreCase)) { bSeedSpec = (FCString::Atoi(*S.Mid(5)) != 0); }
			else { A.Add(S); }
		}
		if (!W || A.Num() < 1) { UE_LOG(LogAsdaFxLight, Warning, TEXT("[стенд] нужно имя эффекта")); return; }
		AsdaFxStandClear();

		// Точка стенда берётся ОТ КАМЕРЫ той же подсистемы, что гейтит свет по дальности: если бы стенд
		// вставал в другом месте, эффект мог бы не попасть в гейт — и «света нет» означало бы «мимо».
		UAsdaFxLightSubsystem* S = W->GetSubsystem<UAsdaFxLightSubsystem>();
		// ★ волна 3 п.11 (15.09.2026): точка и поворот — AsdaView::GetViewPoint, ТА ЖЕ функция, что у гейта (UpdateViewLocation).
		//   ★ ИСПРАВЛЕНИЕ E7: раньше в игре бралась точка обзора контроллера (после первого кадра камеры она равна кэшу
		//   камеры — результат тот же), а в окне редактора — точка гейта БЕЗ поворота, и стенд вставал по мировой оси X.
		//   Теперь в редакторе стенд встаёт перед перспективным вьюпортом этого мира.
		FAsdaViewPoint VP;
		AsdaView::GetViewPoint(W, VP);
		FVector View = VP.Location;
		FRotator ViewRot = VP.bHasRotation ? VP.Rotation : FRotator::ZeroRotator;
		const float Dist = (A.Num() > 1) ? FMath::Clamp(FCString::Atof(*A[1]), 100.f, 5000.f) : 700.f;
		const float Drop = (A.Num() > 2) ? FMath::Clamp(FCString::Atof(*A[2]), 20.f, 2000.f) : 150.f;
		const FVector Fwd = ViewRot.Vector().GetSafeNormal2D().IsNearlyZero() ? FVector(1, 0, 0) : ViewRot.Vector().GetSafeNormal2D();
		const FVector O = View + Fwd * Dist;

		// Площадка plate×plate×40, её ВЕРХ ровно на (O.Z − Drop); стенка 40×plate×600 позади эффекта.
		// ★ fix10: plate=0 не строит ни площадку, ни стенку — свет ложится на пол самой карты.
		if (PlateSide > 0.f)
		{
			AsdaFxStandBox(W, O - FVector(0, 0, Drop + 20.f), FVector(PlateSide, PlateSide, 40.f));
			if (bWall)
			{
				const FVector WallC = O + Fwd * (PlateSide * 0.2625f) + FVector(0, 0, 300.f - Drop);
				AsdaFxStandBox(W, WallC, FVector(40.f, PlateSide, 600.f));
			}
		}

		const bool bRawBP = A[0].StartsWith(TEXT("BP_"));
		AAsdaEffectActor* Fx = bRawBP
			? AAsdaEffectActor::SpawnByBPName(W, A[0], O, FRotator::ZeroRotator, nullptr)
			: AAsdaEffectActor::SpawnByJebName(W, A[0], O, FRotator::ZeroRotator, nullptr);
		if (!Fx)
		{
			UE_LOG(LogAsdaFxLight, Warning, TEXT("[стенд] %s не найден"),
				bRawBP ? *A[0] : *AsdaAssetPaths::FxBpName(A[0]));   // ★ волна 2 п. 7
			return;
		}
		// Эффект навыка одноразовый: без вечного цикла он догорит раньше, чем снимется лестница кадров,
		// и метрика мерила бы догорание, а не свет. Гейт значимости и кулл тоже снимаем — они не про свет.
		for (FAsdaFxEmitter& E : Fx->Emitters) { E.EndMode = 3; }
		Fx->CullDistance = 1.0e9f;
		Fx->RestartEmitters();
		GAsdaFxStandActors.Add(Fx);
		// ★★★ fix13 (КРИТ-1): ПОСЕВ ПОЛЕЙ ДЕЙСТВУЮЩИМИ ЧИСЛАМИ — ДО заведения кандидата.
		//   Тем же зеркалом, что показывает числа владельцу в панели деталей (MirrorSpecToAsset),
		//   и с тем же гейтом «замок ассета не трогаем». После посева ветка разрешения может
		//   смениться с «семейство» на «ассет:замок» (это сделает скрипт стенда), но ЧИСЛА — и
		//   охват в том числе — останутся теми же. До fix13 они менялись молча.
		if (S && bSeedSpec)
		{
			const UAsdaFxLightSettings* Set = UAsdaFxLightSettings::Get();
			const FAsdaFxLightRule* SeedR = Set ? Set->FindRule(UAsdaFxLightSubsystem::ResolveJebName(Fx)) : nullptr;
			const bool bSeeded = S->MirrorSpecToAsset(Fx, SeedR);
			UE_LOG(LogAsdaFxLight, Log, TEXT("[стенд] посев полей действующими числами: %s (правило %s, замок ассета %s)"),
				bSeeded ? TEXT("СДЕЛАН") : TEXT("не сделан"),
				SeedR ? *SeedR->Id : TEXT("<нет>"), Fx->FxLight.bManual ? TEXT("взведён") : TEXT("снят"));
		}
		if (S) { S->RegisterEffect(Fx); }
		// ★ 13.09.2026: код предмета (переопределение «этот предмет + эффект») и печать ДЕЙСТВУЮЩИХ чисел —
		//   по этой строке замер «до/после» подтверждает, что правка конфигуратора дошла до рантайма
		if (StandItem != 0) { Fx->SetSourceItemId(StandItem); }
		{
			const FAsdaFxGlowOverride* Ov = Fx->FindGlowOverride();
			float Boost = 0.f;
			const bool bEm = Fx->ResolveEmissiveOverride(Boost);
			const FAsdaFxLightSpec Sp = UAsdaFxLightComponent::ResolveSpec(Fx, S ? UAsdaFxLightSettings::Get()->FindRule(UAsdaFxLightSubsystem::ResolveJebName(Fx)) : nullptr);
			UE_LOG(LogAsdaFxLight, Log, TEXT("[стенд] свечение: предмет=%d переопределение=%s (свет %d) → свет %d, %.3f кд, R %.0f…%.0f, ярлык=%s; эмиссия %s %.3f"),
				StandItem, Ov ? *FString::Printf(TEXT("%s/%d"), *Ov->Jeb, Ov->ItemId) : TEXT("нет"), Ov ? (int32)Ov->bLight : 0,
				(int32)Sp.bEnabled, Sp.IntensityCd, Sp.RadiusMin, Sp.RadiusMax, *Sp.FamilyId,
				bEm ? TEXT("задана") : TEXT("из материала"), Boost);
		}

		int32 Nodes = 0;
		for (const FAsdaFxEmitter& E : Fx->Emitters) { Nodes += E.Nodes.Num(); }
		// ★ fix10: печатаются ДЕЙСТВУЮЩИЕ размеры площадки и стенки — по этой строке прогон
		//   доказывает, на какую поверхность лёг свет (своя площадка стенда или пол карты)
		UE_LOG(LogAsdaFxLight, Log, TEXT("[стенд] %s: эмиттеров %d, узлов %d, точка (%.0f %.0f %.0f), пол z=%.0f, площадка %.0f×%.0f (%s), стенка %s"),
			*A[0], Fx->Emitters.Num(), Nodes, O.X, O.Y, O.Z, O.Z - Drop,
			PlateSide, PlateSide, (PlateSide > 0.f) ? TEXT("своя") : TEXT("НЕТ — пол карты"),
			(PlateSide > 0.f && bWall) ? *FString::Printf(TEXT("x+%.0f"), PlateSide * 0.2625f) : TEXT("нет"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaFxLightStandClear(
	TEXT("asda.Fx.Light.StandClear"),
	TEXT("Убрать стенд asda.Fx.Light.Stand."),
	FConsoleCommandDelegate::CreateStatic(&AsdaFxStandClear));
#endif // ASDA_DEV_CONSOLE

// ★★★ ВОЛНА Ф9: ПОКАДРОВЫЙ СЛЕД ОТБОРА. Одна короткая строка на кадр, N кадров подряд.
//   ЗАЧЕМ ОТДЕЛЬНАЯ КОМАНДА, А НЕ Dump. Поведение на границе кадра и при быстром вращении — это
//   ПРОЦЕСС, а не состояние: доказывать его снимком таблицы на 437 строк бессмысленно, а разглядыванием
//   кадров — тем более (при вращении двигается вся сцена, попарный дифф бесполезен — та же грабля,
//   что описана у счётчиков поп-ина подсистемы значимости). Нужны именно ряды чисел по кадрам:
//   сколько горит, сколько догорает отсрочку, где стоит ход силы.
int32 GAsdaFxLightWatch = 0;

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaFxLightWatch(
	TEXT("asda.Fx.Light.Watch"),
	TEXT("Печатать по строке на кадр N кадров подряд: кандидаты / прошли отбор / вне кадра / догорают / мест / ГОРИТ / экранный след / ход силы (min..max). Нужно для проверки границы кадра и быстрого вращения."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		GAsdaFxLightWatch = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, 100000) : 120;
	}));
#endif // ASDA_DEV_CONSOLE

// ─────────────── дамп ───────────────
// ★★★ ВОЛНА fix12 (пункт 2 ревизии fix11, 20.09.2026): ГЕЙТЫ ПРОГОНА — В ЛОГ, ПОИМЁННО.
//   Довод — у объявления в AsdaFxLightSubsystem.h. Коротко: шапка дампа печатала ДЕЙСТВУЮЩИЕ числа
//   («бюджет количества 96»), а из них не видно, откуда они взялись — с уровня качества владельца
//   или из ключа замерного стенда. Здесь рядом стоят ОБА числа: «ключ → действует», и отдельным
//   словом назван каждый ключ, который увёл прогон с уровня. Ни одно значение не меняется — строка
//   только читает то, что уже действует.
void UAsdaFxLightSubsystem::DumpGates() const
{
	const int32 Lvl = CurrentEffectsLevel();                 // уровень sg.EffectsQuality, по которому берутся [AsdaFxLight@N]
	const int32 AskFull = UAsdaFxLightComponent::AskFullCostKey();   // ключ счёта запроса мест (волна fix11)

	// Список принуждённых ключей: каждый, чьё значение не «как на уровне / как в полях».
	TArray<FString> Forced;
	if (GAsdaFxLightMaxCandidates != -1)  { Forced.Add(TEXT("предел кандидатов")); }   // ★ fix15 (СРЕДН-2)
	// ★ fix16: прогон с выключенной перепроверкой погасших — это ПОЛОВИНА A/B, и лог обязан сказать
	//   об этом сам: иначе «свет не вернулся» читалось бы как дефект, а не как заказанный откат
	if (GAsdaFxLightEmptyRecheck != 8)    { Forced.Add(TEXT("перепроверка погасших")); }
	if (GAsdaFxLightBudget       != -1)   { Forced.Add(TEXT("бюджет количества")); }
	if (GAsdaFxLightScreenBudget != -1.f) { Forced.Add(TEXT("экранный бюджет")); }
	if (GAsdaFxLightPerEffect    != -1)   { Forced.Add(TEXT("кошелёк на эффект")); }
	if (GAsdaFxLightRadiusCap    != -1.f) { Forced.Add(TEXT("потолок радиуса")); }
	if (GAsdaFxLightForceScope   != -1)   { Forced.Add(TEXT("охват")); }
	if (GAsdaFxLightForceShape   != -1)   { Forced.Add(TEXT("форма")); }
	if (GAsdaFxLightForceSegments!= -1)   { Forced.Add(TEXT("кусков раскладки")); }
	if (GAsdaFxLightForceTiles   != -1)   { Forced.Add(TEXT("плиток вдоль куска")); }
	if (GAsdaFxLightForceShapeSize > 0.f) { Forced.Add(TEXT("размер формы")); }
	if (GAsdaFxLightForceShapePitch != 0.f){ Forced.Add(TEXT("наклон формы")); }
	if (GAsdaFxLightForceColor   != -1)   { Forced.Add(TEXT("режим цвета")); }
	if (GAsdaFxLightForceRadius  != -1)   { Forced.Add(TEXT("режим радиуса")); }
	if (GAsdaFxLightForcePos     != -1)   { Forced.Add(TEXT("режим позиции")); }
	if (GAsdaFxLightPerCandMax   != 0)    { Forced.Add(TEXT("потолок мест на кандидата")); }
	if (GAsdaFxLightScale        != 1.f)  { Forced.Add(TEXT("множитель силы")); }
	if (GAsdaFxLightRadiusScale  != 1.f)  { Forced.Add(TEXT("множитель радиуса")); }
	if (AskFull                  != 1)    { Forced.Add(TEXT("счёт запроса (AskFullCost)")); }
	// ★★★ РЕВИЗИЯ ВОЛНЫ «форма-2» (КРИТ-3, 21.09.2026): КЛЮЧИ САМОЙ ЭТОЙ ВОЛНЫ ТОЖЕ В СПИСКЕ.
	//   Первая редакция завела `asda.Fx.Light.GeomHoldSec` и `asda.Fx.Light.DarkRepickSec`, замерила
	//   ими пары «откат / поставка» — и НЕ ДОБАВИЛА их сюда. В логе оба плеча замера печатали
	//   «принуждённых ключей нет», то есть по логу «44 вырожденных» и «0 вырожденных» были
	//   НЕОТЛИЧИМЫ по условиям прогона, и доказательство держалось на памяти о том, что запускали.
	//   Ровно это же чинила волна fix16 для `EmptyRecheck` — довод стоит тремя строками выше.
	if (GAsdaFxLightGeomHoldSec  != -1.f) { Forced.Add(TEXT("удержание габарита")); }
	if (GAsdaFxLightDarkRepickSec != 0.5f){ Forced.Add(TEXT("перевыбор тёмного набора")); }
	// ★★★ ВТОРАЯ РЕВИЗИЯ (СРЕДН-5): ТРИ КЛЮЧА, КОТОРЫМИ ПОЛЬЗУЕТСЯ КАЖДЫЙ ЗАМЕРНЫЙ ПРОГОН, В
	//   СПИСОК НЕ ПОПАДАЛИ ВОВСЕ. Строка обещает «каждый, чьё значение не как на уровне», а стенд
	//   в КАЖДОМ своём шаге отправляет `FadeInSec 0`, `FadeOutSec 0` и `EditorPropWatch 0` — и дамп
	//   всё равно печатал «принуждённых ключей нет». Тот же класс, что «(ключ)» в шапке: строка
	//   отвечает на вопрос «прогон в настройках уровня?» и обязана отвечать ЦЕЛИКОМ.
	//   ★ `EditorPropWatch` — не уровневый гейт, а поведение редактора (подписка на правку свойств);
	//     назван отдельным словом, потому что прогон с ним выключенным моделирует СОБРАННЫЙ клиент.
	if (GAsdaFxLightFadeIn       != 0.25f){ Forced.Add(TEXT("время разгорания")); }
	if (GAsdaFxLightFadeOut      != 0.35f){ Forced.Add(TEXT("время затухания")); }
	if (GAsdaFxLightEditorPropWatch != 1) { Forced.Add(TEXT("подписка редактора на правку свойств")); }

	// ★ Вывод одной строкой и ОДНИМ словом: прогон либо в настройках уровня, либо нет. Именно этого
	//   слова не хватило отчёту fix11 — там «бюджет владельца» стояло рядом с числами ключа.
	const FString Verdict = Forced.Num() == 0
		? FString::Printf(TEXT("В НАСТРОЙКАХ УРОВНЯ (sg.EffectsQuality=%d), принуждённых ключей нет"), Lvl)
		: FString::Printf(TEXT("★ ГЕЙТЫ ПРИНУЖДЕНЫ (%d): %s — ЭТО НЕ НАСТРОЙКИ УРОВНЯ %d"),
			Forced.Num(), *FString::Join(Forced, TEXT(", ")), Lvl);

	UE_LOG(LogAsdaFxLight, Log, TEXT("  ГЕЙТЫ ПРОГОНА: %s"), *Verdict);
	// Вторая строка — числа: «ключ → действует» у всех четырёх уровневых гейтов и сырые значения
	// замерных ключей. Читается и глазом, и разбором (стенд вынимает их в json прогона).
	UE_LOG(LogAsdaFxLight, Log,
		TEXT("  ГЕЙТЫ ЧИСЛАМИ: бюджет ключ %d → %d | экранный ключ %.2f → %.2f | на эффект ключ %d → %d | потолок радиуса ключ %.1f → %.0f | уровень %d (по уровню: бюджет %d, экранный %.2f, на эффект %d, потолок %.0f)"),
		GAsdaFxLightBudget, ResolveBudget(),
		GAsdaFxLightScreenBudget, ResolveScreenBudget(),
		GAsdaFxLightPerEffect, ResolvePerEffect(),
		GAsdaFxLightRadiusCap, ResolveRadiusCap(), Lvl,
		LevelBudget(Lvl), LevelScreenBudget(Lvl), LevelPerEffect(Lvl), LevelRadiusCap(Lvl));
	UE_LOG(LogAsdaFxLight, Log,
		TEXT("  ГЕЙТЫ ЗАМЕРНЫЕ: охват %d | форма %d | кусков %d | плиток %d | размер формы %.1f | наклон %.1f | цвет/радиус/позиция %d/%d/%d | мест на кандидата %d | сила ×%.2f | радиус ×%.2f | AskFullCost %d | удержание габарита ключ %.2f → %.2f с | перевыбор тёмного набора %.2f с | перепроверка погасших %d"),
		GAsdaFxLightForceScope, GAsdaFxLightForceShape, GAsdaFxLightForceSegments, GAsdaFxLightForceTiles,
		GAsdaFxLightForceShapeSize, GAsdaFxLightForceShapePitch,
		GAsdaFxLightForceColor, GAsdaFxLightForceRadius, GAsdaFxLightForcePos,
		GAsdaFxLightPerCandMax, GAsdaFxLightScale, GAsdaFxLightRadiusScale, AskFull,
		// ★ КРИТ-3: «ключ → действует» у удержания. Ключ −1 значит «как в поле каждого эффекта»,
		//   поэтому вторым числом печатается то, что выйдет у эффекта с ПОСТАВОЧНЫМ полем (0.5).
		GAsdaFxLightGeomHoldSec, ResolveGeomHoldSec(FAsdaFxLightSpec().GeomHoldSec),
		DarkRepickSec(), FMath::Max(GAsdaFxLightEmptyRecheck, 0));
}

void UAsdaFxLightSubsystem::Dump()
{
	const UWorld* W = GetWorld();
	const bool bEditorWorld = W && !W->IsGameWorld();
	const int32 Lvl = CurrentEffectsLevel();
	// ★ ВОЛНА Ф9: в шапке теперь названо, ЧЕМ отбирали (кадром или дистанцией), сколько снято «вне кадра»
	//   и сколько догорает отсрочку. Иначе «мест выдано 12 из 437» одинаково читалось бы и как «остальные
	//   за спиной» (норма), и как «бюджет их вытеснил» (повод поднимать бюджет) — две разные болезни.
	// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 6 ревизии: ДАМП ОБЯЗАН ГОВОРИТЬ, ОТКУДА ЧИСЛО.
	//   Прежняя строка печатала «бюджет количества %d (ключ)», и слово «(ключ)» было ЗАШИТО В
	//   ФОРМАТ — то есть стояло всегда, в том числе когда число пришло с уровня качества. В логах
	//   это давало «бюджет количества 160 (ключ)» рядом с честной строкой гейтов «бюджет ключ -1 →
	//   160», и ревизия едва не приняла уровневое значение за принуждённое (закон
	//   false-negatives-method: миснейм врёт ровно так же, как ложное «нет»).
	//   Теперь источник вычисляется ТОЙ ЖЕ развилкой, что и само число (ResolveBudget), и печатается
	//   рядом с ним. То же сделано у экранного бюджета и у потолка радиуса — они молчали о
	//   происхождении вовсе, а лечатся эти случаи по-разному.
	//   ★ РЕВИЗИЯ (СРЕДН-7): ПОРОГИ НЕ ПОВТОРЯЮТСЯ ЛИТЕРАЛАМИ. Первая редакция переписывала сюда
	//     «0 / −2 / 1.f» из тел ResolveBudget/ResolveScreenBudget/ResolveRadiusCap — вторая формула
	//     тех же чисел, которая разойдётся с первой при любой правке порога. Источник определяется
	//     СРАВНЕНИЕМ: уровневое значение спрашивается напрямую, и если действующее равно ему при
	//     отрицательном ключе — значит пришло с уровня.
	auto SrcWordI = [](int32 Key, int32 Lv, int32 Eff, int32 LvVal) -> FString
	{
		if (Key < 0) { return (Eff == LvVal) ? FString::Printf(TEXT("уровень %d"), Lv) : FString(TEXT("ключ: без предела")); }
		return FString::Printf(TEXT("ключ %d"), Key);
	};
	auto SrcWordF = [](float Key, int32 Lv, float Eff, float LvVal) -> FString
	{
		if (Key < 0.f) { return FMath::IsNearlyEqual(Eff, LvVal) ? FString::Printf(TEXT("уровень %d"), Lv) : FString(TEXT("ключ: без предела")); }
		return FString::Printf(TEXT("ключ %.2f"), Key);
	};
	const FString BudgetSrc = SrcWordI(GAsdaFxLightBudget, Lvl, ResolveBudget(), FMath::Max(0, LevelBudget(Lvl)));
	const FString ScreenSrc = SrcWordF(GAsdaFxLightScreenBudget, Lvl, ResolveScreenBudget(), FMath::Max(0.f, LevelScreenBudget(Lvl)));
	const FString CapSrc    = SrcWordF(GAsdaFxLightRadiusCap, Lvl, ResolveRadiusCap(), FMath::Max(1.f, LevelRadiusCap(Lvl)));
	UE_LOG(LogAsdaFxLight, Log, TEXT("=== эффект-света мира %s (%s): механизм %s%s | ОТБОР %s | кандидатов %d, прошли отбор %d, вне кадра %d, догорают %d, мест выдано %d, ГОРИТ %d | бюджет количества %d (%s) | экранный %.2f из %.2f (%s), отсечено площадью %d | потолок радиуса %.0f (%s) | уровень эффектов %d | множитель силы %.2f | ход %.2f/%.2f с, отсрочка %.2f с | сборка: компонент %s, подсистема %s ==="),
		W ? *W->GetName() : TEXT("?"),
		bEditorWorld ? TEXT("редактор") : TEXT("игра"),
		IsFeatureEnabled() ? TEXT("ВКЛ") : TEXT("ВЫКЛ"),
		bEditorWorld ? (IsEditorPreviewEnabled() ? TEXT(", показ в редакторе ВКЛ") : TEXT(", показ в редакторе ВЫКЛ")) : TEXT(""),
		IsFrustumCull() ? TEXT("ПО КАДРУ") : TEXT("по дистанции"),
		LastCandidates, LastEligible, LastOffScreen, LastFading, LastAdmitted, LastLit, LastBudget, *BudgetSrc,
		LastScreenUsed, ResolveScreenBudget(), *ScreenSrc, LastCutByScreen, ResolveRadiusCap(), *CapSrc, Lvl, GlobalIntensityScale(),
		FadeInSec(), FadeOutSec(), GraceSec(),
		// ★ fix7: метки компиляции ОБОИХ файлов механизма — доказательство, что замер снят на
		//   бинаре с правками волны, а не на прошлом (см. объявление UAsdaFxLightComponent::BuildStamp)
		UAsdaFxLightComponent::BuildStamp(), TEXT(__DATE__) TEXT(" ") TEXT(__TIME__));
	DumpGates();
	// ★ РЕВИЗИЯ (СРЕДН-6): ПРИЧИНА СНА НЕ ЗАШИВАЕТСЯ. «альфа эффекта 0» стояло тут литералом, хотя
	//   та же волна завела развилку причин (DormantGate): спящим кандидат бывает и потому, что
	//   геометрия вырождена, и потому, что владелец снял свечение всем эмиттерам.
	UE_LOG(LogAsdaFxLight, Log, TEXT("  спят (тёмные — мест не занимают): %d; причина у каждого — в колонке «состояние»"), LastDormant);
	// ★★★ fix16: СКОЛЬКО ЭФФЕКТОВ СЕЙЧАС БЕЗ ЕДИНОГО СВЕТА. Таких в строке «кандидатов N» нет вовсе
	//   (кандидат — это компонент света), и до fix16 отличить «эффекту свет не положен» от «эффект
	//   потерял свет и не вернёт его никогда» было нечем. Рядом — потолок проверок за кадр: по нему
	//   владелец видит, за сколько кадров очередь доходит до каждого (★ fix17, МЕЛК-1: «за N/потолок
	//   кадров» — это НИЖНЯЯ оценка, верхняя вдвое больше, см. RecheckExtinguished).
	// ★★★ fix17 (СРЕДН-3): СТРОКА ОБЕЩАЛА ПЕРЕПРОВЕРКУ, КОТОРОЙ НЕ БУДЕТ. Прежняя печатала
	//   «перепроверка 8 за кадр» всегда, кроме `EmptyRecheck <= 0`, — а зовут RecheckExtinguished
	//   ТОЛЬКО из хвоста ReconcileSets, и та выходит раньше ещё в двух случаях: выключена сама
	//   подсистема (`UAsdaFxLightSettings::bEnabled` = 0) и `asda.Fx.Light.AutoReconcile 0` без
	//   принуждения. Ключ EmptyRecheck при этом остаётся равным 8, и дамп спокойно рапортовал
	//   работающий механизм, которого в этом кадре не звали ни разу. Называем ПРИЧИНУ, а не только
	//   свой ключ: владельцу, включившему откат «чтобы не мешало», это единственная подсказка.
	const TCHAR* RecheckWhyOff =
		GAsdaFxLightEmptyRecheck <= 0
			? TEXT(" — ВЫКЛЮЧЕНА (asda.Fx.Light.EmptyRecheck 0: погасший не вернётся сам)")
		: !UAsdaFxLightSettings::Get()->bEnabled
			? TEXT(" — НЕ ЗОВЁТСЯ (подсистема выключена, UAsdaFxLightSettings::bEnabled = 0: сверка набора выходит до перепроверки, погасший не вернётся сам)")
		: GAsdaFxLightAutoReconcile == 0
			? TEXT(" — НЕ ЗОВЁТСЯ (asda.Fx.Light.AutoReconcile 0 выключает И сверку набора, И перепроверку погасших: она зовётся её хвостом — погасший сам не вернётся, кроме кадра, когда игрок сменил уровень «Эффекты»)")
			: TEXT("");
	UE_LOG(LogAsdaFxLight, Log, TEXT("  погасших эффектов (светов 0, ждут возврата в кандидаты): %d; перепроверка %d за кадр%s"),
		Extinguished.Num(), FMath::Max(GAsdaFxLightEmptyRecheck, 0), RecheckWhyOff);
	// ★★★ «форма-2», п. 2: СКОЛЬКО РАЗ НАБОР ПЕРЕСОБИРАЛСЯ ПО ЖИВЫМ ЭМИТТЕРАМ. Ноль при работающем
	//   механизме = «застывших тёмных наборов на сцене нет», и это ответ, а не молчание. Большое
	//   число каждый дамп означало бы дребезг — тогда поднимать asda.Fx.Light.DarkRepickSec.
	int32 PinnedNum = 0;
	for (const TPair<FObjectKey, FAsdaFxDarkState>& P : DarkStates) { if (P.Value.Pinned.Num() > 0) { ++PinnedNum; } }
	UE_LOG(LogAsdaFxLight, Log, TEXT("  перевыборов тёмного набора: %lld с прошлого дампа (в последней сверке %d); отсрочка %.2f с, записей памяти темноты %d (из них со шпилькой живого состава %d)%s"),
		(long long)DarkRepicksSum, LastDarkRepicks, DarkRepickSec(), DarkStates.Num(), PinnedNum,
		DarkRepickSec() <= 0.f ? TEXT(" — ВЫКЛЮЧЕН (asda.Fx.Light.DarkRepickSec 0)") : TEXT(""));
	DarkRepicksSum = 0;
	// ★★★ РЕВИЗИЯ fix3 (С-4): МЕЛЬКАНИЕ ПОДРОБНОСТИ — ЧИСЛОМ. «Смен подробности» = сколько раз
	//   кандидату выдали другое число источников, чем в предыдущем кадре, накопленное с прошлого
	//   дампа. Ноль = форма не моргает. Считать это по кадрам снаружи нельзя: общее число источников
	//   бюджет держит у потолка, а перекладывать подробность между кандидатами может каждый кадр.
	UE_LOG(LogAsdaFxLight, Log, TEXT("  смен выданной подробности: %lld за %d кадров отбора (в последнем кадре %d); гистерезис подробности %.2f с"),
		(long long)DetailChangesSum, DetailFrames, LastDetailChanges, FMath::Max(GAsdaFxLightDetailHold, 0.f));
	DetailChangesSum = 0;
	DetailFrames = 0;
	// ★ ВОЛНА Ф4: колонки «эмит / разнос / геом / срез» отвечают на вопрос владельца «не режут ли потолки
	//   семейств крупные эффекты» ЧИСЛОМ: «геом» — радиус ДО зажимов (габарит эффекта × множитель),
	//   «срез» — что его в итоге ограничило. Числа берутся из ТОГО ЖЕ кадра света, а не пересчитываются здесь.
	// ★ ВОЛНА Ф8: колонка «форма» отвечает сразу на два вопроса — какой формы источник и ЧЕЙ он
	//   (номер эмиттера после решётки; «—» = общий свет эффекта). Без неё в дампе эффекта с семью
	//   поэмиттерными светами семь строк отличались бы только числами, и понять, какая из них чья, было бы нечем.
	// ★ fix13 (КРИТ-1): колонка «откуда» — ВЕТКА цепочки разрешения, давшая числа этой строки.
	//   Без неё «ручн=да» читалось как «числа с ассета», хотя замок ассета и дефолты семейства
	//   дают его одинаково, а расходятся они ровно в охвате — из-за чего и вышло «8 светов при
	//   заведении, 1 после пересканирования».
	// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 7 ревизии: «ГЕОМ ÷ РАДИУС» — ЭТО ФОРМУЛА, А НЕ ЗАМЕР,
	//   И ТЕПЕРЬ ОБ ЭТОМ НАПИСАНО В САМОМ ДАМПЕ. Колонка «геом» — габарит, УЖЕ домноженный на
	//   множитель семейства, а радиус получается из него зажимом:
	//       Radius = Clamp(Geom, RadiusMin, RadiusMax)   (AsdaFxLightComponent.cpp, блок «РАДИУС»).
	//   Значит «геом ÷ радиус» тождественно равно 1.000 всюду, где зажим не сработал, и «медиана
	//   D/R = 1.00» по этим двум колонкам значит НЕ «свет хорошо накрывает форму», а «радиус ничем
	//   не урезан». Прежние волны публиковали её как оценку накрытия — так делать нельзя.
	//   ★★★ РЕВИЗИЯ (КРИТ-2): ПЕРВАЯ РЕДАКЦИЯ ЗАМЕНИЛА ТАВТОЛОГИЮ ПОЧТИ ТАКОЙ ЖЕ. Она считала
	//   «накрыт» от ИСТОЧНИКА РАДИУСА, а радиус из него и получается ⇒ отношение было тождественно
	//   равно `radius_bounds_scale` из таблицы семейств (на живых данных: 2.00 в 97…100 % строк у
	//   семейств навыков, 3.05 у `map_glow_column`). Теперь знаменатель — ПОЛУ-ГАБАРИТ САМОЙ ФОРМЫ,
	//   который построил кадр (`MeasureShapeSizeR`: главные оси секции × удержание × свой множитель
	//   поля × сетка).
	//   ★★★ ВТОРАЯ РЕВИЗИЯ (СРЕДН-2), ЧЕСТНАЯ ОГОВОРКА: «ЦЕПОЧКИ РАЗНЫЕ» ВЕРНО НЕ ВСЕГДА. При
	//   охвате «свой свет у каждого эмиттера» агрегат собран по ОДНОМУ эмиттеру, поэтому
	//   `Agg.GeomRadius` тождественно равен `Agg.DonorRadius` (`GatherEmitters`, ветка
	//   `OnlyEmitter >= 0`), значит `HeldGeomRaw ≡ HeldShapeRaw`, и множитель удержания В ОТНОШЕНИИ
	//   СОКРАЩАЕТСЯ: колонка «накрыт» НЕ МОЖЕТ показать работу поля 6.40. Проверено эмпирически: у
	//   `char_skill` одни и те же значения 2.43 / 2.47 / 2.19 / 2.31 встречаются и при удержании 0,
	//   и при 0.5. Колонка остаётся полезной — она показывает зажимы и множители полей формы
	//   (разброс на живых данных 2.19…9.38), — но ДОКАЗЫВАТЬ ЕЮ УДЕРЖАНИЕ НЕЛЬЗЯ: для этого есть
	//   «удержание ×K» в строке «раскладка …» и прямой замер габарита.
	//   ≥1 — свет достаёт до края формы, <1 — форма торчит из своего света.
	//   «—» = у формы нет собственного габарита (точка, шар, конус) либо свет не горит.
	UE_LOG(LogAsdaFxLight, Log,
		TEXT("  ★ КАК ЧИТАТЬ: «геом» = габарит × множитель семейства, «радиус» = Clamp(геом, минимум, максимум) ⇒ «геом ÷ радиус» = 1.000 везде, где «срез» = «—»; это НЕ мера накрытия формы. Мера накрытия — колонка «накрыт» (радиус ÷ сырой полу-габарит формы): ≥1 свет достаёт до края формы, <1 форма торчит из своего света."));
	UE_LOG(LogAsdaFxLight, Log, TEXT("  %-38s %-20s %-16s %-16s %6s %8s %10s %9s %7s %5s %7s %8s %7s %-18s %8s %6s %5s %-12s %s"),
		TEXT("эффект (.jeb)"), TEXT("семейство"), TEXT("форма"), TEXT("откуда"), TEXT("ручн"), TEXT("дист"), TEXT("счёт"), TEXT("сила,кд"), TEXT("радиус"),
		TEXT("эмит"), TEXT("разнос"), TEXT("геом"), TEXT("накрыт"), TEXT("срез"), TEXT("след"), TEXT("draw"), TEXT("ход"), TEXT("тень"), TEXT("состояние"));
	for (UAsdaFxLightComponent* C : Candidates)
	{
		if (!IsValid(C)) { continue; }
		const AAsdaEffectActor* Fx = C->GetEffect();
		// ★ fix13: та же функция, но с ответом «какая ветка дала числа» (колонка «откуда»)
		UAsdaFxLightComponent::EAsdaFxSpecSource RowSrc = UAsdaFxLightComponent::EAsdaFxSpecSource::NoEffect;
		const FAsdaFxLightSpec Spec = UAsdaFxLightComponent::ResolveSpec(Fx, C->GetRule(), C->GetEmitterIndex(), &RowSrc);
		const float D = (Fx && bHaveView) ? (float)FVector::Dist(ViewLoc, Fx->GetActorLocation()) : -1.f;
		// Форма + чей это свет, одной короткой ячейкой
		// ★ Ф10: у «Авто» печатается ИТОГ распознавания («а:кольцо»), иначе «авто» ничего не говорило бы
		//   о том, какой источник реально горит
		auto ShapeWord = [](EAsdaFxLightShape Sh) -> const TCHAR*
		{
			switch (Sh)
			{
			case EAsdaFxLightShape::Rect: return TEXT("прямоуг");
			case EAsdaFxLightShape::Spot: return TEXT("конус");
			case EAsdaFxLightShape::Tube: return TEXT("трубка");
			case EAsdaFxLightShape::Ring: return TEXT("кольцо");
			// ★ Ф11: блин (N полос-капсул по диску) и шар (точка с «размером лампы» по габариту)
			case EAsdaFxLightShape::Disc: return TEXT("блин");
			case EAsdaFxLightShape::Sphere: return TEXT("шар");
			case EAsdaFxLightShape::Auto: return TEXT("авто");
			default:                      return TEXT("точка");
			}
		};
		// ★★★ РЕВИЗИЯ fix3 (КРИТ-2, 20.09.2026): ПЕЧАТАЕТСЯ ФАКТИЧЕСКАЯ ФОРМА И ЧИСЛО ИСТОЧНИКОВ.
		//   До ревизии здесь стояло `Spec.Shape` — ЗАЯВЛЕННАЯ форма, и вырождение под бюджетом в
		//   дампе не было видно вовсе: владелец читал «блин» там, где на сцене стоял один шарик
		//   полного радиуса, и по дампу это было не отличить от работающей формы. Теперь у горящего
		//   света берётся `GetLastShape()` (что применил кадр) с числом ФАКТИЧЕСКИ зажжённых
		//   источников и выданных мест: «блин×1/4» читается как «выдали одно место из четырёх».
		//   У «Авто» сохранена приставка «а:» — она отвечает на другой вопрос (что распознано).
		const bool bLit = C->IsLit();
		const float Cov = bLit ? C->GetLastCoverage() : 0.f;
		const FString CovWord = (Cov > KINDA_SMALL_NUMBER) ? FString::Printf(TEXT("%.2f"), Cov) : FString(TEXT("—"));
		const FString ShapeName = (Spec.Shape == EAsdaFxLightShape::Auto)
			? FString::Printf(TEXT("а:%s"), ShapeWord(bLit ? C->GetLastShape() : (EAsdaFxLightShape)Spec.Shape))
			: FString(ShapeWord(bLit ? C->GetLastShape() : Spec.Shape));
		// «×N/M» — источников горит сейчас / сколько просит полная подробность формы
		const int32 WantSrc = C->GetLightCost(Spec);
		const FString ShapeCnt = (bLit && (C->GetLastLitSources() > 1 || WantSrc > 1))
			? FString::Printf(TEXT("x%d/%d"), C->GetLastLitSources(), WantSrc)
			: FString();
		const FString ShapeTag = C->GetEmitterIndex() >= 0
			? FString::Printf(TEXT("%s%s#%d"), *ShapeName, *ShapeCnt, C->GetEmitterIndex())
			: ShapeName + ShapeCnt;
		// ★★★ ВОЛНА fix9 (КРИТ-1, 20.09.2026): ЧИСЛА РЕШЕНИЯ РАСКЛАДКИ — ОТДЕЛЬНОЙ СТРОКОЙ.
		//   Гарантия волны fix8 звучит «при ПОЛНОЙ выдаче построено ровно то, что просит правило», а
		//   проверить её по дампу было НЕЧЕМ: число полос приходилось восстанавливать перебором
		//   радиусов источников из сырья прогона — и ровно поэтому расхождение «правило просит 10,
		//   построено 8» нашлось только на ревизии. Теперь кадр печатает три числа, по которым
		//   решал, и то, что построил: сверка — вычитанием, а не реконструкцией.
		//   Строка идёт ТОЛЬКО у разложенных форм и только у горящего света: у точки раскладки нет.
		if (bLit && C->GetLastSplitParents() > 0 && C->GetLastRuleBaseR() > 0.f)
		{
			const EAsdaFxLightShape ShNow = C->GetLastShape();
			if (ShNow == EAsdaFxLightShape::Disc || ShNow == EAsdaFxLightShape::Ring
				|| ShNow == EAsdaFxLightShape::Tube || ShNow == EAsdaFxLightShape::Rect)
			{
				const float RuleRatio = 2.f * C->GetLastRuleSizeR() / FMath::Max(C->GetLastRuleBaseR(), 1.f);
				// ★★★ ВОЛНА fix10 (СРЕДН-1 ревизии fix9, 20.09.2026): МЕТКА СТАВИТСЯ ТОЛЬКО ТОГДА,
				//   КОГДА У КАДРА БЫЛ ВЫБОР. Прежде она стояла по одному сравнению «просит N —
				//   построено M» и кричала «← РАСХОЖДЕНИЕ» на ЗАКОННЫЙ срез бюджетом: живой прогон
				//   ревизии при бюджете количества 30 (rv9_bud_9x.log) напечатал «правило просит 10,
				//   построено 6, источников 28, мест выдано 30 ← РАСХОЖДЕНИЕ», хотя 30 мест из нужных
				//   56 — это не дефект, а работа бюджета. Цену полной раскладки кадр теперь считает
				//   сам (GetLastRuleFullCost), и метка ставится, лишь когда выданных мест на неё
				//   хватало. Сама цена печатается ВСЕГДА — по ней видно, чего не хватило.
				//   ★ Если цена ещё не посчитана (0 — кадр не проходил ApplyFrame), считаем, что
				//     выбор был: молчать о возможном дефекте хуже, чем лишний раз спросить.
				const int32 FullCost = C->GetLastRuleFullCost();
				// ═══ ★★★ ВОЛНА fix11 (пункт 3 ревизии fix10, 20.09.2026): СРЕЗ — ЭТО «ВЫДАНО МЕНЬШЕ
				//     ЗАПРОШЕННОГО», А НЕ «ВЫДАНО МЕНЬШЕ ПОЛНОЙ РАСКЛАДКИ» ═══════════════════════
				//
				//   ЧТО БЫЛО НЕ ТАК. Волна fix10 писала `bHadRoom = AdmittedCost >= FullCost`. Но
				//   раздача НИКОГДА не выдаёт больше запрошенного (Give[i] ≤ Cands[i].Cost), поэтому
				//   у кандидата, который ПОПРОСИЛ меньше своей полной раскладки, это сравнение ложно
				//   всегда — и любой такой дефект печатался как «срез бюджетом», то есть сам себя
				//   маскировал. Проверка на том самом примере, ради которого метка и заводилась:
				//   КРИТ волны fix9 («правило просит 10, построено 8, 48 источников против полных
				//   56») прежняя метка ПРОПУСТИЛА БЫ — 48 < 56, значит «срез», хотя бюджет был 512 и
				//   не резал ничего. Теперь тот же случай читается как 48 запрошено, 48 выдано,
				//   выбор был ⇒ «← РАСХОЖДЕНИЕ».
				//
				//   Запрос берётся ТОТ, с которым кандидат шёл в раздачу (SetRequestedCost в отборе),
				//   а не пересчитывается здесь: между отбором и дампом проходит кадр света, и второй
				//   вызов GetLightCost ответил бы уже про другую геометрию (закон
				//   diagnostic-must-use-real-path: мерить надо ту величину, которая работала).
				//   ★ Запроса ещё нет (0 — отбора не было) — считаем, что выбор был: молчать о
				//     возможном дефекте хуже, чем лишний раз спросить.
				//   ★ Полная раскладка печатается по-прежнему ВСЕГДА: по паре «запрошено / полная»
				//     видно и то, что бюджет не дал, и то, что кандидат не попросил.
				const int32 AskCost = C->GetRequestedCost();
				const bool bHadRoom = (AskCost <= 0) || (C->GetAdmittedCost() >= AskCost);
				const bool bMismatch = bHadRoom && (C->GetLastSplitParents() != C->GetLastRuleParents());
				// ★ fix11: запрос НИЖЕ полной раскладки при достатке мест — отдельная болезнь (пункт 2),
				//   и называть её надо отдельным словом, иначе она читается как обычное расхождение
				const bool bUnderAsk = bHadRoom && (AskCost > 0) && (FullCost > 0) && (AskCost < FullCost);
				UE_LOG(LogAsdaFxLight, Log,
					TEXT("      раскладка %s: размер формы %.1f юн (удержание ×%.2f), радиус кадра %.1f юн, D/R = %.3f; правило просит %d кусков, построено %d, источников %d, запрошено мест %d, выдано %d, полная раскладка стоит %d%s%s"),
					*ShapeName, C->GetLastRuleSizeR(), C->GetShapeHoldK(), C->GetLastRuleBaseR(), RuleRatio,
					C->GetLastRuleParents(), C->GetLastSplitParents(), C->GetLastLitSources(),
					AskCost, C->GetAdmittedCost(),
					FullCost,
					bMismatch ? TEXT("  ← РАСХОЖДЕНИЕ")
						: (bHadRoom ? TEXT("") : TEXT("  (срез бюджетом: выдано меньше запрошенного)")),
					bUnderAsk ? TEXT("  ← ЗАПРОС НИЖЕ ПОЛНОЙ РАСКЛАДКИ") : TEXT(""));
			}
		}
		UE_LOG(LogAsdaFxLight, Log, TEXT("  %-38s %-20s %-16s %-16s %6s %8.0f %10.5f %9.3f %7.0f %5d %7.0f %8.0f %7s %-18s %8.4f %6.0f %5.2f %-12s %s"),
			Fx ? *ResolveJebName(Fx) : TEXT("<нет>"),
			Spec.FamilyId.IsEmpty() ? TEXT("<нет>") : *Spec.FamilyId,
			*ShapeTag,
			UAsdaFxLightComponent::SpecSourceName(RowSrc),
			(Fx && Fx->FxLight.bManual) ? TEXT("да") : TEXT("—"),
			// ★ РЕВИЗИЯ (СРЕДН-6): печатается СЧЁТ ОТБОРА, а не пересчёт. Прежде дамп звал
			//   ComputeScore заново — без множителя альфы и без гистерезиса аренды, которые
			//   применяет сам отбор, — и печатал ЧИСЛО, ПО КОТОРОМУ НИКТО НЕ РЕШАЛ.
			D, C->GetLastScore(),
			C->GetLastIntensityCd(), C->GetLastRadius(),
			C->GetLastLive(), C->GetLastSpread(), C->GetLastRadiusGeom(),
			// ★ «форма-2», п. 7 + ревизия КРИТ-2: ЧЕСТНОЕ накрытие формы светом. Прочерк вместо
			//   «0.00» — чтобы «у этой формы габарита нет» не читалось как «свет не накрывает».
			*CovWord, C->GetClipName(),
			// ★ Ревизия fix3 (КРИТ-1): печатается след ТОГО, что реально горит (за выданные места),
			//   а не след полной формы: иначе дамп показывал бы цену, которой бюджет не списывал
			bHaveView ? C->EstimateScreenArea(ViewLoc, Spec, ResolveRadiusCap() * WorldLengthScale(W), C->GetAdmittedCost()) : 0.f,
			C->ResolveDrawDistance(Spec),
			// ★ Ф9: «ход» — доля разгорания 0..1 этого кадра. Ею отличается «свет только появился
			//   и ещё разгорается» от «горит вполсилы, потому что эффект догорает по альфе».
			C->GetLitRamp(),
			// ★ Ф6: три разных «нет тени» — поле выключено / бюджет не дал / просто далеко. Одна колонка на всех
			//   ответила бы «нет» одинаково, а лечатся эти три случая по-разному (закон false-negatives-method)
			// ★ ВОЛНА Ф8: спрашиваем НЕ `C->CastShadows`, а тот источник, который реально светит:
			//   у прямоугольной и конусной формы тень живёт на дочернем компоненте, и прежняя проверка
			//   отвечала бы «нет: бюджет» на честно горящую тень.
			!Spec.bCastShadow ? TEXT("поле выкл")
				: (C->IsCastingShadowNow() ? TEXT("ТЕНЬ")
					: (bHaveView && C->WantsShadow(Spec, (float)FVector::Dist(ViewLoc, Fx ? Fx->GetActorLocation() : ViewLoc)) ? TEXT("нет: бюджет") : TEXT("нет: далеко"))),
			// ★ ВОЛНА Ф3: состояние называет ПРИЧИНУ. Прежнее «место есть, гейт погасил» одинаково отвечало
			//   и на «эффект догорел», и на «кадр света ни разу не считался» — а это разные поломки
			C->GetGateName());
	}
}
