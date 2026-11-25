// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ТОЧКА НАБЛЮДАТЕЛЯ (волна 3 п.11, 15.09.2026) — правило, назначение и перечень прежних копий в шапке AsdaView.h. [ПОРТ]
#include "AsdaView.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaView, Log, All);

APlayerController* AsdaView::GetLocalPlayerController(const UWorld* W)
{
	return W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
}

APlayerCameraManager* AsdaView::GetPlayerCameraManager(const UWorld* W)
{
	// Побайтно прежний вызов эффекта, купола и значимости (движок: GetPlayerController(W, 0) → PlayerCameraManager)
	return W ? UGameplayStatics::GetPlayerCameraManager(W, 0) : nullptr;
}

bool AsdaView::Resolve(const FAsdaViewInputs& In, FAsdaViewPoint& Out)
{
	Out = FAsdaViewPoint();
	if (!In.bWorld) { return false; }

	if (In.bGameWorld)
	{
		// Игровой мир: камера → павн. Вьюпорты редактора игровому миру НЕ источник (это другой мир).
		if (In.bHasCamera)
		{
			Out.Location = In.CameraLocation;
			Out.Rotation = In.CameraRotation;
			Out.bHasRotation = true;
			Out.Source = EAsdaViewSource::PlayerCamera;
			return true;
		}
		if (In.bHasPawn)
		{
			Out.Location = In.PawnLocation;
			Out.Source = EAsdaViewSource::PlayerPawn;
			return true;
		}
	}
	else
	{
		// Окно редактора / превью: первый перспективный вьюпорт уровня, показывающий ЭТОТ мир.
		for (const FAsdaViewInputs::FLevelViewport& V : In.LevelViewports)
		{
			if (V.bPerspective && V.bSameWorld)
			{
				Out.Location = V.Location;
				Out.Rotation = V.Rotation;
				Out.bHasRotation = true;
				Out.Source = EAsdaViewSource::LevelViewport;
				return true;
			}
		}
	}

	// Последний отрисованный вид ЭТОГО мира (игровой вьюпорт, перспективный вьюпорт редактора или превью-сцены)
	if (In.RenderedLastFrame.Num() > 0)
	{
		Out.Location = In.RenderedLastFrame[0];
		Out.Source = EAsdaViewSource::RenderedLastFrame;
		return true;
	}
	return false;
}

bool AsdaView::GetViewPoint(const UWorld* W, FAsdaViewPoint& Out)
{
	Out = FAsdaViewPoint();
	if (!W) { return false; }

	FAsdaViewInputs In;
	In.bWorld = true;
	In.bGameWorld = W->IsGameWorld();
	if (In.bGameWorld)
	{
		if (const APlayerController* PC = GetLocalPlayerController(W))
		{
			if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
			{
				In.bHasCamera = true;
				In.CameraLocation = Cam->GetCameraLocation();
				In.CameraRotation = Cam->GetCameraRotation();
			}
			if (const APawn* P = PC->GetPawn())
			{
				In.bHasPawn = true;
				In.PawnLocation = P->GetActorLocation();
			}
		}
	}
#if WITH_EDITOR
	else if (GEditor)
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (!C) { continue; }
			FAsdaViewInputs::FLevelViewport& V = In.LevelViewports.AddDefaulted_GetRef();
			V.bPerspective = C->IsPerspective();
			V.bSameWorld = (C->GetWorld() == W);
			V.Location = C->GetViewLocation();
			V.Rotation = C->GetViewRotation();
			if (V.bPerspective && V.bSameWorld) { break; }   // дальше Resolve не смотрит — не собираем
		}
	}
#endif
	In.RenderedLastFrame = W->ViewLocationsRenderedLastFrame;
	return Resolve(In, Out);
}

bool AsdaView::GetViewLocation(const UWorld* W, FVector& Out)
{
	FAsdaViewPoint VP;
	if (!GetViewPoint(W, VP)) { return false; }
	Out = VP.Location;
	return true;
}

const TCHAR* AsdaView::SourceName(EAsdaViewSource Source)
{
	switch (Source)
	{
	case EAsdaViewSource::PlayerCamera:      return TEXT("камера игрока");
	case EAsdaViewSource::PlayerPawn:        return TEXT("павн игрока");
	case EAsdaViewSource::LevelViewport:     return TEXT("вьюпорт уровня этого мира");
	case EAsdaViewSource::RenderedLastFrame: return TEXT("отрисованный вид прошлого кадра");
	default:                                 return TEXT("нет наблюдателя");
	}
}

