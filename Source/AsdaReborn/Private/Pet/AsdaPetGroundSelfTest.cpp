// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПОСАДКИ ПИТОМЦЕВ ПО ВЫСОТЕ `asda.Pet.SelfTestGround` (29.10.2026). [ПОРТ]
//   Дефект владельца: питомец 31 «SnowLion» провален под землю (`Reports/bugs_2026-10-29/pet31_snowlion_sunk.webp`).
//   Разбор, эталон и таблица — `Reports/pet31_snowlion/GROUND.md`; правка и ключ отката — `asda.Pet.HoverFromOwner`
//   (`World/AsdaPetActor.cpp`).
//
// ★ КАК. Каждая порода из `DT_PetModels` (все строки, 0–31) ставится настоящим `AAsdaPetActor` (тот же `InitPet` и `Tick`, что
//   в мире) за хозяином-болванкой. Болванка стоит на сетке высот карты (`AsdaGround::GroundZ`) в двух точках возле своего
//   персонажа: РОВНОЕ место (уклон < 2 %) и СКЛОН (уклон 25–60 %). Режимы: ключ `asda.Pet.HoverFromOwner` = 1 (как эталон) и
//   = 0 (прежний порт); клипы: покой (хозяин стоит) и бег (хозяин качается на ±2 см — порог «идёт» в `Tick` 1 см).
//   Замер — по СКИННУТЫМ вершинам LOD0 (`ComputeSkinnedPositions`, то, что видит игрок; приём `AsdaLoginGameMode_Soul.cpp`):
//     зазор вершины = её Z − высота сетки под ней; «низ» = наименьший зазор по модели. Берётся мин/макс по кадрам (анимация).
//     Нет CPU-копии вершин — откат на кости (строка помечена «кости»).
//   Первая фаза идёт дважды: первый проход — прогрев (первая загрузка мешей), его замер выбрасывается.
//   Оракулы: (1) режим 1 — Z актора = эталонная формула (наземный: земля под питомцем; парящий: Z хозяина + 20 + HoverHeight);
//   (2) режим 1, ровное место, поза покоя — ни одна порода не уходит в землю глубже kSinkCm (бег — только в таблице: лапы
//   в клипе бега у части пород сами уходят ниже точки модели); (3) контроль режима 0 — львы 17–19/31 в земле
//   (дефект воспроизводится старым путём). Таблица по породам — строки «САМОТЕСТ-ПОСАДКА порода …».
// Консоль: `asda.Pet.SelfTestGround [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, ключи, итог, выход
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaDataTableCache.h"                // AsdaDt::Cached — DT_PetModels
#include "World/AsdaPetActor.h"
#include "World/AsdaGroundTrace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"  // LOD0 вершины для ComputeSkinnedPositions
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetGroundTest, Log, All);

