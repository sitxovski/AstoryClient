// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActorInternal.h — ★ R2 10.11.2026 (разрез AsdaEffectActor.cpp): ЗАКРЫТЫЙ заголовок частей AsdaEffectActor*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaEffectActor.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "AsdaEffectActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "AsdaSelfTestBatch.h"   // ★ ревизия В2 дедупа №2 (11.10.2026): Quit — выход самотеста по итогу (перехватывается пакетом)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): имя параметра EmissiveBoost
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1): единый своп осей игра→UE
#include "ProceduralMeshComponent.h"
#include "AsdaFxNames.h"   // теги свечения заточки (цвет вершин)
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"   // ★ Ф17 (С-1): уборка сирот обязана отменяться Ctrl+Z — разбор у DropOldestGlowOrphans
#endif
#include "Kismet/GameplayStatics.h"
// ★ Для консольной проверки яркости `asda.Fx.MatDump` (конец файла): чтение скалярного параметра
//   с ЭКЗЕМПЛЯРА материала на секции, отложенные шаги сценария и выполнение консольных команд.
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"   // FMaterialParameterInfo (MaterialTypes.h объявлен устаревшим)
#include "Materials/MaterialInstanceDynamic.h"  // ★ Ф5: MID «силы свечения» поверх материала эмиттера
#include "TimerManager.h"
#include "EngineUtils.h"   // TActorIterator — чтение живых эффектов мира (`asda.Fx.SectionDump`)
#include "UObject/UObjectIterator.h"   // ★ Ф16 (МЕЛК-4): TObjectIterator — CDO запечённых BP_Fx_* (в мире их нет)
#include "World/AsdaFxLightSettings.h"    // ★ 13.09.2026: переопределения свечения предмета/эффекта и сила свечения семейств
#include "World/AsdaFxLightSubsystem.h"   // ★ 13.09.2026: ключ отката asda.Fx.Glow.Overrides, имя .jeb, перезаведение света
#include "Data/AsdaFxLoopRows.h"          // ★ 08.10.2026: ЦИКЛ 1:1 — endMode диска, контейнер, сид-узлы карт (DT_FxLoop)
#include "Engine/DataTable.h"             // ★ 08.10.2026: чтение DT_FxLoop
#include "AsdaDataTableCache.h"            // ★ дедуп №2 В2-C (F3): AsdaDt::Cached — общая загрузка DT_FxLoop
#include "UI/AsdaEffectsSubsystem.h"        // ★ дедуп №2 В2-C (F3): удержание DT_FxLoop на сессию (FAsdaHeldTables)
#include "Engine/GameInstance.h"           // ★ дедуп №2 В2-C (F3): GameInstance эффекта — владелец удержания
#include "Components/SkeletalMeshComponent.h"   // ★ 08.10.2026: холдер режима 2 — порог анимации носителя («≥ порога» с защёлкой)
#include "World/AsdaWorldKind.h"              // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "World/AsdaFxLightComponent.h"        // ★ 15.10.2026: RestVerts строятся только при свете эффекта (IsCarrierAnchorOn)
#if WITH_EDITOR
#include "HAL/PlatformApplicationMisc.h"      // ★ 27.10.2026: буфер обмена кнопок «Копировать/Вставить свет»
#endif

static const int32 kSlots = 100;                       // 100 слотов частиц — как движок (gap_5E8)

// CVarAsdaFxSpawnFromNode: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaFxSpawnFromNode;
// CVarAsdaFxLoopHold: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaFxLoopHold;
// CVarAsdaFxEndTail: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaFxEndTail;

// CVarAsdaFxLoop: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaFxLoop;
// CVarAsdaFxLoopSeeds: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaFxLoopSeeds;
// CVarAsdaFxEnd0Freeze: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaFxEnd0Freeze;

// ★★★ 08.10.2026 (СРЕДН-2/3 ревизии fx_loop): «поле равно» — допуск 1e-3 абсолютно плюс 1e-5 относительно
//   (float32 у значений в тысячи единиц сам по себе даёт ~1e-4; JSON таблицы и ассет — оба float).
static bool AsdaFxLoopNear(double A, double B)
{
	return FMath::Abs(A - B) <= 1e-3 + 1e-5 * FMath::Max(FMath::Abs(A), FMath::Abs(B));
}

// Отражение вектора об плоскость с нормалью N — ровно `_nz()` печки карт (`v − 2(v·n)n`, game-оси);
//   нулевая нормаль = без отражения.
static FVector AsdaFxLoopReflect(const FVector& V, const FVector& N)
{
	return N.IsNearlyZero() ? V : V - 2.0 * FVector::DotProduct(V, N) * N;
}

#if !UE_BUILD_SHIPPING
// GAsdaFxViewSampleCheck: определение — в домашней части (R2).
extern int32 GAsdaFxViewSampleCheck;

// Счётчики стенда (игровой поток)
struct FAsdaFxViewCheckStats
{
	uint64 Sections = 0;          // сверено сборок секций
	uint64 SampleMismatch = 0;    // снимок ≠ прежняя выборка (по битам)
	uint64 BufferMismatch = 0;    // буферы геометрии разошлись хоть в одном байте
	uint64 WithLocation = 0;      // точка наблюдателя была
	uint64 WithRotation = 0;      // поворот наблюдателя был (частицы в плоскости экрана)
	uint64 Override = 0;          // оверрайд превью
	uint64 Billboard = 0;         // исполнялась биллборд-ветка 0xC (есть точка, BillboardMode != 0, не глиф)
	uint64 GameWorld = 0;         // сборка в игровом мире (Tick) — остальное редактор/превью (Tick или прогрев)
	uint64 Verts = 0;             // вершин сверено
};
#endif // !UE_BUILD_SHIPPING