// `asda.View.Dump` — живая сверка правила: мир, источник, точка, поворот, сколько видов отрисовано, какие вьюпорты уровня.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaViewDump(
	TEXT("asda.View.Dump"),
	TEXT("asda.View.Dump — точка наблюдателя этого мира (AsdaView): источник, позиция, поворот; отрисованные виды и вьюпорты "
	     "уровня редактора. Одна функция для кулла и частиц эффектов, купола неба, гейта света эффектов и бюджета теней."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (!W) { UE_LOG(LogAsdaView, Warning, TEXT("[наблюдатель] мира нет")); return; }
		FAsdaViewPoint VP;
		const bool bOk = AsdaView::GetViewPoint(W, VP);
		UE_LOG(LogAsdaView, Log, TEXT("[наблюдатель] мир=%s тип=%d игровой=%d | есть=%d источник=«%s» точка=%s поворот=%s (есть=%d) | отрисовано видов=%d"),
			*W->GetName(), (int32)W->WorldType, W->IsGameWorld() ? 1 : 0, bOk ? 1 : 0, AsdaView::SourceName(VP.Source),
			*VP.Location.ToString(), *VP.Rotation.ToString(), VP.bHasRotation ? 1 : 0, W->ViewLocationsRenderedLastFrame.Num());
#if WITH_EDITOR
		if (GEditor)
		{
			int32 Idx = 0;
			for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
			{
				if (!C) { continue; }
				UE_LOG(LogAsdaView, Log, TEXT("[наблюдатель]   вьюпорт уровня #%d: перспективный=%d этот мир=%d точка=%s"),
					Idx++, C->IsPerspective() ? 1 : 0, C->GetWorld() == W ? 1 : 0, *C->GetViewLocation().ToString());
			}
		}
#endif
	}));
#endif // ASDA_DEV_CONSOLE

#if WITH_DEV_AUTOMATION_TESTS
// ─────────────── автотест на синтетических состояниях мира ───────────────
//   Прежние копии воспроизведены ПОСТРОЧНО как функции над снимком (редакторная сборка, GEditor есть) и сравниваются с
//   новым правилом на всех сочетаниях: игровой/нет, камера, павн, 6 раскладок вьюпортов уровня, отрисованный вид есть/нет.
//   Совпадение обязано быть везде, КРОМЕ перечисленных случаев ★ ИСПРАВЛЕНИЯ E7 (они проверяются отдельно).
namespace AsdaViewTest
{
	// Точка прежней копии: есть/нет + позиция (+ поворот для копии поворота частиц)
	struct FOld
	{
		bool bHas = false;
		FVector Location = FVector::ZeroVector;
	};

	// Первый перспективный вьюпорт уровня любого мира (прежний выбор эффекта и купола), nullptr — нет
	static const FAsdaViewInputs::FLevelViewport* FirstPerspectiveAnyWorld(const FAsdaViewInputs& In)
	{
		for (const FAsdaViewInputs::FLevelViewport& V : In.LevelViewports) { if (V.bPerspective) { return &V; } }
		return nullptr;
	}

	// Первый перспективный вьюпорт уровня ЭТОГО мира, nullptr — нет
	static const FAsdaViewInputs::FLevelViewport* FirstPerspectiveSameWorld(const FAsdaViewInputs& In)
	{
		for (const FAsdaViewInputs::FLevelViewport& V : In.LevelViewports) { if (V.bPerspective && V.bSameWorld) { return &V; } }
		return nullptr;
	}

	// ДО 15.09.2026: AAsdaEffectActor::GetViewLocation (без оверрайда превью) ≡ AAsdaSkyDomeActor::GetViewLocation
	static FOld OldEffectSky(const FAsdaViewInputs& In)
	{
		FOld R;
		if (In.bGameWorld && In.bHasCamera) { R.bHas = true; R.Location = In.CameraLocation; return R; }
		if (const FAsdaViewInputs::FLevelViewport* V = FirstPerspectiveAnyWorld(In)) { R.bHas = true; R.Location = V->Location; }
		return R;
	}

	// ДО 15.09.2026: UAsdaFxLightSubsystem::UpdateViewLocation
	static FOld OldFxLight(const FAsdaViewInputs& In)
	{
		FOld R;
		if (In.bGameWorld)
		{
			if (In.bHasCamera) { R.bHas = true; R.Location = In.CameraLocation; }
			else if (In.bHasPawn) { R.bHas = true; R.Location = In.PawnLocation; }
			return R;
		}
		if (const FAsdaViewInputs::FLevelViewport* V = FirstPerspectiveSameWorld(In)) { R.bHas = true; R.Location = V->Location; }
		return R;
	}

	// ДО 15.09.2026: UAsdaLightBudgetSubsystem::PlayerPos (подсистема только в игровых мирах; «нет» = ноль мира)
	static FOld OldLightBudget(const FAsdaViewInputs& In)
	{
		FOld R;
		if (In.bHasCamera) { R.bHas = true; R.Location = In.CameraLocation; }
		else if (In.bHasPawn) { R.bHas = true; R.Location = In.PawnLocation; }
		return R;
	}

