// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НЕВИДИМЫЕ МОБЫ» — ПАЧКА 4013 ИЗ НЕСКОЛЬКИХ ЗАПИСЕЙ (26.10.2026, волна invismobs). [ИСПРАВЛЕНИЕ]
//
// ★ ЗАЧЕМ. Жалоба игроков: «мобы подземелья (Queen's Castle) не видны и бьют 10–15 с», владелец — «стоит один моб, начинаешь
//   драться, из невидимости выходят ещё несколько». Журналы игроков 21–26.10: сервер шлёт 4013 записями по 173 б (до 3 в
//   пакете), а клиент шагал по 174 (эталон network.c:2579) и брал только первую запись — 2/3 мобов сектора не существовали,
//   пока не ударят (4015 kind=3) или не придёт 4019. Правка — ключ `asda.Mobs.BatchAllRecords` (AsdaWorldSubsystem.cpp).
//   Самотест идёт путём игрока (пакеты — `UAsdaNetSubsystem::DebugFeedPacket`, тот же `HandlePacket`, что у сокета) и проверяет:
//   1) вход в зону: пачка из 3 записей по 173 (группа castle_of_chess Knight/Pawn/Bishop, типы 314/298/306 из живого журнала)
//      → все трое в мире и ВИДНЫ (актор не скрыт, меш назначен и видим); время «пакет → видимый меш» в миллисекундах;
//   2) контроль: ключ 0 — та же пачка даёт только первого моба (дефект воспроизводится);
//   3) агрессивная группа: 2-й и 3-й гонятся (4015 kind=5) и бьют (kind=3) — ни один не создаётся «из пакета удара», актор на rid один;
//   4) удар от незнакомого моба (rid, которого сервер не присылал): моб создаётся и виден в том же кадре;
//   5) портал / вход в подземелье: сброс «смена карты», удар 4015 обгоняет пачку, затем пачка — трое видны, у обогнавшего один актор.
//
// Консоль: `asda.Mob.SelfTestBatchSpawn [delay=N] [quit]`. Только офлайн (-AsdaNoNet). Строки — «САМОТЕСТ-ПАЧКАМОБ»,
//   итог — «САМОТЕСТ-ПАЧКАМОБ ИТОГ». Сценарий 5 сбрасывает «смену карты» — в пакете самотестов ставить его последним.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaSessionReset.h"                  // сброс «смена карты» (портал)
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, итог, выход
#include "World/AsdaPlayerController.h"        // свой контроллер
#include "World/AsdaPlayerCharacter.h"         // свой персонаж
#include "World/AsdaWorldSubsystem.h"          // FindNetMobByRid, MobStats
#include "World/AsdaCoord.h"                   // клетки ↔ игра
#include "Npc/AsdaMobActor.h"                  // моб
#include "Components/SkeletalMeshComponent.h"  // видимость скелетного меша
#include "Components/StaticMeshComponent.h"    // видимость статик-фолбэка
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"                       // TActorIterator — счёт акторов на rid
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobBatchTest, Log, All);

namespace AsdaMobBatchTestLocal
{
	constexpr int32 kRecLen = 173;          // длина записи в пачке сервера (GameServer.c:16471 memcpy 0xAD)
	constexpr int32 kSpdDenom = 250;        // знаменатель скорости погони (100/250 = 0.4 юн/мс)
	// Группа из живого журнала castle_of_chess 26.10 18:48:18 (пачка rid 272/273/274): Knight 314, Pawn 298, Bishop 306.
	constexpr int32 kTypes[3] = { 314, 298, 306 };
	constexpr int32 kHp[3] = { 2514, 1764, 1911 };
	// «Сразу» = видимый меш в ТОМ ЖЕ кадре, где обработан пакет (кадров между подачей и показом 0). Миллисекунды печатаются
	//   справочно: первый моб нового типа включает синхронную загрузку модели с диска (холодный процесс ~90 мс на тип).
	constexpr uint64 kVisibleMaxFrames = 0;

