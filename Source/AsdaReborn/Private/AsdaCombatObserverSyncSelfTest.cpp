// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАБЛЮДАТЕЛЬ: БОЙ СОСЕДА ПО МОБУ КАК У ОРИГИНАЛА» (17.10.2026, синхронизация боя, фаза Ф0).
//   [ПОРТ] Проект `Reports/combat_sync_2026-10-17/03_DESIGN.md` (ред. 2), ревизия `04_REVIEW.md`.
//
// ★ ЗАЧЕМ. Владелец: «чтобы и у игрока, и у других игроков всё отображалось одинаково». Ф0 — срочные дефекты наблюдателя (чужой
//   игрок бьёт моба), ключ `asda.Combat.ObserverSync` (разбор у объявления в `AsdaWorldSubsystem.cpp`). Самотест ставит
//   ФЕЙКОВОГО соседа (`DebugSpawnFakePeer`, тот же путь, что снапшот 4005/4065), даёт ему оружие пакетом 4021 (штатный
//   `OnCharPartUpdate`), мобов — 4013, а бой соседа — пакетами 4029/4017 через `DebugFeedPacket` (= `HandlePacket` сокета).
//   Наблюдаемое: что реально вывел `ShowHitResult` (значение, стиль надписи, авторство) — запись `HitResultsSeen`; поворот
//   меша моба; активность замаха соседа; просьбы строки в журнал чата (`CombatNoticeCalls`).
//   Сценарии (каждый — с ключом 1 и контрольной парой с ключом 0, где дефект обязан быть виден):
//   1) утечка очереди соседа между мобами: хвост моба A (баллиста, 1 удар за замах) не выводится на мобе B; по мёртвому A
//      удары не копятся; новый 4029 вне замаха начинает очередь заново;
//   2) дельта HP от соседа (4029 с пустой очередью, 4017) не рисуется как наша: нет «нашей» цифры и строки в чат, полоса —
//      по серверу;
//   3) стили цифр соседа по мобу 1/2/6/5 (урон, крит, DODGE, MISS), строки в чат нет;
//   4) поворот моба к соседу при флинче;
//   5) отказ 4029 код 1 гасит замах соседа (и сбрасывает очередь); код 2 при живой цели серию не рвёт, при мёртвой — рвёт.
//   Если ключ уже 0 на старте (`--pre "asda.Combat.ObserverSync 0"`) — КОНТРОЛЬНЫЙ ПРОГОН: все бои с ключом 0, проверки
//   ключа 1 проваливаются (дефект виден).
// ★★★ 17.10.2026 (Ф1, лента попаданий моба — `UAsdaWorldSubsystem::ArmImpact`; ревизия Ф0 `06_REVIEW_F0.md`). Добавлено:
//   • сценарии проекта §8 С РАЗНОСОМ ПО РОЛЯМ: атакующий (свой персонаж; замах при «отправке», ответ 4029 через RTT) и
//     наблюдатель (тот же бой ведёт сосед; 4029 через RTT/2 + своё RTT/2) — сравнение момента трупа относительно начала
//     прохода A (у навыка/4102 — от «отправки») по смертельной записи, допуск 1 кадр: два атакующих (смерть от A, пока летит
//     B), навык 5257, запись 4102, добыча и опыт при отложенной смерти, реген во время удержания, старый верхний снимок после
//     2.5 с; «атакующий виден — моба нет» и наоборот; каждый — с контролем ключом отката;
//   • серия соседа по признаку `field11C` (ревизия Ф0 п.1/п.2/п.6): пропуск 4029 у медленного оружия серию не рвёт (сценарий 1),
//     4027 case 1, смена цели посреди серии, код 2 при живой цели со следующим 4029, коды 3–7;
//   • свой номер — `FAsdaNetSelfScope` до `FakeSelfSpawn` (ревизия Ф0 п.12).
// ★★★ 17.10.2026 (Ф2, замах соседа от пакетов — `AAsdaPlayerCharacter::PeerAcceptSwingPacket`; ревизия Ф1 `07_REVIEW_F1.md`).
//   Добавлено:
//   • сценарии Ф2 С РАЗНОСОМ ПО РОЛЯМ для меча, лука и баллисты: серия 4029 «атакующий шлёт 4028 в точках отправки, ответ
//     через RTT 0.2 с», у атакующего (свой персонаж) и у наблюдателя (сосед) — сравнение момента каждой цифры/выстрела
//     относительно начала ОТРЕЗКА (прохода) на своём экране, номер отрезка не сдвигается (допуск 1 кадр); у баллисты в
//     проходах цикла — мировое время выстрела (атакующий стреляет по приходу ответа, наблюдатель — в точке перемотки своего
//     прохода, они совпадают); пропуск 4029 (отрезок без цифр, серия не рвётся), вход посреди пачки, джиттер интервала и
//     темп за пределами 0.8…1.25; полоса за бой > 4.5 с ни разу не поднимается и ленте не нужен потолок (ревизия Ф1 п.1);
//     контроль `asda.Combat.SwingPlanFromPackets 0`;
//   • ревизия Ф1 п.3: фейковые соседи и добыча убираются штатно (4011, 5015) в каждом сбросе и в итоге; свой
//     `SelfWeaponItemId`/`EquippedWeaponSubType` возвращаются;
//   • ревизия Ф1 п.4: два мечника по одной цели дольше 4.5 с, код 2 с непустой очередью, 4102 при идущем показе навыка
//     (выпуск на пороге фазы) и порог фазы 5257 (фактический против расчётного).
//
// Консоль: `asda.Combat.SelfTestObserverSync [lootexp [N]] [delay=N] [quit]` (★ 18.10.2026: `lootexp N` — только сценарий Ф1
//   «добыча 5005» N раз, проверка разброса ролей). Только офлайн. Строки — «САМОТЕСТ-НАБЛЮДАТЕЛЬ», итог —
//   «САМОТЕСТ-НАБЛЮДАТЕЛЬ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "Test/AsdaSelfTestSelfReady.h"   // ★ 01.11.2026: изоляция пакета — свой персонаж заводится и без ссылки мира/номера

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013
#include "World/AsdaLootActor.h"                // добыча на земле (Ф1: появление вместе со смертью)
#include "Skill/AsdaSkillFxSubsystem.h"         // ★ Ф2: наблюдение порога фазы показа навыка
#include "Skill/AsdaSkillClipRow.h"             // ★ Ф2: выбор навыка с показом (строка DT_SkillClips)
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaPlayerController.h"        // свой контроллер (боя не ведёт)
#include "World/AsdaPlayerCharacter.h"         // сосед: замах
#include "World/AsdaWorldSubsystem.h"          // соседи, мобы, наблюдения показа
#include "World/AsdaCoord.h"                   // игра ↔ UE, клетки, курс
#include "Items/AsdaItemTableSubsystem.h"      // оружие соседа
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 17.10.2026 (ревизия Ф2, мелочь 5): своя роль под серверными часами
#include "UI/AsdaInventorySubsystem.h"         // EAsdaEquipSlot — слот оружия в 4021
#include "Npc/AsdaMobActor.h"                  // моб
#include "Components/SkeletalMeshComponent.h"  // курс меша моба
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaObserverTest, Log, All);

namespace AsdaObserverTestLocal
{
	constexpr int32 kMobType = 1;         // тип моба (волк alpen) — как у `asda.Combat.SelfTestHitFrame`
	constexpr int32 kSpdDenom = 250;      // знаменатель скорости (бег сервера)
	constexpr int32 kMobHp = 100;         // ★ Ф1: HP моба из записи 4013 (`AsdaTestPackets::MobSpawn4013`)
	constexpr int32 kPeer = 7702;         // userIdx ПЕРВОГО фейкового соседа (не пересекается с `SelfUserNum` 4321 и соседом ритма 7701)
	constexpr float kQuietS = 1.2f;       // пауза до первого пакета сценария (с): моб заспавнен, цифры прошлых сценариев старше 1 с
	constexpr float kFaceTolDeg = 15.f;   // допуск курса «моб смотрит на соседа» (град.)
	const TCHAR* const kCmd = TEXT("asda.Combat.SelfTestObserverSync");   // имя команды (отказ, итог, перехват)

	// ★ Ф1: событие прогона — пакет в момент «отправка A + At».
	struct FAsdaF1Ev
	{
		double At = 0.0;          // с от «отправки» A
		int32 Op = 0;             // опкод
		TArray<uint8> Body;       // тело
		bool bFed = false;        // подан
	};
	// ★ Ф1: итог одной роли (моменты — относительно опоры сравнения, с; −1 — не было).
	struct FAsdaF1Out
	{
		double DeathLagS = 0.0;   // ★ R5-fix7: длина кадра, в котором применена смерть, сверх 1/60 с (срок «труп на смертельном ударе»)
		double FrameExcessS = 0.0;   // R5-fix7: худший кадр прогона этой роли сверх 1/60 с (допуск сравнения ролей)
		double DeathRel = -1.0, DigitRel = -1.0, HpDropRel = -1.0, LootRel = -1.0, ExpRel = -1.0, DigitBRel = -1.0;   // моменты
		double PassFromTs = -1.0; // начало прохода A от «отправки» (журнал)
		int32 FinalHp = -1;       // итог HP
		bool bDead = false;       // моб умер
		bool bServerDead = false; // метка «обречён» на конце
		double PhaseRel = -1.0;   // ★ Ф2: порог фазы показа навыка A (фактический)
	};
	// ★ Ф2: событие показа удара в бою серии — цифра (ближний) или выстрел (дальнобойное).
	struct FAsdaF2Ev
	{
		int32 Val = 0;            // значение удара (метка)
		double AtS = -1.0;        // мир-время
		double FromTs = -1.0;     // от «отправки» 4026 (мир-с)
		int32 Seg = -1;           // номер отрезка (прохода) на этом экране
		double Rel = -1.0;        // от начала отрезка (с)
		float Rate = 1.f;         // скорость клипа отрезка
		int32 InSeg = 0;          // порядковый номер в отрезке
	};

	struct FStep
	{
		FString Name;                  // имя шага (в строках журнала)
		float TimeoutSec = 20.f;       // потолок шага (с реального времени) — дальше провал «не завершился»
		TFunction<void()> Start;       // начало шага (один раз)
		TFunction<bool()> Poll;        // опрос каждый тик: true — шаг завершён
		TFunction<void()> Check;       // проверки после завершения (или таймаута)
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0;               // текущий шаг
		bool bStepStarted = false;       // `Start` текущего шага выполнен
		double StepStartS = 0.0;         // реальное время начала шага
		FTimerHandle Tick;               // исполнитель шагов
		bool bQuit = false;              // выйти по итогу
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;          // счёт проверок
		uint32 Gen = 0;                  // поколение перехвата исходящих
		int32 NOut = 0;                  // перехвачено исходящих боя (4006/4008/4026/4028) — наш персонаж не воюет, ждём 0
		FVector2D Home = FVector2D::ZeroVector;   // дом своего персонажа (игра)
		FIntPoint HomeTile = FIntPoint::ZeroValue;// клетка дома
		bool bWasInGame = false;         // `bInGameConnected` до прогона
		TUniquePtr<FAsdaNetSelfScope> SelfScope;  // свой userNum на время прогона
		bool bControlAll = false;        // ключ ObserverSync был 0 на старте: контрольный прогон
		int32 Key = 1;                   // ключ ObserverSync текущего сценария
		TArray<int32> Rids;              // мобы прогона (убрать в сбросе)
		TArray<int32> Gone;              // мобы, уже отправленные в AoI-фейд (итог ждёт их ухода из реестра)
		int32 NextRid = 31051;           // следующий rid моба прогона
		// Сосед текущего сценария. Каждый сценарий — НОВЫЙ сосед (новый userIdx): при ключе 0 очередь соседа не сбрасывается
		//   никогда (это и есть дефект), и хвост прошлого сценария испортил бы контроль следующего.
		int32 PeerIdx = kPeer;
		int32 PeerWeaponBallista = 0, PeerWeaponSword = 0;   // предметы оружия соседа
		// Сценарий.
		int32 RidA = -1, RidB = -1;      // мобы сценария
		int32 Phase = 0;                 // фаза сценария
		double T0 = -1.0;                // реальное время первого пакета сценария
		double TPhase = -1.0;            // реальное время начала текущей фазы
		int32 SeenFrom = 0;              // с какой записи `HitResultsSeen` начинается сценарий
		int32 NoticeFrom = 0;            // `CombatNoticeCalls` на начало сценария
		int32 SuppressedFrom = 0;        // `DeltaNumbersSuppressed` на начало сценария
		int32 QAfterDead = -1;           // длина очереди соседа после 4029 по мёртвой цели
		int32 FirstOnB = 0x7FFFFFFF;     // первое значение, показанное на B (0x7FFFFFFF — не было)
		int32 SeenBefore2nd = 0;         // записей на начало второй серии по B
		int32 FirstOnB2 = 0x7FFFFFFF;    // первое значение второй серии по B (после выхода соседа из замаха)
		bool bPeerIdleSeen = false;      // сосед вышел из замаха до второй серии
		bool bSwingBefore = false, bSwingAfter1 = false, bSwingAfter2Alive = false, bSwingAtDeath = false, bSwingAfter2Dead = false;
		int32 QAfter1 = -1;              // длина очереди соседа после отказа код 1
		int32 FinalHp = -1;              // HP моба на конце
		float YawErrDeg = 999.f;         // расхождение курса меша моба с направлением на соседа (град.)
		float YawErrBefore = 999.f;      // то же до удара (моб повёрнут от соседа)
		// ★ Ф1 (лента попаданий, роли).
		int32 NOutF0 = -1;               // исходящих боя за Ф0 (проверяется на входе в Ф1)
		int32 PeerIdxA = 0;              // сосед-атакующий A (роль наблюдателя)
		int32 AIdx = -1, BIdx = -1;      // атакующие A и B прогона (userIdx)
		bool bRoleAttacker = false;      // прогон в роли атакующего (A — свой)
		TArray<FAsdaF1Ev> Evs;           // события прогона (время от «отправки» A)
		TArray<TPair<double, double>> FrameLog;          // ★ R5-fix7: кадры шага: мир-время → длина кадра (граница отрезка Ф2)
		// ★ R5-fix7: длина кадра, в котором мир-время было `T` (записи нет — худший кадр шага).
		double FrameDtAt(double T) const
		{
			for (int32 i = FrameLog.Num() - 1; i >= 0; --i) { if (FMath::Abs(FrameLog[i].Key - T) < 1e-4) { return FrameLog[i].Value; } }
			return MaxDt;
		}
		double EndR = 3.0;               // длительность прогона после «отправки» (с)
		double Ts = -1.0;                // мир-время «отправки» A
		double PassStartA = -1.0;        // мир-время начала прохода A на этом экране
		double TArriveA = -1.0;          // мир-время прихода первого ответа/каста
		double TDigitA = -1.0, TDigitB = -1.0;   // мир-время первой цифры A / B по мобу
		bool bDigitAOurs = false;        // цифра A показана «нашей»
		double THpDrop = -1.0;           // мир-время первого падения полосы
		double TDeathF1 = -1.0;          // мир-время смерти моба (`GetDeathAtS`)
		double TLoot = -1.0, TExp = -1.0;// мир-время появления добычи / показа опыта
		int32 Hp0 = -1;                  // HP моба на «отправке»
		int32 HpMax = -1;                // максимум полосы за прогон (подъём «старым снимком»)
		int32 HpAt3 = -1;                // полоса через 3 с после «отправки»
		bool bFinalServerDead = false;   // метка «обречён» на конце
		FIntPoint LootTile = FIntPoint::ZeroValue;   // клетка добычи
		double MaxDt = 1.0 / 60.0;       // самый длинный кадр прогона (допуск «1 кадр»)
		FDelegateHandle ExpHandle;       // подписка на показ опыта (4038 → `OnSelfExp`)
		TMap<FString, FAsdaF1Out> F1Out; // итоги ролей: «сценарий/роль/контроль»
		// ★ Ф2.
		TArray<int32> SpawnedPeers;      // фейковые соседи прогона (ревизия Ф1 п.3: убрать штатным 4011)
		FIntPoint LastLootTile = FIntPoint(-1, -1);   // клетка добычи прошлого прогона (убрать штатным 5015)
		int32 SavedWeaponItem = 0, SavedEquipped = 0; // свой предмет оружия и подтип до прогона (вернуть в итоге)
		double TPhaseFired = -1.0, TPhasePlanned = -1.0;   // мир-время порога фазы показа навыка A: факт и по данным
		int32 PhaseFrom = 0;             // с какой записи `PhasesFiredSeen` начинается прогон
		int32 PeerWeaponBow = 0, BowWt = 8;   // лук соседа и его подтип
		TMap<int32, TArray<float>> F2NatSegs;  // подтип оружия → естественные длительности отрезков (снято в роли атакующего)
		TMap<FString, TArray<FAsdaF2Ev>> F2Out;// события прогонов Ф2: «подтип/роль/вариант»
		TArray<double> F2SendFromTs;     // «отправки» 4028 атакующего от 4026 (с) — начала отрезков атакующего
		int32 HpPrev = -1, HpRises = 0, HpRiseMax = 0;   // подъёмы полосы за бой (ревизия Ф1 п.1: их быть не должно)
		int32 FlushFrom = 0;             // `LedgerFlushCount` на начало прогона
		int32 ShotFrom = 0;              // с какой записи `ShotsSeen` начинается прогон
		int32 F2LastServerHp = -1;       // HP последнего поданного 4029 (итог полосы)
		bool bF2SwingGap = false;        // сосед выходил из замаха посреди серии (пропуск не должен рвать серию)
		float F2CoefMin = 99.f, F2CoefMax = -1.f;   // коэффициент темпа соседа за прогон
		int32 PeerIdxB2 = 0;             // второй мечник (бой двух мечников)
		int32 RetimeKept0 = 0, RetimeRearmed0 = 0;   // ★ R5-fix8: счётчики перестановки своих кадров удара на начало шага
		int32 SkillVis = 9101;           // навык с показом (строка DT_SkillClips, одна фаза ближнего боя) — порог фазы 5257/4102
		bool bFlagA = false, bFlagB = false;   // ★ ревизия Ф2 (мелочь 5): признаки сценария «навык соседа посреди серии»
		TArray<double> HitFrameTimes;    // ★ ревизия Ф2 (мелочь 5): мир-время пройденных кадров удара своего клипа (роль под часами)

