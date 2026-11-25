// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_CityWar.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem.cpp, дословный перенос): война за города: подмена текстур флагов и площадок.
//   Общее для частей — в AsdaWorldSubsystem_Private.h; шапка и реверс — в AsdaWorldSubsystem.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер

#include "AsdaNetSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWalkGrid.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "Components/MeshComponent.h"          // подошва цели: низ ВИДИМОЙ геометрии (TargetSoleZ)
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"   // ★ 09.11: экземпляры площадки города
#include "Algo/Reverse.h"
#include "ProceduralMeshComponent.h"               // ★ 09.11: площадка города по рельефу
#include "StaticMeshResources.h"                   // ★ 09.11: вершины/UV квада площадки
#include "Engine/Texture2D.h"                      // ★ 09.11: закрепление мипов текстуры площадки
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/TextureRenderTarget2D.h"   // RT эмблемы клана — это UTexture-наследник
#include "World/AsdaClanFlag.h"              // FAsdaClanFlagData::FromWire (флаг клана из 6542)
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem*.cpp

// Показать игровой мир: снять загрузочный экран, закрыть окна логина/char-select, включить игровой ввод.
//   Идемпотентно — зовётся по спавну игрока (4003) и как страховка по 4055/4056.
// ══════════ ФЛАГИ И ПЛОЩАДКИ ВОЙНЫ ЗА ГОРОДА — подмена текстур мешей (см. шапку в .h) ══════════
// Порядок наборов = КОД ФРАКЦИИ: 0 Свет, 1 Тьма, 2 Хаос, 3 «не занят» — ровно порядок загрузки в
//   `CMap_LoadUniconWarTextures@0x9C88E0`.
static const TCHAR* kUniconFlagTex[4] = {
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconLightFlag.uniconLightFlag"),
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconDarkFlag.uniconDarkFlag"),
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconChaosFlag.uniconChaosFlag"),
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconNoConqureFlag.uniconNoConqureFlag"),
};
static const TCHAR* kUniconFloorTex[4] = {
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconLightFloor.uniconLightFloor"),
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconDarkFloor.uniconDarkFloor"),
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconChaosFloor.uniconChaosFloor"),
	TEXT("/Game/AsdaAssets/Textures/Maps/uniconNotConqureFloor.uniconNotConqureFloor"),
};

// ★ 09.11.2026 [ИСПРАВЛЕНИЕ] (баг alpha-7.4.0 «эмблемы захвата висят в воздухе», все 3 города).
//   Эмблема-площадка — ОДИН плоский квад ≈2000×2000 (4 вершины, `SM_unicon03`, экземпляры ISM `I_unicon03` на
//   `AsdaMapObjects_<карта>`), поставленный по данным оригинала (Pos.POS). Земля под ним неровная: замер
//   `asda.SelfTest.CityWarFloor` — средний зазор до видимой земли 2…27 ед., местами до 77 (Пламения), плюс тень
//   маскированного квада. Плоскость не может лечь на бугристый рельеф, сдвиг вниз лишь врежет её в землю.
//   ПРАВКА: при входе на город каждый экземпляр заменяется сеткой 32×32 в UV исходного квада, каждая вершина — на
//   видимой земле (статичная геометрия под max(квад, рельеф) + 60: рельеф, мощение, бордюр) + 5 ед.; материал тот же (перекраска по
//   хозяину идёт прежним путём), тени нет; исходный квад скрыт. Карты не правятся.
// ★ 10.11.2026 [ИСПРАВЛЕНИЕ] (alpha-7.5.0: «висит на высоте щиколотки, режет ботинки, по краю ступенька»): сетка 32×32 с
//   подъёмом 5 заменена площадкой ИЗ ТРЕУГОЛЬНИКОВ .TER, обрезанных по квадрату эмблемы, подъём 1 (разбор — в `AsdaCityFloor`).
static ASDA_DEV_CVAR(int32, CVarCityFloorConform,
	TEXT("asda.War.FloorConform"),
	1,
	TEXT("Эмблема захвата города (площадка UNICON03): 1 = лежит по рельефу (сетка по земле вместо плоского квада); "
	     "0 = прежний плоский квад (висит над низинами). Действует при следующем входе на карту."),
	ECVF_Default);

