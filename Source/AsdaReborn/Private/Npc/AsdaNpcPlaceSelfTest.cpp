// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ МЕСТ NPC — `asda.SelfTest.NpcPlace map=<id> [delay=N] [quit]` (31.10.2026, решение владельца «у сервера верные
//   значения — синхронизируй позиции, где сервер = правда»).
//
//   КОРЕНЬ. Эталон ставит NPC класса 0/2/4 в клетку СЕРВЕРА (`npcMgr_spawnWorldEntityFromTable@0xF90600`: 4067 → клетка·100+50),
//   класса 1/3/5 — в float `npc_list +0x70/+0x74`; метки «!»/«?» мини-карты и карты мира, значок NPC сдачи и подсказка «где
//   выдающий» берут место из `npc_list` (`npcUi_BuildNpcActionList@0xD97650`). У 36/49/104/139/182 npc_list расходился с сервером
//   (метка не у NPC), у 37 — другая карта (в Рейне «!» над ним не ставился вовсе), у 38/39/55/74 места не было. Исправление —
//   ведомость импорта `Tools/data/npc_list_fixes.json` (`Tools/asda_npc_list_sync.py`) → `DT_NpcQuestNpc`, вторые места NPC —
//   строки `<NpcId>_<n>` с тем же NpcId (55: карты 21 и 22; 74: две точки карты 0).
//
//   Офлайн (`-AsdaNoNet`), на запечённой карте `map=` (пакет — `--map /Game/Map/<карта>/<карта>?game=…AsdaWorldGameMode`):
//     A. ПЕРЕПИСЬ. Каждый запечённый NPC карты (кроме отключённых): есть строка `DT_NpcQuestNpc` этого NpcId на этой карте в той же
//        клетке, что актор (класс 0/2/4 — точно; класс 1/3/5 — float npc_list, расстановка берёт его же). Это и «у NPC без правки
//        ничего не сдвинулось», и «у исправленных место = актор»; у NPC с несколькими акторами (74) — каждый на своей строке.
//     B. ПУТЬ ИГРОКА — метки. Для каждого NPC карты с записями сценария «!»/«?» подбирается состояние персонажа и журнала по условию
//        записи (`asda.Quest.FakeSelf`/`asda.Quest.FakeState` → штатный `ApplyQuestStateBlob`, как 5047), затем штатная раздача
//        `RebuildMarks`: B1 — метка над головой актора (`QuestMarkKind`); B2 — метка мини-карты (модель меток `UAsdaMinimapSubsystem`,
//        из неё рисуют и мини-карта, и карта мира) в клетке актора; B3 — то же при ОТКРЫТОЙ карте мира; B4 — ни одна метка карты не
//        стоит вне NPC (метка у NPC, у которого есть актор, — только в клетке актора). Состояние, при котором метки нет, — строка
//        «нет состояния» (не провал: условия записи шире, чем умеет эмуляция), итог честно её считает.
//     C. Вторые места: `AsdaQuestGuideTarget::NpcPlaces` (значок NPC сдачи, «Следовать») отдаёт все места сервера (55 → 2, 74 → 2).
//   Строки — «МЕСТО-NPC ok/FAIL/инфо», итог — «МЕСТО-NPC ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcGmOnly.h"                 // NPC «только для GM_» — вне переписи мест
#include "AsdaDataTableCache.h"                 // AsdaDt::Cached — DT_NpcQuestNpc / DT_NpcQuestScript
#include "AsdaCVarSave.h"                       // ArmWorldGuard
#include "Quest/AsdaQuestTypes.h"               // FAsdaNpcQuestNpcRow, FAsdaNpcQuestScriptRow
#include "Quest/AsdaQuestMarkerSubsystem.h"     // RebuildMarks — штатная раздача меток
#include "Quest/AsdaQuestGuideTarget.h"         // NpcPlaces — места NPC для значка сдачи и «Следовать»
#include "UI/AsdaMinimapSubsystem.h"            // модель меток мини-карты и карты мира
#include "World/AsdaCoord.h"                    // UeToGame2D, kTileUnits
#include "Test/AsdaSelfTestKit.h"               // аргументы, отказ, итог, выход
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNpcPlaceTest, Log, All);

