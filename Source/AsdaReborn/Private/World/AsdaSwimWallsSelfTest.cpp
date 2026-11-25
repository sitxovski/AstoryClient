// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СТЕНЫ МАСКИ ПЛАВАНИЯ» (08.11.2026, пачка 18, баг 9: «игроки проходят сквозь запретные зоны — невидимые стены»,
//   Альпен x106 y384 и x54 y403). [ИСПРАВЛЕНИЕ]
//
// Причина: маска открытой воды (`DA_WalkGrid.SwimBits`, 28.10) открывала блок-тайлы `.TER`, до которых от суши оригинала можно
//   дойти только сквозь стену — с обрыва в провал/котловину (Альпен: провал под башней `tower_1` в центре города, Санни-Кост:
//   котловины под обрывами) и через кромку города (ровные блок-тайлы безопасной зоны на уровне мостовой — Альпен, Силалис).
//   Правило — `Tools/asda_water_swim.py::wall_tiles`, клиент — `DA_WalkGrid.SwimWallBits`, ключ `asda.Swim.WallGuard`.
//
// Запуск: `asda.SelfTest.SwimWalls <id карты> [delay=N] [quit]` — игра на уровне этой карты (кадры):
//   `asda_selftest_batch.py --lang ru|en --map /Game/Map/alpen/alpen "asda.SelfTest.SwimWalls 3"`. Только офлайн.
//   Пары «суша → стена»: на Альпене (3) — точки владельца (106,384) и (54,403), на прочих картах — первые две найденные
//   (тайл стены, по прямой от него проходимая суша `.TER`, между ними только тайлы маски). Пара «суша → открытая вода» — регресс.
// ЧТО ПРОВЕРЯЕТ (настоящие входы игрока):
//   S0  у карты есть `SwimWallBits`; цель пары: исходно блок `.TER`, в `SwimBits` (сервер её открыл), в стене.
//   S1  `IsTileWalkable`: ключ 1 — блок, ключ 0 — проходим (прежнее поведение, воспроизведение бага).
//   S2  автопуть `AsdaPath::FindPathTiles` (тот же, что клик, мини-карта и автопуть заданий): при 1 ни один узел не в стене и цель
//       не достигнута; при 0 — путь доходит до цели.
//   S3  клавиша W (`InputKey`, камера развёрнута к цели): при 1 персонаж упирается, ни одного кадра на тайле стены; при 0 — входит.
//   S4  клик по земле (`RequestMoveTo` — общая точка клика по земле и мини-карте; 4006 перехвачен, грант 4007 — самотестом): при 1
//       ни одного кадра в стене; при 0 — входит.
//   S5  верхом (транспорт 522): клавиша W и клик — при 1 ни одного кадра в стене.
//   S6  регресс воды: пара «суша → открытая вода вне стены» — клавишей W при 1 персонаж входит в воду маски.
//   Кадры — Reports/batch18/walls/<ru|en>_map<id>_<пара>_<раздел>.png.
// Строки — «САМОТЕСТ-СТЕНЫ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWalkGrid.h"
#include "World/AsdaPathfinder.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSwimWallsTest, Log, All);