namespace AsdaCityFloor {
	// ★ 10.11.2026 (жалоба на alpha-7.5.0: «висит на высоте щиколотки, режет ботинки, по краю ступенька»).
	//   Прежняя сетка 33×33 (шаг ~61 ед.) ставила вершины на землю трассой, но между вершинами шла по СВОИМ прямым, а земля —
	//   по треугольникам .TER (шаг 100, диагональ (minX,minZ)—(maxX,maxZ)): сетка то проваливалась под землю, то висела, и
	//   прятать провалы приходилось подъёмом всей сетки на 5 ед. — это и есть «на высоте щиколотки». Теперь площадка
	//   собирается ИЗ ТЕХ ЖЕ треугольников, что и земля: каждый треугольник ячейки .TER режется по квадрату эмблемы, высота
	//   каждой вершины — `AsdaGround::GroundZ` (та же интерполяция, по которой ходит персонаж и построен рендер-меш
	//   `SM_terrain`, расхождение 0.0 на всех трёх городах). Эмблема лежит в плоскости земли, подъём — kLift (от мерцания).
	//   Трасс и коллизии больше нет: где поверх земли стоит объект карты (настил лавки), он рисуется поверх края эмблемы.
	static constexpr float kLift = 1.f;   // подъём над плоскостью земли, ед. (против мерцания; щиколотка ≈ 10–15)

	// Сетка-заменитель одного экземпляра площадки.
	struct FPatch
	{
		TWeakObjectPtr<UMeshComponent> Src;                  // исходный компонент площадки (ISM `I_unicon03`)
		int32 MatIndex = 0;                                  // слот материала площадки на исходном компоненте
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;       // сетка по земле
	};
	static TArray<FPatch> GPatches;                          // сетки текущего мира

	// Снять сетки прошлого мира (компоненты принадлежат его актёру — уничтожены вместе с ним).
	static void Forget(UWorld* W)
	{
		GPatches.RemoveAll([W](const FPatch& P) { return !P.Mesh.IsValid() || P.Mesh->GetWorld() != W; });
	}

