// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОТКРЫТОЙ ВОДЫ (плавание, задача владельца 28.10.2026). [ПОРТ]
//
// Карта: `asda.SelfTest.SwimOpenWater <id карты> [delay=N] [quit]` — игру запускать на уровне ЭТОЙ карты (меш воды нужен для
//   высоты поверхности): `asda_selftest_batch.py --map <путь уровня> "asda.SelfTest.SwimOpenWater 1"`. Только офлайн.
// ЧТО ПРОВЕРЯЕТ (настоящие входы):
//   W0  на карте есть открытая вода (`DA_WalkGrid.SwimBits`), найдена пара «суша → вода» с прямой видимостью по сетке:
//       глубокая (вода − земля > 150) и/или мелкая (10…90) точка.
//   W1  проходимость: тайл воды проходим при `asda.Swim.OpenWater 1`, при 0 — блок (эталон); исходный бит .TER — блок.
//   W2  автопуть `AsdaPath::FindPathTiles` суша → вода: при 1 цель достигнута, при 0 — нет (цель сдвинута на сушу).
//   W3  клик по воде (`RequestMoveTo` — тот же вход, что ЛКМ после проверки проходимости): уходит 4006 (перехват, в сеть
//       ничего), маршрут построен.
//   W4  ход своего персонажа (`NetMove`: шаг хода проверяет проходимость каждого тайла): при 0 упирается в берег и не
//       плывёт; при 1 доходит до глубокой воды — плывёт, клип swim01 (237) в ходе и swimloop01 (238) стоя, ноги на 90 ниже
//       поверхности.
//   W5  мелководье (вода ниже пояса): не плывёт, обычный клип, ноги на земле.
//   W6  возврат на сушу — не плывёт.
//   W7  смерть в глубокой воде: труп у поверхности (вода − 15), ключ `asda.Swim.DeadFloat 0` — как донор (вода − 90);
//       воскрешение — снова плывёт.
//   W8  сосед в той же воде плывёт (клип 238).
//   W9  sunnycoast (6): океан открыт полосой от берега, дальняя часть у края карты закрыта (решение владельца — 30 %).
//   W10 плывущему (решение владельца 28.10) — только ход и предметы: эмоция (4024 нет), сесть (4024 нет), прыжок, навык (окно
//       навыков), атака моба (автобой не взводится), рыбалка (109 не начат), лопата (5028 нет), транспорт из сумки (5450 нет)
//       отклонены; ключ `asda.Swim.ActionGate 0` — контрольная пара каждого пункта.
//   W11 высота пловца (`asda.Swim.HeightFix`): точка актора (ноги) = вода − 20 стоя и в ходе (донор `0x46CF5D`/`0x46E0A2`),
//       ключ 0 — прежние вода − 90; кадры `shots`.
//   W12 крылья (полёт премиум-зелья): плавание в приоритете (решение владельца) — с крыльями в ходе в глубокой воде плывёт
//       (swim01, не взмахи 35, высота пловца), остановился — swimloop01; на суше с крыльями — снова взмахи.
// Строки — «САМОТЕСТ-ВОДА», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWalkGrid.h"
#include "World/AsdaPathfinder.h"
#include "AsdaBytes.h"
#include "AsdaTestPackets.h"                 // W10: запись 4013 (моб для атаки)
#include "Npc/AsdaMobActor.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "World/AsdaFishingSubsystem.h"
#include "Misc/OutputDevice.h"
#include "Engine/DataTable.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/AsdaWornParts.h"   // FAsdaWornParts — соседка-женщина в воде   ★ R3: FAsdaWornParts — узкий заголовок вместо AsdaCharAssembly.h
#include "UnrealClient.h"                     // FScreenshotRequest — кадры `shots`
#include "Misc/Paths.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSwimWaterTest, Log, All);

namespace AsdaSwimOpenWaterTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.SwimOpenWater");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ВОДА");
	static constexpr int32 kPeerUserNum = 7781;     // сосед-заглушка
	static constexpr float kSpeed = 0.35f;          // скорость хода теста, ед./мс

	// Состояние прогона (живёт между таймерами).
	struct FState
	{
		int32 Ok = 0, Fail = 0;                         // счётчики проверок
		int32 MapId = -1;                               // карта прогона
		bool bShots = false;                            // `shots` — кадры в Reports/water_swim/shots/
		bool bMoveBonesDumped = false;                  // дамп костей в ходе (swim01) уже сделан
		float MoveLiftLast = -999.f;                    // точка актора над поверхностью в последнем кадре хода вплавь
		uint32 CapGen = 0;                              // поколение перехвата отправок
		int32 Sent4006 = 0;                             // перехваченные 4006
		TArray<int32> Sent4024;                         // коды действия перехваченных 4024
		int32 Sent5028 = 0;                             // перехваченные 5028 (предмет сумки)
		int32 Sent5450 = 0;                             // перехваченные 5450 (премиум-предмет / транспорт)
		FIntPoint Land = FIntPoint(-1, -1);             // тайл суши (исходно проходимый)
		FIntPoint Deep = FIntPoint(-1, -1);             // тайл глубокой воды (по прямой от суши)
		FIntPoint Shallow = FIntPoint(-1, -1);          // тайл мелководья (по прямой от суши)
		float DeepWaterZ = 0.f;                         // поверхность воды над Deep (UE Z)
		float ShallowGroundZ = 0.f;                     // земля в Shallow
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;        // свой персонаж
		TWeakObjectPtr<UAsdaNetSubsystem> Net;          // сеть

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSwimWaterTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaSwimWaterTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Исходный бит проходимости .TER (без открытой воды).
	static bool OrigWalk(const UAsdaWalkGrid* G, int32 X, int32 Z)
	{
		const int32 Idx = Z * G->Width + X;
		return G->Bits.IsValidIndex(Idx >> 3) && ((G->Bits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}

	// Центр тайла в game XZ.
	static FVector2D TileGame(const FIntPoint& T) { return AsdaCoord::TileCenterGame(T.X, T.Y); }

	// Глубина воды в центре тайла (вода − земля, UE Z); NaN — воды нет.
	static float DepthAt(UWorld* W, const FIntPoint& T, float* OutWaterZ = nullptr, float* OutGroundZ = nullptr)
	{
		const FVector2D Ue = AsdaCoord::GameToUe2D(TileGame(T));
		const float Wz = AsdaGround::WaterSurfaceZ(W, (float)Ue.X, (float)Ue.Y);
		if (FMath::IsNaN(Wz)) { return NAN; }
		const float Gz = AsdaGround::GroundZ(W, (float)Ue.X, (float)Ue.Y, Wz + 2000.f);
		if (OutWaterZ) { *OutWaterZ = Wz; }
		if (OutGroundZ) { *OutGroundZ = Gz; }
		return Wz - Gz;
	}

	// Пара «суша → вода»: тайл суши у кромки открытой воды и тайл воды нужной глубины по прямой (видимость по сетке с ключом 1).
	static bool FindPair(UWorld* W, const UAsdaWalkGrid* G, float MinDepth, float MaxDepth, FIntPoint& OutLand, FIntPoint& OutWater)
	{
		const int32 Wd = G->Width, Hd = G->Height;
		int32 Tried = 0;
		for (int32 Z = 2; Z < Hd - 2 && Tried < 400; Z += 3)
		{
			for (int32 X = 2; X < Wd - 2 && Tried < 400; X += 3)
			{
				if (!OrigWalk(G, X, Z)) { continue; }
				// Суша рядом с открытой водой.
				FIntPoint Entry(-1, -1);
				for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					if (G->IsTileSwimOpen(X + D.X, Z + D.Y)) { Entry = FIntPoint(D.X, D.Y); break; }
				}
				if (Entry.X == -1 && Entry.Y == -1) { continue; }
				++Tried;
				// По направлению от берега вглубь — до 25 тайлов.
				for (int32 K = 2; K <= 25; ++K)
				{
					const FIntPoint T(X + Entry.X * K, Z + Entry.Y * K);
					if (!G->IsTileSwimOpen(T.X, T.Y)) { break; }
					const float D = DepthAt(W, T);
					if (!FMath::IsNaN(D) && D > MinDepth && D < MaxDepth
						&& AsdaPath::IsLineWalkable(G, FVector2D(X + 0.5f, Z + 0.5f), FVector2D(T.X + 0.5f, T.Y + 0.5f)))
					{
						OutLand = FIntPoint(X, Z); OutWater = T;
						return true;
					}
				}
			}
		}
		return false;
	}

	// Запуск шага через Delay секунд от «сейчас».
	using AsdaSelfTestCtx::ByWorld::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Время хода между двумя тайлами при скорости теста (+ запас), с.
	static float WalkTime(const FIntPoint& A, const FIntPoint& B)
	{
		return FVector2D::Distance(TileGame(A), TileGame(B)) / (kSpeed * 1000.f) + 1.0f;
	}

	// Высоты костей тела относительно поверхности воды (журнал «САМОТЕСТ-ВОДА кости»): по ним ставится высота пловца.
	static void DumpBones(const AAsdaPlayerCharacter* C, float WaterZ, const TCHAR* Tag)
	{
		const USkeletalMeshComponent* K = C ? C->BaseSkel.Get() : nullptr;
		if (!K) { return; }
		TArray<TPair<float, FName>> Rows;
		for (int32 i = 0; i < K->GetNumBones(); ++i)
		{
			const FName N = K->GetBoneName(i);
			Rows.Add(TPair<float, FName>((float)K->GetBoneLocation(N).Z - WaterZ, N));
		}
		Rows.Sort([](const TPair<float, FName>& A, const TPair<float, FName>& B) { return A.Key > B.Key; });
		FString Line;
		for (const TPair<float, FName>& R : Rows) { Line += FString::Printf(TEXT(" %s=%.0f"), *R.Value.ToString(), R.Key); }
		UE_LOG(LogAsdaSwimWaterTest, Display, TEXT("%s кости %s (клип %s, ноги %.0f над водой):%s"), kPrefix, Tag,
			*C->GetPlayingClipName(), (float)C->GetActorLocation().Z - WaterZ, *Line);
	}

	// Средние высоты ключевых костей над точкой актора по клипу (журнал «САМОТЕСТ-ВОДА поза»): макушка 4_End, плечи 5/6,
	//   кисти 8/_8_a1 — по ним подобран подъём пловца (`AsdaFableMove::SwimLift`).
	struct FPoseAcc { double Head = 0, Shoulder = 0, Hand = 0, Pelvis = 0, Foot = 0; int32 N = 0; };
	static TMap<FString, FPoseAcc> GPose;
	static void SamplePose(const AAsdaPlayerCharacter* C, const TCHAR* Who)
	{
		const USkeletalMeshComponent* K = C ? C->BaseSkel.Get() : nullptr;
		if (!K || K->GetBoneIndex(TEXT("5")) == INDEX_NONE) { return; }
		const FString Clip = C->GetPlayingClipName();
		const FString Key = FString::Printf(TEXT("%s %s"), Who, *Clip.Right(8));
		const float A = (float)C->GetActorLocation().Z;
		const float Sc = FMath::Max(0.01f, (float)C->GetActorScale3D().Z);
		auto Z = [K, A, Sc](const TCHAR* B) { return ((float)K->GetBoneLocation(FName(B)).Z - A) / Sc; };
		FPoseAcc& P = GPose.FindOrAdd(Key);
		P.Head += Z(TEXT("4_End")); P.Shoulder += 0.5 * (Z(TEXT("5")) + Z(TEXT("6"))); P.Hand += 0.5 * (Z(TEXT("8")) + Z(TEXT("_8_a1")));
		P.Pelvis += Z(TEXT("1")); P.Foot += Z(TEXT("12_Nub")); ++P.N;
	}
	static void PrintPose()
	{
		for (const TPair<FString, FPoseAcc>& It : GPose)
		{
			const FPoseAcc& P = It.Value;
			if (P.N <= 0) { continue; }
			UE_LOG(LogAsdaSwimWaterTest, Display, TEXT("%s поза %s: кадров %d, над точкой актора — макушка %.1f, плечи %.1f, кисти %.1f, таз(1) %.1f, ступня(12_Nub) %.1f"),
				kPrefix, *It.Key, P.N, P.Head / P.N, P.Shoulder / P.N, P.Hand / P.N, P.Pelvis / P.N, P.Foot / P.N);
		}
		GPose.Reset();
	}

	// Кадр (с UI) в Reports/water_swim/shots/<карта>_<имя>.png — только при аргументе `shots`.
	static void Shot(const TSharedRef<FState>& S, const TCHAR* Name)
	{
		if (!S->bShots) { return; }
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/water_swim/shots")));
		const FString F = FPaths::Combine(Dir, FString::Printf(TEXT("map%d_%s.png"), S->MapId, Name));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaSwimWaterTest, Display, TEXT("%s кадр %s"), kPrefix, *F);
	}

	// Перехват строк журнала (отказы «персонаж плывёт» пишут только журнал — нотиса нет).
	struct FLogCatch : public FOutputDevice
	{
		TArray<FString> Lines;                          // строки с «плывёт»
		virtual bool CanBeUsedOnAnyThread() const override { return true; }   // строки любого потока (вместе со строкой ниже)
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			const FString L(V);
			if (L.Contains(TEXT("плывёт"))) { Lines.Add(L); }
		}
		bool Has(const TCHAR* Sub) const
		{
			GLog->FlushThreadedLogs();   // журнал UE пишет устройствам из своего потока — дождаться строк
			for (const FString& L : Lines) { if (L.Contains(Sub)) { return true; } }
			return false;
		}
	};

	static constexpr int32 kMobRid = 31001;         // W10: моб-заглушка для атаки (rid в 4013 — int16; ставится заранее)

	// W10, подготовка: моб-заглушка у глубокой воды (запись 4013; актор появляется не в том же кадре).
	static void SpawnGateMob(UWorld* W, const TSharedRef<FState>& S)
	{
		if (UAsdaNetSubsystem* Net = S->Net.Get())
		{
			Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(kMobRid, 1, S->Deep + FIntPoint(1, 0), 1, 250));
		}
	}

	// W10: каждый запрет при ключе 1 и контроль при ключе 0 (свой персонаж уже плывёт стоя).
	static void RunActionGates(UWorld* W, const TSharedRef<FState>& S, AAsdaPlayerCharacter* Me)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		UAsdaCharaInfoSubsystem* CI = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		UAsdaSkillUseSubsystem* SU = GI ? GI->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		UAsdaFishingSubsystem* Fi = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = S->Net.Get();
		if (!PC || !CI || !SU || !Fi || !Inv || !IT || !WS || !Net || !Me)
		{
			S->Expect(false, TEXT("W10 нет подсистем для проверки запретов"));
			return;
		}
		S->Expect(Me->IsSwimming(), TEXT("W10 до проверок свой персонаж плывёт"));
		FLogCatch Catch;
		GLog->AddOutputDevice(&Catch);
		const bool bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;   // отправители 4024/5028/5450 шлют только «в игре» — пакеты забирает перехват

		// Предметы для лопаты и транспорта — из таблиц (первые подходящие), ячейки сумки — свободные с конца.
		int32 ShovelId = 0, MountId = 0;
		for (const FAsdaItemRecord& Rec : IT->GetAllItems())
		{
			if (ShovelId == 0 && Rec.ItemType == 11 && Rec.ItemSubType == 33) { ShovelId = Rec.ItemCode; }
		}
		if (const UDataTable* SpT = IT->GetDataTable(TEXT("special_itemlist")))
		{
			SpT->ForeachRow<FAsdaSpecialItemlistRow>(TEXT("самотест воды"), [&MountId](const FName&, const FAsdaSpecialItemlistRow& Sp)
			{
				if (MountId == 0 && Sp.SubType == 15 && AsdaBytes::HexByteAt(Sp.DataHex, 101) == 2) { MountId = Sp.Code; }
			});
		}
		int32 CellA = -1, CellB = -1;
		for (int32 C = 47; C >= 0 && (CellA < 0 || CellB < 0); --C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, C);
			if (It && It->IsEmpty()) { if (CellA < 0) { CellA = C; } else { CellB = C; } }
		}
		auto Rnd = [&](int32 Key, const TCHAR* Tag)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.ActionGate"), Key);
			Catch.Lines.Reset();
			const bool bLock = Key != 0;
			const TCHAR* K = bLock ? TEXT("ключ 1") : TEXT("ключ 0");
			// Эмоция «радость» (группа 0, строка 0 — код действия 33).
			Me->SetAnimState(EAsdaPlayerState::Idle);
			// Транспорт из сумки. ★ 08.11.2026: ПЕРВЫМ, до «сесть» и лопаты: в контрольном раунде (ключ 0) после «сесть» персонаж в
			//   состоянии 7/8/9, а при нём оригинал транспорт не вызывает (`itemUseDispatch@0xA105A0`, AsdaStory_game.c:75384;
			//   порт — ключ `asda.Mount.BusyGate`). Проверка здесь — только запрет «плывёт», чужие гейты ей мешать не должны.
			if (MountId > 0 && CellB >= 0)
			{
				Inv->DebugPutItem(1, CellB, MountId, 1);
				const int32 N0 = S->Sent5450;
				Inv->UsePremiumItem(1, CellB);
				const bool bMountLog = Catch.Has(TEXT("(транспорт): персонаж плывёт"));
				S->Expect(bLock ? (bMountLog && S->Sent5450 == N0) : (!bMountLog && S->Sent5450 > N0),
					FString::Printf(TEXT("W10 %s транспорт %d: отказ «плывёт»=%d, 5450 %d"), K, MountId, bMountLog ? 1 : 0, S->Sent5450 - N0));
				Inv->DebugPutItem(1, CellB, 0, 0);
			}
			else { S->Expect(false, FString::Printf(TEXT("W10 транспорт не проверен (предмет %d, ячейка %d)"), MountId, CellB)); }
			const int32 E0 = S->Sent4024.Num();
			const bool bEmo = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 0));
			const bool bEmoSent = S->Sent4024.Num() > E0;
			Me->StopEmotionClip();
			Me->SetAnimState(EAsdaPlayerState::Idle);
			S->Expect(bLock ? (!bEmo && !bEmoSent) : bEmoSent, FString::Printf(TEXT("W10 %s эмоция: принята=%d, 4024=%d (%s)"), K, bEmo ? 1 : 0, bEmoSent ? 1 : 0, Tag));
			// Прыжок.
			const bool bJump = Me->StartJump(false);
			const FString JumpWhy = Me->GetLastJumpRefusal();
			S->Expect(bLock ? (!bJump && JumpWhy == TEXT("плывёт")) : bJump,
				FString::Printf(TEXT("W10 %s прыжок: начат=%d (%s)"), K, bJump ? 1 : 0, *JumpWhy));
			// Сесть (окно действий / клик / клавиши — общая ToggleSitStand).
			const int32 E1 = S->Sent4024.Num();
			PC->ToggleSitStand(TEXT("самотест воды"));
			const bool bSit = S->Sent4024.Num() > E1 && S->Sent4024.Last() == 107;
			Me->SetAnimState(EAsdaPlayerState::Idle);
			S->Expect(bLock ? !bSit : bSit, FString::Printf(TEXT("W10 %s сесть: 4024 107 ушёл=%d"), K, bSit ? 1 : 0));
			// Навык из окна навыков (код 501 — гейт плавания первый в общей точке CheckGates).
			SU->UseFromSkillWindow(501);
			const bool bSkillLog = Catch.Has(TEXT("[навыки] 501 не применён: персонаж плывёт"));
			S->Expect(bLock ? bSkillLog : !bSkillLog, FString::Printf(TEXT("W10 %s навык 501: отказ «плывёт»=%d"), K, bSkillLog ? 1 : 0));
			// Атака моба: второй клик по мобу (тот же вход, что ЛКМ) взводит автобой. Моб поставлен заранее (`kMobRid`).
			AAsdaMobActor* Mob = WS->FindNetMobByRid(kMobRid);
			if (Mob)
			{
				PC->bAutoAttack = false;
				PC->SetTarget(Mob);
				PC->EngageMobFromClick(Mob);
				const bool bAtk = PC->bAutoAttack;
				PC->bAutoAttack = false;
				PC->bCombatStarted = false;
				PC->SetTarget(nullptr);
				S->Expect(bLock ? !bAtk : bAtk, FString::Printf(TEXT("W10 %s атака моба: автобой взведён=%d"), K, bAtk ? 1 : 0));
			}
			else { S->Expect(false, TEXT("W10 моб-заглушка не появился")); }
			// Рыбалка (вход 109).
			Fi->RequestStart109();
			const bool bFishLog = Catch.Has(TEXT("109 не начат: персонаж плывёт"));
			Fi->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			S->Expect(bLock ? bFishLog : !bFishLog, FString::Printf(TEXT("W10 %s рыбалка: отказ «плывёт»=%d"), K, bFishLog ? 1 : 0));
			// Лопата.
			if (ShovelId > 0 && CellA >= 0)
			{
				Inv->DebugPutItem(1, CellA, ShovelId, 1);
				const int32 N0 = S->Sent5028;
				Inv->UseConsumable(1, CellA);
				const bool bShovelLog = Catch.Has(TEXT("лопата"));
				S->Expect(bLock ? (bShovelLog && S->Sent5028 == N0) : !bShovelLog,
					FString::Printf(TEXT("W10 %s лопата %d: отказ «плывёт»=%d, 5028 %d"), K, ShovelId, bShovelLog ? 1 : 0, S->Sent5028 - N0));
				Inv->DebugPutItem(1, CellA, 0, 0);
			}
			else { S->Expect(false, FString::Printf(TEXT("W10 лопата не проверена (предмет %d, ячейка %d)"), ShovelId, CellA)); }
		};
		Rnd(1, TEXT("плывёт — запрет"));
		Rnd(0, TEXT("контроль: без запрета"));
		AsdaCVarSave::SetForTest(TEXT("asda.Swim.ActionGate"), 1);
		Net->bInGameConnected = bWasInGame;
		Net->ClearMoveRequestInFlight(TEXT("самотест воды: W10"));
		GLog->RemoveOutputDevice(&Catch);
	}

	// W12: крылья (полёт) — плавание в приоритете: в ходе в глубокой воде плывёт, а не машет крыльями; стоя — swimloop01.
	static void RunWingsOverWater(UWorld* W, const TSharedRef<FState>& S, TFunction<void()> Finish)
	{
		AAsdaPlayerCharacter* Me = S->Me.Get();
		if (!Me) { Finish(); return; }
		TWeakObjectPtr<UWorld> WW(W);
		Me->SetAnimState(EAsdaPlayerState::Idle);
		Me->NetSnapHard(TileGame(S->Land));
		Me->SetWingsOn(true);
		// На суше с крыльями ход — взмахи (anim_35): проверка на месте (путь до воды короткий, суша — пара тайлов).
		Me->SetAnimState(EAsdaPlayerState::Move);
		const bool bFlapLand = !Me->IsSwimming() && Me->GetPlayingClipName().EndsWith(TEXT("anim_35"));
		const FString LandClip = Me->GetPlayingClipName();
		Me->SetAnimState(EAsdaPlayerState::Idle);
		S->Expect(bFlapLand, FString::Printf(TEXT("W12 с крыльями на суше ход — взмахи: клип «%s» (ждём anim_35)"), *LandClip));
		Me->NetMove(TileGame(S->Land), TileGame(S->Deep), kSpeed);
		const float T = WalkTime(S->Land, S->Deep);
		TSharedRef<int32> SwimSeen = MakeShared<int32>(0);
		TSharedRef<int32> FlapInDeep = MakeShared<int32>(0);
		TSharedRef<int32> FlapOnLand = MakeShared<int32>(0);
		for (float Tq = 0.2f; Tq < T; Tq += 0.1f)
		{
			After(W, Tq, [S, SwimSeen, FlapInDeep, FlapOnLand]()
			{
				AAsdaPlayerCharacter* M = S->Me.Get();
				if (!M || !M->IsMoving()) { return; }
				const FVector L = M->GetActorLocation();
				const float Wz = AsdaGround::WaterSurfaceZ(M->GetWorld(), (float)L.X, (float)L.Y);
				const float Gz = AsdaGround::GroundZ(M->GetWorld(), (float)L.X, (float)L.Y, (float)L.Z);
				const bool bDeepHere = !FMath::IsNaN(Wz) && Gz + 100.f < Wz;
				const bool bFlap = M->GetPlayingClipName().EndsWith(TEXT("anim_35"));
				if (bDeepHere && M->IsSwimming() && M->GetPlayingClipName().EndsWith(TEXT("anim_237"))) { ++*SwimSeen; }
				if (bDeepHere && bFlap && M->IsSwimming()) { ++*FlapInDeep; }
				if (!bDeepHere && !M->IsSwimming() && bFlap) { ++*FlapOnLand; }
			});
		}
		After(W, T + 0.5f, [S, Finish, SwimSeen, FlapInDeep, FlapOnLand]()
		{
			AAsdaPlayerCharacter* M = S->Me.Get();
			if (!M) { Finish(); return; }
			S->Expect(*SwimSeen > 0 && *FlapInDeep == 0,
				FString::Printf(TEXT("W12 с крыльями в глубокой воде плавание перебивает полёт: кадров swim01 %d, взмахов в воде %d"), *SwimSeen, *FlapInDeep));
			S->Expect(M->IsSwimming() && M->GetPlayingClipName().EndsWith(TEXT("anim_238")),
				FString::Printf(TEXT("W12 с крыльями остановился в глубокой воде — плывёт стоя: клип «%s»"), *M->GetPlayingClipName()));
			Shot(S, TEXT("wings_swim"));
			M->SetWingsOn(false);
			Finish();
		});
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		S->MapId = A.Int(0, 3);
		S->bShots = A.Has(TEXT("shots"));
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;

		// t = delay: офлайн-спавн своего павна на карте прогона штатными крюками.
		After(W, A.Delay, [WW, Gate, S]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd) || !GEngine) { return; }
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), S->MapId));
			GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
		});

		// Финал (общий для всех веток и отказов).
		auto Finish = [WW, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (S->Net.IsValid() && S->CapGen) { AsdaSelfTestKit::DisarmCapture(S->Net.Get(), S->CapGen); S->CapGen = 0; }
			AsdaCVarSave::RestoreAll(kCmd);
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaSwimWaterTest, kPrefix, S->Ok, S->Fail,
				*FString::Printf(TEXT(" (карта %d, 4006: %d)"), S->MapId, S->Sent4006));
		};

		After(W, A.Delay + 3.f, [WW, Gate, S, bQuit, Finish]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			if (!Me || !Net || !Me->WalkGrid) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сети / сетки карты")); return; }
			S->Me = Me; S->Net = Net;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 4006) { ++S->Sent4006; }
				if (Op == 4024 && P && Len >= 20) { S->Sent4024.Add((int32)AsdaBytes::RdU16(P + 18)); }
				if (Op == 5028) { ++S->Sent5028; }
				if (Op == 5450) { ++S->Sent5450; }
			}, 0, kCmd);
			if (S->CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.OpenWater"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.DeadFloat"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.ActionGate"), 1);
			const UAsdaWalkGrid* G = Me->WalkGrid;

			// ── W0: открытая вода и пары ──
			int32 NOpen = 0;
			for (uint8 B : G->SwimBits) { NOpen += FMath::CountBits(B); }
			S->Expect(NOpen > 0, FString::Printf(TEXT("W0 на карте %d открытой воды %d тайлов (SwimBits)"), S->MapId, NOpen));
			FIntPoint LandD(-1, -1), LandS(-1, -1);
			const bool bDeep = FindPair(Wd, G, 150.f, 3000.f, LandD, S->Deep);
			const bool bShallow = FindPair(Wd, G, 10.f, 90.f, LandS, S->Shallow);
			S->Land = bDeep ? LandD : LandS;
			S->Expect(bDeep || bShallow, FString::Printf(TEXT("W0 пара суша→вода: глубокая %s, мелкая %s"),
				bDeep ? *FString::Printf(TEXT("(%d,%d)→(%d,%d) глубина %.0f"), LandD.X, LandD.Y, S->Deep.X, S->Deep.Y, DepthAt(Wd, S->Deep, &S->DeepWaterZ)) : TEXT("нет"),
				bShallow ? *FString::Printf(TEXT("(%d,%d)→(%d,%d) глубина %.0f"), LandS.X, LandS.Y, S->Shallow.X, S->Shallow.Y, DepthAt(Wd, S->Shallow, nullptr, &S->ShallowGroundZ)) : TEXT("нет")));
			if (!bDeep && !bShallow) { Finish(); return; }
			if (bDeep) { DepthAt(Wd, S->Deep, &S->DeepWaterZ); }
			if (bShallow) { DepthAt(Wd, S->Shallow, nullptr, &S->ShallowGroundZ); }
			const FIntPoint Wt = bDeep ? S->Deep : S->Shallow;

			// ── W1: проходимость ──
			S->Expect(!OrigWalk(G, Wt.X, Wt.Y) && G->IsTileSwimOpen(Wt.X, Wt.Y) && G->IsTileWalkable(Wt.X, Wt.Y),
				FString::Printf(TEXT("W1 тайл воды (%d,%d): исходно блок, открыт, проходим при ключе 1"), Wt.X, Wt.Y));
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.OpenWater"), 0);
			S->Expect(!G->IsTileWalkable(Wt.X, Wt.Y), TEXT("W1 ключ asda.Swim.OpenWater 0 — вода снова блок (эталон)"));

			// ── W2: автопуть ──
			{
				TArray<FIntPoint> P0; FAsdaPathStats St0;
				AsdaPath::FindPathTiles(G, S->Land, Wt, P0, &St0);
				AsdaCVarSave::SetForTest(TEXT("asda.Swim.OpenWater"), 1);
				TArray<FIntPoint> P1; FAsdaPathStats St1;
				const bool b1 = AsdaPath::FindPathTiles(G, S->Land, Wt, P1, &St1);
				S->Expect(b1 && St1.bReachedGoal && !St1.bGoalMoved && P1.Num() > 0 && P1.Last() == Wt,
					FString::Printf(TEXT("W2 автопуть суша→вода при 1: достигнута=%d, узлов %d"), St1.bReachedGoal ? 1 : 0, P1.Num()));
				S->Expect(!St0.bReachedGoal || St0.bGoalMoved,
					FString::Printf(TEXT("W2 при 0 — цель недостижима (достигнута=%d, сдвинута=%d)"), St0.bReachedGoal ? 1 : 0, St0.bGoalMoved ? 1 : 0));
			}

			// ── W3: клик по воде ──
			Me->NetSnapHard(TileGame(S->Land));
			const int32 N0 = S->Sent4006;
			// 4006 отрезка уходит только «в игре» (`SendRouteLeg`: `bInGameConnected`) — на время клика взводим, как другие
			//   самотесты хода; пакет забирает перехват, в сеть ничего не уходит.
			const bool bWasInGame = Net->bInGameConnected;
			Net->bInGameConnected = true;
			PC->RequestMoveTo(TileGame(Wt));
			Net->bInGameConnected = bWasInGame;
			const bool bRoute = PC->HasRoute();
			// Отрезок маршрута уходит 4006 из тика маршрута (эталонная схема) — проверка через 0.8 с, затем W4.
			After(Wd, 0.8f, [WW, S, Finish, bDeep, bShallow, Wt, N0, bRoute]()
			{
			UWorld* WdB = WW.Get(); AAsdaPlayerCharacter* MeB = S->Me.Get();
			AAsdaPlayerController* PCB = WdB ? Cast<AAsdaPlayerController>(WdB->GetFirstPlayerController()) : nullptr;
			if (!WdB || !MeB || !PCB) { Finish(); return; }
			S->Expect(S->Sent4006 > N0 && bRoute, FString::Printf(TEXT("W3 клик по воде: 4006 ушёл (%d), маршрут построен=%d"),
				S->Sent4006 - N0, bRoute ? 1 : 0));
			PCB->CancelRoute();
			MeB->StopMove();
			if (S->Net.IsValid()) { S->Net->ClearMoveRequestInFlight(TEXT("самотест воды: клик проверен")); }

			// ── W4 (ключ 0): ход упирается в берег ──
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.OpenWater"), 0);
			MeB->NetSnapHard(TileGame(S->Land));
			MeB->NetMove(TileGame(S->Land), TileGame(Wt), kSpeed);
			After(WdB, WalkTime(S->Land, Wt), [WW, S, Finish, bDeep, bShallow, Wt]()
			{
				UWorld* W2 = WW.Get(); AAsdaPlayerCharacter* Me2 = S->Me.Get();
				if (!W2 || !Me2) { Finish(); return; }
				const FVector2D P = Me2->GetGamePosXZ();
				const FIntPoint Pt(FMath::FloorToInt(P.X / 100.f), FMath::FloorToInt(P.Y / 100.f));
				S->Expect(OrigWalk(Me2->WalkGrid, Pt.X, Pt.Y) && Pt != Wt,
					FString::Printf(TEXT("W4 ключ 0: ход встал на исходно проходимом тайле (%d,%d), в закрытую воду не вошёл"), Pt.X, Pt.Y));
				AsdaCVarSave::SetForTest(TEXT("asda.Swim.OpenWater"), 1);
				Me2->StopMove();
				Me2->NetSnapHard(TileGame(S->Land));
				Me2->NetMove(TileGame(S->Land), TileGame(Wt), kSpeed);
				// Середина пути у глубокой цели — проверка клипа хода в воде (последняя четверть пути — уже глубоко).
				const float T = WalkTime(S->Land, Wt);
				// Опрос хода раз в 0.1 с: был ли кадр «идёт + плывёт + клип swim01» (у глубокой цели).
				TSharedRef<int32> SwimMoveSeen = MakeShared<int32>(0);
				TSharedRef<FString> LastClip = MakeShared<FString>();
				for (float Tq = 0.2f; bDeep && Tq < T; Tq += 0.1f)
				{
					After(W2, Tq, [S, SwimMoveSeen, LastClip]()
					{
						AAsdaPlayerCharacter* M = S->Me.Get();
						if (!M || !M->IsMoving()) { return; }
						*LastClip = M->GetPlayingClipName();
						if (M->IsSwimming() && LastClip->EndsWith(TEXT("anim_237")))
						{
							S->MoveLiftLast = (float)M->GetActorLocation().Z - S->DeepWaterZ;
							SamplePose(M, TEXT("свой"));
							++*SwimMoveSeen;
							if (!S->bMoveBonesDumped) { S->bMoveBonesDumped = true; DumpBones(M, S->DeepWaterZ, TEXT("плывёт в ходе")); Shot(S, TEXT("swim_move")); }
						}
					});
				}
				if (bDeep)
				{
					After(W2, T, [S, SwimMoveSeen, LastClip]()
					{
						S->Expect(*SwimMoveSeen > 0, FString::Printf(TEXT("W4 плывёт в ходе: кадров «идёт+плывёт+anim_237» %d (последний клип в ходе «%s»)"),
							*SwimMoveSeen, **LastClip));
						// Высота в ходе вплавь: цель +5 (плавный переход от −10 за ~0.13 с, в последнем кадре хода — уже у цели).
						S->Expect(FMath::IsNearlyEqual(S->MoveLiftLast, -20.f, 3.f),
							FString::Printf(TEXT("W11 в ходе вплавь точка актора над поверхностью %.1f (донор: −20)"), S->MoveLiftLast));
					});
				}
				After(W2, T + 0.5f, [WW, S, Finish, bDeep, bShallow, Wt]()
				{
					UWorld* W3 = WW.Get(); AAsdaPlayerCharacter* Me3 = S->Me.Get();
					if (!W3 || !Me3) { Finish(); return; }
					const FVector2D P3 = Me3->GetGamePosXZ();
					const float Dist = FVector2D::Distance(P3, TileGame(Wt));
					if (bDeep)
					{
						const float Z = Me3->GetActorLocation().Z;
						S->Expect(Dist < 60.f && Me3->IsSwimming() && Me3->GetPlayingClipName().EndsWith(TEXT("anim_238"))
							&& FMath::IsNearlyEqual(Z, S->DeepWaterZ - 20.f, 3.f),
							FString::Printf(TEXT("W4 ключ 1: дошёл до глубокой воды (до цели %.0f), плывёт=%d, клип «%s» (ждём anim_238), точка актора %.0f = вода %.0f − 20 (донор; asda.Swim.HeightFix)"),
								Dist, Me3->IsSwimming() ? 1 : 0, *Me3->GetPlayingClipName(), Z, S->DeepWaterZ));
						DumpBones(Me3, S->DeepWaterZ, TEXT("стоя в воде"));
						for (float Tq = 0.f; Tq < 1.5f; Tq += 0.1f) { After(W3, Tq, [S]() { SamplePose(S->Me.Get(), TEXT("свой")); }); }
						if (S->bShots)
						{
							Me3->CameraArm->TargetArmLength = 320.f;
							if (AAsdaPlayerController* PCs = Cast<AAsdaPlayerController>(W3->GetFirstPlayerController())) { PCs->SetOrbitAnglesForBench(PCs->GetCameraYawUE(), -12.f); }
						}
						After(W3, 0.4f, [S]() { Shot(S, TEXT("swim_idle")); });
						// W11 контроль ключа: `asda.Swim.HeightFix 0` — прежняя постановка (вода − 90), 1 — снова вода − 20.
						After(W3, 0.45f, [S]() { AsdaCVarSave::SetForTest(TEXT("asda.Swim.HeightFix"), 0); });
						After(W3, 0.8f, [S]()
						{
							if (AAsdaPlayerCharacter* M = S->Me.Get())
							{
								S->Expect(FMath::IsNearlyEqual((float)M->GetActorLocation().Z, S->DeepWaterZ - 90.f, 3.f),
									FString::Printf(TEXT("W11 ключ asda.Swim.HeightFix 0 — как прежде: %.0f = вода − 90"), (float)M->GetActorLocation().Z));
							}
							Shot(S, TEXT("swim_idle_old"));
							AsdaCVarSave::SetForTest(TEXT("asda.Swim.HeightFix"), 1);
						});
					}
					else
					{
						S->Expect(Dist < 60.f && !Me3->IsSwimming() && !Me3->GetPlayingClipName().EndsWith(TEXT("anim_238")),
							FString::Printf(TEXT("W5 ключ 1: дошёл по мелководью (до цели %.0f), не плывёт, клип «%s»"), Dist, *Me3->GetPlayingClipName()));
					}
					// ── W5: мелководье ──
					if (bDeep && bShallow)
					{
						// После замеров позы и кадров в глубокой воде (1.5 с) — на мелководье.
						After(W3, 1.6f, [S]() { if (AAsdaPlayerCharacter* M = S->Me.Get()) { M->NetSnapHard(TileGame(S->Shallow)); } });
						After(W3, 2.2f, [S]()
						{
							AAsdaPlayerCharacter* M = S->Me.Get();
							if (!M) { return; }
							const float Z = M->GetActorLocation().Z;
							S->Expect(!M->IsSwimming() && !M->GetPlayingClipName().EndsWith(TEXT("anim_238")) && FMath::IsNearlyEqual(Z, S->ShallowGroundZ, 3.f),
								FString::Printf(TEXT("W5 мелководье (вода ниже пояса): плывёт=%d, клип «%s», ноги %.0f = земля %.0f"),
									M->IsSwimming() ? 1 : 0, *M->GetPlayingClipName(), Z, S->ShallowGroundZ));
						});
					}
					After(W3, 2.8f, [WW, S, Finish, bDeep]()
					{
						UWorld* W4 = WW.Get(); AAsdaPlayerCharacter* Me4 = S->Me.Get();
						if (!W4 || !Me4) { Finish(); return; }
						// ── W6: на сушу ──
						Me4->NetSnapHard(TileGame(S->Land));
						After(W4, 0.6f, [WW, S, Finish, bDeep]()
						{
							UWorld* W5 = WW.Get(); AAsdaPlayerCharacter* Me5 = S->Me.Get();
							if (!W5 || !Me5) { Finish(); return; }
							S->Expect(!Me5->IsSwimming(), TEXT("W6 вернулся на сушу — не плывёт"));
							if (!bDeep) { Finish(); return; }
							// ── W7: смерть в глубокой воде ──
							Me5->NetSnapHard(TileGame(S->Deep));
							Me5->SetAnimState(EAsdaPlayerState::Dead);
							After(W5, 0.6f, [WW, S, Finish]()
							{
								UWorld* W6 = WW.Get(); AAsdaPlayerCharacter* Me6 = S->Me.Get();
								if (!W6 || !Me6) { Finish(); return; }
								const float Z1 = Me6->GetActorLocation().Z;
								S->Expect(FMath::IsNearlyEqual(Z1, S->DeepWaterZ - 15.f, 3.f),
									FString::Printf(TEXT("W7 труп в воде у поверхности: %.0f = вода %.0f − 15"), Z1, S->DeepWaterZ));
								AsdaCVarSave::SetForTest(TEXT("asda.Swim.DeadFloat"), 0);
								After(W6, 0.6f, [WW, S, Finish]()
								{
									UWorld* W7 = WW.Get(); AAsdaPlayerCharacter* Me7 = S->Me.Get();
									if (!W7 || !Me7) { Finish(); return; }
									const float Z2 = Me7->GetActorLocation().Z;
									S->Expect(FMath::IsNearlyEqual(Z2, S->DeepWaterZ - 90.f, 3.f),
										FString::Printf(TEXT("W7 ключ asda.Swim.DeadFloat 0 — как донор: %.0f = вода − 90"), Z2));
									AsdaCVarSave::SetForTest(TEXT("asda.Swim.DeadFloat"), 1);
									Me7->SetAnimState(EAsdaPlayerState::Idle);
									// ── W8: сосед в воде ──
									UAsdaWorldSubsystem* WS = W7->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
									if (WS) { WS->DebugSpawnFakePeer(kPeerUserNum, TEXT("Пловец"), TileGame(S->Deep) + FVector2D(40.f, 0.f)); }
									After(W7, 0.8f, [WW, S, Finish, WS]()
									{
										UWorld* W8 = WW.Get(); AAsdaPlayerCharacter* Me8 = S->Me.Get();
										if (!W8 || !Me8) { Finish(); return; }
										S->Expect(Me8->IsSwimming() && Me8->GetPlayingClipName().EndsWith(TEXT("anim_238")),
											FString::Printf(TEXT("W7 воскрешение в воде — снова плывёт, клип «%s»"), *Me8->GetPlayingClipName()));
										AAsdaPlayerCharacter* P = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum)) : nullptr;
										S->Expect(P && P->IsSwimming() && P->GetPlayingClipName().EndsWith(TEXT("anim_238")),
											FString::Printf(TEXT("W8 сосед в воде плывёт: %s"), P ? *P->GetPlayingClipName() : TEXT("соседа нет")));
										if (P)
										{
											DumpBones(P, S->DeepWaterZ, TEXT("сосед (мужчина) в воде"));
										}
										if (WS)
										{
											WS->DebugSpawnFakePeer(kPeerUserNum + 1, TEXT("Пловчиха"), TileGame(S->Land));
											if (AAsdaPlayerCharacter* Pf0 = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum + 1)))
											{
												Pf0->InitAppearance(2, FAsdaWornParts());   // соседка: женская модель (пол записи 2) — ещё на суше
												Pf0->SetAnimState(EAsdaPlayerState::Idle);
												// Точка соседки — рядом с глубокой целью, тоже глубокая (у края глубины проверяем 4 стороны).
											FVector2D Off(-40.f, 0.f);
											for (const FVector2D Cand : { FVector2D(-40.f, 0.f), FVector2D(0.f, 40.f), FVector2D(0.f, -40.f), FVector2D(40.f, 40.f) })
											{
												const FVector2D Ue = AsdaCoord::GameToUe2D(TileGame(S->Deep) + Cand);
												const float Wz = AsdaGround::WaterSurfaceZ(W8, (float)Ue.X, (float)Ue.Y);
												const float Gz = AsdaGround::GroundZ(W8, (float)Ue.X, (float)Ue.Y, Wz + 2000.f);
												if (!FMath::IsNaN(Wz) && Wz - Gz > 120.f) { Off = Cand; break; }
											}
											Pf0->NetSnapHard(TileGame(S->Deep) + Off);
											}
										}
										for (float Tq = 0.2f; Tq < 0.9f; Tq += 0.1f)
										{
											After(W8, Tq, [S, WS]()
											{
												if (!WS) { return; }
												SamplePose(Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum)), TEXT("сосед"));
												SamplePose(Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum + 1)), TEXT("соседка"));
											});
										}
										After(W8, 0.95f, [S]() { Shot(S, TEXT("swim_peers")); });   // после появления соседки (фейд спавна) и оценки воды
										// Поза хода в воде (swim01): соседям клип хода на месте (`SetAnimState(Move)` при плавании играет 237).
										After(W8, 1.0f, [WS]()
										{
											for (int32 U : { kPeerUserNum, kPeerUserNum + 1 })
											{
												if (AAsdaPlayerCharacter* Pm = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(U)) : nullptr) { Pm->SetAnimState(EAsdaPlayerState::Move); }
											}
										});
										for (float Tq = 1.1f; Tq < 2.4f; Tq += 0.05f)
										{
											After(W8, Tq, [WS]()
											{
												if (!WS) { return; }
												SamplePose(Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum)), TEXT("сосед"));
												SamplePose(Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum + 1)), TEXT("соседка"));
											});
										}
										After(W8, 1.6f, [S]() { Shot(S, TEXT("swim_peers_move")); });
										After(W8, 2.45f, [WS]()
										{
											for (int32 U : { kPeerUserNum, kPeerUserNum + 1 })
											{
												if (AAsdaPlayerCharacter* Pm = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(U)) : nullptr) { Pm->SetAnimState(EAsdaPlayerState::Idle); }
											}
										});
										SpawnGateMob(W8, S);
										TWeakObjectPtr<AAsdaPlayerCharacter> MeW(Me8);
										// ── W9: океан sunnycoast ──
										if (S->MapId == 6 && Me8->WalkGrid)
										{
											const UAsdaWalkGrid* G9 = Me8->WalkGrid;
											int32 Near = 0, Far = 0;
											for (int32 X = 80; X < 430; X += 10)
											{
												Near += G9->IsTileSwimOpen(X, 370) ? 1 : 0;
												Far += (G9->IsTileSwimOpen(X, 480) || G9->IsTileWalkable(X, 480)) ? 1 : 0;
											}
											S->Expect(Near > 20 && Far == 0, FString::Printf(TEXT("W9 океан: у берега (z 370) открыто %d из 35, у края (z 480) %d"), Near, Far));
										}
										// ── W10 через 1 с (актор моба-заглушки появляется не в том же кадре) ──
										After(W8, 2.6f, [WW, S, MeW, Finish, WS]()
										{
											if (AAsdaPlayerCharacter* Pf = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum + 1)) : nullptr)
											{
												S->Expect(Pf->IsSwimming() && Pf->GetPlayingClipName().EndsWith(TEXT("anim_238")) && Pf->GetPlayingClipName().Contains(TEXT("_f_")),
													FString::Printf(TEXT("W8 соседка (женская модель) в воде плывёт: %s"), *Pf->GetPlayingClipName()));
												DumpBones(Pf, S->DeepWaterZ, TEXT("соседка (женщина) в воде"));
											}
											PrintPose();
											UWorld* W9 = WW.Get();
											if (!W9) { Finish(); return; }
											RunActionGates(W9, S, MeW.Get());
											After(W9, 1.6f, [WW, S, Finish]() { if (UWorld* W10 = WW.Get()) { RunWingsOverWater(W10, S, Finish); } else { Finish(); } });   // прыжок контроля W10 (1.5 с) — доиграть
										});
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
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSwimOpenWaterSelfTest(
	TEXT("asda.SelfTest.SwimOpenWater"),
	TEXT("Самотест открытой воды: проходимость SwimBits и ключ asda.Swim.OpenWater, автопуть и клик в воду (4006), ход в глубокую "
	     "воду (плавание, клипы 237/238), мелководье, суша, труп в воде (asda.Swim.DeadFloat), сосед в воде, океан sunnycoast. "
	     "`<id карты>` `delay=N` `quit`; игра — на уровне этой карты. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaSwimOpenWaterTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
