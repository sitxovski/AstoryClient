// Copyright (c) 2026 @sitxovski (Discord)
// Реализация обслуживания StaticMesh: снятие `UStaticMesh::bDoFastBuild`.
// Разбор причины дефекта — в заголовке AsdaMeshMaintenance.h.
#include "Editor/AsdaMeshMaintenance.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DistanceFieldAtlas.h"      // ★ Сет E-1: FDistanceFieldVolumeData, GDistanceFieldAsyncQueue
#include "Engine/StaticMesh.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "MeshCardBuild.h"           // ★ Сет E-1: GCardRepresentationAsyncQueue (карточки Lumen строятся следом за DF)
#include "AssetCompilingManager.h"    // ★ Сет E-1: FAssetCompilingManager::ProcessAsyncTasks — прокачка всех фоновых сборок
#include "StaticMeshCompiler.h"       // ★ Сет E-1: FStaticMeshCompilingManager — фоновые сборки мешей идут ПЕРЕД DF
#include "StaticMeshResources.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
// ★ 16.09.2026: общий список пакетов сцены логина (Tools/data/asda_login_packages.json) — тот же, что у Python и у сторожа.
#include "Editor/AsdaLoginPackageList.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMeshFix, Log, All);

bool UAsdaMeshMaintenance::IsFastBuildStaticMesh(UStaticMesh* Mesh)
{
	return Mesh != nullptr && Mesh->bDoFastBuild != 0;
}

bool UAsdaMeshMaintenance::ClearFastBuildStaticMesh(UStaticMesh* Mesh, bool bRebuild)
{
	if (Mesh == nullptr || Mesh->bDoFastBuild == 0)
	{
		return false;
	}

	Mesh->Modify();
	Mesh->bDoFastBuild = 0;

#if WITH_EDITOR
	if (bRebuild)
	{
		// Честная пересборка обычным билдером (MeshBuilderModule::BuildMesh) — она и заполняет
		// FStaticMeshRenderData::Bounds. Без неё сборка всё равно произойдёт при следующей
		// загрузке/куке, уже по обычной ветке Cache.
		UStaticMesh::FBuildParameters BuildParameters;
		BuildParameters.bInSilent = true;
		Mesh->Build(BuildParameters);
	}
#endif

	return true;
}

bool UAsdaMeshMaintenance::RefreshDerivedDataKey(UStaticMesh* Mesh)
{
#if WITH_EDITOR
	if (Mesh == nullptr || !Mesh->IsSourceModelValid(0) || !Mesh->IsMeshDescriptionValid(0))
	{
		return false;
	}
	// Описание должно быть поднято в кэш источника — иначе коммитить нечего.
	if (Mesh->GetMeshDescription(0) == nullptr)
	{
		return false;
	}
	Mesh->Modify();
	UStaticMesh::FCommitMeshDescriptionParams Params;
	Params.bMarkPackageDirty = false;   // пакет помечаем сами, из главного потока
	Params.bUseHashAsGuid = true;       // идентификатор = хеш содержимого → ключ DDC другой и стабильный
	Mesh->CommitMeshDescription(0, Params);
	return true;
#else
	return false;
#endif
}

/** Сохранение пакета ассета на диск. */
static bool SaveAssetPackage(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.Error = GWarn;
	return UPackage::SavePackage(Package, nullptr, *Filename, SaveArgs);
}

/** Список всех StaticMesh под указанными корнями реестра ассетов. */
static void CollectStaticMeshes(const TArray<FString>& Roots, TArray<FAssetData>& OutAssets)
{
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();
	Registry.SearchAllAssets(/*bSynchronousSearch=*/true);

	for (const FString& Root : Roots)
	{
		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*Root));
		Registry.GetAssets(Filter, OutAssets);
	}
}

