// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxStatusSelfTest.cpp — самотест ЭФФЕКТОВ СОСТОЯНИЙ и одноразовых эффектов после правок ревизии
//   `Reports/owner_1008/fx_loop_REVIEW.md` (08.10.2026, отчёт исполнителя — `fx_loop_fixes.md`).
//
// ЗАЧЕМ. Ревизия ЦИКЛА 1:1 нашла, что переопределение контейнера (`эффект+0x2A8`) у 40 обработчиков состояний
//   в порту не подключено: оглушение отыгрывало один раз и гасло, заморозка гасла посреди действия. Проверять
//   это нужно ТЕМ ЖЕ путём, что в игре ([[diagnostic-must-use-real-path]]): состояние приходит ПАКЕТОМ в штатный
//   `UAsdaNetSubsystem::HandlePacket` (через `DebugFeedPacket`), дальше — делегат, `UAsdaEffectsSubsystem`,
//   программа `DT_BuffFx`, `SpawnBuffFx`, эффект-актор и его `SimulateStep`. Копии разбора здесь нет.
//
// СЦЕНАРИЙ `asda.Fx.StatusSelfTest [сек действия = 12] [сек после снятия = 30]`:
//   1) фейковый сосед userNum 777 (`DebugSpawnFakePeer`, путь снапшота 4005/4065) и фейковый моб rid 30777
//      (тип 1 «волк alpen», запись 4013 — штатный приём батча мобов) рядом с ним;
//   2) 5272 на соседа — состояния игрока; 6079 (★ 26.10.2026; прежде 4102) на моба — состояния моба;
//   3) всё время действия раз в 0.2 с: живы ли эффект-акторы слотов fx0/fx1, сколько эмиттеров ВИДНО
//      (построенная секция с вершинами — то, что ушло в рендер), сколько раз контейнер начался заново;
//   4) снятие: 5268 у соседа, 4017 (flags=2, пустой список состояний) у моба — штатные пакеты снятия;
//   5) через 0.5 с — эффекты слотов уничтожены; через «после» — висящих эффект-акторов этих `.jeb` в мире 0;
//   6) одноразовые эффекты (`SpawnByJebName` + `NaturalLifeSpan` — путь всех разовых вспышек): когда видны
//      в последний раз, длительность контейнера, срок жизни, когда исчез актор.
//   Итог — строки `[fx-состояния] ИТОГ …` в логе; ничего не пишет ни в ассеты, ни в настройки.
// ★ Офлайн-гейт: при живом соединении команда отказывается (`RefuseDebugWhenOnline`) — фейковые пакеты на
//   живом сервере недопустимы.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): самотесты и стенды в Shipping не входят
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestRunGate.h"   // RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelfTestBatch.h"                // ★ ревизия В2 дедупа №2: Quit — выход по итогу (перехватывается пакетом)
#include "AsdaBytes.h"                        // WrI16 / WrI32 — тела пакетов
#include "AsdaTestPackets.h"                  // ★ дедуп №2 В1-C (F18): общая раскладка записи 4013
#include "AsdaEffectActor.h"                  // эффект-актор: секции, контейнер, срок жизни
#include "UI/AsdaEffectsSubsystem.h"          // списки состояний (fx0/fx1 записей)
#include "World/AsdaWorldSubsystem.h"         // DebugSpawnFakePeer / GetPlayerActorByUserNum / NetMobs
#include "World/AsdaCoord.h"                  // GameToTile — тайл моба рядом с соседом
#include "World/AsdaPlayerCharacter.h"        // GetGamePosXZ — позиция соседа в игровых координатах
#include "GameFramework/PlayerController.h"   // свой павн — точка, рядом с которой ставится сосед
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"                      // TActorIterator — висящие эффект-акторы мира
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace AsdaFxStatusTest
{
	// Сосед и моб самотеста: номера заведомо вне живых диапазонов (как у прочих самотестов).
	constexpr int32 kPeer = 777;            // userNum фейкового соседа
	constexpr int32 kMobRid = 30777;        // renderId фейкового моба
	constexpr int32 kMobType = 1;           // тип моба (Mob list: волк alpen `mon_s_0_rwolf01-a_00`)
	constexpr float kStep = 0.2f;           // шаг выборки, сек

	// Одно проверяемое состояние: кто носитель и какой режим контейнера ждём по эталону.
	struct FCase
	{
		int32 Id = -1;                      // id состояния 0..349
		bool bMob = false;                  // true — моб (вторая таблица обработчиков), false — сосед-игрок
		const TCHAR* Name = TEXT("");       // ярлык для лога
		int32 ExpectLoop = -1;              // переопределение контейнера по `DT_BuffFx` (1 — повтор, 3 — вечный)
		// живые наблюдения
		int32 Samples = 0;                  // выборок за время действия
		int32 VisibleSamples = 0;           // из них с хотя бы одной видимой секцией
		int32 Restarts = 0;                 // сколько раз таймер контейнера начался заново
		TMap<TWeakObjectPtr<AActor>, float> LastTimer;   // прошлый таймер контейнера ПО АКТОРУ (детектор рестарта; у timbom два актора)
		float ContDur = 0.f;                // длительность контейнера (из актора)
		int32 ContEnd = -1;                 // действующий endMode контейнера (с переопределением)
		bool bVisibleAtEnd = false;         // видно ли эффект в последние 0.6 с действия
		bool bHadActor = false;             // актор слота вообще появился
		int32 AliveAfterRemove = -1;        // живых акторов слотов через 0.5 с после снятия
		TArray<TWeakObjectPtr<AActor>> Seen;   // все акторы слотов, замеченные за прогон
	};

	// Одноразовый эффект (не состояние): путь `SpawnByJebName` + `NaturalLifeSpan`.
	struct FOneShot
	{
		const TCHAR* Jeb = TEXT("");        // база имени .jeb
		TWeakObjectPtr<AAsdaEffectActor> Fx;   // заспавненный актор
		float SpawnT = 0.f;                 // время спавна (сек от старта теста)
		float LifeSpan = 0.f;               // выставленный срок жизни
		float ContDur = 0.f;                // контейнер из таблицы
		int32 ContEnd = -1;                 // endMode контейнера
		float LastVisibleT = -1.f;          // последнее время, когда секция была видна
		float FrozenFromT = -1.f;           // с какого времени все видимые эмиттеры застыли
		float GoneT = -1.f;                 // когда актор исчез
	};

	// Состояние прогона (живёт в лямбде таймера).
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;       // мир прогона
		FTimerHandle Timer;                 // таймер шага
		float T = 0.f;                      // сек от старта
		float ActSec = 12.f;                // длительность действия состояний
		float AfterSec = 30.f;              // ожидание после снятия
		int32 Phase = 0;                    // 0 подготовка, 1 наложено, 2 снято, 3 итог
		int32 TraceId = -1;                 // id состояния для покадровой трассировки контейнера (третий аргумент; −1 = нет)
		float ApplyT = 2.f;                 // момент наложения
		TArray<FCase> Cases;                // проверяемые состояния
		TArray<FOneShot> Shots;             // одноразовые эффекты
		TArray<FString> Stems;              // имена .jeb (без суффикса варианта) для подсчёта висящих
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Fx.StatusSelfTest"), false };   // ★ СРЕДН-1: офлайн повторно на первом шаге таймера
		bool bQuit = false;                 // ★ ревизия В2 дедупа №2 (11.10.2026): аргумент `quit` — выход по итогу (пакет самотестов)
	};

	// Видимые эмиттеры актора (секции с вершинами) и сколько из видимых застыло.
	static void CountVisible(const AAsdaEffectActor* Fx, int32& OutVis, int32& OutFrozenLike)
	{
		OutVis = 0; OutFrozenLike = 0;
		if (!Fx) { return; }
		for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
		{
			FVector C; float R = 0.f; int32 V = 0;
			if (Fx->EmitterSectionStats(i, C, R, V) && V > 0)
			{
				++OutVis;
				int32 Mode = 0, DT = 0, DE = 0, Wrap = 0; float Life = 0.f, Over = 0.f; bool bStop = false, bSeed = false;
				if (Fx->GetLoopDebug(i, Mode, DT, DE, Life, Over, Wrap, bStop, bSeed)
					&& Mode == (int32)EAsdaFxLoopMode::Freeze && Life >= Fx->Emitters[i].Period - 1e-3f)
				{
					++OutFrozenLike;
				}
			}
		}
	}

	// Тело 5272: i16 userIdx@0, u16 id@2, i16 флаг@6, u8 стек@9, u8 вид@10, i16 остаток@11, u8 байт9@13.
	static TArray<uint8> Body5272(int32 UserIdx, int32 Id, int32 Sec)
	{
		TArray<uint8> B; B.SetNumZeroed(16);
		AsdaBytes::WrI16(&B[0], UserIdx);
		AsdaBytes::WrI16(&B[2], Id);
		B[9] = 1;
		AsdaBytes::WrI16(&B[11], Sec);
		B[13] = 99;
		return B;
	}

	// ★ 26.10.2026 (bowfx): состояние моба по номеру СОСТОЯНИЯ кладёт 6079 (`pktHandlerSkillCast`: `buff_ApplySkillBuff(ent, @8, …)`);
	//   4102 по сущности с 26.10 несёт КОД НАВЫКА (цепочка buff0/1/2), поэтому прежняя подача «id состояния в 4102» больше не годится.
	//   Тело 21 б: ok@0, кастер@1, вид цели@3 (0 — моб), цель@4, навык@6 (705 — запись есть), состояние@8, ур.@10, длит.@11, шаг@15, тик@17.
	//   Тик 0 — слота урона по времени нет, только состояние.
	static TArray<uint8> Body6079MobState(int32 Rid, int32 StateId)
	{
		TArray<uint8> B; B.SetNumZeroed(21);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], kPeer);
		B[3] = 0;
		AsdaBytes::WrI16(&B[4], Rid);
		AsdaBytes::WrI16(&B[6], 705);
		AsdaBytes::WrI16(&B[8], StateId);
		B[10] = 1;
		return B;
	}

	// Запись 4013 (шаг 174): rid@0, тип@2, состояние@8, HP@9, скорость@13, тайлы cur@19/21 и dst@23/25, гейт@27.
	//   ★ Дедуп №2 В1-C (F18): раскладка — общая фикстура; значения этого стенда прежние (состояние 1, скорость 10).
	static TArray<uint8> Body4013(int32 Rid, int32 Type, const FIntPoint& Tile)
	{
		return AsdaTestPackets::MobSpawn4013(Rid, Type, Tile, /*State*/1, /*Speed*/10);
	}

	// Тело 4017 flags=2: тип@0, rid@2, флаги@4, 28×i16 id@8 (−1 = пусто), 28×u8 уровни@64 — пустой список снимает всё.
	static TArray<uint8> Body4017Clear(int32 Rid, int32 Type)
	{
		TArray<uint8> B; B.SetNumZeroed(92);
		AsdaBytes::WrI16(&B[0], Type);
		AsdaBytes::WrI16(&B[2], Rid);
		AsdaBytes::WrI32(&B[4], 2);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		return B;
	}

	// Список записей состояния у носителя (сосед — по userIdx, моб — по renderId).
	static const TArray<FAsdaEffectEntry>& ListFor(const UAsdaEffectsSubsystem* Fx, const FCase& C)
	{
		return C.bMob ? Fx->GetMobList(kMobRid) : Fx->GetList(kPeer);
	}

	// Шаг прогона (таймер мира, раз в kStep).
	static void Step(TSharedRef<FRun> Run)   // ★ 08.11 по значению: `ClearTimer` уничтожает лямбду с последней ссылкой (иначе `Run->bQuit` читается из освобождённого)
	{
		UWorld* W = Run->World.Get();
		if (!W) { return; }
		if (!Run->Gate.Pass(W)) { W->GetTimerManager().ClearTimer(Run->Timer); return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
		UGameInstance* GI = W->GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaEffectsSubsystem* FxS = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Net || !FxS || !WS) { W->GetTimerManager().ClearTimer(Run->Timer); return; }
		Run->T += kStep;
		const float T = Run->T;

		// ── 1. моб рядом с соседом (сосед уже стоит — поставлен при запуске) ─────────────────────────
		if (Run->Phase == 0 && T >= 0.6f && T < 0.6f + kStep)
		{
			FVector2D G(0.f, 0.f);
			if (const AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(WS->GetPlayerActorByUserNum(kPeer))) { G = P->GetGamePosXZ(); }
			AsdaTestNet::DebugFeedPacket(Net, 4013, Body4013(kMobRid, kMobType, AsdaCoord::GameToTile(G + FVector2D(150.f, 0.f))));
		}
		// ── 2. наложение ─────────────────────────────────────────────────────────────────────────────
		if (Run->Phase == 0 && T >= Run->ApplyT)
		{
			for (const FCase& C : Run->Cases)
			{
				if (C.bMob) { AsdaTestNet::DebugFeedPacket(Net, 6079, Body6079MobState(kMobRid, C.Id)); }   // ★ bowfx: прежде 4102 (см. Body6079MobState)
				else { AsdaTestNet::DebugFeedPacket(Net, 5272, Body5272(kPeer, C.Id, FMath::CeilToInt(Run->ActSec) + 5)); }
			}
			// одноразовые — у соседа под ногами (тот же спавн, что у разовых вспышек навыков)
			const AActor* PeerA = WS->GetPlayerActorByUserNum(kPeer);
			const FVector At = PeerA ? PeerA->GetActorLocation() : FVector::ZeroVector;
			for (FOneShot& S : Run->Shots)
			{
				AAsdaEffectActor* A = AAsdaEffectActor::SpawnByJebName(W, S.Jeb, At + FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator, nullptr);
				if (!A) { UE_LOG(LogTemp, Warning, TEXT("[fx-состояния] разовый %s: BP не найден"), S.Jeb); continue; }
				S.Fx = A; S.SpawnT = T;
				S.LifeSpan = A->NaturalLifeSpan(0.5f, 1.f, 30.f);
				A->SetLifeSpan(S.LifeSpan);
				A->PeekLoopContainer(S.ContDur, S.ContEnd);
			}
			Run->Phase = 1;
			UE_LOG(LogTemp, Warning, TEXT("[fx-состояния] наложено: состояний %d, разовых %d, действие %.0f с"),
				Run->Cases.Num(), Run->Shots.Num(), Run->ActSec);
			return;
		}

		// ── 3. наблюдение за состояниями ─────────────────────────────────────────────────────────────
		if (Run->Phase == 1)
		{
			const float Rel = T - Run->ApplyT;
			for (FCase& C : Run->Cases)
			{
				int32 Vis = 0;
				for (const FAsdaEffectEntry& E : ListFor(FxS, C))
				{
					if (E.Type != 0 || E.Id != C.Id) { continue; }
					for (const TWeakObjectPtr<AActor>& WA : { E.Fx0, E.Fx1 })
					{
						AAsdaEffectActor* A = Cast<AAsdaEffectActor>(WA.Get());
						if (!A) { continue; }
						C.bHadActor = true;
						C.Seen.AddUnique(A);
						int32 V = 0, Fr = 0; CountVisible(A, V, Fr); Vis += V;
						bool bK = false, bH = false; float CD = 0.f, CT = 0.f; int32 CE = -1;
						A->GetLoopContainerDebug(bK, bH, CD, CE, CT);
						if (bK && WA == E.Fx0) { C.ContDur = CD; C.ContEnd = CE; }   // ожидание рестартов — по эффекту слота fx0 (у timbom fx1 — свой контейнер)
						if (const float* Prev = C.LastTimer.Find(A)) { if (CT + 0.05f < *Prev && WA == E.Fx0) { ++C.Restarts; } }
						if (C.Id == Run->TraceId)
						{
							UE_LOG(LogTemp, Warning, TEXT("[fx-состояния] трасса %s id=%d %s t=%.2f контейнер=%.3f/%.2f режим %d видно %d лайфспан %.2f"),
								C.bMob ? TEXT("моб") : TEXT("сосед"), C.Id, *A->GetName(), Rel, CT, CD, CE, V, A->GetLifeSpan());
						}
						C.LastTimer.Add(A, CT);
					}
				}
				++C.Samples;
				if (Vis > 0) { ++C.VisibleSamples; }
				if (Rel >= Run->ActSec - 0.6f) { C.bVisibleAtEnd |= (Vis > 0); }
			}
		}
		// ── одноразовые: видимость и исчезновение (всё время прогона) ─────────────────────────────────
		if (Run->Phase >= 1)
		{
			for (FOneShot& S : Run->Shots)
			{
				if (S.GoneT >= 0.f || S.SpawnT <= 0.f) { continue; }
				AAsdaEffectActor* A = S.Fx.Get();
				if (!A || A->IsActorBeingDestroyed()) { S.GoneT = T - S.SpawnT; continue; }
				int32 V = 0, Fr = 0; CountVisible(A, V, Fr);
				if (V > 0) { S.LastVisibleT = T - S.SpawnT; }
				if (V > 0 && Fr == V) { if (S.FrozenFromT < 0.f) { S.FrozenFromT = T - S.SpawnT; } }
				else if (V > 0) { S.FrozenFromT = -1.f; }
			}
		}
		// ── 4. снятие — штатными пакетами ────────────────────────────────────────────────────────────
		if (Run->Phase == 1 && T >= Run->ApplyT + Run->ActSec)
		{
			for (const FCase& C : Run->Cases)
			{
				if (!C.bMob)
				{
					TArray<uint8> B; B.SetNumZeroed(4);
					AsdaBytes::WrI16(&B[0], kPeer); AsdaBytes::WrI16(&B[2], C.Id);
					AsdaTestNet::DebugFeedPacket(Net, 5268, B);
				}
			}
			AsdaTestNet::DebugFeedPacket(Net, 4017, Body4017Clear(kMobRid, kMobType));
			Run->Phase = 2;
			return;
		}
		// ── 5. через 0.5 с — акторы слотов уничтожены ────────────────────────────────────────────────
		if (Run->Phase == 2 && T >= Run->ApplyT + Run->ActSec + 0.5f && T < Run->ApplyT + Run->ActSec + 0.5f + kStep)
		{
			for (FCase& C : Run->Cases)
			{
				C.AliveAfterRemove = 0;
				for (const TWeakObjectPtr<AActor>& WA : C.Seen) { if (WA.IsValid() && !WA->IsActorBeingDestroyed()) { ++C.AliveAfterRemove; } }
			}
		}
		// ── 6. итог через «после» ────────────────────────────────────────────────────────────────────
		if (Run->Phase == 2 && T >= Run->ApplyT + Run->ActSec + Run->AfterSec)
		{
			int32 Hanging = 0, HangingVisible = 0;
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				AAsdaEffectActor* A = *It;
				if (!A || A->IsActorBeingDestroyed()) { continue; }
				const FString Nm = (A->EffectName.IsEmpty() ? A->GetName() : A->EffectName).ToLower().Replace(TEXT("-"), TEXT("_"));
				bool bOur = false;
				for (const FString& St : Run->Stems) { if (Nm.Contains(St)) { bOur = true; break; } }
				if (!bOur) { continue; }
				++Hanging;
				int32 V = 0, Fr = 0; CountVisible(A, V, Fr);
				if (V > 0) { ++HangingVisible; }
				UE_LOG(LogTemp, Warning, TEXT("[fx-состояния] ВИСИТ %s (%s) видимых эмиттеров %d"), *Nm, *A->GetName(), V);
			}
			int32 Bad = 0;
			for (const FCase& C : Run->Cases)
			{
				const float VisPct = C.Samples ? 100.f * C.VisibleSamples / C.Samples : 0.f;
				const int32 ExpRestarts = (C.ExpectLoop == 1 && C.ContDur > 0.f) ? FMath::FloorToInt(Run->ActSec / C.ContDur) - 1 : 0;
				const bool bOk = C.bHadActor && C.AliveAfterRemove == 0 && C.ContEnd == C.ExpectLoop
					&& (C.ExpectLoop != 1 || C.Restarts >= ExpRestarts);
				if (!bOk) { ++Bad; }
				UE_LOG(LogTemp, Warning,
					TEXT("[fx-состояния] %s %s id=%d «%s»: актор=%d контейнер %.2f с режим %d (ждали %d) рестартов %d (ждали ≥%d) видно %.0f%% выборок, в конце действия видно=%d, живых через 0.5 с после снятия %d"),
					bOk ? TEXT("OK ") : TEXT("ПЛОХО"), C.bMob ? TEXT("моб") : TEXT("сосед"), C.Id, C.Name,
					C.bHadActor ? 1 : 0, C.ContDur, C.ContEnd, C.ExpectLoop, C.Restarts, ExpRestarts, VisPct,
					C.bVisibleAtEnd ? 1 : 0, C.AliveAfterRemove);
			}
			for (const FOneShot& S : Run->Shots)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[fx-состояния] разовый %s: контейнер %.2f с режим %d, срок жизни %.2f, последний видимый кадр %.2f с, все видимые застыли с %.2f с, актор исчез %.2f с"),
					S.Jeb, S.ContDur, S.ContEnd, S.LifeSpan, S.LastVisibleT, S.FrozenFromT, S.GoneT);
			}
			UE_LOG(LogTemp, Warning,
				TEXT("[fx-состояния] ИТОГ: End0Freeze=%d Loop=%d — состояний %d, плохих %d; висящих эффект-акторов через %.0f с после снятия: %d (видимых %d)"),
				IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fx.End0Freeze"))->GetInt(),
				IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fx.Loop"))->GetInt(),
				Run->Cases.Num(), Bad, Run->AfterSec, Hanging, HangingVisible);
			Run->Phase = 3;
			W->GetTimerManager().ClearTimer(Run->Timer);
			if (Run->bQuit) { AsdaSelfTestBatch::Quit(W); }   // ★ ревизия В2 дедупа №2: конец самотеста для пакета / выход
		}
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxStatusSelfTest(
	TEXT("asda.Fx.StatusSelfTest"),
	TEXT("asda.Fx.StatusSelfTest [сек действия=12] [сек после снятия=30] [id трассы] — эффекты состояний через штатный HandlePacket ")
	TEXT("(5272/5268 на фейкового соседа 777, 4013/4102/4017 на фейкового моба 30777) и одноразовые эффекты: ")
	TEXT("повтор/удержание всё время действия, гашение по снятию, висящие акторы. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaFxStatusTest;
		if (!W) { return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Fx.StatusSelfTest"), KitArgs)) { return; }
		UGameInstance* GI = W->GetGameInstance();
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { return; }
		TSharedRef<FRun> Run = MakeShared<FRun>();
		Run->World = W;
		// ★ Ревизия В2 дедупа №2 (11.10.2026): `quit` — не число, а «выйти по итогу». Пакет `asda.SelfTest.Batch` дописывает
		//   `quit` последним аргументом; прежде он попадал в A[0] (`Atof("quit")` = 0 → действие 4 с вместо 12) и выхода не было —
		//   пакет стоял до потолка. Числа — позиционно среди аргументов БЕЗ `quit`.
		TArray<FString> Num;   // позиционные числовые аргументы
		for (const FString& Arg : A) { if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { Run->bQuit = true; } else { Num.Add(Arg); } }
		Run->Gate.bQuit = Run->bQuit;   // отказ на отложенном старте тоже выходит
		if (Num.Num() > 0) { Run->ActSec = FMath::Max(4.f, FCString::Atof(*Num[0])); }
		if (Num.Num() > 1) { Run->AfterSec = FMath::Max(1.f, FCString::Atof(*Num[1])); }
		if (Num.Num() > 2) { Run->TraceId = FCString::Atoi(*Num[2]); }
		// Состояния: значения `Loop` — из `DT_BuffFx` (сверены со сканом машинного кода, отчёт fx_loop_fixes.md §1).
		auto Add = [&Run](int32 Id, bool bMob, const TCHAR* Nm, int32 Loop)
		{
			FCase C; C.Id = Id; C.bMob = bMob; C.Name = Nm; C.ExpectLoop = Loop; Run->Cases.Add(C);
		};
		Add(3,   false, TEXT("оглушение shocku01"), 1);
		Add(70,  false, TEXT("яд dot301"), 1);
		Add(10,  false, TEXT("молчание silenc01"), 1);
		Add(4,   false, TEXT("паника fearu01"), 1);
		Add(51,  false, TEXT("пронзание briec01"), 1);
		Add(156, false, TEXT("firtim01"), 1);
		Add(178, false, TEXT("timbom01"), 1);
		Add(1,   false, TEXT("заморозка freez201"), 3);
		Add(2,   false, TEXT("заморозка freez301"), 3);
		Add(8,   false, TEXT("сон sleep01"), 3);
		Add(68,  false, TEXT("кровотечение bleedc01"), 3);
		Add(104, false, TEXT("straim01"), 3);
		Add(50,  false, TEXT("defsr101"), 3);
		Add(3,   true,  TEXT("оглушение shocku01"), 1);
		Add(70,  true,  TEXT("яд dot301"), 1);
		Add(8,   true,  TEXT("сон sleep01 (запись 8 поправлена)"), 3);
		Add(9,   true,  TEXT("ловушка freez301 (запись 9 поправлена)"), 3);
		Add(2,   true,  TEXT("заморозка freez301"), 3);
		Add(51,  true,  TEXT("пронзание briec01"), 3);
		Add(28,  true,  TEXT("attsdw01"), 3);
		// Одноразовые: удар, выстрел, стрела моба (стоп до бита), застывающие по End0Freeze до конца контейнера.
		const TCHAR* Shots[] = {
			TEXT("eff_2_char_99_hitdwn01-a_00"), TEXT("eff_1_char_04_bshotb03-a_00"), TEXT("mon_rangearrowdam_ac_00"),
			TEXT("eff_2_char_99_attdow01-a_00"), TEXT("eff_1_char_04_vshotb02-a_00"), TEXT("mon_rangeareadam_ac_00"),
			TEXT("eff_2_char_99_expplt01-a_00"), TEXT("eff_2_char_99_movup01-a_00") };
		for (const TCHAR* S : Shots) { FOneShot O; O.Jeb = S; Run->Shots.Add(O); }
		for (const TCHAR* St : { TEXT("shocku01"), TEXT("dot301"), TEXT("silenc01"), TEXT("fearu01"), TEXT("briec01"),
			TEXT("firtim01"), TEXT("timbom01"), TEXT("freez201"), TEXT("freez301"), TEXT("sleep01"), TEXT("bleedc01"),
			TEXT("straim01"), TEXT("defsr101"), TEXT("attsdw01"), TEXT("hitdwn01"), TEXT("bshotb03"), TEXT("rangearrowdam"),
			TEXT("attdow01"), TEXT("vshotb02"), TEXT("rangeareadam"), TEXT("expplt01"), TEXT("movup01") })
		{
			Run->Stems.Add(St);
		}
		// Сосед — через общую точку снапшота (если его ещё нет).
		if (!WS->GetPlayerActorByUserNum(kPeer))
		{
			FVector2D Base(0.f, 0.f);
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (const AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(PC->GetPawn())) { Base = Me->GetGamePosXZ(); }
			}
			WS->DebugSpawnFakePeer(kPeer, TEXT("FxСосед"), Base + FVector2D(200.f, 0.f));
		}
		W->GetTimerManager().SetTimer(Run->Timer, FTimerDelegate::CreateLambda([Run]() { Step(Run); }), kStep, true);
		UE_LOG(LogTemp, Warning, TEXT("[fx-состояния] старт: действие %.0f с, ожидание после снятия %.0f с"), Run->ActSec, Run->AfterSec);
	}));
#endif // ASDA_DEV_CONSOLE

#endif // !UE_BUILD_SHIPPING