namespace AsdaNpcPlaceTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.NpcPlace");                                       // команда
	static const TCHAR* const kPrefix = TEXT("МЕСТО-NPC");                                                 // префикс строк лога
	static const TCHAR* const kNpcDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc");    // места NPC
	static const TCHAR* const kScriptDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestScript.DT_NpcQuestScript");   // сценарии

	// Запечённый NPC карты: актор и его клетка.
	struct FActorCell
	{
		TWeakObjectPtr<AAsdaNpcActor> Actor;   // актор
		int32 NpcId = 0;                       // NpcId
		FIntPoint Tile;                        // клетка актора (игровые X/Y / 100, вниз)
	};

	// Состояние для подбора метки: уровень/класс своего персонажа и журнал.
	struct FTry
	{
		int32 Level = 1, Class = 1;            // asda.Quest.FakeSelf
		int32 Quest = 999, Stage = 0;          // asda.Quest.FakeState: взятое задание и шаг (999 — нет)
		int32 Done = -1;                       // завершённое задание (−1 — нет)
	};

	// Прогон.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;              // мир прогона
		bool bQuit = false;                    // выйти по итогу
		int32 MapId = -1;                      // карта прогона
		int32 Ok = 0, Fail = 0, NoState = 0;   // счётчики: проверки и NPC без найденного состояния с меткой
		TArray<FActorCell> Actors;             // запечённые NPC карты
		TArray<int32> MarkNpcs;                // NPC раздела B (есть записи сценария с меткой 1/2/3)
		int32 Next = 0;                        // следующий NPC раздела B

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaNpcPlaceTest, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else { ++Fail; UE_LOG(LogAsdaNpcPlaceTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Info(const FString& What) { UE_LOG(LogAsdaNpcPlaceTest, Log, TEXT("%s инфо %s"), kPrefix, *What); }
		template <class T> T* Sub() const { return W.IsValid() && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Delay секунд (мир мог уйти — тогда ничего).
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	static void Exec(const FCtxRef& C, const FString& Cmd) { if (GEngine && C->W.IsValid()) { GEngine->Exec(C->W.Get(), *Cmd); } }

	// Клетка строки таблицы — как у меток карты (`WorldX / kTileUnits`, вниз).
	static FIntPoint RowTile(const FAsdaNpcQuestNpcRow& R)
	{
		return FIntPoint(FMath::FloorToInt(R.WorldX / AsdaCoord::kTileUnits), FMath::FloorToInt(R.WorldY / AsdaCoord::kTileUnits));
	}

	// Строки `DT_NpcQuestNpc` этого NpcId (MapId −1 — любые карты).
	static TArray<const FAsdaNpcQuestNpcRow*> RowsOf(int32 NpcId, int32 MapId)
	{
		TArray<const FAsdaNpcQuestNpcRow*> Out;
		if (const UDataTable* T = AsdaDt::Cached(kNpcDT))
		{
			for (const TPair<FName, uint8*>& It : T->GetRowMap())
			{
				const FAsdaNpcQuestNpcRow* R = reinterpret_cast<const FAsdaNpcQuestNpcRow*>(It.Value);
				if (R && R->NpcId == NpcId && (MapId < 0 || R->MapId == MapId)) { Out.Add(R); }
			}
		}
		return Out;
	}

	// Есть ли метка вида Kind в клетке Tile (модель меток мини-карты/карты мира).
	static bool HasMarkAt(UAsdaMinimapSubsystem* MM, const FIntPoint& Tile)
	{
		for (EAsdaMapMark K : { EAsdaMapMark::Quest, EAsdaMapMark::Quest2 })
		{
			if (const TArray<FAsdaMapMark>* L = MM->FindMarks(K))
			{
				for (const FAsdaMapMark& M : *L) { if (M.TileX == Tile.X && M.TileY == Tile.Y) { return true; } }
			}
		}
		return false;
	}

	// B4: каждая метка карты — в клетке актора своего NPC; метка, у строки которой актора на карте нет, — допустима (сервер этого
	//   NPC здесь не ставит — его не правим). Возвращает число «чужих» меток (метка от строки NPC с актором, но не в клетке актора).
	static int32 StrayMarks(const FCtxRef& C, UAsdaMinimapSubsystem* MM, FString& Where)
	{
		TSet<FIntPoint> ActorTiles, Excused;
		for (const FActorCell& A : C->Actors) { ActorTiles.Add(A.Tile); }
		if (const UDataTable* T = AsdaDt::Cached(kNpcDT))
		{
			for (const TPair<FName, uint8*>& It : T->GetRowMap())
			{
				const FAsdaNpcQuestNpcRow* R = reinterpret_cast<const FAsdaNpcQuestNpcRow*>(It.Value);
				if (!R || R->MapId != C->MapId) { continue; }
				if (!C->Actors.ContainsByPredicate([R](const FActorCell& A) { return A.NpcId == R->NpcId; })) { Excused.Add(RowTile(*R)); }
			}
		}
		int32 N = 0;
		for (EAsdaMapMark K : { EAsdaMapMark::Quest, EAsdaMapMark::Quest2 })
		{
			if (const TArray<FAsdaMapMark>* L = MM->FindMarks(K))
			{
				for (const FAsdaMapMark& M : *L)
				{
					const FIntPoint P(M.TileX, M.TileY);
					if (M.MapId != C->MapId || ActorTiles.Contains(P) || Excused.Contains(P)) { continue; }
					++N;
					Where += FString::Printf(TEXT(" (%d,%d)"), P.X, P.Y);
				}
			}
		}
		return N;
	}

	// Состояния, при которых сценарий NPC может дать метку 1/2/3: по одному на запись сценария с меткой (условия записи —
	//   уровень, класс, взятое задание и шаг, завершённое задание).
	static TArray<FTry> TriesFor(const FString& Nsr)
	{
		TArray<FTry> Out;
		const UDataTable* T = AsdaDt::Cached(kScriptDT);
		if (!T) { return Out; }
		for (const TPair<FName, uint8*>& It : T->GetRowMap())
		{
			const FAsdaNpcQuestScriptRow* S = reinterpret_cast<const FAsdaNpcQuestScriptRow*>(It.Value);
			if (!S || !S->NsrName.Equals(Nsr, ESearchCase::IgnoreCase) || S->Mark < 1 || S->Mark > 3) { continue; }
			FTry X;
			X.Level = FMath::Clamp(S->LevelMin > 0 ? S->LevelMin : 1, 1, S->LevelMax > 0 ? S->LevelMax : 150);
			X.Class = S->ClassId > 0 ? S->ClassId : 1;
			if (S->QuestActive > 0) { X.Quest = S->QuestActive; X.Stage = FMath::Max(S->QuestStage, 0); }
			X.Done = S->QuestDone > 0 ? S->QuestDone : -1;
			if (!Out.ContainsByPredicate([&X](const FTry& Y)
				{ return Y.Level == X.Level && Y.Class == X.Class && Y.Quest == X.Quest && Y.Stage == X.Stage && Y.Done == X.Done; }))
			{
				Out.Add(X);
			}
		}
		return Out;
	}

	static void Finish(const FCtxRef& C)
	{
		if (UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>()) { if (MM->IsBigMapOpen()) { MM->ToggleBigMap(); } }
		Exec(C, TEXT("asda.Quest.FakeState 999 0"));   // журнал — пустой
		Exec(C, TEXT("asda.Quest.FakeSelf"));          // подмена уровня снята
		const FString Tail = FString::Printf(TEXT(" карта=%d NPC без состояния с меткой=%d"), C->MapId, C->NoState);
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaNpcPlaceTest, kPrefix, C->Ok, C->Fail, *Tail);
	}

	// Раздел B для NPC C->MarkNpcs[C->Next]: перебор состояний, пока у актора не появится метка.
	static void NextMarkNpc(const FCtxRef& C);

	static void TryState(const FCtxRef& C, int32 NpcId, TSharedRef<TArray<FTry>> Tries, int32 Idx)
	{
		if (Idx >= Tries->Num())
		{
			++C->NoState;
			C->Info(FString::Printf(TEXT("B NPC %d: ни одно из %d состояний записей сценария не дало метки — меток не проверить"),
				NpcId, Tries->Num()));
			++C->Next;
			NextMarkNpc(C);
			return;
		}
		const FTry& X = (*Tries)[Idx];
		Exec(C, FString::Printf(TEXT("asda.Quest.FakeSelf %d %d 0"), X.Level, X.Class));
		Exec(C, X.Done > 0 ? FString::Printf(TEXT("asda.Quest.FakeState %d %d %d"), X.Quest, X.Stage, X.Done)
		                   : FString::Printf(TEXT("asda.Quest.FakeState %d %d"), X.Quest, X.Stage));
		At(C, 0.3f, [C, NpcId, Tries, Idx]()
		{
			UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
			UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>();
			if (!QM || !MM) { C->Expect(false, TEXT("подсистемы меток есть")); Finish(C); return; }
			QM->RebuildMarks();
			// Метка над головой — у всех акторов этого NPC (B1); первый актор с меткой задаёт состояние.
			TArray<const FActorCell*> Mine;
			for (const FActorCell& A : C->Actors) { if (A.NpcId == NpcId && A.Actor.IsValid()) { Mine.Add(&A); } }
			const int32 Mk = Mine.Num() ? Mine[0]->Actor->QuestMarkKind : 0;
			if (Mk < 1 || Mk > 3) { TryState(C, NpcId, Tries, Idx + 1); return; }
			const FTry& Y = (*Tries)[Idx];
			const FString St = FString::Printf(TEXT("ур.%d кл.%d задание %d/%d завершено %d"), Y.Level, Y.Class, Y.Quest, Y.Stage, Y.Done);
			for (const FActorCell* A : Mine)
			{
				C->Expect(A->Actor->QuestMarkKind == Mk, FString::Printf(TEXT("B1 NPC %d (%d,%d): метка %d над головой (%s)"),
					NpcId, A->Tile.X, A->Tile.Y, A->Actor->QuestMarkKind, *St));
				// Метка 3 («?») на карту не идёт (`partyListPushEntry` — только 1 и 2).
				if (Mk != 3)
				{
					C->Expect(HasMarkAt(MM, A->Tile), FString::Printf(TEXT("B2 NPC %d: метка «!» мини-карты в клетке актора (%d,%d)"),
						NpcId, A->Tile.X, A->Tile.Y));
				}
			}
			FString Where;
			const int32 Stray = StrayMarks(C, MM, Where);
			C->Expect(Stray == 0, FString::Printf(TEXT("B4 NPC %d: меток вне своих NPC %d%s"), NpcId, Stray, *Where));
			if (Mk == 3) { ++C->Next; NextMarkNpc(C); return; }
			// B3: карта мира открыта — метки текущей карты из той же раздачи.
			if (!MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
			At(C, 0.4f, [C, NpcId, Mine]()
			{
				UAsdaMinimapSubsystem* MM2 = C->Sub<UAsdaMinimapSubsystem>();
				UAsdaQuestMarkerSubsystem* QM2 = C->Sub<UAsdaQuestMarkerSubsystem>();
				if (QM2) { QM2->RebuildMarks(); }
				for (const FActorCell* A : Mine)
				{
					C->Expect(MM2 && MM2->IsBigMapOpen() && HasMarkAt(MM2, A->Tile),
						FString::Printf(TEXT("B3 NPC %d: карта мира открыта, метка в клетке актора (%d,%d)"), NpcId, A->Tile.X, A->Tile.Y));
				}
				if (MM2 && MM2->IsBigMapOpen()) { MM2->ToggleBigMap(); }
				++C->Next;
				At(C, 0.2f, [C]() { NextMarkNpc(C); });
			});
		});
	}

	static void NextMarkNpc(const FCtxRef& C)
	{
		if (C->Next >= C->MarkNpcs.Num()) { Finish(C); return; }
		const int32 NpcId = C->MarkNpcs[C->Next];
		const TArray<const FAsdaNpcQuestNpcRow*> R = RowsOf(NpcId, C->MapId);
		TSharedRef<TArray<FTry>> Tries = MakeShared<TArray<FTry>>(R.Num() ? TriesFor(R[0]->NsrName) : TArray<FTry>());
		TryState(C, NpcId, Tries, 0);
	}

	static void Run(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (!W || !AsdaDt::Cached(kNpcDT)) { C->Expect(false, TEXT("мир и DT_NpcQuestNpc есть")); Finish(C); return; }

		// ═══ A. Перепись: каждый запечённый NPC карты — на строке таблицы в своей клетке ═══
		TMap<FName, int32> Used;   // строка таблицы → сколько акторов на ней (у 74 два актора — две строки)
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bDisabled || A->bNetSpawned || A->NpcId <= 0) { continue; }
			// ★ 11.11.2026 (D4, разбор красных эталона 7.6.0): с 09.11.2026 (решение владельца, Npc/AsdaNpcGmOnly.h) на карте стоят
			//   акторы «только для GM_» — например Аршес 37 на Альпене («should be in alpen map, but hidden for players»), строка
			//   таблицы которого с 31.10 — Рейн. Игроку такой NPC не показывается и меток не имеет, поэтому в перепись «актор — на
			//   строке таблицы своей карты» он не входит (эталон: «A1 NPC 37 актор (74,372) — строка таблицы НЕТ»).
			if (AsdaNpcGmOnly::HiddenForSelfOnMap(W->GetGameInstance(), A->NpcId, C->MapId)) { continue; }
			FActorCell Cell;
			Cell.Actor = A;
			Cell.NpcId = A->NpcId;
			const FVector2D G = AsdaCoord::UeToGame2D(A->GetActorLocation());
			Cell.Tile = FIntPoint(FMath::FloorToInt(G.X / AsdaCoord::kTileUnits), FMath::FloorToInt(G.Y / AsdaCoord::kTileUnits));
			C->Actors.Add(Cell);
		}
		C->Expect(C->Actors.Num() > 0, FString::Printf(TEXT("A0 на карте %d запечённых NPC: %d"), C->MapId, C->Actors.Num()));
		const UDataTable* T = AsdaDt::Cached(kNpcDT);
		for (const FActorCell& A : C->Actors)
		{
			FName Hit;
			FString Rows;
			for (const TPair<FName, uint8*>& It : T->GetRowMap())
			{
				const FAsdaNpcQuestNpcRow* R = reinterpret_cast<const FAsdaNpcQuestNpcRow*>(It.Value);
				if (!R || R->NpcId != A.NpcId) { continue; }
				Rows += FString::Printf(TEXT(" %s:%d(%d,%d)"), *It.Key.ToString(), R->MapId, RowTile(*R).X, RowTile(*R).Y);
				if (R->MapId == C->MapId && RowTile(*R) == A.Tile && Hit.IsNone() && !Used.Contains(It.Key)) { Hit = It.Key; }
			}
			if (Hit.IsNone())
			{
				// Второй актор той же клетки (нет) — строка уже занята: допускаем повтор, только если клетка совпала.
				for (const TPair<FName, uint8*>& It : T->GetRowMap())
				{
					const FAsdaNpcQuestNpcRow* R = reinterpret_cast<const FAsdaNpcQuestNpcRow*>(It.Value);
					if (R && R->NpcId == A.NpcId && R->MapId == C->MapId && RowTile(*R) == A.Tile) { Hit = It.Key; break; }
				}
			}
			if (!Hit.IsNone()) { Used.FindOrAdd(Hit)++; }
			C->Expect(!Hit.IsNone(), FString::Printf(TEXT("A1 NPC %d актор (%d,%d) — строка таблицы %s; строки NPC:%s"),
				A.NpcId, A.Tile.X, A.Tile.Y, Hit.IsNone() ? TEXT("НЕТ") : *Hit.ToString(), *Rows));
		}

		// ═══ C. Вторые места сервера: NpcPlaces отдаёт все места (55 — карты 21 и 22, 74 — две точки карты 0) ═══
		for (const TPair<int32, int32>& P : { TPair<int32, int32>(55, 2), TPair<int32, int32>(74, 2) })
		{
			TArray<AsdaQuestGuideTarget::FPlace> Pl;
			AsdaQuestGuideTarget::NpcPlaces(P.Key, -1, Pl);
			FString S;
			for (const AsdaQuestGuideTarget::FPlace& X : Pl) { S += FString::Printf(TEXT(" %d:(%d,%d)"), X.MapId, X.TileX, X.TileY); }
			C->Expect(Pl.Num() == P.Value, FString::Printf(TEXT("C NPC %d: мест у значка сдачи / «Следовать» %d (ждём %d):%s"),
				P.Key, Pl.Num(), P.Value, *S));
		}

		// ═══ B. Путь игрока: метки у каждого NPC карты, у сценария которого есть записи с меткой ═══
		TSet<int32> Seen;
		for (const FActorCell& A : C->Actors)
		{
			if (Seen.Contains(A.NpcId)) { continue; }
			Seen.Add(A.NpcId);
			const TArray<const FAsdaNpcQuestNpcRow*> R = RowsOf(A.NpcId, C->MapId);
			if (R.Num() == 0) { continue; }
			// Метки берут только классы 0/4/5 (`npcUi_BuildNpcActionList`); доска (5) считается своим счётом — не здесь.
			if (R[0]->NpcType != 0 && R[0]->NpcType != 4) { continue; }
			if (TriesFor(R[0]->NsrName).Num() > 0) { C->MarkNpcs.Add(A.NpcId); }
		}
		C->Info(FString::Printf(TEXT("B NPC с записями меток на карте: %d"), C->MarkNpcs.Num()));
		NextMarkNpc(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestNpcPlace(
	AsdaNpcPlaceTest::kCmd,
	TEXT("asda.SelfTest.NpcPlace map=<id> [delay=N] [quit] — места NPC: каждый запечённый NPC карты на строке DT_NpcQuestNpc в своей клетке, ")
	TEXT("метки «!»/«?» (над головой, мини-карта, карта мира) в клетке актора, вторые места 55/74. Офлайн, на запечённой карте `map`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaNpcPlaceTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 MapId = -1;
		for (const FString& A : Args) { if (A.StartsWith(TEXT("map="))) { MapId = FCString::Atoi(*A.Mid(4)); } }
		if (MapId < 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нужен map=<номер карты>")); return; }
		// Свой персонаж нужен гейтам мира — офлайн его нет: номер карты, штатный фейковый спавн и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s map=%d selfready%s"), kCmd, MapId, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->MapId = MapId;
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.5f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