/**
 * ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (16.09.2026, Reports/dedup_audit/login_writers_scan.md, раздел 9).
 *
 * ★ Зачем здесь. RefreshDerivedDataKeysInPaths и ClearFastBuildInPaths — UFUNCTION(BlueprintCallable) с ПРОИЗВОЛЬНЫМ
 *   массивом корней и FARFilter.bRecursivePaths=true: корень «/Game» или «/Game/AsdaAssets/Models» накрывает статик-меши
 *   сцены логина (Models/Backdrop/SM_m*, cre_creation01_static, blackscreen) и СОХРАНЯЕТ пакет каждого. Обёртки в Tools
 *   (asda_clear_fastbuild_flag.py, asda_refresh_mesh_ddc_key.py) корни уже отсеивают через login_expand_roots, но ПРЯМОЙ
 *   вызов из Blueprint или консоли в ИНТЕРАКТИВНОМ редакторе не прикрыт ничем: AsdaLoginSaveGuard ставит делегаты и
 *   файловую обёртку ТОЛЬКО в автоматической сессии (коммандлет, -unattended, -ExecutePythonScript).
 * ★ Правило. Пакеты сцены логина эти функции пропускают ВСЕГДА — кроме явного ASDA_ALLOW_LOGIN_WRITE=1 при запуске
 *   процесса (IsLoginPackageBlocked проверяет разрешение первым, с ним поведение прежнее и список даже не читается).
 * ★ Двойная фильтрация с обёртками Tools допустима и намеренна: список один и тот же, у уже отсеянных корней пропускать
 *   нечего (Skipped=0, строки в логе нет), а прямой вызов теперь закрыт. Отсев идёт ДО загрузки ассетов: Asset.GetAsset()
 *   на пакете логина даже не вызывается.
 */
static int32 DropLoginPackages(TArray<FAssetData>& Assets, const TCHAR* Tool)
{
	const int32 Before = Assets.Num();
	// ★ Поправка ревизии 16.09.2026 (МЕЛК-6). Разрешение читается ОДИН РАЗ НА ВЫЗОВ, а не на каждый ассет:
	//   IsLoginPackageBlocked дёргает FPlatformMisc::GetEnvironmentVariable внутри, и на корне «/Game» это 20+ тысяч
	//   чтений окружения подряд. Один раз именно НА ВЫЗОВ (а не на процесс): в интерактивной сессии владелец может
	//   выставить переменную окружения и позвать функцию заново — она подхватится, как и раньше.
	const bool bWriteAllowed = AsdaLoginPackageList::IsAssetWriteAllowedByEnv();
	if (bWriteAllowed)
	{
		return 0;   // с ASDA_ALLOW_LOGIN_WRITE=1 поведение прежнее: ничего не отсеивается, список даже не читается
	}
	const AsdaLoginPackageList::FMatcher& Matcher = AsdaLoginPackageList::GetSharedMatcher();
	Assets.RemoveAll([&Matcher](const FAssetData& Asset)
	{
		return Matcher.Classify(Asset.PackageName.ToString()) == AsdaLoginPackageList::EOwnership::Owned;
	});
	const int32 Skipped = Before - Assets.Num();
	if (Skipped > 0)
	{
		UE_LOG(LogAsdaMeshFix, Warning,
			TEXT("[LOGIN-GUARD] %s: пропущено пакетов сцены логина %d из %d (правка и сохранение — только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"),
			Tool, Skipped, Before);
	}
	return Skipped;
}

FString UAsdaMeshMaintenance::RefreshDerivedDataKeysInPaths(const TArray<FString>& Roots, bool bDryRun)
{
	TArray<FAssetData> Assets;
	CollectStaticMeshes(Roots, Assets);
	const int32 SkippedLogin = DropLoginPackages(Assets, TEXT("RefreshDerivedDataKeysInPaths"));

	int32 Seen = 0, Refreshed = 0, Saved = 0, Errors = 0, FlagsCleared = 0;
	for (const FAssetData& Asset : Assets)
	{
		++Seen;
		UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
		if (Mesh == nullptr)
		{
			++Errors;
			continue;
		}
		if (bDryRun)
		{
			continue;
		}
		// Защита от повторного взведения: если флаг ещё стоит — снять здесь же, иначе новая
		// запись DDC будет ровно так же испорчена «быстрой» веткой.
		if (Mesh->bDoFastBuild != 0)
		{
			Mesh->Modify();
			Mesh->bDoFastBuild = 0;
			++FlagsCleared;
		}
		if (!RefreshDerivedDataKey(Mesh))
		{
			++Errors;
			continue;
		}
		++Refreshed;
		if (SaveAssetPackage(Mesh))
		{
			++Saved;
		}
		else
		{
			++Errors;
			UE_LOG(LogAsdaMeshFix, Error, TEXT("не сохранён пакет %s"), *Mesh->GetOutermost()->GetName());
		}
		if ((Refreshed % 500) == 0)
		{
			UE_LOG(LogAsdaMeshFix, Display, TEXT("...обновлено %d из %d просмотренных"), Refreshed, Seen);
			CollectGarbage(RF_NoFlags);
		}
	}

	const FString Report = FString::Printf(
		TEXT("просмотрено %d, ключ обновлён %d, попутно снят флаг %d, сохранено %d, ошибок %d, пропущено пакетов сцены логина %d"),
		Seen, Refreshed, FlagsCleared, Saved, Errors, SkippedLogin);
	UE_LOG(LogAsdaMeshFix, Display, TEXT("[ddckey] %s"), *Report);
	return Report;
}