		// Убрать фейкового соседа штатным путём (4011 → `OnEntityDespawn`): реестр соседей, его серия и очередь.
		void DespawnPeer(int32 Idx)
		{
			if (Idx <= 0) { return; }
			if (UAsdaNetSubsystem* Nn = N())
			{
				TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], (int16)Idx);
				Nn->DebugFeedPacket(4011, B);
			}
			SpawnedPeers.Remove(Idx);
		}
		// Убрать всех соседей прогона и добычу прошлого прогона.
		void DespawnAll()
		{
			const TArray<int32> Copy = SpawnedPeers;
			for (const int32 Idx : Copy) { DespawnPeer(Idx); }
			if (LastLootTile.X >= 0)
			{
				if (UAsdaNetSubsystem* Nn = N())
				{
					TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], (int16)LastLootTile.X); AsdaBytes::WrI16(&B[2], (int16)LastLootTile.Y);
					Nn->DebugFeedPacket(5015, B);
				}
				LastLootTile = FIntPoint(-1, -1);
			}
		}
		// Поставить фейкового соседа (тот же путь, что снапшот) и запомнить для уборки.
		void SpawnPeer(int32 Idx, const FString& Name, const FIntPoint& Tile)
		{
			if (UAsdaWorldSubsystem* W = WS()) { W->DebugSpawnFakePeer(Idx, Name, AsdaCoord::TileCenterGame(Tile)); SpawnedPeers.AddUnique(Idx); }
		}

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		// ★ 17.10.2026 (ревизия Ф2, мелочь 7): запись наблюдений (пороги фаз навыка, показ ударов) выключается в ЛЮБОМ исходе —
		//   и при отказе на отложенном старте, и при уходе мира: состояние прогона уничтожается вместе с последним таймером.
		~FState() { StopRecording(); }
		void StopRecording() const
		{
			if (UGameInstance* G = GI())
			{
				if (UAsdaSkillFxSubsystem* Fx = G->GetSubsystem<UAsdaSkillFxSubsystem>()) { Fx->bRecordPhases = false; }
				if (UAsdaWorldSubsystem* W = G->GetSubsystem<UAsdaWorldSubsystem>()) { W->bRecordHitResults = false; }
			}
		}
		// ★ Ревизия Ф2 (мелочь 7): мобы прогона убираются штатным путём мира, а не `Destroy()` мимо реестров.
		// ★ 30.10.2026 (порядок пакета): путь — клиентский AoI-деспаун (`TickAoiDespawn`: `StartFadeOut(Aoi)` → фейд 1 с →
		//   Destroy фейд-компонентом, запись реестра гаснет сама). Прежний 4011 моба НЕ убирал (`OnEntityDespawn`, эталон
		//   `Recv_LogOutPacket` удаляет только игроков): волки прогона (rid 31051+) жили после самотеста, и следующий в пакете
		//   `asda.Mob.SelfTestSkill` спаунил Bishop/Rook на те же rid — 4013 шёл веткой «rid найден» (снап волка без смены типа).
		//   Гаснущих (труп в посмертном фейде) не трогаем — второй фейд не вешается (`StartFadeOut`). Убранные rid копятся в
		//   `Gone` — итог ждёт, пока реестр их отпустит.
		void DespawnMobs()
		{
			for (const int32 Rr : Rids)
			{
				if (AAsdaMobActor* M = Mob(Rr)) { M->StartFadeOut(EAsdaMobFade::Aoi, AAsdaMobActor::AoiFadeSeconds); }
				Gone.AddUnique(Rr);
			}
			Rids.Reset();
		}
		// Сколько убранных мобов прогона ещё в реестре мира (в том числе гаснущих).
		int32 MobsLeft() const { int32 C = 0; for (const int32 Rr : Gone) { if (Mob(Rr)) { ++C; } } return C; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		AAsdaPlayerCharacter* Peer() const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->GetPlayerActorByUserNum(PeerIdx)) : nullptr; }
		bool PeerSwinging() const { const AAsdaPlayerCharacter* P = Peer(); return P && (P->IsAttackAnimActive() || P->IsAttackLoopAlive()); }
		int32 PeerQueue() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->GetDamageQueueLen(PeerIdx) : -1; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		double R() const { return T0 >= 0.0 ? FPlatformTime::Seconds() - T0 : -1.0; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		int32 NewRid() { const int32 Rr = NextRid++; Rids.Add(Rr); return Rr; }
		// Записи наблюдения показа сценария по цели (с начала сценария или с `From`).
		TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> SeenOn(int32 Rid, int32 From = -1) const
		{
			TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> Out;
			const UAsdaWorldSubsystem* W = WS();
			if (!W) { return Out; }
			for (int32 i = FMath::Max(0, From < 0 ? SeenFrom : From); i < W->HitResultsSeen.Num(); ++i)
			{
				if (W->HitResultsSeen[i].Rid == Rid) { Out.Add(W->HitResultsSeen[i]); }
			}
			return Out;
		}
		int32 SeenNum() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->HitResultsSeen.Num() : 0; }
		// ★ Ф1: мир-время, атакующий A (свой или сосед), допуск «1 кадр», событие прогона.
		double WNow() const { const UWorld* W = World.Get(); return W ? W->GetTimeSeconds() : 0.0; }
		AAsdaPlayerCharacter* AChar() const
		{
			if (bRoleAttacker) { return Ch(); }
			const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->GetPlayerActorByUserNum(PeerIdxA)) : nullptr;
		}
		double FrameTol() const { return MaxDt + 0.002; }
		void AddEv(double At, int32 Op, const TArray<uint8>& Body) { FAsdaF1Ev& E = Evs.AddDefaulted_GetRef(); E.At = At; E.Op = Op; E.Body = Body; }
		// Сброс прогона Ф1: ключи, новые соседи A и B, свой персонаж в покое, наблюдения с нуля.
		void F1Reset(int32 KeyObs, int32 KeyHit)
		{
			UAsdaWorldSubsystem* WSs = WS();
			if (AAsdaPlayerCharacter* P = Peer()) { P->StopAttackAnim(); P->AttackFaceTarget = nullptr; }
			if (AAsdaPlayerCharacter* PA = (WSs ? Cast<AAsdaPlayerCharacter>(WSs->GetPlayerActorByUserNum(PeerIdxA)) : nullptr)) { PA->StopAttackAnim(); PA->AttackFaceTarget = nullptr; }
			if (AAsdaPlayerCharacter* C = Ch()) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->NetSnap(Home); }
			DespawnAll();   // ★ Ф2 (ревизия Ф1 п.3): соседи и добыча прошлого прогона убираются штатно
			if (WSs) { WSs->ClearOwnDamageQueuePublic(TEXT("самотест: новый прогон")); }   // своя очередь прошлого прогона не утекает
			++PeerIdx; PeerIdxA = PeerIdx + 1000; PeerIdxB2 = PeerIdx + 2000;
			SpawnPeer(PeerIdx, FString::Printf(TEXT("СоседБ%d"), PeerIdx), HomeTile + FIntPoint(0, -4));
			SpawnPeer(PeerIdxA, FString::Printf(TEXT("СоседА%d"), PeerIdxA), HomeTile + FIntPoint(-1, 1));
			DespawnMobs();   // ★ ревизия Ф2 (мелочь 7): штатный 4011
			Key = bControlAll ? 0 : KeyObs;   // контрольный прогон (`--pre ObserverSync 0`) — все бои с ключом 0
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ObserverSync"), Key);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.HitFrameAllChannels"), KeyHit);
			RidA = RidB = -1; Phase = 0; T0 = TPhase = -1.0;
			SeenFrom = SeenNum();
			Evs.Reset(); EndR = 3.0; Ts = PassStartA = TArriveA = -1.0; TDigitA = TDigitB = THpDrop = TDeathF1 = TLoot = TExp = -1.0;
			bDigitAOurs = false; Hp0 = -1; HpMax = -1; HpAt3 = -1; FinalHp = -1; bFinalServerDead = false; MaxDt = 1.0 / 60.0; FrameLog.Reset();
			QAfterDead = -1; FirstOnB2 = 0x7FFFFFFF; SeenBefore2nd = 0; bSwingBefore = bSwingAfter1 = bSwingAfter2Alive = false; QAfter1 = -1;
			AIdx = BIdx = -1; bRoleAttacker = false;
			TPhaseFired = TPhasePlanned = -1.0;
			if (UGameInstance* G = GI()) { if (const UAsdaSkillFxSubsystem* Fx = G->GetSubsystem<UAsdaSkillFxSubsystem>()) { PhaseFrom = Fx->PhasesFiredSeen.Num(); } }
			F2SendFromTs.Reset(); HpPrev = -1; HpRises = 0; HpRiseMax = 0; F2LastServerHp = -1; bF2SwingGap = false; F2CoefMin = 99.f; F2CoefMax = -1.f;
			FlushFrom = WSs ? WSs->LedgerFlushCount : 0; ShotFrom = WSs ? WSs->ShotsSeen.Num() : 0;
		}
	};

	// Тело 4017 (100 б): @0 тип, @2 rid, @4 флаги (0 — смерть, 3 — живой в бою), @8 28×i16 id (−1), @92 HP, @96/@98 клетка.
	static TArray<uint8> Body4017(int32 Rid, const FIntPoint& Tile, int32 Flags, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], (int16)kMobType);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], Flags);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], Hp);
		AsdaBytes::WrI16(&B[96], (int16)Tile.X); AsdaBytes::WrI16(&B[98], (int16)Tile.Y);
		return B;
	}
	// Тело 4029 (49 б, `Recv_CastState@0xF41620`): @0 i16 атакующий, @2 i16 rid, @4 i32 HP цели, @8 i32[4] очередь (0 — пусто),
	//   @24 sub (0 — удар принят, иначе отказ), @25 i16[4] второй канал (−1), @33 i32[4] (0).
	static TArray<uint8> Body4029(int32 AtkU, int32 Rid, int32 Hp, const TArray<int32>& Q, uint8 Sub = 0)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)AtkU);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], Hp);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], Q[i]); }
		B[24] = Sub;
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}
	// Тело 4021 (9 б, `Recv_CharFullSpawn@0xF3E970`): @0 i16 userIdx, @2 i16 слот (9 — оружие), @4 i32 предмет, @8 u8 заточка.
	static TArray<uint8> Body4021Weapon(int32 UserIdx, int32 ItemId)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(&B[0], (int16)UserIdx);
		AsdaBytes::WrI16(&B[2], (int16)EAsdaEquipSlot::Weapon);
		AsdaBytes::WrI32(&B[4], ItemId);
		return B;
	}
	// ★ Ф1. Тело 4015 (37 б, `Send_SectorInUpdateMonster_NPC_Info`): @4 rid, @6 kind 1 (стоп), @7/@9 cur, @11/@13 dst, @25 HP.
	static TArray<uint8> Body4015(int32 Rid, const FIntPoint& Tile, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], -1);
		AsdaBytes::WrI16(&B[2], (int16)kMobType);
		AsdaBytes::WrI16(&B[4], (int16)Rid);
		B[6] = 1;
		AsdaBytes::WrI16(&B[7], (int16)Tile.X);  AsdaBytes::WrI16(&B[9], (int16)Tile.Y);
		AsdaBytes::WrI16(&B[11], (int16)Tile.X); AsdaBytes::WrI16(&B[13], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[21], -1);
		AsdaBytes::WrI32(&B[25], Hp);
		B[29] = 0xFF;
		AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
	// ★ Ф1. Тело 4027 case 1 (53 б, `Recv_CombatAnimState@0xF3EEB0`): @0 case, @1 атакующий, @3 rid, @5/@7 живая клетка цели,
	//   @9 HP, @13 i32[4] очередь, @29 i16[4] второй канал (−1), @37 i32[4].
	static TArray<uint8> Body4027(int32 AtkU, int32 Rid, const FIntPoint& Tile, int32 Hp, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], (int16)AtkU);
		AsdaBytes::WrI16(&B[3], (int16)Rid);
		AsdaBytes::WrI16(&B[5], (int16)Tile.X); AsdaBytes::WrI16(&B[7], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[9], Hp);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[13 + 4 * i], Q[i]); }
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[29 + 2 * i], -1); }
		return B;
	}
	// ★ Ф1. Тело 5257 (8 + 29 б, `Recv_SkillCast@0xF4E370`): @0 result 1, @1 u16 кастер, @3 u16 навык, @6 u16 уровень; запись:
	//   +0 вид (0 — сущность), +1 rid, +3 урон, +7 флаги (бит0 попадание), +11 HP после, +19 состояние (−1), +24 всплывашка.
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, int32 Rid, int32 Dmg, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], (int16)Caster);
		AsdaBytes::WrI16(&B[3], (int16)Skill);
		AsdaBytes::WrI16(&B[6], 1);
		uint8* R = &B[8];
		R[0] = 0;
		AsdaBytes::WrI16(&R[1], (int16)Rid);
		AsdaBytes::WrI32(&R[3], Dmg);
		R[7] = 0x01;
		AsdaBytes::WrI32(&R[11], HpAfter);
		AsdaBytes::WrI16(&R[19], -1);
		return B;
	}
	// ★ Ф1. Запись 4102 (17 б, `netApplySkillHitResultPacket@0xF4F8A0`): @0 вид 0, @1 rid, @3 id эффекта, @5 урон, @9 HP после,
	//   @13 применять 1, @14 кастер, @16 стек.
	static TArray<uint8> Body4102(int32 Caster, int32 Rid, int32 Dmg, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(17);
		B[0] = 0;
		AsdaBytes::WrI16(&B[1], (int16)Rid);
		AsdaBytes::WrI16(&B[3], 110);   // эффект 110 (сервер `sub_4283F0` case 110 — прок/отражение)
		AsdaBytes::WrI32(&B[5], Dmg);
		AsdaBytes::WrI32(&B[9], HpAfter);
		B[13] = 1;
		AsdaBytes::WrI16(&B[14], (int16)Caster);
		B[16] = 1;
		return B;
	}
	// ★ Ф1. Запись 5005 (15 б): @0 предмет, @4/@6 клетка, @8 кол-во, @12 rid моба-дропнувшего.
	static TArray<uint8> Body5005(int32 Item, const FIntPoint& Tile, int32 DropperRid)
	{
		TArray<uint8> B; B.SetNumZeroed(15);
		AsdaBytes::WrI32(&B[0], Item);
		AsdaBytes::WrI16(&B[4], (int16)Tile.X); AsdaBytes::WrI16(&B[6], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[8], 1);
		AsdaBytes::WrI16(&B[12], (int16)DropperRid);
		return B;
	}
	// ★ Ф1. Тело 4038 (19 б): @0 флаг 0 (обычный опыт), @1 i64 опыт, @9 i64 прибавка, @17 i16 rid убитого.
	static TArray<uint8> Body4038(int32 SrcRid, int64 Gain)
	{
		TArray<uint8> B; B.SetNumZeroed(19);
		const int64 Exp = 1000;
		FMemory::Memcpy(&B[1], &Exp, 8);
		FMemory::Memcpy(&B[9], &Gain, 8);
		AsdaBytes::WrI16(&B[17], (int16)SrcRid);
		return B;
	}
	// Первое оружие подтипа (тип 9, не удочка) с дальностью. 0 — нет.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* Rr = IT->GetItem(Code);
			if (Rr && Rr->ItemType == 9 && Rr->ItemSubType == Wt && Rr->ItemVariant != 1 && Rr->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}
	// Расхождение курса меша моба с направлением на актор (град., 0..180).
	static float FaceErrDeg(const AAsdaMobActor* M, const AActor* To)
	{
		if (!M || !M->Mesh || !To) { return 999.f; }
		const FVector2D Dir = AsdaCoord::UeToGame2D(To->GetActorLocation()) - AsdaCoord::UeToGame2D(M->GetActorLocation());
		const float Want = AsdaCoord::FaceYawFromGameDir(Dir) + M->MeshYawOffset;
		// ★ 22.10.2026 (плавность): ЛОГИЧЕСКИЙ курс моба — видимый меш доворачивается к нему со скоростью
		//   `asda.Anim.MobTurnDegPerSec` ([ОРИГИНАЛ] 360 °/с), и через 0.1 с после флинча ещё не доехал бы.
		return FMath::Abs(FMath::FindDeltaAngleDegrees(M->GetFacingYawLogical(), Want));
	}
	static const TCHAR* Hex(int32 V) { static FString S; S = FString::Printf(TEXT("0x%X"), V); return *S; }
}

struct FAsdaCombatObserverSyncSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaCombatObserverSyncSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaObserverTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaObserverTest, Warning, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);   // ★ ревизия Ф0 п.12: до `FakeSelfSpawn`
	if (const IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Combat.ObserverSync")))
	{
		S->bControlAll = Cv->GetInt() == 0;
	}

	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(Op, B); } };
	auto T = [S](int32 Dx, int32 Dz) { return S->HomeTile + FIntPoint(Dx, Dz); };
	// Сброс сценария: мобы прошлого убираются, сосед — в покой, ключ — на сценарий, отсчёты наблюдений — с текущего места.
	auto Reset = [S](int32 Key)
	{
		UAsdaWorldSubsystem* WSs = S->WS();
		if (AAsdaPlayerCharacter* P = S->Peer()) { P->StopAttackAnim(); P->AttackFaceTarget = nullptr; }
		// Новый сосед на месте прежнего (штатный путь снапшота `SpawnRemotePlayer`, синхронный) — очередь у него пустая.
		// ★ Ф2 (ревизия Ф1 п.3): прежний сосед убирается штатным 4011 — десятки соседей в мире грузили следующий самотест пакета.
		S->DespawnAll();
		++S->PeerIdx;
		S->SpawnPeer(S->PeerIdx, FString::Printf(TEXT("СоседБой%d"), S->PeerIdx), S->HomeTile + FIntPoint(0, -4));
		S->DespawnMobs();   // ★ ревизия Ф2 (мелочь 7): штатный 4011
		S->Key = S->bControlAll ? 0 : Key;
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ObserverSync"), S->Key);
		S->RidA = S->RidB = -1; S->Phase = 0; S->T0 = S->TPhase = -1.0;
		S->SeenFrom = S->SeenNum();
		S->NoticeFrom = WSs ? WSs->CombatNoticeCalls : 0;
		S->SuppressedFrom = WSs ? WSs->DeltaNumbersSuppressed : 0;
		S->QAfterDead = -1; S->FirstOnB = S->FirstOnB2 = 0x7FFFFFFF; S->SeenBefore2nd = 0; S->bPeerIdleSeen = false;
		S->bSwingBefore = S->bSwingAfter1 = S->bSwingAfter2Alive = S->bSwingAtDeath = S->bSwingAfter2Dead = false;
		S->QAfter1 = -1; S->FinalHp = -1; S->YawErrDeg = S->YawErrBefore = 999.f;
	};
	// Оружие соседа — штатным пакетом 4021 (`OnCharPartUpdate`).
	auto Arm = [S, Feed](int32 ItemId) { Feed(4021, Body4021Weapon(S->PeerIdx, ItemId)); };

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 3.f, [S, Net, Feed]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC(); UAsdaWorldSubsystem* WSs = S->WS();
		if (!C || !PC || !WSs) { S->Expect(false, TEXT("нет своего персонажа/контроллера/подсистемы мира после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		C->StopAttackAnim();
		PC->bAutoAttack = false; PC->bCombatStarted = false; PC->TargetActor = nullptr;   // наш персонаж НЕ воюет: он наблюдатель
		// ★ Ревизия Ф0 п.12: `FAsdaNetSelfScope` взят ДО `FakeSelfSpawn` (в `Run`) — свой номер вернётся к состоянию до прогона.
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (St && (Op == 4006 || Op == 4008 || Op == 4026 || Op == 4028)) { ++St->NOut; }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		// ★ Ф1: момент показа опыта (4038 → `OnSelfExp`) по мобу прогона.
		S->ExpHandle = Net->OnSelfExp.AddLambda([WSp](int32, int64, int64, bool, int32 Src)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (St && Src == St->RidA && St->TExp < 0.0) { St->TExp = St->WNow(); }
		});
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.HpSnapshotOnHitFrame"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.HitFrameAllChannels"), 1);
		WSs->bRecordHitResults = true;
		WSs->HitResultsSeen.Reset();
		const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		S->PeerWeaponBallista = FindWeapon(IT, 11);
		S->PeerWeaponSword = FindWeapon(IT, 1);
		S->Expect(S->PeerWeaponBallista > 0 && S->PeerWeaponSword > 0,
			FString::Printf(TEXT("оружие соседа: баллиста %d, одноручный меч %d"), S->PeerWeaponBallista, S->PeerWeaponSword));
		S->SpawnPeer(S->PeerIdx, TEXT("СоседБой"), S->HomeTile + FIntPoint(0, -4));
		// ★ Ф2: свой предмет оружия и подтип вернутся в итоге (роли атакующего их меняют); лук соседа; запись порогов фаз навыка.
		S->SavedWeaponItem = Net->SelfWeaponItemId; S->SavedEquipped = C->EquippedWeaponSubType;
		S->PeerWeaponBow = FindWeapon(IT, 8); S->BowWt = 8;
		if (S->PeerWeaponBow <= 0) { S->PeerWeaponBow = FindWeapon(IT, 10); S->BowWt = 10; }
		S->Expect(S->PeerWeaponBow > 0, FString::Printf(TEXT("Ф2: лук (подтип %d): предмет %d"), S->BowWt, S->PeerWeaponBow));
		if (UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr) { Fx->bRecordPhases = true; Fx->PhasesFiredSeen.Reset(); }
		UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ дом: клетка (%d,%d), сосед %d%s"), S->HomeTile.X, S->HomeTile.Y, S->PeerIdx,
			S->bControlAll ? TEXT(" | КОНТРОЛЬНЫЙ ПРОГОН: ключ asda.Combat.ObserverSync = 0 на старте — все бои с ключом 0, провалы ожидаемы (дефект виден)") : TEXT(""));
	}, [S]() { return S->El() > 0.5 && S->Peer() != nullptr; }, [S]()
	{
		S->Expect(S->Peer() != nullptr, TEXT("фейковый сосед стоит в мире (реестр соседей)"));
	});

	for (const int32 K : { 1, 0 })
	{
		const bool bFix = K != 0;   // проверки исправления (ключ 1) или контроля (ключ 0: дефект виден)
		// ─────────────── 1. Утечка очереди соседа между мобами ───────────────
		Add(FString::Printf(TEXT("1 утечка очереди соседа (баллиста), ObserverSync %d"), K), 28.f, [S, Reset, Feed, T, Arm, K]()
		{
			Reset(K);
			Arm(S->PeerWeaponBallista);
			S->RidA = S->NewRid(); S->RidB = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(3, -2), /*State*/1, kSpdDenom));
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidB, kMobType, T(-3, -2), /*State*/1, kSpdDenom));
		},
		[S, Feed, T]()
		{
			const double Now = FPlatformTime::Seconds();
			if (S->Phase == 0 && S->El() > kQuietS && S->Mob(S->RidA) && S->Mob(S->RidB))
			{
				S->T0 = Now; S->Phase = 1;
				Feed(4029, Body4029(S->PeerIdx, S->RidA, 70, { 10, 11, 12, 13 }));   // 1 удар за замах — хвост [11 12 13]
			}
			if (S->Phase == 1 && S->R() >= 1.0) { S->Phase = 2; Feed(4017, Body4017(S->RidA, T(3, -2), 0, 0)); }   // смерть A
			// ★ Ф1: серверная смерть A держится лентой до попадания снаряда соседа (≤ 0.5 с без смертельной записи) — ответ «по
			//   мёртвой» подаётся, когда A действительно лёг на этом экране.
			const AAsdaMobActor* MA = S->Mob(S->RidA);
			if (S->Phase == 2 && ((MA && MA->IsDead()) || S->R() >= 3.0))
			{
				S->Phase = 3; S->TPhase = Now;
				Feed(4029, Body4029(S->PeerIdx, S->RidA, 0, { 90, 91, 92, 93 }));   // ответ по уже мёртвой A (эталон: state 10 → выход)
				S->QAfterDead = S->PeerQueue();
			}
			if (S->Phase == 3 && Now - S->TPhase >= 0.2) { S->Phase = 4; Feed(4029, Body4029(S->PeerIdx, S->RidB, 50, { 50, 51, 52, 53 })); }
			if (S->Phase == 4)
			{
				const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> OnB = S->SeenOn(S->RidB);
				if (OnB.Num() > 0 && S->FirstOnB == 0x7FFFFFFF) { S->FirstOnB = OnB[0].Raw; }
				if (S->FirstOnB != 0x7FFFFFFF && !S->PeerSwinging())
				{
					// ★ Ф1 (ревизия Ф0 п.1): анимация замаха соседа угасла (сервер молча отбросил ранний 4028 — 4029 пропущен), а серия
					//   у эталона идёт (признак 0x40 без таймаута) — следующий 4029 ДОПИСЫВАЕТ очередь: первым выйдет хвост 51.
					S->bPeerIdleSeen = true; S->Phase = 5; S->SeenBefore2nd = S->SeenNum();
					Feed(4029, Body4029(S->PeerIdx, S->RidB, 40, { 40, 41, 42, 43 }));
				}
				if (S->R() > 16.0) { return true; }
			}
			if (S->Phase == 5)
			{
				const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> OnB2 = S->SeenOn(S->RidB, S->SeenBefore2nd);
				if (OnB2.Num() > 0) { S->FirstOnB2 = OnB2[0].Raw; return true; }
				if (S->R() > 20.0) { return true; }
			}
			return false;
		},
		[S, bFix]()
		{
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ замер: очередь после 4029 по мёртвой A %d | первая цифра на B %d | сосед вышел из замаха %s | первая цифра второй серии по B %d"),
				S->QAfterDead, S->FirstOnB, S->bPeerIdleSeen ? TEXT("да") : TEXT("нет"), S->FirstOnB2);
			if (bFix)
			{
				S->Expect(S->QAfterDead == 0, FString::Printf(TEXT("по мёртвой цели удары не копятся: очередь соседа %d (ждём 0)"), S->QAfterDead));
				S->Expect(S->FirstOnB == 50, FString::Printf(TEXT("на новом мобе B первая цифра — его удар 50, а не хвост A (%d)"), S->FirstOnB));
				S->Expect(S->bPeerIdleSeen && S->FirstOnB2 == 51,
					FString::Printf(TEXT("пропуск 4029 у медленного оружия серию не рвёт (эталон 0x40): первым вышел хвост 51 (%d)"), S->FirstOnB2));
			}
			else
			{
				// Хвост A — любое значение серий по A (11…13 или 90…93 по мёртвой): какое именно — зависит от расхода по ходу.
				const bool bTailOfA = (S->FirstOnB >= 11 && S->FirstOnB <= 13) || (S->FirstOnB >= 90 && S->FirstOnB <= 93);
				S->Expect(bTailOfA, FString::Printf(TEXT("контроль, ObserverSync 0: на мобе B вышел хвост моба A (%d вместо 50) — дефект виден"), S->FirstOnB));
				S->Expect(S->QAfterDead > 3, FString::Printf(TEXT("контроль: по мёртвой цели удары копятся (очередь %d) — дефект виден"), S->QAfterDead));
			}
		});

		// ─────────────── 2. Р2: дельта HP соседа — не «наша» цифра ───────────────
		Add(FString::Printf(TEXT("2 дельта HP от соседа без авторства, ObserverSync %d"), K), 12.f, [S, Reset, Feed, T, Arm, K]()
		{
			Reset(K);
			Arm(S->PeerWeaponSword);
			S->RidA = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, 2), /*State*/1, kSpdDenom));
		},
		[S, Feed, T]()
		{
			const double Now = FPlatformTime::Seconds();
			if (S->Phase == 0 && S->El() > kQuietS && S->Mob(S->RidA))
			{
				S->T0 = Now; S->Phase = 1;
				Feed(4029, Body4029(S->PeerIdx, S->RidA, 70, {}));   // ответ на замах соседа без ударов (снимок HP 100 → 70)
			}
			if (S->Phase == 1 && S->R() >= 1.5) { S->Phase = 2; Feed(4017, Body4017(S->RidA, T(2, 2), 3, 40)); }   // HP моба 40
			if (S->Phase == 2 && S->R() >= 3.0)
			{
				if (AAsdaMobActor* M = S->Mob(S->RidA)) { S->FinalHp = M->CurHp; }
				return true;
			}
			return false;
		},
		[S, bFix]()
		{
			const UAsdaWorldSubsystem* WSs = S->WS();
			const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> On = S->SeenOn(S->RidA);
			int32 Ours = 0; for (const auto& E : On) { if (E.bOurs) { ++Ours; } }
			const int32 Notices = WSs ? WSs->CombatNoticeCalls - S->NoticeFrom : -1;
			const int32 Supp = WSs ? WSs->DeltaNumbersSuppressed - S->SuppressedFrom : -1;
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ замер: показов на мобе %d (из них «наших» %d), строк в журнал %d, дельт без цифры %d, итог HP %d"),
				On.Num(), Ours, Notices, Supp, S->FinalHp);
			if (bFix)
			{
				S->Expect(Ours == 0 && On.Num() == 0, FString::Printf(TEXT("дельта HP соседа не рисуется: показов %d, «наших» %d (ждём 0)"), On.Num(), Ours));
				S->Expect(Notices == 0, FString::Printf(TEXT("строки «вы нанесли» в чат нет (%d)"), Notices));
				S->Expect(Supp >= 1, FString::Printf(TEXT("дельта 4017 без нашего замаха — только полоса (%d подавлено)"), Supp));
			}
			else
			{
				S->Expect(Ours >= 1, FString::Printf(TEXT("контроль, ObserverSync 0: чужая дельта нарисована «нашей» цифрой (%d) — дефект виден"), Ours));
			}
			S->Expect(S->FinalHp == 40, FString::Printf(TEXT("полоса — по серверу: HP 40 (%d)"), S->FinalHp));
		});

		// ─────────────── 3. Стили цифр соседа по мобу ───────────────
		Add(FString::Printf(TEXT("3 стили цифр соседа по мобу, ObserverSync %d"), K), 14.f, [S, Reset, Feed, T, Arm, K]()
		{
			Reset(K);
			Arm(S->PeerWeaponSword);
			S->RidA = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(1, -3), /*State*/1, kSpdDenom));
		},
		[S, Feed]()
		{
			const double Now = FPlatformTime::Seconds();
			// По одному значению на 4029 (сосед в серии — дописывает): урон 20, крит 15, DODGE (−1), MISS (−6).
			static const int32 kVals[] = { 20, (int32)0x8000000F, -1, -6 };
			static const int32 kHp[] = { 80, 65, 65, 65 };
			if (S->Phase < 4 && S->El() > kQuietS + 0.4f * S->Phase && S->Mob(S->RidA))
			{
				if (S->Phase == 0) { S->T0 = Now; }
				Feed(4029, Body4029(S->PeerIdx, S->RidA, kHp[S->Phase], { kVals[S->Phase] }));
				++S->Phase;
			}
			if (S->Phase == 4 && S->SeenOn(S->RidA).Num() >= 4) { return true; }
			return S->Phase == 4 && S->R() > 8.0;
		},
		[S, bFix]()
		{
			const UAsdaWorldSubsystem* WSs = S->WS();
			const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> On = S->SeenOn(S->RidA);
			const int32 Notices = WSs ? WSs->CombatNoticeCalls - S->NoticeFrom : -1;
			FString Got; for (const auto& E : On) { Got += FString::Printf(TEXT(" %s→%d%s"), Hex(E.Raw), E.Style, E.bOurs ? TEXT("(наш)") : TEXT("")); }
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ замер: показано %d:%s | строк в журнал %d"), On.Num(), *Got, Notices);
			auto StyleOf = [&On](int32 Raw) { for (const auto& E : On) { if (E.Raw == Raw) { return E.Style; } } return -1; };
			const int32 SDmg = StyleOf(20), SCrit = StyleOf((int32)0x8000000F), SDodge = StyleOf(-1), SMiss = StyleOf(-6);
			S->Expect(On.Num() == 4, FString::Printf(TEXT("показаны все 4 результата соседа (%d)"), On.Num()));
			if (bFix)
			{
				S->Expect(SDmg == 1 && SCrit == 2 && SDodge == 6 && SMiss == 5,
					FString::Printf(TEXT("стили соседа по мобу как у оригинала: урон 1 (%d), крит 2 (%d), DODGE 6 (%d), MISS 5 (%d)"), SDmg, SCrit, SDodge, SMiss));
			}
			else
			{
				S->Expect(SDmg == 9 && SCrit == 0xA && SDodge == 0xE && SMiss == 0xD,
					FString::Printf(TEXT("контроль, ObserverSync 0: красные 9/0xA/0xE/0xD (%d/%d/%d/%d) — расхождение с оригиналом видно"), SDmg, SCrit, SDodge, SMiss));
			}
			bool bAnyOurs = false; for (const auto& E : On) { bAnyOurs |= E.bOurs; }
			S->Expect(!bAnyOurs && Notices == 0, FString::Printf(TEXT("удары соседа — не «наши», строки в чат нет (%d)"), Notices));
		});

		// ─────────────── 4. Поворот моба к соседу (Р8) ───────────────
		Add(FString::Printf(TEXT("4 поворот моба к соседу, ObserverSync %d"), K), 10.f, [S, Reset, Feed, T, Arm, K]()
		{
			Reset(K);
			Arm(S->PeerWeaponSword);
			S->RidA = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(-2, 1), /*State*/1, kSpdDenom));
		},
		[S, Feed]()
		{
			AAsdaMobActor* M = S->Mob(S->RidA); AAsdaPlayerCharacter* P = S->Peer();
			if (S->Phase == 0 && S->El() > kQuietS && M && P)
			{
				// Моб отвёрнут от соседа — без поворота курс останется «от соседа».
				//   (`FaceGameDir` моба закрыт — курс ставим тем же правилом: курс из игрового направления + доворот модели.)
				const FVector2D Away = AsdaCoord::UeToGame2D(M->GetActorLocation()) - AsdaCoord::UeToGame2D(P->GetActorLocation());
				M->SnapFaceGameDir(Away);   // ★ 22.10.2026: логический и видимый курс сразу (тем же правилом)
				S->YawErrBefore = FaceErrDeg(M, P);
				S->T0 = FPlatformTime::Seconds(); S->Phase = 1;
				Feed(4029, Body4029(S->PeerIdx, S->RidA, 80, { 20 }));
			}
			if (S->Phase == 1 && S->SeenOn(S->RidA).Num() > 0) { S->Phase = 2; S->TPhase = FPlatformTime::Seconds(); }
			if (S->Phase == 2 && FPlatformTime::Seconds() - S->TPhase > 0.1) { S->YawErrDeg = FaceErrDeg(M, P); return true; }
			return S->Phase >= 1 && S->R() > 6.0;
		},
		[S, bFix]()
		{
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ замер: курс моба до удара %.1f° от соседа, после %.1f°"), S->YawErrBefore, S->YawErrDeg);
			S->Expect(S->YawErrBefore > 90.f, FString::Printf(TEXT("до удара моб отвёрнут от соседа (%.1f°)"), S->YawErrBefore));
			if (bFix)
			{
				S->Expect(S->YawErrDeg <= kFaceTolDeg, FString::Printf(TEXT("флинч от удара соседа развернул моба к соседу (%.1f° ≤ %.0f°)"), S->YawErrDeg, kFaceTolDeg));
			}
			else
			{
				S->Expect(S->YawErrDeg > 90.f, FString::Printf(TEXT("контроль, ObserverSync 0: моб не повернулся к соседу (%.1f°) — дефект виден"), S->YawErrDeg));
			}
		});

		// ─────────────── 5. Отказ 4029 гасит замах соседа ───────────────
		Add(FString::Printf(TEXT("5 отказ 4029 гасит замах соседа, ObserverSync %d"), K), 10.f, [S, Reset, Feed, T, Arm, K]()
		{
			Reset(K);
			Arm(S->PeerWeaponSword);
			S->RidA = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, -1), /*State*/1, kSpdDenom));
		},
		[S, Feed, T]()
		{
			if (S->Phase == 0 && S->El() > kQuietS && S->Mob(S->RidA))
			{
				S->T0 = FPlatformTime::Seconds(); S->Phase = 1;
				Feed(4029, Body4029(S->PeerIdx, S->RidA, 90, { 10, 11 }));
			}
			if (S->Phase == 1 && S->R() >= 0.3)
			{
				S->Phase = 2; S->bSwingBefore = S->PeerSwinging();
				Feed(4029, Body4029(S->PeerIdx, S->RidA, 90, {}, /*Sub=*/1));   // код 1 — «цель вне досягаемости», в сектор
			}
			if (S->Phase == 2 && S->R() >= 0.4) { S->Phase = 3; S->bSwingAfter1 = S->PeerSwinging(); S->QAfter1 = S->PeerQueue(); }
			if (S->Phase == 3 && S->R() >= 0.8) { S->Phase = 4; Feed(4029, Body4029(S->PeerIdx, S->RidA, 80, { 10 })); }   // новая серия
			if (S->Phase == 4 && S->R() >= 1.1) { S->Phase = 5; Feed(4029, Body4029(S->PeerIdx, S->RidA, 80, {}, /*Sub=*/2)); }   // код 2, цель жива
			if (S->Phase == 5 && S->R() >= 1.2) { S->Phase = 6; S->bSwingAfter2Alive = S->PeerSwinging(); }
			if (S->Phase == 6 && S->R() >= 1.5) { S->Phase = 7; Feed(4017, Body4017(S->RidA, T(2, -1), 0, 0)); }   // смерть цели
			if (S->Phase == 7 && S->R() >= 1.6)
			{
				S->Phase = 8; S->bSwingAtDeath = S->PeerSwinging();
				Feed(4029, Body4029(S->PeerIdx, S->RidA, 0, {}, /*Sub=*/2));   // код 2, HP цели ≤ 0
			}
			if (S->Phase == 8 && S->R() >= 1.7) { S->bSwingAfter2Dead = S->PeerSwinging(); return true; }
			return false;
		},
		[S, bFix]()
		{
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ замер: замах до отказа %s, после кода 1 %s (очередь %d), после кода 2 при живой цели %s, к смерти цели %s, после кода 2 по мёртвой %s"),
				S->bSwingBefore ? TEXT("да") : TEXT("нет"), S->bSwingAfter1 ? TEXT("да") : TEXT("нет"), S->QAfter1,
				S->bSwingAfter2Alive ? TEXT("да") : TEXT("нет"), S->bSwingAtDeath ? TEXT("да") : TEXT("нет"), S->bSwingAfter2Dead ? TEXT("да") : TEXT("нет"));
			S->Expect(S->bSwingBefore, TEXT("до отказа сосед в замахе (4029 запустил его)"));
			if (bFix)
			{
				S->Expect(!S->bSwingAfter1 && S->QAfter1 == 0, FString::Printf(TEXT("отказ 4029 код 1 погасил замах соседа и сбросил очередь (%d)"), S->QAfter1));
				S->Expect(S->bSwingAfter2Alive, TEXT("код 2 при живой цели серию не рвёт (эталон network.c:42104)"));
				S->Expect(!S->bSwingAfter2Dead, FString::Printf(TEXT("код 2 при HP цели ≤ 0 гасит замах (к смерти цели замах %s)"),
					S->bSwingAtDeath ? TEXT("ещё шёл") : TEXT("уже погас — проверка слабая")));
			}
			else
			{
				S->Expect(S->bSwingAfter1, TEXT("контроль, ObserverSync 0: после отказа код 1 замах соседа доигрывает — дефект виден"));
			}
		});
	}


	// ═══════════════════════ ★★★ Ф1 — ЛЕНТА ПОПАДАНИЙ МОБА: ОДИНАКОВО У АТАКУЮЩЕГО И У НАБЛЮДАТЕЛЯ ═══════════════════════
	//   Проект §8: один сценарий пакетов дважды — роль АТАКУЮЩЕГО (свой персонаж; замах стартует «при отправке», ответ 4029
	//   приходит через RTT) и роль НАБЛЮДАТЕЛЯ (тот же бой ведёт сосед; его 4029 приходит через RTT/2 + своё RTT/2 = тот же миг,
	//   замах соседа стартует по приходу). Сравнение — относительно начала ПРОХОДА атакующего A у каждой роли по смертельной
	//   записи, допуск 1 кадр. Серверные 4015/4017/5005/4038 у обеих ролей приходят в один и тот же миг (рассылка одновременная).
	//   [ПОРТ] Свой замах в роли атакующего ставится тем же путём, что у контроллера при 4026 (`StartAttackSwing` +
	//   `ArmOurImpactWindowFromSend`) — без отправки пакета; серверные часы ритма (`asda.Combat.ServerRhythm`) на время Ф1
	//   выключены: темп своего прохода под серверными часами — предмет Ф2, здесь проверяется лента.
	//   Контроль каждого сценария — с ключом отката (`asda.Combat.ObserverSync 0` у роли наблюдателя, а у «старого верхнего
	//   снимка» — `asda.Combat.HitFrameAllChannels 0`): дефект обязан быть виден.
	Add(TEXT("Ф1 подготовка"), 3.f, [S]()
	{
		S->NOutF0 = S->NOut;   // исходящие Ф0 — отдельно (в Ф1 свой персонаж атакует, но пакетов не шлёт)
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 0);
		// ★ Ф2 (ревизия Ф1 п.4): навык с НАСТОЯЩИМ показом для сценариев порога фазы — строка `DT_SkillClips` для обоих полов и
		//   меча, клип импортирован, одна фаза ближнего боя (без снаряда), COMBO0 300…1500 мс. 9101 показа не имеет (выпуск по
		//   сторожу полки, а не на пороге фазы).
		if (const UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr)
		{
			for (int32 Id = 1; Id < 6000; ++Id)
			{
				const FAsdaSkillClipRow* R1 = Fx->ResolveRow(Id, 1, 1);
				const FAsdaSkillClipRow* R2 = Fx->ResolveRow(Id, 2, 1);
				auto Ok = [](const FAsdaSkillClipRow* R)
				{
					return R && R->AnimIdx >= 0 && R->Combo0Ms >= 300.f && R->Combo0Ms <= 1500.f && R->Combo1Ms <= 0.f && R->Proj == 0
						&& R->ProjDelay == 0 && (!R->FxHit0.IsEmpty() || !R->SndHit.IsEmpty()) && !R->FxHit0.Contains(TEXT("projectile"));
				};
				if (Ok(R1) && Ok(R2)) { S->SkillVis = Id; break; }
			}
		}
		UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ навык с показом для порога фазы: %d"), S->SkillVis);
		S->TPhase = FPlatformTime::Seconds();
	}, [S]() { return FPlatformTime::Seconds() - S->TPhase > 0.2; }, [S]()
	{
		S->Expect(S->NOutF0 == 0, FString::Printf(TEXT("Ф0: наш персонаж боевых пакетов не слал (%d) — он только наблюдатель"), S->NOutF0));
	});

	// Сценарии Ф1. Каждый: события (время от «отправки» A, мир-с), длительность, проверки.
	// ★ Ревизия 18.10 (плавный бой): LethalNoHits — двойной старт серии A + смертельный ответ без ударов (смерть на ударе, доводящем
	//   полосу до 0, одинаково у ролей); PeerCutLethal — серия соседа A прервана отказом, затем смертельный ответ без ударов (удары
	//   прерванной серии не выбираются смертельными — смерть на кадре нового отрезка).
	//   ★ 18.10.2026 (пол HP, `asda.Combat.HpFloorFromServer`): Live161 — живая последовательность rid=161 (журнал
	//   `AsdaCombat_20261018-205020_45284.log`): у обеих ролей полоса за весь бой не растёт, смерть на смертельном ударе.
	enum class EF1 : uint8 { TwoAttackers, Skill5257, Kill4102, LootExp, RegenHold, StaleTop, Skill4102Shown, LethalNoHits, PeerCutLethal, Live161, Skill4102SameFrame };
	auto F1Name = [](EF1 Sc) -> const TCHAR*
	{
		switch (Sc)
		{
		case EF1::TwoAttackers: return TEXT("два атакующих: смерть от A (меч), пока летит снаряд B (баллиста)");
		case EF1::Skill5257:    return TEXT("убийство навыком 5257 (+11 = 0), 4017 flags=0 раньше выпуска записи");
		case EF1::Kill4102:     return TEXT("убийство записью 4102 «убит» при идущем замахе A");
		case EF1::LootExp:      return TEXT("добыча 5005 и опыт 4038 при отложенной смерти");
		case EF1::RegenHold:    return TEXT("реген 4017 flags=4 во время удержания смерти");
		case EF1::StaleTop:     return TEXT("старый верхний снимок 4017 через 3.0 с не поднимает полосу");
		case EF1::Skill4102Shown: return TEXT("4102 «убит» при идущем показе навыка 5257 — выпуск на пороге фазы");
		case EF1::LethalNoHits: return TEXT("серия A (два ответа по 4 удара) + смертельный ответ без ударов: смерть на ударе, доводящем полосу до 0");
		case EF1::PeerCutLethal: return TEXT("серия соседа A прервана отказом, затем смертельный ответ без ударов");
		case EF1::Live161: return TEXT("живой бой rid=161 (4027 + 4029 с регеном 4017): полоса за весь бой не растёт");
		case EF1::Skill4102SameFrame: return TEXT("5257 и 4102 «убит» ОДНИМ проходом сети — запись ждёт показ, выпуск на пороге фазы");
		}
		return TEXT("?");
	};
	// Роль: 0 — атакующий (A = свой), 1 — наблюдатель (A = сосед). KeyObs/KeyHit — ключи прогона (контроль — 0).
	// ★ R5-fix8: KeyQ — ключ `asda.Skill.Hit4102WaitsQueuedCast` прогона (0 — контроль сценария «5257 и 4102 одним кадром»).
	auto AddF1 = [S, Add, Feed, T, F1Name](EF1 Sc, int32 Role, int32 KeyObs, int32 KeyHit, int32 KeyQ = 1)
	{
		const bool bControl = KeyObs == 0 || KeyHit == 0 || KeyQ == 0;
		Add(FString::Printf(TEXT("Ф1 %s — роль %s%s"), F1Name(Sc), Role == 0 ? TEXT("АТАКУЮЩЕГО") : TEXT("НАБЛЮДАТЕЛЯ"),
			bControl ? (KeyQ == 0 ? TEXT(", КОНТРОЛЬ Hit4102WaitsQueuedCast 0") : KeyObs == 0 ? TEXT(", КОНТРОЛЬ ObserverSync 0") : TEXT(", КОНТРОЛЬ HitFrameAllChannels 0")) : TEXT("")), 14.f,
		[S, Feed, T, Sc, Role, KeyObs, KeyHit, KeyQ]()
		{
			S->F1Reset(KeyObs, KeyHit);
			AsdaCVarSave::SetForTest(TEXT("asda.Skill.Hit4102WaitsQueuedCast"), KeyQ);   // ★ R5-fix8: ключ ставится на каждый прогон
			UAsdaNetSubsystem* Nn = S->N(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaWorldSubsystem* WSs = S->WS();
			if (!Nn || !C || !WSs) { return; }
			// A — меч у обеих ролей (без полёта: момент попадания — только кадр удара); B — баллиста (снаряд в полёте).
			Nn->SelfWeaponItemId = S->PeerWeaponSword; C->EquippedWeaponSubType = 1;
			Feed(4021, Body4021Weapon(S->PeerIdx, S->PeerWeaponBallista));   // сосед B
			Feed(4021, Body4021Weapon(S->PeerIdxA, S->PeerWeaponSword));     // сосед A (роль наблюдателя)
			S->AIdx = Role == 0 ? (int32)Nn->SelfUserNum : S->PeerIdxA;
			S->BIdx = S->PeerIdx;
			S->bRoleAttacker = Role == 0;
			S->RidA = S->NewRid();
			{
				TArray<uint8> MobRec = AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(1, 0), /*State*/1, kSpdDenom);   // запись спауна моба
				if (Sc == EF1::Live161) { AsdaBytes::WrI32(&MobRec[9], 35); }   // ★ 18.10.2026: FieldWolf живого боя — 35 HP
				Feed(4013, MobRec);
			}
			S->LootTile = T(2, 2 + (S->PeerIdx % 9));   // своя клетка на прогон: добыча прошлых прогонов лежит на своих
			if (Sc == EF1::LootExp) { S->LastLootTile = S->LootTile; }   // ★ Ф2 (ревизия Ф1 п.3): убрать в следующем сбросе
			// События (с от «отправки» A; ответ A — через RTT 0.2 с).
			const int32 A = S->AIdx, B = S->BIdx, R = S->RidA;
			switch (Sc)
			{
			case EF1::TwoAttackers:
				S->AddEv(0.15, 4029, Body4029(B, R, 60, { 40 }));       // B: удар 40, снаряд в полёте ~1.4 с
				S->AddEv(0.20, 4029, Body4029(A, R, 0, { 60 }));        // A: смертельный ответ
				S->AddEv(0.30, 4015, Body4015(R, T(1, 0), 0));          // серверная смерть: HP 0 сразу
				S->AddEv(1.20, 4017, Body4017(R, T(1, 0), 0, 0));       // и смерть через 1000 мс
				S->EndR = 3.4; break;
			case EF1::Skill5257:
				S->AddEv(0.20, 5257, Body5257(A, S->SkillVis, R, 100, 0));   // навык A с показом: урон 100, HP после 0
				S->AddEv(0.30, 4017, Body4017(R, T(1, 0), 0, 0));       // смерть раньше выпуска записи навыка
				S->EndR = 2.5; break;
			case EF1::Skill4102SameFrame:
				// ★ R5-fix8 (14.11.2026): оба пакета — в ОДИН момент расписания, то есть одним вызовом подачи, как одно чтение сокета:
				//   5257 ещё стоит в очереди навыков (показ стартует следующим тиком), когда разбирается 4102.
				S->AddEv(0.20, 5257, Body5257(A, S->SkillVis, R, 10, 90));   // навык A: урон 10, HP после 90
				S->AddEv(0.20, 4102, Body4102(A, R, 30, 0));            // 4102 «убит» тем же проходом сети
				S->AddEv(0.35, 4017, Body4017(R, T(1, 0), 0, 0));       // серверная смерть раньше порога
				S->EndR = 2.5; break;
			case EF1::Skill4102Shown:
				S->AddEv(0.20, 5257, Body5257(A, S->SkillVis, R, 10, 90));   // навык A: урон 10, HP после 90 — показ идёт
				S->AddEv(0.25, 4102, Body4102(A, R, 30, 0));            // 4102 «убит» — паркуется в показ (порог фазы)
				S->AddEv(0.35, 4017, Body4017(R, T(1, 0), 0, 0));       // серверная смерть раньше порога
				S->EndR = 2.5; break;
			case EF1::Kill4102:
				S->AddEv(0.20, 4029, Body4029(A, R, 30, { 70 }));       // A: не смертельный ответ
				S->AddEv(0.25, 4102, Body4102(A, R, 30, 0));            // прок/отражение A: «убит»
				S->AddEv(0.35, 4017, Body4017(R, T(1, 0), 0, 0));
				S->EndR = 2.5; break;
			case EF1::LootExp:
				S->AddEv(0.20, 4029, Body4029(A, R, 0, { 100 }));       // A: смертельный ответ
				S->AddEv(0.30, 4015, Body4015(R, T(1, 0), 0));
				S->AddEv(0.35, 5005, Body5005(S->PeerWeaponSword, S->LootTile, R));
				S->AddEv(0.36, 4038, Body4038(R, 10));
				S->AddEv(1.20, 4017, Body4017(R, T(1, 0), 0, 0));
				S->EndR = 3.0; break;
			case EF1::RegenHold:
				S->AddEv(0.20, 4029, Body4029(A, R, kMobHp, { -2 }));   // A: ответ без видимого урона — исход неизвестен
				S->AddEv(0.30, 4015, Body4015(R, T(1, 0), 0));          // серверная смерть (добит кем-то/накопителем)
				S->AddEv(0.40, 4017, Body4017(R, T(1, 0), 4, 80));      // реген/респаун — смерть отменена
				S->EndR = 3.0; break;
			case EF1::StaleTop:
				S->AddEv(0.20, 4029, Body4029(A, R, 40, { 60 }));       // A: HP 40 (прогноз 40)
				S->AddEv(3.20, 4017, Body4017(R, T(1, 0), 3, 100));     // живое поле до слива накопителя — старый верхний снимок
				S->EndR = 4.0; break;
			case EF1::LethalNoHits:
			{
				// Пакеты — в естественном темпе замаха меча (ответ через 0.2 с после «отправки» в начале каждого отрезка): темп отрезков
				//   соседа задаёт интервал его 4029, и в другом темпе роли расходились бы уже по цифрам, а не по правилу смерти.
				TArray<float> Segs;   // естественные отрезки меча (с)
				C->StartAttackSwing(1, 1.f); C->NaturalSegmentsS(3, Segs); C->StopAttackAnim();
				const double S0 = Segs.Num() > 0 ? (double)Segs[0] : 0.93, S1 = Segs.Num() > 1 ? (double)Segs[1] : 0.93;
				S->AddEv(0.20, 4029, Body4029(A, R, kMobHp, { -2, -1, 30, 30 }));        // №1 — ответ на 4026
				S->AddEv(S0 + 0.20, 4029, Body4029(A, R, 40, { 20, 20, 20, 20 }));       // №2 — ответ на первый 4028
				S->AddEv(S0 + S1 + 0.20, 4029, Body4029(A, R, 0, {}));                    // №3 — HP 0, очередь пуста
				S->AddEv(S0 + S1 + 0.35, 4017, Body4017(R, T(1, 0), 0, 0));               // серверная смерть
				S->EndR = S0 + S1 + 3.5; break;
			}
			case EF1::Live161:
				// Время живого журнала от 4027 (+0.2 с RTT): 4027 сырое HP; 4017 flags=1 — реген в бою; flags=3 — HP по удару.
				S->AddEv(0.200, 4027, Body4027(A, R, T(1, 0), 35, { 9, -2, -2, 9 }));
				S->AddEv(1.106, 4029, Body4029(A, R, 26, {}));
				S->AddEv(1.731, 4017, Body4017(R, T(1, 0), 1, 33));
				S->AddEv(2.074, 4029, Body4029(A, R, 24, { -2, 10, 10, 9 }));
				S->AddEv(2.231, 4017, Body4017(R, T(1, 0), 3, 24));
				S->AddEv(2.950, 4029, Body4029(A, R, 14, {}));
				S->AddEv(3.918, 4029, Body4029(A, R, 0, { 10, 10, 9, 9 }));
				S->AddEv(4.918, 4017, Body4017(R, T(1, 0), 0, 0));
				S->EndR = 7.5; break;
			case EF1::PeerCutLethal:
				S->AddEv(0.20, 4029, Body4029(A, R, kMobHp, { -2, -1, 30, 30 }));
				S->AddEv(0.25, 4029, Body4029(A, R, 40, { 20, 20, 20, 20 }));
				S->AddEv(0.60, 4029, Body4029(A, R, 40, {}, /*Sub=*/1));             // отказ код 1 — серия соседа окончена
				S->AddEv(1.00, 4029, Body4029(A, R, 0, {}));                         // смертельный ответ без ударов — новая серия
				S->AddEv(1.15, 4017, Body4017(R, T(1, 0), 0, 0));
				S->EndR = 4.0; break;
			}
		},
		[S, Feed, Sc]()
		{
			UAsdaWorldSubsystem* WSs = S->WS(); AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(S->RidA);
			const double Now = S->WNow();
			if (S->Ts < 0.0)
			{
				if (S->El() < kQuietS || !M || !C || !WSs) { return false; }
				S->Ts = Now; S->Hp0 = M->CurHp;
				if (S->bRoleAttacker)
				{
					// «Отправка» 4026: свой замах стартует сразу (как контроллер), лента — запись по отправке.
					C->FaceGameDir(AsdaCoord::UeToGame2D(M->GetActorLocation()) - C->GetGamePosXZ());
					C->StartAttackSwing(1, Sc == EF1::Live161 ? 8.0f : 4.0f);   // ★ 18.10.2026: живой бой длиннее 4 с
					WSs->ArmOurImpactWindowFromSend(C, M, 1, TEXT("самотест Ф1: заявка 4026"));
				}
			}
			const double R = Now - S->Ts;
			// ★ R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: подача событий сценария не гонится с кадром игры.
			//   (а) За один опрос подаются события только ОДНОГО момента расписания. Прежде после длинного кадра в один опрос уходили
			//       события разных моментов (5257 и 4102 «убит»): показ навыка ещё не начался (он стартует на следующем тике), запись
			//       «убит» применялась сразу — «смерть 0,251 с против порога фазы 0,704» (журнал rx_r5fix3).
			//   (б) Ответ 4029 СВОЕМУ замаху (роль атакующего) подаётся ДО ближайшего собственного кадра удара, если расписание ставит
			//       его вплотную к этому кадру. В живом бою rid=161 смертельный ответ стоит на 3,918 с, а свой кадр удара — на 3,931 с:
			//       от границы кадра зависело, обработан ответ до удара или после, и вся цепочка цифр уезжала на проход (труп 4,5 или
			//       5,0 с — проверка «ОДИНАКОВО по правилу» плавала и в эталоне). Ответ привязан к событию «кадр удара», а не к часам.
			{
				double MinAt = 1e18;   // ближайший неподанный момент расписания (события могут стоять в списке не по порядку)
				for (const FAsdaF1Ev& E : S->Evs) { if (!E.bFed) { MinAt = FMath::Min(MinAt, E.At); } }
				bool bDue = MinAt < 1e17 && R >= MinAt;
				if (!bDue && MinAt < 1e17 && S->bRoleAttacker)
				{
					const double Guard = FMath::Max(0.10, 2.0 * S->MaxDt);   // окно «вплотную»: не меньше двух кадров
					const double Left = MinAt - R;                            // до расписания, с
					bool b4029 = false;                                       // в этом моменте есть ответ 4029
					for (const FAsdaF1Ev& E : S->Evs) { if (!E.bFed && FMath::IsNearlyEqual(E.At, MinAt, 1e-6) && E.Op == 4029) { b4029 = true; } }
					if (b4029 && Left <= Guard)
					{
						TArray<float> Rem; WSs->DebugOwnHitTimersRemaining(Rem);
						float MinRem = -1.f;
						for (const float X : Rem) { if (X > 0.f && (MinRem < 0.f || X < MinRem)) { MinRem = X; } }
						if (MinRem >= 0.f && (double)MinRem <= Left + Guard)
						{
							bDue = true;
							UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ ответ 4029 подан за %.3f с до расписания: свой кадр удара через %.3f с (окно %.3f с)"), Left, MinRem, Guard);
						}
					}
				}
				if (bDue)
				{
					for (FAsdaF1Ev& E : S->Evs)
					{
						if (E.bFed || !FMath::IsNearlyEqual(E.At, MinAt, 1e-6)) { continue; }
						E.bFed = true;
						Feed(E.Op, E.Body);
						if (E.Op == 4029 || E.Op == 5257) { S->TArriveA = (S->TArriveA < 0.0) ? Now : S->TArriveA; }
					}
				}
			}
			// Наблюдения (мир-время).
			if (AAsdaPlayerCharacter* AC = S->AChar(); AC && S->PassStartA < 0.0 && AC->IsAttackAnimActive())
			{
				S->PassStartA = Now - AC->GetAttackSwingElapsedSec();   // начало прохода A на этом экране
			}
			if (WSs)
			{
				for (int32 i = S->SeenFrom; i < WSs->HitResultsSeen.Num(); ++i)
				{
					const UAsdaWorldSubsystem::FAsdaHitResultSeen& E = WSs->HitResultsSeen[i];
					if (E.Rid != S->RidA) { continue; }
					if (E.AttackerUserIdx == S->AIdx && S->TDigitA < 0.0) { S->TDigitA = E.AtS; S->bDigitAOurs = E.bOurs; }
					if (E.AttackerUserIdx == S->BIdx && S->TDigitB < 0.0) { S->TDigitB = E.AtS; }
				}
				// ★ 18.10.2026 (разброс «добычи» у ролей): момент появления — мир-время СПАВНА актора добычи (кадр, в котором он впервые
				//   рисуется), а не мир-время опроса. Опрос — таймер самотеста в менеджере таймеров мира; выпуск удержанной добычи — таймер
				//   ленты в том же менеджере, и если он стоит в кадре ПОСЛЕ опроса, добыча замечалась кадром позже — а кадр после спавна
				//   добычи длинный (синхронная подгрузка модели `FlushAsyncLoading`): +0.023…0.063 с (final/r2: труп 0.525, «добыча» 0.551,
				//   хотя `[лут]` в журнале — тот же кадр 910, что и смерть). Время смерти и так берётся точное (`GetDeathAtS`).
				if (S->TLoot < 0.0)
				{
					if (const AAsdaLootActor* L = WSs->FindLootAtTile(S->LootTile)) { S->TLoot = Now - (double)L->GetGameTimeSinceCreation(); }
				}
			}
			if (M)
			{
				if (S->THpDrop < 0.0 && M->CurHp < S->Hp0) { S->THpDrop = Now; }
				if (S->TDeathF1 < 0.0 && M->IsDead()) { S->TDeathF1 = M->GetDeathAtS(); }
				// ★ Ф2 (ревизия Ф1 п.4): порог фазы показа навыка A — фактический и расчётный.
				if (S->TPhaseFired < 0.0 && S->GI())
				{
					if (const UAsdaSkillFxSubsystem* Fx = S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>())
					{
						for (int32 i = S->PhaseFrom; i < Fx->PhasesFiredSeen.Num(); ++i)
						{
							if (Fx->PhasesFiredSeen[i].CasterUserIdx == S->AIdx) { S->TPhaseFired = Fx->PhasesFiredSeen[i].AtS; S->TPhasePlanned = Fx->PhasesFiredSeen[i].PlannedS; break; }
						}
					}
				}
				S->FinalHp = M->CurHp; S->bFinalServerDead = M->bServerDead;
				S->HpMax = FMath::Max(S->HpMax, M->CurHp);
				// ★ 18.10.2026 (пол HP): подъёмы полосы живого моба за прогон (сценарий Live161 — их быть не должно).
				if (!M->IsDead()) { if (S->HpPrev >= 0 && M->CurHp > S->HpPrev) { ++S->HpRises; S->HpRiseMax = FMath::Max(S->HpRiseMax, M->CurHp - S->HpPrev); } S->HpPrev = M->CurHp; }
				if (R > 3.0 && S->HpAt3 < 0) { S->HpAt3 = M->CurHp; }
			}
			return R > S->EndR;
		},
		[S, Sc, Role, bControl, KeyQ]()
		{
			const double Tol = S->FrameTol();
			// Опора сравнения ролей: начало прохода A (удар) или приход каста/записи (навык, 4102 — у ролей один миг).
			const bool bSwingRef = Sc == EF1::TwoAttackers || Sc == EF1::LootExp || Sc == EF1::LethalNoHits;
			const double Ref = bSwingRef ? S->PassStartA : S->Ts;
			auto Rel = [Ref](double X) { return (X >= 0.0 && Ref >= 0.0) ? X - Ref : -1.0; };
			FAsdaF1Out O;
			// ★ R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: смерть применяется таймером через 0,05 с после смертельной цифры — то есть в
			//   первом кадре после неё. Одиночный длинный кадр (в журнале — 0,61 с под тремя играми) сдвигал труп на свою длину:
			//   «труп 4,900, последняя цифра 4,500». В срок входит длина кадра смерти сверх 1/60 с; при обычном кадре — ноль.
			O.DeathLagS = (S->TDeathF1 >= 0.0) ? FMath::Max(0.0, S->FrameDtAt(S->TDeathF1) - 1.0 / 60.0) : 0.0;
			O.FrameExcessS = FMath::Max(0.0, S->MaxDt - 1.0 / 60.0);
			O.DeathRel = Rel(S->TDeathF1); O.DigitRel = Rel(S->TDigitA); O.HpDropRel = Rel(S->THpDrop); O.LootRel = Rel(S->TLoot);
			O.ExpRel = Rel(S->TExp); O.DigitBRel = Rel(S->TDigitB); O.FinalHp = S->FinalHp; O.bDead = S->TDeathF1 >= 0.0;
			O.PhaseRel = Rel(S->TPhaseFired);
			O.bServerDead = S->bFinalServerDead; O.PassFromTs = (S->PassStartA >= 0.0 && S->Ts >= 0.0) ? S->PassStartA - S->Ts : -1.0;
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ Ф1 замер (от %s, с): проход A от «отправки» %.3f | цифра A %.3f (наша %s) | цифра B %.3f | полоса вниз %.3f | труп %.3f | добыча %.3f | опыт %.3f | итог HP %d (max %d, в 3 с %d) | «обречён» %s | допуск %.3f"),
				bSwingRef ? TEXT("начала прохода A") : TEXT("«отправки» A"), O.PassFromTs, O.DigitRel, S->bDigitAOurs ? TEXT("да") : TEXT("нет"), O.DigitBRel,
				O.HpDropRel, O.DeathRel, O.LootRel, O.ExpRel, O.FinalHp, S->HpMax, S->HpAt3, O.bServerDead ? TEXT("да") : TEXT("нет"), Tol);
			const FString Key = FString::Printf(TEXT("%d/%d/%d"), (int32)Sc, Role, bControl ? 1 : 0);
			S->F1Out.Add(Key, O);
			const FAsdaF1Out* Att = S->F1Out.Find(FString::Printf(TEXT("%d/0/0"), (int32)Sc));   // роль атакующего с ключами 1
			// ★ R5-fix8: контроль ключа `asda.Skill.Hit4102WaitsQueuedCast 0` — запись 4102 применяется сразу (прежний дефект виден).
			if (Sc == EF1::Skill4102SameFrame && KeyQ == 0)
			{
				S->Expect(O.bDead && O.DeathRel >= 0.0 && O.DeathRel <= 0.20 + 0.10 + Tol,
					FString::Printf(TEXT("контроль ключа 0: смерть сразу на записи 4102 (%.3f при пакете 0.20, порог фазы %.3f) — раньше порога, прежний дефект виден"), O.DeathRel, O.PhaseRel));
				return;
			}
			// ── Проверки внутри роли ──
			if (!bControl)
			{
				switch (Sc)
				{
				case EF1::TwoAttackers:
					S->Expect(O.bDead && O.DigitRel >= 0.0 && O.DeathRel >= O.DigitRel - Tol && O.DeathRel <= O.DigitRel + 0.15,
						FString::Printf(TEXT("смерть на смертельном попадании A: труп %.3f, цифра A %.3f (серверная смерть пришла раньше)"), O.DeathRel, O.DigitRel));
					S->Expect(O.HpDropRel >= O.DigitRel - Tol, FString::Printf(TEXT("полоса не ушла раньше попадания A (%.3f против %.3f)"), O.HpDropRel, O.DigitRel));
					S->Expect(O.DigitBRel < 0.0 || O.DigitBRel > O.DeathRel, FString::Printf(TEXT("снаряд B долетел уже в труп (%.3f > %.3f) — смерть не ждала B"), O.DigitBRel, O.DeathRel));
					if (Role == 0) { S->Expect(S->bDigitAOurs, TEXT("своя цифра удара A — «наша» (своя очередь цела)")); }
					else { S->Expect(!S->bDigitAOurs, TEXT("цифра соседа A — не «наша»")); }
					break;
				case EF1::Skill5257:
					S->Expect(O.bDead && O.DeathRel > 0.30 + Tol && O.HpDropRel >= 0.0 && O.DeathRel >= O.HpDropRel - Tol,
						FString::Printf(TEXT("смерть на выпуске записи навыка (%.3f; запись вычла HP в %.3f), а не по 4017 в 0.30"), O.DeathRel, O.HpDropRel));
					// ★ Ф2 (ревизия Ф1 п.4): выпуск записи — на пороге фазы показа, а порог — на расчётном моменте COMBO по данным.
					S->Expect(O.PhaseRel >= 0.0 && FMath::Abs(O.HpDropRel - O.PhaseRel) <= Tol,
						FString::Printf(TEXT("запись навыка выпущена на пороге фазы: полоса %.3f, порог %.3f (допуск %.3f)"), O.HpDropRel, O.PhaseRel, Tol));
					S->Expect(S->TPhaseFired >= 0.0 && S->TPhaseFired - S->TPhasePlanned >= -0.002 && S->TPhaseFired - S->TPhasePlanned <= Tol,
						FString::Printf(TEXT("порог фазы 5257 сработал на расчётном моменте COMBO (факт − расчёт %.3f с, допуск 1 кадр %.3f)"), S->TPhaseFired - S->TPhasePlanned, Tol));
					break;
				case EF1::Skill4102Shown: case EF1::Skill4102SameFrame:
					S->Expect(O.bDead && O.PhaseRel >= 0.0 && O.DeathRel > 0.35 + Tol && FMath::Abs(O.DeathRel - O.PhaseRel) <= Tol,
						FString::Printf(TEXT("4102 «убит» при показе навыка: смерть на пороге фазы (%.3f против порога %.3f), а не по 4017 в 0.35"), O.DeathRel, O.PhaseRel));
					break;
				case EF1::Kill4102:
					S->Expect(O.bDead && FMath::Abs(O.DeathRel - 0.25) <= 0.05 + Tol, FString::Printf(TEXT("4102 «убит» без показа навыка — смерть сразу на нём (%.3f, пакет 0.25)"), O.DeathRel));
					break;
				case EF1::LootExp:
					S->Expect(O.bDead && O.DigitRel >= 0.0 && O.DeathRel >= O.DigitRel - Tol, FString::Printf(TEXT("смерть на смертельном попадании (%.3f, цифра %.3f)"), O.DeathRel, O.DigitRel));
					S->Expect(O.LootRel >= O.DeathRel - Tol && O.LootRel <= O.DeathRel + 0.1, FString::Printf(TEXT("добыча 5005 появилась вместе со смертью (%.3f против %.3f), не под живым мобом"), O.LootRel, O.DeathRel));
					S->Expect(O.ExpRel >= O.DeathRel - Tol && O.ExpRel <= O.DeathRel + 0.1, FString::Printf(TEXT("опыт 4038 показан вместе со смертью (%.3f против %.3f)"), O.ExpRel, O.DeathRel));
					break;
				case EF1::RegenHold:
					S->Expect(!O.bDead && !O.bServerDead && O.FinalHp == 80, FString::Printf(TEXT("реген при отложенной смерти: моб жив (%s), «обречён» снят (%s), HP 80 (%d)"),
						O.bDead ? TEXT("НЕТ") : TEXT("да"), O.bServerDead ? TEXT("НЕТ") : TEXT("да"), O.FinalHp));
					break;
				case EF1::StaleTop:
					S->Expect(O.FinalHp == 40 && S->HpMax <= kMobHp, FString::Printf(TEXT("старый верхний снимок 100 через 3.0 с отброшен: HP %d (ждём 40)"), O.FinalHp));
					break;
				case EF1::LethalNoHits:
				{
					// Цифры A по цели (не по трупу): ровно применённое сервером, смерть — сразу после шестой (удар, доводящий 100 до 0).
					TArray<int32> Vals; TArray<double> At;
					for (const UAsdaWorldSubsystem::FAsdaHitResultSeen& E : S->SeenOn(S->RidA)) { if (E.AttackerUserIdx == S->AIdx && !E.bOnCorpse) { Vals.Add(E.Raw); At.Add(E.AtS); } }
					FString L; for (int32 i = 0; i < Vals.Num(); ++i) { L += FString::Printf(TEXT(" %d@%.3f"), Vals[i], Rel(At[i])); }
					const TArray<int32> Want = { -2, -1, 30, 30, 20, 20 };
					const double T6 = At.Num() >= 6 ? Rel(At[5]) : -1.0;   // смертельная цифра
					S->Expect(Vals == Want, FString::Printf(TEXT("цифры A — ровно −2 −1 30 30 20 20, по трупу ничего: [%s ]"), *L));
					S->Expect(O.bDead && T6 >= 0.0 && O.DeathRel >= T6 - Tol && O.DeathRel <= T6 + 0.15,
						FString::Printf(TEXT("смерть на ударе, доводящем полосу до 0: труп %.3f, шестая цифра %.3f"), O.DeathRel, T6));
					break;
				}
				case EF1::Live161:
				{
					TArray<double> At;   // цифры A по цели (не по трупу)
					for (const UAsdaWorldSubsystem::FAsdaHitResultSeen& E : S->SeenOn(S->RidA)) { if (E.AttackerUserIdx == S->AIdx && !E.bOnCorpse) { At.Add(E.AtS); } }
					const double TLast = At.Num() > 0 ? Rel(At.Last()) : -1.0;   // смертельная цифра
					UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ живой бой rid=161: цифр A %d, последняя %.3f, труп %.3f, подъёмов полосы %d (наибольший %d), полоса макс %d"),
						At.Num(), TLast, O.DeathRel, S->HpRises, S->HpRiseMax, S->HpMax);
					S->Expect(S->HpRises == 0 && S->HpMax <= 35, FString::Printf(TEXT("полоса за весь бой монотонно не растёт: подъёмов %d (наибольший %d), максимум %d"), S->HpRises, S->HpRiseMax, S->HpMax));
					S->Expect(O.bDead && TLast >= 0.0 && O.DeathRel >= TLast - Tol && O.DeathRel <= TLast + 0.25 + O.DeathLagS,
						FString::Printf(TEXT("смерть на смертельном ударе: труп %.3f, последняя цифра A %.3f"), O.DeathRel, TLast));
					break;
				}
				case EF1::PeerCutLethal:
				{
					// Удары прерванной серии не показаны и не выбраны смертельными: смерть — на первом кадре удара нового отрезка (с кадра 0).
					int32 AfterCut = 0;   // цифры A после отказа (прерванная серия)
					for (const UAsdaWorldSubsystem::FAsdaHitResultSeen& E : S->SeenOn(S->RidA)) { if (E.AttackerUserIdx == S->AIdx && E.AtS > S->Ts + 0.60 + Tol) { ++AfterCut; } }
					AAsdaPlayerCharacter* AC = S->AChar();   // атакующий A (тайминги клипа)
					const AAsdaPlayerCharacter::FAsdaComboTiming CTa = AC ? AC->GetComboTiming(1) : AAsdaPlayerCharacter::FAsdaComboTiming();
					const double Hit0 = (CTa.bValid && CTa.HitTimesSec.Num() > 0) ? (double)CTa.HitTimesSec[0] : -1.0;   // первый кадр удара от кадра 0
					S->Expect(AfterCut == 0, FString::Printf(TEXT("прерванная серия соседа: цифр после отказа нет (%d)"), AfterCut));
					S->Expect(O.bDead && Hit0 >= 0.0 && O.DeathRel >= 1.00 + Hit0 - 0.1 && O.DeathRel <= 1.00 + Hit0 + 0.2,
						FString::Printf(TEXT("смерть на кадре удара нового отрезка (1.00 + %.3f с), а не на ударе прерванной серии: труп %.3f"), Hit0, O.DeathRel));
					break;
				}
				}
			}
			// ── Сравнение ролей (наблюдатель против атакующего) и контроль ──
			if (Role == 1 && Att)
			{
				// R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: две роли — два отдельных прогона со своими кадрами; сравниваются моменты из разных
				//   прогонов, общего «кадра события» у них нет — к допуску добавляется превышение худшего кадра прогона атакующего над 1/60 с
				const double TolRoles = Tol + Att->FrameExcessS;   // кадр этого прогона + превышение кадра прогона атакующего
				auto Same = [TolRoles](double A, double B) { return A >= 0.0 && B >= 0.0 && FMath::Abs(A - B) <= TolRoles; };
				switch (Sc)
				{
				case EF1::TwoAttackers: case EF1::LootExp: case EF1::Skill5257: case EF1::Kill4102: case EF1::Skill4102Shown: case EF1::LethalNoHits:
				case EF1::Skill4102SameFrame:
					if (!bControl)
					{
						S->Expect(Same(O.DeathRel, Att->DeathRel), FString::Printf(TEXT("ОДИНАКОВО: труп у наблюдателя %.3f, у атакующего %.3f (разнос %.3f, допуск 1 кадр %.3f)"),
							O.DeathRel, Att->DeathRel, O.DeathRel - Att->DeathRel, Tol));
						if (Sc == EF1::LootExp) { S->Expect(Same(O.LootRel, Att->LootRel), FString::Printf(TEXT("ОДИНАКОВО: добыча %.3f против %.3f"), O.LootRel, Att->LootRel)); }
					}
					else if (Sc == EF1::Kill4102)
					{
						S->Expect(Same(O.DeathRel, Att->DeathRel), FString::Printf(TEXT("контроль: 4102 без показа применяется сразу у всех — ключ не влияет (%.3f против %.3f)"), O.DeathRel, Att->DeathRel));
					}
					else
					{
						S->Expect(O.DeathRel >= 0.0 && Att->DeathRel >= 0.0 && Att->DeathRel - O.DeathRel > 0.1,
							FString::Printf(TEXT("контроль ObserverSync 0: у наблюдателя труп по серверу %.3f — раньше атакующего %.3f, дефект виден"), O.DeathRel, Att->DeathRel));
						if (Sc == EF1::LootExp) { S->Expect(O.LootRel >= 0.0 && O.LootRel < Att->DeathRel - 0.1, FString::Printf(TEXT("контроль: добыча %.3f — раньше смерти у атакующего %.3f"), O.LootRel, Att->DeathRel)); }
					}
					break;
				case EF1::RegenHold:
					if (!bControl) { S->Expect(!O.bDead && !Att->bDead && O.FinalHp == Att->FinalHp, TEXT("ОДИНАКОВО: у обеих ролей моб жив после регена, HP совпадает")); }
					else { S->Expect(O.bDead, FString::Printf(TEXT("контроль ObserverSync 0: у наблюдателя труп лёг по 4015 (%.3f) до регена — дефект виден"), O.DeathRel)); }
					break;
				case EF1::StaleTop:
					if (!bControl) { S->Expect(O.FinalHp == Att->FinalHp, FString::Printf(TEXT("ОДИНАКОВО: итог HP %d против %d"), O.FinalHp, Att->FinalHp)); }
					else { S->Expect(S->HpMax == 100, FString::Printf(TEXT("контроль HitFrameAllChannels 0 (прогноз 2.5 с): старый снимок поднял полосу до %d — дефект виден"), S->HpMax)); }
					break;
				case EF1::PeerCutLethal:
					break;   // только роль наблюдателя (у атакующего «отказ соседа» не бывает)
				case EF1::Live161:
					// [ОРИГИНАЛ] свой замах показывает значения ответа подряд (`StepCount` за ответ), сосед — по кадрам своего отрезка (2 за
					//   4029): моменты цифр у ролей разные по эталону. Одинаково — правило: обе полосы не растут, смерть у обеих — на
					//   смертельном ударе (проверено выше в каждой роли); разнос момента трупа — в пределах кадра удара отрезка.
					S->Expect(O.bDead && Att->bDead && FMath::Abs(O.DeathRel - Att->DeathRel) <= 0.15 + O.DeathLagS + Att->DeathLagS,
						FString::Printf(TEXT("ОДИНАКОВО по правилу: у обеих ролей смерть на смертельном ударе (наблюдатель %.3f, атакующий %.3f, разнос %.3f — в пределах кадра отрезка)"),
							O.DeathRel, Att->DeathRel, O.DeathRel - Att->DeathRel));
					break;
				}
			}
		});
	};
	for (const EF1 Sc : { EF1::TwoAttackers, EF1::Skill5257, EF1::Kill4102, EF1::LootExp, EF1::RegenHold, EF1::StaleTop, EF1::Skill4102Shown, EF1::LethalNoHits })
	{
		AddF1(Sc, 0, 1, 1);
		AddF1(Sc, 1, 1, 1);
		if (Sc == EF1::StaleTop) { AddF1(Sc, 1, 1, 0); } else { AddF1(Sc, 1, 0, 1); }
	}
	AddF1(EF1::PeerCutLethal, 1, 1, 1);   // ★ ревизия 18.10 (п.3): только наблюдатель
	// ★ R5-fix8 (14.11.2026): «5257 и 4102 одним проходом сети» — обе роли и контроль ключа 0 (красный без правки).
	AddF1(EF1::Skill4102SameFrame, 0, 1, 1);
	AddF1(EF1::Skill4102SameFrame, 1, 1, 1);
	AddF1(EF1::Skill4102SameFrame, 1, 1, 1, 0);
	AddF1(EF1::Live161, 0, 1, 1);         // ★ 18.10.2026 (пол HP): атакующий
	AddF1(EF1::Live161, 1, 1, 1);         // наблюдатель (контроль ключом 0 — в asda.Combat.SelfTestHitFrame)

	// ── «Атакующий виден, моб нет» и «моб виден, атакующий нет» (+ контроль) ──
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("Ф1 атакующий виден, моба нет (4029 по неизвестному rid), ObserverSync %d"), K), 8.f, [S, Feed, T, K]()
		{
			S->F1Reset(K, 1);
			Feed(4021, Body4021Weapon(S->PeerIdx, S->PeerWeaponSword));
			S->RidA = S->NewRid();
		}, [S, Feed, T]()
		{
			const double Now = S->WNow();
			if (S->Ts < 0.0) { if (S->El() < kQuietS) { return false; } S->Ts = Now; Feed(4029, Body4029(S->PeerIdx, S->RidA, 50, { 10, 11 })); S->QAfterDead = S->PeerQueue(); }
			const double R = Now - S->Ts;
			if (S->Phase == 0 && R >= 0.3) { S->Phase = 1; Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, -1), /*State*/1, kSpdDenom)); }
			if (S->Phase == 1 && R >= 0.6 && S->Mob(S->RidA)) { S->Phase = 2; S->TPhase = Now; Feed(4017, Body4017(S->RidA, T(2, -1), 0, 0)); }
			if (AAsdaMobActor* M = S->Mob(S->RidA); M && S->TDeathF1 < 0.0 && M->IsDead()) { S->TDeathF1 = M->GetDeathAtS(); }
			return S->Phase == 2 && R > 1.2;
		}, [S, K]()
		{
			const UAsdaWorldSubsystem* WSs = S->WS();
			const double D = (S->TDeathF1 >= 0.0 && S->TPhase >= 0.0) ? S->TDeathF1 - S->TPhase : -1.0;
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ Ф1 замер: очередь соседа после 4029 по отсутствующему мобу %d | записей ленты %d | смерть после 4017 через %.3f с"),
				S->QAfterDead, WSs ? WSs->GetLedgerPendingNum(S->RidA) : -1, D);
			S->Expect(D >= 0.0 && D <= S->FrameTol() + 0.02, FString::Printf(TEXT("моба не было при 4029 — записей нет, смерть 4017 сразу (%.3f с)"), D));
			if (K) { S->Expect(S->QAfterDead == 0, FString::Printf(TEXT("удары по отсутствующему мобу не копятся (%d)"), S->QAfterDead)); }
			else { S->Expect(S->QAfterDead == 2, FString::Printf(TEXT("контроль ObserverSync 0: удары по отсутствующему мобу копятся (%d) — дефект виден"), S->QAfterDead)); }
		});
		Add(FString::Printf(TEXT("Ф1 моб виден, атакующего нет (только 4017), ObserverSync %d"), K), 8.f, [S, Feed, T, K]()
		{
			S->F1Reset(K, 1);
			S->RidA = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(-2, -1), /*State*/1, kSpdDenom));
		}, [S, Feed, T]()
		{
			const double Now = S->WNow();
			AAsdaMobActor* M = S->Mob(S->RidA);
			if (S->Ts < 0.0) { if (S->El() < kQuietS || !M) { return false; } S->Ts = Now; Feed(4017, Body4017(S->RidA, T(-2, -1), 3, 50)); if (M) { S->FinalHp = M->CurHp; } }
			const double R = Now - S->Ts;
			if (S->Phase == 0 && R >= 0.3) { S->Phase = 1; S->TPhase = Now; Feed(4017, Body4017(S->RidA, T(-2, -1), 0, 0)); }
			if (M && S->TDeathF1 < 0.0 && M->IsDead()) { S->TDeathF1 = M->GetDeathAtS(); }
			return S->Phase == 1 && R > 0.8;
		}, [S]()
		{
			const double D = (S->TDeathF1 >= 0.0 && S->TPhase >= 0.0) ? S->TDeathF1 - S->TPhase : -1.0;
			const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> On = S->SeenOn(S->RidA);
			S->Expect(S->FinalHp == 50, FString::Printf(TEXT("HP 4017 сразу (%d) — законно, как оригинал (лента не держит: записей нет)"), S->FinalHp));
			if (S->Key) { S->Expect(On.Num() == 0, FString::Printf(TEXT("дельта без атакующего — без цифры (%d)"), On.Num())); }
			else { S->Expect(On.Num() >= 1, FString::Printf(TEXT("контроль ObserverSync 0: дельта нарисована «нашей» цифрой (%d) — дефект Ф0 виден"), On.Num())); }
			S->Expect(D >= 0.0 && D <= S->FrameTol() + 0.02, FString::Printf(TEXT("смерть 4017 сразу (%.3f с)"), D));
		});
	}

	// ── Ревизия Ф0 п.6: серия соседа по признаку `field11C` (4027 case 1, смена цели, код 2 при живой цели, коды 3–7) ──
	enum class EQ : uint8 { From4027, TargetChange, Code2Alive, Code2Queue };
	for (const int32 K : { 1, 0 })
	{
		for (const EQ Q : { EQ::From4027, EQ::TargetChange, EQ::Code2Alive, EQ::Code2Queue })
		{
			const TCHAR* QName = Q == EQ::From4027 ? TEXT("4027 case 1 — очередь заново") : Q == EQ::TargetChange ? TEXT("смена цели посреди серии — очередь заново [ПОРТ]")
				: Q == EQ::Code2Alive ? TEXT("код 2 при живой цели — следующий 4029 начинает очередь заново")
				: TEXT("код 2 с непустой очередью при живой цели — очередь пакета дописана, серия продолжается");
			Add(FString::Printf(TEXT("Ф1 серия соседа: %s, ObserverSync %d"), QName, K), 12.f, [S, Feed, T, K]()
			{
				S->F1Reset(K, 1);
				Feed(4021, Body4021Weapon(S->PeerIdx, S->PeerWeaponBallista));   // баллиста: 1 удар за замах — хвост очереди виден
				S->RidA = S->NewRid(); S->RidB = S->NewRid();
				Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(3, -2), /*State*/1, kSpdDenom));
				Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidB, kMobType, T(-3, -2), /*State*/1, kSpdDenom));
			}, [S, Feed, T, Q]()
			{
				const double Now = S->WNow();
				if (S->Ts < 0.0) { if (S->El() < kQuietS || !S->Mob(S->RidA) || !S->Mob(S->RidB)) { return false; } S->Ts = Now; Feed(4029, Body4029(S->PeerIdx, S->RidA, 90, { 10, 11, 12, 13 })); }
				const double R = Now - S->Ts;
				// Событие — после показа первой цифры серии (10): её выстрел уже поставлен таймером и к событию не относится.
				if (S->Phase == 0 && (S->SeenOn(S->RidA).Num() >= 1 || R >= 4.0))
				{
					S->Phase = 1; S->SeenBefore2nd = S->SeenNum();
					if (Q == EQ::From4027) { Feed(4027, Body4027(S->PeerIdx, S->RidA, T(3, -2), 80, { 50, 51, 52, 53 })); }
					else if (Q == EQ::TargetChange) { Feed(4029, Body4029(S->PeerIdx, S->RidB, 80, { 20, 21, 22, 23 })); }
					else if (Q == EQ::Code2Queue) { const int32 Q0 = S->PeerQueue(); Feed(4029, Body4029(S->PeerIdx, S->RidA, 90, { 60, 61, 62, 63 }, /*Sub=*/2)); S->QAfter1 = S->PeerQueue() - Q0; }
					else { Feed(4029, Body4029(S->PeerIdx, S->RidA, 90, {}, /*Sub=*/2)); }
				}
				if (S->Phase == 1 && S->TPhase < 0.0) { S->TPhase = Now; }
				// ★ Ф2 [ОРИГИНАЛ]: 4027 case 1 соседа замаха не начинает (network.c:40812–40819, `LABEL_36`) — очередь покажет отрезок
				//   следующего 4029 (атакующий дошёл до моба и шлёт 4028).
				if (Q == EQ::From4027 && S->Phase == 1 && Now - S->TPhase >= 0.3) { S->Phase = 2; Feed(4029, Body4029(S->PeerIdx, S->RidA, 80, {})); }
				if (Q == EQ::Code2Alive && S->Phase == 1 && Now - S->TPhase >= 0.1) { S->Phase = 2; S->bSwingAfter2Alive = S->PeerSwinging(); }
				if (Q == EQ::Code2Alive && S->Phase == 2 && Now - S->TPhase >= 0.4) { S->Phase = 3; S->SeenBefore2nd = S->SeenNum(); Feed(4029, Body4029(S->PeerIdx, S->RidA, 80, { 40, 41, 42, 43 })); }
				const int32 TargetRid = Q == EQ::TargetChange ? S->RidB : S->RidA;
				const bool bWaitPhase = (Q == EQ::Code2Alive) ? S->Phase == 3 : (Q == EQ::From4027 ? S->Phase >= 1 : S->Phase == 1);
				if (bWaitPhase)
				{
					const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> On = S->SeenOn(TargetRid, S->SeenBefore2nd);
					if (On.Num() > 0) { S->FirstOnB2 = On[0].Raw; return true; }
				}
				return R > 9.0;
			}, [S, K, Q]()
			{
				const int32 Want = Q == EQ::From4027 ? 50 : Q == EQ::TargetChange ? 20 : Q == EQ::Code2Queue ? 11 : 40;
				UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ Ф1 замер: первая цифра после события %d (ждём %d)"), S->FirstOnB2, Want);
				if (K && Q == EQ::Code2Queue)
				{
					// [ОРИГИНАЛ] network.c:42204–42216 (ветка соседа): очередь пакета ДОПИСЫВАЕТСЯ — серия продолжается хвостом 11.
					// ★ Ревизия Ф2 (мелочь 5): без ослабления — пакет дописывает 4 значения, а его отрезок (баллиста: один выстрел) тем же
					//   вызовом снимает одно на свой кадр (план Ф2) ⇒ ровно +3.
					S->Expect(S->QAfter1 == 3,
						FString::Printf(TEXT("код 2 с очередью при живой цели: очередь пакета дописана (+%d, ждём +3: +4 и одно снято отрезком)"), S->QAfter1));
					S->Expect(S->FirstOnB2 == 11, FString::Printf(TEXT("после кода 2 серия продолжается хвостом прошлой очереди: первая цифра %d (ждём 11)"), S->FirstOnB2));
				}
				else if (K)
				{
					S->Expect(S->FirstOnB2 == Want, FString::Printf(TEXT("очередь соседа начата заново: первая цифра %d (ждём %d, хвост прошлой — 11)"), S->FirstOnB2, Want));
					if (Q == EQ::Code2Alive) { S->Expect(S->bSwingAfter2Alive, TEXT("код 2 при живой цели замах соседа не гасит (эталон: состояние 5)")); }
				}
				else if (Q == EQ::Code2Queue)
				{
					S->Expect(S->QAfter1 == 0, FString::Printf(TEXT("контроль ObserverSync 0: отказ соседа не разбирается — очередь кода 2 потеряна (+%d) — дефект виден"), S->QAfter1));
				}
				else
				{
					S->Expect(S->FirstOnB2 == 11, FString::Printf(TEXT("контроль ObserverSync 0: вышел хвост прошлой очереди 11 (%d) — дефект виден"), S->FirstOnB2));
				}
			});
		}
		// Коды 3–7: сосед в замахе → отказ гасит замах и очередь.
		//   ★ 18.10.2026 (ревизия Ф4, мелочь 4) [ОРИГИНАЛ]: кроме кода 5 — у него покой только НЕ занятому (`!charIsActionStateNonCombat`,
		//   network.c:42253; функция — миснейм «персонаж ЗАНЯТ», entity_char.c:25398). Сосед в замахе занят: замах и очередь остаются.
		for (const uint8 Code : { (uint8)3, (uint8)4, (uint8)5, (uint8)6, (uint8)7 })
		{
			Add(FString::Printf(TEXT("Ф1 отказ 4029 код %u гасит замах соседа, ObserverSync %d"), (uint32)Code, K), 8.f, [S, Feed, T, K]()
			{
				S->F1Reset(K, 1);
				Feed(4021, Body4021Weapon(S->PeerIdx, S->PeerWeaponSword));
				S->RidA = S->NewRid();
				Feed(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, -1), /*State*/1, kSpdDenom));
			}, [S, Feed, Code]()
			{
				const double Now = S->WNow();
				if (S->Ts < 0.0) { if (S->El() < kQuietS || !S->Mob(S->RidA)) { return false; } S->Ts = Now; Feed(4029, Body4029(S->PeerIdx, S->RidA, 90, { 10, 11, 12, 13 })); }
				const double R = Now - S->Ts;
				if (S->Phase == 0 && R >= 0.3) { S->Phase = 1; S->bSwingBefore = S->PeerSwinging(); Feed(4029, Body4029(S->PeerIdx, S->RidA, 90, {}, Code)); }
				if (S->Phase == 1 && R >= 0.4) { S->bSwingAfter1 = S->PeerSwinging(); S->QAfter1 = S->PeerQueue(); return true; }
				return false;
			}, [S, K, Code]()
			{
				UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ Ф1 замер: код %u — замах до %s, после %s, очередь %d"), (uint32)Code,
					S->bSwingBefore ? TEXT("да") : TEXT("нет"), S->bSwingAfter1 ? TEXT("да") : TEXT("нет"), S->QAfter1);
				S->Expect(S->bSwingBefore, TEXT("до отказа сосед в замахе"));
				if (K && Code == 5)
				{
					S->Expect(S->bSwingAfter1, FString::Printf(TEXT("код 5 при замахе (сосед занят): замах не погашен, серия идёт (очередь %d) — эталон network.c:42253"), S->QAfter1));
				}
				else if (K) { S->Expect(!S->bSwingAfter1 && S->QAfter1 == 0, FString::Printf(TEXT("код %u погасил замах и сбросил очередь (%d)"), (uint32)Code, S->QAfter1)); }
				else { S->Expect(S->bSwingAfter1, FString::Printf(TEXT("контроль ObserverSync 0: после кода %u замах доигрывает%s"), (uint32)Code,
					Code == 5 ? TEXT(" (у кода 5 при замахе — как и эталон)") : TEXT(" — дефект виден"))); }
			});
		}
	}

	// ═══════════════════════ ★★★ Ф2 — ЗАМАХ СОСЕДА ОТ ПАКЕТОВ: ЦИФРЫ И ВЫСТРЕЛЫ НА КАДРАХ УДАРА ОТРЕЗКА ═══════════════════════
	//   Проект §4, §8. Модель сервера: атакующий шлёт 4028 в эталонных точках отправки своего клипа (номинальный темп — свой персонаж
	//   без серверных часов), ответ 4029 приходит через RTT 0.2 с (наблюдателю — в тот же миг: RTT/2 + своё RTT/2). Пачка из 4 ударов,
	//   расход 2 за замах у меча (тип ≤ 3) и 1 у дальнобойного; HP ответа — «после» ударов, израсходованных сервером к этому замаху.
	//   Сравнение ролей — момент каждой цифры (ближний) / выстрела (дальнобойный) относительно начала ОТРЕЗКА на своём экране и номер
	//   отрезка (допуск 1 кадр). Моб с HP 2000 (метки ударов — малые числа, бой не кончается смертью).
	enum class EF2 : uint8 { Full, Skip, MidBatch, Jitter, Fast, Slow, Control, TwoSwords, CatchUp, BigRtt };
	auto F2Name = [](EF2 V) -> const TCHAR*
	{
		switch (V)
		{
		case EF2::Full:      return TEXT("серия 4029");
		case EF2::Skip:      return TEXT("пропуск 4029 №3 (сервер отбросил ранний 4028)");
		case EF2::MidBatch:  return TEXT("вход посреди пачки (первый 4029 — пустой №1)");
		case EF2::Jitter:    return TEXT("джиттер интервала 4029 ±12 %");
		case EF2::Fast:      return TEXT("4029 чаще естественного темпа (×0.7) — предел 1.25");
		case EF2::Slow:      return TEXT("4029 реже естественного темпа (×1.35) — предел 0.8");
		case EF2::Control:   return TEXT("серия 4029, КОНТРОЛЬ SwingPlanFromPackets 0");
		case EF2::TwoSwords: return TEXT("два мечника по одной цели дольше 4.5 с");
		case EF2::CatchUp:   return TEXT("догон: 12 пакетов 4029 чаще естественного темпа (×0.6)");
		case EF2::BigRtt:    return TEXT("большой RTT (ответ через 0.8 с)");
		}
		return TEXT("?");
	};
	constexpr int32 kF2MobHp = 2000;   // HP моба боёв Ф2
	// Kind: 0 — меч, 1 — лук, 2 — баллиста. Role: 0 — атакующий (свой), 1 — наблюдатель (сосед).
	auto AddF2 = [S, Add, Feed, T, F2Name](int32 Kind, int32 Role, EF2 V)
	{
		const TCHAR* KindName = Kind == 0 ? TEXT("меч") : Kind == 1 ? TEXT("лук") : TEXT("баллиста");
		Add(FString::Printf(TEXT("Ф2 %s: %s — роль %s"), KindName, F2Name(V), Role == 0 ? TEXT("АТАКУЮЩЕГО") : TEXT("НАБЛЮДАТЕЛЯ")), 20.f,
		[S, Feed, T, Kind, Role, V]()
		{
			S->F1Reset(1, 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.SwingPlanFromPackets"), V == EF2::Control ? 0 : 1);
			// Свой замах — номинальным темпом без серверных часов (`RhythmFromAnim 0`: свой персонаж не владелец ритма), но с
			//   исправлениями ритма (`ServerRhythm 1`): при `ServerRhythm 0` (прежний порт) фиджет покоя idle03 перебивает идущий
			//   замах через 6–12 с (видно в r0c: клип своего меча сброшен посреди серии).
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.RhythmFromAnim"), 0);
			UAsdaNetSubsystem* Nn = S->N(); AAsdaPlayerCharacter* C = S->Ch();
			if (!Nn || !C) { return; }
			const int32 Item = Kind == 0 ? S->PeerWeaponSword : Kind == 1 ? S->PeerWeaponBow : S->PeerWeaponBallista;   // оружие A
			const int32 Wt = Kind == 0 ? 1 : Kind == 1 ? S->BowWt : 11;                                                    // его подтип
			if (Role == 0) { Nn->SelfWeaponItemId = Item; C->EquippedWeaponSubType = Wt; }
			Feed(4021, Body4021Weapon(S->PeerIdxA, Item));
			if (V == EF2::TwoSwords)
			{
				S->SpawnPeer(S->PeerIdxB2, FString::Printf(TEXT("СоседВ%d"), S->PeerIdxB2), S->HomeTile + FIntPoint(1, 2));
				Feed(4021, Body4021Weapon(S->PeerIdxB2, S->PeerWeaponSword));
			}
			S->AIdx = Role == 0 ? (int32)Nn->SelfUserNum : S->PeerIdxA;
			S->BIdx = V == EF2::TwoSwords ? S->PeerIdxB2 : -1;
			S->bRoleAttacker = Role == 0;
			S->RidA = S->NewRid();
			TArray<uint8> Mob = AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, 1), /*State*/1, kSpdDenom);
			AsdaBytes::WrI32(&Mob[9], kF2MobHp);
			Feed(4013, Mob);
		},
		[S, Feed, Kind, V]()
		{
			UAsdaWorldSubsystem* WSs = S->WS(); AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(S->RidA);
			const double Now = S->WNow();
			const bool bRanged = Kind != 0;
			const int32 Wt = Kind == 0 ? 1 : Kind == 1 ? S->BowWt : 11;
			if (S->Ts < 0.0)
			{
				if (S->El() < kQuietS || !M || !C || !WSs) { return false; }
				S->Ts = Now; S->HpPrev = M->CurHp;
				if (S->bRoleAttacker)
				{
					// «Отправка» 4026: свой замах с кадра 0, держится до конца прогона (как контроллер в автобое); расписание отрезков.
					C->FaceGameDir(AsdaCoord::UeToGame2D(M->GetActorLocation()) - C->GetGamePosXZ());
					C->StartAttackSwing(Wt, -1.f);
					TArray<float> Segs; C->NaturalSegmentsS(12, Segs);
					S->F2NatSegs.Add(Wt, Segs);
					FString L; for (const float X : Segs) { L += FString::Printf(TEXT(" %.3f"), X); }
					UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ Ф2 расписание %s (подтип %d): отрезки [%s ] с"), Kind == 0 ? TEXT("меч") : Kind == 1 ? TEXT("лук") : TEXT("баллиста"), Wt, *L);
				}
				const TArray<float>* Segs = S->F2NatSegs.Find(Wt);
				if (!Segs || Segs->Num() < 9) { S->Expect(false, TEXT("Ф2: нет расписания отрезков (роль атакующего не прошла)")); S->EndR = 0.0; return true; }
				// Пакеты: 4029 №j — ответ на «отправку» j (начало отрезка j атакующего) через 0.2 с; интервалы — множитель варианта.
				const int32 N = V == EF2::CatchUp ? 12 : (bRanged ? 8 : 6);   // число 4029
				static const float kJit[] = { 1.12f, 0.9f, 1.1f, 0.88f, 1.12f, 0.9f, 1.1f, 0.9f };
				double SendNat = 0.0, Arr = (V == EF2::BigRtt) ? 0.8 : 0.2;   // естественная «отправка» атакующего и приход ответа (с от Ts)
				// Модель сервера: очередь атакующего (по 2/1 за замах) и HP после израсходованных ударов.
				TArray<int32> SimQ; int32 SimHp = kF2MobHp;
				auto Batch = [bRanged](int32 J, int32 Base) -> TArray<int32>
				{
					TArray<int32> Q;
					const bool bPack = bRanged ? (J % 4 == 0) : (J % 2 == 0);   // пачка из 4 — раз в 2 (меч) / 4 (дальнобойное) замаха
					if (bPack) { const int32 B0 = Base + (bRanged ? J : J * 2); for (int32 k = 0; k < 4; ++k) { Q.Add(B0 + k + 1); } }
					return Q;
				};
				for (int32 j = 0; j < N; ++j)
				{
					S->F2SendFromTs.Add(SendNat);
					TArray<int32> Q = Batch(j, 0);
					const bool bFed = !(V == EF2::Skip && j == 3) && !(V == EF2::MidBatch && j == 0);
					// Сервер расходует удары только принятого замаха (пропущенный 4028 — ничего; вход посреди пачки — сервер расходовал
					//   и до того, наблюдатель этого не видел).
					SimQ.Append(Q);
					if (bFed || V == EF2::MidBatch)
					{
						const int32 Take = FMath::Min(bRanged ? 1 : 2, SimQ.Num());
						for (int32 k = 0; k < Take; ++k) { SimHp -= SimQ[0]; SimQ.RemoveAt(0); }
					}
					if (V == EF2::MidBatch && j == 0) { const int32 Extra = FMath::Min(2, SimQ.Num()); for (int32 k = 0; k < Extra; ++k) { SimHp -= SimQ[0]; SimQ.RemoveAt(0); } Q.Reset(); }
					if (bFed) { S->AddEv(Arr, 4029, Body4029(S->AIdx, S->RidA, SimHp, V == EF2::MidBatch && j == 1 ? TArray<int32>() : Q)); S->F2LastServerHp = SimHp; }
					const float F = V == EF2::Jitter ? kJit[j % 8] : V == EF2::Fast ? 0.7f : V == EF2::Slow ? 1.35f : V == EF2::CatchUp ? 0.6f : 1.f;   // множитель интервала
					SendNat += (*Segs)[j];
					Arr += (double)(*Segs)[j] * F;
				}
				if (V == EF2::TwoSwords)
				{
					// Второй мечник B: та же серия со сдвигом 0.37 с, метки 101…112; HP общий (сервер расходует оба).
					double ArrB = 0.57;
					TArray<int32> SimQB;
					for (int32 j = 0; j < N; ++j)
					{
						TArray<int32> Q = Batch(j, 100);
						SimQB.Append(Q);
						const int32 Take = FMath::Min(2, SimQB.Num());
						for (int32 k = 0; k < Take; ++k) { SimHp -= SimQB[0]; SimQB.RemoveAt(0); }
						S->AddEv(ArrB, 4029, Body4029(S->BIdx, S->RidA, SimHp, Q));
						ArrB += (double)(*Segs)[j];
					}
					// HP пакетов A (поданы выше) не учитывали расход B — пересчитать HP всех событий по порядку прихода.
					S->Evs.Sort([](const FAsdaF1Ev& A, const FAsdaF1Ev& B) { return A.At < B.At; });
					TArray<int32> QA, QB; int32 Hp = kF2MobHp;
					for (FAsdaF1Ev& E : S->Evs)
					{
						const int32 Atk = (int32)AsdaBytes::RdI16(&E.Body[0]);
						TArray<int32>& Qx = (Atk == S->BIdx) ? QB : QA;
						for (int32 k = 0; k < 4; ++k) { const int32 Vv = AsdaBytes::RdI32(&E.Body[8 + 4 * k]); if (Vv != 0) { Qx.Add(Vv); } }
						for (int32 k = 0; k < 2 && Qx.Num() > 0; ++k) { Hp -= Qx[0]; Qx.RemoveAt(0); }
						AsdaBytes::WrI32(&E.Body[4], Hp);
					}
					S->F2LastServerHp = Hp;
				}
				S->EndR = Arr + (bRanged ? 2.6 : 1.6);
			}
			const double R = Now - S->Ts;
			// ★ R5-fix7: за один опрос — события одного момента расписания (после длинного кадра пакеты разных моментов не сливаются).
			{
				double MinAt = 1e18;   // ближайший неподанный момент расписания
				for (const FAsdaF1Ev& E : S->Evs) { if (!E.bFed) { MinAt = FMath::Min(MinAt, E.At); } }
				if (MinAt < 1e17 && R >= MinAt)
				{
					for (FAsdaF1Ev& E : S->Evs) { if (!E.bFed && FMath::IsNearlyEqual(E.At, MinAt, 1e-6)) { E.bFed = true; Feed(E.Op, E.Body); } }
				}
			}
			if (M)
			{
				if (S->HpPrev >= 0 && M->CurHp > S->HpPrev) { ++S->HpRises; S->HpRiseMax = FMath::Max(S->HpRiseMax, M->CurHp - S->HpPrev); }
				S->HpPrev = M->CurHp; S->FinalHp = M->CurHp;
			}
			if (!S->bRoleAttacker)
			{
				if (AAsdaPlayerCharacter* A = S->AChar(); A && A->IsPeerPacketDriven())
				{
					const float Cf = A->GetPeerTempoCoef();
					S->F2CoefMin = FMath::Min(S->F2CoefMin, Cf); S->F2CoefMax = FMath::Max(S->F2CoefMax, Cf);
				}
				// Пропуск не рвёт серию: между первым и последним 4029 сосед всё время в замахе.
				const double First = S->Evs.Num() > 0 ? S->Evs[0].At : 0.0, Last = S->Evs.Num() > 0 ? S->Evs.Last().At : 0.0;
				if (AAsdaPlayerCharacter* A = S->AChar(); A && R > First + 0.1 && R < Last && !A->IsAttackLoopAlive()) { S->bF2SwingGap = true; }
			}
			return R > S->EndR;
		},
		[S, Kind, Role, V]()
		{
			UAsdaWorldSubsystem* WSs = S->WS();
			if (!WSs || S->Ts < 0.0) { S->Expect(false, TEXT("Ф2: прогон не начался")); return; }
			const bool bRanged = Kind != 0;
			const int32 Wt = Kind == 0 ? 1 : Kind == 1 ? S->BowWt : 11;
			const double Tol = S->FrameTol() + 0.005;
			const bool bControl = V == EF2::Control;
			// Начала отрезков на ЭТОМ экране: атакующий — его «отправки» (клип с кадра 0 номинальным темпом), наблюдатель — журнал
			//   отрезков соседа.
			auto Collect = [&](int32 Atk, const AAsdaPlayerCharacter* A, TArray<FAsdaF2Ev>& Out)
			{
				TArray<double> Starts; TArray<float> Rates;
				if (Atk == S->AIdx && S->bRoleAttacker) { for (const double X : S->F2SendFromTs) { Starts.Add(S->Ts + X); Rates.Add(1.f); } }
				else if (A) { for (const AAsdaPlayerCharacter::FAsdaPeerSegSeen& G : A->GetPeerSegLog()) { if (G.StartS >= S->Ts - 0.001) { Starts.Add(G.StartS); Rates.Add(G.Rate); } } }
				TArray<TPair<int32, double>> Raw;   // значение → мир-время
				if (bRanged) { for (int32 i = S->ShotFrom; i < WSs->ShotsSeen.Num(); ++i) { const auto& E = WSs->ShotsSeen[i]; if (E.Rid == S->RidA && E.AttackerUserIdx == Atk) { Raw.Add({ E.Raw, E.AtS }); } } }
				else { for (int32 i = S->SeenFrom; i < WSs->HitResultsSeen.Num(); ++i) { const auto& E = WSs->HitResultsSeen[i]; if (E.Rid == S->RidA && E.AttackerUserIdx == Atk) { Raw.Add({ E.Raw, E.AtS }); } } }
				int32 PrevSeg = -1, InSeg = 0;
				for (const TPair<int32, double>& X : Raw)
				{
					FAsdaF2Ev& E = Out.AddDefaulted_GetRef();
					E.Val = X.Key; E.AtS = X.Value; E.FromTs = X.Value - S->Ts;
					// Граница отрезков: у ближнего второй кадр удара отрезка — В его точке отправки (конец отрезка, B[нечётный]), у
					//   дальнобойного кадр прохода цикла баллисты — в начале отрезка (точка перемотки). Событие в пределах кадра от
					//   границы относится к отрезку, которому принадлежит по плану.
					// ★ R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: «в пределах кадра от границы» — по фактической длине кадра прогона, а не
					//   0,03 с. Второй кадр удара отрезка стоит на его конце; при кадре 55–100 мс цифра показывалась через 0,037–0,055 с
					//   после начала следующего отрезка и приписывалась ему («2(отр 1, ждём 0)»), за ней краснело сравнение ролей.
					//   Не больше 0,25 с: первый кадр удара отрезка стоит на +0,48 с и с границей не путается.
					// ★ ревизия R5-fix7: длина ТОГО кадра, в котором показана эта цифра (журнал кадров шага), а не худшего кадра шага —
					//   один длинный кадр не расширяет границу остальным цифрам.
					const double Edge = bRanged ? 0.03 : FMath::Clamp(S->FrameDtAt(X.Value) + 0.002, 0.03, 0.25);   // R5-fix7: only melee (hit frame at segment END); ranged keeps 0.03 s
					for (int32 k = 0; k < Starts.Num(); ++k) { if (bRanged ? (Starts[k] <= X.Value + Edge) : (Starts[k] < X.Value - Edge)) { E.Seg = k; } }
					if (E.Seg >= 0) { E.Rel = X.Value - Starts[E.Seg]; E.Rate = Rates[E.Seg]; }
					InSeg = (E.Seg == PrevSeg) ? InSeg + 1 : 0; PrevSeg = E.Seg; E.InSeg = InSeg;
				}
			};
			TArray<FAsdaF2Ev> Ev, EvB;
			Collect(S->AIdx, S->AChar(), Ev);
			if (V == EF2::TwoSwords)
			{
				const AAsdaPlayerCharacter* B = Cast<AAsdaPlayerCharacter>(WSs->GetPlayerActorByUserNum(S->BIdx));
				Collect(S->BIdx, B, EvB);
			}
			FString Line; for (const FAsdaF2Ev& E : Ev) { Line += FString::Printf(TEXT(" %d@%.3f[отр %d +%.3f ×%.2f]"), E.Val, E.FromTs, E.Seg, E.Rel, E.Rate); }
			const int32 Flushes = WSs->LedgerFlushCount - S->FlushFrom;
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ Ф2 замер: %s:%s | подъёмов полосы %d (макс %d) | итог HP %d (сервер %d) | применений по потолку %d | коэффициент темпа %.3f…%.3f | разрыв замаха %s"),
				bRanged ? TEXT(" выстрелы") : TEXT(" цифры"), *Line, S->HpRises, S->HpRiseMax, S->FinalHp, S->F2LastServerHp, Flushes,
				S->F2CoefMin < 90.f ? S->F2CoefMin : 1.f, S->F2CoefMax >= 0.f ? S->F2CoefMax : 1.f, S->bF2SwingGap ? TEXT("был") : TEXT("нет"));
			S->F2Out.Add(FString::Printf(TEXT("%d/%d/%d"), Wt, Role, (int32)V), Ev);
			// Ожидаемый отрезок значения (без сдвига на проход): меч — 2 удара на отрезок, дальнобойное — 1.
			auto WantSeg = [bRanged, V](int32 Val) -> int32
			{
				const int32 Per = bRanged ? 1 : 2;
				if (V == EF2::MidBatch) { return (Val - 5) / Per + 1; }   // первая полная пачка (5…8) — на отрезке 4029 №2 (отрезок 1)
				const int32 Seg = (Val - 1) / Per;
				return (V == EF2::Skip && Seg >= 3) ? Seg + 1 : Seg;      // отрезок №3 без ответа — дальше на отрезок позже
			};
			const int32 WantN = V == EF2::Skip ? 10 : V == EF2::MidBatch ? 8 : V == EF2::CatchUp ? 24 : (bRanged ? 8 : 12);
			if (!bControl)
			{
				// Все значения показаны по порядку, каждое — на кадре удара СВОЕГО отрезка (не сдвинуто на проход).
				int32 BadSeg = 0, BadOrder = 0; FString BadList;
				for (int32 i = 0; i < Ev.Num(); ++i)
				{
					if (i > 0 && Ev[i].Val < Ev[i - 1].Val) { ++BadOrder; }
					// [ОРИГИНАЛ] Пакеты РЕЖЕ, чем позволяет нижний предел темпа (Slow ×1.35 при пределе 0.8): клип соседа обгоняет пакеты, и
					//   один отрезок проходит без ответа — у эталона значения очереди снимаются на следующих кадрах удара (не раньше прихода
					//   пакета). Поэтому для Slow законен сдвиг РОВНО на один отрезок позже (раньше — никогда, больше одного — дефект).
					const int32 Wsg = WantSeg(Ev[i].Val);
					const bool bSegOk = (V == EF2::Slow) ? (Ev[i].Seg == Wsg || Ev[i].Seg == Wsg + 1) : (Ev[i].Seg == Wsg);
					if (!bSegOk) { ++BadSeg; BadList += FString::Printf(TEXT(" %d(отр %d, ждём %d)"), Ev[i].Val, Ev[i].Seg, Wsg); }
				}
				const int32 FirstVal = Ev.Num() > 0 ? Ev[0].Val : -1;
				S->Expect(Ev.Num() == WantN && BadOrder == 0 && FirstVal == (V == EF2::MidBatch ? 5 : 1),
					FString::Printf(TEXT("показано %d из %d значений по порядку (нарушений %d), первое %d"), Ev.Num(), WantN, BadOrder, FirstVal));
				// ★ Ревизия Ф2 (мелочь 5): без ослабления для Fast/Slow — каждое значение на своём отрезке (догон без обрыва, ВАЖНО 2).
				S->Expect(BadSeg == 0, FString::Printf(TEXT("каждое %s — на кадре удара своего отрезка, сдвига на проход нет (ошибок %d:%s)"),
					bRanged ? TEXT("выстрел") : TEXT("цифра"), BadSeg, *BadList));
				S->Expect(S->HpRises == 0, FString::Printf(TEXT("полоса за бой ни разу не поднялась (подъёмов %d, макс %d) — ревизия Ф1 п.1"), S->HpRises, S->HpRiseMax));
				S->Expect(Flushes == 0, FString::Printf(TEXT("бой > 4.5 с без применения удержанного по потолку (%d) — ревизия Ф1 п.1"), Flushes));
				S->Expect(S->FinalHp == S->F2LastServerHp, FString::Printf(TEXT("итог полосы = серверному HP (%d против %d) — двойного вычета нет"), S->FinalHp, S->F2LastServerHp));
				if (Role == 1)
				{
					S->Expect(S->F2CoefMin >= 0.8f - 1e-3f && S->F2CoefMax <= 1.25f + 1e-3f,
						FString::Printf(TEXT("коэффициент темпа соседа в пределах 0.8…1.25 (%.3f…%.3f)"), S->F2CoefMin, S->F2CoefMax));
					float RMin = 99.f, RMax = 0.f;
					double LastClaimed = -1.0;   // начало последнего отрезка, взятого пакетом
					if (const AAsdaPlayerCharacter* A = S->AChar()) { for (const auto& G : A->GetPeerSegLog()) { if (G.StartS >= S->Ts - 0.001) { RMin = FMath::Min(RMin, G.Rate); RMax = FMath::Max(RMax, G.Rate); if (G.bClaimed) { LastClaimed = G.StartS; } } } }
					// ★ Ревизия Ф2 (ВАЖНО 2): догон ускоряет НОВЫЙ отрезок сверх 1.25 (до 3.0), темп по интервалу — по-прежнему 0.8…1.25.
					const float RateMax = V == EF2::CatchUp ? 3.0f : 1.25f;
					S->Expect(RMin >= 0.8f - 1e-3f && RMax <= RateMax + 1e-3f, FString::Printf(TEXT("скорость клипа отрезков соседа в пределах 0.8…%.2f номинала (%.3f…%.3f)"), RateMax, RMin, RMax));
					if (V == EF2::CatchUp)
					{
						// Отставание не копится: отрезок последнего пакета начинается не позже чем через 2 естественных отрезка после его прихода.
						const TArray<float>* Segs2 = S->F2NatSegs.Find(Wt);
						const double Nat = (Segs2 && Segs2->Num() > 2) ? (double)(*Segs2)[2] : 1.0;
						const double LastArr = S->Ts + (S->Evs.Num() > 0 ? S->Evs.Last().At : 0.0);
						S->Expect(LastClaimed > 0.0 && LastClaimed - LastArr <= 2.0 * Nat + Tol,
							FString::Printf(TEXT("догон: отрезок последнего 4029 начат через %.3f с после прихода (предел 2 отрезка = %.3f с) — отставание не копится"), LastClaimed - LastArr, 2.0 * Nat));
					}
					S->Expect(!S->bF2SwingGap, TEXT("замах соседа не прерывался между первым и последним 4029 (пропуск серию не рвёт)"));
				}
				if (V == EF2::Skip)
				{
					int32 InGap = 0; for (const FAsdaF2Ev& E : Ev) { if (E.Seg == 3) { ++InGap; } }
					S->Expect(InGap == 0, FString::Printf(TEXT("отрезок без ответа (№3) — без цифр (%d)"), InGap));
				}
				if (V == EF2::TwoSwords)
				{
					int32 BadB = 0; for (const FAsdaF2Ev& E : EvB) { if (E.Seg != (E.Val - 101) / 2) { ++BadB; } }
					S->Expect(EvB.Num() == 12 && BadB == 0, FString::Printf(TEXT("второй мечник: показано %d из 12, не на своём отрезке %d"), EvB.Num(), BadB));
				}
			}
			// ── Сравнение ролей: наблюдатель против атакующего (тот же подтип, полная серия) ──
			const TArray<FAsdaF2Ev>* Att = S->F2Out.Find(FString::Printf(TEXT("%d/0/%d"), Wt, (int32)EF2::Full));
			if (Role == 1 && Att && V != EF2::TwoSwords)
			{
				// Пара по (отрезок, номер в отрезке): у входа посреди пачки значения другие, а кадры отрезка — те же.
				auto FindAtt = [Att](int32 Seg, int32 InSeg) -> const FAsdaF2Ev* { for (const FAsdaF2Ev& E : *Att) { if (E.Seg == Seg && E.InSeg == InSeg) { return &E; } } return nullptr; };
				int32 Cmp = 0, Bad = 0; double Worst = 0.0; FString BadList;
				auto FindAttVal = [Att](int32 Val) -> const FAsdaF2Ev* { for (const FAsdaF2Ev& E : *Att) { if (E.Val == Val) { return &E; } } return nullptr; };
				for (const FAsdaF2Ev& E : Ev)
				{
					if (bControl)
					{
						// Контроль: замах соседа по времени (отрезков нет) — номинальный темп с прихода первого 4029, т.е. клип наблюдателя =
						//   клип атакующего со сдвигом 0.2 с; верное место цифры — момент атакующего + 0.2 с.
						const FAsdaF2Ev* A = FindAttVal(E.Val);
						if (!A) { continue; }
						++Cmp;
						const double D = FMath::Abs(E.FromTs - (A->FromTs + 0.2));
						if (D > Tol) { ++Bad; BadList += FString::Printf(TEXT(" %d:%.3f"), E.Val, D); }
						Worst = FMath::Max(Worst, D);
						continue;
					}
					const FAsdaF2Ev* A = FindAtt(E.Seg, E.InSeg);
					if (!A) { continue; }
					++Cmp;
					// Баллиста в проходах цикла: атакующий стреляет по приходу ответа (кадр удара — точка перемотки, ответ после неё), а
					//   наблюдатель — в точке перемотки своего отрезка; совпадает мировое время от «отправки» 4026, а не сдвиг от отрезка.
					const bool bAbs = Kind == 2 && E.Seg >= 1;
					// Темп: сравнивается клип-расстояние от начала отрезка (скорость × время), у атакующего скорость 1.
					const double D = bAbs ? FMath::Abs(E.FromTs - A->FromTs) : FMath::Abs(E.Rel * E.Rate - A->Rel * A->Rate);
					const double TolE = Tol * FMath::Max(1.f, E.Rate);
					if (D > TolE) { ++Bad; BadList += FString::Printf(TEXT(" отр%d#%d:%.3f"), E.Seg, E.InSeg, D); }
					Worst = FMath::Max(Worst, D);
				}
				if (!bControl)
				{
					// ★ Ревизия Ф2 (мелочь 5): пары по всем значениям серии атакующего (кроме последнего); у Slow отрезок без ответа (см. выше)
					//   законно выпадает из пар — минус его два кадра удара.
					const int32 NeedPairs = FMath::Min(WantN, Att->Num()) - 1 - (V == EF2::Slow ? 2 : 0);
					S->Expect(Cmp >= NeedPairs && Bad == 0,
						FString::Printf(TEXT("ОДИНАКОВО у ролей: %s относительно начала отрезка совпадают (%d пар, расхождений %d, худшее %.3f с, допуск 1 кадр %.3f)%s"),
							bRanged ? TEXT("выстрелы") : TEXT("цифры"), Cmp, Bad, Worst, Tol, *BadList));
					if (Kind == 2)
					{
						int32 BadPhase = 0; for (const FAsdaF2Ev& E : Ev) { if (E.Seg >= 1 && E.Rel > Tol) { ++BadPhase; } }
						S->Expect(BadPhase == 0, FString::Printf(TEXT("баллиста соседа стреляет в точке перемотки своего прохода (вне фазы %d)"), BadPhase));
					}
				}
				else
				{
					S->Expect(Bad >= 2, FString::Printf(TEXT("контроль SwingPlanFromPackets 0: цифры соседа не на кадрах удара отрезка (расхождений %d из %d, худшее %.3f с) — дефект виден"), Bad, Cmp, Worst));
				}
			}
		});
	};
	// Роль атакующего — первой: она снимает расписание отрезков оружия для наблюдателя.
	for (const int32 Kind : { 0, 1, 2 })
	{
		AddF2(Kind, 0, EF2::Full);
		AddF2(Kind, 1, EF2::Full);
	}
	AddF2(0, 1, EF2::Skip);
	AddF2(0, 1, EF2::MidBatch);
	AddF2(0, 1, EF2::Jitter);
	AddF2(0, 1, EF2::Fast);
	AddF2(0, 1, EF2::Slow);
	AddF2(0, 1, EF2::TwoSwords);
	AddF2(0, 1, EF2::Control);
	// ★★★ 17.10.2026 (ревизия Ф2, мелочь 5): догон ×0.6 на 12 пакетов (ВАЖНО 2), большой RTT.
	AddF2(0, 1, EF2::CatchUp);
	AddF2(0, 1, EF2::BigRtt);

	// ── ★ Ревизия Ф2 (ВАЖНО 1): смена цели соседа при очереди отрезков — значения новой цели на БЛИЖАЙШИХ кадрах ──
	//   Меч, пакеты A чаще естественного (×0.6 — копится очередь отрезков), с №4 цель — моб B. [ОРИГИНАЛ] новая серия: очередь
	//   сброшена и наполнена заново (network.c:42069–42081) — первая цифра по B не позже одного естественного отрезка после пакета.
	Add(TEXT("Ф2 смена цели соседа при очереди отрезков (ВАЖНО 1)"), 20.f, [S, Feed, T]()
	{
		S->F1Reset(1, 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.SwingPlanFromPackets"), 1);
		Feed(4021, Body4021Weapon(S->PeerIdxA, S->PeerWeaponSword));
		S->AIdx = S->PeerIdxA; S->bRoleAttacker = false;
		S->RidA = S->NewRid(); S->RidB = S->NewRid();
		TArray<uint8> Ma = AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, 1), /*State*/1, kSpdDenom); AsdaBytes::WrI32(&Ma[9], kF2MobHp); Feed(4013, Ma);
		TArray<uint8> Mb = AsdaTestPackets::MobSpawn4013(S->RidB, kMobType, T(-2, 1), /*State*/1, kSpdDenom); AsdaBytes::WrI32(&Mb[9], kF2MobHp); Feed(4013, Mb);
		S->TPhase = -1.0;
	}, [S, Feed]()
	{
		const double Now = S->WNow();
		if (S->Ts < 0.0)
		{
			if (S->El() < kQuietS || !S->Mob(S->RidA) || !S->Mob(S->RidB)) { return false; }
			S->Ts = Now;
			const TArray<float>* Segs = S->F2NatSegs.Find(1);
			double Arr = 0.2;
			for (int32 j = 0; j < 10; ++j)   // ★ №8/№9 — пустые продолжения: остаток очереди новой цели снимают их отрезки
			{
				const bool bB = j >= 4;
				const int32 Jl = bB ? j - 4 : j;   // номер в серии своей цели
				TArray<int32> Q;
				if (Jl % 2 == 0 && Jl < 4) { const int32 B0 = (bB ? 100 : 0) + Jl * 2; for (int32 k = 0; k < 4; ++k) { Q.Add(B0 + k + 1); } }
				S->AddEv(Arr, 4029, Body4029(S->PeerIdxA, bB ? S->RidB : S->RidA, kF2MobHp - 50, Q));
				Arr += (double)((Segs && Segs->IsValidIndex(j)) ? (*Segs)[j] : 1.f) * 0.6;
			}
			S->EndR = Arr + 2.5;
		}
		const double R = Now - S->Ts;
		for (int32 i = 0; i < S->Evs.Num(); ++i)
		{
			FAsdaF1Ev& E = S->Evs[i];
			if (!E.bFed && R >= E.At) { E.bFed = true; Feed(E.Op, E.Body); if (i == 4) { S->TPhase = Now; } }
		}
		return R > S->EndR;
	}, [S]()
	{
		const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> OnB = S->SeenOn(S->RidB);
		const TArray<float>* Segs = S->F2NatSegs.Find(1);
		const double Nat = (Segs && Segs->Num() > 4) ? (double)(*Segs)[4] : 1.0;
		const double First = OnB.Num() > 0 ? OnB[0].AtS - S->TPhase : 99.0;
		bool bOnlyB = true; int32 Prev = 0; bool bOrder = true;
		for (const auto& E : OnB) { bOnlyB &= E.Raw > 100; bOrder &= E.Raw > Prev; Prev = E.Raw; }
		S->Expect(OnB.Num() == 8 && bOnlyB && bOrder && OnB[0].Raw == 101,
			FString::Printf(TEXT("по новой цели показаны 8 значений 101…108 по порядку, хвоста прошлой цели нет (показано %d, первое %d)"), OnB.Num(), OnB.Num() > 0 ? OnB[0].Raw : -1));
		S->Expect(First >= 0.0 && First <= Nat + S->FrameTol() + 0.05,
			FString::Printf(TEXT("первая цифра по новой цели через %.3f с после её пакета (предел — естественный отрезок %.3f с): очередь прошлой серии снята"), First, Nat));
	});

	// ── ★ Ревизия Ф2 (ВАЖНО 3): навык соседа посреди серии снимает цикл замаха (эталон — состояния 24/25) ──
	Add(TEXT("Ф2 навык соседа посреди серии (ВАЖНО 3)"), 16.f, [S, Feed, T]()
	{
		S->F1Reset(1, 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.SwingPlanFromPackets"), 1);
		Feed(4021, Body4021Weapon(S->PeerIdxA, S->PeerWeaponSword));
		S->AIdx = S->PeerIdxA; S->bRoleAttacker = false;
		S->RidA = S->NewRid();
		TArray<uint8> Ma = AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, 1), /*State*/1, kSpdDenom); AsdaBytes::WrI32(&Ma[9], kF2MobHp); Feed(4013, Ma);
		S->Phase = 0; S->bFlagA = false; S->bFlagB = false; S->TPhase = -1.0;
	}, [S, Feed]()
	{
		const double Now = S->WNow();
		AAsdaPlayerCharacter* A = S->AChar();
		if (S->Ts < 0.0)
		{
			if (S->El() < kQuietS || !S->Mob(S->RidA) || !A) { return false; }
			S->Ts = Now;
		}
		const double R = Now - S->Ts;
		if (S->Phase == 0) { S->Phase = 1; Feed(4029, Body4029(S->PeerIdxA, S->RidA, kF2MobHp - 10, { 1, 2, 3, 4 })); }
		if (S->Phase == 1 && R >= 1.0) { S->Phase = 2; Feed(4029, Body4029(S->PeerIdxA, S->RidA, kF2MobHp - 20, {})); }
		if (S->Phase == 2 && R >= 1.6)
		{
			S->Phase = 3; S->TPhase = Now;
			S->bFlagA = A && A->IsAttackLoopAlive();   // до навыка замах шёл
			Feed(5257, Body5257(S->PeerIdxA, S->SkillVis, S->RidA, 5, kF2MobHp - 25));
		}
		// Пока идёт клип навыка (до следующего 4029) — цикла замаха нет и пакетный режим снят.
		if (S->Phase == 3 && Now - S->TPhase >= 0.1 && A && (A->IsAttackLoopAlive() || A->IsPeerPacketDriven())) { S->bFlagB = true; }
		if (S->Phase == 3 && Now - S->TPhase >= 1.2) { S->Phase = 4; Feed(4029, Body4029(S->PeerIdxA, S->RidA, kF2MobHp - 30, { 5, 6, 7, 8 })); S->TPhase = Now; }
		if (S->Phase == 4 && Now - S->TPhase >= 0.2) { S->Phase = 5; S->QAfter1 = (A && A->IsAttackLoopAlive() && A->IsPeerPacketDriven()) ? 1 : 0; }
		return S->Phase == 5 && R > 4.5;
	}, [S]()
	{
		S->Expect(S->bFlagA, TEXT("до навыка сосед в замахе (отрезки от пакетов)"));
		S->Expect(!S->bFlagB, TEXT("навык соседа посреди серии: цикл замаха и пакетный режим сняты на всё время клипа навыка (эталон: состояния 24/25)"));
		S->Expect(S->QAfter1 == 1, TEXT("следующий 4029 после навыка начинает замах «взводом»"));
	});

	// ── ★ Ревизия Ф2 (мелочь 5): своя роль под СЕРВЕРНЫМИ ЧАСАМИ — цифры на кадрах удара своего клипа ──
	//   `ServerRhythm 1` + `RhythmFromAnim 1` (владелец ритма), серия открыта как 4026 (`NoteSent4026`): скорость отрезков задают
	//   часы. Каждая цифра обязана совпасть (1 кадр) с моментом, когда клип прошёл кадр удара (`ConsumeCrossedHitFrames`).
	// ★ R5-fix8 (14.11.2026): шаг идёт дважды — с правкой `asda.Combat.OwnHitRetimeKeepDue 1` и контролем 0. С правкой перестановка
	//   таймеров не заводит заново уже наступивший кадр удара (счётчик `DebugOwnRetimeRearmedDue` не растёт); при 0 — заводит, и
	//   цифра встаёт на кадр позже (прежнее поведение видно по счётчику; проверка «цифра на кадре удара» в контроле не ставится).
	for (const int32 KeyRetime : { 1, 0 })
	Add(FString::Printf(TEXT("Ф2 своя роль под серверными часами: цифры на кадрах удара%s"), KeyRetime ? TEXT("") : TEXT(", КОНТРОЛЬ OwnHitRetimeKeepDue 0")), 16.f, [S, Feed, T, KeyRetime]()
	{
		S->F1Reset(1, 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitRetimeKeepDue"), KeyRetime);
		if (const UAsdaWorldSubsystem* Wr = S->WS()) { S->RetimeKept0 = Wr->DebugOwnRetimeKeptDue; S->RetimeRearmed0 = Wr->DebugOwnRetimeRearmedDue; }
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.SwingPlanFromPackets"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RhythmFromAnim"), 1);
		UAsdaNetSubsystem* Nn = S->N(); AAsdaPlayerCharacter* C = S->Ch();
		if (!Nn || !C) { return; }
		Nn->SelfWeaponItemId = S->PeerWeaponSword; C->EquippedWeaponSubType = 1;
		S->AIdx = (int32)Nn->SelfUserNum; S->bRoleAttacker = true;
		S->RidA = S->NewRid();
		TArray<uint8> Ma = AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, T(2, 1), /*State*/1, kSpdDenom); AsdaBytes::WrI32(&Ma[9], kF2MobHp); Feed(4013, Ma);
		S->HitFrameTimes.Reset();
	}, [S, Feed]()
	{
		const double Now = S->WNow();
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(S->RidA);
		if (S->Ts < 0.0)
		{
			if (S->El() < kQuietS || !C || !M) { return false; }
			S->Ts = Now;
			if (UAsdaAttackRhythmSubsystem* Rh = S->GI() ? S->GI()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSent4026(); }
			C->FaceGameDir(AsdaCoord::UeToGame2D(M->GetActorLocation()) - C->GetGamePosXZ());
			C->StartAttackSwing(1, -1.f);
			TArray<int32> Dummy; C->ConsumeCrossedHitFrames(Dummy);   // отсчёт кадров с «взвода»
			for (int32 j = 0; j < 5; ++j)
			{
				TArray<int32> Q; if (j % 2 == 0) { for (int32 k = 0; k < 4; ++k) { Q.Add(j * 2 + k + 1); } }
				S->AddEv(0.25 + 1.3 * j, 4029, Body4029(S->AIdx, S->RidA, kF2MobHp - 40 * j, Q));
			}
			S->EndR = 0.25 + 1.3 * 5 + 1.5;
		}
		if (C) { TArray<int32> St; const int32 Nh = C->ConsumeCrossedHitFrames(St); for (int32 i = 0; i < Nh; ++i) { S->HitFrameTimes.Add(Now); } }
		const double R = Now - S->Ts;
		for (FAsdaF1Ev& E : S->Evs) { if (!E.bFed && R >= E.At) { E.bFed = true; Feed(E.Op, E.Body); } }
		return R > S->EndR;
	}, [S, KeyRetime]()
	{
		const TArray<UAsdaWorldSubsystem::FAsdaHitResultSeen> On = S->SeenOn(S->RidA);
		int32 Bad = 0; double Worst = 0.0; FString BadList;
		const double TolF = S->FrameTol() + 0.005;
		for (const auto& E : On)
		{
			double Best = 99.0; for (const double Hf : S->HitFrameTimes) { Best = FMath::Min(Best, FMath::Abs(E.AtS - Hf)); }
			Worst = FMath::Max(Worst, Best);
			if (Best > TolF) { ++Bad; BadList += FString::Printf(TEXT(" %d:%.3f"), E.Raw, Best); }
		}
		if (KeyRetime != 0) S->Expect(On.Num() >= 6 && Bad == 0, FString::Printf(TEXT("под серверными часами каждая своя цифра — на кадре удара клипа (цифр %d, кадров %d, мимо %d, худшее %.3f с, допуск %.3f)%s"),
			On.Num(), S->HitFrameTimes.Num(), Bad, Worst, TolF, *BadList));
		{
			const UAsdaWorldSubsystem* Wr = S->WS();
			const int32 Kept = Wr ? Wr->DebugOwnRetimeKeptDue - S->RetimeKept0 : -1;           // наступивших таймеров оставлено как есть
			const int32 Rearmed = Wr ? Wr->DebugOwnRetimeRearmedDue - S->RetimeRearmed0 : -1;  // наступивших таймеров заведено заново
			if (KeyRetime != 0)
			{
				S->Expect(Rearmed == 0 && Kept > 0, FString::Printf(TEXT("перестановка не заводит заново наступивший кадр удара: заведено %d (ждём 0), оставлено %d (ждём больше 0 — случай в прогоне возник), худшее отставание цифры %.3f с"), Rearmed, Kept, Worst));
			}
			else
			{
				S->Expect(Rearmed > 0 && Kept == 0, FString::Printf(TEXT("контроль ключа 0: наступивший кадр удара заведён заново %d раз(а) (оставлено %d), худшее отставание цифры %.3f с — прежнее поведение видно"), Rearmed, Kept, Worst));
			}
		}
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->StopAttackAnim(); }
	});

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 6.f, [S, Reset]()
	{
		Reset(1);   // мобы прогона убраны: таймеры показа по ним (слабые ссылки) больше ничего не выведут
		S->TPhase = FPlatformTime::Seconds();
	}, [S]() { return FPlatformTime::Seconds() - S->TPhase > 3.0 && S->MobsLeft() == 0; }, [S, Net]()
	{
		// ★ 30.10.2026 (порядок пакета): мобы прогона ушли из реестра — следующий самотест пакета получает свободные rid.
		S->Expect(S->MobsLeft() == 0, FString::Printf(TEXT("мобы прогона убраны из реестра мира (осталось %d)"), S->MobsLeft()));
		S->Expect(S->NOut == S->NOutF0, FString::Printf(TEXT("Ф1: свой персонаж в роли атакующего боевых пакетов не слал (%d)"), S->NOut - S->NOutF0));
		if (UAsdaWorldSubsystem* WSs = S->WS()) { WSs->bRecordHitResults = false; WSs->HitResultsSeen.Reset(); WSs->ShotsSeen.Reset(); }
		// ★ Ф2 (ревизия Ф1 п.3): состояние прогона не остаётся следующему самотесту пакета — соседи и добыча убраны штатно, своё
		//   оружие возвращено, запись порогов фаз выключена.
		S->DespawnAll();
		Net->SelfWeaponItemId = S->SavedWeaponItem;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		if (UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr) { Fx->bRecordPhases = false; Fx->PhasesFiredSeen.Reset(); }
		{
			const UAsdaWorldSubsystem* WSs = S->WS();
			S->Expect(WSs && WSs->DebugNetPlayersNum() == 0, FString::Printf(TEXT("фейковые соседи убраны штатно (в реестре осталось %d)"), WSs ? WSs->DebugNetPlayersNum() : -1));
		}
		Net->OnSelfExp.Remove(S->ExpHandle);
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->StopAttackAnim(); }
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaObserverTest, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ"), S->Ok, S->Fail,
			S->bControlAll ? TEXT(" (КОНТРОЛЬНЫЙ прогон с ключом ObserverSync 0: провалы ожидаемы — дефект виден)") : TEXT(""));
	});

	// ★ 18.10.2026 (проверка устойчивости разброса ролей): `asda.Combat.SelfTestObserverSync lootexp [N] [quit]` — только подготовка,
	//   «Ф1 подготовка», N раз (по умолчанию 5) тройка «Ф1 добыча 5005 и опыт 4038 при отложенной смерти» (атакующий, наблюдатель,
	//   контроль) и итог. Шаги те же самые (копии), меняется только их набор.
	if (KitArgs.Pos.Num() > 0 && KitArgs.Pos[0].Equals(TEXT("lootexp"), ESearchCase::IgnoreCase))
	{
		const int32 NRep = FMath::Clamp(KitArgs.Int(1, 5), 1, 50);   // число повторов тройки
		const FString LootName = F1Name(EF1::LootExp);
		TArray<FStep> Pre, Loot, Fin;   // подготовка | тройка сценария | итог
		for (const FStep& St : S->Steps)
		{
			if (St.Name == TEXT("подготовка") || St.Name == TEXT("Ф1 подготовка")) { Pre.Add(St); }
			else if (St.Name.Contains(LootName)) { Loot.Add(St); }
			else if (St.Name == TEXT("итог")) { Fin.Add(St); }
		}
		S->Steps = MoveTemp(Pre);
		for (int32 i = 0; i < NRep; ++i) { S->Steps.Append(Loot); }
		S->Steps.Append(Fin);
		UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ выборочный прогон: «%s» × %d (шагов %d)"), *LootName, NRep, S->Steps.Num());
	}
	// ★ R5-fix8 (14.11.2026): `asda.Combat.SelfTestObserverSync ownclock [N] [quit]` — только подготовки, N раз (по умолчанию 40)
	//   шаг «Ф2 своя роль под серверными часами» (с правкой, без контроля) и итог: проверка устойчивости «цифра на кадре удара клипа».
	if (KitArgs.Pos.Num() > 0 && KitArgs.Pos[0].Equals(TEXT("ownclock"), ESearchCase::IgnoreCase))
	{
		const int32 NRep = FMath::Clamp(KitArgs.Int(1, 40), 1, 200);   // число повторов шага
		TArray<FStep> Pre, Own, Fin;   // подготовки | шаг | итог
		for (const FStep& St : S->Steps)
		{
			if (St.Name.Contains(TEXT("подготовка"))) { Pre.Add(St); }
			else if (St.Name == TEXT("Ф2 своя роль под серверными часами: цифры на кадрах удара")) { Own.Add(St); }
			else if (St.Name == TEXT("итог")) { Fin.Add(St); }
		}
		S->Steps = MoveTemp(Pre);
		for (int32 i = 0; i < NRep; ++i) { S->Steps.Append(Own); }
		S->Steps.Append(Fin);
		UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ выборочный прогон: «своя роль под серверными часами» × %d (шагов %d)"), NRep, S->Steps.Num());
	}

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
			if (Wd && !AsdaSelfTestSelfReady::IsReady(Wd) && GEngine)
			{
				if (!P || !P->GetPawn()) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); }   // ★ 01.11.2026: карта — только без павна
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); St->StopRecording(); return; }   // ★ ревизия Ф2 (мелочь 7)
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		{   // ★ R5-fix7: журнал кадров шага (мир-время → длина кадра); опрос может сработать несколько раз за кадр — запись одна
			const double Tw = Wd->GetTimeSeconds();
			if (St->FrameLog.Num() == 0 || St->FrameLog.Last().Key != Tw) { St->FrameLog.Emplace(Tw, (double)Wd->GetDeltaSeconds()); }
		}
		if (St->bStepStarted) { St->MaxDt = FMath::Max(St->MaxDt, (double)Wd->GetDeltaSeconds()); }   // ★ Ф1: допуск «1 кадр»
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaObserverTest, Log, TEXT("САМОТЕСТ-НАБЛЮДАТЕЛЬ шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCombatObserverSync(
	TEXT("asda.Combat.SelfTestObserverSync"),
	TEXT("Самотест наблюдателя (синхронизация боя, Ф0 и Ф1 — лента попаданий моба, роли атакующего и наблюдателя): фейковый сосед бьёт мобов пакетами 4029/4017 — утечка очереди соседа между "
	     "мобами и по мёртвой цели, дельта HP соседа без «нашей» цифры и строки в чат, стили цифр соседа по мобу 1/2/6/5, поворот "
	     "моба к соседу, отказ 4029 код 1/2 гасит замах соседа. Каждый сценарий — и контроль с asda.Combat.ObserverSync 0 (дефект "
	     "виден); ключ 0 на старте — контрольный прогон. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Combat.SelfTestObserverSync"));
		FAsdaCombatObserverSyncSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
