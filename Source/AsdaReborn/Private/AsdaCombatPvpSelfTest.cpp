// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «PvP: ОБЫЧНАЯ АТАКА ИГРОКА ПО ИГРОКУ И РЕЖИМ ПОКАЗА ЦИФР» (17.10.2026, синхронизация боя, фазы Ф4 и Ф5).
//   [ПОРТ] ТЗ `Reports/combat_sync_2026-10-17/05_TZ_PVP_FIELD10112.md`. Только офлайн (`-AsdaNoNet`), исходящие — под
//   перехватом (`AsdaSelfTestKit::ArmCapture`, весь трафик), входящие — штатный приём `DebugFeedPacket` (= `HandlePacket` сокета).
//   ФЕЙКОВЫЕ соседи A и B — `DebugSpawnFakePeer` (путь снапшота), оружие им — штатным 4021, HP — снимком (`SetVitalsHp`).
//   Сценарии:
//     1) 4206: режим своего персонажа и соседа по userIdx; переключатель — функция эмоций 2/8 (`PlayEmotion(208)`, путь игрока):
//        тело 4206 (13 б: свой userIdx, charId, байт [88] дважды), локальный отбой, повторное включение;
//     2) своя атака мечом: второй клик (`EngagePlayerFromClick`) → 4201 (тело 18 б, userIdx цели i16, свои X/Z); 4202 код 1 →
//        подход/замах → удары на кадрах удара (стили 1/2/6, строки 20013/20012/20011, HP цели), 4203 на чётном ударе (в ответ —
//        4204 код 0 с двумя значениями, как сервер); отказ 4204 код 1 → нотис 1024 и стоп;
//     3) своя атака луком: 4203 на каждый выстрел;
//     4) коды отказа 4202 и 4204 → нотисы по таблице «событие → loc → канал» (ТЗ; разбор у `UAsdaWorldSubsystem::OnPvpAttackStart/
//        OnPvpHit`) и стоп/не стоп атаки;
//     5) жертва: сосед A бьёт меня — стили 9/0xA/0xE/0xD, строки 20242/20243/20014/20015, мой HP; в режиме показа 0 — тоже;
//     6) наблюдатель: A бьёт B — режим 1: цифры 9 без строк; режим 0: цифр нет, HP B всё равно вычитается;
//     7) режим 2: своих цифр нет, строка в чат есть; 4053 по B — серия A снята.
//   ★ 18.10.2026 (ревизия Ф4 `09_REVIEW_F4.md`): 8) ВАЖНО 1 — сосед A бьёт B, затем моба (4029): остатки PvP-очереди не выводятся
//     над B во время боя с мобом, новая PvP-серия после паузы начинается со своих значений; 9) ВАЖНО 2 — свой поздний 4204 после
//     конца своей атаки серию не заводит (следующий замах его значений не снимает); 4) код 5 у 4202/4204 — покой только НЕ
//     занятому (эталонный миснейм `charIsActionStateNonCombat`); итог — 4011 снимает режим соседа (мелочь 6), отбой режима — штатным
//     4206 (мелочь 8). Сценарии 8 и 9 — с контрольной парой: ключ `asda.Combat.PvpSeriesGuard 0` обязан показать дефект.
//   Своё состояние (номер, charId, оружие, режим показа, PvP-режимы, соседи) — возвращается в итоге.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись/чтение полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона
#include "AsdaOutgoingGate.h"                  // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaPlayerController.h"        // свой контроллер: второй клик, PvP-флаги
#include "World/AsdaPlayerCharacter.h"         // замах
#include "World/AsdaBlendAnimInstance.h"      // ★ 30.10: вздрагивание жертвы PvP-удара
#include "World/AsdaWorldSubsystem.h"          // соседи, наблюдения PvP-ударов, режим показа
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 25.10.2026: сброс серверных часов атаки между сценариями
#include "World/AsdaCoord.h"                   // клетки ↔ игра
#include "Items/AsdaItemTableSubsystem.h"      // оружие
#include "UI/AsdaInventorySubsystem.h"         // EAsdaEquipSlot — слот оружия в 4021
#include "UI/AsdaCharaInfoSubsystem.h"         // функция эмоций 2/8 — переключатель режима
#include "Resources/AsdaGameUserSettings.h"    // режим показа цифр (Ф5)
#include "UI/AsdaSkillUseSubsystem.h"          // ★ 18.10: флаг PvP-режима диспетчера навыков (штатный отбой 4206)
#include "UI/AsdaSkillDatabase.h"              // ★ 06.11: маска оружия навыка (сценарий 12)
#include "UI/AsdaNotice.h"                     // ★ 06.11: строка 8155 из таблицы языка
#include "UI/AsdaEffectsSubsystem.h"           // ★ 06.11: контроль и слоты DoT к итогу дуэли (сценарий 12)
#include "UI/AsdaDuelSubsystem.h"              // ★ 06.11: число арен после разрыва (сценарий 12)
#include "AsdaSessionReset.h"                  // ★ 06.11: сброс сессии при разрыве (сценарий 12)
#include "Npc/AsdaMobActor.h"                  // ★ 18.10: моб сценария 8 (сосед переходит к бою с мобом)
#include "AsdaTestPackets.h"                   // ★ 18.10: запись 4013 (спаун моба)
#include "World/AsdaProjectileActor.h"       // ★ 31.10: снаряды дальнобойного оружия (сценарий 11)
#include "EngineUtils.h"                      // ★ 31.10: TActorIterator
#include "World/AsdaAttackExit.h"             // ★ 01.11: общий выход своего в покой (сброс сценария)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPvpTest, Log, All);

void AsdaCancelDeferred4201(AAsdaPlayerController* PC);   // `AsdaPlayerController.cpp`: снять отложенную заявку 4201 (свой таймер)
bool AsdaIsDeferred4201Pending(AAsdaPlayerController* PC); // `AsdaPlayerController.cpp`: ждёт ли отложенная 4201

namespace AsdaPvpTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Combat.SelfTestPvp");   // имя команды
	constexpr int32 kSelfFallback = 4321;   // свой userNum, если не известен (как у прочих боевых самотестов)
	constexpr int32 kCharIdFallback = 970017;   // свой charId, если не известен
	constexpr int32 kHp = 50000;            // HP всех участников (крупно — урон не доводит до нуля)
	constexpr int32 kMobRid = 30991;        // ★ 18.10: rid моба сценария 8
	constexpr int32 kDuelSkill = 705;       // ★ 05.11: зонд навыка сценария 12 («Многократный выстрел», лук)
	constexpr int32 kDuelMob = 30992;       // ★ 05.11: моб-цель навыка после дуэли (сценарий 12)
	const TCHAR* const kGuardKey = TEXT("asda.Combat.PvpSeriesGuard");   // ★ 18.10: ключ отката ВАЖНО 1–2 (контрольная пара)

	// Один перехваченный исходящий пакет.
	struct FOut
	{
		uint16 Op = 0;          // опкод
		TArray<uint8> Payload;  // полезная нагрузка (без секрета и MD5 у конверта 'S')
		TCHAR Env = 0;          // конверт
		double AtS = 0.0;       // реальное время
	};

	struct FStep
	{
		FString Name;                  // имя шага
		float TimeoutSec = 20.f;       // потолок шага
		TFunction<void()> Start;       // начало (один раз)
		TFunction<bool()> Poll;        // опрос: true — шаг завершён
		TFunction<void()> Check;       // проверки
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
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0;                       // поколение перехвата
		TArray<FOut> Outs;                    // перехваченные исходящие
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false;
		int32 SavedWeapon = 0, SavedEquipped = 0;   // своё оружие до прогона
		int32 SavedMode = 1;                  // режим показа цифр до прогона
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		FVector2D Home = FVector2D::ZeroVector;
		int32 Self = -1;                      // свой userIdx
		int32 A = 7811, B = 7812;             // соседи: A — атакующий, B — цель
		int32 Sword = 0, Bow = 0, BowWt = 8;  // оружие
		TArray<int32> Spawned;                // соседи прогона
		// Сценарий.
		int32 HitFrom = 0, NoticeFrom = 0, OutFrom = 0;   // начала наблюдений сценария
		double TLastFeed = 0.0;               // последний поданный пакет (реальное время)
		int32 FlinchSeen = 0;                 // ★ 30.10: оценок позы с лёгшим вздрагиванием у наблюдаемой жертвы за шаг
		int32 N4203Answered = 0;              // сколько 4203 получили ответ 4204
		int32 Phase = 0;
		bool bFlag = false;
		// ★ 18.10.2026: сценарии 8/9 и итог.
		double TPhase = 0.0;                  // начало фазы (реальное время)
		int32 HitsMark = 0;                   // число ударов A→B (или своих по B) на отметке фазы
		int32 QAfter = -1;                    // длина PvP-очереди сразу после пакета-границы
		double T4201 = 0.0;                   // ★ 25.10.2026 (сценарий 10): момент первой 4201 (FPlatformTime)
		int32 ReqMs = -1;                     // ★ 25.10.2026 (сценарий 10): требование серверных часов после первой 4201, мс
		int32 HitsDuringMob = 0;              // ударов A→B во время боя A с мобом
		TArray<int32> NewSeriesRaw;           // значения новой серии A→B (первые)
		TSet<FString> ProjSeen;               // ★ 31.10.2026 (сценарий 11): снаряды, вылетевшие за шаг (имена акторов)
		int32 Wpn = 0;                        // ★ 31.10.2026 (сценарий 11): оружие шага (код предмета)
		int32 LoserHp = -1;                   // ★ 05.11.2026 (сценарий 12): HP проигравшего к итогу дуэли
		TMap<int32, int32> KindSent;          // ★ 06.11.2026 (сценарий 12): навык → сколько заявок 5256 ушло
		int32 AutoIdx = 0;                    // ★ 31.10.2026 (сценарий 11): первый перехваченный пакет без ответа автоответчика
		bool bDuelHadPvp = false;             // ★ 31.10.2026 (сценарий 11): запись своего PvP-режима до входа в дуэль была
		UAsdaNetSubsystem::FAsdaPvpState DuelSavedPvp;   // ★ 31.10.2026 (сценарий 11): её копия — вернётся на выходе
		bool bSavedPvpOn = false;             // свой PvP-режим до прогона
		bool bHadSelfPvpState = false;        // запись режима своего была до прогона
		UAsdaNetSubsystem::FAsdaPvpState SavedSelfPvp;   // её копия

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		// ★ 30.10: вздрагивание жертвы (поворот кости «4» лёг на оценке позы) — счёт за шаг.
		void TrackFlinch(AAsdaPlayerCharacter* V)
		{
			UAsdaBlendAnimInstance* I = (V && V->BaseSkel) ? Cast<UAsdaBlendAnimInstance>(V->BaseSkel->GetAnimInstance()) : nullptr;
			if (I && I->WasHitFlinchAppliedGT()) { ++FlinchSeen; }
		}
		AAsdaPlayerCharacter* Peer(int32 Idx) const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->GetPlayerActorByUserNum(Idx)) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPvpTest, Log, TEXT("САМОТЕСТ-PVP %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& Body) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, Body); } TLastFeed = FPlatformTime::Seconds(); }
		void SetMode(int32 M) { if (UAsdaGameUserSettings* S = UAsdaGameUserSettings::Get()) { S->DamageTextMode = M; } }
		int32 HpOf(int32 Idx) const { const UAsdaNetSubsystem* Nn = N(); const UAsdaNetSubsystem::FAsdaVitals* V = Nn ? Nn->VitalsByUserIdx.Find(Idx) : nullptr; return V ? V->HpCur : -1; }
		void SetHp(int32 Idx) { if (UAsdaNetSubsystem* Nn = N()) { Nn->SetVitalsHp(Idx, kHp, kHp, TEXT("самотест PvP"), /*bServerSnapshot=*/true); } }
		// Удары сценария (атакующий → жертва).
		TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> Hits(int32 Atk, int32 Vic) const
		{
			TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> Out;
			if (const UAsdaWorldSubsystem* W = WS())
			{
				for (int32 i = HitFrom; i < W->PvpHitsSeen.Num(); ++i) { if (W->PvpHitsSeen[i].Attacker == Atk && W->PvpHitsSeen[i].Victim == Vic) { Out.Add(W->PvpHitsSeen[i]); } }
			}
			return Out;
		}
		// Нотисы сценария.
		TArray<int32> Notices() const
		{
			TArray<int32> Out;
			if (const UAsdaWorldSubsystem* W = WS()) { for (int32 i = NoticeFrom; i < W->PvpNoticesSeen.Num(); ++i) { Out.Add(W->PvpNoticesSeen[i]); } }
			return Out;
		}
		bool HasNotice(int32 Id) const { return Notices().Contains(Id); }
		FString NoticesStr() const { FString S; for (const int32 I : Notices()) { S += FString::Printf(TEXT("%d "), I); } return S; }
		// Исходящие сценария с опкодом.
		// ★ 05.11: сколько заявок 5256 навыка Code ушло с отметки (код навыка — @24 полезной нагрузки).
		int32 SkillSent(int32 Code) const { int32 N = 0; for (const FOut& X : OutsOf(5256)) { if (X.Payload.Num() >= 26 && AsdaBytes::RdI16(&X.Payload[24]) == Code) { ++N; } } return N; }
		TArray<FOut> OutsOf(uint16 Op) const { TArray<FOut> R; for (int32 i = OutFrom; i < Outs.Num(); ++i) { if (Outs[i].Op == Op) { R.Add(Outs[i]); } } return R; }
		void Mark()
		{
			const UAsdaWorldSubsystem* W = WS();
			HitFrom = W ? W->PvpHitsSeen.Num() : 0; NoticeFrom = W ? W->PvpNoticesSeen.Num() : 0; OutFrom = Outs.Num();
			Phase = 0; bFlag = false; N4203Answered = 0; FlinchSeen = 0; ProjSeen.Reset(); AutoIdx = Outs.Num();
			TPhase = FPlatformTime::Seconds(); HitsMark = 0; QAfter = -1; HitsDuringMob = 0; NewSeriesRaw.Reset();
		}
		void SpawnPeer(int32 Idx, const FString& Name, const FIntPoint& Tile)
		{
			if (UAsdaWorldSubsystem* W = WS()) { W->DebugSpawnFakePeer(Idx, Name, AsdaCoord::TileCenterGame(Tile)); Spawned.AddUnique(Idx); }
		}
		void DespawnAll()
		{
			for (const int32 Idx : Spawned)
			{
				TArray<uint8> Bd; Bd.SetNumZeroed(2); AsdaBytes::WrI16(&Bd[0], (int16)Idx);
				Feed(4011, Bd);
			}
			Spawned.Reset();
		}
		// Свой персонаж — в покой, без цели и без PvP-серии.
		void CalmSelf()
		{
			if (AAsdaPlayerController* P = PC())
			{
				// ★ 01.11.2026 (изоляция пакета): общий выход в мирный покой — флаги боя/PvP, цикл клипа, стойка, слот навыка
				//   (`AsdaAttackExit::StopSelfActionToIdle`), затем цель снята: прежний бой следующему сценарию/самотесту не достаётся.
				AsdaAttackExit::StopSelfActionToIdle(GI(), TEXT("самотест: сброс"));
				P->ClearTarget();
				P->ExitCombatStance();
				P->GetWorldTimerManager().ClearTimer(P->DeferredAttackSendTimer);
				AsdaCancelDeferred4201(P);   // ★ 25.10.2026: отложенная заявка 4201 сценарию не достаётся
			}
			// ★ 25.10.2026 (`asda.Combat.RestartSeriesGate`): сценарии — независимые бои; без сброса серверных часов 4201 следующего
			//   сценария ждал бы длительности удара прошлого (как у живого сервера с N19), а сценарий ждёт заявку сразу.
			if (UAsdaAttackRhythmSubsystem* R = GI() ? GI()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { R->DebugReset(); }
			if (AAsdaPlayerCharacter* C = Ch()) { C->StopAttackAnim(); C->NetSnap(Home); }
			if (UAsdaWorldSubsystem* W = WS()) { W->ResetPvpSeries(Self, TEXT("самотест: сброс")); W->ResetPvpSeries(A, TEXT("самотест: сброс")); }
			if (AAsdaPlayerCharacter* PA = Peer(A)) { PA->StopAttackAnim(); }
		}
	};

	// 4206 (13 б): @0 i16 userIdx, @2 i32 charId, @6 u8 байт [88], @7 u8 режим, @8 i32 соперник/команда, @12 u8 команда.
	// ★ 05.11: 4304 (55 б): группа @0, победитель userNum @2 / charId @4, проигравший @8 / @10, тип @14 (2 — смерть), имена @15/@35.
	static TArray<uint8> Body4304(int16 Pvp, int32 WinU, int32 WinC, int32 LoseU, int32 LoseC, uint8 Type = 2)
	{
		TArray<uint8> P; P.SetNumZeroed(55);
		AsdaBytes::WrI16(P.GetData(), Pvp); AsdaBytes::WrI16(P.GetData() + 2, (int16)WinU); AsdaBytes::WrI32(P.GetData() + 4, WinC);
		AsdaBytes::WrI16(P.GetData() + 8, (int16)LoseU); AsdaBytes::WrI32(P.GetData() + 10, LoseC); P[14] = Type;
		const char* WN = "Winner"; const char* LN = "Loser";
		for (int32 i = 0; WN[i]; ++i) { P[15 + i] = (uint8)WN[i]; }
		for (int32 i = 0; LN[i]; ++i) { P[35 + i] = (uint8)LN[i]; }
		return P;
	}
	static TArray<uint8> Body4206(int32 Uidx, uint8 Arg, uint8 Mode, int32 P8, uint8 Cmd)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		AsdaBytes::WrI16(&B[0], (int16)Uidx);
		B[6] = Arg; B[7] = Mode;
		AsdaBytes::WrI32(&B[8], P8);
		B[12] = Cmd;
		return B;
	}
	// 4202 (53 б): @0 код, @1 i16 атакующий, @3 i16 цель, @13 i32[4] урон, @29 i16[4] навык (−1), @37 i32[4].
	static TArray<uint8> Body4202(uint8 Code, int32 Atk, int32 Tgt, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = Code;
		AsdaBytes::WrI16(&B[1], (int16)Atk); AsdaBytes::WrI16(&B[3], (int16)Tgt);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[13 + 4 * i], Q[i]); }
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[29 + 2 * i], -1); }
		return B;
	}
	// 4204 (49 б): @0 i16 атакующий, @2 i16 цель, @8 i32[4] урон, @24 код, @25 i16[4] навык (−1), @33 i32[4].
	static TArray<uint8> Body4204(uint8 Code, int32 Atk, int32 Tgt, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)Atk); AsdaBytes::WrI16(&B[2], (int16)Tgt);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], Q[i]); }
		B[24] = Code;
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}
	// 4021 (9 б): @0 i16 userIdx, @2 i16 слот (оружие), @4 i32 предмет.
	static TArray<uint8> Body4021Weapon(int32 Uidx, int32 Item)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI16(&B[2], (int16)EAsdaEquipSlot::Weapon); AsdaBytes::WrI32(&B[4], Item);
		return B;
	}
	// ★ 18.10.2026: 4029 (49 б, `Recv_CastState`) — удар по мобу принят: @0 i16 атакующий, @2 i16 rid, @4 i32 HP, @8 i32[4] очередь,
	//   @24 sub 0, @25 i16[4] второй канал (−1).
	static TArray<uint8> Body4029Mob(int32 Atk, int32 Rid, int32 Hp, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)Atk); AsdaBytes::WrI16(&B[2], (int16)Rid); AsdaBytes::WrI32(&B[4], Hp);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], Q[i]); }
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}
	// 4053 (4 б): @0 i16 умерший, @2 i16 убийца.
	using AsdaTestPackets::Body4053;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// Первое оружие подтипа (тип 9, не удочка) с дальностью.
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
	static FString StylesStr(const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen>& H)
	{
		FString S; for (const auto& X : H) { S += FString::Printf(TEXT("%d:0x%X "), X.Raw, X.Style); } return S;
	}
}

