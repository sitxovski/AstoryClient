// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «ПЛАВАНИЕ НАЗАД» (09.11.2026, пачка 20, баг 2: «плывёт назад спиной, тем же клипом, что вперёд»). [ИСПРАВЛЕНИЕ]
//
// Разбор и правка — у ключа `asda.Swim.FaceMoveDir` (AsdaPlayerCharacter_Move.cpp, `SwimFacesMoveDir`).
// Запуск: `asda.SelfTest.SwimBack <id карты> [delay=N] [quit]` — игра на уровне этой карты (нужна геометрия воды `.WAT`):
//   `asda_selftest_batch.py --lang ru|en --map /Game/Map/rein/rein "asda.SelfTest.SwimBack 1"`. Только офлайн.
// Путь игрока: клавиши — `InputKey` (S, W, A/D, ПКМ — через ввод контроллера и раскладку), камера к нужному курсу —
//   `SetOrbitAnglesForBench`; клик по земле — `RequestMoveTo` (общая точка клика и мини-карты; 4006 перехвачен, грант 4007 —
//   самотестом); автопуть — `AsdaPath::FindPathTiles` той же точкой клика на дальнюю воду.
// Курс модели — видимый `MeshRoot` (после доворота) против ФАКТИЧЕСКОГО хода (сдвиг позиции за окно замера).
// ЧТО ПРОВЕРЯЕТ (глубокая вода, найденная по сетке и `.WAT`):
//   B1  S в воде: плывёт, клип swim01 (anim_237), модель смотрит по ходу (±25°); ключ 0 — смотрит против хода (донор, баг).
//   B2  S + ПКМ + A (назад-влево по диагонали): по ходу, swim01.
//   B3  ПКМ + D (вбок вправо): по ходу, swim01.
//   B4  W (вперёд): по ходу, swim01 — регресс.
//   B5  клик за спину (вода позади): по ходу, swim01.
//   B6  автопуть: маршрут на дальнюю воду, в пути по ходу, swim01.
//   B7  вход в воду пятясь (S с суши спиной к воде): на суше — лицом против хода, клип back01 (эталон); в воде — разворот по ходу,
//       swim01. Выход из воды пятясь: в воде по ходу, на суше снова лицом против хода и back01.
//   B8  стоя в воде после хода: swimloop01 (anim_238) — регресс.
//   Кадры — Reports/batch20/swim_back/<ru|en>_map<id>_<раздел>.png.
// Строки — «САМОТЕСТ-ПЛЫТЬ-НАЗАД», итог — «… ИТОГ: ok=… fail=…».
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
#include "World/AsdaGroundTrace.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSwimBackTest, Log, All);

