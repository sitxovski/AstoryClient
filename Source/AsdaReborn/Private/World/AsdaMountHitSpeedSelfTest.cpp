// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ВЕРХОМ УДАРИЛИ — ТРАНСПОРТ ПРОПАЛ — СЕРВЕР ДЁРНУЛ НАЗАД» (04.11.2026). [ИСПРАВЛЕНИЕ]/[ОРИГИНАЛ]
//
// Разбор — у `AsdaResyncSelfSegmentAfterSpeed` (AsdaWorldSubsystem.cpp). Пакеты подаются НАСТОЯЩИМ приёмником (`DebugFeedPacket` →
//   `HandlePacket`): 5454 (снятие предметного усилителя = высадка) и 6071 (новая скорость) — та же пара и в том же порядке, что
//   шлёт сервер при ударе по всаднику (`sub_4F2170` → `RecalcMoveSpeed@0x5588F0(…,1)`), при добровольной высадке
//   (`PopFunctionItemState_Toggle@0x4F1F60`); 6071 без 5454 — состояния скорости навыков (ускорение/замедление/оглушение:
//   `sub_48A440` → `RecalcMoveSpeed(…,1)`) и посадка (`sub_4EF270` case 15). Отправки перехватываются (в сеть ничего не уходит).
//   Ответ сервера на 4006 подаётся делегатом сети (тот же путь, что пакет 4007).
//
// МОДЕЛЬ СЕРВЕРА (по декомпилу, `CharMove@0x544EB0` / `MoveSyncProcess@0x5427D0` / `CharStop@0x545C70`): длительность отрезка =
//   путь / скорость считается при перекате (приём 4006); тик двигает точку на Δt·ТЕКУЩАЯ скорость и останавливает отрезок по
//   истечении длительности; 6071 меняет только скорость; 4006/4008 на ходу: клиент впереди сервера > 1000 мс → поправка
//   (4007 код 5 / 4009 Revision). Модель ведётся по НАСТОЯЩИМ перехваченным 4006 и поданным 6071.
//
// ЧТО ПРОВЕРЯЕТ:
//   A. Посадка; 6071 стоя — кэш скорости (LastServerSpeed) новый, скорость шага не трогается (эталон: гейт «идёт»).
//   B. Верхом «в точку» 0.56 → удар: 5454 + 6071 0.35 — транспорт снят, скорость шага сразу 0.35, кэш 0.35, ровно один 4006 из
//      текущей точки к той же цели; ответ сервера (грант) — ход идёт дальше; замер ≈ 350 ед./с; ДОХОД ДО ЦЕЛИ — по модели сервера
//      расхождение < 1000 мс (поправки нет).
//   B7. То же при asda.Move.SpeedChangeResync 0 (как оригинал) — модель даёт > 1000 мс = рывок назад (воспроизведение бага).
//   C. 6071 при 4006 в пути — повторного 4006 нет.   D. Ускорение посреди хода — 4006; ответ-отказ (код 0) — ход не обрывается.
//   E. После 4008 — 4006 нет.   F. Цель в своей клетке — нет.   N. До цели < 200 ед. — нет.   G. Ключ 0 — нет, скорость применена.
//   H. LocalPrevSpeed — новая.   L. Мёртвый — нет.   M. Под оглушением (состояние 3 в списке) — нет.
//   I. Высадка стоя — 4006 нет, кэш 0.35.   S. Замедление навыком (6071 без 5454) посреди хода — 4006, модель < 1000 мс.
//   J. 5454 НЕ-транспорта (бустер) верхом — транспорт остаётся; ключ asda.Mount.RemoveOnlyMountCode 0 — прежнее.
//   K. Сосед: его 5454 + 6071 — спешен, шаг 0.35, мой кэш и отправки не тронуты; эхо его 4006 (4007) — идёт к цели без снапа.
// Консоль: `asda.SelfTest.MountHitSpeed [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ВЫСАДКА», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"
#include "AsdaDataTableCache.h"  // AsdaDt::Cached — таблица премиум-предметов (код бустера)
#include "Items/AsdaItemDbRows.h" // FAsdaSpecialItemlistRow
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"   // оглушение (состояние 3) в своём списке — раздел M
#include "World/AsdaCoord.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountHitTest, Log, All);

extern int32 GAsdaSpeedResyncCount;   // AsdaWorldSubsystem.cpp — счётчик пересинхронизаций отрезка по 6071