	// ДО 15.09.2026: биллборд-квады 0xC в AAsdaEffectActor::BuildQuads (без оверрайда превью)
	static FOld OldBillboard(const FAsdaViewInputs& In)
	{
		FOld R;
		if (In.bHasCamera) { R.bHas = true; R.Location = In.CameraLocation; }
		else if (In.RenderedLastFrame.Num() > 0) { R.bHas = true; R.Location = In.RenderedLastFrame[0]; }
		return R;
	}

	// ДО 15.09.2026: поворот камеры для частиц в AAsdaEffectActor::BuildQuads (без оверрайда превью): только камера игрока
	static bool OldParticleRotation(const FAsdaViewInputs& In, FRotator& OutRot)
	{
		if (In.bHasCamera) { OutRot = In.CameraRotation; return true; }
		return false;
	}

	// Совпадение точки прежней копии и нового правила
	static bool Same(const FOld& Old, bool bNew, const FAsdaViewPoint& New)
	{
		if (Old.bHas != bNew) { return false; }
		return !Old.bHas || Old.Location.Equals(New.Location, 0.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsdaViewResolveTest, "AsdaReborn.View.Resolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAsdaViewResolveTest::RunTest(const FString& Parameters)
{
	using namespace AsdaViewTest;
	// Различимые точки каждого источника
	const FVector LCam(1, 0, 0), LPawn(2, 0, 0), LOrthoSame(3, 0, 0), LPerspOther(4, 0, 0), LPerspSame(5, 0, 0), LRendered(6, 0, 0);
	const FRotator RCam(-20, 10, 0), RVp(-5, 90, 0);
	const TArray<FVector> RenderedOne = { LRendered };
	const TArray<FVector> RenderedNone;

	// 6 раскладок вьюпортов уровня: пусто / орто этого мира / перспектива чужого / перспектива этого /
	//   перспектива чужого, затем этого / орто этого, затем перспектива чужого
	auto MakeVp = [&](bool bPersp, bool bSame, const FVector& L)
	{
		FAsdaViewInputs::FLevelViewport V; V.bPerspective = bPersp; V.bSameWorld = bSame; V.Location = L; V.Rotation = RVp; return V;
	};
	TArray<TArray<FAsdaViewInputs::FLevelViewport>> Layouts;
	Layouts.AddDefaulted();
	Layouts.Add({ MakeVp(false, true, LOrthoSame) });
	Layouts.Add({ MakeVp(true, false, LPerspOther) });
	Layouts.Add({ MakeVp(true, true, LPerspSame) });
	Layouts.Add({ MakeVp(true, false, LPerspOther), MakeVp(true, true, LPerspSame) });
	Layouts.Add({ MakeVp(false, true, LOrthoSame), MakeVp(true, false, LPerspOther) });

	int32 Cases = 0, FixEffectSky = 0, FixFxLight = 0, FixLightBudget = 0, FixBillboard = 0, FixRotation = 0;
	for (int32 bGame = 0; bGame <= 1; ++bGame)
	for (int32 bCam = 0; bCam <= bGame; ++bCam)          // камера и павн бывают только у игрового мира
	for (int32 bPawn = 0; bPawn <= bGame; ++bPawn)
	for (const TArray<FAsdaViewInputs::FLevelViewport>& Layout : Layouts)
	for (int32 bRendered = 0; bRendered <= 1; ++bRendered)
	{
		FAsdaViewInputs In;
		In.bWorld = true;
		In.bGameWorld = bGame != 0;
		In.bHasCamera = bCam != 0; In.CameraLocation = LCam; In.CameraRotation = RCam;
		In.bHasPawn = bPawn != 0; In.PawnLocation = LPawn;
		In.LevelViewports.Append(Layout);
		In.RenderedLastFrame = bRendered ? RenderedOne : RenderedNone;
		const FString Tag = FString::Printf(TEXT("игровой=%d камера=%d павн=%d вьюпортов=%d отрисовано=%d"),
			bGame, bCam, bPawn, Layout.Num(), bRendered);
		++Cases;

		FAsdaViewPoint New;
		const bool bNew = AsdaView::Resolve(In, New);
		const FAsdaViewInputs::FLevelViewport* SameVp = FirstPerspectiveSameWorld(In);
		const FAsdaViewInputs::FLevelViewport* AnyVp = FirstPerspectiveAnyWorld(In);

		// Инвариант нового правила: вьюпорт ЧУЖОГО мира и ортогональный вьюпорт не выбираются никогда;
		//   игровому миру вьюпорт редактора не источник вовсе.
		TestFalse(*(TEXT("чужой/орто вьюпорт не выбран: ") + Tag), bNew && (New.Location.Equals(LPerspOther) || New.Location.Equals(LOrthoSame)));
		TestFalse(*(TEXT("игровой мир не берёт вьюпорт: ") + Tag), In.bGameWorld && New.Source == EAsdaViewSource::LevelViewport);

		// 1) Свет эффектов — копия, признанная верной (аудит K5): там, где она давала точку, новое правило даёт ТУ ЖЕ.
		//    ★ E7: новое правило добавляет только отрисованный вид там, где у света наблюдателя не было.
		const FOld OFx = OldFxLight(In);
		if (OFx.bHas) { TestTrue(*(TEXT("свет эффектов: точка прежняя: ") + Tag), Same(OFx, bNew, New)); }
		else if (!Same(OFx, bNew, New))
		{
			++FixFxLight;
			TestTrue(*(TEXT("свет эффектов: E7 — только отрисованный вид: ") + Tag), New.Source == EAsdaViewSource::RenderedLastFrame);
		}

		// 2) Эффект и купол: совпадение обязано быть, если у игрового мира есть камера или у мира редактора первый
		//    перспективный вьюпорт — этого мира. Иначе ★ E7 (чужой вьюпорт / вьюпорт редактора в PIE без контроллера).
		const FOld OEs = OldEffectSky(In);
		const bool bEsMustMatch = (In.bGameWorld && In.bHasCamera) || (!In.bGameWorld && AnyVp && AnyVp == SameVp);
		if (bEsMustMatch) { TestTrue(*(TEXT("эффект/купол: точка прежняя: ") + Tag), Same(OEs, bNew, New)); }
		else if (!Same(OEs, bNew, New)) { ++FixEffectSky; }

		// 3) Бюджет теней (только игровые миры): при камере или павне — прежняя точка; иначе ★ E7 — отрисованный вид вместо нуля.
		if (In.bGameWorld)
		{
			const FOld OLb = OldLightBudget(In);
			if (OLb.bHas) { TestTrue(*(TEXT("бюджет теней: точка прежняя: ") + Tag), Same(OLb, bNew, New)); }
			else if (!Same(OLb, bNew, New))
			{
				++FixLightBudget;
				TestTrue(*(TEXT("бюджет теней: E7 — только отрисованный вид: ") + Tag), New.Source == EAsdaViewSource::RenderedLastFrame);
			}
		}

		// 4) Биллборд-квады 0xC: при камере — прежняя точка; в мире без вьюпорта этого мира и без павна — прежний
		//    отрисованный вид. ★ E7: у мира редактора с вьюпортом этого мира — вьюпорт (как у прочих частиц эффекта).
		const FOld OBb = OldBillboard(In);
		const bool bBbMustMatch = In.bHasCamera || (!In.bGameWorld && !SameVp) || (In.bGameWorld && !In.bHasPawn);
		if (bBbMustMatch) { TestTrue(*(TEXT("биллборд 0xC: точка прежняя: ") + Tag), Same(OBb, bNew, New)); }
		else if (!Same(OBb, bNew, New)) { ++FixBillboard; }

		// 5) Поворот для частиц: у камеры — прежний; ★ E7: у мира редактора с вьюпортом этого мира появился поворот вьюпорта.
		FRotator OldRot;
		const bool bOldRot = OldParticleRotation(In, OldRot);
		const bool bNewRot = bNew && New.bHasRotation;
		if (bOldRot) { TestTrue(*(TEXT("поворот частиц прежний: ") + Tag), bNewRot && New.Rotation.Equals(OldRot, 0.0)); }
		else if (bNewRot)
		{
			++FixRotation;
			TestTrue(*(TEXT("поворот частиц: E7 — только вьюпорт этого мира: ") + Tag),
				!In.bGameWorld && New.Source == EAsdaViewSource::LevelViewport && New.Rotation.Equals(RVp, 0.0));
		}

		// 6) Внутри одного эффекта кулл, частицы и биллборды теперь смотрят из ОДНОЙ точки (одна функция) — проверять нечего.
	}

	// Нет мира — нет наблюдателя
	{
		FAsdaViewInputs In;
		FAsdaViewPoint Out;
		TestFalse(TEXT("мира нет — наблюдателя нет"), AsdaView::Resolve(In, Out));
	}

	AddInfo(FString::Printf(TEXT("[наблюдатель] состояний %d; расхождений с прежними копиями (все — ★ E7): эффект/купол %d, свет эффектов %d, "
		"бюджет теней %d, биллборд 0xC %d, поворот частиц %d"), Cases, FixEffectSky, FixFxLight, FixLightBudget, FixBillboard, FixRotation));
	return true;
}
#endif