	// Отсечь выпуклый многоугольник Poly полуплоскостью «слева от ребра A→B» (многоугольник отсечения — против часовой).
	static void ClipByEdge(TArray<FVector2D>& Poly, const FVector2D& A, const FVector2D& B)
	{
		TArray<FVector2D> Out;
		const int32 N = Poly.Num();
		auto Side = [&A, &B](const FVector2D& P) { return FVector2D::CrossProduct(B - A, P - A); };
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& P = Poly[i];
			const FVector2D& Q = Poly[(i + 1) % N];
			const double Sp = Side(P), Sq = Side(Q);
			if (Sp >= 0.0) { Out.Add(P); }
			if ((Sp >= 0.0) != (Sq >= 0.0)) { Out.Add(P + (Q - P) * (Sp / (Sp - Sq))); }
		}
		Poly = MoveTemp(Out);
	}

	// Построить сетки по всем экземплярам компонента площадки MC (слот материала MatIndex). Возврат — сколько построено.
	//   ★ Вершины меша НЕ читаются: в собранной игре CPU-копии вершинных буферов нет (у `SM_unicon03` нет bAllowCPUAccess).
	//   Квад берётся из ГАБАРИТОВ меша (есть и в собранной игре) и раскладки UV, снятой с ассетов всех трёх городов
	//   09.11 (`asda.SelfTest.CityWarFloor`, строка «раскладка»): локальный квадрат на Z=0, U = (y − minY)/ширина,
	//   V = (maxX − x)/ширина. В редакторе, где вершины есть, раскладка сверяется и расхождение пишется в журнал.
	static int32 Build(UWorld* W, UMeshComponent* MC, int32 MatIndex, const UAsdaWalkGrid* Grid)
	{
		UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(MC);
		const UStaticMesh* SM = SMC ? SMC->GetStaticMesh() : nullptr;
		if (!SM || !Grid || Grid->CellSize <= 0.f || Grid->Width <= 0 || Grid->Height <= 0
			|| Grid->VertHeights.Num() != (Grid->Width + 1) * (Grid->Height + 1))
		{
			UE_LOG(LogAsdaWorld, Warning, TEXT("[война] площадка: нет меша или сетки высот карты — остаётся исходный квад"));
			return 0;
		}
		const FBox LB = SM->GetBoundingBox();
		if (!LB.IsValid || LB.GetSize().X < 1.f || LB.GetSize().Y < 1.f) { return 0; }
		auto LocalAt = [&LB](double U, double V) { return FVector(LB.Max.X - V * (LB.Max.X - LB.Min.X), LB.Min.Y + U * (LB.Max.Y - LB.Min.Y), LB.GetCenter().Z); };
#if WITH_EDITOR
		// Сверка раскладки с настоящими вершинами (только где CPU-копия есть — редакторный бинарь).
		if (const FStaticMeshRenderData* RD = SM->GetRenderData())
		{
			const FStaticMeshLODResources& L0 = RD->LODResources[0];
			const FPositionVertexBuffer& PVB = L0.VertexBuffers.PositionVertexBuffer;
			const FStaticMeshVertexBuffer& VB = L0.VertexBuffers.StaticMeshVertexBuffer;
			if (PVB.GetVertexData() && VB.GetTangentData())
			{
				for (uint32 v = 0; v < PVB.GetNumVertices(); ++v)
				{
					const FVector2f Uv = VB.GetVertexUV(v, 0);
					const FVector Want = LocalAt(Uv.X, Uv.Y);
					if (FVector::Dist(Want, FVector(PVB.VertexPosition(v))) > 1.0)
					{
						UE_LOG(LogAsdaWorld, Warning, TEXT("[война] площадка %s: раскладка UV не совпала (вершина %u) — сетка может лечь неверно"), *SM->GetName(), v);
						break;
					}
				}
			}
		}
#endif
		TArray<FTransform> Xf;
		if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(SMC))
		{
			for (int32 k = 0; k < ISM->GetInstanceCount(); ++k) { FTransform T; ISM->GetInstanceTransform(k, T, /*bWorldSpace*/ true); Xf.Add(T); }
		}
		else { Xf.Add(SMC->GetComponentTransform()); }
		AActor* Owner = MC->GetOwner();
		if (!Owner) { return 0; }
		const double C = Grid->CellSize;
		int32 Built = 0;
		for (const FTransform& T : Xf)
		{
			// Квадрат эмблемы в мире (XY UE), против часовой стрелки.
			TArray<FVector2D> Pad;
			for (const FVector2D& Uv : { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) })
			{
				const FVector P = T.TransformPosition(LocalAt(Uv.X, Uv.Y));
				Pad.Add(FVector2D(P.X, P.Y));
			}
			double Area2 = 0.0;
			for (int32 i = 0; i < 4; ++i) { Area2 += FVector2D::CrossProduct(Pad[i], Pad[(i + 1) % 4]); }
			if (Area2 < 0.0) { Algo::Reverse(Pad); }
			FBox2D PadBox(ForceInit);
			for (const FVector2D& P : Pad) { PadBox += P; }
			// Ячейки .TER под квадратом. Игра: gx = UE.Y, gz = −UE.X (`AsdaCoord`).
			const int32 Tx0 = FMath::Max(0, FMath::FloorToInt(PadBox.Min.Y / C)), Tx1 = FMath::Min(Grid->Width - 1, FMath::FloorToInt(PadBox.Max.Y / C));
			const int32 Tz0 = FMath::Max(0, FMath::FloorToInt(-PadBox.Max.X / C)), Tz1 = FMath::Min(Grid->Height - 1, FMath::FloorToInt(-PadBox.Min.X / C));
			auto Ue = [](double Gx, double Gz) { return FVector2D(-Gz, Gx); };
			const FTransform Inv = T.Inverse();
			const FVector UDir = T.TransformVectorNoScale(FVector(0, 1, 0)).GetSafeNormal();   // направление роста U в мире
			TArray<FVector> Pos; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FColor> Col; TArray<FProcMeshTangent> Tan; TArray<int32> Tri;
			bool bHeightFail = false;   // высота не взялась из сетки — экземпляр не заменяем (остаётся исходный квад)
			auto AddVert = [&](const FVector2D& P)
			{
				// Высота — из сетки .TER ЭТОЙ карты (UE→игра: gx = UE.Y, gz = −UE.X), как `AsdaGround::GroundZ`.
				float Z = T.GetLocation().Z;
				if (!Grid->GetGroundY((float)P.Y, (float)-P.X, Z)) { bHeightFail = true; }
				const FVector L = Inv.TransformPosition(FVector(P.X, P.Y, T.GetLocation().Z));
				Pos.Add(FVector(P.X, P.Y, Z + kLift));
				Nrm.Add(FVector::UpVector);
				UV.Add(FVector2D((L.Y - LB.Min.Y) / (LB.Max.Y - LB.Min.Y), (LB.Max.X - L.X) / (LB.Max.X - LB.Min.X)));
				Col.Add(FColor::White);
				Tan.Add(FProcMeshTangent(UDir, false));
				return Pos.Num() - 1;
			};
			for (int32 tz = Tz0; tz <= Tz1; ++tz)
			{
				for (int32 tx = Tx0; tx <= Tx1; ++tx)
				{
					// Два треугольника ячейки — ровно как `UAsdaWalkGrid::GetGroundY` (диагональ (minX,minZ)—(maxX,maxZ)).
					const FVector2D A = Ue(tx * C, tz * C), B = Ue((tx + 1) * C, tz * C), D = Ue((tx + 1) * C, (tz + 1) * C), E = Ue(tx * C, (tz + 1) * C);
					for (int32 h = 0; h < 2; ++h)
					{
						TArray<FVector2D> Poly;
						if (h == 0) { Poly = { A, B, D }; } else { Poly = { A, D, E }; }
						for (int32 e = 0; e < 4 && Poly.Num() >= 3; ++e) { ClipByEdge(Poly, Pad[e], Pad[(e + 1) % 4]); }
						if (Poly.Num() < 3) { continue; }
						// Крошечный кусок у самого ребра треугольника — не нужен.
						double A2 = 0.0;
						for (int32 i = 0; i < Poly.Num(); ++i) { A2 += FVector2D::CrossProduct(Poly[i], Poly[(i + 1) % Poly.Num()]); }
						if (FMath::Abs(A2) < 1e-2) { continue; }
						TArray<int32> Idx;
						for (const FVector2D& P : Poly) { Idx.Add(AddVert(P)); }
						for (int32 i = 1; i + 1 < Idx.Num(); ++i)
						{
							// Лицом вверх: лицевая сторона UE — та, куда смотрит (C − A) × (B − A).
							const int32 a = Idx[0], b = Idx[i], c = Idx[i + 1];
							if (((Pos[c] - Pos[a]) ^ (Pos[b] - Pos[a])).Z >= 0.0) { Tri.Append({ a, b, c }); }
							else { Tri.Append({ a, c, b }); }
						}
					}
				}
			}
			if (Tri.Num() == 0 || bHeightFail)
			{
				UE_LOG(LogAsdaWorld, Warning, TEXT("[война] площадка экз.%d (%.0f, %.0f): сетка .TER не покрывает квадрат — экземпляр не заменён"),
					Built, T.GetLocation().X, T.GetLocation().Y);
				continue;
			}
			UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
			PM->SetMobility(EComponentMobility::Movable);
			PM->SetAbsolute(true, true, true);
			PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PM->SetCastShadow(false);
			PM->bUseAsyncCooking = false;
			PM->CreateMeshSection(0, Pos, Tri, Nrm, UV, Col, Tan, /*bCreateCollision*/ false);
			PM->SetMaterial(0, MC->GetMaterial(MatIndex));
			PM->RegisterComponent();
			PM->SetWorldTransform(FTransform::Identity);
			FPatch Rec; Rec.Src = MC; Rec.MatIndex = MatIndex; Rec.Mesh = PM;
			GPatches.Add(Rec);
			UE_LOG(LogAsdaWorld, Log, TEXT("[война] площадка экз.%d (%.0f, %.0f): по треугольникам .TER — ячеек %d×%d, вершин %d, треугольников %d, подъём %.1f"),
				Built, T.GetLocation().X, T.GetLocation().Y, Tx1 - Tx0 + 1, Tz1 - Tz0 + 1, Pos.Num(), Tri.Num() / 3, kLift);
			++Built;
		}
		// Исходный плоский квад прячем, только если заменены ВСЕ его экземпляры (ISM скрывается целиком). Его коллизия
		//   (невидимая плоскость, в низинах до 77 ед. над землёй) снимается вместе с ним — трассы видимости в неё не бьют.
		if (Built > 0 && Built == Xf.Num())
		{
			MC->SetVisibility(false);
			MC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else if (Built > 0)
		{
			// Не все экземпляры заменены: сетки убираем, остаётся исходный квад (лучше висящая эмблема, чем пропавшая).
			for (int32 k = GPatches.Num() - 1; k >= 0 && Built > 0; --k)
			{
				if (GPatches[k].Src.Get() == MC) { if (GPatches[k].Mesh.IsValid()) { GPatches[k].Mesh->DestroyComponent(); } GPatches.RemoveAt(k); --Built; }
			}
		}
		return Built;
	}

	// Сеткам этого компонента — тот же материал, что сейчас у слота (после перекраски по хозяину).
	static void SyncMaterial(const UMeshComponent* MC, UMaterialInterface* MI)
	{
		for (const FPatch& P : GPatches)
		{
			if (P.Src.Get() == MC && P.Mesh.IsValid()) { P.Mesh->SetMaterial(0, MI); }
		}
	}
}

