// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КАДРЫ ТЕКСТУР В ИГРЕ» (05.11.2026, bunny-hood): [ПОРТ]
//   `asda.SelfTest.TexShots dir=<папка> [find=<файл>] [swatch=<файл>] [tag=<метка>] [settle=<с>] [quit]`
//   Зачем: 05.11 у 110 цветных текстур оригинала снято ошибочное сжатие «карта нормалей» (BC5 без синего канала искажал цвет,
//   жёлтый капюшон 328). Самотест снимает эти текстуры путём игрока (-game -RenderOffscreen -AsdaNoNet), чтобы сравнить
//   кадры «до» (ассеты из бэкапа) и «после» и сверить с текстурой оригинала.
//     find=<файл>   — пути текстур (/Game/…/Имя, по строке): в текущем мире ищутся меш-компоненты, у которых параметр
//                     материала Tex/BaseTex = эта текстура; камера ставится на первый экземпляр (ISM) или на видимый участок
//                     плоскости (вода: точка, где рельеф ниже плоскости) и снимается кадр `<dir>/<tag>_<текстура>.png`;
//     swatch=<файл> — пути текстур: каждая кладётся на плоскость перед камерой у своего персонажа (материал M_AsdaObject,
//                     параметр Tex) и снимается кадр `<dir>/<tag>_sw_<текстура>.png`;
//     settle        — пауза перед кадром (стриминг мипов), по умолчанию 3 с.
//   Итог: «КАДРЫ-ТЕКСТУР ИТОГ: ok=N fail=M» (fail — текстура не найдена в мире / не загрузилась / нет кадра).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTexShots, Log, All);