namespace AsdaMountHitTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.MountHitSpeed");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ВЫСАДКА");
	static constexpr int32 kMountCode = 522;       // транспорт (subType 15), им же пользуется самотест приседа
	static constexpr int32 kPeerUserNum = 7791;    // сосед-заглушка
	static constexpr uint16 kDebugSelfNum = 41;    // свой номер офлайн, если 4003 не приходил
	static constexpr float kMountSpeed = 0.56f;    // конь +60 %: 0.35 + 60·0.0035 (сервер `RecalcMoveSpeed`)
	static constexpr float kRunSpeed = 0.35f;      // пешком
	static constexpr float kRideLen = 1500.f;      // длина хода разделов B/B7 (остаток после удара > 1.7 с — порог рывка у оригинала)

	// Перехваченный 4006: точка и цель.
	struct FSent { FVector2D Cur, Dst; };

	// ── Модель отрезка сервера (правила — в шапке). Время — FPlatformTime (мс). ──
	struct FSrvModel
	{
		bool bOn = false;                 // модель ведётся
		FVector2D Start, Pos, Dst, Dir;   // начало отрезка, точка сервера, цель, направление
		double DurMs = 0, ElMs = 0;       // длительность отрезка (зафиксирована при перекате) и прошедшее
		float Speed = kRunSpeed;          // текущая скорость сервера (`CUser+21758`)
		double LastS = 0;                 // момент последнего продвижения
		int32 Revisions = 0;              // поправок на 4006 (клиент впереди > 1000 мс)

		void Advance(double NowS)
		{
			if (bOn && ElMs < DurMs)
			{
				const double Dt = FMath::Min((NowS - LastS) * 1000.0, DurMs - ElMs);
				if (Dt > 0) { Pos += Dir * (float)(Dt * Speed); ElMs += Dt; }
			}
			LastS = NowS;
		}
		void Begin(const FVector2D& From, const FVector2D& To, float V)
		{
			bOn = true; Speed = V; Start = Pos = From; Dst = To;
			Dir = (To - From).GetSafeNormal(); DurMs = FVector2D::Distance(From, To) / FMath::Max(0.0001f, V); ElMs = 0;
			LastS = FPlatformTime::Seconds();
		}
		// Мс, на которые точка P впереди сервера вдоль отрезка (проверка CharMove/CharStop «на ходу»).
		double AheadMs(const FVector2D& P) const
		{
			return (FVector2D::Distance(Start, P) - FVector2D::Distance(Start, Pos)) / FMath::Max(0.0001f, Speed);
		}
		// Приём 4006 на ходу: сверка, затем перекат с текущей скоростью.
		void On4006(const FVector2D& Cur, const FVector2D& To)
		{
			if (!bOn) { return; }
			Advance(FPlatformTime::Seconds());
			const double Ahead = AheadMs(Cur);
			FVector2D From = Pos;
			if (Ahead > 1000.0) { ++Revisions; }   // «CharReMove Hack Position» — сервер оставляет свою точку
			else if (Ahead > 0.0) { From = Cur; }   // клиент впереди в допуске — сервер принимает точку клиента
			const float V = Speed;
			Begin(From, To, V);
		}
		void OnSpeed(float V) { if (bOn) { Advance(FPlatformTime::Seconds()); Speed = V; } }
	};

	// Состояние прогона (живёт между таймерами).
	struct FState
	{
		int32 Ok = 0, Fail = 0;                    // счётчики проверок
		uint32 CapGen = 0;                         // поколение перехвата отправок
		TArray<FSent> Sent4006;                    // перехваченные 4006
		int32 Sent4008 = 0;                        // перехваченные 4008
		bool bConn0 = false;                       // исходный признак игрового соединения
		int32 Self = 0;                            // свой userNum
		FVector2D Origin = FVector2D::ZeroVector;  // точка старта прогона
		FVector2D Dst = FVector2D::ZeroVector;     // цель текущего хода
		FVector2D P0 = FVector2D::ZeroVector;      // точка замера скорости
		double T0 = 0;                             // момент начала замера
		int32 Mark = 0;                            // счётчик 4006 на начало проверки
		FSrvModel Srv;                             // модель сервера
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;   // свой персонаж
		TWeakObjectPtr<AAsdaPlayerController> PC;  // свой контроллер
		TWeakObjectPtr<UAsdaNetSubsystem> Net;     // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;    // мир
		TWeakObjectPtr<UWorld> World;              // мир движка

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaMountHitTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaMountHitTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		int32 New4006() const { return Sent4006.Num() - Mark; }
	};

	// Шаг сценария: пауза перед ним и тело; тело возвращает true — шаг закончен, false — повторить через 0.1 с (ожидание).
	struct FStep { float Delay; float TimeoutS; TFunction<bool()> Fn; FString Name; };

	static void RunSteps(TSharedRef<FState> S, TSharedRef<TArray<FStep>> Steps, int32 Idx, double StartedS, TFunction<void()> Finish)
	{
		UWorld* W = S->World.Get();
		if (!W || !S->Me.IsValid() || !S->Net.IsValid()) { Finish(); return; }
		if (!Steps->IsValidIndex(Idx)) { Finish(); return; }
		const FStep& St = (*Steps)[Idx];
		FTimerHandle H;
		const float Delay = StartedS < 0 ? St.Delay : 0.1f;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([S, Steps, Idx, StartedS, Finish]()
		{
			const FStep& St2 = (*Steps)[Idx];
			const double Now = FPlatformTime::Seconds();
			const double Began = StartedS < 0 ? Now : StartedS;
			const bool bDone = St2.Fn();
			if (bDone) { RunSteps(S, Steps, Idx + 1, -1.0, Finish); return; }
			if (Now - Began > St2.TimeoutS)
			{
				S->Expect(false, FString::Printf(TEXT("%s — ожидание дольше %.1f с"), *St2.Name, St2.TimeoutS));
				RunSteps(S, Steps, Idx + 1, -1.0, Finish);
				return;
			}
			RunSteps(S, Steps, Idx, Began, Finish);
		}), FMath::Max(0.01f, Delay), false);
	}

	// 5454: i16 userIdx, i32 itemCode (эталон `CItem_OnRemoveByTable2Record`).
	static void Feed5454(UAsdaNetSubsystem* Net, int32 Uidx, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(6);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI32(&B[2], Code);
		Net->DebugFeedPacket(5454, B);
	}

	// 6071: i16 userIdx, i32 charId (не читается), f32 скорость (сервер `RecalcMoveSpeed`, `push 0Ah`).
	static void Feed6071(UAsdaNetSubsystem* Net, int32 Uidx, float Speed)
	{
		TArray<uint8> B; B.SetNumZeroed(10);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI32(&B[2], 0);
		FMemory::Memcpy(&B[6], &Speed, 4);
		Net->DebugFeedPacket(6071, B);
	}

	// Свой 6071: сервер меняет скорость в момент рассылки (модель) — затем пакет приходит клиенту.
	static void SelfSpeed(FState& S, float V) { S.Srv.OnSpeed(V); Feed6071(S.Net.Get(), S.Self, V); }

	// Первый код таблицы премиум-предметов с subType ≠ 15 (бустер скорости subType 4, если есть).
	static int32 FindNonMountCode()
	{
		const UDataTable* T = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/ItemDB/DT_special_itemlist.DT_special_itemlist"));
		if (!T) { return 0; }
		int32 Any = 0;
		for (const TPair<FName, uint8*>& It : T->GetRowMap())
		{
			const FAsdaSpecialItemlistRow* R = reinterpret_cast<const FAsdaSpecialItemlistRow*>(It.Value);
			if (!R || R->Code <= 0 || R->SubType == 15) { continue; }
			if (R->SubType == 4) { return R->Code; }
			if (!Any) { Any = R->Code; }
		}
		return Any;
	}

	static bool Near(float A, float B, float Eps = 0.0005f) { return FMath::Abs(A - B) <= Eps; }

	// Персонаж идёт «в точку» Dst со скоростью V; сервер «идёт» (4006 принят, 4008 не было), ответ на него пришёл; модель — тот же отрезок.
	static void StartRide(FState& S, const FVector2D& Dst, float V)
	{
		AAsdaPlayerCharacter* Me = S.Me.Get(); UAsdaNetSubsystem* Net = S.Net.Get();
		S.Dst = Dst;
		Me->NetMoveFromActor(Dst, V);
		Net->bActionPending = true;
		Net->ClearMoveRequestInFlight(TEXT("самотест высадки: ответ на ход пришёл"));
		S.Srv.Begin(Me->GetGamePosXZ(), Dst, V);
		S.Srv.Revisions = 0;
	}

	// Свой ответ сервера на последний перехваченный 4006 (тот же делегат, что пакет 4007): код Res, скорость сервера.
	static void Answer4007(FState& S, int32 Res)
	{
		if (!S.Sent4006.Num()) { return; }
		const FSent X = S.Sent4006.Last();
		S.Net->OnEntityMove.Broadcast(S.Self, Res, X.Cur, X.Dst, Res == 1 ? S.Srv.Speed : 0.f, false, 2, -1);
	}

	// Дошёл ли свой персонаж до цели (или встал).
	static bool Arrived(const FState& S)
	{
		const AAsdaPlayerCharacter* Me = S.Me.Get();
		return !Me || !Me->IsMoving() || FVector2D::Distance(Me->GetGamePosXZ(), S.Dst) < 3.f;
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
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

		auto Finish = [WW, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (UAsdaEffectsSubsystem* Fx = Wd->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>())
			{
				Fx->RemoveBySkillState(Fx->SelfUserIdx(), 3, 0);
			}
			if (AAsdaPlayerCharacter* Me = S->Me.Get()) { Me->StopMove(); Me->ClearMount(); Me->SetAnimState(EAsdaPlayerState::Idle); }
			if (UAsdaNetSubsystem* Net = S->Net.Get())
			{
				Net->bInGameConnected = S->bConn0;
				Net->bActionPending = false;
				Net->ClearMoveRequestInFlight(TEXT("самотест высадки: конец"));
				Net->LastServerSpeed = kRunSpeed;
				if (S->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, S->CapGen); S->CapGen = 0; }
			}
			AsdaCVarSave::RestoreAll(kCmd);
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaMountHitTest, kPrefix, S->Ok, S->Fail,
				*FString::Printf(TEXT(" (4006: %d, 4008: %d, пересинхронизаций: %d)"), S->Sent4006.Num(), S->Sent4008, GAsdaSpeedResyncCount));
		};

		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, S, bQuit, Finish]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			UAsdaWorldSubsystem* WS = Wd->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
			if (!Me || !Net || !WS) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сети / мира")); return; }
			S->Me = Me; S->PC = PC; S->Net = Net; S->WS = WS; S->World = Wd;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 4006 && P && Len >= 37)
				{
					FSent X;
					X.Cur = FVector2D(AsdaBytes::RdF32(P + 16 + 5), AsdaBytes::RdF32(P + 16 + 9));
					X.Dst = FVector2D(AsdaBytes::RdF32(P + 16 + 13), AsdaBytes::RdF32(P + 16 + 17));
					S->Sent4006.Add(X);
					S->Srv.On4006(X.Cur, X.Dst);
				}
				else if (Op == 4008) { ++S->Sent4008; }
			}, 0, kCmd);
			if (S->CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.SpeedChangeResync"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Mount.RemoveOnlyMountCode"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Mount.PortalReapply"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.StunHardStop"), 1);
			if (!Net->IsSelfUserNumKnown()) { Net->SetSelfUserNumForDebug(kDebugSelfNum); }
			S->Self = (int32)Net->SelfUserNum;
			S->bConn0 = Net->bInGameConnected;
			Net->bInGameConnected = true;   // путь «в игре» (отправки перехвачены)
			Net->LastServerSpeed = kRunSpeed;
			Me->StopMove(); Me->SetAnimState(EAsdaPlayerState::Idle);
			S->Origin = Me->GetGamePosXZ();

			TSharedRef<TArray<FStep>> St = MakeShared<TArray<FStep>>();
			auto Add = [St](const TCHAR* Name, float Delay, float Timeout, TFunction<bool()> Fn) { St->Add(FStep{ Delay, Timeout, MoveTemp(Fn), Name }); };

			// ── A. Посадка и 6071 стоя ──
			Add(TEXT("A"), 0.01f, 1.f, [S]()
			{
				AAsdaPlayerCharacter* M = S->Me.Get(); UAsdaNetSubsystem* N = S->Net.Get();
				N->OnMountState.Broadcast(S->Self, kMountCode, true);
				S->Expect(M->IsMounted(), FString::Printf(TEXT("A1 посадка: транспорт %d на своём персонаже (uidx %d)"), kMountCode, S->Self));
				const float Spd0 = M->GetMoveSpeed();
				S->Mark = S->Sent4006.Num();
				Feed6071(N, S->Self, kMountSpeed);
				S->Expect(Near(N->LastServerSpeed, kMountSpeed) && Near(M->GetMoveSpeed(), Spd0) && S->New4006() == 0,
					FString::Printf(TEXT("A2 6071 стоя: кэш %.4f (= %.2f), скорость шага не тронута (%.4f), 4006 нет (%d)"),
						N->LastServerSpeed, kMountSpeed, M->GetMoveSpeed(), S->New4006()));
				StartRide(*S, S->Origin + FVector2D(kRideLen, 0.f), kMountSpeed);
				return true;
			});
			// ── B. Удар верхом посреди хода ──
			Add(TEXT("B"), 0.3f, 1.f, [S]()
			{
				AAsdaPlayerCharacter* M = S->Me.Get(); UAsdaNetSubsystem* N = S->Net.Get();
				const float Walked = FVector2D::Distance(M->GetGamePosXZ(), S->Origin);
				S->Expect(M->IsMovingToPoint() && Near(M->GetMoveSpeed(), kMountSpeed) && Walked > 100.f,
					FString::Printf(TEXT("B0 верхом идём «в точку»: скорость %.4f, за 0.3 с пройдено %.0f ед."), M->GetMoveSpeed(), Walked));
				S->Mark = S->Sent4006.Num();
				const int32 R0 = GAsdaSpeedResyncCount;
				Feed5454(N, S->Self, kMountCode);   // сервер: `sub_4F2170` → `sub_4F1B40` → bcast 5454
				SelfSpeed(*S, kRunSpeed);           // сервер: `RecalcMoveSpeed(…,1)` → bcast 6071 (себе тоже)
				const FVector2D Now = M->GetGamePosXZ();
				S->Expect(!M->IsMounted(), TEXT("B1 удар: 5454 — транспорт снят"));
				S->Expect(Near(M->GetMoveSpeed(), kRunSpeed) && Near(N->LastServerSpeed, kRunSpeed),
					FString::Printf(TEXT("B2 6071: скорость шага %.4f и кэш %.4f — сразу бег %.2f (не скорость транспорта)"), M->GetMoveSpeed(), N->LastServerSpeed, kRunSpeed));
				const bool bOne = S->New4006() == 1 && GAsdaSpeedResyncCount == R0 + 1;
				const FSent X = bOne ? S->Sent4006.Last() : FSent{};
				S->Expect(bOne && FVector2D::Distance(X.Cur, Now) < 5.f && FVector2D::Distance(X.Dst, S->Dst) < 1.f,
					FString::Printf(TEXT("B3 ровно один 4006 из текущей точки (%.0f,%.0f) к той же цели (%.0f,%.0f); ушло %d"),
						X.Cur.X, X.Cur.Y, X.Dst.X, X.Dst.Y, S->New4006()));
				Answer4007(*S, 1);   // ответ сервера на пересчёт — грант
				S->Expect(M->IsMovingToPoint() && FVector2D::Distance(M->GetMoveDstGame(), S->Dst) < 1.f && Near(M->GetMoveSpeed(), kRunSpeed, 0.02f),
					FString::Printf(TEXT("B4 после ответа сервера ход идёт к прежней цели, скорость %.4f"), M->GetMoveSpeed()));
				S->P0 = M->GetGamePosXZ(); S->T0 = FPlatformTime::Seconds();
				return true;
			});
			Add(TEXT("B5"), 0.5f, 1.f, [S]()
			{
				AAsdaPlayerCharacter* M = S->Me.Get();
				const double Dt = FMath::Max(0.01, FPlatformTime::Seconds() - S->T0);
				const float V = (float)(FVector2D::Distance(S->P0, M->GetGamePosXZ()) / Dt);
				S->Expect(M->IsMovingToPoint() && FMath::Abs(V - kRunSpeed * 1000.f) < kRunSpeed * 1000.f * 0.15f,
					FString::Printf(TEXT("B5 замер хода после высадки: %.0f ед./с (бег %.0f, транспорт %.0f)"), V, kRunSpeed * 1000.f, kMountSpeed * 1000.f));
				return true;
			});
			Add(TEXT("B6 доход до цели"), 0.1f, 8.f, [S]()
			{
				if (!Arrived(*S)) { return false; }
				S->Srv.Advance(FPlatformTime::Seconds());
				const double Ahead = S->Srv.AheadMs(S->Me->GetGamePosXZ());
				S->Expect(Ahead < 1000.0 && S->Srv.Revisions == 0,
					FString::Printf(TEXT("B6 дошли до цели: по модели сервера клиент впереди на %.0f мс (< 1000 — 4008 без «Revision Pos»), поправок 4006: %d"),
						Ahead, S->Srv.Revisions));
				return true;
			});
			// ── B7. Ключ 0 (как оригинал) — воспроизведение рывка ──
			Add(TEXT("B7 старт"), 0.2f, 1.f, [S]()
			{
				AAsdaPlayerCharacter* M = S->Me.Get(); UAsdaNetSubsystem* N = S->Net.Get();
				M->StopMove(); N->bActionPending = false;
				N->OnMountState.Broadcast(S->Self, kMountCode, true);
				Feed6071(N, S->Self, kMountSpeed);
				AsdaCVarSave::SetForTest(TEXT("asda.Move.SpeedChangeResync"), 0);
				StartRide(*S, S->Origin, kMountSpeed);   // обратно к старту — та же проходимая полоса
				return true;
			});
			Add(TEXT("B7 удар"), 0.3f, 1.f, [S]()
			{
				S->Mark = S->Sent4006.Num();
				Feed5454(S->Net.Get(), S->Self, kMountCode);
				SelfSpeed(*S, kRunSpeed);
				S->Expect(S->New4006() == 0 && Near(S->Me->GetMoveSpeed(), kRunSpeed),
					FString::Printf(TEXT("B7a ключ 0: скорость %.4f применена, 4006 нет (%d) — как оригинал"), S->Me->GetMoveSpeed(), S->New4006()));
				return true;
			});
			Add(TEXT("B7 доход до цели"), 0.1f, 8.f, [S]()
			{
				if (!Arrived(*S)) { return false; }
				S->Srv.Advance(FPlatformTime::Seconds());
				const double Ahead = S->Srv.AheadMs(S->Me->GetGamePosXZ());
				S->Expect(Ahead > 1000.0,
					FString::Printf(TEXT("B7b ключ 0: модель сервера встала, не дойдя %.0f ед.; клиент впереди на %.0f мс (> 1000) — 4008 дал бы «Revision Pos» = рывок назад (баг воспроизведён)"),
						FVector2D::Distance(S->Srv.Pos, S->Dst), Ahead));
				AsdaCVarSave::SetForTest(TEXT("asda.Move.SpeedChangeResync"), 1);
				S->Me->StopMove(); S->Net->bActionPending = false;
				return true;
			});
			// ── S. Замедление навыком (6071 без 5454) посреди хода пешком ──
			Add(TEXT("S старт"), 0.2f, 1.f, [S]() { StartRide(*S, S->Me->GetGamePosXZ() + FVector2D(kRideLen, 0.f), kRunSpeed); return true; });
			Add(TEXT("S замедление"), 0.3f, 1.f, [S]()
			{
				S->Mark = S->Sent4006.Num();
				SelfSpeed(*S, 0.245f);   // −30 %: 0.35 − 30·0.0035
				S->Expect(Near(S->Me->GetMoveSpeed(), 0.245f) && S->New4006() == 1,
					FString::Printf(TEXT("S1 замедление навыком посреди хода: скорость %.4f, 4006 пересчёта ушёл (%d)"), S->Me->GetMoveSpeed(), S->New4006()));
				Answer4007(*S, 1);
				return true;
			});
			Add(TEXT("S ускорение"), 0.3f, 1.f, [S]()
			{
				S->Mark = S->Sent4006.Num();
				SelfSpeed(*S, 0.455f);   // ускорение +30 %
				S->Expect(Near(S->Me->GetMoveSpeed(), 0.455f) && S->New4006() == 1,
					FString::Printf(TEXT("S2 ускорение навыком посреди хода: скорость %.4f, 4006 ушёл (%d)"), S->Me->GetMoveSpeed(), S->New4006()));
				Answer4007(*S, 1);
				return true;
			});
			Add(TEXT("S доход до цели"), 0.1f, 8.f, [S]()
			{
				if (!Arrived(*S)) { return false; }
				S->Srv.Advance(FPlatformTime::Seconds());
				const double Ahead = S->Srv.AheadMs(S->Me->GetGamePosXZ());
				const double Behind = FVector2D::Distance(S->Srv.Pos, S->Dst);
				S->Expect(FMath::Abs(Ahead) < 1000.0 && Behind < 60.0 && S->Srv.Revisions == 0,
					FString::Printf(TEXT("S3 дошли: модель сервера у цели (недобег/перебег %.0f ед.), расхождение %.0f мс — поправки нет"), Behind, Ahead));
				S->Me->StopMove(); S->Net->bActionPending = false;
				return true;
			});
			// ── Ветки без 4006 и прочее на новом ходу ──
			Add(TEXT("ветки"), 0.2f, 1.f, [S]()
			{
				AAsdaPlayerCharacter* M = S->Me.Get(); UAsdaNetSubsystem* N = S->Net.Get(); AAsdaPlayerController* PC = S->PC.Get();
				const FVector2D Back = (FVector2D::Distance(M->GetGamePosXZ(), S->Origin) > 600.f) ? S->Origin : M->GetGamePosXZ() + FVector2D(kRideLen, 0.f);
				StartRide(*S, Back, kRunSpeed);
				// C. 6071 при 4006 в пути
				S->Mark = S->Sent4006.Num();
				N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
				Feed6071(N, S->Self, 0.49f);
				S->Expect(Near(M->GetMoveSpeed(), 0.49f) && S->New4006() == 0,
					FString::Printf(TEXT("C 6071 при 4006 в пути: скорость %.4f, повторного 4006 нет (%d)"), M->GetMoveSpeed(), S->New4006()));
				N->ClearMoveRequestInFlight(TEXT("самотест высадки C"));
				// D. Ускорение посреди хода; ответ-отказ кодом 0 — ход не обрывается
				S->Mark = S->Sent4006.Num();
				Feed6071(N, S->Self, kMountSpeed);
				S->Expect(Near(M->GetMoveSpeed(), kMountSpeed) && S->New4006() == 1,
					FString::Printf(TEXT("D1 ускорение 0.49 → %.2f посреди хода: 4006 ушёл (%d)"), kMountSpeed, S->New4006()));
				Answer4007(*S, 0);
				S->Expect(M->IsMovingToPoint() && FVector2D::Distance(M->GetMoveDstGame(), S->Dst) < 1.f,
					TEXT("D2 сервер ответил отказом 0 на пересчёт — ход продолжается (маршрута нет, отказ не обрывает)"));
				N->ClearMoveRequestInFlight(TEXT("самотест высадки D"));
				// H. LocalPrevSpeed
				PC->LocalPrevSpeed = kMountSpeed;
				Feed6071(N, S->Self, kRunSpeed);
				S->Expect(Near(PC->LocalPrevSpeed, kRunSpeed), FString::Printf(TEXT("H скорость прежнего гранта локального старта: %.4f (= %.2f)"), PC->LocalPrevSpeed, kRunSpeed));
				N->ClearMoveRequestInFlight(TEXT("самотест высадки H"));
				PC->LocalPrevSpeed = 0.f;
				// E. После 4008
				S->Mark = S->Sent4006.Num();
				N->bActionPending = false;
				Feed6071(N, S->Self, kMountSpeed);
				S->Expect(Near(M->GetMoveSpeed(), kMountSpeed) && S->New4006() == 0,
					FString::Printf(TEXT("E после 4008: скорость %.4f, 4006 нет (%d)"), M->GetMoveSpeed(), S->New4006()));
				N->bActionPending = true;
				// G. Ключ 0
				AsdaCVarSave::SetForTest(TEXT("asda.Move.SpeedChangeResync"), 0);
				S->Mark = S->Sent4006.Num();
				Feed6071(N, S->Self, kRunSpeed);
				S->Expect(Near(M->GetMoveSpeed(), kRunSpeed) && S->New4006() == 0,
					FString::Printf(TEXT("G asda.Move.SpeedChangeResync 0: скорость %.4f применена, 4006 нет (%d)"), M->GetMoveSpeed(), S->New4006()));
				AsdaCVarSave::SetForTest(TEXT("asda.Move.SpeedChangeResync"), 1);
				// M. Под оглушением (состояние 3 в своём списке) — тем же кадром, до остановки стана
				if (UAsdaEffectsSubsystem* Fx = S->World->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>())
				{
					Fx->AddOrUpdateSkill(Fx->SelfUserIdx(), 3, 0, 0, 4, 600, 0);
					S->Mark = S->Sent4006.Num();
					Feed6071(N, S->Self, 0.175f);   // оглушение: сервер делит скорость на 2
					S->Expect(S->New4006() == 0 && PC->IsMoveInputLockedByState(),
						FString::Printf(TEXT("M под оглушением (состояние 3): 4006 нет (%d) — сервер ответил бы 6, отказ закрыл бы маршрут"), S->New4006()));
					Fx->RemoveBySkillState(Fx->SelfUserIdx(), 3, 0);
				}
				else { S->Expect(false, TEXT("M нет подсистемы эффектов")); }
				// F. Цель в своей клетке
				const FVector2D Pf = M->GetGamePosXZ();
				const FIntPoint Tf = AsdaCoord::GameToTile(Pf);
				FVector2D Df((float)Tf.X * 100.f + 50.f, (float)Tf.Y * 100.f + 50.f);
				if (FVector2D::Distance(Df, Pf) < 5.f) { Df.X = (float)Tf.X * 100.f + 5.f; }
				StartRide(*S, Df, kRunSpeed);
				S->Mark = S->Sent4006.Num();
				Feed6071(N, S->Self, kMountSpeed);
				S->Expect(S->New4006() == 0, FString::Printf(TEXT("F цель в своей клетке (%d,%d): 4006 нет (%d)"), Tf.X, Tf.Y, S->New4006()));
				// N. До цели < 200 ед. (другая клетка)
				StartRide(*S, M->GetGamePosXZ() + FVector2D(150.f, 0.f), kRunSpeed);
				S->Mark = S->Sent4006.Num();
				Feed6071(N, S->Self, kMountSpeed);
				S->Expect(S->New4006() == 0, FString::Printf(TEXT("N до цели 150 ед.: 4006 нет (%d) — недобег сервера ниже порога"), S->New4006()));
				// L. Мёртвый
				StartRide(*S, M->GetGamePosXZ() + FVector2D(800.f, 0.f), kRunSpeed);
				M->SetAnimState(EAsdaPlayerState::Dead);
				S->Mark = S->Sent4006.Num();
				Feed6071(N, S->Self, kMountSpeed);
				S->Expect(S->New4006() == 0, FString::Printf(TEXT("L мёртвый: 4006 нет (%d)"), S->New4006()));
				M->SetAnimState(EAsdaPlayerState::Idle);
				M->StopMove();
				// I. Высадка стоя
				N->OnMountState.Broadcast(S->Self, kMountCode, true);
				Feed6071(N, S->Self, kMountSpeed);
				S->Mark = S->Sent4006.Num();
				Feed5454(N, S->Self, kMountCode);
				Feed6071(N, S->Self, kRunSpeed);
				S->Expect(!M->IsMounted() && Near(N->LastServerSpeed, kRunSpeed) && S->New4006() == 0,
					FString::Printf(TEXT("I высадка стоя: транспорт снят %d, кэш %.4f, 4006 нет (%d)"), M->IsMounted() ? 0 : 1, N->LastServerSpeed, S->New4006()));
				// J. 5454 не-транспорта верхом
				const int32 NonMount = FindNonMountCode();
				N->OnMountState.Broadcast(S->Self, kMountCode, true);
				if (NonMount > 0)
				{
					Feed5454(N, S->Self, NonMount);
					S->Expect(M->IsMounted(), FString::Printf(TEXT("J1 5454 усилителя %d (subType ≠ 15) верхом — транспорт остаётся (эталон case 0xF)"), NonMount));
					AsdaCVarSave::SetForTest(TEXT("asda.Mount.RemoveOnlyMountCode"), 0);
					Feed5454(N, S->Self, NonMount);
					S->Expect(!M->IsMounted(), TEXT("J2 ключ asda.Mount.RemoveOnlyMountCode 0 — прежний порт: любой 5454 ссаживает"));
					AsdaCVarSave::SetForTest(TEXT("asda.Mount.RemoveOnlyMountCode"), 1);
				}
				else { S->Expect(false, TEXT("J в DT_special_itemlist нет кода с subType ≠ 15")); }
				M->ClearMount();
				return true;
			});
			// ── K. Сосед верхом ──
			Add(TEXT("K"), 0.2f, 1.f, [S]()
			{
				AAsdaPlayerCharacter* M = S->Me.Get(); UAsdaNetSubsystem* N = S->Net.Get(); UAsdaWorldSubsystem* WS2 = S->WS.Get();
				WS2->DebugSpawnFakePeer(kPeerUserNum, TEXT("Всадник"), M->GetGamePosXZ() + FVector2D(300.f, 300.f));
				AAsdaPlayerCharacter* Pe = Cast<AAsdaPlayerCharacter>(WS2->ActorByUserIdx(kPeerUserNum));
				if (!Pe) { S->Expect(false, TEXT("K сосед-заглушка не появился")); return true; }
				N->OnMountState.Broadcast(kPeerUserNum, kMountCode, true);
				const FVector2D Pp = Pe->GetGamePosXZ();
				const FVector2D PDst = Pp + FVector2D(kRideLen, 0.f);
				N->OnEntityMove.Broadcast(kPeerUserNum, 1, Pp, PDst, kMountSpeed, false, 2, -1);
				const float MyCache = N->LastServerSpeed;
				S->Mark = S->Sent4006.Num();
				Feed5454(N, kPeerUserNum, kMountCode);
				Feed6071(N, kPeerUserNum, kRunSpeed);
				S->Expect(!Pe->IsMounted() && Pe->IsMoving() && Near(Pe->GetMoveSpeed(), kRunSpeed),
					FString::Printf(TEXT("K1 сосед: спешен, идёт дальше со скоростью %.4f (= %.2f)"), Pe->GetMoveSpeed(), kRunSpeed));
				S->Expect(Near(N->LastServerSpeed, MyCache) && S->New4006() == 0,
					FString::Printf(TEXT("K2 чужой 6071: мой кэш не тронут (%.4f), моих 4006 нет (%d)"), N->LastServerSpeed, S->New4006()));
				// Его клиент (с исправлением) шлёт 4006 пересчёта — сервер эхом рассылает 4007 соседям: его точка, та же цель, 0.35.
				const FVector2D PNow = Pe->GetGamePosXZ();
				N->OnEntityMove.Broadcast(kPeerUserNum, 1, PNow, PDst, kRunSpeed, false, 2, -1);
				S->Expect(Pe->IsMovingToPoint() && FVector2D::Distance(Pe->GetGamePosXZ(), PNow) < 5.f && Near(Pe->GetMoveSpeed(), kRunSpeed)
					&& FVector2D::Distance(Pe->GetMoveDstGame(), PDst) < 1.f,
					FString::Printf(TEXT("K3 эхо его пересчёта (4007): идёт к той же цели %.4f, без снапа (сдвиг %.1f ед.)"),
						Pe->GetMoveSpeed(), FVector2D::Distance(Pe->GetGamePosXZ(), PNow)));
				Pe->StopMove();
				return true;
			});

			RunSteps(S, St, 0, -1.0, Finish);
		}), A.Delay + 2.f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaMountHitSpeedSelfTest(
	TEXT("asda.SelfTest.MountHitSpeed"),
	TEXT("Самотест высадки с транспорта на ходу (удар/добровольно) и смены скорости навыком: 5454 + 6071 — транспорт снят, скорость сразу бег, "
	     "4006 пересчёта отрезка (модель сервера: без рывка назад; ключ 0 — рывок воспроизводится); ветки 4006 в пути / после 4008 / своя клетка / "
	     "< 200 ед. / мёртв / оглушение / ключ 0 / стоя / бустер / сосед. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaMountHitTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