FString UAsdaMeshMaintenance::ClearFastBuildInPaths(const TArray<FString>& Roots, bool bDryRun, bool bRebuild)
{
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();
	Registry.SearchAllAssets(/*bSynchronousSearch=*/true);

	TArray<FAssetData> Assets;
	for (const FString& Root : Roots)
	{
		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*Root));
		Registry.GetAssets(Filter, Assets);
	}
	// ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА — см. разбор у DropLoginPackages выше (корни здесь такие же произвольные).
	const int32 SkippedLogin = DropLoginPackages(Assets, TEXT("ClearFastBuildInPaths"));

	int32 Seen = 0, WasSet = 0, Cleared = 0, Saved = 0, Errors = 0;
	for (const FAssetData& Asset : Assets)
	{
		++Seen;
		UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
		if (Mesh == nullptr)
		{
			++Errors;
			continue;
		}
		if (Mesh->bDoFastBuild == 0)
		{
			continue;
		}
		++WasSet;
		if (bDryRun)
		{
			continue;
		}
		if (!ClearFastBuildStaticMesh(Mesh, bRebuild))
		{
			++Errors;
			continue;
		}
		++Cleared;

		UPackage* Package = Mesh->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = GWarn;
		if (UPackage::SavePackage(Package, nullptr, *Filename, SaveArgs))
		{
			++Saved;
		}
		else
		{
			++Errors;
			UE_LOG(LogAsdaMeshFix, Error, TEXT("не сохранён пакет %s"), *Package->GetName());
		}

		// Периодическая сборка мусора: иначе 20 тыс. загруженных мешей не помещаются в память.
		if ((Cleared % 500) == 0)
		{
			UE_LOG(LogAsdaMeshFix, Display, TEXT("...снято %d из %d просмотренных"), Cleared, Seen);
			CollectGarbage(RF_NoFlags);
		}
	}

	const FString Report = FString::Printf(
		TEXT("просмотрено %d, было взведено %d, снято %d, сохранено %d, ошибок %d"),
		Seen, WasSet, Cleared, Saved, Errors);
	UE_LOG(LogAsdaMeshFix, Display, TEXT("[fastbuild] %s"), *Report);
	return Report;
}


// =================================================================================================
// ★ Сет E-1 (06.09.2026): Mesh Distance Fields
// =================================================================================================

FAsdaDistanceFieldInfo UAsdaMeshMaintenance::GetDistanceFieldInfo(UStaticMesh* Mesh)
{
	FAsdaDistanceFieldInfo Info;
	if (Mesh == nullptr)
	{
		return Info;
	}
#if WITH_EDITOR
	Info.bPerMeshFlag = Mesh->bGenerateMeshDistanceField != 0;
	if (Mesh->IsSourceModelValid(0))
	{
		const FMeshBuildSettings& BS = Mesh->GetSourceModel(0).BuildSettings;
		Info.ResolutionScale = BS.DistanceFieldResolutionScale;
		Info.bTwoSidedSetting = BS.bGenerateDistanceFieldAsIfTwoSided != 0;
	}
#endif
	const FStaticMeshRenderData* RD = Mesh->GetRenderData();
	if (RD != nullptr)
	{
		Info.BoundsSize = RD->Bounds.BoxExtent * 2.0;
		if (RD->LODResources.IsValidIndex(0) && RD->LODResources[0].DistanceFieldData != nullptr)
		{
			const FDistanceFieldVolumeData* DF = RD->LODResources[0].DistanceFieldData;
			Info.bHasData = true;
			Info.bAsyncBuilding = DF->bAsyncBuilding;
			Info.bValid = DF->IsValid();
			Info.bMostlyTwoSided = DF->bMostlyTwoSided;
			Info.Mip0Bricks = FIntVector(DF->Mips[0].IndirectionDimensions.X, DF->Mips[0].IndirectionDimensions.Y, DF->Mips[0].IndirectionDimensions.Z);
			Info.Mip0NumBricks = DF->Mips[0].NumDistanceFieldBricks;
			Info.ResourceBytes = (int64)DF->GetResourceSizeBytes();
		}
	}
	return Info;
}