struct FAsdaCombatPvpSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaCombatPvpSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaPvpTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4201, 4203, 4206 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);   // свой номер/charId/оружие-вес вернутся
	if (const UAsdaGameUserSettings* Gs = UAsdaGameUserSettings::Get()) { S->SavedMode = Gs->DamageTextMode; }

	// ★ 31.10.2026 (баг 9, дуэль): позиционный `duel` — только подготовка, сценарий 11 (дуэль) и итог.
	const bool bDuelOnly = KitArgs.Pos.Contains(TEXT("duel"));
	const bool bDuelEndOnly = KitArgs.Pos.Contains(TEXT("duelend"));   // ★ 05.11.2026: только сценарий 12 (навык после конца дуэли)
	auto Add = [S, bDuelOnly, bDuelEndOnly](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		if (bDuelOnly && !Name.StartsWith(TEXT("подготовка")) && !Name.StartsWith(TEXT("11 ")) && !Name.StartsWith(TEXT("12 ")) && !Name.StartsWith(TEXT("итог"))) { return; }
		if (bDuelEndOnly && !Name.StartsWith(TEXT("подготовка")) && !Name.StartsWith(TEXT("12 ")) && !Name.StartsWith(TEXT("итог"))) { return; }
		FStep St; St.Name = Name; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 4.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC(); UAsdaWorldSubsystem* WSs = S->WS();
		if (!C || !PC || !WSs) { S->Expect(false, TEXT("нет своего персонажа/контроллера/мира после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home); C->StopAttackAnim();
		PC->bAutoAttack = false; PC->bCombatStarted = false; PC->TargetActor = nullptr;
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		if (Net->SelectedCharId == 0) { Net->SelectedCharId = kCharIdFallback; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		// ★ 18.10.2026 (мелочь 8): свой PvP-режим до прогона — вернётся штатным 4206 в итоге.
		S->bSavedPvpOn = Net->IsSelfPvpOn();
		if (const UAsdaNetSubsystem::FAsdaPvpState* Sp = Net->GetPvpState(S->Self)) { S->bHadSelfPvpState = true; S->SavedSelfPvp = *Sp; }
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR Env)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (!St) { return; }
			FOut& O = St->Outs.AddDefaulted_GetRef();
			O.Op = Op; O.Env = Env; O.AtS = FPlatformTime::Seconds();
			if (P && Len > 0) { O.Payload.Append(P, Len); }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		WSs->bRecordPvpHits = true; WSs->PvpHitsSeen.Reset(); WSs->PvpNoticesSeen.Reset();
		const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		S->Sword = FindWeapon(IT, 1);
		S->Bow = FindWeapon(IT, 8); S->BowWt = 8;
		if (S->Bow <= 0) { S->Bow = FindWeapon(IT, 10); S->BowWt = 10; }
		S->Expect(S->Sword > 0 && S->Bow > 0, FString::Printf(TEXT("оружие: меч %d, лук (подтип %d) %d"), S->Sword, S->BowWt, S->Bow));
		S->SavedWeapon = Net->SelfWeaponItemId; S->SavedEquipped = C->EquippedWeaponSubType;
		S->SpawnPeer(S->A, TEXT("PvPСоседА"), S->HomeTile + FIntPoint(2, -1));
		S->SpawnPeer(S->B, TEXT("PvPСоседБ"), S->HomeTile + FIntPoint(1, 0));
		S->Feed(4021, Body4021Weapon(S->A, S->Sword));
		S->Feed(4021, Body4021Weapon(S->B, S->Sword));
		S->SetMode(1);
		UE_LOG(LogAsdaPvpTest, Log, TEXT("САМОТЕСТ-PVP дом (%d,%d), я %d, A %d, B %d"), S->HomeTile.X, S->HomeTile.Y, S->Self, S->A, S->B);
	}, [S]() { return S->El() > 0.6 && S->Peer(S->A) && S->Peer(S->B); }, [S]()
	{
		S->Expect(S->Peer(S->A) && S->Peer(S->B), TEXT("соседи A и B стоят в мире"));
		S->SetHp(S->Self); S->SetHp(S->A); S->SetHp(S->B);
		S->Expect(S->HpOf(S->B) == kHp && S->HpOf(S->Self) == kHp, TEXT("снимки HP поставлены"));
	});

	// ─────────────────────── 1. 4206 и переключатель (функция 2/8) ───────────────────────
	Add(TEXT("1 режим 4206 и переключатель 2/8"), 3.f, [S, Net]()
	{
		S->Mark();
		S->Feed(4206, Body4206(S->Self, 0, 2, 0, 1));
		S->Expect(Net->IsSelfPvpOn(), TEXT("4206 свой, режим 2, команда 1 → флаг [160] включён"));
		S->Feed(4206, Body4206(S->A, 3, 1, S->B, 1));
		const UAsdaNetSubsystem::FAsdaPvpState* SA = Net->GetPvpState(S->A);
		S->Expect(SA && SA->Mode == 1 && SA->Rival == S->B && SA->ArgByte == 3 && !SA->bOn,
			TEXT("4206 соседа A: режим 1, соперник B, байт @6 = 3, флаг [160] у чужого не ставится (эталон — только свой)"));
		// Отбой путём игрока: функция эмоций 2/8.
		UAsdaCharaInfoSubsystem* CI = S->GI() ? S->GI()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		const bool bOk = CI && CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 8));
		const TArray<FOut> O = S->OutsOf(4206);
		const bool bBody = O.Num() == 1 && O[0].Payload.Num() == 13 && AsdaBytes::RdI16(&O[0].Payload[0]) == S->Self
			&& AsdaBytes::RdI32(&O[0].Payload[2]) == Net->SelectedCharId && O[0].Payload[6] == 0 && O[0].Payload[7] == 0;
		S->Expect(bOk && bBody, FString::Printf(TEXT("функция 2/8 при включённом режиме → 4206 (13 б: свой userIdx, charId %d, байт 0/0); ушло %d"),
			Net->SelectedCharId, O.Num()));
		S->Expect(!Net->IsSelfPvpOn(), TEXT("локальный отбой: флаг [160] снят до ответа сервера (ui_core.c:32299)"));
		// Включение: 4206 с текущим байтом [88] (0), флаг — только по ответу.
		if (CI) { CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 8)); }
		S->Expect(S->OutsOf(4206).Num() == 2 && !Net->IsSelfPvpOn(), TEXT("функция 2/8 при выключенном → 4206 ушёл, режим ждёт ответа сервера"));
		S->Feed(4206, Body4206(S->Self, 0, 2, 0, 1));
		S->Expect(Net->IsSelfPvpOn(), TEXT("ответ 4206 команда 1 → режим включён"));
	}, nullptr, nullptr);

	// ─────────────────────── 2. Своя атака мечом ───────────────────────
	Add(TEXT("2 своя атака мечом"), 14.f, [S, Net]()
	{
		S->Mark(); S->CalmSelf(); S->SetHp(S->B);
		Net->SelfWeaponItemId = S->Sword;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 1; }
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* Bc = S->Peer(S->B);
		if (!PC || !Bc) { S->Expect(false, TEXT("нет контроллера/цели")); return; }
		PC->SetTarget(Bc);
		PC->EngagePlayerFromClick(Bc);
		const TArray<FOut> O = S->OutsOf(4201);
		const FVector2D Me = S->Ch() ? S->Ch()->GetGamePosXZ() : FVector2D::ZeroVector;
		const bool bBody = O.Num() == 1 && O[0].Payload.Num() == 18 && AsdaBytes::RdI16(&O[0].Payload[0]) == S->B
			&& FMath::IsNearlyEqual(AsdaBytes::RdF32(&O[0].Payload[2]), (float)Me.X, 1.f) && FMath::IsNearlyEqual(AsdaBytes::RdF32(&O[0].Payload[6]), (float)Me.Y, 1.f)
			&& FMath::IsNearlyEqual(AsdaBytes::RdF32(&O[0].Payload[10]), (float)Me.X, 1.f);
		S->Expect(bBody, FString::Printf(TEXT("второй клик по B → 4201: 18 б, @0 i16 userIdx цели %d (не charId), @2/@6 свои X/Z, @10 дубль; ушло %d"), S->B, O.Num()));
		S->Expect(PC->bPvpAttack && !PC->bPvpAccepted, TEXT("атака ждёт 4202"));
		S->Feed(4202, Body4202(1, S->Self, S->B, { 100, (int32)(0x80000000u | 150u), -1, 120 }));
		S->Expect(PC->bPvpAccepted, TEXT("4202 код 1 → атака принята"));
	}, [S]()
	{
		// Сервер: на каждый 4203 — 4204 код 0 с двумя значениями (пачка расходуется по 2 за 4203 у ближнего).
		const TArray<FOut> O = S->OutsOf(4203);
		while (S->N4203Answered < O.Num())
		{
			++S->N4203Answered;
			S->Feed(4204, Body4204(0, S->Self, S->B, { 110, 111 }));
		}
		S->TrackFlinch(S->Peer(S->B));
		return S->El() > 9.0;
	}, [S]()
	{
		const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->Self, S->B);
		const TArray<FOut> O = S->OutsOf(4203);
		S->Expect(H.Num() >= 4 && H[0].Style == 0x1 && H[1].Style == 0x2 && H[2].Style == 0x6 && H[3].Style == 0x1,
			FString::Printf(TEXT("свои удары по B: стили 1/2/6/1 (урон/крит/DODGE/урон) — %s"), *StylesStr(H)));
		S->Expect(S->FlinchSeen > 0, FString::Printf(TEXT("★ 30.10: B вздрагивает от моих ударов (ResolveBasicAttack :27057/:27169), оценок с поворотом %d"), S->FlinchSeen));
		S->Expect(S->HasNotice(20013) && S->HasNotice(20012) && S->HasNotice(20011),
			FString::Printf(TEXT("строки в чат 20013/20012/20011 (белые) — %s"), *S->NoticesStr()));
		int32 Sum = 0; for (const auto& X : H) { Sum += X.Dmg; }
		S->Expect(Sum > 0 && S->HpOf(S->B) == kHp - Sum, FString::Printf(TEXT("HP B вычтен на кадрах удара: %d − %d = %d (факт %d)"), kHp, Sum, kHp - Sum, S->HpOf(S->B)));
		bool bIdx = O.Num() > 0; for (const FOut& X : O) { bIdx &= X.Payload.Num() == 18 && AsdaBytes::RdI16(&X.Payload[0]) == S->B; }
		S->Expect(O.Num() >= 2 && bIdx, FString::Printf(TEXT("4203 уходят (%d), тело 18 б, @0 — userIdx B"), O.Num()));
		// Ближний бой: 4203 на каждый чётный удар ⇒ ударов ≈ 2 × 4203 (последний отрезок может быть не досчитан).
		//   ★ 18.10.2026: модель самотеста уточнена — значений всего 4 (4202) + 2 на каждый отвеченный 4203, т.е. показанных ударов
		//   не больше 2·4203 + 4; прежний допуск «±3» ронял прогон, когда до конца окна успевали выйти ВСЕ значения (22 = 4 + 2·9).
		S->Expect(O.Num() >= 2 && H.Num() >= 2 * O.Num() - 3 && H.Num() <= 2 * O.Num() + 4,
			FString::Printf(TEXT("частота 4203 у меча: %d 4203 на %d ударов (ожидание ≈ 2 удара на 4203: от 2·4203 − 3 до 2·4203 + 4 значений 4202)"), O.Num(), H.Num()));
		// Отказ 4204 код 1 → нотис 1024 и стоп.
		S->NoticeFrom = S->WS() ? S->WS()->PvpNoticesSeen.Num() : 0;
		S->Feed(4204, Body4204(1, S->Self, S->B, {}));
		AAsdaPlayerController* PC = S->PC();
		S->Expect(S->HasNotice(1024) && PC && !PC->bPvpAttack && !PC->bAutoAttack, TEXT("4204 код 1 → нотис 1024 (золотой), атака окончена"));
		S->CalmSelf();
	});

	// ─────────────────────── 3. Своя атака луком: 4203 на каждый выстрел ───────────────────────
	Add(TEXT("3 своя атака луком"), 14.f, [S, Net]()
	{
		S->Mark(); S->CalmSelf(); S->SetHp(S->B);
		Net->SelfWeaponItemId = S->Bow;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->BowWt; }
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* Bc = S->Peer(S->B);
		if (!PC || !Bc) { S->Expect(false, TEXT("нет контроллера/цели")); return; }
		PC->SetTarget(nullptr); PC->SetTarget(Bc);
		PC->EngagePlayerFromClick(Bc);
		S->Feed(4202, Body4202(1, S->Self, S->B, { 200, 201, 202, 203 }));
	}, [S]()
	{
		const TArray<FOut> O = S->OutsOf(4203);
		while (S->N4203Answered < O.Num()) { ++S->N4203Answered; S->Feed(4204, Body4204(0, S->Self, S->B, { 210 })); }
		return S->El() > 9.0;
	}, [S]()
	{
		const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->Self, S->B);
		const TArray<FOut> O = S->OutsOf(4203);
		S->Expect(H.Num() >= 2 && H[0].Raw == 200 && H[0].Style == 0x1, FString::Printf(TEXT("выстрелы по B: прилёт со стилем 1 — %s"), *StylesStr(H)));
		// ★ 18.10.2026: модель самотеста уточнена (как у меча) — значений 4 (4202) + 1 на каждый отвеченный 4203, выстрелов не больше
		//   4203 + 4; прежний допуск «±2» ронял прогон, когда до конца окна выходили все значения (8 = 4 + 4; флейк был и в Ф4 r0/r0b/r1a).
		S->Expect(O.Num() >= 2 && H.Num() >= O.Num() - 2 && H.Num() <= O.Num() + 4,
			FString::Printf(TEXT("частота 4203 у лука: %d 4203 на %d выстрелов (ожидание — 1 на выстрел: от 4203 − 2 до 4203 + 4 значений 4202)"), O.Num(), H.Num()));
		S->CalmSelf();
	});

	// ─────────────────────── 4. Коды отказа 4202/4204 → нотисы по таблице ───────────────────────
	Add(TEXT("4 коды отказа 4202/4204"), 3.f, [S, Net]()
	{
		S->Mark(); S->CalmSelf();
		Net->SelfWeaponItemId = S->Sword;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 1; }
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* Bc = S->Peer(S->B); UAsdaWorldSubsystem* WSs = S->WS();
		if (!PC || !Bc || !WSs) { S->Expect(false, TEXT("нет контроллера/цели")); return; }
		struct FRow { int32 Code; int32 Loc; bool bStop; };
		// ★ 18.10.2026 (мелочь 4): код 5 — покой только НЕ занятому (эталонный миснейм `charIsActionStateNonCombat` = «занят»); свой
		//   после 4201 занят (ожидание 4202, эталонное состояние 22) — атака не снимается, нотис есть.
		const FRow R4202[] = { {0, 0, true}, {2, 2320, true}, {3, 2204, true}, {4, 0, false}, {5, 1074, false}, {6, 1082, true},
		                       {7, 2036, true}, {8, 2038, true}, {9, 1300, true}, {11, 112117, false}, {12, 112118, false} };
		for (const FRow& R : R4202)
		{
			S->CalmSelf(); PC->SetTarget(Bc);
			PC->EngagePlayerFromClick(Bc);
			const int32 From = WSs->PvpNoticesSeen.Num();
			S->Feed(4202, Body4202((uint8)R.Code, S->Self, S->B, {}));
			const bool bNotice = (R.Loc == 0) ? WSs->PvpNoticesSeen.Num() == From : (WSs->PvpNoticesSeen.Num() == From + 1 && WSs->PvpNoticesSeen.Last() == R.Loc);
			const bool bStopped = !PC->bPvpAttack;
			S->Expect(bNotice && bStopped == R.bStop, FString::Printf(TEXT("4202 код %d → нотис %d, стоп %s (факт: нотисов %d, стоп %s)"),
				R.Code, R.Loc, R.bStop ? TEXT("да") : TEXT("нет"), WSs->PvpNoticesSeen.Num() - From, bStopped ? TEXT("да") : TEXT("нет")));
		}
		const FRow R4204[] = { {1, 1024, true}, {2, 0, true}, {3, 1095, true}, {4, 1082, true}, {5, 1074, false}, {6, 1082, true},
		                       {7, 2036, true}, {8, 0, false} };
		for (const FRow& R : R4204)
		{
			S->CalmSelf(); PC->SetTarget(Bc);
			PC->EngagePlayerFromClick(Bc);
			S->Feed(4202, Body4202(1, S->Self, S->B, { 5 }));
			const int32 From = WSs->PvpNoticesSeen.Num();
			S->Feed(4204, Body4204((uint8)R.Code, S->Self, S->B, {}));
			const bool bNotice = (R.Loc == 0) ? WSs->PvpNoticesSeen.Num() == From : (WSs->PvpNoticesSeen.Num() == From + 1 && WSs->PvpNoticesSeen.Last() == R.Loc);
			const bool bStopped = !PC->bPvpAttack;
			S->Expect(bNotice && bStopped == R.bStop, FString::Printf(TEXT("4204 код %d → нотис %d, стоп %s (факт: нотисов %d, стоп %s)"),
				R.Code, R.Loc, R.bStop ? TEXT("да") : TEXT("нет"), WSs->PvpNoticesSeen.Num() - From, bStopped ? TEXT("да") : TEXT("нет")));
		}
		// ★ 18.10.2026 (мелочь 4): сосед A — код 5 при замахе (занят) снимает только признак серии; без замаха — покой и сброс серии.
		{
			S->CalmSelf();
			S->Feed(4202, Body4202(1, S->A, S->B, { 7, 7, 7, 7 }));
			S->Feed(4204, Body4204(0, S->A, S->B, {}));
			AAsdaPlayerCharacter* PA = S->Peer(S->A);
			const int32 Q0 = WSs->GetPvpQueueLen(S->A);
			const bool bSwing0 = PA && PA->IsAttackLoopAlive();
			S->Feed(4204, Body4204(5, S->A, S->B, {}));
			S->Expect(Q0 > 0 && bSwing0 && WSs->GetPvpQueueLen(S->A) == Q0 && PA && PA->IsAttackLoopAlive(),
				FString::Printf(TEXT("сосед A в замахе (занят): 4204 код 5 — серия (%d → %d) и замах остаются (эталон network.c:41813)"), Q0, WSs->GetPvpQueueLen(S->A)));
			if (PA) { PA->StopAttackAnim(); }
			S->Feed(4202, Body4202(5, S->A, S->B, {}));
			S->Expect(WSs->GetPvpQueueLen(S->A) == 0, FString::Printf(TEXT("сосед A без замаха (свободен): 4202 код 5 — покой, серия снята (очередь %d)"), WSs->GetPvpQueueLen(S->A)));
			S->CalmSelf();
		}
		// Гейт клика «нет оружия» → звук 214 и нотис 1004, пакета нет.
		S->CalmSelf(); PC->SetTarget(Bc);
		const int32 Out0 = S->OutsOf(4201).Num();
		Net->SelfWeaponItemId = 0;
		const int32 From = WSs->PvpNoticesSeen.Num();
		PC->EngagePlayerFromClick(Bc);
		S->Expect(WSs->PvpNoticesSeen.Num() == From + 1 && WSs->PvpNoticesSeen.Last() == 1004 && S->OutsOf(4201).Num() == Out0,
			TEXT("второй клик без оружия → нотис 1004 (золотой), 4201 не уходит"));
		Net->SelfWeaponItemId = S->Sword;
		S->CalmSelf();
	}, nullptr, nullptr);

	// ─────────────────────── 5. Жертва: сосед A бьёт меня (режимы 1 и 0) ───────────────────────
	for (const int32 Mode : { 1, 0 })
	{
		Add(FString::Printf(TEXT("5 жертва: A бьёт меня, режим показа %d"), Mode), 12.f, [S, Mode]()
		{
			S->Mark(); S->CalmSelf(); S->SetHp(S->Self); S->SetMode(Mode);
			S->Feed(4202, Body4202(1, S->A, S->Self, { 200, (int32)(0x80000000u | 300u), -1, -2 }));
			S->Feed(4204, Body4204(0, S->A, S->Self, {}));   // 4204 код 0 — замах A (очередь уже есть)
		}, [S]()
		{
			// Пакеты A — раз в 0.8 с (каждый — отрезок замаха).
			if (FPlatformTime::Seconds() - S->TLastFeed > 0.8) { S->Feed(4204, Body4204(0, S->A, S->Self, {})); }
			S->TrackFlinch(S->Ch());
			return S->Hits(S->A, S->Self).Num() >= 4 || S->El() > 10.0;
		}, [S, Mode]()
		{
			const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->A, S->Self);
			S->Expect(H.Num() >= 4 && H[0].Style == 0x9 && H[1].Style == 0xA && H[2].Style == 0xE && H[3].Style == 0xD,
				FString::Printf(TEXT("режим %d: удары A по мне — стили 9/0xA/0xE/0xD (жертва видит всегда) — %s"), Mode, *StylesStr(H)));
			S->Expect(S->FlinchSeen > 0, FString::Printf(TEXT("★ 30.10: я вздрагиваю от удара A (AttackAnimTick :2169/:2256), оценок с поворотом %d"), S->FlinchSeen));
			S->Expect(S->HasNotice(20242) && S->HasNotice(20243) && S->HasNotice(20014) && S->HasNotice(20015),
				FString::Printf(TEXT("строки жертве 20242/20243/20014/20015 (красные) — %s"), *S->NoticesStr()));
			S->Expect(S->HpOf(S->Self) == kHp - 500, FString::Printf(TEXT("мой HP: %d − 500 = %d (факт %d) — HUD из единого источника"), kHp, kHp - 500, S->HpOf(S->Self)));
			S->CalmSelf(); S->SetMode(1);
		});
	}

	// ─────────────────────── 6. Наблюдатель: A бьёт B (режимы 1 и 0) ───────────────────────
	for (const int32 Mode : { 1, 0 })
	{
		Add(FString::Printf(TEXT("6 наблюдатель: A бьёт B, режим показа %d"), Mode), 12.f, [S, Mode]()
		{
			S->Mark(); S->CalmSelf(); S->SetHp(S->B); S->SetMode(Mode);
			S->Feed(4202, Body4202(1, S->A, S->B, { 70, 80, 90, 100 }));
			S->Feed(4204, Body4204(0, S->A, S->B, {}));
		}, [S]()
		{
			if (FPlatformTime::Seconds() - S->TLastFeed > 0.8) { S->Feed(4204, Body4204(0, S->A, S->B, {})); }
			return S->Hits(S->A, S->B).Num() >= 4 || S->El() > 10.0;
		}, [S, Mode]()
		{
			const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->A, S->B);
			bool bStyles = H.Num() >= 4;
			for (const auto& X : H) { bStyles &= (Mode == 1) ? (X.Style == 0x9) : (X.Style == -1); }
			S->Expect(bStyles, FString::Printf(TEXT("режим %d: удары A по B — %s (ожидание: %s)"), Mode, *StylesStr(H),
				Mode == 1 ? TEXT("цифры 9") : TEXT("цифр нет")));
			S->Expect(!S->HasNotice(20242) && !S->HasNotice(20013), FString::Printf(TEXT("строк в чат наблюдателю нет — %s"), *S->NoticesStr()));
			S->Expect(S->HpOf(S->B) == kHp - 340, FString::Printf(TEXT("HP B вычтен и без цифр: %d (ожидание %d)"), S->HpOf(S->B), kHp - 340));
			S->CalmSelf(); S->SetMode(1);
		});
	}

	// ─────────────────────── 8. ВАЖНО 1: сосед A от PvP к мобу и обратно (ключ 1 и контроль 0) ───────────────────────
	for (const int32 Key : { 1, 0 })
	{
		Add(FString::Printf(TEXT("8 сосед A: PvP → моб → PvP, PvpSeriesGuard %d%s"), Key, Key == 0 ? TEXT(" (контроль)") : TEXT("")), 16.f, [S, Key]()
		{
			S->Mark(); S->CalmSelf(); S->SetHp(S->B); S->SetMode(1);
			AsdaCVarSave::SetForTest(kGuardKey, Key);
			if (UAsdaWorldSubsystem* WSs = S->WS()) { if (AAsdaMobActor* M = WSs->FindNetMobByRid(kMobRid)) { M->Destroy(); } }
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(kMobRid, /*Type*/1, S->HomeTile + FIntPoint(3, -1), /*State*/1, /*Speed*/250));
			// Серия A по B: 4 значения, замах — по 4204 код 0.
			S->Feed(4202, Body4202(1, S->A, S->B, { 31, 32, 33, 34 }));
			S->Feed(4204, Body4204(0, S->A, S->B, {}));
		}, [S]()
		{
			UAsdaWorldSubsystem* WSs = S->WS();
			const double Now = FPlatformTime::Seconds();
			if (!WSs) { return true; }
			// Фаза 0: первое значение серии показано — A переходит к мобу (принятый 4029, как живой сервер).
			if (S->Phase == 0 && S->Hits(S->A, S->B).Num() >= 1)
			{
				S->HitsMark = S->Hits(S->A, S->B).Num();
				S->Feed(4029, AsdaPvpTestLocal::Body4029Mob(S->A, kMobRid, 900, { 5 }));
				S->QAfter = WSs->GetPvpQueueLen(S->A);
				S->Phase = 1; S->TPhase = Now;
			}
			// Фаза 1: бой с мобом 2.5 с (4029 раз в 0.8 с — отрезки замаха A по мобу).
			if (S->Phase == 1)
			{
				if (Now - S->TLastFeed > 0.8) { S->Feed(4029, AsdaPvpTestLocal::Body4029Mob(S->A, kMobRid, 900, { 5 })); }
				S->HitsDuringMob = S->Hits(S->A, S->B).Num() - S->HitsMark;
				if (Now - S->TPhase > 2.5)
				{
					// Новая PvP-серия после паузы (A снова бьёт B).
					S->HitsMark = S->Hits(S->A, S->B).Num();
					S->Feed(4202, Body4202(1, S->A, S->B, { 41, 42, 43, 44 }));
					S->Feed(4204, Body4204(0, S->A, S->B, {}));
					S->Phase = 2; S->TPhase = Now;
				}
			}
			if (S->Phase == 2)
			{
				if (Now - S->TLastFeed > 0.8) { S->Feed(4204, Body4204(0, S->A, S->B, {})); }
				const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->A, S->B);
				S->NewSeriesRaw.Reset();
				for (int32 i = S->HitsMark; i < H.Num(); ++i) { S->NewSeriesRaw.Add(H[i].Raw); }
				return S->NewSeriesRaw.Num() >= 3 || Now - S->TPhase > 5.0;
			}
			return false;
		}, [S, Key]()
		{
			FString NewS; for (const int32 V : S->NewSeriesRaw) { NewS += FString::Printf(TEXT("%d "), V); }
			const bool bNewOk = S->NewSeriesRaw.Num() >= 3 && S->NewSeriesRaw[0] == 41 && S->NewSeriesRaw[1] == 42 && S->NewSeriesRaw[2] == 43;
			if (Key == 1)
			{
				S->Expect(S->QAfter == 0, FString::Printf(TEXT("4029 A по мобу снял PvP-серию A (очередь после пакета %d)"), S->QAfter));
				S->Expect(S->HitsDuringMob == 0, FString::Printf(TEXT("во время боя A с мобом остатки PvP-очереди над B не выводятся (ударов A→B: %d)"), S->HitsDuringMob));
				S->Expect(bNewOk, FString::Printf(TEXT("новая PvP-серия после паузы — свои значения без сдвига: %s(ожидание 41 42 43)"), *NewS));
			}
			else
			{
				// Контроль: без границ серии остатки (32…34) снимаются кадрами замаха по мобу — дефект виден, проверка не слепая.
				S->Expect(S->QAfter > 0 && S->HitsDuringMob > 0, FString::Printf(TEXT("КОНТРОЛЬ ключ 0: дефект виден — серия после 4029 жива (%d), ударов A→B во время боя с мобом %d"),
					S->QAfter, S->HitsDuringMob));
			}
			AsdaCVarSave::SetForTest(kGuardKey, 1);
			S->CalmSelf();
			if (UAsdaWorldSubsystem* WSs = S->WS()) { if (AAsdaMobActor* M = WSs->FindNetMobByRid(kMobRid)) { M->Destroy(); } }
		});
	}

	// ─────────────────────── 9. ВАЖНО 2: свой поздний 4204 после конца своей атаки (ключ 1 и контроль 0) ───────────────────────
	for (const int32 Key : { 1, 0 })
	{
		Add(FString::Printf(TEXT("9 свой поздний 4204 после StopPvpAttack, PvpSeriesGuard %d%s"), Key, Key == 0 ? TEXT(" (контроль)") : TEXT("")), 8.f, [S, Net, Key]()
		{
			S->Mark(); S->CalmSelf(); S->SetHp(S->B); S->SetMode(1);
			AsdaCVarSave::SetForTest(kGuardKey, Key);
			Net->SelfWeaponItemId = S->Sword;
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* Bc = S->Peer(S->B);
			UAsdaWorldSubsystem* WSs = S->WS();
			if (!C || !PC || !Bc || !WSs) { S->Expect(false, TEXT("нет персонажа/контроллера/цели")); return; }
			C->EquippedWeaponSubType = 1;
			PC->SetTarget(nullptr); PC->SetTarget(Bc);
			PC->EngagePlayerFromClick(Bc);
			S->Feed(4202, Body4202(1, S->Self, S->B, { 5 }));
			// Игрок снял атаку (переход к мобу, отмена) — ответ на последний 4203 приходит уже после.
			PC->StopPvpAttack(TEXT("самотест: атака снята до ответа 4204"));
			S->Feed(4204, Body4204(0, S->Self, S->B, { 77, 78 }));
			S->QAfter = WSs->GetPvpQueueLen(S->Self);
			// Следующий замах своего персонажа (по мобу) — его кадры удара не должны снимать значения позднего 4204.
			C->StartAttackSwing(1, /*HoldSec=*/3.f);
		}, [S]() { return S->El() > 2.2; }, [S, Key]()
		{
			int32 Late = 0;
			for (const auto& X : S->Hits(S->Self, S->B)) { if (X.Raw == 77 || X.Raw == 78) { ++Late; } }
			if (Key == 1)
			{
				S->Expect(S->QAfter == 0, FString::Printf(TEXT("поздний свой 4204 серию не завёл (очередь %d)"), S->QAfter));
				S->Expect(Late == 0, FString::Printf(TEXT("следующий замах значения позднего 4204 не снял (ударов 77/78 по B: %d)"), Late));
			}
			else
			{
				S->Expect(S->QAfter > 0 && Late > 0, FString::Printf(TEXT("КОНТРОЛЬ ключ 0: дефект виден — серия %d, ложных ударов по B на замахе %d"), S->QAfter, Late));
			}
			AsdaCVarSave::SetForTest(kGuardKey, 1);
			S->CalmSelf();
		});
	}

	// ─────────────────────── 10. ★ 25.10.2026 (фантомный урон, ревизия М-4/М-7): отложенная 4201 ───────────────────────
	//   Атака по B (4201 уходит сразу, часы атаки взведены) → клик по A раньше срока часов: 4201 откладывается на СВОЙ таймер,
	//   PvP-атака по B снята, стойка лицом к A. Варианты: 0 — ничего не мешает (4201 по A уходит не раньше срока); 1 — ход (снята);
	//   2 — снятие цели (снята).
	for (const int32 Var : { 0, 1, 2 })
	{
		Add(FString::Printf(TEXT("10 отложенная 4201 (%s)"), Var == 0 ? TEXT("уходит к сроку часов") : (Var == 1 ? TEXT("снята ходом") : TEXT("снята снятием цели"))), 6.f,
		[S, Net, Var]()
		{
			S->Mark(); S->CalmSelf(); S->SetHp(S->B);
			Net->SelfWeaponItemId = S->Sword;
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
			AAsdaPlayerCharacter* Bc = S->Peer(S->B); AAsdaPlayerCharacter* Ac = S->Peer(S->A);
			if (!C || !PC || !Bc || !Ac) { S->Expect(false, TEXT("нет персонажа/контроллера/целей A и B")); return; }
			C->EquippedWeaponSubType = 1;
			PC->SetTarget(Bc); PC->EngagePlayerFromClick(Bc);
			S->Feed(4202, Body4202(1, S->Self, S->B, { 5 }));
			S->T4201 = FPlatformTime::Seconds();
			const UAsdaAttackRhythmSubsystem* R = S->GI() ? S->GI()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr;
			S->ReqMs = R ? R->GetRequiredMs() : -1;
			PC->SetTarget(Ac); PC->EngagePlayerFromClick(Ac);   // клик по A раньше срока часов
			S->Expect(S->OutsOf(4201).Num() == 1 && AsdaIsDeferred4201Pending(PC),
				FString::Printf(TEXT("4201 по A отложена (ушло 4201: %d, ожидание %s)"), S->OutsOf(4201).Num(), AsdaIsDeferred4201Pending(PC) ? TEXT("есть") : TEXT("нет")));
			S->Expect(!PC->bPvpAttack && C->AttackFaceTarget == Ac, TEXT("на время ожидания атака по B снята, стойка лицом к A"));
			if (Var == 1) { PC->StartRoutedMove(C->GetGamePosXZ() + FVector2D(0.0, 300.0)); }
			if (Var == 2) { PC->SetTarget(nullptr); }
		}, [S]() { return S->El() > 2.5; }, [S, Var]()
		{
			const TArray<FOut> O = S->OutsOf(4201);
			if (Var == 0)
			{
				const double Gap = (O.Num() >= 2) ? (O[1].AtS - O[0].AtS) * 1000.0 : -1.0;
				S->Expect(O.Num() == 2 && O[1].Payload.Num() >= 2 && AsdaBytes::RdI16(&O[1].Payload[0]) == S->A && Gap + 1.0 >= (double)S->ReqMs,
					FString::Printf(TEXT("4201 по A ушла к сроку часов: ушло %d, через %.0f мс (требование %d мс)"), O.Num(), Gap, S->ReqMs));
			}
			else
			{
				S->Expect(O.Num() == 1, FString::Printf(TEXT("отложенная 4201 снята (%s): ушло 4201 всего %d"), Var == 1 ? TEXT("ход") : TEXT("снятие цели"), O.Num()));
			}
			// Ход сценария не должен достаться следующим: маршрут снят, «ход в пути» (офлайн 4007 нет) погашен.
			if (AAsdaPlayerController* PCx = S->PC()) { PCx->CancelRoute(); }
			if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест PvP 10: сброс хода")); }
			S->CalmSelf();
		});
	}

	// ─────────────────────── 11. ★ 31.10.2026 (баг 9 владельца): ДУЭЛЬ — обычная атака каждым видом оружия ───────────────────────
	//   Путь игрока: режим дуэли — штатные 4206 (подтип 1, соперник @8) себе и сопернику и арена 4305 (оба участника); второй клик по
	//   сопернику (`EngagePlayerFromClick`) → 4201; 4202 код 1 → замах → 4203 (сервер-автоответчик: на 4006 — грант 4007, на 4203 —
	//   4204 код 0: ближнему 2 значения, дальнобойному 1 — `sub_555800` GameServer.c:131427). Соперник стоит на дистанции оружия
	//   (у дальнобойного — далеко, как на арене) или за ней (подход). Проверяется: 4201 по сопернику, 4203 уходят, удары (цифры)
	//   по сопернику, у лука/арбалета/баллисты — вылет снарядов.
	{
		struct FDw { int32 Wt; const TCHAR* Name; };
		static const FDw kDuelW[] = { {1, TEXT("меч")}, {5, TEXT("посох")}, {10, TEXT("лук")}, {9, TEXT("арбалет")}, {11, TEXT("баллиста")} };   // подтипа 8 в таблице предметов нет
		Add(TEXT("11 дуэль: вход (4206 подтип 1 ×2, арена 4305)"), 3.f, [S, Net]()
		{
			S->Mark(); S->CalmSelf();
			const UAsdaNetSubsystem::FAsdaPvpState* Sp = Net->GetPvpState(S->Self);
			S->bDuelHadPvp = Sp != nullptr; if (Sp) { S->DuelSavedPvp = *Sp; }
			const int32 PeerCharId = 900000 + S->B;   // `DebugSpawnFakePeer`: charId = 900000 + userNum
			S->Feed(4206, Body4206(S->Self, 0, 1, PeerCharId, 1));
			S->Feed(4206, Body4206(S->B, 0, 1, Net->SelectedCharId, 1));
			TArray<uint8> A; A.SetNumZeroed(14);   // 4305: группа @0, charId A @2, charId B @6, тайл-центр @10/@12
			AsdaBytes::WrI16(&A[0], 3); AsdaBytes::WrI32(&A[2], Net->SelectedCharId); AsdaBytes::WrI32(&A[6], PeerCharId);
			AsdaBytes::WrI16(&A[10], (int16)(S->HomeTile.X + 3)); AsdaBytes::WrI16(&A[12], (int16)S->HomeTile.Y);
			S->Feed(4305, A);
		}, nullptr, [S, Net]()
		{
			S->Expect(Net->IsSelfPvpOn(), TEXT("дуэль: 4206 подтип 1 своему → флаг [160] включён (network.c:44121)"));
		});
		for (const FDw& Dw : kDuelW)
		{
			// Варианты: 0 — на дальности, сервер ответил 4202; 1 — подход (4202); 2 — на дальности, сервер обработал первый удар сразу:
			//   только 4204 код 0, 4202 нет (живой NpcServer `sub_460710` → `sub_45FF80(…,1)` → 8225 → 4204, баг 9); 3 — вариант 2 под
			//   ключом `asda.Combat.PvpHitAccepts 0` (контроль: прежний дефект — атака снята «нет ответа 4202», выстрелов нет).
			for (const int32 Far : { 0, 1, 2, 3 })
			{
				if (Far == 3 && Dw.Wt != 10 && Dw.Wt != 1) { continue; }   // контроль — лук и меч
				static const TCHAR* const kVar[] = { TEXT("соперник на дальности оружия (4202)"), TEXT("подход к сопернику (4202)"),
					TEXT("на дальности, сервер сразу 4204 без 4202"), TEXT("на дальности, сразу 4204, PvpHitAccepts 0 (контроль)") };
				const FString Nm = FString::Printf(TEXT("11 дуэль: %s (подтип %d), %s"), Dw.Name, Dw.Wt, kVar[Far]);
				const int32 Wt = Dw.Wt;
				Add(Nm, 16.f, [S, Net, Wt, Far]()
				{
					AsdaCVarSave::SetForTest(TEXT("asda.Combat.PvpHitAccepts"), Far == 3 ? 0 : 1);
					// Стрелы прошлого шага ещё в полёте: их прилёт попал бы в наблюдения этого шага — снять.
					if (UWorld* Wd = S->World.Get()) { for (TActorIterator<AAsdaProjectileActor> It(Wd); It; ++It) { It->Destroy(); } }
					S->Mark(); S->CalmSelf(); S->SetHp(S->B);
					const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
					S->Wpn = FindWeapon(IT, Wt);
					const FAsdaItemRecord* R = (IT && S->Wpn > 0) ? IT->GetItem(S->Wpn) : nullptr;
					if (!R) { S->Expect(false, FString::Printf(TEXT("оружие подтипа %d в таблице есть"), Wt)); return; }
					Net->SelfWeaponItemId = S->Wpn;
					if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = Wt; }
					// Соперник: на дальности оружия (тайлов − 1, не ближе 1) или на 3 тайла дальше неё.
					const int32 Tiles = (Far == 1) ? R->AttackRangeTiles + 3 : FMath::Max(1, R->AttackRangeTiles - 1);
					AAsdaPlayerCharacter* Bc = S->Peer(S->B);
					if (Bc) { Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(Tiles, 0))); }
					AAsdaPlayerController* PC = S->PC();
					if (!PC || !Bc) { S->Expect(false, TEXT("нет контроллера/соперника")); return; }
					UE_LOG(LogAsdaPvpTest, Log, TEXT("САМОТЕСТ-PVP дуэль: оружие %d (подтип %d, дальность %d тайлов), соперник в %d тайлах"),
						S->Wpn, Wt, R->AttackRangeTiles, Tiles);
					PC->SetTarget(nullptr); PC->SetTarget(Bc);
					PC->EngagePlayerFromClick(Bc);
					const TArray<FOut> O = S->OutsOf(4201);
					S->Expect(O.Num() == 1 && O[0].Payload.Num() == 18 && AsdaBytes::RdI16(&O[0].Payload[0]) == S->B,
						FString::Printf(TEXT("второй клик по сопернику → 4201 по userIdx %d (ушло %d)"), S->B, O.Num()));
					const bool bRangedW = Wt >= 8 && Wt <= 11;
					if (Far >= 2)
					{
						// Ответ живого сервера на 4201 «в дальности»: первый удар уже посчитан — 4204 код 0 (ближнему 2 значения, дальнобойному 1).
						if (bRangedW) { S->Feed(4204, Body4204(0, S->Self, S->B, { 300 })); }
						else { S->Feed(4204, Body4204(0, S->Self, S->B, { 300, 301 })); }
					}
					else { S->Feed(4202, Body4202(1, S->Self, S->B, { 300, 301, 302, 303 })); }
				}, [S, Wt]()
				{
					// Сервер-автоответчик: 4006 → 4007 код 1 (эхо тела), 4203 → 4204 код 0.
					const bool bRanged = Wt >= 8 && Wt <= 11;
					for (; S->AutoIdx < S->Outs.Num(); ++S->AutoIdx)
					{
						const FOut X = S->Outs[S->AutoIdx];   // копия: ответ ниже может дописать в перехват
						if (X.Op == 4006 && X.Payload.Num() >= 43)
						{
							const uint8* P = X.Payload.GetData() + 16;   // тело 4006 — после 16 б заголовка (`FillMoveBody`, как у самотеста 4007)
							TArray<uint8> B; B.SetNumZeroed(27);
							B[0] = 1; AsdaBytes::WrI16(&B[1], (int16)S->Self);
							FMemory::Memcpy(&B[3], P + 3, 22);            // подтип, cur, dst — эхо
							const float Spd = 0.35f; FMemory::Memcpy(&B[21], &Spd, 4);
							FMemory::Memcpy(&B[25], P + 25, 2);           // цель — эхо
							S->Feed(4007, B);
						}
						else if (X.Op == 4203)
						{
							if (bRanged) { S->Feed(4204, Body4204(0, S->Self, S->B, { 310 })); }
							else { S->Feed(4204, Body4204(0, S->Self, S->B, { 310, 311 })); }
						}
					}
					if (UWorld* Wd = S->World.Get())
					{
						for (TActorIterator<AAsdaProjectileActor> It(Wd); It; ++It) { S->ProjSeen.Add(It->GetName()); }
					}
					return S->El() > 11.0;
				}, [S, Wt, Far]()
				{
					const bool bRanged = Wt >= 8 && Wt <= 11;
					const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->Self, S->B);
					const TArray<FOut> O = S->OutsOf(4203);
					const AAsdaPlayerCharacter* C = S->Ch(); const AAsdaPlayerCharacter* Bc = S->Peer(S->B);
					const float D = (C && Bc) ? FVector::Dist2D(C->GetActorLocation(), Bc->GetActorLocation()) : -1.f;
					if (Far == 3)
					{
						const AAsdaPlayerController* PCc = S->PC();
						S->Expect(O.Num() == 0 && H.Num() == 0 && PCc && !PCc->bPvpAttack, FString::Printf(TEXT("контроль PvpHitAccepts 0: прежний дефект — 4203 нет (%d), ударов нет (%d), атака снята (%s)"),
							O.Num(), H.Num(), (PCc && !PCc->bPvpAttack) ? TEXT("да") : TEXT("нет")));
						AsdaCVarSave::SetForTest(TEXT("asda.Combat.PvpHitAccepts"), 1);
						S->CalmSelf();
						return;
					}
					S->Expect(O.Num() >= 2, FString::Printf(TEXT("подтип %d: 4203 уходят (%d), дистанция до соперника %.0f, ходов 4006 %d"), Wt, O.Num(), D, S->OutsOf(4006).Num()));
					S->Expect(H.Num() >= 2, FString::Printf(TEXT("подтип %d: удары по сопернику показаны (%d) — %s"), Wt, H.Num(), *StylesStr(H)));
					if (bRanged)
					{
						S->Expect(S->ProjSeen.Num() >= 2, FString::Printf(TEXT("подтип %d: снаряды вылетели (%d)"), Wt, S->ProjSeen.Num()));
					}
					(void)Far;
					S->CalmSelf();
				});
			}
		}
		// Жертва: соперник B бьёт МЕНЯ каждым видом оружия (оружие — штатным 4021), стоя на дальности оружия. Пакеты — как у сервера:
		//   4202 код 1 (4 значения), далее на каждый его 4203 — 4204 код 0 (ближнему 2 значения, дальнобойному 1), раз в 0,8 с.
		//   Вариант «сразу 4204»: соперник кликнул меня, уже стоя на дальности, — сервер первый удар посчитал сразу, 4202 нет (баг 9).
		for (const FDw& Dw : kDuelW)
		for (const int32 NoStart : { 0, 1 })
		{
			const int32 Wt = Dw.Wt;
			Add(FString::Printf(TEXT("11 дуэль: соперник бьёт меня — %s (подтип %d)%s"), Dw.Name, Wt, NoStart ? TEXT(", сразу 4204 без 4202") : TEXT("")), 12.f, [S, Wt, NoStart]()
			{
				S->Mark(); S->CalmSelf(); S->SetHp(S->Self);
				const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				S->Wpn = FindWeapon(IT, Wt);
				const FAsdaItemRecord* R = (IT && S->Wpn > 0) ? IT->GetItem(S->Wpn) : nullptr;
				if (!R) { S->Expect(false, FString::Printf(TEXT("оружие подтипа %d в таблице есть"), Wt)); return; }
				S->Feed(4021, Body4021Weapon(S->B, S->Wpn));
				const int32 Tiles = FMath::Max(1, R->AttackRangeTiles - 1);
				if (AAsdaPlayerCharacter* Bc = S->Peer(S->B)) { Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(Tiles, 0))); }
				if (NoStart) { S->Feed(4204, Body4204(0, S->B, S->Self, { 400, 401 })); }
				else
				{
					S->Feed(4202, Body4202(1, S->B, S->Self, { 400, 401, 402, 403 }));
					S->Feed(4204, Body4204(0, S->B, S->Self, {}));
				}
			}, [S, Wt]()
			{
				const bool bRanged = Wt >= 8 && Wt <= 11;
				if (FPlatformTime::Seconds() - S->TLastFeed > 0.8)
				{
					if (bRanged) { S->Feed(4204, Body4204(0, S->B, S->Self, { 410 })); }
					else { S->Feed(4204, Body4204(0, S->B, S->Self, { 410, 411 })); }
				}
				if (UWorld* Wd = S->World.Get())
				{
					for (TActorIterator<AAsdaProjectileActor> It(Wd); It; ++It) { S->ProjSeen.Add(It->GetName()); }
				}
				return S->El() > 8.0;
			}, [S, Wt]()
			{
				const bool bRanged = Wt >= 8 && Wt <= 11;
				const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->B, S->Self);
				const AAsdaPlayerCharacter* Bc = S->Peer(S->B);
				S->Expect(H.Num() >= 4, FString::Printf(TEXT("подтип %d: удары соперника по мне показаны (%d) — %s; замах соперника %s"), Wt, H.Num(), *StylesStr(H),
					(Bc && Bc->IsAttackLoopAlive()) ? TEXT("идёт") : TEXT("НЕ идёт")));
				if (bRanged) { S->Expect(S->ProjSeen.Num() >= 2, FString::Printf(TEXT("подтип %d: снаряды соперника вылетели (%d)"), Wt, S->ProjSeen.Num())); }
				if (AAsdaPlayerCharacter* PB = S->Peer(S->B)) { PB->StopAttackAnim(); }
				if (UAsdaWorldSubsystem* W = S->WS()) { W->ResetPvpSeries(S->B, TEXT("самотест: сброс")); }
			});
		}
		// ★ 01.11.2026 (ревизия 4024/400) [ОРИГИНАЛ]: выход своего из действия атаки 5 → 0 = 4024 код 400 (`CCharacter_SetAction`
		//   case 0, entity_char.c:16545-16556); из ожидания ответа на 4201 (22) — нет. Тело 4024 в перехвате: actionType @16+2.
		auto Count400 = [S]() { int32 N = 0; for (const FOut& X : S->OutsOf(4024)) { if (X.Payload.Num() >= 20 && AsdaBytes::RdI16(&X.Payload[18]) == 400) { ++N; } } return N; };
		for (const int32 V : { 0, 1 })
		{
			Add(FString::Printf(TEXT("11 дуэль: выход из атаки — %s"), V == 0 ? TEXT("из замаха (отказ 4204 код 1) → 4024/400") : TEXT("из ожидания ответа на 4201 (отказ 4202) → без 4024/400")), 6.f,
				[S, Net, V]()
			{
				if (UWorld* Wd = S->World.Get()) { for (TActorIterator<AAsdaProjectileActor> It(Wd); It; ++It) { It->Destroy(); } }
				S->Mark(); S->CalmSelf(); S->SetHp(S->B);
				const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				S->Wpn = FindWeapon(IT, 10);
				Net->SelfWeaponItemId = S->Wpn;
				if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
				AAsdaPlayerCharacter* Bc = S->Peer(S->B); AAsdaPlayerController* PC = S->PC();
				if (!Bc || !PC) { S->Expect(false, TEXT("нет контроллера/соперника")); return; }
				Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(10, 0)));
				PC->SetTarget(nullptr); PC->SetTarget(Bc);
				S->Mark();   // 4024 снятия цели прошлого шага не в счёт
				PC->EngagePlayerFromClick(Bc);
				if (V == 0) { S->Feed(4204, Body4204(0, S->Self, S->B, { 300 })); }   // первый удар посчитан — замах
			}, [S, V]()
			{
				const double E = S->El();
				if (!S->bFlag && E > (V == 0 ? 1.5 : 0.3))
				{
					S->bFlag = true;
					const AAsdaPlayerController* PCc = S->PC(); const AAsdaPlayerCharacter* C = S->Ch();
					S->Phase = (PCc && PCc->bInCombatStance && C && C->IsAttackLoopAlive()) ? 1 : 0;   // замах шёл к моменту выхода
					if (V == 0) { S->Feed(4204, Body4204(1, S->Self, S->B, {})); } else { S->Feed(4202, Body4202(2, S->Self, S->B, {})); }
				}
				return S->bFlag && E > 2.5;
			}, [S, V, Count400]()
			{
				const AAsdaPlayerController* PCc = S->PC();
				if (V == 0)
				{
					S->Expect(S->Phase == 1, TEXT("к отказу шёл замах (действие 5)"));
					S->Expect(Count400() == 1 && PCc && !PCc->bPvpAttack, FString::Printf(TEXT("выход из замаха (отказ 4204 код 1) → ровно один 4024 код 400 (ушло %d), атака снята"), Count400()));
				}
				else
				{
					S->Expect(Count400() == 0 && PCc && !PCc->bPvpAttack, FString::Printf(TEXT("выход из ожидания ответа (22) → 4024/400 НЕ уходит (ушло %d), атака снята"), Count400()));
				}
				S->CalmSelf();
			});
		}
		// ★ 01.11.2026 (ревизия бага 9, ВАЖНО 1/2): первый удар 4204 без 4202 — (0) при висящем 4006 без 4007 атака ПРИНИМАЕТСЯ (гейт
		//   эталона `field_C & 6` не подменяется битом «ход в пути»); (1) во время своего навыка — атака принята, замаха нет, пока навык
		//   идёт (эталон: 24 не в списке network.c:41459-41488), после навыка замах возвращает `PvpAttackTick`, 4203 уходят.
		for (const int32 V : { 0, 1 })
		{
			Add(FString::Printf(TEXT("11 дуэль: первый удар 4204 — %s"), V == 0 ? TEXT("при висящем 4006 (ответа 4007 нет)") : TEXT("во время навыка")), 8.f,
				[S, Net, V]()
			{
				if (UWorld* Wd = S->World.Get()) { for (TActorIterator<AAsdaProjectileActor> It(Wd); It; ++It) { It->Destroy(); } }
				S->Mark(); S->CalmSelf(); S->SetHp(S->B);
				const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				S->Wpn = FindWeapon(IT, 10);
				Net->SelfWeaponItemId = S->Wpn;
				AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerCharacter* Bc = S->Peer(S->B); AAsdaPlayerController* PC = S->PC();
				if (!C || !Bc || !PC) { S->Expect(false, TEXT("нет своего/соперника/контроллера")); return; }
				C->EquippedWeaponSubType = 10;
				Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(10, 0)));
				PC->SetTarget(Bc);
				S->Mark();
				PC->EngagePlayerFromClick(Bc);
				if (V == 0) { Net->bMoveRequestInFlight = true; }                                       // 4006 «в пути», 4007 не пришёл
				else if (UWorld* Wd = S->World.Get()) { C->SkillAnimUntilS = Wd->GetTimeSeconds() + 1.5; }   // свой навык ещё 1,5 с
				S->Feed(4204, Body4204(0, S->Self, S->B, { 300 }));
				S->Expect(PC->bPvpAttack && PC->bPvpAccepted, FString::Printf(TEXT("4204 код 0 без 4202 → атака принята (%s)"), V == 0 ? TEXT("ход в пути") : TEXT("навык")));
				if (V == 1) { S->Expect(!C->IsAttackLoopAlive(), TEXT("во время навыка замах НЕ начат (эталон: навык 24 действие не меняет)")); }
			}, [S]()
			{
				for (; S->AutoIdx < S->Outs.Num(); ++S->AutoIdx)
				{
					const FOut X = S->Outs[S->AutoIdx];
					if (X.Op == 4203) { S->Feed(4204, Body4204(0, S->Self, S->B, { 310 })); }
				}
				return S->El() > 5.0;
			}, [S, Net, V]()
			{
				const AAsdaPlayerController* PC = S->PC(); const AAsdaPlayerCharacter* C = S->Ch();
				S->Expect(PC && PC->bPvpAttack && S->OutsOf(4203).Num() >= 1 && C && C->IsAttackLoopAlive(),
					FString::Printf(TEXT("%s: атака идёт, замах жив, 4203 уходят (%d)"), V == 0 ? TEXT("ход в пути") : TEXT("после навыка"), S->OutsOf(4203).Num()));
				Net->bMoveRequestInFlight = false;
				if (AAsdaPlayerCharacter* Cm = S->Ch()) { Cm->SkillAnimUntilS = 0.0; }
				S->CalmSelf();
			});
		}
		// Сосед: его 4025 код 400 (вышел из атаки) — замах снят, PvP-очередь сброшена (`CChar_SetState(0)`, network.c:39645).
		Add(TEXT("11 дуэль: 4025/400 соперника — его замах и очередь сняты"), 4.f, [S]()
		{
			S->Mark(); S->CalmSelf();
			const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			S->Feed(4021, Body4021Weapon(S->B, FindWeapon(IT, 1)));
			if (AAsdaPlayerCharacter* Bc = S->Peer(S->B)) { Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(1, 0))); }
			S->Feed(4202, Body4202(1, S->B, S->Self, { 500, 501, 502, 503 }));
			S->Feed(4204, Body4204(0, S->B, S->Self, {}));
		}, [S]()
		{
			if (!S->bFlag && S->El() > 0.5)
			{
				S->bFlag = true;
				const AAsdaPlayerCharacter* Bc = S->Peer(S->B); const UAsdaWorldSubsystem* W = S->WS();
				S->Phase = (Bc && Bc->IsAttackLoopAlive() ? 1 : 0) + (W && W->GetPvpQueueLen(S->B) > 0 ? 2 : 0);
				TArray<uint8> B; B.SetNumZeroed(13);
				AsdaBytes::WrI16(&B[0], (int16)S->B); AsdaBytes::WrI16(&B[2], 400);
				const float Fx = 1.f, Fz = 0.f; FMemory::Memcpy(&B[5], &Fx, 4); FMemory::Memcpy(&B[9], &Fz, 4);
				S->Feed(4025, B);
			}
			return S->bFlag && S->El() > 1.0;
		}, [S]()
		{
			const AAsdaPlayerCharacter* Bc = S->Peer(S->B); const UAsdaWorldSubsystem* W = S->WS();
			S->Expect(S->Phase == 3, FString::Printf(TEXT("до 4025: замах соперника идёт и очередь есть (%d)"), S->Phase));
			S->Expect(Bc && !Bc->IsAttackLoopAlive() && W && W->GetPvpQueueLen(S->B) == 0,
				TEXT("4025 код 400 соперника → его замах снят, PvP-очередь сброшена (эталон CChar_SetState 0)"));
			S->CalmSelf();
		});
		Add(TEXT("11 дуэль: выход (4206 отбой ×2)"), 2.f, [S, Net]()
		{
			S->CalmSelf();
			// Свой режим — тот, что был до дуэли (следующие сценарии ждут включённый режим шага 1), штатным 4206; сопернику — отбой.
			const UAsdaNetSubsystem::FAsdaPvpState& Sp = S->DuelSavedPvp;
			if (S->bDuelHadPvp && Sp.bOn) { S->Feed(4206, Body4206(S->Self, Sp.ArgByte, Sp.Mode, Sp.Mode == 4 ? Sp.Team : Sp.Rival, 1)); }
			else { S->Feed(4206, Body4206(S->Self, 0, 0, 0, 1)); }
			S->Feed(4206, Body4206(S->B, 0, 0, 0, 1));
			if (AAsdaPlayerCharacter* Bc = S->Peer(S->B)) { Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(1, 0))); }
		}, nullptr, nullptr);
	}

	// ─────────────────────── 12. ★ 05.11.2026 (баг дуэли «победитель после дуэли не может применять навыки») ───────────────────────
	//   Полная дуэль РЕАЛЬНЫМИ пакетами сервера: 4303 код 1 (принято), 4305 (арена), 4206 подтип 1 себе и сопернику; бой; конец — как
	//   `CThreadRun::PvpResult` (GameServer.c:77760-78019): каждому 4206 подтип 0, затем 4304 (тип 2 — смерть). Смерть в дуэли игрока
	//   не убивает: `SetCharDeadTime` сразу зовёт PvpResult (GameServer.c:80773) — навык, которым добили соперника, получает ответ
	//   5257 ПОСЛЕ 4206/4304. Затем навык 705 по мобу путём игрока — заявка 5256 обязана уйти.
	//   Варианты: 0 — победа после обычной атаки; 1 — победа навыком (5257 после 4304); 2 — поражение (после жеста 51);
	//   3 — повторная дуэль, снова победа навыком.
	{
		//   4 — поражение, добивающий удар соперника довёл мой HP до 0 (сервер не убивает); 5 — победа навыком, мой удар довёл HP
		//   соперника до 0.
		//   6–9 — прочие итоги сервера (победа навыком): 6 сдача (тип 4), 7 время (0), 8 выход из зоны (1), 9 разрыв/выход соперника (3).
		for (const int32 V : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 })
		{
			static const TCHAR* const kV[] = { TEXT("победа обычной атакой"), TEXT("победа навыком (5257 после 4304)"),
				TEXT("поражение (после жеста 51)"), TEXT("повторная дуэль, победа навыком"), TEXT("поражение, мой HP 0 от добивающего удара"),
				TEXT("победа навыком, HP соперника 0"), TEXT("сдача соперника (тип 4)"), TEXT("конец времени (тип 0)"), TEXT("выход из зоны (тип 1)"),
				TEXT("разрыв соперника (тип 3)") };
			Add(FString::Printf(TEXT("12 конец дуэли: навык после — %s"), kV[V]), (V == 2 || V == 4) ? 18.f : 12.f, [S, Net, V]()
			{
				if (UWorld* Wd = S->World.Get()) { for (TActorIterator<AAsdaProjectileActor> It(Wd); It; ++It) { It->Destroy(); } }
				// Заявка 705 прошлого шага ещё «в полёте» (25) — снять штатным отказом 5257 до нажатий этого шага.
				if (UAsdaSkillUseSubsystem* SU0 = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU0->SimulateCastResult5257(0, kDuelSkill); }
				S->Mark(); S->CalmSelf(); S->SetHp(S->B); S->SetHp(S->Self);
				const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				S->Wpn = FindWeapon(IT, 10);
				Net->SelfWeaponItemId = S->Wpn;
				AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerCharacter* Bc = S->Peer(S->B); AAsdaPlayerController* PC = S->PC();
				if (!C || !Bc || !PC) { S->Expect(false, TEXT("нет своего/соперника/контроллера")); return; }
				C->EquippedWeaponSubType = 10;
				Net->bStandCaptureSkillUse = true;
				{   // 5251 — навык изучен (34 б: код@0, уровень@2, 1@3); MP с запасом
					TArray<uint8> L; L.SetNumZeroed(34); AsdaBytes::WrI16(L.GetData(), (int16)kDuelSkill); L[2] = 5; L[3] = 1; S->Feed(5251, L);
					Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест дуэли"), true);
				}
				if (UAsdaWorldSubsystem* WSs = S->WS()) { if (AAsdaMobActor* M = WSs->FindNetMobByRid(kDuelMob)) { M->Destroy(); } }
				S->Feed(4013, AsdaTestPackets::MobSpawn4013(kDuelMob, /*Type*/1, S->HomeTile + FIntPoint(0, 4), /*State*/1, /*Speed*/250));
				Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(6, 0)));
				const int32 PeerCharId = 900000 + S->B;
				const int16 Pvp = (int16)(40 + V);
				{   // 4303 код 1 (вызов принят): [0] код, [1..2] userNum получателя, [7..26] имя, [27..28] группа
					TArray<uint8> A; A.SetNumZeroed(33); A[0] = 1; AsdaBytes::WrI16(&A[1], (int16)S->Self); AsdaBytes::WrI16(&A[27], Pvp);
					const char* Nm = "PvPSosedB"; for (int32 i = 0; Nm[i]; ++i) { A[7 + i] = (uint8)Nm[i]; }
					S->Feed(4303, A);
				}
				{   // 4305 арена: группа @0, charId A @2, charId B @6, тайл-центр @10/@12
					TArray<uint8> A; A.SetNumZeroed(14);
					AsdaBytes::WrI16(&A[0], Pvp); AsdaBytes::WrI32(&A[2], Net->SelectedCharId); AsdaBytes::WrI32(&A[6], PeerCharId);
					AsdaBytes::WrI16(&A[10], (int16)(S->HomeTile.X + 3)); AsdaBytes::WrI16(&A[12], (int16)S->HomeTile.Y);
					S->Feed(4305, A);
				}
				S->Feed(4206, Body4206(S->Self, 0, 1, PeerCharId, 1));
				S->Feed(4206, Body4206(S->B, 0, 1, Net->SelectedCharId, 1));
				S->Expect(Net->IsSelfPvpOn(), TEXT("дуэль: режим включён"));
				PC->SetTarget(nullptr); PC->SetTarget(Bc);
				S->Phase = 0; S->TPhase = FPlatformTime::Seconds();
				if (V == 4)
				{
					// Соперник бьёт меня мечом: 4202 код 1 с уроном больше моего HP — кадр удара доводит HP до 0.
					Net->SetVitalsHp(S->Self, 300, kHp, TEXT("самотест дуэли: мало HP"), true);
					S->Feed(4021, Body4021Weapon(S->B, FindWeapon(IT, 1)));
					Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(1, 0)));
					S->Feed(4202, Body4202(1, S->B, S->Self, { 500, 500, 500, 500 }));
					S->Feed(4204, Body4204(0, S->B, S->Self, {}));
				}
				else if (V == 5) { Net->SetVitalsHp(S->B, 300, kHp, TEXT("самотест дуэли: мало HP"), true); }
				S->bFlag = false;   // атака/навык по сопернику — в опросе через 0,2 с (отказ 5257 выше применяется очередью позже в тике)
			}, [S, Net, V]()
			{
				const double E = FPlatformTime::Seconds() - S->TPhase;
				if (S->Phase == 0 && !S->bFlag && E > 0.2)
				{
					S->bFlag = true;
					AAsdaPlayerController* PCa = S->PC(); AAsdaPlayerCharacter* Bca = S->Peer(S->B);
					if (V == 0) { if (PCa && Bca) { PCa->EngagePlayerFromClick(Bca); } S->Feed(4204, Body4204(0, S->Self, S->B, { 300 })); }
					else if (V != 4) { if (UAsdaSkillUseSubsystem* SU = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU->DispatchByKind(kDuelSkill, -1); } }   // уходит действием 102
				}
				const int16 Pvp = (int16)(40 + V);
				const int32 PeerCharId = 900000 + S->B;
				const bool bWin = V != 2 && V != 4;
				if (S->Phase == 0 && E > (V == 4 || V == 5 ? 3.0 : 1.0))
				{
					if (V == 4) { S->LoserHp = S->HpOf(S->Self); }   // мой HP к итогу (ждём 0)
					S->Phase = 1;
					if (V != 0) { S->HitsMark = S->SkillSent(kDuelSkill); }   // заявок навыка по сопернику в дуэли
					// PvpResult: 4206 подтип 0 обоим, затем 4304 (тип 2 — смерть проигравшего).
					// Как живой журнал 01.11: 4206 и 5306 каждому участнику, затем 4304.
					const TArray<uint8> Hdr1 = { 0x89 };
					S->Feed(4206, Body4206(S->Self, 0, 0, 0, 1));
					Net->DebugFeedPacket(5306, Hdr1, (uint16)S->Self);
					S->Feed(4206, Body4206(S->B, 0, 0, 0, 1));
					Net->DebugFeedPacket(5306, Hdr1, (uint16)S->B);
					const uint8 Type = (V == 6) ? 4 : (V == 7) ? 0 : (V == 8) ? 1 : (V == 9) ? 3 : 2;   // тип итога @14
					if (bWin) { S->Feed(4304, Body4304(Pvp, S->Self, Net->SelectedCharId, S->B, PeerCharId, Type)); }
					else { S->Feed(4304, Body4304(Pvp, S->B, PeerCharId, S->Self, Net->SelectedCharId)); }
					if (V == 1 || V == 3 || V >= 5)
					{
						// Ответ на добивший навык — ПОСЛЕ итога: 5257 код 1, запись по игроку (вид 1, userIdx соперника).
						TArray<uint8> B5; B5.SetNumZeroed(8 + 29);
						B5[0] = 1; AsdaBytes::WrI16(&B5[1], (int16)S->Self); AsdaBytes::WrI16(&B5[3], (int16)kDuelSkill); B5[5] = 1; AsdaBytes::WrI16(&B5[6], 1);
						uint8* R = B5.GetData() + 8; R[0] = 1; AsdaBytes::WrI16(R + 1, (int16)S->B); AsdaBytes::WrI32(R + 3, 900); R[7] = 0x01;
						AsdaBytes::WrI32(R + 11, 900); AsdaBytes::WrI16(R + 19, -1);
						S->Feed(5257, B5);
					}
					if (V == 5)
					{
						// HP игрока — только серверным снимком 4058 (`netHandleHpMpUpdate`): @0 userIdx, @2 макс, @6 тек., @10 тип, @11 навык, @13 второй.
						TArray<uint8> H; H.SetNumZeroed(17);
						AsdaBytes::WrI16(&H[0], (int16)S->B); AsdaBytes::WrI32(&H[2], kHp); AsdaBytes::WrI32(&H[6], 0); AsdaBytes::WrI16(&H[11], -1);
						S->Feed(4058, H);
					}
					S->TPhase = FPlatformTime::Seconds();
				}
				else if (S->Phase == 1 && E > (bWin ? 1.0 : 5.0)
					&& ((S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>() && S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()->GetSelfCharState() == 0) || E > 8.0))
				{
					// Нажатие — когда своё состояние навыка вернулось в 0 (клип добившего навыка доиграл) или по сроку 8 с (тогда защёлка).
					S->QAfter = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>() ? S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()->GetSelfCharState() : -1;
					if (V == 5) { S->LoserHp = S->HpOf(S->B); }   // запись 5257 (урон 900 при HP 300) уже показана: HP соперника 0
					S->Phase = 2;
					S->OutFrom = S->Outs.Num();
					AAsdaPlayerController* PC = S->PC();
					UAsdaWorldSubsystem* WSs = S->WS();
					AAsdaMobActor* M = WSs ? WSs->FindNetMobByRid(kDuelMob) : nullptr;
					if (PC && M) { PC->SetTarget(M); }
					if (UAsdaSkillUseSubsystem* SU = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>())
					{
						UE_LOG(LogAsdaPvpTest, Log, TEXT("САМОТЕСТ-PVP дуэль-навык: перед нажатием состояние %d, активный %d, PvP %d, атака %d, моб %s"),
							SU->GetSelfCharState(), SU->GetActiveSkill(), Net->IsSelfPvpOn() ? 1 : 0, (PC && PC->bPvpAttack) ? 1 : 0, M ? TEXT("есть") : TEXT("НЕТ"));
						SU->DispatchByKind(kDuelSkill, -1);
					}
					S->TPhase = FPlatformTime::Seconds();
				}
				return S->Phase == 2 && FPlatformTime::Seconds() - S->TPhase > 1.5;   // 5256 уходит отложенным действием в следующем тике
			}, [S, Net, V]()
			{
				if (V == 4 || V == 5) { S->Expect(S->LoserHp == 0, FString::Printf(TEXT("HP проигравшего %d (ждём 0 — добивающий удар)"), S->LoserHp)); }
				if (V != 0 && V != 4) { S->Expect(S->HitsMark == 1, FString::Printf(TEXT("в дуэли навык %d по сопернику — 5256 ушёл (%d)"), kDuelSkill, S->HitsMark)); }
				S->Expect(S->QAfter == 0, FString::Printf(TEXT("после дуэли своё состояние навыка вернулось в 0 до 8 с (было %d)"), S->QAfter));
				S->Expect(S->SkillSent(kDuelSkill) == 1, FString::Printf(TEXT("после дуэли (%s) навык %d по мобу — заявка 5256 ушла (%d)"),
					(V == 2 || V == 4) ? TEXT("проигравший") : TEXT("победитель"), kDuelSkill, S->SkillSent(kDuelSkill)));
				if (UAsdaSkillUseSubsystem* SU = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU->SimulateCastResult5257(0, kDuelSkill); }
				Net->bStandCaptureSkillUse = false;
				if (UAsdaWorldSubsystem* WSs = S->WS()) { if (AAsdaMobActor* M = WSs->FindNetMobByRid(kDuelMob)) { M->Destroy(); } }
				if (AAsdaPlayerCharacter* C = S->Ch()) { C->LeaveCampPose(51, TEXT("самотест: сброс")); }
				S->CalmSelf();
			});
		}
		// ★ 06.11.2026 (ревизия координатора): состояние контроля и урон по времени к моменту итога. TargetControlGate/ControlStopsAttack
		//   читают `HasControlState(свой)`: контроль, лежавший к итогу, обязан сняться 5306, иначе навыки и атака закрыты до перезахода.
		//   Слот DoT (6079) — там же; контроль `asda.Dot.ClearOn5306 0` показывает прежний дефект: HP падает и после итога.
		for (const int32 K : { 1, 0 })
		{
			Add(FString::Printf(TEXT("12 конец дуэли: контроль и урон по времени к итогу, ClearOn5306 %d%s"), K, K == 0 ? TEXT(" (контроль)") : TEXT("")), 6.f, [S, Net, K]()
			{
				if (UAsdaSkillUseSubsystem* SU0 = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU0->SimulateCastResult5257(0, kDuelSkill); }
				AsdaCVarSave::SetForTest(TEXT("asda.Dot.ClearOn5306"), K);
				S->Mark(); S->CalmSelf(); S->SetHp(S->Self);
				UAsdaEffectsSubsystem* Fx = S->GI()->GetSubsystem<UAsdaEffectsSubsystem>();
				if (!Fx) { S->Expect(false, TEXT("нет подсистемы эффектов")); return; }
				S->Feed(4206, Body4206(S->Self, 0, 1, 900000 + S->B, 1));
				Fx->AddOrUpdateSkill(S->Self, 3, 0, 0, 4, 600, 0);      // оглушение (постоянное: сам не истечёт)
				Fx->AddOrUpdateSkill(S->Self, 68, 0, 0, 0, 20, 0);      // кровотечение от соперника
				FAsdaDotSlotPkt P; P.Ok = 1; P.CasterIdx = S->B; P.TargetKind = 1; P.TargetIdx = S->Self; P.SkillId = 207; P.StateId = 68;
				P.Level = 5; P.DurationMs = 20000; P.IntervalMs = 500; P.TickDamage = 15;
				Fx->OnDotSlotPacket(P);
				S->Expect(Fx->HasControlState(S->Self) && Fx->FindDotSlot(false, S->Self, 68), TEXT("к итогу: контроль на мне, слот DoT заведён"));
				S->Feed(4206, Body4206(S->Self, 0, 0, 0, 1));
				const TArray<uint8> Hdr1 = { 0x89 };
				Net->DebugFeedPacket(5306, Hdr1, (uint16)S->Self);
				S->Feed(4304, Body4304(50, S->Self, Net->SelectedCharId, S->B, 900000 + S->B, 2));
				S->LoserHp = S->HpOf(S->Self);
			}, [S]() { return S->El() > 2.2; }, [S, K]()
			{
				const UAsdaEffectsSubsystem* Fx = S->GI()->GetSubsystem<UAsdaEffectsSubsystem>();
				S->Expect(Fx && !Fx->HasControlState(S->Self), TEXT("после 5306 контроля нет — гейты TargetControlGate/ControlStopsAttack открыты"));
				const bool bSlot = Fx && Fx->FindDotSlot(false, S->Self, 68);
				const int32 Hp = S->HpOf(S->Self);
				if (K != 0) { S->Expect(!bSlot && Hp == S->LoserHp, FString::Printf(TEXT("5306 снял слот DoT, HP не падает (%d → %d)"), S->LoserHp, Hp)); }
				else { S->Expect(bSlot && Hp < S->LoserHp, FString::Printf(TEXT("контроль: слот живёт, HP падает после итога (%d → %d) — прежний дефект"), S->LoserHp, Hp)); }
				if (UAsdaEffectsSubsystem* FxW = S->GI()->GetSubsystem<UAsdaEffectsSubsystem>()) { FxW->RemoveBySkillState(S->Self, 68, 0); FxW->RemoveBySkillState(S->Self, 3, 0); }
				AsdaCVarSave::SetForTest(TEXT("asda.Dot.ClearOn5306"), 1);
				S->CalmSelf();
			});
		}
		// ★ 06.11.2026 Хвост 5306: слот Дружбы 0 и эффект записи типа 2. Режим 1 — игра (нотис только на 5230), 2 — как оригинал
		//   (`asda.Soul.NoNoticeOn5306 0`: нотис и на 5306), 0 — контроль прежнего порта: нотиса нет, эффект живёт.
		for (const int32 K : { 1, 2, 0 })
		{
			Add(FString::Printf(TEXT("12 конец дуэли: слот Дружбы и эффект типа 2 на 5306, режим %d%s"), K, K == 0 ? TEXT(" (контроль)") : K == 2 ? TEXT(" (как оригинал)") : TEXT("")), 4.f, [S, Net, K]()
			{
				AsdaCVarSave::SetForTest(TEXT("asda.Soul.SlotEnd"), K != 0 ? 1 : 0);
				AsdaCVarSave::SetForTest(TEXT("asda.Fx.StopType2On5306"), K != 0 ? 1 : 0);
				AsdaCVarSave::SetForTest(TEXT("asda.Soul.NoNoticeOn5306"), K == 1 ? 1 : 0);
				S->Mark(); S->CalmSelf();
				UAsdaEffectsSubsystem* Fx = S->GI()->GetSubsystem<UAsdaEffectsSubsystem>();
				UWorld* Wd = S->World.Get();
				if (!Fx || !Wd) { S->Expect(false, TEXT("нет подсистемы эффектов или мира")); return; }
				const auto Slot = [S](int32 Op, int32 User, int32 Id)
				{
					TArray<uint8> B; B.SetNumZeroed(6);
					AsdaBytes::WrI16(&B[0], (int16)User); AsdaBytes::WrI16(&B[2], (int16)Id); AsdaBytes::WrI16(&B[4], 600);
					S->Feed(Op, B);
				};
				const TArray<uint8> Hdr1 = { 0x89 };
				const int32 Want = K != 0 ? 1 : 0;         // нотисов на 5230
				const int32 Want5306 = K == 2 ? 1 : 0;     // нотисов на 5306
				// 5306 при слоте 0 и эффекте записи типа 2
				int32 N0 = Fx->DebugSoulRecallEndNotices();
				Slot(5229, S->Self, 0);
				Fx->AddType2(S->Self, 15, 0, 0);
				AActor* Probe = Wd->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
				TWeakObjectPtr<AActor> ProbeW = Probe;
				if (FAsdaEffectEntry* E = Fx->FindByTypeSub(S->Self, 15, 2)) { E->Fx0 = Probe; E->bFxSpawned = true; }
				Net->DebugFeedPacket(5306, Hdr1, (uint16)S->Self);
				S->Expect(Fx->DebugSoulRecallEndNotices() - N0 == Want5306 && !Fx->FindByTypeSub(S->Self, 0, 1),
					FString::Printf(TEXT("5306 при слоте 0: нотисов 8155 %d (ждём %d), запись снята"), Fx->DebugSoulRecallEndNotices() - N0, Want5306));
				S->Expect(Fx->FindByTypeSub(S->Self, 15, 2) && (K != 0) != ProbeW.IsValid(),
					FString::Printf(TEXT("запись типа 2 пережила 5306, её эффект %s"), ProbeW.IsValid() ? TEXT("живёт") : TEXT("погашен")));
				if (ProbeW.IsValid()) { ProbeW->Destroy(); }
				Fx->RemoveBySkillState(S->Self, 15, 2);
				// Повторный 5306 и слот другого номера нотиса не дают
				N0 = Fx->DebugSoulRecallEndNotices();
				Net->DebugFeedPacket(5306, Hdr1, (uint16)S->Self);
				Slot(5229, S->Self, 3);
				Net->DebugFeedPacket(5306, Hdr1, (uint16)S->Self);
				S->Expect(Fx->DebugSoulRecallEndNotices() == N0, TEXT("без слота 0 нотиса 8155 нет: пустой 5306 и слот 3"));
				// Слот 0 у соседа: нотис только своему
				Slot(5229, S->B, 0);
				Net->DebugFeedPacket(5306, Hdr1, (uint16)S->B);
				S->Expect(Fx->DebugSoulRecallEndNotices() == N0, TEXT("слот 0 соседа: нотиса 8155 нет"));
				// Тот же конец слота пакетом 5230
				Slot(5229, S->Self, 0);
				Slot(5230, S->Self, 0);
				S->Expect(Fx->DebugSoulRecallEndNotices() - N0 == Want && !Fx->FindByTypeSub(S->Self, 0, 1),
					FString::Printf(TEXT("5230 слота 0: нотисов 8155 %d (ждём %d), запись снята"), Fx->DebugSoulRecallEndNotices() - N0, Want));
				S->Expect(!AsdaNotice::Loc(S->GI(), 8155).IsEmpty(), FString::Printf(TEXT("строка 8155 есть в таблице языка: «%s»"), *AsdaNotice::Loc(S->GI(), 8155)));
			}, [S]() { return S->El() > 0.1; }, [S]()
			{
				AsdaCVarSave::SetForTest(TEXT("asda.Soul.SlotEnd"), 1);
				AsdaCVarSave::SetForTest(TEXT("asda.Fx.StopType2On5306"), 1);
				AsdaCVarSave::SetForTest(TEXT("asda.Soul.NoNoticeOn5306"), 1);
				S->CalmSelf();
			});
		}
		// Навыки разных видов после итога: на себя 598 и 584, по цели 705, враждебная площадь вокруг себя 556 (маска оружия — по записи
		//   навыка). Групповые вне отряда не берём: нотис 1223 по эталону, заявки нет.
		Add(TEXT("12 конец дуэли: навыки разных видов после итога"), 12.f, [S, Net]()
		{
			if (UAsdaSkillUseSubsystem* SU0 = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU0->SimulateCastResult5257(0, kDuelSkill); }
			S->Mark(); S->CalmSelf();
			const int32 Codes[] = { 598, 584, 705, 556 };
			TArray<uint8> L; L.SetNumZeroed(34 * 4);
			for (int32 i = 0; i < 4; ++i) { uint8* P = L.GetData() + 34 * i; AsdaBytes::WrI16(P, (int16)Codes[i]); P[2] = 5; P[3] = 1; }
			S->Feed(5251, L);
			Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест дуэли"), true);
			Net->bStandCaptureSkillUse = true;
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(kDuelMob, /*Type*/1, S->HomeTile + FIntPoint(0, 2), /*State*/1, /*Speed*/250));
			S->Feed(4206, Body4206(S->Self, 0, 1, 900000 + S->B, 1));
			S->Feed(4206, Body4206(S->Self, 0, 0, 0, 1));
			const TArray<uint8> Hdr1 = { 0x89 };
			Net->DebugFeedPacket(5306, Hdr1, (uint16)S->Self);
			S->Feed(4304, Body4304(51, S->Self, Net->SelectedCharId, S->B, 900000 + S->B, 2));
			S->Phase = 0; S->bFlag = false; S->TPhase = FPlatformTime::Seconds() + 0.3;
			S->KindSent.Reset();
		}, [S, Net]()
		{
			static const int32 Codes[] = { 598, 584, 705, 556 };
			const double Now = FPlatformTime::Seconds();
			if (S->Phase >= 4) { return true; }
			if (Now < S->TPhase) { return false; }
			const int32 Code = Codes[S->Phase];
			UAsdaSkillUseSubsystem* SU = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>();
			if (!S->bFlag)
			{
				S->bFlag = true;
				const UAsdaSkillDatabase* Db = S->GI()->GetSubsystem<UAsdaSkillDatabase>();
				const FAsdaSkillDef* D = Db ? Db->Get(Code) : nullptr;
				if (D && D->WeaponMask != 0 && S->Ch())
				{
					for (int32 W = 0; W < 18; ++W) { if ((D->WeaponMask >> W) & 1) { S->Ch()->EquippedWeaponSubType = W; break; } }
				}
				if (AAsdaPlayerController* PC = S->PC()) { if (UAsdaWorldSubsystem* WSs = S->WS()) { PC->SetTarget(WSs->FindNetMobByRid(kDuelMob)); } }
				if (SU) { SU->DispatchByKind(Code, -1); }
				S->TPhase = Now + 0.6;
				return false;
			}
			S->KindSent.Add(Code, S->SkillSent(Code));
			if (SU) { SU->SimulateCastResult5257(0, Code); }
			S->bFlag = false; ++S->Phase; S->TPhase = Now + 0.5;
			return false;
		}, [S, Net]()
		{
			for (const TPair<int32, int32>& It : S->KindSent)
			{
				S->Expect(It.Value == 1, FString::Printf(TEXT("после итога навык %d — заявка 5256 ушла (%d)"), It.Key, It.Value));
			}
			S->Expect(S->KindSent.Num() == 4, FString::Printf(TEXT("проверено видов навыков: %d"), S->KindSent.Num()));
			Net->bStandCaptureSkillUse = false;
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
			if (UAsdaWorldSubsystem* WSs = S->WS()) { if (AAsdaMobActor* M = WSs->FindNetMobByRid(kDuelMob)) { M->Destroy(); } }
			S->CalmSelf();
		});
		// Итог сценария: режим шага 1 (следующие сценарии ждут его включённым), сосед B — с мечом рядом.
		Add(TEXT("12 конец дуэли: возврат режима"), 2.f, [S]()
		{
			S->CalmSelf();
			S->Feed(4206, Body4206(S->Self, 0, 2, 0, 1));
			S->Feed(4021, Body4021Weapon(S->B, S->Sword));
			if (AAsdaPlayerCharacter* Bc = S->Peer(S->B)) { Bc->NetSnap(AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(1, 0))); }
		}, nullptr, nullptr);
	}

	// ─────────────────────── 7. Режим 2 и смерть 4053 ───────────────────────
	Add(TEXT("7 режим 2 и 4053"), 12.f, [S, Net]()
	{
		S->Mark(); S->CalmSelf(); S->SetHp(S->B); S->SetMode(2);
		S->Expect(UAsdaWorldSubsystem::GetDamageTextMode() == 2 && !UAsdaWorldSubsystem::ShouldShowDamageText(true)
			&& !UAsdaWorldSubsystem::ShouldShowDamageText(false), TEXT("режим 2: не показывать ни своё, ни чужое"));
		S->SetMode(0);
		S->Expect(UAsdaWorldSubsystem::ShouldShowDamageText(true) && !UAsdaWorldSubsystem::ShouldShowDamageText(false), TEXT("режим 0: только касающееся меня"));
		S->SetMode(2);
		Net->SelfWeaponItemId = S->Sword;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 1; }
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* Bc = S->Peer(S->B);
		if (!PC || !Bc) { S->Expect(false, TEXT("нет контроллера/цели")); return; }
		PC->SetTarget(nullptr); PC->SetTarget(Bc);
		PC->EngagePlayerFromClick(Bc);
		S->Feed(4202, Body4202(1, S->Self, S->B, { 100, 101, 102, 103 }));
	}, [S]()
	{
		const TArray<FOut> O = S->OutsOf(4203);
		while (S->N4203Answered < O.Num()) { ++S->N4203Answered; S->Feed(4204, Body4204(0, S->Self, S->B, { 104, 105 })); }
		return S->Hits(S->Self, S->B).Num() >= 2 || S->El() > 10.0;
	}, [S]()
	{
		const TArray<UAsdaWorldSubsystem::FAsdaPvpHitSeen> H = S->Hits(S->Self, S->B);
		bool bNone = H.Num() >= 2; for (const auto& X : H) { bNone &= X.Style == -1 && X.Dmg > 0; }
		S->Expect(bNone, FString::Printf(TEXT("режим 2: свои удары без надписей, урон в HP идёт — %s"), *StylesStr(H)));
		S->Expect(S->HasNotice(20013), TEXT("режим 2: строка 20013 в чат остаётся"));
		S->CalmSelf(); S->SetMode(1);
		// 4053: A бьёт B, B умирает — серия A снята, замах A погас.
		S->Feed(4202, Body4202(1, S->A, S->B, { 9, 9, 9, 9 }));
		S->Feed(4204, Body4204(0, S->A, S->B, {}));
		UAsdaWorldSubsystem* WSs = S->WS();
		const int32 QBefore = WSs ? WSs->GetPvpQueueLen(S->A) : -1;
		S->Feed(4053, Body4053(S->B, S->A));
		const AAsdaPlayerCharacter* PA = S->Peer(S->A);
		S->Expect(QBefore > 0 && WSs && WSs->GetPvpQueueLen(S->A) == 0 && PA && !PA->IsAttackLoopAlive(),
			FString::Printf(TEXT("4053 по B: серия A снята (очередь %d → %d), замах A погас"), QBefore, WSs ? WSs->GetPvpQueueLen(S->A) : -1));
	});

	// ─────────────────────── 12. ★ 06.11.2026: разрыв связи своего посреди дуэли (последний шаг: сброс сессии уносит соседей) ───────────────────────
	//   Сервер закрывает дуэль сам (`PvpResult` тип 3), клиенту 4304 не приходит: всё держится на сбросе сессии. После нового входа
	//   режим, арена, навык и контроль обязаны быть чистыми, навык — уходить.
	Add(TEXT("12 конец дуэли: разрыв связи своего посреди дуэли"), 8.f, [S, Net]()
	{
		S->Mark(); S->CalmSelf();
		UAsdaSessionResetSubsystem* SR = S->GI()->GetSubsystem<UAsdaSessionResetSubsystem>();
		UAsdaEffectsSubsystem* Fx = S->GI()->GetSubsystem<UAsdaEffectsSubsystem>();
		TArray<uint8> A; A.SetNumZeroed(14);
		AsdaBytes::WrI16(&A[0], 52); AsdaBytes::WrI32(&A[2], Net->SelectedCharId); AsdaBytes::WrI32(&A[6], 900000 + S->B);
		AsdaBytes::WrI16(&A[10], (int16)(S->HomeTile.X + 3)); AsdaBytes::WrI16(&A[12], (int16)S->HomeTile.Y);
		S->Feed(4305, A);
		S->Feed(4206, Body4206(S->Self, 0, 1, 900000 + S->B, 1));
		if (Fx) { Fx->AddOrUpdateSkill(S->Self, 3, 0, 0, 4, 600, 0); }
		if (SR) { SR->Broadcast(EAsdaSessionResetReason::Disconnected); SR->Broadcast(EAsdaSessionResetReason::EnteringWorld); }
		if (GEngine && S->World.IsValid()) { GEngine->Exec(S->World.Get(), TEXT("Asda.World.FakeSelfSpawn 256 256")); }
		S->Phase = 0; S->bFlag = false;
	}, [S, Net]()
	{
		if (S->Phase == 0 && S->El() > 0.8)
		{
			S->Phase = 1;
			const int32 Me = (int32)Net->SelfUserNum;
			AAsdaPlayerCharacter* C = S->Ch();
			const UAsdaEffectsSubsystem* Fx = S->GI()->GetSubsystem<UAsdaEffectsSubsystem>();
			const UAsdaDuelSubsystem* Du = S->GI()->GetSubsystem<UAsdaDuelSubsystem>();
			const UAsdaSkillUseSubsystem* SUc = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>();
			S->Expect(!Net->IsSelfPvpOn() && Du && Du->GetArenaCount() == 0 && SUc && SUc->GetSelfCharState() == 0 && Fx && !Fx->HasControlState(Me),
				FString::Printf(TEXT("после разрыва: PvP %d, арен %d, состояние %d, контроль %d — всё чисто"), Net->IsSelfPvpOn() ? 1 : 0, Du ? Du->GetArenaCount() : -1,
					SUc ? SUc->GetSelfCharState() : -1, (Fx && Fx->HasControlState(Me)) ? 1 : 0));
			if (C)
			{
				C->EquippedWeaponSubType = 10;
				TArray<uint8> L; L.SetNumZeroed(34); AsdaBytes::WrI16(L.GetData(), (int16)kDuelSkill); L[2] = 5; L[3] = 1; S->Feed(5251, L);
				Net->SetVitalsMp(Me, 99999, 99999, TEXT("самотест дуэли"), true);
				Net->bStandCaptureSkillUse = true;
				S->Feed(4013, AsdaTestPackets::MobSpawn4013(kDuelMob, /*Type*/1, AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(0, 4), /*State*/1, /*Speed*/250));
			}
		}
		else if (S->Phase == 1 && S->El() > 1.6)
		{
			S->Phase = 2;
			AAsdaPlayerController* PC = S->PC(); UAsdaWorldSubsystem* WSs = S->WS();
			if (PC && WSs) { PC->SetTarget(WSs->FindNetMobByRid(kDuelMob)); }
			if (UAsdaSkillUseSubsystem* SU = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU->DispatchByKind(kDuelSkill, -1); }
		}
		return S->Phase == 2 && S->El() > 2.8;
	}, [S, Net]()
	{
		S->Expect(S->SkillSent(kDuelSkill) == 1, FString::Printf(TEXT("после разрыва и входа навык %d по мобу — заявка 5256 ушла (%d)"), kDuelSkill, S->SkillSent(kDuelSkill)));
		if (UAsdaSkillUseSubsystem* SU = S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU->SimulateCastResult5257(0, kDuelSkill); }
		Net->bStandCaptureSkillUse = false;
		if (UAsdaWorldSubsystem* WSs = S->WS()) { if (AAsdaMobActor* M = WSs->FindNetMobByRid(kDuelMob)) { M->Destroy(); } }
		S->Self = (int32)Net->SelfUserNum;
		S->Spawned.Reset();   // соседей унёс сброс сессии
	});

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 3.f, [S, Net]()
	{
		S->CalmSelf();
		// ★ 18.10.2026 (мелочь 6): соседи уходят штатным 4011 — он же снимает их запись PvP-режима (проверка — ниже).
		S->DespawnAll();
		if (UAsdaWorldSubsystem* WSs = S->WS()) { WSs->bRecordPvpHits = false; WSs->PvpHitsSeen.Reset(); WSs->PvpNoticesSeen.Reset(); }
		// ★ 18.10.2026 (мелочь 8): свой режим — штатным 4206 (иконка, флаг [160], флаг диспетчера навыков идут делегатом), не правкой
		//   записи: был включён — вернуть его, не был — отбой; запись своего, которой до прогона не было, снимается после отбоя.
		if (S->bSavedPvpOn)
		{
			const UAsdaNetSubsystem::FAsdaPvpState& Sp = S->SavedSelfPvp;
			S->Feed(4206, Body4206(S->Self, Sp.ArgByte, Sp.Mode, Sp.Mode == 4 ? Sp.Team : Sp.Rival, 1));
		}
		else
		{
			S->Feed(4206, Body4206(S->Self, 0, 0, 0, 1));
			if (!S->bHadSelfPvpState) { Net->PvpStates.Remove(S->Self); }
		}
		Net->VitalsByUserIdx.Remove(S->A); Net->VitalsByUserIdx.Remove(S->B);
		Net->SelfWeaponItemId = S->SavedWeapon;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		S->SetMode(S->SavedMode);
		S->StepStartS = FPlatformTime::Seconds();
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		const UAsdaWorldSubsystem* WSs = S->WS();
		S->Expect(!S->Peer(S->A) && !S->Peer(S->B), TEXT("соседи A и B убраны штатным 4011"));
		S->Expect(!Net->GetPvpState(S->A) && !Net->GetPvpState(S->B), TEXT("4011 снял запись PvP-режима ушедших соседей (мелочь 6)"));
		const UAsdaSkillUseSubsystem* SU = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		S->Expect(Net->IsSelfPvpOn() == S->bSavedPvpOn && SU && SU->IsSelfPvpModeFlag() == S->bSavedPvpOn,
			FString::Printf(TEXT("свой PvP-режим возвращён штатным 4206: флаг [160] %d, флаг диспетчера навыков %d (до прогона %d)"),
				Net->IsSelfPvpOn() ? 1 : 0, (SU && SU->IsSelfPvpModeFlag()) ? 1 : 0, S->bSavedPvpOn ? 1 : 0));
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		(void)WSs;
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaPvpTest, TEXT("САМОТЕСТ-PVP"), S->Ok, S->Fail);
	});

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
		if (!St->Gate.Pass(Wd))
		{
			// Отказ на отложенном старте: состояние прогона не оставляем.
			Wd->GetTimerManager().ClearTimer(St->Tick);
			if (UAsdaWorldSubsystem* WSs = St->WS()) { WSs->bRecordPvpHits = false; }
			St->SetMode(St->SavedMode);
			return;
		}
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
			UE_LOG(LogAsdaPvpTest, Log, TEXT("САМОТЕСТ-PVP шаг %s"), *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCombatPvp(
	TEXT("asda.Combat.SelfTestPvp"),
	TEXT("Самотест PvP (синхронизация боя, Ф4/Ф5): 4206 и переключатель 2/8; своя атака (4201/4203 под перехватом, ответы 4202/4204) мечом и "
	     "луком — тела, userIdx цели, частота 4203 по оружию, цифры/стили/чат/HP цели; коды отказа → нотисы; роль жертвы и наблюдателя; "
	     "режимы показа цифр 0/1/2; 4053; границы PvP-серии (сосед PvP → моб → PvP, свой поздний 4204) с контролем "
	     "asda.Combat.PvpSeriesGuard 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Combat.SelfTestPvp"));
		FAsdaCombatPvpSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