namespace AsdaTexShotsTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.TexShots");               // имя команды
	static const TCHAR* kSwatchMaster = TEXT("/Game/Materials/M_AsdaObject.M_AsdaObject");   // мастер образца (параметр Tex)
	static const TCHAR* kPlaneMesh = TEXT("/Engine/BasicShapes/Plane.Plane");             // плоскость 100×100 образца

	// Шаг сценария: true — шаг завершён. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                 // мир
		bool bQuit = false;                       // `quit` в конце
		FString Dir;                              // папка кадров
		FString Tag;                              // метка в имени кадра (до/после)
		float Settle = 3.f;                       // пауза перед кадром
		TArray<FString> Find;                     // текстуры для поиска в мире
		TArray<FString> Swatch;                   // текстуры для образца на плоскости
		int32 Ok = 0, Fail = 0;                   // счётчики
		TArray<FStep> Steps;                      // сценарий
		int32 Idx = 0;                            // текущий шаг
		double StepT0 = 0.0;                      // начало шага
		FDelegateHandle H;                        // подписка на тик мира
		TWeakObjectPtr<ACameraActor> Cam;         // камера кадров
		TWeakObjectPtr<AStaticMeshActor> Plane;   // плоскость образца
		TWeakObjectPtr<AActor> OrigView;          // цель обзора до прогона

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaTexShots, Log,   TEXT("КАДРЫ-ТЕКСТУР ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaTexShots, Error, TEXT("КАДРЫ-ТЕКСТУР FAIL %s"), *What); }
		}
	};

	// Прочитать список путей текстур (строки, начинающиеся с /Game/).
	static TArray<FString> ReadList(const FString& File)
	{
		TArray<FString> Lines, Out;
		FFileHelper::LoadFileToStringArray(Lines, *File);
		for (FString L : Lines)
		{
			L.TrimStartAndEndInline();
			if (L.StartsWith(TEXT("/Game/"))) { Out.Add(L); }
		}
		return Out;
	}

	// Короткое имя текстуры из пути пакета.
	static FString ShortName(const FString& Path) { return FPaths::GetBaseFilename(Path); }

	// Есть ли у материала параметр-текстура Tex/BaseTex с этим путём пакета.
	static bool MatUsesTex(const UMaterialInterface* M, const FString& Path)
	{
		if (!M) { return false; }
		static const FName kNames[] = { FName(TEXT("Tex")), FName(TEXT("BaseTex")) };
		for (const FName& N : kNames)
		{
			UTexture* T = nullptr;
			if (M->GetTextureParameterValue(FHashedMaterialParameterInfo(N), T) && T
				&& T->GetPackage() && T->GetPackage()->GetName() == Path)
			{
				return true;
			}
		}
		return false;
	}

	// Камера кадров (создаётся один раз) и переключение обзора на неё.
	static ACameraActor* CamOf(FState& S)
	{
		UWorld* W = S.W.Get();
		if (!W) { return nullptr; }
		if (!S.Cam.IsValid())
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACameraActor* C = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
			if (C && C->GetCameraComponent())
			{
				C->GetCameraComponent()->SetConstraintAspectRatio(false);
				C->GetCameraComponent()->SetFieldOfView(60.f);
			}
			S.Cam = C;
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				S.OrigView = PC->GetViewTarget();
				PC->SetViewTarget(C);
			}
		}
		return S.Cam.Get();
	}

	// Видна ли точка Target из Eye (первое попадание — сам объект или почти у цели).
	static bool Visible(UWorld* W, const FVector& Eye, const FVector& Target, const UPrimitiveComponent* Comp)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaTexShots), /*bTraceComplex*/true);
		if (!W->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Q)) { return true; }
		return Hit.GetComponent() == Comp || (Hit.ImpactPoint - Target).Size() < 80.f;
	}

	// Поставить камеру на объект: перебор ракурсов, первый с прямой видимостью.
	static bool AimAt(FState& S, const FVector& Center, float Radius, const UPrimitiveComponent* Comp)
	{
		UWorld* W = S.W.Get();
		ACameraActor* C = CamOf(S);
		if (!W || !C) { return false; }
		const float Dist = FMath::Clamp(Radius * 3.f, 350.f, 6000.f);
		static const float kPitch[] = { -20.f, -40.f, -65.f };
		static const float kYaw[] = { 0.f, 90.f, 180.f, 270.f, 45.f, 135.f, 225.f, 315.f };
		for (float P : kPitch)
		{
			for (float Y : kYaw)
			{
				const FRotator R(P, Y, 0.f);
				const FVector Eye = Center - R.Vector() * Dist;
				if (Visible(W, Eye, Center, Comp))
				{
					C->SetActorLocationAndRotation(Eye, R);
					return true;
				}
			}
		}
		const FRotator R(-80.f, 0.f, 0.f);   // нет чистого ракурса — сверху
		C->SetActorLocationAndRotation(Center - R.Vector() * Dist, R);
		return false;
	}

	// Найти в мире объект с текстурой и навести камеру. Возвращает описание (пусто — не найден).
	static FString FindAndAim(FState& S, const FString& Path)
	{
		UWorld* W = S.W.Get();
		if (!W) { return FString(); }
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMeshComponent* SM = *It;
			if (!IsValid(SM) || SM->GetWorld() != W || !SM->IsRegistered() || !SM->GetStaticMesh()) { continue; }
			bool bUse = false;
			for (int32 i = 0; i < SM->GetNumMaterials() && !bUse; ++i) { bUse = MatUsesTex(SM->GetMaterial(i), Path); }
			if (!bUse) { continue; }
			if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(SM))
			{
				if (ISM->GetInstanceCount() <= 0) { continue; }
				FTransform T;
				ISM->GetInstanceTransform(0, T, /*bWorldSpace*/true);
				const FBoxSphereBounds B = SM->GetStaticMesh()->GetBounds().TransformBy(T);
				const bool bClear = AimAt(S, B.Origin, B.SphereRadius, SM);
				return FString::Printf(TEXT("ISM %s экз.0 центр %s радиус %.0f ракурс %s"), *GetNameSafe(SM->GetOwner()),
					*B.Origin.ToCompactString(), B.SphereRadius, bClear ? TEXT("чистый") : TEXT("сверху"));
			}
			// Большая плоскость (вода): точки сетки, где рельеф ниже плоскости — вода там видна.
			const FBoxSphereBounds B = SM->Bounds;
			const float Z = B.Origin.Z;
			FVector Best = B.Origin; float BestD = TNumericLimits<float>::Max(); bool bAny = false;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaTexShotsWater), true);
			Q.AddIgnoredComponent(SM);
			for (int32 gx = -12; gx <= 12; ++gx)
			{
				for (int32 gy = -12; gy <= 12; ++gy)
				{
					const FVector P(B.Origin.X + B.BoxExtent.X * gx / 13.f, B.Origin.Y + B.BoxExtent.Y * gy / 13.f, Z);
					FHitResult Hit;
					const bool bHit = W->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 30000), P - FVector(0, 0, 30000), ECC_Visibility, Q);
					if (bHit && Hit.ImpactPoint.Z < Z - 60.f)
					{
						const float D = FVector2D(P - B.Origin).Size();
						if (D < BestD) { BestD = D; Best = P; bAny = true; }
					}
				}
			}
			if (!bAny) { continue; }
			AimAt(S, Best, 500.f, SM);
			return FString::Printf(TEXT("плоскость %s точка %s"), *GetNameSafe(SM->GetOwner()), *Best.ToCompactString());
		}
		return FString();
	}

	// Положить текстуру на плоскость перед камерой у своего персонажа.
	static bool ShowSwatch(FState& S, const FString& Path)
	{
		UWorld* W = S.W.Get();
		ACameraActor* C = CamOf(S);
		UTexture2D* T = LoadObject<UTexture2D>(nullptr, *(Path + TEXT(".") + ShortName(Path)));
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, kSwatchMaster);
		UStaticMesh* PM = LoadObject<UStaticMesh>(nullptr, kPlaneMesh);
		if (!W || !C || !T || !M || !PM) { return false; }
		APlayerController* PC = W->GetFirstPlayerController();
		const APawn* Me = PC ? PC->GetPawn() : nullptr;
		const FVector Base = (Me ? Me->GetActorLocation() : FVector::ZeroVector) + FVector(0, 0, 400.f);
		const FRotator R(0.f, 0.f, 0.f);
		C->SetActorLocationAndRotation(Base, R);
		if (!S.Plane.IsValid())
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
			if (!A) { return false; }
			A->SetMobility(EComponentMobility::Movable);
			A->GetStaticMeshComponent()->SetStaticMesh(PM);
			A->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			A->GetStaticMeshComponent()->SetCastShadow(false);
			S.Plane = A;
		}
		AStaticMeshActor* A = S.Plane.Get();
		// Плоскость (нормаль +Z) развёрнута к камере: в 120 ед. перед ней, 1.1×1.1 м — закрывает середину кадра.
		A->SetActorLocationAndRotation(Base + R.Vector() * 120.f, FRotator(90.f, 0.f, 0.f));   // тангаж 90: нормаль +Z → −X (к камере)
		A->SetActorScale3D(FVector(1.1f));
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(M, A);
		MID->SetTextureParameterValue(TEXT("Tex"), T);
		A->GetStaticMeshComponent()->SetMaterial(0, MID);
		return true;
	}

	// Заказать кадр.
	static FString Shot(FState& S, const FString& Name)
	{
		const FString Path = FPaths::Combine(S.Dir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		return Path;
	}

	// Конец прогона: вернуть обзор, убрать камеру и плоскость, итог.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		UWorld* W = S.W.Get();
		if (APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr)
		{
			if (S.OrigView.IsValid()) { PC->SetViewTarget(S.OrigView.Get()); }
		}
		if (S.Cam.IsValid()) { S.Cam->Destroy(); }
		if (S.Plane.IsValid()) { S.Plane->Destroy(); }
		AsdaSelfTestKit::Finish(W, kCmd, S.bQuit, LogAsdaTexShots, TEXT("КАДРЫ-ТЕКСТУР"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& SP, UWorld* InW)
	{
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx))
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Сценарий: пауза загрузки карты, затем по каждой текстуре — навести, подождать, снять, проверить файл.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		S->Steps.Add(Wait(6.f));   // карта, рельеф и объекты карты
		auto AddShot = [S, Wait](const FString& Path, bool bSwatch)
		{
			TSharedRef<FString> Desc = MakeShared<FString>();     // что наведено (пусто — не найдено)
			TSharedRef<FString> File = MakeShared<FString>();     // путь кадра
			S->Steps.Add(FStep([S, Path, bSwatch, Desc](float)
			{
				*Desc = bSwatch ? (ShowSwatch(*S, Path) ? FString(TEXT("образец на плоскости")) : FString()) : FindAndAim(*S, Path);
				return true;
			}));
			S->Steps.Add(Wait(S->Settle));
			S->Steps.Add(FStep([S, Path, bSwatch, Desc, File](float)
			{
				if (!Desc->IsEmpty())
				{
					*File = Shot(*S, FString::Printf(TEXT("%s_%s%s"), *S->Tag, bSwatch ? TEXT("sw_") : TEXT(""), *ShortName(Path)));
				}
				return true;
			}));
			S->Steps.Add(Wait(1.5f));
			S->Steps.Add(FStep([S, Path, Desc, File](float)
			{
				const bool bFile = !File->IsEmpty() && IFileManager::Get().FileExists(**File);
				S->Check(bFile, FString::Printf(TEXT("%s: %s → %s"), *Path, Desc->IsEmpty() ? TEXT("НЕ НАЙДЕНО") : **Desc,
					File->IsEmpty() ? TEXT("кадра нет") : **File));
				return true;
			}));
		};
		for (const FString& P : S->Find)   { AddShot(P, false); }
		for (const FString& P : S->Swatch) { AddShot(P, true); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestTexShots(
	TEXT("asda.SelfTest.TexShots"),
	TEXT("Кадры текстур в игре: `dir=<папка> [find=<файл путей>] [swatch=<файл путей>] [tag=<метка>] [settle=<с>] [quit]`. "
	     "find — объекты текущей карты с этой текстурой (Tex/BaseTex), swatch — образец на плоскости перед камерой."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaTexShotsTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Tag = TEXT("shot");
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("dir=")))         { SP->Dir = FPaths::ConvertRelativePathToFull(P.Mid(4)); }
			else if (P.StartsWith(TEXT("find=")))   { SP->Find = ReadList(P.Mid(5)); }
			else if (P.StartsWith(TEXT("swatch="))) { SP->Swatch = ReadList(P.Mid(7)); }
			else if (P.StartsWith(TEXT("tag=")))    { SP->Tag = P.Mid(4); }
			else if (P.StartsWith(TEXT("settle="))) { SP->Settle = FMath::Max(0.5f, FCString::Atof(*P.Mid(7))); }
		}
		if (SP->Dir.IsEmpty() || (SP->Find.Num() == 0 && SP->Swatch.Num() == 0))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нужны dir= и непустой find= или swatch="));
			return;
		}
		IFileManager::Get().MakeDirectory(*SP->Dir, /*Tree*/true);
		UE_LOG(LogAsdaTexShots, Log, TEXT("КАДРЫ-ТЕКСТУР старт: найти %d, образцов %d, папка %s"), SP->Find.Num(), SP->Swatch.Num(), *SP->Dir);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