bool UAsdaMeshMaintenance::SetDistanceFieldBuildSettings(UStaticMesh* Mesh, float ResolutionScale, bool bTwoSided, bool bRebuild)
{
#if WITH_EDITOR
	if (Mesh == nullptr || !Mesh->IsSourceModelValid(0))
	{
		return false;
	}
	// ★ Сет E-2 (06.09.2026): нижняя граница 0.0, а не 0.1 — scale 0 = «DF у меша НЕ строится» (MeshDistanceFieldUtilities.cpp:602
	//   `if (DistanceFieldResolutionScale > 0)`: поле остаётся пустым, IsValid()=false, примитив не попадает в DF-сцену). Это ЕДИНСТВЕННЫЙ
	//   честный per-mesh выключатель при проектном r.GenerateMeshDistanceFields=1: флаг bGenerateMeshDistanceField при нём ИГНОРИРУЕТСЯ
	//   (StaticMesh.cpp:4564 `CVar!=0 || bGenerateMeshDistanceField`). Используется DF-чисткой Models/Char|Items|Mob (≈3.5 ГБ DF, review_setE1 В-1).
	const float Scale = FMath::Clamp(ResolutionScale, 0.0f, 8.0f);
	FMeshBuildSettings& BS = Mesh->GetSourceModel(0).BuildSettings;
	const bool bChanged = !FMath::IsNearlyEqual(BS.DistanceFieldResolutionScale, Scale) || ((BS.bGenerateDistanceFieldAsIfTwoSided != 0) != bTwoSided);
	if (!bChanged)
	{
		return false;
	}
	Mesh->Modify();
	BS.DistanceFieldResolutionScale = Scale;
	BS.bGenerateDistanceFieldAsIfTwoSided = bTwoSided;
	if (bRebuild)
	{
		UStaticMesh::FBuildParameters BuildParameters;
		BuildParameters.bInSilent = true;
		Mesh->Build(BuildParameters);
	}
	return true;
#else
	return false;
#endif
}

int32 UAsdaMeshMaintenance::GetOutstandingDistanceFieldBuilds()
{
	int32 N = 0;
	if (GDistanceFieldAsyncQueue != nullptr)
	{
		N += GDistanceFieldAsyncQueue->GetNumOutstandingTasks();
	}
	if (GCardRepresentationAsyncQueue != nullptr)
	{
		N += GCardRepresentationAsyncQueue->GetNumOutstandingTasks();
	}
	return N;
}

bool UAsdaMeshMaintenance::SetGenerateMeshDistanceFieldFlag(UStaticMesh* Mesh, bool bEnable)
{
	if (Mesh == nullptr || (Mesh->bGenerateMeshDistanceField != 0) == bEnable)
	{
		return false;
	}
	Mesh->Modify();
	Mesh->bGenerateMeshDistanceField = bEnable ? 1 : 0;
	return true;
}

int32 UAsdaMeshMaintenance::GetOutstandingStaticMeshCompilations()
{
#if WITH_EDITOR
	return FStaticMeshCompilingManager::Get().GetNumRemainingMeshes();
#else
	return 0;
#endif
}

int32 UAsdaMeshMaintenance::WaitForDistanceFieldBuilds(float TimeoutSec)
{
	const double T0 = FPlatformTime::Seconds();
	double NextLog = T0;
	while ((GetOutstandingDistanceFieldBuilds() > 0 || GetOutstandingStaticMeshCompilations() > 0) && FPlatformTime::Seconds() - T0 < TimeoutSec)
	{
		// Завершённые фоновые задачи «доводятся» (DDC Put, копия в рендер-данные) только здесь, на игровом потоке.
#if WITH_EDITOR
		// ProcessAsyncTasks у FStaticMeshCompilingManager приватный — прокачиваем через общий менеджер (он обходит всех, включая DF/карточки).
		FAssetCompilingManager::Get().ProcessAsyncTasks(false);
#endif
		if (GDistanceFieldAsyncQueue != nullptr)
		{
			GDistanceFieldAsyncQueue->ProcessAsyncTasks();
		}
		if (GCardRepresentationAsyncQueue != nullptr)
		{
			GCardRepresentationAsyncQueue->ProcessAsyncTasks();
		}
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextLog)
		{
			// Пульс в лог каждые 30 с: обёртка run_headless_safe.py режет прогон по простою лога, а ожидание бывает долгим.
			UE_LOG(LogAsdaMeshFix, Display, TEXT("DF-WAIT %.0f с: мешей в сборке %d, DF %d, карточек %d"),
				Now - T0, GetOutstandingStaticMeshCompilations(),
				GDistanceFieldAsyncQueue ? GDistanceFieldAsyncQueue->GetNumOutstandingTasks() : 0,
				GCardRepresentationAsyncQueue ? GCardRepresentationAsyncQueue->GetNumOutstandingTasks() : 0);
			NextLog = Now + 30.0;
		}
		FPlatformProcess::Sleep(0.05f);
	}
	return GetOutstandingDistanceFieldBuilds() + GetOutstandingStaticMeshCompilations();
}