namespace AsdaSwimWallsTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.SwimWalls");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-СТЕНЫ");
	static constexpr float kSpeed = 0.6f;            // скорость хода теста, ед./мс
	static constexpr int32 kMountItem = 522;         // транспорт для раздела S5

	// Пара «суша → цель».
	struct FPair
	{
		FIntPoint Land = FIntPoint(-1, -1);          // исходно проходимый тайл суши
		FIntPoint Target = FIntPoint(-1, -1);        // тайл стены (или открытой воды — пара регресса)
		bool bWall = true;                           // false — пара регресса (вода вне стены)
		FString Tag;                                 // имя пары в журнале и кадрах
	};

	// Состояние прогона.
	struct FState
	{
		int32 Ok = 0, Fail = 0;                      // счётчики проверок
		int32 MapId = 3;                             // карта прогона
		bool bQuit = false;                          // выйти после прогона
		uint32 CapGen = 0;                           // поколение перехвата отправок
		TArray<TPair<FVector2D, FVector2D>> Sent4006; // перехваченные 4006 (Cur, Dst)
		int32 Granted = 0;                           // сколько 4006 «одобрено» гарантом
		TArray<FPair> Pairs;                         // пары прогона
		int32 PairIdx = 0;                           // текущая пара
		int32 WallFrames = 0;                        // кадров на тайле стены за текущую фазу
		int32 MaskFrames = 0;                        // кадров на тайле маски вне стены за текущую фазу
		bool bSampling = false;                      // опрос позиции идёт
		bool bWasInGame = false;                     // исходное bInGameConnected
		float Speed0 = 0.f;                          // исходная скорость сервера
		FString Lang;                                // ru | en
		FString ShotDir;                             // папка кадров
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;
		TWeakObjectPtr<AAsdaPlayerController> PC;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSwimWallsTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaSwimWallsTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};
	using FS = TSharedRef<FState>;

	static void After(const FS& S, float Delay, TFunction<void()> Fn)
	{
		UWorld* W = S->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, Delay), false);
	}
	static FVector2D TileGame(const FIntPoint& T) { return AsdaCoord::TileCenterGame(T.X, T.Y); }
	static bool OrigWalk(const UAsdaWalkGrid* G, int32 X, int32 Z)
	{
		const int32 Idx = Z * G->Width + X;
		return G->Bits.IsValidIndex(Idx >> 3) && ((G->Bits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}
	static float WalkTime(const FPair& P) { return FVector2D::Distance(TileGame(P.Land), TileGame(P.Target)) / (kSpeed * 1000.f) + 1.2f; }
	static void SetKey(int32 V)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Swim.WallGuard"), V);
		AsdaPath::InvalidateCostGrid();   // вес клеток зависит от проходимости — пересчитать под новый ключ
	}
	static void Shot(const FS& S, const FString& Name)
	{
		const FString F = FPaths::Combine(S->ShotDir, FString::Printf(TEXT("%s_map%d_%s.png"), *S->Lang, S->MapId, *Name));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaSwimWallsTest, Display, TEXT("%s кадр %s"), kPrefix, *F);
	}

	// Суша по прямой от цели: идём от цели в 4 направлениях; между целью и сушей — только тайлы маски (для пары стены — хотя бы
	//   один тайл стены по дороге не обязателен: цель сама в стене). Для пары регресса путь не должен касаться стены.
	static bool FindLand(const UAsdaWalkGrid* G, const FIntPoint& T, bool bWallPair, FIntPoint& OutLand)
	{
		int32 Best = MAX_int32;
		for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			for (int32 K = 1; K <= 25; ++K)
			{
				const FIntPoint P = T + D * K;
				if (P.X < 2 || P.Y < 2 || P.X >= G->Width - 2 || P.Y >= G->Height - 2) { break; }
				if (OrigWalk(G, P.X, P.Y))
				{
					if (K >= 2 && K < Best) { Best = K; OutLand = P; }
					break;
				}
				if (!G->IsTileSwimOpen(P.X, P.Y)) { break; }
				if (!bWallPair && G->IsTileSwimWall(P.X, P.Y)) { break; }
			}
		}
		return Best != MAX_int32;
	}

	// Пары карты.
	static void BuildPairs(const FS& S, const UAsdaWalkGrid* G)
	{
		auto Add = [&](const FIntPoint& T, bool bWall, const FString& Tag)
		{
			FPair P; P.Target = T; P.bWall = bWall; P.Tag = Tag;
			if (FindLand(G, T, bWall, P.Land)) { S->Pairs.Add(P); return true; }
			S->Expect(false, FString::Printf(TEXT("S0 %s: у цели (%d,%d) не найдена суша по прямой"), *Tag, T.X, T.Y));
			return false;
		};
		if (S->MapId == 3)
		{
			Add(FIntPoint(106, 384), true, TEXT("x106y384"));
			Add(FIntPoint(54, 403), true, TEXT("x54y403"));
		}
		else
		{
			int32 Found = 0;
			for (int32 Z = 3; Z < G->Height - 3 && Found < 2; ++Z)
			{
				for (int32 X = 3; X < G->Width - 3 && Found < 2; ++X)
				{
					if (!G->IsTileSwimWall(X, Z)) { continue; }
					FPair P; P.Target = FIntPoint(X, Z); P.bWall = true;
					if (!FindLand(G, P.Target, true, P.Land)) { continue; }
					if (Found == 1 && FVector2D::Distance(FVector2D(P.Target), FVector2D(S->Pairs.Last().Target)) < 40.f) { continue; }
					P.Tag = FString::Printf(TEXT("x%dy%d"), X, Z);
					S->Pairs.Add(P); ++Found;
				}
			}
			S->Expect(Found > 0, FString::Printf(TEXT("S0 на карте %d найдено пар «суша → стена»: %d"), S->MapId, Found));
		}
		// Пара регресса: открытая вода вне стены, ближайшая к первой цели.
		const FIntPoint Ref = S->Pairs.Num() ? S->Pairs[0].Target : FIntPoint(G->Width / 2, G->Height / 2);
		float BestD = TNumericLimits<float>::Max(); FPair BestP;
		for (int32 Z = 3; Z < G->Height - 3; ++Z)
		{
			for (int32 X = 3; X < G->Width - 3; ++X)
			{
				if (!G->IsTileSwimOpen(X, Z) || G->IsTileSwimWall(X, Z)) { continue; }
				const float D = FVector2D::Distance(FVector2D(X, Z), FVector2D(Ref));
				if (D >= BestD) { continue; }
				FPair P; P.Target = FIntPoint(X, Z); P.bWall = false;
				if (FindLand(G, P.Target, false, P.Land)) { BestD = D; BestP = P; }
			}
		}
		if (BestD < TNumericLimits<float>::Max())
		{
			BestP.Tag = FString::Printf(TEXT("water_x%dy%d"), BestP.Target.X, BestP.Target.Y);
			S->Pairs.Add(BestP);
		}
		else
		{
			UE_LOG(LogAsdaSwimWallsTest, Display, TEXT("%s на карте %d открытой воды вне стены по прямой от суши нет — регресс S6 пропущен"), kPrefix, S->MapId);
		}
	}

	// Опрос позиции: кадры на тайле стены / на тайле маски вне стены.
	static void Sample(const FS& S)
	{
		if (!S->bSampling) { return; }
		AAsdaPlayerCharacter* Me = S->Me.Get();
		if (Me && Me->WalkGrid)
		{
			const FIntPoint T = AsdaCoord::GameToTile(Me->GetGamePosXZ());
			if (Me->WalkGrid->IsTileSwimWall(T.X, T.Y)) { ++S->WallFrames; }
			else if (Me->WalkGrid->IsTileSwimOpen(T.X, T.Y)) { ++S->MaskFrames; }
		}
		// Гарант 4007: каждый новый 4006 «одобряется» тем же делегатом, что пакет 4007 (сервер `.sim` эти тайлы пускает).
		UAsdaNetSubsystem* Net = S->Net.Get();
		if (Net && Me && S->Granted < S->Sent4006.Num())
		{
			const TPair<FVector2D, FVector2D> X = S->Sent4006.Last();
			S->Granted = S->Sent4006.Num();
			Net->OnEntityMove.Broadcast((int32)Net->SelfUserNum, 1, X.Key, X.Value, Net->LastServerSpeed, false, 2, -1);
		}
		After(S, 0.05f, [S]() { Sample(S); });
	}
	static void BeginSample(const FS& S) { S->WallFrames = 0; S->MaskFrames = 0; S->Granted = S->Sent4006.Num(); S->bSampling = true; Sample(S); }

	static void ResetPose(const FS& S, const FPair& P)
	{
		AAsdaPlayerCharacter* Me = S->Me.Get(); AAsdaPlayerController* PC = S->PC.Get();
		if (!Me || !PC) { return; }
		PC->CancelRoute();
		Me->StopMove();
		if (S->Net.IsValid()) { S->Net->ClearMoveRequestInFlight(TEXT("самотест стен: новая фаза")); }
		Me->NetSnapHard(TileGame(P.Land));
		// Камера к цели: клавиша W ведёт вперёд по взгляду камеры.
		const FVector2D U = AsdaCoord::GameToUe2D(TileGame(P.Target)) - AsdaCoord::GameToUe2D(TileGame(P.Land));
		PC->SetOrbitAnglesForBench(FMath::RadiansToDegrees(FMath::Atan2(U.Y, U.X)), PC->GetCameraPitchUE());
	}
	static void Key(const FS& S, bool bDown)
	{
		if (AAsdaPlayerController* PC = S->PC.Get())
		{
			static_cast<APlayerController*>(PC)->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
		}
	}

	// Фаза хода: способ (W / клик), ключ, верхом. Done — продолжение.
	static void Phase(const FS& S, const FPair& P, bool bClick, int32 KeyV, bool bMount, TFunction<void()> Done)
	{
		SetKey(KeyV);
		ResetPose(S, P);
		const FString What = FString::Printf(TEXT("%s %s%s, ключ %d"), *P.Tag, bClick ? TEXT("клик") : TEXT("клавиша W"), bMount ? TEXT(" верхом") : TEXT(""), KeyV);
		After(S, 0.2f, [S, P, bClick, KeyV, bMount, What, Done]()
		{
			AAsdaPlayerController* PC = S->PC.Get();
			if (!PC) { Done(); return; }
			BeginSample(S);
			if (bClick) { PC->RequestMoveTo(TileGame(P.Target)); }
			else { Key(S, true); }
			After(S, WalkTime(P), [S, P, bClick, KeyV, bMount, What, Done]()
			{
				if (!bClick) { Key(S, false); }
				S->bSampling = false;
				AAsdaPlayerCharacter* Me = S->Me.Get();
				const FIntPoint Pt = Me ? AsdaCoord::GameToTile(Me->GetGamePosXZ()) : FIntPoint(-1, -1);
				const FString Tail = FString::Printf(TEXT("кадров в стене %d, в маске вне стены %d, конец (%d,%d)"), S->WallFrames, S->MaskFrames, Pt.X, Pt.Y);
				if (P.bWall && KeyV == 1)
				{
					S->Expect(S->WallFrames == 0, FString::Printf(TEXT("%s %s: в стену не вошёл (%s)"), bMount ? TEXT("S5") : (bClick ? TEXT("S4") : TEXT("S3")), *What, *Tail));
					Shot(S, FString::Printf(TEXT("%s_%s%s_guard"), *P.Tag, bClick ? TEXT("click") : TEXT("key"), bMount ? TEXT("_mount") : TEXT("")));
				}
				else if (P.bWall)
				{
					S->Expect(S->WallFrames > 0, FString::Printf(TEXT("%s %s: без защиты вошёл в стену — воспроизведение бага (%s)"), bClick ? TEXT("S4") : TEXT("S3"), *What, *Tail));
					Shot(S, FString::Printf(TEXT("%s_%s_noguard"), *P.Tag, bClick ? TEXT("click") : TEXT("key")));
				}
				else
				{
					S->Expect(S->MaskFrames > 0 && S->WallFrames == 0, FString::Printf(TEXT("S6 %s: в открытую воду вошёл (%s)"), *What, *Tail));
					Shot(S, FString::Printf(TEXT("%s_key_water"), *P.Tag));
				}
				if (Me) { Me->StopMove(); }
				if (AAsdaPlayerController* PC = S->PC.Get()) { PC->CancelRoute(); }
				After(S, 0.3f, Done);
			});
		});
	}

	static void Finish(const FS& S)
	{
		UWorld* W = S->W.Get();
		if (!W) { return; }
		S->bSampling = false;
		if (AAsdaPlayerCharacter* Me = S->Me.Get()) { if (Me->IsMounted()) { Me->ClearMount(); } Me->StopMove(); }
		if (UAsdaNetSubsystem* Net = S->Net.Get())
		{
			if (S->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, S->CapGen); S->CapGen = 0; }
			Net->bInGameConnected = S->bWasInGame;
			Net->LastServerSpeed = S->Speed0;
		}
		AsdaCVarSave::RestoreAll(kCmd);
		AsdaPath::InvalidateCostGrid();
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaSwimWallsTest, kPrefix, S->Ok, S->Fail,
			*FString::Printf(TEXT(" (карта %d, язык %s, пар %d, 4006: %d)"), S->MapId, *S->Lang, S->Pairs.Num(), S->Sent4006.Num()));
	}

	static void RunPair(const FS& S);

	// Разделы одной пары по порядку.
	static void RunPairPhases(const FS& S, const FPair& P)
	{
		const UAsdaWalkGrid* G = S->Me.IsValid() ? S->Me->WalkGrid : nullptr;
		if (!G) { Finish(S); return; }
		auto Next = [S]() { ++S->PairIdx; RunPair(S); };
		if (!P.bWall)
		{
			// S6 регресс воды: клавиша W при ключе 1.
			S->Expect(!OrigWalk(G, P.Target.X, P.Target.Y) && G->IsTileWalkable(P.Target.X, P.Target.Y),
				FString::Printf(TEXT("S6 %s: вода (%d,%d) вне стены проходима при ключе 1; суша (%d,%d)"), *P.Tag, P.Target.X, P.Target.Y, P.Land.X, P.Land.Y));
			Phase(S, P, false, 1, false, Next);
			return;
		}
		// S0
		S->Expect(!OrigWalk(G, P.Target.X, P.Target.Y) && G->IsTileSwimOpen(P.Target.X, P.Target.Y) && G->IsTileSwimWall(P.Target.X, P.Target.Y),
			FString::Printf(TEXT("S0 %s: цель (%d,%d) исходно блок .TER, открыта маской (сервер), в стене; суша (%d,%d)"),
				*P.Tag, P.Target.X, P.Target.Y, P.Land.X, P.Land.Y));
		// S1
		SetKey(1);
		const bool bW1 = G->IsTileWalkable(P.Target.X, P.Target.Y);
		SetKey(0);
		const bool bW0 = G->IsTileWalkable(P.Target.X, P.Target.Y);
		S->Expect(!bW1 && bW0, FString::Printf(TEXT("S1 %s: проходимость при ключе 1 = %d (блок), при 0 = %d (как до правки)"), *P.Tag, (int32)bW1, (int32)bW0));
		// S2
		{
			TArray<FIntPoint> P0; FAsdaPathStats St0;
			AsdaPath::FindPathTiles(G, P.Land, P.Target, P0, &St0);
			SetKey(1);
			TArray<FIntPoint> P1; FAsdaPathStats St1;
			AsdaPath::FindPathTiles(G, P.Land, P.Target, P1, &St1);
			int32 InWall = 0;
			for (const FIntPoint& T : P1) { InWall += G->IsTileSwimWall(T.X, T.Y) ? 1 : 0; }
			S->Expect(InWall == 0 && !(St1.bReachedGoal && !St1.bGoalMoved),
				FString::Printf(TEXT("S2 %s автопуть при 1: узлов в стене %d, цель достигнута=%d, сдвинута=%d, узлов %d"),
					*P.Tag, InWall, (int32)St1.bReachedGoal, (int32)St1.bGoalMoved, P1.Num()));
			S->Expect(St0.bReachedGoal && !St0.bGoalMoved && P0.Num() > 0 && P0.Last() == P.Target,
				FString::Printf(TEXT("S2 %s автопуть при 0 доходит до цели (как до правки): достигнута=%d, узлов %d"), *P.Tag, (int32)St0.bReachedGoal, P0.Num()));
		}
		// S3 клавиша (1, затем контроль 0) → S4 клик (1, 0) → S5 верхом (клавиша, клик при 1).
		Phase(S, P, false, 1, false, [S, P, Next]()
		{
			Phase(S, P, false, 0, false, [S, P, Next]()
			{
				Phase(S, P, true, 1, false, [S, P, Next]()
				{
					Phase(S, P, true, 0, false, [S, P, Next]()
					{
						AAsdaPlayerCharacter* Me = S->Me.Get();
						UAsdaWorldSubsystem* WS = (S->W.IsValid() && S->W->GetGameInstance()) ? S->W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
						FString Model, Set; float H = 0.f;
						if (!Me || !WS || !WS->LookupMount(kMountItem, Model, Set, H))
						{
							S->Expect(false, FString::Printf(TEXT("S5 %s: транспорт %d не найден"), *P.Tag, kMountItem));
							Next(); return;
						}
						Me->SetMount(Model, Set, H, FString(), FString(), FString(), FString());
						S->Expect(Me->IsMounted(), FString::Printf(TEXT("S5 %s: верхом (%s)"), *P.Tag, *Model));
						Phase(S, P, false, 1, true, [S, P, Next]()
						{
							Phase(S, P, true, 1, true, [S, Next]()
							{
								if (AAsdaPlayerCharacter* M = S->Me.Get()) { M->ClearMount(); }
								Next();
							});
						});
					});
				});
			});
		});
	}

	static void RunPair(const FS& S)
	{
		if (S->PairIdx >= S->Pairs.Num()) { Finish(S); return; }
		const FPair P = S->Pairs[S->PairIdx];
		UE_LOG(LogAsdaSwimWallsTest, Display, TEXT("%s ── пара %s: суша (%d,%d) → цель (%d,%d)%s"), kPrefix, *P.Tag,
			P.Land.X, P.Land.Y, P.Target.X, P.Target.Y, P.bWall ? TEXT(" [стена]") : TEXT(" [вода, регресс]"));
		RunPairPhases(S, P);
	}

	static void Begin(const FS& S)
	{
		UWorld* W = S->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(W);
		if (!Me || !Net || !Me->WalkGrid) { AsdaSelfTestKit::Refused(W, kCmd, S->bQuit, TEXT("нет своего персонажа / сети / сетки карты")); return; }
		S->Me = Me; S->PC = PC; S->Net = Net;
		S->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		S->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch18/walls")));
		TWeakPtr<FState> WS = S;
		S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WS](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WS.Pin();
			if (St && Op == 4006 && P && Len >= 37)
			{
				St->Sent4006.Add(TPair<FVector2D, FVector2D>(FVector2D(AsdaBytes::RdF32(P + 16 + 5), AsdaBytes::RdF32(P + 16 + 9)),
					FVector2D(AsdaBytes::RdF32(P + 16 + 13), AsdaBytes::RdF32(P + 16 + 17))));
			}
		}, 0, kCmd);
		if (S->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, S->bQuit, TEXT("перехват отправок занят")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.Swim.OpenWater"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Walk.ClosedTiles"), 1);
		S->bWasInGame = Net->bInGameConnected; S->Speed0 = Net->LastServerSpeed;
		if (!Net->IsSelfUserNumKnown()) { Net->SetSelfUserNumForDebug(41); }   // свой номер офлайн (4003 не приходил) — гарант 4007
		Net->bInGameConnected = true;   // путь «в игре»: 4006 уходит в перехват, в сеть ничего
		Net->LastServerSpeed = kSpeed;
		const UAsdaWalkGrid* G = Me->WalkGrid;
		int32 NWall = 0, NOpen = 0;
		for (uint8 B : G->SwimWallBits) { NWall += FMath::CountBits(B); }
		for (uint8 B : G->SwimBits) { NOpen += FMath::CountBits(B); }
		S->Expect(NWall > 0, FString::Printf(TEXT("S0 карта %d: маска плавания %d тайлов, из них стена %d (SwimWallBits)"), S->MapId, NOpen, NWall));
		if (NWall == 0) { Finish(S); return; }
		BuildPairs(S, G);
		RunPair(S);
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		FS S = MakeShared<FState>();
		S->W = W; S->MapId = A.Int(0, 3); S->bQuit = A.bQuit;
		After(S, A.Delay, [S, Gate]()
		{
			UWorld* Wd = S->W.Get();
			if (!Wd || !Gate->Pass(Wd) || !GEngine) { return; }
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), S->MapId));
			GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
		});
		After(S, A.Delay + 3.f, [S, Gate]()
		{
			UWorld* Wd = S->W.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			Begin(S);
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSwimWallsSelfTest(
	TEXT("asda.SelfTest.SwimWalls"),
	TEXT("Самотест стен маски плавания (пачка 18, баг 9): тайлы DA_WalkGrid.SwimWallBits непроходимы при asda.Swim.WallGuard 1 для "
	     "автопути, клавиши W, клика по земле и верхом; при 0 — воспроизведение бага; регресс входа в открытую воду. "
	     "`<id карты>` `delay=N` `quit`; игра — на уровне этой карты. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaSwimWallsTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