namespace AsdaPetGroundTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Pet.SelfTestGround");
	static const TCHAR* kPfx = TEXT("САМОТЕСТ-ПОСАДКА");
	constexpr float kSinkCm = 5.f;        // глубже — «в земле» (стопы в клипе ходьбы чуть пружинят)
	constexpr float kFloatCm = 5.f;       // выше — «над землёй»
	constexpr double kPhaseSec = 1.6;     // длина одной фазы (поза успевает пройти несколько кадров клипа)
	constexpr double kSettleSec = 0.25;   // первые кадры фазы не мерим: клип только назначен

	// Итог замера одной породы в одной фазе.
	struct FMeas
	{
		bool bAny = false;          // был хотя бы один замер
		bool bBones = false;        // CPU-копии вершин нет — мерили кости
		float MinClear = 0.f;       // наименьший «низ» (зазор самой низкой вершины до сетки) за фазу
		float MaxClear = 0.f;       // наибольший «низ» за фазу (размах анимации)
		float ActorRel = 0.f;       // Z актора − земля под актором (последний кадр)
		float ActorErr = 0.f;       // |Z актора − эталонная формула| (последний кадр)
	};

	// Фаза: точка (0 ровно, 1 склон) × режим ключа (1/0) × клип (0 покой, 1 бег).
	struct FPhase { int32 Spot; int32 Mode; int32 Clip; };
	static const FPhase kPhases[] = {
		{0, 1, 0}, {0, 1, 1}, {1, 1, 0}, {1, 1, 1},
		{0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {1, 0, 1},
	};
	constexpr int32 kNumPhases = UE_ARRAY_COUNT(kPhases);

	struct FRow { int32 Type; FString Model; bool bHover; float Hover; };

	struct FState
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		TUniquePtr<FAsdaCVarScope> Cv;
		FTSTicker::FDelegateHandle Tick;
		TArray<FRow> Rows;
		FVector Spot[2];             // точки хозяина (на сетке)
		float Slope[2] = {0.f, 0.f}; // уклон точки, доля
		int32 Phase = -1;
		bool bWarm = true;           // первая фаза — прогрев (первая загрузка мешей/буферов вершин), её замер выбрасывается
		double PhaseStart = 0.0;
		int32 Frame = 0;
		TWeakObjectPtr<AActor> Owner;
		TArray<TWeakObjectPtr<AAsdaPetActor>> Pets;
		TArray<FMeas> M[kNumPhases];

		void Expect(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPetGroundTest, Warning, TEXT("%s %s %s"), kPfx, b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		}
	};

	// Уклон сетки в точке: наибольший перепад на ±100 по X и Y, доля.
	static float SlopeAt(UWorld* W, float X, float Y)
	{
		const float Z = AsdaGround::GroundZ(W, X, Y);
		float G = 0.f;
		for (const FVector2D D : { FVector2D(100, 0), FVector2D(-100, 0), FVector2D(0, 100), FVector2D(0, -100) })
		{
			G = FMath::Max(G, FMath::Abs(AsdaGround::GroundZ(W, X + D.X, Y + D.Y, Z) - Z) / 100.f);
		}
		return G;
	}

	// Ближайшие к дому ровная точка и склон (сетка 100, радиус 4000). Вода над точкой — пропуск.
	static bool PickSpots(UWorld* W, const FVector& Home, FState& S)
	{
		bool bHave[2] = {false, false};
		float Best[2] = {FLT_MAX, FLT_MAX};
		for (int32 iy = -40; iy <= 40; ++iy)
		{
			for (int32 ix = -40; ix <= 40; ++ix)
			{
				const float X = Home.X + ix * 100.f, Y = Home.Y + iy * 100.f;
				const float Z = AsdaGround::GroundZ(W, X, Y, NAN);
				if (FMath::IsNaN(Z)) { continue; }
				const float Wz = AsdaGround::WaterSurfaceZ(W, X, Y);
				if (!FMath::IsNaN(Wz) && Wz > Z) { continue; }
				// питомец встанет на (+40, −30) от хозяина — уклон меряем и там
				const float G = FMath::Max(SlopeAt(W, X, Y), SlopeAt(W, X + 40.f, Y - 30.f));
				const float D = FVector2D::Distance(FVector2D(X, Y), FVector2D(Home.X, Home.Y));
				const int32 K = (G < 0.02f) ? 0 : (G >= 0.25f && G <= 0.6f) ? 1 : -1;
				if (K >= 0 && D < Best[K])
				{
					Best[K] = D; bHave[K] = true;
					S.Spot[K] = FVector(X, Y, Z); S.Slope[K] = G;
				}
			}
		}
		return bHave[0] && bHave[1];
	}

	// Эталонная высота актора питомца (см. `CVarPetHoverFromOwner`, AsdaStory_game.c:594813-594832).
	static float EtalonZ(UWorld* W, const AAsdaPetActor* P, const AActor* Own)
	{
		const FVector L = P->GetActorLocation();
		if (P->bHover) { return Own->GetActorLocation().Z + P->FollowOffset.Z + P->HoverHeight; }
		return AsdaGround::GroundZ(W, L.X, L.Y, L.Z);
	}

	// «Низ» модели: наименьший зазор вершины до сетки под ней. false — замер невозможен.
	static bool LowestClearance(UWorld* W, AAsdaPetActor* P, float& Out, bool& bBones)
	{
		USkeletalMeshComponent* S = P->Mesh;
		if (!S || !S->IsRegistered() || !S->GetSkinnedAsset()) { return false; }
		const FTransform L2W = S->GetComponentTransform();
		Out = FLT_MAX; bBones = false;
		FSkeletalMeshRenderData* RD = S->GetSkeletalMeshRenderData();
		const FSkinWeightVertexBuffer* SW = S->GetSkinWeightBuffer(0);
		if (RD && RD->LODRenderData.Num() > 0 && SW && SW->GetNeedsCPUAccess()
			&& RD->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.GetAllowCPUAccess()
			&& RD->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() > 0)
		{
			TArray<FMatrix44f> RefToLocal;
			S->GetCurrentRefToLocalMatrices(RefToLocal, 0);
			TArray<FVector3f> Pos;
			USkinnedMeshComponent::ComputeSkinnedPositions(S, Pos, RefToLocal,
				RD->LODRenderData[0], *SW);
			for (const FVector3f& V : Pos)
			{
				const FVector Wp = L2W.TransformPosition(FVector(V));
				Out = FMath::Min(Out, (float)(Wp.Z - AsdaGround::GroundZ(W, Wp.X, Wp.Y, Wp.Z)));
			}
			return Pos.Num() > 0;
		}
		bBones = true;
		for (const FTransform& T : S->GetComponentSpaceTransforms())
		{
			const FVector Wp = L2W.TransformPosition(T.GetLocation());
			Out = FMath::Min(Out, (float)(Wp.Z - AsdaGround::GroundZ(W, Wp.X, Wp.Y, Wp.Z)));
		}
		return Out < FLT_MAX;
	}

	static void ClearPhase(FState& S)
	{
		for (TWeakObjectPtr<AAsdaPetActor>& P : S.Pets) { if (P.IsValid()) { P->Destroy(); } }
		S.Pets.Reset();
		if (S.Owner.IsValid()) { S.Owner->Destroy(); }
		S.Owner = nullptr;
	}

	static void StartPhase(FState& S, int32 Ph)
	{
		UWorld* W = S.W.Get();
		ClearPhase(S);
		S.Phase = Ph; S.PhaseStart = FPlatformTime::Seconds(); S.Frame = 0;
		S.Cv->Set(TEXT("asda.Pet.HoverFromOwner"), kPhases[Ph].Mode);
		FActorSpawnParameters P;
		P.ObjectFlags |= RF_Transient;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* O = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
		USceneComponent* R = NewObject<USceneComponent>(O, TEXT("Root"));
		O->SetRootComponent(R);
		R->RegisterComponent();
		O->SetActorLocation(S.Spot[kPhases[Ph].Spot]);
		S.Owner = O;
		for (const FRow& Row : S.Rows)
		{
			AAsdaPetActor* A = W->SpawnActor<AAsdaPetActor>(AAsdaPetActor::StaticClass(), O->GetActorTransform(), P);
			if (!A) { S.Pets.Add(nullptr); continue; }
			A->SetActorEnableCollision(false);
			A->InitPet(Row.Type, FString::Printf(TEXT("t%d"), Row.Type), O);
			S.Pets.Add(A);
		}
		S.M[Ph].SetNum(S.Rows.Num());
	}

	// Один кадр фазы: качнуть хозяина (бег), замерить породы.
	static void Sample(FState& S)
	{
		UWorld* W = S.W.Get();
		const FPhase& F = kPhases[S.Phase];
		AActor* O = S.Owner.Get();
		if (!O) { return; }
		++S.Frame;
		if (F.Clip == 1)   // ±2 см по X каждый кадр: `Tick` питомца видит «хозяин идёт» → трек 2
		{
			O->SetActorLocation(S.Spot[F.Spot] + FVector((S.Frame & 1) ? 2.f : -2.f, 0.f, 0.f));
		}
		if (FPlatformTime::Seconds() - S.PhaseStart < kSettleSec || (S.Frame % 3) != 0) { return; }
		for (int32 i = 0; i < S.Pets.Num(); ++i)
		{
			AAsdaPetActor* P = S.Pets[i].Get();
			if (!P) { continue; }
			float C = 0.f; bool bB = false;
			if (!LowestClearance(W, P, C, bB)) { continue; }
			FMeas& Me = S.M[S.Phase][i];
			Me.MinClear = Me.bAny ? FMath::Min(Me.MinClear, C) : C;
			Me.MaxClear = Me.bAny ? FMath::Max(Me.MaxClear, C) : C;
			Me.bAny = true; Me.bBones |= bB;
			const FVector L = P->GetActorLocation();
			Me.ActorRel = L.Z - AsdaGround::GroundZ(W, L.X, L.Y, L.Z);
			Me.ActorErr = FMath::Abs(L.Z - EtalonZ(W, P, O));
		}
	}

	static const TCHAR* Verdict(const FMeas& M)
	{
		if (!M.bAny) { return TEXT("нет замера"); }
		if (M.MinClear < -kSinkCm) { return TEXT("В ЗЕМЛЕ"); }
		if (M.MinClear > kFloatCm) { return TEXT("над землёй"); }
		return TEXT("норма");
	}

	static int32 PhaseIdx(int32 Spot, int32 Mode, int32 Clip)
	{
		for (int32 i = 0; i < kNumPhases; ++i)
		{
			if (kPhases[i].Spot == Spot && kPhases[i].Mode == Mode && kPhases[i].Clip == Clip) { return i; }
		}
		return 0;
	}

	static void Judge(FState& S)
	{
		UE_LOG(LogAsdaPetGroundTest, Warning, TEXT("%s точки: ровно %s (уклон %.1f %%), склон %s (уклон %.1f %%)"), kPfx,
			*S.Spot[0].ToString(), S.Slope[0] * 100.f, *S.Spot[1].ToString(), S.Slope[1] * 100.f);
		UE_LOG(LogAsdaPetGroundTest, Warning, TEXT("%s «низ» = зазор самой низкой вершины до сетки, см, мин..макс за фазу; актор = Z актора − земля"), kPfx);
		int32 BadEtalon = 0, SunkFlat = 0, SunkSlope = 0, BonesOnly = 0;
		FString SunkList;
		for (int32 i = 0; i < S.Rows.Num(); ++i)
		{
			const FRow& R = S.Rows[i];
			for (int32 Mode : {1, 0})
			{
				FString Line = FString::Printf(TEXT("%s порода %2d %-20s %s H=%4.0f режим %d |"), kPfx, R.Type, *R.Model,
					R.bHover ? TEXT("парит") : TEXT("ходит"), R.Hover, Mode);
				for (int32 Spot : {0, 1})
				{
					const FMeas& Id = S.M[PhaseIdx(Spot, Mode, 0)][i];
					const FMeas& Rn = S.M[PhaseIdx(Spot, Mode, 1)][i];
					Line += FString::Printf(TEXT(" %s: актор %+6.1f, низ покой %+6.1f..%+6.1f → %s; бег %+6.1f..%+6.1f%s |"),
						Spot == 0 ? TEXT("ровно") : TEXT("склон"), Id.ActorRel, Id.MinClear, Id.MaxClear, Verdict(Id),
						Rn.MinClear, Rn.MaxClear, (Id.bBones || Rn.bBones) ? TEXT(" (кости)") : TEXT(""));
					if (Mode == 1)
					{
						if (Id.ActorErr > 0.5f || Rn.ActorErr > 0.5f || !Id.bAny || !Rn.bAny) { ++BadEtalon; }
						if (Id.bBones || Rn.bBones) { ++BonesOnly; }
						// посадку судим по ПОЗЕ ПОКОЯ: в клипе бега лапы у части пород сами уходят ниже точки модели
						//   (порода 11 — до −14 см) — это клип, а не высота актора; размах бега — в таблице
						const float Low = Id.MinClear;
						if (!Id.bAny || Low < -kSinkCm)
						{
							Spot == 0 ? ++SunkFlat : ++SunkSlope;
							if (Spot == 0) { SunkList += FString::Printf(TEXT(" %d(%.0f)"), R.Type, Low); }
						}
					}
				}
				UE_LOG(LogAsdaPetGroundTest, Warning, TEXT("%s"), *Line);
			}
		}
		S.Expect(S.Rows.Num() == 32, FString::Printf(TEXT("пород в DT_PetModels: %d (ждали 32: 0–31)"), S.Rows.Num()));
		S.Expect(BonesOnly == 0, FString::Printf(TEXT("замер по скиннутым вершинам у всех (по костям — %d фаз-пород)"), BonesOnly));
		S.Expect(BadEtalon == 0, FString::Printf(TEXT("режим 1: Z актора = эталонной формуле у всех пород и фаз (расхождений > 0.5 см: %d)"), BadEtalon));
		S.Expect(SunkFlat == 0, FString::Printf(TEXT("режим 1, ровное место, покой: в землю глубже %.0f см не уходит ни одна порода (ушли: %d%s)"),
			kSinkCm, SunkFlat, *SunkList));
		UE_LOG(LogAsdaPetGroundTest, Warning, TEXT("%s режим 1, склон, покой: в земле глубже %.0f см — %d пород (наземных прижимает к сетке под ТОЧКОЙ питомца, как эталон)"),
			kPfx, kSinkCm, SunkSlope);
		// контроль: старый путь воспроизводит дефект владельца у львов
		int32 LionsSunkOld = 0, Lions = 0;
		for (int32 i = 0; i < S.Rows.Num(); ++i)
		{
			if (!S.Rows[i].bHover || S.Rows[i].Hover >= 0.f) { continue; }
			++Lions;
			const float Low = S.M[PhaseIdx(0, 0, 0)][i].MinClear;
			if (Low < -kSinkCm) { ++LionsSunkOld; }
		}
		S.Expect(Lions > 0 && LionsSunkOld == Lions, FString::Printf(
			TEXT("контроль: режим 0 (прежний порт) — породы с HoverHeight < 0 в земле на ровном месте: %d из %d (дефект воспроизводится)"),
			LionsSunkOld, Lions));
	}

	static void Finish(TSharedRef<FState> S)
	{
		S->Tick.Reset();
		UWorld* W = S->W.Get();
		if (S->Phase == kNumPhases - 1 && W) { Judge(*S); }
		else { S->Expect(false, FString::Printf(TEXT("прогон оборван на фазе %d из %d"), S->Phase + 1, kNumPhases)); }
		ClearPhase(*S);
		if (S->Cv) { S->Cv->Restore(); }
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaPetGroundTest, kPfx, S->Ok, S->Fail, TEXT(""), true);
	}
}