namespace AsdaSwimBackTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.SwimBack");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПЛЫТЬ-НАЗАД");
	static constexpr float kSpeed = 0.35f;          // скорость хода теста, ед./мс
	static constexpr float kYawTol = 25.f;          // допуск курса модели, градусы
	static constexpr float kHold = 1.6f;            // удержание клавиши до замера, с (доворот модели успевает)

	struct FState
	{
		int32 Ok = 0, Fail = 0;
		int32 MapId = 1;
		bool bQuit = false;
		uint32 CapGen = 0;
		TArray<TPair<FVector2D, FVector2D>> Sent4006;   // перехваченные 4006 (Cur, Dst)
		int32 Granted = 0;
		bool bGranting = false;
		bool bWasInGame = false;
		float Speed0 = 0.f;
		FString Lang, ShotDir;
		FIntPoint Land = FIntPoint(-1, -1);             // суша у воды
		FIntPoint Deep = FIntPoint(-1, -1);             // глубокая вода (центр «озера» для разделов B1–B6)
		FVector2D InDir = FVector2D::ZeroVector;        // направление суша → вода (game XZ, единичное)
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;
		TWeakObjectPtr<AAsdaPlayerController> PC;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSwimBackTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaSwimBackTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
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
	static void Shot(const FS& S, const TCHAR* Name)
	{
		const FString F = FPaths::Combine(S->ShotDir, FString::Printf(TEXT("%s_map%d_%s.png"), *S->Lang, S->MapId, Name));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaSwimBackTest, Display, TEXT("%s кадр %s"), kPrefix, *F);
	}
	static void Key(const FS& S, const FKey& K, bool bDown)
	{
		if (AAsdaPlayerController* PC = S->PC.Get())
		{
			static_cast<APlayerController*>(PC)->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
		}
	}
	static void AllKeysUp(const FS& S)
	{
		for (const FKey& K : { EKeys::W, EKeys::S, EKeys::A, EKeys::D, EKeys::RightMouseButton }) { Key(S, K, false); }
	}

	// Глубина воды в центре тайла (вода − земля); NaN — воды нет.
	static float Depth(UWorld* W, const FIntPoint& T)
	{
		const FVector2D Ue = AsdaCoord::GameToUe2D(TileGame(T));
		const float Wz = AsdaGround::WaterSurfaceZ(W, (float)Ue.X, (float)Ue.Y);
		if (FMath::IsNaN(Wz)) { return NAN; }
		return Wz - AsdaGround::GroundZ(W, (float)Ue.X, (float)Ue.Y, Wz + 2000.f);
	}
	static bool DeepOpen(UWorld* W, const UAsdaWalkGrid* G, const FIntPoint& T)
	{
		if (!G->IsTileWalkable(T.X, T.Y) || !G->IsTileSwimOpen(T.X, T.Y)) { return false; }
		const float D = Depth(W, T);
		return !FMath::IsNaN(D) && D > 160.f;
	}
	static bool LandOk(const UAsdaWalkGrid* G, const FIntPoint& T)
	{
		return G->IsTileWalkable(T.X, T.Y) && !G->IsTileSwimOpen(T.X, T.Y);
	}

	// Суша, от которой по прямой 4..7 тайлов к глубокой воде, а за ней ещё ≥ 10 тайлов глубокой воды по той же прямой и
	//   ±4 тайла поперёк (простор для разделов в воде). Сухая полоса за сушей ≥ 6 тайлов (выход из воды пятясь).
	static bool FindSpot(const FS& S, UWorld* W, const UAsdaWalkGrid* G)
	{
		int32 Tried = 0;
		for (int32 Z = 12; Z < G->Height - 12 && Tried < 3000; Z += 2)
		{
			for (int32 X = 12; X < G->Width - 12 && Tried < 3000; X += 2)
			{
				if (!LandOk(G, FIntPoint(X, Z))) { continue; }
				for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					if (!G->IsTileSwimOpen(X + D.X, Z + D.Y) && !G->IsTileSwimOpen(X + 2 * D.X, Z + 2 * D.Y)) { continue; }
					++Tried;
					bool bDry = true;
					for (int32 K = 1; K <= 6 && bDry; ++K) { bDry = LandOk(G, FIntPoint(X, Z) - D * K); }
					if (!bDry) { continue; }
					int32 FirstDeep = -1;
					for (int32 K = 1; K <= 7; ++K)
					{
						const FIntPoint P(X + D.X * K, Z + D.Y * K);
						if (!G->IsTileWalkable(P.X, P.Y)) { break; }
						if (DeepOpen(W, G, P)) { FirstDeep = K; break; }
					}
					if (FirstDeep < 0) { continue; }
					bool bRoom = true;
					const FIntPoint Side(D.Y, D.X);
					for (int32 K = FirstDeep; K <= FirstDeep + 12 && bRoom; ++K)
					{
						for (int32 L = -4; L <= 4 && bRoom; ++L)
						{
							bRoom = DeepOpen(W, G, FIntPoint(X, Z) + D * K + Side * L);
						}
					}
					if (!bRoom) { continue; }
					S->Land = FIntPoint(X, Z);
					S->Deep = FIntPoint(X, Z) + D * (FirstDeep + 6);
					S->InDir = FVector2D(D.X, D.Y);
					return true;
				}
			}
		}
		return false;
	}

	// Гарант 4007 на каждый новый 4006 (тот же делегат, что пакет), пока идёт раздел клика/автопути.
	static void GrantLoop(const FS& S)
	{
		if (!S->bGranting) { return; }
		UAsdaNetSubsystem* Net = S->Net.Get();
		if (Net && S->Granted < S->Sent4006.Num())
		{
			const TPair<FVector2D, FVector2D> X = S->Sent4006.Last();
			S->Granted = S->Sent4006.Num();
			Net->OnEntityMove.Broadcast((int32)Net->SelfUserNum, 1, X.Key, X.Value, Net->LastServerSpeed, false, 2, -1);
		}
		After(S, 0.05f, [S]() { GrantLoop(S); });
	}

	// Курс видимой модели против хода. Знак: +1 — по ходу, −1 — против.
	static float YawErr(const AAsdaPlayerCharacter* Me, const FVector2D& MoveDir, float Sign)
	{
		const float Exp = AsdaCoord::FaceYawFromGameDir(MoveDir * Sign) + Me->MeshYawOffset;
		return FMath::Abs(FRotator::NormalizeAxis(Me->MeshRoot->GetComponentRotation().Yaw - Exp));
	}
	static bool ClipIs(const AAsdaPlayerCharacter* Me, int32 Idx) { return Me->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), Idx)); }

	// Замер: позиция сейчас и через 0.35 с → направление хода; проверка курса/клипа/плавания.
	static void Measure(const FS& S, const FString& Tag, float Sign, bool bExpectSwim, int32 ClipIdx, const TCHAR* ShotName, TFunction<void()> Done)
	{
		AAsdaPlayerCharacter* Me = S->Me.Get();
		if (!Me) { Done(); return; }
		const FVector2D P0 = Me->GetGamePosXZ();
		After(S, 0.35f, [S, Tag, Sign, bExpectSwim, ClipIdx, ShotName, P0, Done]()
		{
			AAsdaPlayerCharacter* Me = S->Me.Get();
			if (!Me) { Done(); return; }
			const FVector2D D = Me->GetGamePosXZ() - P0;
			const bool bMoved = D.Size() > 20.f;
			const float Err = bMoved ? YawErr(Me, D.GetSafeNormal(), Sign) : 999.f;
			const FString Clip = Me->GetPlayingClipName();
			S->Expect(bMoved && Me->IsSwimming() == bExpectSwim && Err <= kYawTol && ClipIs(Me, ClipIdx),
				FString::Printf(TEXT("%s: сдвиг %.0f, плывёт=%d (ждём %d), курс модели %s хода с ошибкой %.1f°, клип «%s» (ждём anim_%d)"),
					*Tag, D.Size(), (int32)Me->IsSwimming(), (int32)bExpectSwim, Sign > 0.f ? TEXT("по") : TEXT("против"), Err, *Clip, ClipIdx));
			if (ShotName) { Shot(S, ShotName); }
			Done();
		});
	}

	// Камера с тайла T смотрит по CamGameDir (курс — относительно актора, как у прочих самотестов), сверху −50° — пловца не
	//   заслоняет берег в кадре.
	static void CamTo(const FS& S, const FIntPoint& T, const FVector2D& CamGameDir)
	{
		AAsdaPlayerController* PC = S->PC.Get(); AAsdaPlayerCharacter* Me = S->Me.Get();
		if (!PC || !Me) { return; }
		const FVector2D U = AsdaCoord::GameToUe2D(TileGame(T) + CamGameDir * 100.f) - AsdaCoord::GameToUe2D(TileGame(T));
		PC->SetOrbitAnglesForBench(FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(U.Y, U.X)) - (float)Me->GetActorRotation().Yaw), -50.f);
	}
	static void Reset(const FS& S, const FIntPoint& T, const FVector2D& CamGameDir)
	{
		AAsdaPlayerCharacter* Me = S->Me.Get(); AAsdaPlayerController* PC = S->PC.Get();
		if (!Me || !PC) { return; }
		AllKeysUp(S);
		PC->CancelRoute();
		Me->StopMove();
		if (S->Net.IsValid()) { S->Net->ClearMoveRequestInFlight(TEXT("самотест плавания назад: новый раздел")); }
		Me->NetSnapHard(TileGame(T));
		Me->FaceGameDir(CamGameDir);
		Me->SnapFacingToLogical();
		CamTo(S, T, CamGameDir);
	}

	// Раздел с клавишами: Keys зажаты kHold, затем замер, отпускание.
	static void KeyPhase(const FS& S, const FIntPoint& At, const FVector2D& CamDir, TArray<FKey> Keys, const FString& Tag, float Sign,
		bool bSwim, int32 Clip, const TCHAR* ShotName, TFunction<void()> Done)
	{
		Reset(S, At, CamDir);
		After(S, 0.3f, [S, Keys, Tag, Sign, bSwim, Clip, ShotName, Done]()
		{
			for (const FKey& K : Keys) { Key(S, K, true); }
			After(S, kHold, [S, Tag, Sign, bSwim, Clip, ShotName, Done]()
			{
				Measure(S, Tag, Sign, bSwim, Clip, ShotName, [S, Done]() { AllKeysUp(S); After(S, 0.3f, Done); });
			});
		});
	}

	static void Finish(const FS& S)
	{
		UWorld* W = S->W.Get();
		if (!W) { return; }
		S->bGranting = false;
		AllKeysUp(S);
		if (AAsdaPlayerCharacter* Me = S->Me.Get()) { Me->StopMove(); }
		if (UAsdaNetSubsystem* Net = S->Net.Get())
		{
			if (S->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, S->CapGen); S->CapGen = 0; }
			Net->bInGameConnected = S->bWasInGame;
			Net->LastServerSpeed = S->Speed0;
		}
		AsdaCVarSave::RestoreAll(kCmd);
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaSwimBackTest, kPrefix, S->Ok, S->Fail,
			*FString::Printf(TEXT(" (карта %d, язык %s, суша (%d,%d), вода (%d,%d))"), S->MapId, *S->Lang, S->Land.X, S->Land.Y, S->Deep.X, S->Deep.Y));
	}

	static void Run2(const FS& S);

	static void Begin(const FS& S)
	{
		UWorld* W = S->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(W);
		if (!Me || !Net || !Me->WalkGrid) { AsdaSelfTestKit::Refused(W, kCmd, S->bQuit, TEXT("нет своего персонажа / сети / сетки карты")); return; }
		S->Me = Me; S->PC = PC; S->Net = Net;
		S->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		S->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch20/swim_back")));
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
		AsdaCVarSave::SetForTest(TEXT("asda.Swim.Enable"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Swim.OpenWater"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Swim.FaceMoveDir"), 1);
		if (!Net->IsSelfUserNumKnown()) { Net->SetSelfUserNumForDebug(41); }
		S->bWasInGame = Net->bInGameConnected; S->Speed0 = Net->LastServerSpeed;
		Net->bInGameConnected = true;   // 4006 уходит в перехват, в сеть ничего
		Net->LastServerSpeed = kSpeed;
		if (!FindSpot(S, W, Me->WalkGrid))
		{
			S->Expect(false, FString::Printf(TEXT("B0 на карте %d не найдено место «суша → глубокая вода» с простором"), S->MapId));
			Finish(S); return;
		}
		S->Expect(true, FString::Printf(TEXT("B0 место: суша (%d,%d), глубокая вода (%d,%d), направление к воде (%.0f,%.0f), глубина %.0f"),
			S->Land.X, S->Land.Y, S->Deep.X, S->Deep.Y, S->InDir.X, S->InDir.Y, Depth(W, S->Deep)));
		const int32 kBack01 = Me->MotionClipIdx(EAsdaMotionSlot::Back01);   // back01 — слот 24 эталона (`MoveClip` пятясь на суше)
		UE_LOG(LogAsdaSwimBackTest, Display, TEXT("%s клипы набора: back01=anim_%d, run01=anim_%d, walk01=anim_%d"), kPrefix,
			kBack01, Me->MotionClipIdx(EAsdaMotionSlot::Run01), Me->MotionClipIdx(EAsdaMotionSlot::Walk01));
		// Клавиатурная схема эталона: каждый 4006 ждёт грант 4007 — гарант работает весь прогон (клавиши, клик, автопуть).
		S->Granted = S->Sent4006.Num(); S->bGranting = true; GrantLoop(S);
		const FVector2D Cam = S->InDir;   // камера «к воде»; S ведёт к камере
		// Прогрев: персонаж в глубокой воде, ждём признак «плывёт» (вода `.WAT` и высота встают не в первый кадр), не дольше 5 с.
		Reset(S, S->Deep, Cam);
		TSharedRef<int32> WarmTries = MakeShared<int32>(0);
		TSharedRef<TFunction<void()>> Warm = MakeShared<TFunction<void()>>();
		*Warm = [S, Cam, kBack01, WarmTries, Warm]()
		{
		AAsdaPlayerCharacter* MeW = S->Me.Get();
		if (MeW && !MeW->IsSwimming() && ++*WarmTries < 50) { After(S, 0.1f, *Warm); return; }
		S->Expect(MeW && MeW->IsSwimming(), FString::Printf(TEXT("B0 прогрев: в глубокой воде плывёт через %.1f с"), *WarmTries * 0.1f));
		// B1: S в воде (ключ 1) — по ходу, swim01.
		KeyPhase(S, S->Deep, Cam, { EKeys::S }, TEXT("B1 S в воде, ключ 1"), +1.f, true, AAsdaPlayerCharacter::kClipSwimMove, TEXT("b1_s_guard"), [S, Cam, kBack01]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.FaceMoveDir"), 0);
			KeyPhase(S, S->Deep, Cam, { EKeys::S }, TEXT("B1 S в воде, ключ 0 — как донор, против хода (воспроизведение)"), -1.f, true,
				AAsdaPlayerCharacter::kClipSwimMove, TEXT("b1_s_noguard"), [S, Cam, kBack01]()
			{
				AsdaCVarSave::SetForTest(TEXT("asda.Swim.FaceMoveDir"), 1);
				KeyPhase(S, S->Deep, Cam, { EKeys::RightMouseButton, EKeys::S, EKeys::A }, TEXT("B2 S+ПКМ+A (назад-влево)"), +1.f, true,
					AAsdaPlayerCharacter::kClipSwimMove, TEXT("b2_diag"), [S, Cam, kBack01]()
				{
					KeyPhase(S, S->Deep, Cam, { EKeys::RightMouseButton, EKeys::D }, TEXT("B3 ПКМ+D (вбок)"), +1.f, true,
						AAsdaPlayerCharacter::kClipSwimMove, TEXT("b3_strafe"), [S, Cam, kBack01]()
					{
						KeyPhase(S, S->Deep, Cam, { EKeys::W }, TEXT("B4 W (вперёд)"), +1.f, true,
							AAsdaPlayerCharacter::kClipSwimMove, TEXT("b4_w"), [S]()
						{
							Run2(S);
						});
					});
				});
			});
		});
		};
		After(S, 0.2f, *Warm);
	}

	// B5 клик за спину, B6 автопуть, B7 вход/выход пятясь, B8 стоя, регресс суши.
	static void Run2(const FS& S)
	{
		const int32 kBack01 = S->Me.IsValid() ? S->Me->MotionClipIdx(EAsdaMotionSlot::Back01) : -1;
		const FVector2D Cam = S->InDir;
		// B5: клик за спину — вода позади (к суше, но ещё в глубине).
		Reset(S, S->Deep, Cam);
		After(S, 0.3f, [S, Cam, kBack01]()
		{
			AAsdaPlayerController* PC = S->PC.Get();
			if (!PC) { Finish(S); return; }
			PC->RequestMoveTo(TileGame(S->Deep) - S->InDir * 500.f);   // вода позади, до суши ещё ≥ 1 тайла
			After(S, 0.4f, [S, Cam, kBack01]()
			{
				Measure(S, TEXT("B5 клик за спину в воде"), +1.f, true, AAsdaPlayerCharacter::kClipSwimMove, TEXT("b5_click_behind"), [S, Cam, kBack01]()
				{
					// B6: автопуть — та же точка клика на дальнюю воду (маршрут `FindPathTiles`).
					Reset(S, S->Deep, Cam);
					AAsdaPlayerController* PC2 = S->PC.Get();
					TArray<FIntPoint> Path; FAsdaPathStats St;
					const FIntPoint Far = S->Deep + FIntPoint(FMath::RoundToInt(S->InDir.X), FMath::RoundToInt(S->InDir.Y)) * 5
						+ FIntPoint(FMath::RoundToInt(S->InDir.Y), FMath::RoundToInt(S->InDir.X)) * 3;
					const bool bPath = S->Me.IsValid() && AsdaPath::FindPathTiles(S->Me->WalkGrid, S->Deep, Far, Path, &St);
					S->Expect(bPath && St.bReachedGoal, FString::Printf(TEXT("B6 автопуть по воде (%d,%d)→(%d,%d): узлов %d, достигнута=%d"),
						S->Deep.X, S->Deep.Y, Far.X, Far.Y, Path.Num(), (int32)St.bReachedGoal));
					if (PC2) { PC2->RequestMoveTo(TileGame(Far)); }
					After(S, 0.6f, [S, Cam, kBack01]()
					{
						Measure(S, TEXT("B6 автопуть в воде"), +1.f, true, AAsdaPlayerCharacter::kClipSwimMove, TEXT("b6_autopath"), [S, Cam, kBack01]()
						{
							// B8: стоя в воде — swimloop01.
							if (AAsdaPlayerController* P = S->PC.Get()) { P->CancelRoute(); }
							if (AAsdaPlayerCharacter* Me = S->Me.Get()) { Me->StopMove(); }
							After(S, 0.5f, [S, Cam, kBack01]()
							{
								AAsdaPlayerCharacter* Me = S->Me.Get();
								S->Expect(Me && Me->IsSwimming() && ClipIs(Me, AAsdaPlayerCharacter::kClipSwimIdle),
									FString::Printf(TEXT("B8 стоит в воде: плывёт=%d, клип «%s» (ждём anim_%d)"), Me ? (int32)Me->IsSwimming() : -1,
										Me ? *Me->GetPlayingClipName() : TEXT("-"), AAsdaPlayerCharacter::kClipSwimIdle));
								// B7: вход в воду пятясь. Стоим на суше спиной к воде: камера — от воды (S ведёт к камере = к воде).
								const FVector2D Away = -S->InDir;
								const FIntPoint Start = S->Land - FIntPoint(FMath::RoundToInt(S->InDir.X), FMath::RoundToInt(S->InDir.Y)) * 2;
								Reset(S, Start, -Away);
								// Камера смотрит ОТ воды: персонаж лицом от воды, S ведёт к воде.
								CamTo(S, Start, Away);
								After(S, 0.3f, [S, kBack01]()
								{
									Key(S, EKeys::S, true);
									After(S, 0.5f, [S, kBack01]()
									{
										Measure(S, TEXT("B7 пятясь на суше к воде: лицом против хода, back01 (эталон)"), -1.f, false, kBack01, TEXT("b7_land_back"), [S, kBack01]()
										{
											// Держим S, пока не поплывёт (≤ 6 с), затем замер в воде.
											TSharedRef<int32> Tries = MakeShared<int32>(0);
											TSharedRef<TFunction<void()>> Wait = MakeShared<TFunction<void()>>();
											*Wait = [S, Tries, Wait, kBack01]()
											{
												AAsdaPlayerCharacter* Me = S->Me.Get();
												if (Me && !Me->IsSwimming() && ++*Tries < 60) { After(S, 0.1f, *Wait); return; }
												After(S, 1.2f, [S, kBack01]()
												{
													Measure(S, TEXT("B7 вошёл в воду пятясь: разворот по ходу, swim01"), +1.f, true, AAsdaPlayerCharacter::kClipSwimMove, TEXT("b7_enter_water"), [S, kBack01]()
													{
														Key(S, EKeys::S, false);
														// Выход пятясь: в глубине лицом к воде (камера к воде) — S ведёт к суше.
														Reset(S, S->Deep, -S->InDir);
														CamTo(S, S->Deep, S->InDir);
														After(S, 0.3f, [S, kBack01]()
														{
															Key(S, EKeys::S, true);
															After(S, 1.2f, [S, kBack01]()
															{
																Measure(S, TEXT("B7 пятясь из воды: в воде по ходу, swim01"), +1.f, true, AAsdaPlayerCharacter::kClipSwimMove, nullptr, [S, kBack01]()
																{
																	TSharedRef<int32> Tries2 = MakeShared<int32>(0);
																	TSharedRef<TFunction<void()>> Wait2 = MakeShared<TFunction<void()>>();
																	*Wait2 = [S, Tries2, Wait2, kBack01]()
																	{
																		AAsdaPlayerCharacter* Me = S->Me.Get();
																		if (Me && Me->IsSwimming() && ++*Tries2 < 80) { After(S, 0.1f, *Wait2); return; }
																		After(S, 1.2f, [S, kBack01]()
																		{
																			Measure(S, TEXT("B7 вышел на сушу пятясь: снова лицом против хода, back01"), -1.f, false, kBack01, TEXT("b7_exit_land"), [S]()
																			{
																				Key(S, EKeys::S, false);
																				Finish(S);
																			});
																		});
																	};
																	(*Wait2)();
																});
															});
														});
													});
												});
											};
											(*Wait)();
										});
									});
								});
							});
						});
					});
				});
			});
		});
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		FS S = MakeShared<FState>();
		S->W = W; S->MapId = A.Int(0, 1); S->bQuit = A.bQuit;
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSwimBackSelfTest(
	TEXT("asda.SelfTest.SwimBack"),
	TEXT("Самотест плавания назад (пачка 20, баг 2): курс модели и клип в воде при S, S+ПКМ+A, ПКМ+D, W, клике за спину, автопути, "
	     "входе/выходе пятясь; ключ asda.Swim.FaceMoveDir 0 — воспроизведение. `<id карты>` `delay=N` `quit`; игра — на уровне "
	     "этой карты. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaSwimBackTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