// ★ Инверсия winding при ЗЕРКАЛЬНОМ актор-скейле (нечётное число отрицательных осей, det<0): X-флип вершин
//   выворачивает треугольники → backface (зубцы). Меняем 2-ю↔3-ю вершину каждого треугольника — facing сохранён.
//   Нужно для type-2 эффектов, унаследовавших зеркало кости дирижабля (совпадение хиральности со скелетом).
static void FlipWindingIfMirrored(TArray<int32>& I, const FVector& Scale)
{
	if (Scale.X * Scale.Y * Scale.Z >= 0.f) { return; }
	for (int32 t = 0; t + 2 < I.Num(); t += 3) { Swap(I[t + 1], I[t + 2]); }
}

// AsdaFxVertexColorModeFor: определение — в домашней части (R2).
int32 AsdaFxVertexColorModeFor(const UProceduralMeshComponent* Mesh);

// Цвет вершин для записи в секцию: режим 2 переводит RGB в линейное, альфа не меняется.
static TArray<FLinearColor> AsdaFxVertexColors(const TArray<FLinearColor>& Col, int32 Mode)
{
	if (Mode != 2) { return Col; }
	TArray<FLinearColor> Out;
	Out.Reserve(Col.Num());
	for (const FLinearColor& C : Col)
	{
		const FLinearColor L = FLinearColor::FromSRGBColor(FLinearColor(C.R, C.G, C.B, C.A).ToFColor(false));
		Out.Add(FLinearColor(L.R, L.G, L.B, C.A));
	}
	return Out;
}

// Создание секции эффекта с цветом по режиму меша.
static void AsdaFxCreateSection(UProceduralMeshComponent* Mesh, int32 Section, const TArray<FVector>& V, const TArray<int32>& I,
	const TArray<FVector>& N, const TArray<FVector2D>& UV, const TArray<FLinearColor>& Col)
{
	const int32 Mode = AsdaFxVertexColorModeFor(Mesh);
	Mesh->CreateMeshSection_LinearColor(Section, V, I, N, UV, AsdaFxVertexColors(Col, Mode), TArray<FProcMeshTangent>(), false, /*bSRGBConversion=*/false);
}

// Обновление секции эффекта с цветом по режиму меша. В режиме 0 остаётся поведение движка по умолчанию (кодирование sRGB).
static void AsdaFxUpdateSection(UProceduralMeshComponent* Mesh, int32 Section, const TArray<FVector>& V,
	const TArray<FVector>& N, const TArray<FVector2D>& UV, const TArray<FLinearColor>& Col)
{
	const int32 Mode = AsdaFxVertexColorModeFor(Mesh);
	Mesh->UpdateMeshSection_LinearColor(Section, V, N, UV, AsdaFxVertexColors(Col, Mode), TArray<FProcMeshTangent>(), /*bSRGBConversion=*/Mode == 0);
}

// Категория журнала частей AsdaEffectActor*.cpp: определение — AsdaEffectActor.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaFxManualLock, Log, All );

static void AsdaFxMatDumpOnce(UWorld* W, const FString& Filter, const TCHAR* Tag)
{
	if (!W) { return; }
	TArray<AActor*> Acts;
	UGameplayStatics::GetAllActorsOfClass(W, AAsdaEffectActor::StaticClass(), Acts);
	int32 Shown = 0;
	for (AActor* A : Acts)
	{
		AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(A);
		if (!Fx) { continue; }
		if (!Filter.IsEmpty() && !Fx->EffectName.Contains(Filter)) { continue; }
		UProceduralMeshComponent* PMC = Fx->FindComponentByClass<UProceduralMeshComponent>();
		if (!PMC) { continue; }
		const int32 N = PMC->GetNumMaterials();
		for (int32 i = 0; i < N; ++i)
		{
			UMaterialInterface* MI = PMC->GetMaterial(i);
			if (!MI)
			{
				UE_LOG(LogTemp, Warning, TEXT("[FxMatDump][%s] %s (%s) сек=%d МАТЕРИАЛА НЕТ"),
					Tag, *Fx->GetName(), *Fx->EffectName, i);
				continue;
			}
			// ★ ГРАБЛЯ волны Ф1: чтение ОТСУТСТВУЮЩЕГО имени молча отдаёт 0.0. У C++-версии
			//   `GetScalarParameterValue` возвращает ФЛАГ найденности — печатаем его рядом со значением.
			float Boost = 0.f;
			const bool bFound = MI->GetScalarParameterValue(FMaterialParameterInfo(AsdaMat::kParamEmissiveBoost), Boost);   // ★ В1-C F10
			const UMaterial* Base = MI->GetBaseMaterial();
			UE_LOG(LogTemp, Warning,
				TEXT("[FxMatDump][%s] %s (%s) сек=%d мат=%s класс=%s мастер=%s найден=%d EmissiveBoost=%.3f"),
				Tag, *Fx->GetName(), *Fx->EffectName, i, *MI->GetName(), *MI->GetClass()->GetName(),
				Base ? *Base->GetName() : TEXT("?"), bFound ? 1 : 0, Boost);
			++Shown;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[FxMatDump][%s] секций напечатано %d (фильтр «%s», эффект-акторов в мире %d)"),
		Tag, Shown, *Filter, Acts.Num());
}