	struct FStep
	{
		FString Name;
		float MinSec = 0.f, TimeoutSec = 5.f;
		TFunction<void()> Start;
		TFunction<bool()> Poll;
		TFunction<void()> Check;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0;
		bool bStepStarted = false;
		double StepStartS = 0.0;
		FTimerHandle Tick;
		bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Mob.SelfTestBatchSpawn"), false };
		int32 Ok = 0, Fail = 0;
		bool bWasInGame = false;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 NextRid = 31801;
		TArray<int32> Rids;                     // все мобы прогона (убрать в итоге)
		int32 G[3] = { -1, -1, -1 };            // группа сценария
		int32 Lone = -1;                        // незнакомый моб (сценарий 4)
		double FeedS = 0.0;                     // время подачи пакета сценария
		uint64 FeedFrame = 0;                   // кадр подачи пакета сценария
		TMap<int32, double> SeenMs;             // rid → мс от подачи до первого видимого меша
		TMap<int32, uint64> SeenFrames;         // rid → кадров от подачи до первого видимого меша
		int32 CreatedMark = 0;                  // MobStats.Created на старте сценария

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* Gi = GI(); return Gi ? Gi->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		int32 NewRid() { const int32 R = NextRid++; Rids.Add(R); return R; }
		int32 Created() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->MobStats.Created : 0; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaMobBatchTest, Log, TEXT("САМОТЕСТ-ПАЧКАМОБ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		// Видим ли моб так, как его видит игрок: актор жив и не скрыт, меш назначен и включён, моб не труп.
		bool Shown(int32 R, FString* Why = nullptr) const
		{
			const AAsdaMobActor* M = Mob(R);
			if (!M || !IsValid(M)) { if (Why) { *Why = TEXT("актора нет"); } return false; }
			if (M->IsHidden()) { if (Why) { *Why = TEXT("актор скрыт"); } return false; }
			if (M->IsDead()) { if (Why) { *Why = TEXT("труп"); } return false; }
			const bool bSkel = M->Mesh && M->Mesh->GetSkeletalMeshAsset() && M->Mesh->IsVisible();
			const bool bStat = M->StaticMeshComp && M->StaticMeshComp->GetStaticMesh() && M->StaticMeshComp->IsVisible();
			if (!bSkel && !bStat) { if (Why) { *Why = TEXT("меш не назначен/невидим"); } return false; }
			if (Why) { *Why = FString::Printf(TEXT("виден (%s «%s»)"), bSkel ? TEXT("скелетный") : TEXT("статик"),
				bSkel ? *M->Mesh->GetSkeletalMeshAsset()->GetName() : *M->StaticMeshComp->GetStaticMesh()->GetName()); }
			return true;
		}
		// Отметить первое появление видимого меша у rid (мс от подачи пакета).
		void NoteSeen(int32 R)
		{
			if (!SeenMs.Contains(R) && Shown(R)) { SeenMs.Add(R, (FPlatformTime::Seconds() - FeedS) * 1000.0); SeenFrames.Add(R, GFrameCounter - FeedFrame); }
		}
		// ★ 26.10.2026 (решение владельца «убедись, что они не в одном тайле»): мобы группы G[0..2] видны в РАЗНЫХ клетках —
		//   клетка по видимой позиции актора, попарно; сервер прислал три разные клетки, клиент не должен их склеить.
		void ExpectDistinctCells(const TCHAR* What)
		{
			FIntPoint Cell[3] = { FIntPoint(-1, -1), FIntPoint(-1, -1), FIntPoint(-1, -1) }; bool bAll = true;
			for (int32 i = 0; i < 3; ++i)
			{
				const AAsdaMobActor* M = Mob(G[i]);
				if (!M) { bAll = false; continue; }
				Cell[i] = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(M->GetActorLocation()));
			}
			const bool bDistinct = bAll && Cell[0] != Cell[1] && Cell[0] != Cell[2] && Cell[1] != Cell[2];
			Expect(bDistinct, FString::Printf(TEXT("%s: мобы в разных клетках — (%d,%d) (%d,%d) (%d,%d)"), What,
				Cell[0].X, Cell[0].Y, Cell[1].X, Cell[1].Y, Cell[2].X, Cell[2].Y));
		}
		// Число живых акторов мобов с этим rid (дубль = два актора).
		int32 ActorsOf(int32 R) const
		{
			int32 C = 0;
			if (UWorld* W = World.Get())
			{
				for (TActorIterator<AAsdaMobActor> It(W); It; ++It)
				{
					if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->bNetSpawned && It->NetRenderId == R) { ++C; }
				}
			}
			return C;
		}
	};

	// Запись пачки 4013 длиной 173 (раскладка сервера): rid@0, тип@2, состояние@8 = 1 (стоит), HP@9, скорость@13, cur=dst@19..25, гейт@27 = 1.
	static TArray<uint8> Rec173(int32 Rid, int32 Type, const FIntPoint& Tile, int32 Hp)
	{
		TArray<uint8> R = AsdaTestPackets::MobSpawn4013(Rid, Type, Tile, /*State*/1, /*Speed*/1666);
		R.SetNum(kRecLen);
		AsdaBytes::WrI32(&R[9], Hp);
		return R;
	}
	// Пачка: записи вплотную, как Send_SectorInNewAddMonsterInfo (`v15 += 173`).
	static TArray<uint8> Batch(const TArray<TArray<uint8>>& Recs)
	{
		TArray<uint8> B;
		for (const TArray<uint8>& R : Recs) { B.Append(R); }
		return B;
	}
	// Тело 4015 (37 б, раскладка Send_SectorInUpdateMonster_NPC_Info): @0 игрок-жертва (или −1), @2 тип, @4 rid, @6 kind, @7/@9 cur,
	//   @11/@13 dst, @15 знаменатель скорости, @17/@19 темп клипа ×1e-4, @21 урон по @0 (−1 — нет), @25 HP моба, @29 вариант, @30 тип удара,
	//   @31 отражение −1.
	static TArray<uint8> Body4015(int32 Victim, int32 Type, int32 Rid, uint8 Kind, const FIntPoint& Cur, const FIntPoint& Dst, int32 Dmg, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], (int16)Victim);
		AsdaBytes::WrI16(&B[2], (int16)Type);
		AsdaBytes::WrI16(&B[4], (int16)Rid);
		B[6] = Kind;
		AsdaBytes::WrI16(&B[7], (int16)Cur.X);  AsdaBytes::WrI16(&B[9], (int16)Cur.Y);
		AsdaBytes::WrI16(&B[11], (int16)Dst.X); AsdaBytes::WrI16(&B[13], (int16)Dst.Y);
		AsdaBytes::WrI16(&B[15], (int16)((Kind == 3) ? 0 : kSpdDenom));
		AsdaBytes::WrI16(&B[17], (int16)10000);
		AsdaBytes::WrI16(&B[19], (int16)10000);
		AsdaBytes::WrI32(&B[21], Dmg);
		AsdaBytes::WrI32(&B[25], Hp);
		B[29] = 0;
		B[30] = 0;
		AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
}

struct FAsdaMobBatchSpawnSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaMobBatchSpawnSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMobBatchTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Mob.SelfTestBatchSpawn"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaMobBatchTest, Warning, TEXT("САМОТЕСТ-ПАЧКАМОБ: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;

	auto Add = [S](const FString& Name, float MinSec, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.MinSec = MinSec; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(Op, B); } };
	auto T = [S](int32 Dx, int32 Dz) { return S->HomeTile + FIntPoint(Dx, Dz); };
	// Убрать мобов прошлых сценариев (реестр мира сам снимет протухшие слабые ссылки).
	auto Clear = [S]()
	{
		for (const int32 R : S->Rids) { if (AAsdaMobActor* M = S->Mob(R)) { M->Destroy(); } }
		S->Rids.Reset(); S->SeenMs.Reset(); S->SeenFrames.Reset();
		S->G[0] = S->G[1] = S->G[2] = -1; S->Lone = -1;
	};
	// Подать пачку из 3 записей группы у клеток +3..+4 от дома.
	auto FeedGroup = [S, Feed, T]()
	{
		TArray<TArray<uint8>> Recs;
		const FIntPoint Tiles[3] = { T(3, 0), T(4, 1), T(3, 2) };
		for (int32 i = 0; i < 3; ++i) { S->G[i] = S->NewRid(); Recs.Add(Rec173(S->G[i], kTypes[i], Tiles[i], kHp[i])); }
		S->CreatedMark = S->Created();
		S->FeedS = FPlatformTime::Seconds(); S->FeedFrame = GFrameCounter;
		Feed(4013, Batch(Recs));
		for (int32 i = 0; i < 3; ++i) { S->NoteSeen(S->G[i]); }   // обработка синхронная — замер в том же кадре
	};

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 0.f, 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC()) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, TEXT("asda.Mob.SelfTestBatchSpawn"));
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Mobs.BatchAllRecords"), 1);
		S->Expect(IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Mobs.BatchAllRecords")) != nullptr, TEXT("ключ asda.Mobs.BatchAllRecords зарегистрирован"));
		UE_LOG(LogAsdaMobBatchTest, Log, TEXT("САМОТЕСТ-ПАЧКАМОБ дом: клетка (%d,%d), свой userNum %d"), S->HomeTile.X, S->HomeTile.Y, Net->SelfUserNum);
	}, []() { return true; }, []() {});

	// ─────────────────────── 1. Вход в зону: пачка из 3 записей ───────────────────────
	Add(TEXT("1 вход в зону: пачка 4013 из 3 записей по 173"), 0.2f, 3.f, [S, Clear, FeedGroup]()
	{
		Clear();
		FeedGroup();
	}, [S]()
	{
		for (int32 i = 0; i < 3; ++i) { S->NoteSeen(S->G[i]); }
		return S->SeenMs.Num() == 3;
	}, [S, T]()
	{
		const FIntPoint Tiles[3] = { T(3, 0), T(4, 1), T(3, 2) };
		for (int32 i = 0; i < 3; ++i)
		{
			FString Why; const bool bShown = S->Shown(S->G[i], &Why);
			const double* Ms = S->SeenMs.Find(S->G[i]);
			const uint64* Fr = S->SeenFrames.Find(S->G[i]);
			const AAsdaMobActor* M = S->Mob(S->G[i]);
			const float Err = M ? FVector2D::Distance(AsdaCoord::UeToGame2D(M->GetActorLocation()), AsdaCoord::TileCenterGame(Tiles[i])) : -1.f;
			S->Expect(bShown && Fr && *Fr <= kVisibleMaxFrames && Err >= 0.f && Err <= 15.f,
				FString::Printf(TEXT("запись %d (rid %d тип %d «%s»): %s, пакет → видимый меш: кадров %d (тот же кадр), %.1f мс; на клетке пакета (ошибка %.1f ед.)"),
					i + 1, S->G[i], kTypes[i], M ? *M->MobName : TEXT("—"), *Why, Fr ? (int32)*Fr : -1, Ms ? *Ms : -1.0, Err));
		}
		S->Expect(S->Created() - S->CreatedMark == 3, FString::Printf(TEXT("создано мобов пачкой: %d (ожидалось 3)"), S->Created() - S->CreatedMark));
		S->ExpectDistinctCells(TEXT("группа одной пачки"));
	});

	// ─────────────────────── 2. Контроль: ключ 0 — эталон 1:1 ───────────────────────
	Add(TEXT("2 контроль: ключ 0 (эталон, шаг 174)"), 0.5f, 2.f, [S, Clear, FeedGroup]()
	{
		Clear();
		AsdaCVarSave::SetForTest(TEXT("asda.Mobs.BatchAllRecords"), 0);
		FeedGroup();
	}, []() { return true; }, [S]()
	{
		const bool b0 = S->Shown(S->G[0]), b1 = S->Mob(S->G[1]) != nullptr, b2 = S->Mob(S->G[2]) != nullptr;
		S->Expect(b0 && !b1 && !b2, FString::Printf(TEXT("контроль: первая запись видна=%d, 2-я в мире=%d, 3-я в мире=%d — дефект воспроизводится (ожидалось 1/0/0)"),
			b0 ? 1 : 0, b1 ? 1 : 0, b2 ? 1 : 0));
		AsdaCVarSave::SetForTest(TEXT("asda.Mobs.BatchAllRecords"), 1);
	});

	// ─────────────────────── 3. Агрессивная группа: погоня и удары 2-го и 3-го ───────────────────────
	Add(TEXT("3 агрессивная группа: погоня и удары 2-й и 3-й записи"), 1.2f, 4.f, [S, Clear, FeedGroup, Feed, T]()
	{
		Clear();
		FeedGroup();
		S->CreatedMark = S->Created();
		const int32 Me = S->N() ? S->N()->SelfUserNum : 0;
		for (int32 i = 1; i < 3; ++i)
		{
			const AAsdaMobActor* M = S->Mob(S->G[i]);
			const FIntPoint Cur = M ? AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(M->GetActorLocation())) : T(3, 2 * (i - 1));
			Feed(4015, Body4015(-1, kTypes[i], S->G[i], /*kind*/5, Cur, T(1, 0), /*Dmg*/-1, kHp[i]));
		}
		FTimerHandle H;
		TWeakPtr<FState> WS = S;
		if (UWorld* Wd = S->World.Get())
		{
			Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WS, Feed, T, Me]()
			{
				TSharedPtr<FState> St = WS.Pin(); if (!St) { return; }
				for (int32 i = 1; i < 3; ++i) { Feed(4015, Body4015(Me, kTypes[i], St->G[i], /*kind*/3, T(1, 0), T(1, 0), /*Dmg*/37, kHp[i])); }
			}), 0.5f, false);
		}
	}, []() { return true; }, [S]()
	{
		const int32 NewC = S->Created() - S->CreatedMark;
		S->Expect(NewC == 0, FString::Printf(TEXT("погоня 4015 kind=5 и удары kind=3 по известным мобам — новых «из пакета удара»: %d (ожидалось 0)"), NewC));
		for (int32 i = 1; i < 3; ++i)
		{
			FString Why; const bool bShown = S->Shown(S->G[i], &Why);
			const int32 NA = S->ActorsOf(S->G[i]);
			S->Expect(bShown && NA == 1, FString::Printf(TEXT("атакующий rid %d: %s, акторов на rid %d (ожидался 1) — невидимым не бил"), S->G[i], *Why, NA));
		}
	});

	// ─────────────────────── 4. Удар от незнакомого моба ───────────────────────
	Add(TEXT("4 удар от незнакомого моба (rid без 4013)"), 0.1f, 2.f, [S, Clear, Feed, T]()
	{
		Clear();
		S->Lone = S->NewRid();
		S->CreatedMark = S->Created();
		S->FeedS = FPlatformTime::Seconds(); S->FeedFrame = GFrameCounter;
		const int32 Me = S->N() ? S->N()->SelfUserNum : 0;
		Feed(4015, Body4015(Me, kTypes[2], S->Lone, /*kind*/3, T(1, 1), T(1, 1), /*Dmg*/41, kHp[2]));
		S->NoteSeen(S->Lone);
	}, [S]() { S->NoteSeen(S->Lone); return S->SeenMs.Contains(S->Lone); }, [S]()
	{
		FString Why; const bool bShown = S->Shown(S->Lone, &Why);
		const double* Ms = S->SeenMs.Find(S->Lone);
		const uint64* Fr = S->SeenFrames.Find(S->Lone);
		S->Expect(bShown && Fr && *Fr <= kVisibleMaxFrames && S->Created() - S->CreatedMark == 1,
			FString::Printf(TEXT("незнакомый rid %d создан из пакета удара: %s, пакет → видимый меш: кадров %d, %.1f мс; создано %d"),
				S->Lone, *Why, Fr ? (int32)*Fr : -1, Ms ? *Ms : -1.0, S->Created() - S->CreatedMark));
	});

	// ─────────────────────── 5. Портал / вход в подземелье ───────────────────────
	Add(TEXT("5 портал: смена карты, удар обгоняет пачку"), 0.3f, 3.f, [S, Clear, Feed, T]()
	{
		Clear();
		AActor* Me = S->WS() ? S->WS()->LocalPlayerActor.Get() : nullptr;
		if (UGameInstance* Gi = S->GI())
		{
			if (UAsdaSessionResetSubsystem* SR = Gi->GetSubsystem<UAsdaSessionResetSubsystem>()) { SR->Broadcast(EAsdaSessionResetReason::MapChanged); }
		}
		// Офлайн травела нет — свой актор жив; возвращаем ссылку, как это сделал бы 4003 новой карты.
		if (UAsdaWorldSubsystem* Wsub = S->WS()) { if (!Wsub->LocalPlayerActor && Me) { Wsub->LocalPlayerActor = Me; } }
		const int32 MeIdx = S->N() ? S->N()->SelfUserNum : 0;
		for (int32 i = 0; i < 3; ++i) { S->G[i] = S->NewRid(); }
		S->CreatedMark = S->Created();
		S->FeedS = FPlatformTime::Seconds(); S->FeedFrame = GFrameCounter;
		// 4015 удар 3-го обгоняет пачку (журнал: «4015 обгоняет батч 4013» — штатный путь), затем пачка всех троих.
		Feed(4015, Body4015(MeIdx, kTypes[2], S->G[2], /*kind*/3, T(3, 2), T(3, 2), /*Dmg*/29, kHp[2]));
		TArray<TArray<uint8>> Recs;
		const FIntPoint Tiles[3] = { T(3, 0), T(4, 1), T(3, 2) };
		for (int32 i = 0; i < 3; ++i) { Recs.Add(Rec173(S->G[i], kTypes[i], Tiles[i], kHp[i])); }
		Feed(4013, Batch(Recs));
		for (int32 i = 0; i < 3; ++i) { S->NoteSeen(S->G[i]); }
	}, [S]()
	{
		for (int32 i = 0; i < 3; ++i) { S->NoteSeen(S->G[i]); }
		return S->SeenMs.Num() == 3;
	}, [S]()
	{
		for (int32 i = 0; i < 3; ++i)
		{
			FString Why; const bool bShown = S->Shown(S->G[i], &Why);
			const double* Ms = S->SeenMs.Find(S->G[i]);
			const uint64* Fr = S->SeenFrames.Find(S->G[i]);
			const int32 NA = S->ActorsOf(S->G[i]);
			S->Expect(bShown && Fr && *Fr <= kVisibleMaxFrames && NA == 1,
				FString::Printf(TEXT("после портала запись %d (rid %d): %s, пакет → видимый меш: кадров %d, %.1f мс; акторов на rid %d (ожидался 1)"),
					i + 1, S->G[i], *Why, Fr ? (int32)*Fr : -1, Ms ? *Ms : -1.0, NA));
		}
		S->Expect(S->Created() - S->CreatedMark == 3, FString::Printf(TEXT("создано мобов: %d (1 из удара + 2 из пачки; ожидалось 3, без дублей)"), S->Created() - S->CreatedMark));
		S->ExpectDistinctCells(TEXT("группа после портала"));
	});

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 0.f, 1.f, [S, Clear, Net]()
	{
		Clear();
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), TEXT("asda.Mob.SelfTestBatchSpawn"), false, LogAsdaMobBatchTest, TEXT("САМОТЕСТ-ПАЧКАМОБ"),
			S->Ok, S->Fail, TEXT(""));
	}, []() { return true; }, []() {});

	// ─────────────────────── Спавн и исполнитель ───────────────────────
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaMobBatchTest, Log, TEXT("САМОТЕСТ-ПАЧКАМОБ шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = El >= Stp.MinSec && (!Stp.Poll || Stp.Poll());
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobBatchSpawn(
	TEXT("asda.Mob.SelfTestBatchSpawn"),
	TEXT("Самотест «невидимые мобы»: пачка 4013 из 3 записей по 173 → все трое видны сразу (замер пакет → видимый меш); контроль "
	     "asda.Mobs.BatchAllRecords 0 — только первая; погоня/удары 2-й и 3-й записи без создания «из пакета удара»; удар незнакомого "
	     "моба — моб виден в том же кадре; портал (смена карты, удар обгоняет пачку) — без дублей. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Mob.SelfTestBatchSpawn"));
		FAsdaMobBatchSpawnSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