struct FAsdaPetGroundSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPetGroundTestLocal;
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, A.Delay), false);
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			APlayerController* PC = Wd->GetFirstPlayerController();
			const APawn* Me = PC ? PC->GetPawn() : nullptr;
			if (!Me || !AsdaGround::GetGrid()) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сетки высот карты")); return; }
			TSharedRef<FState> S = MakeShared<FState>();
			S->W = Wd; S->bQuit = bQuit;
			S->Cv = MakeUnique<FAsdaCVarScope>(Wd, kCmd);
			if (const UDataTable* T = AsdaDt::Cached(AAsdaPetActor::kPetModelsPath))
			{
				for (const TPair<FName, uint8*>& It : T->GetRowMap())
				{
					const FAsdaPetModelRow* R = reinterpret_cast<const FAsdaPetModelRow*>(It.Value);
					if (R) { S->Rows.Add({R->PetTypeId, R->ModelName, R->bHover, R->HoverHeight}); }
				}
				S->Rows.Sort([](const FRow& a, const FRow& b) { return a.Type < b.Type; });
			}
			if (S->Rows.Num() == 0 || !PickSpots(Wd, Me->GetActorLocation(), *S))
			{
				AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет DT_PetModels или не нашлось ровного места/склона у персонажа"));
				return;
			}
			StartPhase(*S, 0);
			S->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
			{
				if (!S->W.IsValid()) { Finish(S); return false; }
				Sample(*S);
				if (FPlatformTime::Seconds() - S->PhaseStart >= kPhaseSec)
				{
					if (S->bWarm) { S->bWarm = false; S->M[0].Reset(); StartPhase(*S, 0); }
					else if (S->Phase + 1 < kNumPhases) { StartPhase(*S, S->Phase + 1); }
					else { Finish(S); return false; }
				}
				return true;
			}));
		}), A.Delay + 2.f, false);
	}
};

static FAutoConsoleCommandWithWorldAndArgs GAsdaPetGroundSelfTest(
	TEXT("asda.Pet.SelfTestGround"),
	TEXT("Самотест посадки питомцев по высоте (29.10.2026): все породы DT_PetModels за хозяином-болванкой на ровном месте и склоне, ")
	TEXT("покой и бег; низ модели по скиннутым вершинам до сетки высот; режимы asda.Pet.HoverFromOwner 1/0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { FAsdaPetGroundSelfTest::Run(A, W); }));

#endif // !UE_BUILD_SHIPPING