static ASDA_DEV_CVAR(int32, CVarNoCityWarFlags,
	TEXT("asda.War.NoFlagTextures"),
	0,
	TEXT("Флаги и площадки войны за города: 0 = подменяются по фракции-хозяину (эталон); "
	     "1 = подмена выключена, меши остаются с родными UNICON/UNICON03."),
	ECVF_Default);

// Сетка .TER текущей карты для площадки города — тот же путь, что у спавна своего персонажа (реестр карт по CurrentMapId);
//   глобальная `AsdaGround::GetGrid()` ставится загрузчиком, но вход мимо него (самотест, переподключение) её не обновляет.
static const UAsdaWalkGrid* AsdaCityFloorGridFor(UGameInstance* GI)
{
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FString Dir = LM ? LM->GetMapDirName(LM->CurrentMapId) : FString();
	if (Dir.IsEmpty()) { return nullptr; }
	return LoadObject<UAsdaWalkGrid>(nullptr, *AsdaMapRegistry::WalkGridObjectPath(Dir), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

int32 UAsdaWorldSubsystem::GetCityWarSlotForCurrentMap() const
{
	const UAsdaLoadManagerSubsystem* LM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 MapId = LM ? LM->CurrentMapId : -1;
	if (MapId == 3) { return 0; }   // Альпия
	if (MapId == 0) { return 1; }   // Силарис
	if (MapId == 7) { return 2; }   // Пламения
	return -1;
}

void UAsdaWorldSubsystem::ApplyCityWarTextures()
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	const int32 Slot = GetCityWarSlotForCurrentMap();
	if (Slot < 0) { CityWarAppliedState = -2; bCityWarSlotsScanned = false; CityWarSlots.Reset(); return; }
	if (CVarNoCityWarFlags.GetValueOnGameThread() != 0) { return; }

	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	// Без пакета 6743 состояние = 3 «не занят» — ровно то, чем `CMap_LoadUniconWarTextures`
	//   инициализирует все три слота при старте карты.
	int32 State = Net ? Net->GetCityOwnerFaction(Slot) : 3;
	if (State < 0 || State > 3) { State = 3; }
	const TArray<uint8>* Design = Net ? Net->GetCityClanFlagDesign(Slot) : nullptr;

	// ★ 28.10.2026: 6743 — рассылка сервера в любой момент (`Chat_Game_OwnerInfoUpdate` → `Send_RoseWarOwnerInfo`),
	//   в т. ч. между сбросом 5058 и готовым новым миром. Тогда скан ложился на УМИРАЮЩИЙ мир, флаг «просканировано»
	//   защёлкивался, и RevealWorld новой карты уже не находил её флаги (оставались родные UNICON/UNICON03).
	//   Скан, снятый с другого мира или пустой (у военных карт слоты есть всегда), считаем недействительным.
	if (bCityWarSlotsScanned)
	{
		bool bStale = CityWarSlots.Num() == 0;
		for (const FAsdaCityWarSlot& Rec : CityWarSlots)
		{
			const UMeshComponent* MC = Rec.Mesh.Get();
			if (!MC || MC->GetWorld() != W) { bStale = true; break; }
		}
		if (bStale)
		{
			bCityWarSlotsScanned = false;
			CityWarAppliedState = -2;   // иначе ранний выход «состояние то же» не перекрасит новые слоты
		}
	}

	// ── Один раз на карту: найти слоты материалов по РОДНЫМ текстурам города ──
	//   Эталон опознаёт объект ровно так же — по имени текстуры (`strcmp(имя, "unicon.TEX")`).
	if (!bCityWarSlotsScanned)
	{
		CityWarSlots.Reset();
		AsdaCityFloor::Forget(W);   // сетки площадок прошлого мира
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!A) { continue; }
			TArray<UMeshComponent*> Meshes;
			A->GetComponents<UMeshComponent>(Meshes);
			for (UMeshComponent* MC : Meshes)
			{
				if (!MC) { continue; }
				const int32 N = MC->GetNumMaterials();
				for (int32 i = 0; i < N; ++i)
				{
					UMaterialInterface* MI = MC->GetMaterial(i);
					if (!MI) { continue; }
					UTexture* Cur = nullptr;
					if (!MI->GetTextureParameterValue(FName(AsdaMat::kParamTex), Cur) || !Cur) { continue; }   // ★ В1-C F10
					const FString CurName = Cur->GetName();
					uint8 Kind = 0xFF;
					if (CurName.Equals(TEXT("UNICON"), ESearchCase::IgnoreCase))           { Kind = 0; }
					else if (CurName.Equals(TEXT("UNICON03"), ESearchCase::IgnoreCase))    { Kind = 1; }
					else if (CurName.Equals(TEXT("UNICON_CLAN"), ESearchCase::IgnoreCase)) { Kind = 2; }
					// `UNICON02` эталон не перехватывает — не трогаем.
					if (Kind == 0xFF) { continue; }
					FAsdaCityWarSlot Rec;
					Rec.Mesh = MC;
					Rec.Index = i;
					Rec.Kind = Kind;
					CityWarSlots.Add(Rec);
					// ★ 09.11.2026 [ИСПРАВЛЕНИЕ]: площадка ложится по рельефу (разбор — у `CVarCityFloorConform`).
					if (Kind == 1 && CVarCityFloorConform.GetValueOnGameThread() != 0 && MC->IsVisible())
					{
						const int32 Made = AsdaCityFloor::Build(W, MC, i, AsdaCityFloorGridFor(GetGameInstance()));
						UE_LOG(LogAsdaWorld, Log, TEXT("[война] площадка %s: сеток по земле %d"), *MC->GetName(), Made);
					}
				}
			}
		}
		bCityWarSlotsScanned = true;
		UE_LOG(LogAsdaWorld, Log, TEXT("[война] на карте найдено слотов города: %d"), CityWarSlots.Num());
	}

	// Эмблема клана-победителя: печём общей точкой (эталон делает то же — `CMiniMap_BakeIconToRenderTarget`).
	UTexture* ClanTex = nullptr;
	if (Design)
	{
		const FAsdaClanFlagData Data = FAsdaClanFlagData::FromWire(Design->GetData());
		ClanTex = UAsdaClanFlag::GetOrBake(this, Data);
	}

	// Пересобираем, если сменилось состояние, город или наличие эмблемы.
	const bool bSame = (State == CityWarAppliedState) && (Slot == CityWarAppliedSlot)
		&& (bCityWarClanApplied == (ClanTex != nullptr));
	if (bSame) { return; }

	UTexture* FlagTex  = LoadObject<UTexture>(nullptr, kUniconFlagTex[State]);
	UTexture* FloorTex = LoadObject<UTexture>(nullptr, kUniconFloorTex[State]);
	int32 Flags = 0, Floors = 0, Clans = 0;
	for (const FAsdaCityWarSlot& Rec : CityWarSlots)
	{
		UMeshComponent* MC = Rec.Mesh.Get();
		if (!MC) { continue; }
		UTexture* Want = nullptr;
		if      (Rec.Kind == 0) { Want = FlagTex; }
		else if (Rec.Kind == 1) { Want = FloorTex; }
		else                    { Want = ClanTex; }   // нет эмблемы ⇒ слот не трогаем (родная текстура)
		if (!Want) { continue; }
		UMaterialInterface* MI = MC->GetMaterial(Rec.Index);
		UMaterialInstanceDynamic* MID = MC->CreateDynamicMaterialInstance(Rec.Index, MI);
		if (!MID) { continue; }
		MID->SetTextureParameterValue(FName(AsdaMat::kParamTex), Want);   // ★ В1-C F10
		if (Rec.Kind == 1)
		{
			AsdaCityFloor::SyncMaterial(MC, MID);   // сетки площадки по земле — тот же материал
			// У процедурной сетки нет данных стриминга текстур — без закрепления текстура площадки остаётся
			//   заглушкой движка (серая шахматка). Текстур площадок 4 и они малы: держим все мипы в памяти.
			//   Флаг читает сам стример (`ShouldMipLevelsBeForcedResident`); пересоздавать ресурс не нужно.
			if (UTexture2D* T2 = Cast<UTexture2D>(Want)) { T2->bForceMiplevelsToBeResident = true; }
		}
		if      (Rec.Kind == 0) { ++Flags; }
		else if (Rec.Kind == 1) { ++Floors; }
		else                    { ++Clans; }
	}
	CityWarAppliedState = State;
	CityWarAppliedSlot = Slot;
	bCityWarClanApplied = (ClanTex != nullptr);
	static const TCHAR* kFac[4] = { TEXT("Свет"), TEXT("Тьма"), TEXT("Хаос"), TEXT("не занят") };
	UE_LOG(LogAsdaWorld, Log,
		TEXT("[война] город слот %d, хозяин %d (%s), клан «%s»: флагов %d, площадок %d, эмблем %d%s"),
		Slot, State, kFac[State],
		Net ? *Net->CityOwner[Slot].ClanName : TEXT(""), Flags, Floors, Clans,
		Design ? TEXT("") : TEXT(" (дизайна флага нет — эмблема остаётся родной текстурой)"));
}
