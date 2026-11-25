// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ СВОЕГО ХОДА 4006 → 4007/4009 → 4008 (27.09.2026, отчёт `Reports/owner_0927/move_4007.md`). [ПОРТ]
//
// ★ ЗАЧЕМ. В ветке своего хода 16.09 было три регрессии подряд (разрыв маршрута `CancelRoute`, потерянный финальный
//   4008, «чужая скорость управляла моим бегом»). Правка 27.09 меняет старт и скорость шага (ключ
//   `asda.Move.EtalonSpeedSync`), читает поле +25 и ведёт бит 2 «запрос хода в пути». Самотест прогоняет ВСЕ сценарии
//   ветки при ОБОИХ значениях ключа и сверяет: пакеты 4006/4008 (перехват, в сеть ничего), позицию, скорость шага,
//   маршрут, бит 2 и поле +25.
//
// ★ КАК. Офлайн: штатные крюки `Asda.Load.CurrentMap 3` + `Asda.World.FakeSelfSpawn 256 256` (как у самотеста 4008),
//   на время прогона `bInGameConnected = true` и ПОЛНЫЙ перехват исходящих (`ArmSendCapture(…, 0)`: сокета нет, всё
//   уходит в перехватчик). Ответы сервера подаются через ШТАТНЫЙ приёмник `UAsdaNetSubsystem::DebugFeedPacket` —
//   разбор 4007/4009/4062 не копируется. «Сервер-автоответчик» повторяет `CharMove@0x544EB0`/`CharStop@0x545C70`:
//   на каждый 4006 через RTT шлёт 4007 result=1 (эхо тела, скорость сервера), на 4008 — эхо 4009.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): каждая проверка эталонной схемы имеет ожидание
//   ПРОТИВОПОЛОЖНОГО результата при ключе 0 (старт до гранта, скорость ровно серверная, снап > 150, …) — проверка,
//   не отличающая ключи, была бы слепой.
//
//   ⚠ Не все проверки имеют пару по ключу: T2, T3, T5, T6, T12, T13 — регрессионные, одинаковые при обоих
//   значениях (ревизия С2). Контрольные пары есть у T1, T4, T7, T8, T9b, T10, T11, T14, K1*, C2a–C2e; у C2f/C2g пара —
//   по коду отказа / до-после сброса (бит ведётся при обоих ключах).
// ★ 27.09.2026 (исправления ревизии, `move_4007_fix.md`): автоответчик повторяет и отказ `CharMove` «та же клетка»
//   (тайл dst == тайл cur → result 0, GameServer.c:124534). Сценарии K1a–K1c — подход в своей клетке (бой, навык) и
//   контроль «клик в своей клетке»; C2a–C2g — все читатели бита 2 (клик по миру, `CheckGates`, два места `AttackTick`,
//   подход ради навыка) и его снятие отказом боя 4027 и сбросом сессии.
//
// Консоль: `asda.Move.SelfTest4007 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-4007», итог — «САМОТЕСТ-4007 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "World/AsdaSkillCastLock.h"   // R5-fix8: сброс отложенного клика шага C2a

#if !UE_BUILD_SHIPPING
#include "Misc/App.h"                          // FApp::GetDeltaTime — длина кадра для порогов «тронулся за N мс»
#include "Test/AsdaSelfTestSelfReady.h"   // ★ 01.11.2026: изоляция пакета — свой персонаж заводится и без ссылки мира/номера
#include "AsdaBytes.h"   // ★ N6 (В1 дедупа №2): единые читалки little-endian

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "Test/AsdaSelfTestRunGate.h"   // RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "World/AsdaCoord.h"                   // перевод UE ↔ игра
#include "World/AsdaPlayerController.h"        // маршрут, SendRouteLeg, ключ схемы
#include "World/AsdaPlayerCharacter.h"         // скорость/цель шага, поле +25
#include "World/AsdaWorldSubsystem.h"          // сосед-заглушка (DebugSpawnFakePeer)
#include "World/AsdaPathfinder.h"              // выбор проходимых направлений
#include "UI/AsdaEffectsSubsystem.h"           // состояние «Паника» (id 4) своему
#include "UI/AsdaSkillUseSubsystem.h"          // гейт бита 2 в `CheckGates`, подход ради навыка
#include "UI/AsdaSkillDatabase.h"              // запись навыка для подхода
#include "Npc/AsdaMobActor.h"                  // моб-заглушка для боевого подхода
#include "Components/CapsuleComponent.h"       // радиус хитбокса моба (как в `AttackTick`)
#include "AsdaSessionReset.h"                  // причина сброса сессии
#include "UI/AsdaWarehouseSubsystem.h"         // самотест клавиш: свиток возврата склада (верхний гейт)
#include "UI/AsdaWarShopSubsystem.h"           // самотест клавиш: военный магазин 67200 (отказ 110611)
#include "UI/AsdaPremiumShopSubsystem.h"       // самотест клавиш: элитный магазин 6000 (гейт окон)
#include "UI/AsdaCastBarSubsystem.h"           // самотест клавиш: состояние каста 14 (молчаливый отказ)
#include "World/AsdaWalkGrid.h"                // самотест клавиш: поиск стены (KB19)
#include "UI/AsdaWindowSubsystem.h"            // ★ 22.10 самотест клавиш KB25: модалка 1500 с зажатой W
#include "UI/AsdaMinimapSubsystem.h"           // ★ 22.10 KB35: автопуть под станом
#include "World/AsdaLoadManagerSubsystem.h"    // ★ 22.10 KB35: номер текущей карты для автопути
#include "UI/AsdaChatWindowSubsystem.h"        // ★ 22.10 KB26: фокус в строке чата с зажатой W
#include "UI/AsdaUiVisibilitySubsystem.h"      // ★ 22.10 KB27: Alt+Z с зажатой W
#include "Framework/Application/SlateApplication.h"   // ★ 22.10 KB25–KB27: клавиши путём игрока (события Slate)
#include "GameFramework/SpringArmComponent.h"   // ★ 22.10 KB47: положение камеры своего персонажа
#include "Engine/GameViewportClient.h"         // ★ 22.10 KB25–KB27: гейт вьюпорта `IgnoreInput` (журнал прогона)
#include "Misc/OutputDevice.h"                 // перехват строки лога гейта навыков
#include "Misc/ScopeLock.h"                    // защита перехваченных строк
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMove4007Test, Log, All);

// Друг `UAsdaNetSubsystem`, `AAsdaPlayerController`, `AAsdaPlayerCharacter` (объявлено в их заголовках).
struct FAsdaMove4007SelfTest
{
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args, UWorld* W);
	// ★ 27.09.2026 — самотест хода клавишами `asda.Move.SelfTestKeyboard` (оба значения `asda.Move.KeyboardEtalon`,
	//   отчёт `Reports/owner_0927/wasd_impl.md`). Живёт в этом же друге: доступ к закрытым полям сети, контроллера и
	//   персонажа уже выдан ему, и общие заголовки (`AsdaNetSubsystem.h`) не трогаются ради второго друга.
	static void RunKeyboard(const TArray<FString>& Args, UWorld* W);

	// ── Доступ к закрытым полям друзей (вызывается из лямбд шагов) ──
	static float  Speed(const AAsdaPlayerCharacter* C)        { return C->MoveSpeed; }
	static FVector2D Dst(const AAsdaPlayerCharacter* C)       { return C->MoveDstGame; }
	static FVector2D Dir(const AAsdaPlayerCharacter* C)       { return C->MoveDirGame; }
	static bool   ToPoint(const AAsdaPlayerCharacter* C)      { return C->bMoveToPoint; }
	static void   SetRoute(AAsdaPlayerController* PC, const TArray<FVector2D>& Nodes)
	{
		PC->CancelRoute();
		PC->Route = Nodes; PC->RouteIdx = 0; PC->RouteRenderId = -1;
		PC->RouteFinalDst = Nodes.Last();
		PC->RouteReplanTime = FPlatformTime::Seconds() + 1.0;
	}
	static void   SendLeg(AAsdaPlayerController* PC, const FVector2D& Cur, const FVector2D& Wp) { PC->SendRouteLeg(Cur, Wp); }
	static int32  RouteIdx(const AAsdaPlayerController* PC)   { return PC->RouteIdx; }
	static FVector2D RouteNode(const AAsdaPlayerController* PC) { return PC->HasRoute() ? PC->Route[PC->RouteIdx] : FVector2D::ZeroVector; }
	static bool   LegUnsent(const AAsdaPlayerController* PC)  { return PC->bRouteLegUnsent; }
};

namespace AsdaMove4007TestLocal
{
	// Один перехваченный 4006/4008.
	struct FCap
	{
		uint16 Op = 0;
		uint16 UserNum = 0;
		int16  Sub = 0;
		int16  Tgt = -1;
		FVector2D Cur = FVector2D::ZeroVector, Dst = FVector2D::ZeroVector;
		double AtS = 0.0;
	};
	// Отложенная подача пакета «от сервера».
	struct FInject
	{
		double AtS = 0.0;
		int32 Op = 0;
		TArray<uint8> Body;
	};
	// Шаг сценария: Start — один раз; Poll — каждые 20 мс после MinSec, true = шаг закончен; Check — итог шага.
	struct FStep
	{
		FString Name;
		float MinSec = 0.f;
		float TimeoutSec = 6.f;
		TFunction<void()> Start;
		TFunction<bool()> Poll;
		TFunction<void()> Check;
	};
	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TWeakObjectPtr<UWorld> World;
		TArray<FCap> Caps;
		TArray<FInject> Queue;
		TArray<FStep> Steps;
		int32 StepIdx = 0;
		bool bStepStarted = false;
		double StepStartS = 0.0;
		int32 Ok = 0, Fail = 0;
		int32 Key = 1;                         // текущее значение ключа схемы
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Move.SelfTest4007"), false };   // ★ СРЕДН-1: общий флаг отложенного старта (спавн + исполнитель)
		uint32 Gen = 0;
		bool bWasInGame = false;
		FTimerHandle Tick;
		bool bQuit = false;
		// Сервер-автоответчик
		bool bAutoServer = true;
		float SrvSpeed = 0.35f;
		double RttS = 0.08;
		bool bOverrideCur = false;             // подменить cur гранта (мягкая коррекция сервера)
		FVector2D OverrideCur = FVector2D::ZeroVector;
		// Геометрия
		FVector2D Home = FVector2D::ZeroVector, DirA = FVector2D(1, 0), DirB = FVector2D(0, 1);
		// Рабочие поля шагов
		int32 Mark = 0;                        // число перехватов на момент начала шага
		float ProbeSpeed = -1.f;               // скорость, снятая в первый кадр движения
		FVector2D ProbePos = FVector2D::ZeroVector;
		FVector2D ProbePos2 = FVector2D::ZeroVector;
		double ProbeS = 0.0;
		bool bFlag = false;
		// ★ 27.09.2026 (исправления ревизии) — поля сценариев K1/C2:
		bool bSameCellRefuse = true;           // автоответчик: тайл dst == тайл cur → result 0 (как `CharMove`)
		FVector2D HomeK = FVector2D::ZeroVector;   // точка у угла клетки: от неё диагональ Diag остаётся в клетке ≥ 120 ед.
		FVector2D Diag = FVector2D(1, 1).GetSafeNormal();   // направление внутрь клетки от HomeK
		bool bHomeKOk = false;                 // точка HomeK найдена (проходимая диагональ)
		TWeakObjectPtr<AAsdaMobActor> Mob;     // моб-заглушка сценария (уничтожается в сбросе)
		int32 SkillCode = -1;                  // навык для подхода (дальность 4..5 м), −1 = не найден
		float SkillRangeU = 0.f;               // его дальность в игровых единицах
		int32 SavedWeapon = 0;                 // надетое оружие до прогона (на время прогона 0 → дальность кулаков)

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		APlayerController* RawPC() const { UWorld* W = World.Get(); return W ? W->GetFirstPlayerController() : nullptr; }
		AAsdaPlayerController* PC() const { return Cast<AAsdaPlayerController>(RawPC()); }
		AAsdaPlayerCharacter* Ch() const { APlayerController* P = RawPC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		FVector2D Pos() const { AAsdaPlayerCharacter* C = Ch(); return C ? C->GetGamePosXZ() : FVector2D::ZeroVector; }
		int32 Count(uint16 Op, int32 From = 0) const
		{
			int32 R = 0; for (int32 i = From; i < Caps.Num(); ++i) { if (Caps[i].Op == Op) { ++R; } } return R;
		}
		const FCap* LastOf(uint16 Op) const
		{
			for (int32 i = Caps.Num() - 1; i >= 0; --i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr;
		}
		// Одна проверка: печать и счёт.
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-4007 %s [ключ=%d] %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Key, Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
	};

	// ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`; прежние имена и типы — переходники.
	static float RdF(const uint8* P) { return AsdaBytes::RdF32(P); }
	static int16 RdI16(const uint8* P) { return (int16)AsdaBytes::RdI16(P); }

	// Тело 4007/4009 (27 б) — раскладка `Recv_EntityMove@0xF3C2C0` / `CharMove@0x544EB0`.
	static TArray<uint8> MoveBody(uint8 Res, int16 Uidx, int16 Sub, const FVector2D& Cur, const FVector2D& Dst, float Spd, int16 Tgt)
	{
		TArray<uint8> B; B.SetNumZeroed(27);
		B[0] = Res;
		FMemory::Memcpy(&B[1], &Uidx, 2);
		FMemory::Memcpy(&B[3], &Sub, 2);
		const float Cx = (float)Cur.X, Cz = (float)Cur.Y, Dx = (float)Dst.X, Dz = (float)Dst.Y;
		FMemory::Memcpy(&B[5], &Cx, 4);  FMemory::Memcpy(&B[9], &Cz, 4);
		FMemory::Memcpy(&B[13], &Dx, 4); FMemory::Memcpy(&B[17], &Dz, 4);
		FMemory::Memcpy(&B[21], &Spd, 4);
		FMemory::Memcpy(&B[25], &Tgt, 2);
		return B;
	}
	// Тело 4062 (25 б) — раскладка `netRecvEntityPosSync@0xF3C150`.
	static TArray<uint8> PosSyncBody(uint8 Kind, int16 Uidx, const FVector2D& Dst, float Spd)
	{
		TArray<uint8> B; B.SetNumZeroed(25);
		B[0] = Kind;
		FMemory::Memcpy(&B[1], &Uidx, 2);
		const float Dx = (float)Dst.X, Dz = (float)Dst.Y;
		FMemory::Memcpy(&B[13], &Dx, 4); FMemory::Memcpy(&B[17], &Dz, 4);
		FMemory::Memcpy(&B[21], &Spd, 4);
		return B;
	}
	// Опкоды, которые офлайн-гейт самотеста проверяет на «в сети» (первый оператор `Run`).
	static const uint16 kGuardOps[] = { 4006, 4008 };
	// Аргумент `delay=N` (секунды до спавна) — чистая функция, без побочных эффектов.
	// ★ R5-fix-c (13.11.2026): своя `ParseDelay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.
	// Перехват строк лога с заданной подстрокой (гейт навыков пишет отказ по биту 2 только в лог — эталон молчит).
	struct FLogGrab : public FOutputDevice
	{
		FString Needle;                        // искомая подстрока
		int32 Hits = 0;                        // сколько строк её содержали
		FCriticalSection Lock;                 // лог может прийти с любого потока
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			if (V && FCString::Strstr(V, *Needle)) { FScopeLock L(&Lock); ++Hits; }
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
	};
	// Моб-заглушка в игровой точке G (renderId Rid) на высоте персонажа — для `AttackTick` и подхода ради навыка.
	static AAsdaMobActor* SpawnMob(UWorld* W, const FVector2D& G, float Z, int32 Rid)
	{
		if (!W) { return nullptr; }
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SP.bDeferConstruction = true;          // признаки сетевого моба — до BeginPlay (как `SpawnNetMobDynamic`)
		AAsdaMobActor* M = W->SpawnActor<AAsdaMobActor>(AAsdaMobActor::StaticClass(), SP);
		if (!M) { return nullptr; }
		M->bNetSpawned = true; M->NetRenderId = Rid; M->CurHp = 100; M->MaxHp = 100;
		const FVector2D Ue = AsdaCoord::GameToUe2D(G);
		M->FinishSpawning(FTransform(FVector(Ue.X, Ue.Y, Z)));
		M->SetActorLocation(FVector(Ue.X, Ue.Y, Z));   // высота = персонажа: дальность навыка трёхмерная
		return M;
	}
	// Радиус хитбокса моба — ровно как в `AAsdaPlayerController::AttackTick` (по модулю).
	static float MobRadius(const AAsdaMobActor* M) { return (M && M->PickHitbox) ? FMath::Abs(M->PickHitbox->GetScaledCapsuleRadius()) : 0.f; }
	static int16 SelfUidx(const FState& S) { const UAsdaNetSubsystem* N = S.N(); return N ? (int16)N->SelfUserNum : 0; }
	static bool Near(const FVector2D& A, const FVector2D& B, float Tol) { return FVector2D::Distance(A, B) <= Tol; }
	static FString V2(const FVector2D& V) { return FString::Printf(TEXT("(%.0f,%.0f)"), V.X, V.Y); }
}

void FAsdaMove4007SelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMove4007TestLocal;
	// ★ ПЕРВЫМ ОПЕРАТОРОМ — офлайн-гейт: в живой сессии самотест подменил бы признак «в мире», ставил бы перехват
	//   всего исходящего трафика и слал бы поддельные ответы сервера в свой приёмник. (Ревизия МЕЛК 1: разбор
	//   `delay=` и список опкодов вынесены из тела — гейт теперь буквально первый оператор.)
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Move.SelfTest4007"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	const float Delay = AsdaSelfTestKit::DelayArg(Args);   // секунды до спавна своего персонажа

	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaMove4007Test, Warning, TEXT("САМОТЕСТ-4007: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W;
	for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { S->bQuit = true; } }
	S->Gate.bQuit = S->bQuit;   // ★ СРЕДН-1: отказ на отложенном старте при `quit` закрывает процесс

	IConsoleVariable* KeyVar = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Move.EtalonSpeedSync"));
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходные значения ключей EtalonSpeedSync / KeyboardEtalon (фаза ключа 0 ставит
	//   пару 0/0) запоминает `AsdaCVarSave::SetForTest` при первом касании, итог возвращает их `RestoreAll` вместе с SetBy
	//   (прежде — снимок KeyDefault/KbDefault и возврат с SetByConsole).

	// ─────────────────────────── Сброс между сценариями ───────────────────────────
	auto Reset = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* N = S->N();
		// ★ Сценарии K1/C2: боевой цикл, подход ради навыка и моб-заглушка гасятся ДО снапа домой.
		if (PC)
		{
			PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
			PC->NextAttackTickTime = 0.0; PC->NextApproachTime = 0.0; PC->TargetActor = nullptr; PC->bAutoRunActive = false;
			if (UGameInstance* G = PC->GetGameInstance())
			{
				if (UAsdaSkillUseSubsystem* SU = G->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU->CancelSkillApproach(TEXT("самотест: сброс")); }
			}
		}
		if (C) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; }
		if (AAsdaMobActor* M = S->Mob.Get()) { M->Destroy(); }
		S->Mob = nullptr;
		if (PC) { PC->CancelRoute(); }
		if (C) { C->StopMove(); C->NetSnap(S->Home); C->MoveTargetRenderId = -1; }
		if (N) { N->ClearMoveRequestInFlight(TEXT("самотест: сброс")); N->bActionPending = false; N->bSelfDead = false; }
		S->Queue.Reset();
		S->bAutoServer = true; S->bOverrideCur = false; S->SrvSpeed = 0.35f;
		S->Mark = S->Caps.Num(); S->ProbeSpeed = -1.f; S->ProbeS = 0.0; S->bFlag = false;
		S->bSameCellRefuse = true;
	};
	auto Add = [S](const FString& Name, float MinSec, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.MinSec = MinSec; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Idle = [S]() -> bool { AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); return PC && C && !PC->HasRoute() && !C->IsMoving(); };
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* N = S->N()) { N->DebugFeedPacket(Op, B); } };

	// ─────────────────────────── Подготовка (один раз) ───────────────────────────
	Add(TEXT("подготовка"), 0.f, 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
		if (!C || !PC) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->Home = C->GetGamePosXZ();
		// Проходимые направления: A — прямая 700 от дома; B — поворот, три отрезка маршрута (400/400/300).
		const UAsdaWalkGrid* G = C->WalkGrid;
		auto LineOk = [G](const FVector2D& P0, const FVector2D& P1)
		{
			return !G || AsdaPath::IsLineWalkable(G, P0 / AsdaCoord::kTileUnits, P1 / AsdaCoord::kTileUnits);
		};
		bool bFound = false;
		for (int32 a = 0; a < 16 && !bFound; ++a)
		{
			const FVector2D A(FMath::Cos(a * PI / 8.f), FMath::Sin(a * PI / 8.f));
			if (!LineOk(S->Home, S->Home + A * 700.f)) { continue; }
			for (int32 b = 0; b < 16 && !bFound; ++b)
			{
				const FVector2D B(FMath::Cos(b * PI / 8.f), FMath::Sin(b * PI / 8.f));
				if (FMath::Abs(FVector2D::DotProduct(A, B)) > 0.8f) { continue; }
				const FVector2D N1 = S->Home + A * 400.f, N2 = N1 + B * 400.f, N3 = N2 + A * 300.f;
				if (LineOk(N1, N2) && LineOk(N2, N3)) { S->DirA = A; S->DirB = B; bFound = true; }
			}
		}
		S->Expect(bFound, FString::Printf(TEXT("проходимые направления найдены: дом %s, A=(%.2f,%.2f), B=(%.2f,%.2f), сетка %s"),
			*V2(S->Home), S->DirA.X, S->DirA.Y, S->DirB.X, S->DirB.Y, G ? TEXT("есть") : TEXT("нет")));
		// ★ K1: точка у угла клетки (отступ 15) и диагональ внутрь — отрезок до 120 ед. остаётся в той же клетке 100×100.
		//   Ищем по соседним клеткам и четырём углам ту, где диагональ проходима.
		{
			const FIntPoint T0 = AsdaCoord::GameToTile(S->Home);
			const FVector2D Corners[4] = { FVector2D(15, 15), FVector2D(85, 15), FVector2D(15, 85), FVector2D(85, 85) };
			const FVector2D Dirs[4] = { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1) };
			for (int32 r = 0; r <= 3 && !S->bHomeKOk; ++r)
			{
				for (int32 dx = -r; dx <= r && !S->bHomeKOk; ++dx)
				{
					for (int32 dy = -r; dy <= r && !S->bHomeKOk; ++dy)
					{
						for (int32 c = 0; c < 4 && !S->bHomeKOk; ++c)
						{
							const FVector2D P = FVector2D((T0.X + dx) * AsdaCoord::kTileUnits, (T0.Y + dy) * AsdaCoord::kTileUnits) + Corners[c];
							const FVector2D Dd = Dirs[c].GetSafeNormal();
							if (LineOk(P, P + Dd * 110.f)) { S->HomeK = P; S->Diag = Dd; S->bHomeKOk = true; }
						}
					}
				}
			}
			S->Expect(S->bHomeKOk, FString::Printf(TEXT("точка K1 у угла клетки: %s, диагональ (%.2f,%.2f), клетка (%d,%d)"),
				*V2(S->HomeK), S->Diag.X, S->Diag.Y, AsdaCoord::GameToTile(S->HomeK).X, AsdaCoord::GameToTile(S->HomeK).Y));
		}
		// ★ K1b/C2e: навык с дальностью 4..5 м (стоп-точка 0.85·R, после допуска прибытия 45 цель уже в зоне).
		if (UGameInstance* G3 = Net->GetGameInstance())
		{
			if (const UAsdaSkillDatabase* Db = G3->GetSubsystem<UAsdaSkillDatabase>())
			{
				for (int32 Code = 501; Code < 1255 && S->SkillCode < 0; ++Code)
				{
					const FAsdaSkillDef* D = Db->Get(Code);
					if (D && D->RangeMeters >= 4 && D->RangeMeters <= 5) { S->SkillCode = Code; S->SkillRangeU = D->RangeMeters * 100.f; }
				}
			}
		}
		S->Expect(S->SkillCode > 0, FString::Printf(TEXT("навык для подхода: %d, дальность %.0f"), S->SkillCode, S->SkillRangeU));
		// Дальность удара = кулаков (`AttackRange`): надетое оружие на время прогона снимается и возвращается в итоге.
		S->SavedWeapon = Net->SelfWeaponItemId;
		Net->SelfWeaponItemId = 0;
		// Перехват ВСЕГО исходящего: в сеть ничего; 4006/4008 разбираем, автоответчик кладёт ответы в очередь.
		TWeakPtr<FState> WS = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WS](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WS.Pin();
			if (!St || !Pl || Len < 43 || (Op != 4006 && Op != 4008)) { return; }
			const uint8* T = Pl + 16;
			FCap Cp; Cp.Op = Op; Cp.AtS = FPlatformTime::Seconds();
			FMemory::Memcpy(&Cp.UserNum, T + 1, 2);
			Cp.Sub = RdI16(T + 3); Cp.Tgt = RdI16(T + 25);
			Cp.Cur = FVector2D(RdF(T + 5), RdF(T + 9)); Cp.Dst = FVector2D(RdF(T + 13), RdF(T + 17));
			St->Caps.Add(Cp);
			if (!St->bAutoServer) { return; }
			FInject In; In.AtS = Cp.AtS + St->RttS;
			const int16 U = SelfUidx(*St);
			if (Op == 4006)
			{
				In.Op = 4007;
				// ★ Правило `CharMove@0x544EB0` (GameServer.c:124534): ход в свою клетку — result 0 (эхо тела). Клетка
				//   сервера — тайл его точки; у нас ей соответствует cur пакета.
				const bool bSameCell = St->bSameCellRefuse && AsdaCoord::GameToTile(Cp.Dst) == AsdaCoord::GameToTile(Cp.Cur);
				In.Body = MoveBody(bSameCell ? 0 : 1, U, Cp.Sub, St->bOverrideCur ? St->OverrideCur : Cp.Cur, Cp.Dst, St->SrvSpeed, Cp.Tgt);
			}
			else
			{
				In.Op = 4009;
				In.Body = MoveBody(0, U, 0, Cp.Cur, Cp.Cur, St->SrvSpeed, Cp.Tgt);
			}
			St->Queue.Add(MoveTemp(In));
		}, (uint16)0, TEXT("asda.Move.SelfTestKeyboard"));
		// ★ 22.10.2026: прежние сценарии проверяют ход ПО ГРАНТУ (эталон) — локальный старт на их время выключен (`asda.Move.LocalStart 0`);
		//   сам локальный старт проверяет `asda.Move.SelfTestKeyboard` (KB40–KB46).
		AsdaCVarSave::SetForTest(TEXT("asda.Move.LocalStart"), 0);
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		// Сосед-заглушка для поля +25 чужого (штатный путь снапшота 4005/4065).
		if (UGameInstance* G2 = Net->GetGameInstance())
		{
			if (UAsdaWorldSubsystem* WSys = G2->GetSubsystem<UAsdaWorldSubsystem>())
			{
				WSys->DebugSpawnFakePeer(77, TEXT("Сосед4007"), S->Home + FVector2D(0.f, -300.f));
			}
		}
	}, []() { return true; }, []() {});

	// ─────────────────────────── Сценарии (прогоняются при ключе 1 и 0) ───────────────────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("фаза ключ=%d"), K), 0.f, 1.f, [S, K, KeyVar, Reset]()
		{
			S->Key = K;
			if (KeyVar) { AsdaCVarSave::SetForTest(TEXT("asda.Move.EtalonSpeedSync"), K); }
			// ★ Ревизия WASD, МЕЛК-5: «ключ 0 = прежний порт ДОСЛОВНО» — это пара `EtalonSpeedSync 0` + `KeyboardEtalon 0`.
			//   Нештатная пара 0/1 игнорирует грант, которого никто не ждёт (контроль T11 «любой грант двигает» — только при 0/0).
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardEtalon"), K);
			Reset();
			S->Expect(AAsdaPlayerController::IsEtalonSpeedSync() == (K != 0), TEXT("ключ asda.Move.EtalonSpeedSync применён"));
		}, []() { return true; }, []() {});

		// T1. Клик-мув одним отрезком: старт до/после гранта, скорость шага, финальный 4008, бит 2.
		Add(TEXT("T1 клик-мув, один отрезок"), 0.05f, 6.f, [S, K, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* N = S->N();
			if (!PC || !C || !N) { return; }
			PC->RequestMoveTo(S->Home + S->DirA * 500.f);
			const FCap* L = S->LastOf(4006);
			S->Expect(S->Count(4006, S->Mark) == 1 && L && Near(L->Cur, S->Home, 2.f) && Near(L->Dst, S->Home + S->DirA * 500.f, 2.f)
				&& L->Sub == 2 && L->Tgt == -1 && L->UserNum == N->SelfUserNum,
				FString::Printf(TEXT("4006: %d шт., cur=%s dst=%s подтип=%d +25=%d userNum=%u"), S->Count(4006, S->Mark),
					L ? *V2(L->Cur) : TEXT("-"), L ? *V2(L->Dst) : TEXT("-"), L ? L->Sub : -99, L ? L->Tgt : -99, L ? L->UserNum : 0));
			S->Expect(N->bMoveRequestInFlight, TEXT("бит 2 взведён отправкой 4006"));
			if (K) { S->Expect(!C->IsMoving(), TEXT("до гранта 4007 персонаж СТОИТ (эталон)")); }
			else   { S->Expect(C->IsMoving(), TEXT("до гранта персонаж уже идёт (прежний порт — контроль)")); }
		}, [S, Idle]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			// Скорость снимаем в первый кадр после гранта (ответ пришёл — бит 2 снят).
			if (C && S->ProbeSpeed < 0.f && C->IsMoving() && S->N() && !S->N()->bMoveRequestInFlight) { S->ProbeSpeed = FAsdaMove4007SelfTest::Speed(C); }
			return Idle();
		}, [S, K]()
		{
			const FVector2D Goal = S->Home + S->DirA * 500.f;
			const FCap* L8 = S->LastOf(4008);
			S->Expect(Near(S->Pos(), Goal, 50.f), FString::Printf(TEXT("дошёл до цели: %s (цель %s)"), *V2(S->Pos()), *V2(Goal)));
			S->Expect(S->Count(4008, S->Mark) == 1 && L8 && Near(L8->Cur, S->Pos(), 2.f),
				FString::Printf(TEXT("финальный 4008: %d шт. в %s"), S->Count(4008, S->Mark), L8 ? *V2(L8->Cur) : TEXT("-")));
			S->Expect(!S->N()->bMoveRequestInFlight, TEXT("бит 2 снят ответом 4007"));
			if (K) { S->Expect(S->ProbeSpeed > 0.3505f && S->ProbeSpeed < 0.40f, FString::Printf(TEXT("скорость шага подогнана под серверное время: %.4f (> 0.35)"), S->ProbeSpeed)); }
			else   { S->Expect(FMath::IsNearlyEqual(S->ProbeSpeed, 0.35f, 0.0005f), FString::Printf(TEXT("скорость шага = серверная 0.35 (контроль): %.4f"), S->ProbeSpeed)); }
		});

		// T2. Маршрут из трёх узлов: переход между нодами, все отрезки, один финальный 4008 (регрессии 16.09).
		Add(TEXT("T2 маршрут 3 узла"), 0.05f, 10.f, [S, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			if (!PC || !C) { return; }
			const FVector2D N1 = S->Home + S->DirA * 400.f, N2 = N1 + S->DirB * 400.f, N3 = N2 + S->DirA * 300.f;
			FAsdaMove4007SelfTest::SetRoute(PC, { N1, N2, N3 });
			FAsdaMove4007SelfTest::SendLeg(PC, C->GetGamePosXZ(), N1);
		}, [Idle]() { return Idle(); }, [S]()
		{
			const FVector2D N1 = S->Home + S->DirA * 400.f, N2 = N1 + S->DirB * 400.f, N3 = N2 + S->DirA * 300.f;
			TArray<FVector2D> Dsts; for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4006) { Dsts.Add(S->Caps[i].Dst); } }
			const bool bOrder = Dsts.Num() == 3 && Near(Dsts[0], N1, 2.f) && Near(Dsts[1], N2, 2.f) && Near(Dsts[2], N3, 2.f);
			S->Expect(bOrder, FString::Printf(TEXT("отрезков 4006: %d, по узлам по порядку: %s"), Dsts.Num(), bOrder ? TEXT("да") : TEXT("нет")));
			const FCap* L8 = S->LastOf(4008);
			S->Expect(S->Count(4008, S->Mark) == 1 && L8 && Near(L8->Cur, N3, 50.f),
				FString::Printf(TEXT("ровно один 4008, в конце маршрута: %d шт. в %s"), S->Count(4008, S->Mark), L8 ? *V2(L8->Cur) : TEXT("-")));
			S->Expect(Near(S->Pos(), N3, 50.f), FString::Printf(TEXT("персонаж в конце маршрута: %s"), *V2(S->Pos())));
		});

		// T3. Античит result=5 посреди маршрута: снап в cur, маршрут НЕ рвётся, финальный 4008 уходит.
		Add(TEXT("T3a маршрут до античита"), 0.4f, 4.f, [S, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			if (!PC || !C) { return; }
			const FVector2D N1 = S->Home + S->DirA * 400.f, N2 = N1 + S->DirB * 300.f;
			FAsdaMove4007SelfTest::SetRoute(PC, { N1, N2 });
			FAsdaMove4007SelfTest::SendLeg(PC, C->GetGamePosXZ(), N1);
		}, [S]() { AAsdaPlayerCharacter* C = S->Ch(); return C && C->IsMoving() && FVector2D::Distance(S->Pos(), S->Home) > 100.f; }, []() {});
		Add(TEXT("T3b античит result=5"), 0.05f, 10.f, [S, Feed]()
		{
			const FVector2D Back = S->Home + S->DirA * 50.f;
			Feed(4007, MoveBody(5, SelfUidx(*S), 2, Back, Back, 0.35f, -1));
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(Near(S->Pos(), Back, 2.f) && C && !C->IsMoving(), FString::Printf(TEXT("снап в серверную точку %s: %s"), *V2(Back), *V2(S->Pos())));
			S->Expect(PC && PC->HasRoute(), TEXT("маршрут после снапа НЕ оборван (регрессия 16.09)"));
			S->Expect(!S->N()->bMoveRequestInFlight, TEXT("бит 2 снят (4007 любого result)"));
		}, [Idle]() { return Idle(); }, [S]()
		{
			const FVector2D N2 = S->Home + S->DirA * 400.f + S->DirB * 300.f;
			const FCap* L8 = S->LastOf(4008);
			S->Expect(Near(S->Pos(), N2, 50.f) && S->Count(4008, S->Mark) == 1 && L8 && Near(L8->Cur, N2, 50.f),
				FString::Printf(TEXT("дошёл до конца маршрута %s и отправил один 4008 (%d шт.)"), *V2(S->Pos()), S->Count(4008, S->Mark)));
		});

		// T4. Грант с подменённой точкой (сервер «впереди»): эталон — без снапа, скорость выше; прежний порт — снап > 150.
		Add(TEXT("T4 грант с серверной точкой впереди"), 0.05f, 6.f, [S, Reset]()
		{
			Reset();
			S->bOverrideCur = true; S->OverrideCur = S->Home + S->DirA * 300.f;
			S->ProbePos = S->Pos();
			if (AAsdaPlayerController* PC = S->PC()) { PC->RequestMoveTo(S->Home + S->DirA * 600.f); }
		}, [S, Idle]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			if (C && S->ProbeSpeed < 0.f && S->N() && !S->N()->bMoveRequestInFlight && C->IsMoving())
			{
				S->ProbeSpeed = FAsdaMove4007SelfTest::Speed(C); S->ProbePos2 = S->Pos();
			}
			return Idle();
		}, [S, K]()
		{
			const float Jump = FVector2D::Distance(S->ProbePos, S->ProbePos2);
			if (K)
			{
				S->Expect(Jump < 60.f, FString::Printf(TEXT("по гранту НЕТ снапа в серверную точку (сдвиг %.0f)"), Jump));
				S->Expect(S->ProbeSpeed > 0.60f, FString::Printf(TEXT("скорость поднята, чтобы догнать сервер: %.4f"), S->ProbeSpeed));
			}
			else
			{
				S->Expect(Jump > 250.f, FString::Printf(TEXT("прежний порт снапит при расхождении > 150 (сдвиг %.0f — контроль)"), Jump));
				S->Expect(FMath::IsNearlyEqual(S->ProbeSpeed, 0.35f, 0.0005f), FString::Printf(TEXT("скорость = серверная: %.4f"), S->ProbeSpeed));
			}
			S->Expect(Near(S->Pos(), S->Home + S->DirA * 600.f, 50.f) && S->Count(4008, S->Mark) == 1,
				FString::Printf(TEXT("дошёл до цели %s, один 4008 (%d)"), *V2(S->Pos()), S->Count(4008, S->Mark)));
		});

		// T5. 4009 посреди хода: снап; бит 2 и поле +25 он НЕ трогает (эталон `Recv_EntityStop`); маршрут доезжает.
		Add(TEXT("T5a ход до 4009"), 0.3f, 4.f, [S, Reset]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { PC->RequestMoveTo(S->Home + S->DirA * 600.f); }
		}, [S]() { AAsdaPlayerCharacter* C = S->Ch(); return C && C->IsMoving() && FVector2D::Distance(S->Pos(), S->Home) > 150.f; }, []() {});
		Add(TEXT("T5b 4009"), 0.05f, 10.f, [S, Feed]()
		{
			UAsdaNetSubsystem* N = S->N(); AAsdaPlayerCharacter* C = S->Ch();
			if (!N || !C) { return; }
			C->MoveTargetRenderId = 123;
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			const FVector2D P = S->Pos() + S->DirB * 20.f;
			Feed(4009, MoveBody(0, SelfUidx(*S), 0, P, P, 0.35f, 555));
			S->Expect(Near(S->Pos(), P, 2.f) && !C->IsMoving(), FString::Printf(TEXT("снап в точку 4009 %s: %s"), *V2(P), *V2(S->Pos())));
			S->Expect(N->bMoveRequestInFlight, TEXT("4009 бит 2 не снимает (эталон)"));
			S->Expect(C->MoveTargetRenderId == 123, FString::Printf(TEXT("4009 поле +25 не пишет: %d"), C->MoveTargetRenderId));
			N->ClearMoveRequestInFlight(TEXT("самотест"));
		}, [Idle]() { return Idle(); }, [S]()
		{
			S->Expect(Near(S->Pos(), S->Home + S->DirA * 600.f, 60.f) && S->Count(4008, S->Mark) == 1,
				FString::Printf(TEXT("после 4009 маршрут доехал (пересчёт): %s, 4008: %d"), *V2(S->Pos()), S->Count(4008, S->Mark)));
		});

		// T6. Боевой подход: 4006 несёт renderId цели @+25, грант пишет его в поле цели хода; result 0 → −1.
		Add(TEXT("T6 подход с +25"), 0.05f, 6.f, [S, Reset]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { PC->StartRoutedMove(S->Home + S->DirA * 400.f, 777); }
			const FCap* L = S->LastOf(4006);
			S->Expect(L && L->Tgt == 777, FString::Printf(TEXT("4006 подхода несёт +25 = %d"), L ? L->Tgt : -99));
		}, [S]() { AAsdaPlayerCharacter* C = S->Ch(); return C && S->N() && !S->N()->bMoveRequestInFlight && C->IsMoving(); }, [S, Feed]()
		{
			AAsdaPlayerCharacter* C = S->Ch(); if (!C) { return; }
			S->Expect(C->MoveTargetRenderId == 777, FString::Printf(TEXT("грант 4007 записал цель хода: %d"), C->MoveTargetRenderId));
			Feed(4007, MoveBody(0, SelfUidx(*S), 2, S->Pos(), S->Pos(), 0.35f, 555));
			S->Expect(C->MoveTargetRenderId == -1, FString::Printf(TEXT("свой result 0 → цель хода −1 (эталон case 0): %d"), C->MoveTargetRenderId));
		});

		// T7. Поле +25 у соседа; чужой result 0 не двигает; 4009 поле не пишет.
		Add(TEXT("T7 сосед +25"), 0.1f, 2.f, [S, Reset, Feed]()
		{
			Reset();
			UAsdaNetSubsystem* N = S->N(); UGameInstance* G = N ? N->GetGameInstance() : nullptr;
			UAsdaWorldSubsystem* WSys = G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			AAsdaPlayerCharacter* P = WSys ? Cast<AAsdaPlayerCharacter>(WSys->GetPlayerActorByUserNum(77)) : nullptr;
			if (!P) { S->Expect(false, TEXT("сосед-заглушка userNum 77 не найден")); return; }
			const FVector2D PP = P->GetGamePosXZ();
			P->StopMove();
			Feed(4007, MoveBody(0, 77, 2, PP, PP + S->DirA * 300.f, 0.35f, 999));
			// ★ Ревизия С3: эталонная ветка соседа (ход только по result 1) — под ключом 1; при ключе 0 прежнее
			//   «любой не-5 = ход» дословно. Контрольная пара по ключу.
			if (S->Key)
			{
				S->Expect(P->MoveTargetRenderId == 999 && !P->IsMoving(), FString::Printf(TEXT("чужой result 0: цель хода %d, не идёт (эталон)"), P->MoveTargetRenderId));
			}
			else
			{
				S->Expect(P->MoveTargetRenderId == 999 && P->IsMoving(), FString::Printf(TEXT("прежний порт: чужой result 0 идёт (контроль), цель хода %d"), P->MoveTargetRenderId));
				P->StopMove(); P->NetSnap(PP);
			}
			Feed(4007, MoveBody(1, 77, 2, PP, PP + S->DirA * 300.f, 0.35f, 888));
			S->Expect(P->MoveTargetRenderId == 888 && P->IsMoving(), FString::Printf(TEXT("чужой result 1: цель хода %d, идёт"), P->MoveTargetRenderId));
			Feed(4009, MoveBody(0, 77, 0, PP, PP, 0.35f, 111));
			S->Expect(P->MoveTargetRenderId == 888 && !P->IsMoving(), FString::Printf(TEXT("чужой 4009: цель хода не тронута (%d), снап"), P->MoveTargetRenderId));
		}, []() { return true; }, []() {});

		// T8. Отказ хода (result 2) при ожидании гранта.
		Add(TEXT("T8 отказ result=2"), 0.15f, 2.f, [S, Reset, Feed]()
		{
			Reset();
			S->bAutoServer = false;
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* N = S->N();
			if (!PC || !C || !N) { return; }
			PC->RequestMoveTo(S->Home + S->DirA * 500.f);
			const FCap* L = S->LastOf(4006); if (!L) { S->Expect(false, TEXT("4006 не ушёл")); return; }
			const int32 M8 = S->Count(4008);
			Feed(4007, MoveBody(2, SelfUidx(*S), 2, L->Cur, L->Dst, 0.35f, -1));
			S->Expect(!N->bMoveRequestInFlight, TEXT("бит 2 снят отказом"));
			if (S->Key)
			{
				S->Expect(!PC->HasRoute() && !C->IsMoving() && S->Count(4008) == M8 + 1,
					FString::Printf(TEXT("эталон: персонаж не пошёл, маршрут закрыт, 4008 (незакрытый ход) — %d"), S->Count(4008) - M8));
			}
			else
			{
				S->Expect(PC->HasRoute() && C->IsMoving(), TEXT("прежний порт: идёт дальше оптимистично (контроль)"));
				PC->StopMoveAndNotify(TEXT("самотест: уборка"));
			}
		}, []() { return true; }, []() {});

		// T9. Бит 2: сторож 10 с; отложенный отрезок при ожидании ответа.
		Add(TEXT("T9a сторож 10 с"), 0.05f, 1.f, [S, Reset]()
		{
			Reset();
			S->bAutoServer = false;
			UAsdaNetSubsystem* N = S->N(); if (!N) { return; }
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs() - 9000.0;
			const bool bStill = N->IsMoveRequestInFlight();
			N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs() - 10001.0;
			const bool bAfter = N->IsMoveRequestInFlight();
			S->Expect(bStill && !bAfter && !N->bMoveRequestInFlight, FString::Printf(TEXT("9 с — ещё взведён (%d), 10.001 с — снят сторожем (%d)"), bStill, bAfter));
		}, []() { return true; }, []() {});
		Add(TEXT("T9b отрезок при ожидании ответа"), 0.3f, 1.f, [S, Reset]()
		{
			Reset();
			S->bAutoServer = false;
			UAsdaNetSubsystem* N = S->N(); AAsdaPlayerController* PC = S->PC(); if (!N || !PC) { return; }
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			const int32 M6 = S->Count(4006);
			PC->RequestMoveTo(S->Home + S->DirA * 500.f);
			if (S->Key)
			{
				S->Expect(S->Count(4006) == M6 && FAsdaMove4007SelfTest::LegUnsent(PC), TEXT("эталон: 4006 не ушёл, отрезок отложен"));
				N->ClearMoveRequestInFlight(TEXT("самотест: ответ пришёл"));
			}
			else
			{
				S->Expect(S->Count(4006) == M6 + 1, TEXT("прежний порт: 4006 уходит сразу (контроль)"));
			}
			S->Mark = S->Count(4006);
		}, []() { return true; }, [S]()
		{
			if (S->Key)
			{
				const FCap* L = S->LastOf(4006);
				S->Expect(S->Count(4006) == S->Mark + 1 && L && Near(L->Dst, S->Home + S->DirA * 500.f, 2.f),
					TEXT("отложенный отрезок ушёл из RouteTick, как только бит 2 снят"));
			}
			if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка")); }
		});

		// T10. Грант опоздал (время пути ≤ 0): эталон — 4008 в dst и снап; прежний порт — просто идёт.
		Add(TEXT("T10a короткий ход"), 0.40f, 1.f, [S, Reset]()
		{
			Reset();
			S->bAutoServer = false;
			if (AAsdaPlayerController* PC = S->PC()) { PC->RequestMoveTo(S->Home + S->DirA * 60.f); }
			S->Mark = S->Caps.Num();
		}, []() { return true; }, []() {});
		Add(TEXT("T10b грант через 400 мс"), 0.05f, 2.f, [S, Feed]()
		{
			const FCap* L = nullptr;
			for (int32 i = S->Caps.Num() - 1; i >= 0; --i) { if (S->Caps[i].Op == 4006) { L = &S->Caps[i]; break; } }
			if (!L) { S->Expect(false, TEXT("4006 короткого хода не ушёл")); return; }
			const FVector2D Dst = L->Dst;
			const int32 M8 = S->Count(4008);
			Feed(4007, MoveBody(1, SelfUidx(*S), 2, L->Cur, Dst, 0.35f, -1));
			const FCap* L8 = S->LastOf(4008);
			if (S->Key)
			{
				S->Expect(S->Count(4008) == M8 + 1 && L8 && Near(L8->Cur, Dst, 1.f) && Near(L8->Dst, Dst, 1.f),
					FString::Printf(TEXT("эталон: 4008 в точке dst %s"), L8 ? *V2(L8->Cur) : TEXT("-")));
				S->Expect(Near(S->Pos(), Dst, 1.f), FString::Printf(TEXT("позиция = dst: %s"), *V2(S->Pos())));
			}
			else
			{
				S->Expect(S->Count(4008) == M8, TEXT("прежний порт: 4008 из гранта не шлётся (контроль)"));
			}
			S->Mark = S->Count(4008);
		}, [Idle]() { return Idle(); }, [S]()
		{
			if (S->Key) { S->Expect(S->Count(4008) == S->Mark, TEXT("после снапа второго 4008 нет (ход уже закрыт)")); }
		});

		// T11. «Паника» (id 4): ход ведёт сервер без нашего 4006 — поправки на путь пакета нет, маршрута нет.
		Add(TEXT("T11 паника"), 0.3f, 2.f, [S, Reset, Feed]()
		{
			Reset();
			S->bAutoServer = false;
			UAsdaNetSubsystem* N = S->N(); AAsdaPlayerCharacter* C = S->Ch();
			UGameInstance* G = N ? N->GetGameInstance() : nullptr;
			UAsdaEffectsSubsystem* Fx = G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			if (!N || !C || !Fx) { return; }
			const FVector2D Dst = S->Home + S->DirA * 300.f;
			// Контроль: без «Паники» и без маршрута грант не нужен никому.
			N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs() - 5000.0;
			Feed(4007, MoveBody(1, SelfUidx(*S), 2, S->Home, Dst, 0.35f, -1));
			if (S->Key) { S->Expect(!C->IsMoving(), TEXT("без «Паники» и маршрута опоздавший грант персонажа не трогает [ПОРТ]")); }
			else        { S->Expect(C->IsMoving(), TEXT("прежний порт: любой грант двигает (контроль)")); }
			C->StopMove(); C->NetSnap(S->Home);
			Fx->AddOrUpdateSkill(Fx->SelfUserIdx(), 4, 0, 0, 0, 30, 0);
			N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs() - 5000.0;
			Feed(4007, MoveBody(1, SelfUidx(*S), 2, S->Home, Dst, 0.35f, -1));
			const float Sp = FAsdaMove4007SelfTest::Speed(C);
			if (S->Key)
			{
				S->Expect(C->IsMoving() && FMath::IsNearlyEqual(Sp, 0.35f, 0.01f),
					FString::Printf(TEXT("«Паника»: идёт, метка сброшена — скорость без поправки %.4f"), Sp));
			}
			else { S->Expect(C->IsMoving(), TEXT("прежний порт: идёт")); }
			Fx->RemoveBySkillState(Fx->SelfUserIdx(), 4, 0);
		}, []() { return true; }, [S]() { if (AAsdaPlayerCharacter* C = S->Ch()) { C->StopMove(); } });

		// T12. Клавиатура: грант во время WASD меняет только скорость (оба ключа).
		Add(TEXT("T12 клавиатура"), 0.1f, 1.f, [S, Reset, Feed]()
		{
			Reset();
			AAsdaPlayerCharacter* C = S->Ch(); if (!C) { return; }
			C->SetMoveDirection(S->DirA, 0.35f, S->DirA, false);
			Feed(4007, MoveBody(1, SelfUidx(*S), 2, S->Pos(), S->Pos() + S->DirB * 800.f, 0.5f, -1));
			S->Expect(C->IsMoving() && !FAsdaMove4007SelfTest::ToPoint(C) && FMath::IsNearlyEqual(FAsdaMove4007SelfTest::Speed(C), 0.5f, 0.001f)
				&& FAsdaMove4007SelfTest::Dir(C).Equals(S->DirA.GetSafeNormal(), 0.01f),
				FString::Printf(TEXT("WASD: режим «по направлению», курс тот же, скорость из гранта %.3f"), FAsdaMove4007SelfTest::Speed(C)));
			C->StopMove();
		}, []() { return true; }, []() {});

		// T13. 4062 перенацеливает узел идущего маршрута (регрессия 16.09): маршрут цел, финальный 4008 уходит.
		Add(TEXT("T13a ход до 4062"), 0.3f, 4.f, [S, Reset]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { PC->RequestMoveTo(S->Home + S->DirA * 600.f); }
		}, [S]() { AAsdaPlayerCharacter* C = S->Ch(); return C && C->IsMoving() && S->N() && !S->N()->bMoveRequestInFlight; }, []() {});
		Add(TEXT("T13b 4062"), 0.05f, 8.f, [S, Feed]()
		{
			const FVector2D NewDst = S->Home + S->DirA * 550.f;
			Feed(4062, PosSyncBody(1, SelfUidx(*S), NewDst, 0.35f));
			AAsdaPlayerController* PC = S->PC();
			S->Expect(PC && PC->HasRoute() && Near(FAsdaMove4007SelfTest::RouteNode(PC), NewDst, 1.f),
				TEXT("узел маршрута перенацелен, маршрут не оборван"));
		}, [Idle]() { return Idle(); }, [S]()
		{
			const FCap* L8 = S->LastOf(4008);
			S->Expect(S->Count(4008, S->Mark) == 1 && L8 && Near(L8->Cur, S->Home + S->DirA * 550.f, 50.f),
				FString::Printf(TEXT("финальный 4008 у новой цели: %d шт. в %s"), S->Count(4008, S->Mark), L8 ? *V2(L8->Cur) : TEXT("-")));
		});

		// T14. Мёртвому свой 4007 эталон не исполняет (гейт состояния 6) — даже бит 2 не снимает.
		Add(TEXT("T14 мёртв"), 0.05f, 1.f, [S, Reset, Feed]()
		{
			Reset();
			S->bAutoServer = false;
			UAsdaNetSubsystem* N = S->N(); AAsdaPlayerCharacter* C = S->Ch(); if (!N || !C) { return; }
			N->bSelfDead = true;
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			Feed(4007, MoveBody(5, SelfUidx(*S), 2, S->Home + S->DirA * 200.f, S->Home + S->DirA * 200.f, 0.35f, 321));
			if (S->Key)
			{
				S->Expect(Near(S->Pos(), S->Home, 1.f) && N->bMoveRequestInFlight && C->MoveTargetRenderId == 321,
					TEXT("эталон: мёртвому result 5 не исполнен, бит 2 не снят, поле +25 записано (оно ДО гейта)"));
			}
			else
			{
				S->Expect(Near(S->Pos(), S->Home + S->DirA * 200.f, 1.f) && !N->bMoveRequestInFlight, TEXT("прежний порт: снап без гейта, бит снят (контроль)"));
			}
			N->bSelfDead = false;
		}, []() { return true; }, []() {});

		// ═══════════ K1 (исправление КРИТ ревизии): подход к цели в СВОЕЙ клетке 100×100 ═══════════
		//   Автоответчик отказывает (result 0) на ход в свою клетку — как `CharMove`. Эталон шлёт подход-4006 только в
		//   чужую клетку. Ключ 1: ни одного 4006, персонаж доходит шагом и начинает бой/навык. Ключ 0 (контроль):
		//   4006 уходит и получает отказ, персонаж идёт оптимистично. Без исправления при ключе 1 — вечный цикл
		//   «отказ → стоп → повтор», 4026 не наступает (провал K1a).
		// Дальности боевого подхода — формулы `AAsdaPlayerController::AttackTick` (кулаки: оружие снято на прогон).
		auto StopDistOf = [S](const AAsdaMobActor* M) -> float
		{
			const AAsdaPlayerController* PC = S->PC();
			const float RangeU = PC ? PC->AttackRange : 150.f;
			return FMath::Max(60.f, RangeU + MobRadius(M) - 20.f);
		};
		auto CharZ = [S]() -> float { AAsdaPlayerCharacter* C = S->Ch(); return C ? (float)C->GetActorLocation().Z : 0.f; };

		// K1a. Боевой подход (`AttackTick`): стоп-точка в 60 ед. по диагонали внутри своей клетки.
		Add(TEXT("K1a бой: подход в своей клетке"), 0.1f, 5.f, [S, Reset, StopDistOf, CharZ]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			if (!PC || !C || !S->bHomeKOk) { S->Expect(false, TEXT("нет персонажа или точки K1")); return; }
			C->NetSnap(S->HomeK);
			S->Mark = S->Caps.Num();
			AAsdaMobActor* M = SpawnMob(S->World.Get(), S->HomeK, CharZ(), 4321);   // позиция уточняется ниже
			if (!M) { S->Expect(false, TEXT("моб-заглушка не создан")); return; }
			S->Mob = M;
			const float SD = StopDistOf(M);
			const FVector2D MG = S->HomeK + S->Diag * (SD + 60.f);
			const FVector2D Ue = AsdaCoord::GameToUe2D(MG);
			M->SetActorLocation(FVector(Ue.X, Ue.Y, CharZ()));
			S->ProbePos = S->HomeK + S->Diag * 60.f;   // стоп-точка подхода
			S->Expect(AsdaCoord::GameToTile(S->ProbePos) == AsdaCoord::GameToTile(S->HomeK),
				FString::Printf(TEXT("геометрия: стоп-точка %s в клетке персонажа, моб в %s (стоп-дистанция %.0f)"), *V2(S->ProbePos), *V2(MG), SD));
			PC->TargetActor = M; PC->bAutoAttack = true;   // боевой цикл ведёт `AttackTick` (тик контроллера)
		}, [S]() { const AAsdaPlayerController* PC = S->PC(); return PC && PC->bCombatStarted; }, [S]()
		{
			const int32 N6 = S->Count(4006, S->Mark), N8 = S->Count(4008, S->Mark);
			const float Moved = FVector2D::Distance(S->Pos(), S->HomeK);
			// Вход в бой = `bCombatStarted` (ставится сразу за отправкой 4026; офлайн 4026 без сокета не уходит).
			const AAsdaPlayerController* PCc = S->PC();
			const bool bAttack = PCc && PCc->bCombatStarted;
			if (S->Key)
			{
				S->Expect(N6 == 0, FString::Printf(TEXT("эталон: подход в своей клетке без 4006 (ушло %d)"), N6));
				S->Expect(bAttack && Moved > 3.f, FString::Printf(TEXT("пошёл шагом (сдвиг %.0f; вход в бой — уже на пороге дальности) и вошёл в бой: %s"), Moved, bAttack ? TEXT("да") : TEXT("НЕТ")));
				const FCap* L8 = S->LastOf(4008);
				S->Expect(N8 >= 1 && L8 && FVector2D::Distance(L8->Cur, S->HomeK) > 3.f,
					FString::Printf(TEXT("4008 сообщил серверу новую точку: %d шт., последний в %s [ПОРТ]"), N8, L8 ? *V2(L8->Cur) : TEXT("-")));
			}
			else
			{
				S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: 4006 в свою клетку уходит и получает отказ 0 (контроль): %d"), N6));
				S->Expect(bAttack, TEXT("прежний порт: бой начался (оптимистичный шаг)"));
			}
		});

		// K1b. Подход ради навыка (`TickSkillApproach`): стоп-точка 0.85·R, от HomeK — (0.15·R + 30) по диагонали.
		Add(TEXT("K1b навык: подход в своей клетке"), 0.1f, 5.f, [S, Reset, CharZ]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* N = S->N();
			UAsdaSkillUseSubsystem* SU = N && N->GetGameInstance() ? N->GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
			if (!PC || !C || !SU || !S->bHomeKOk || S->SkillCode < 0) { S->Expect(false, TEXT("нет персонажа/подсистемы навыков/точки K1/навыка")); return; }
			C->NetSnap(S->HomeK);
			S->Mark = S->Caps.Num();
			const float X = 0.15f * S->SkillRangeU + 30.f;
			const FVector2D MG = S->HomeK + S->Diag * (0.85f * S->SkillRangeU + X);
			AAsdaMobActor* M = SpawnMob(S->World.Get(), MG, CharZ(), 4322);
			if (!M) { S->Expect(false, TEXT("моб-заглушка не создан")); return; }
			S->Mob = M;
			S->ProbePos = S->HomeK + S->Diag * X;
			S->Expect(AsdaCoord::GameToTile(S->ProbePos) == AsdaCoord::GameToTile(S->HomeK),
				FString::Printf(TEXT("геометрия: стоп-точка навыка %s в клетке персонажа (R=%.0f)"), *V2(S->ProbePos), S->SkillRangeU));
			PC->TargetActor = M;
			SU->BeginSkillApproach(S->SkillCode, S->SkillRangeU, 0, 4322);   // тик подсистемы ведёт подход
		}, [S]()
		{
			const UAsdaNetSubsystem* N = S->N();
			const UAsdaSkillUseSubsystem* SU = N && N->GetGameInstance() ? N->GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
			return SU && !SU->HasSkillApproach();
		}, [S]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			const float Moved = FVector2D::Distance(S->Pos(), S->HomeK);
			const UAsdaNetSubsystem* N = S->N();
			const UAsdaSkillUseSubsystem* SU = N && N->GetGameInstance() ? N->GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
			const bool bDone = SU && !SU->HasSkillApproach();
			if (S->Key) { S->Expect(N6 == 0, FString::Printf(TEXT("эталон: подход ради навыка в своей клетке без 4006 (ушло %d)"), N6)); }
			else        { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: 4006 в свою клетку с отказом 0 (контроль): %d"), N6)); }
			S->Expect(bDone && Moved > 10.f, FString::Printf(TEXT("дошёл шагом до зоны навыка (сдвиг %.0f), подход завершён: %s"), Moved, bDone ? TEXT("да") : TEXT("НЕТ")));
		});

		// K1c. Контроль гейта: обычный КЛИК в своей клетке гейтом не охвачен (эталон шлёт 4006 без проверки клетки,
		//   отказ 0 → персонаж не идёт). Заодно доказывает, что автоответчик отказывает «своей клетке».
		Add(TEXT("K1c клик в своей клетке (контроль)"), 0.4f, 2.f, [S, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			if (!PC || !C || !S->bHomeKOk) { return; }
			C->NetSnap(S->HomeK);
			S->Mark = S->Caps.Num();
			PC->RequestMoveTo(S->HomeK + S->Diag * 90.f);
		}, []() { return true; }, [S]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			const float Moved = FVector2D::Distance(S->Pos(), S->HomeK);
			S->Expect(N6 == 1, FString::Printf(TEXT("клик шлёт 4006 и в своей клетке (эталон `CGame_HandleGroundClickAttack`): %d"), N6));
			if (S->Key) { S->Expect(Moved < 2.f, FString::Printf(TEXT("эталон: отказ 0 — персонаж не пошёл (сдвиг %.0f)"), Moved)); }
			else        { S->Expect(Moved > 20.f, FString::Printf(TEXT("прежний порт: пошёл вопреки отказу (контроль, сдвиг %.0f)"), Moved)); }
			if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка")); }
		});

		// ═══════════ C2 (ревизия СРЕДН): читатели бита 2 и его снятие ═══════════
		// C2a. Клик по миру (`OnLeftClick`): при взведённом бите — выход ДО гашения автобега (эталон `world_OnLeftClickPick`).
		Add(TEXT("C2a клик по миру при бите 2"), 0.05f, 1.f, [S, Reset]()
		{
			Reset();
			S->bAutoServer = false;
			AAsdaPlayerController* PC = S->PC(); UAsdaNetSubsystem* N = S->N(); if (!PC || !N) { return; }
			// ★ 22.10.2026: офлайн-курсор (`-RenderOffscreen`, курсора ОС нет — Slate держит последнюю позицию) нередко стоит над окном
			//   (MINISTATE_WIN), и `OnLeftClick` выходит гейтом «курсор над окном» ДО бита 2 и автобега — сценарий проверял бы не то.
			//   На время C2a интерфейс скрыт (Alt+Z: хит-тест окон молчит — `HiddenBlocksUiHitTest`), логика клика не трогается.
			UAsdaUiVisibilitySubsystem* Uv = N->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>();
			const bool bUiWasShown = !Uv || Uv->IsUiShown();
			if (Uv) { Uv->SetUiShown(false, TEXT("самотест 4007 C2a: курсор офлайн-прогона не над окном")); }
			FName OverZone;
			if (PC->IsCursorOverUi(OverZone)) { S->Expect(false, FString::Printf(TEXT("метод: курсор всё ещё над окном «%s»"), *OverZone.ToString())); }
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			PC->bAutoRunActive = true;
			PC->OnLeftClick(false);
			if (S->Key) { S->Expect(PC->bAutoRunActive, TEXT("эталон: клик отброшен битом 2 (автобег не тронут)")); }
			else        { S->Expect(!PC->bAutoRunActive, TEXT("прежний порт: клик обработан, автобег снят (контроль)")); }
			// Внутренняя пара при ключе 1: бит снят → тот же клик обрабатывается.
			N->ClearMoveRequestInFlight(TEXT("самотест"));
			PC->bAutoRunActive = true;
			PC->OnLeftClick(false);
			S->Expect(!PC->bAutoRunActive, TEXT("без бита 2 клик обработан (автобег снят)"));
			// ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: клик шага, отброшенный битом 2, ложится в очередь клика (`asda.Input.ClickQueue`) и
			//   исполняется, когда освободится занятость, — под нагрузкой это случалось через 0,9 с, посреди шага C2d: клик «по пусто»
			//   выводил из боя, и C2d' падал (полный прогон 14.11, 123/2). Шаг проверяет только разбор клика при бите — отложенное
			//   сбрасывается здесь же.
			AsdaSkillCastLock::Reset(TEXT("самотест 4007 C2a: отложенный клик шага не нужен"));
			if (Uv && bUiWasShown) { Uv->SetUiShown(true, TEXT("самотест 4007 C2a: возврат")); }
		}, []() { return true; }, [S]() { if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка")); } });

		// C2b. Гейт навыков (`CheckGates`, эталон `combatHandleSkillUse` и др.): при бите — молчаливый отказ (строка лога).
		Add(TEXT("C2b навык при бите 2"), 0.05f, 1.f, [S, Reset]()
		{
			Reset();
			S->bAutoServer = false;
			UAsdaNetSubsystem* N = S->N();
			UAsdaSkillUseSubsystem* SU = N && N->GetGameInstance() ? N->GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
			if (!N || !SU || !GLog) { S->Expect(false, TEXT("нет подсистемы навыков")); return; }
			// Навык Хранителя (> 1201): гейты «выучен» и «маска оружия» его не касаются — до гейта бита доходим офлайн.
			FAsdaSkillDef D; D.SkillCode = 1210; D.Name = TEXT("самотест-4007"); D.SubType = 0;
			FLogGrab Grab; Grab.Needle = TEXT("(бит 2) — эталон молчит");
			GLog->AddOutputDevice(&Grab);
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			const bool bWith = SU->CheckGates(1210, D, false);
			GLog->Flush();   // строки доставляются устройствам и с потока лога — дождаться
			int32 HitsWith = 0; { FScopeLock L(&Grab.Lock); HitsWith = Grab.Hits; }
			N->ClearMoveRequestInFlight(TEXT("самотест"));
			SU->CheckGates(1210, D, false);
			GLog->Flush();
			int32 HitsWithout = 0; { FScopeLock L(&Grab.Lock); HitsWithout = Grab.Hits - HitsWith; }
			GLog->RemoveOutputDevice(&Grab);
			if (S->Key) { S->Expect(HitsWith == 1 && !bWith, FString::Printf(TEXT("эталон: при бите навык молча не применён (строк %d)"), HitsWith)); }
			else        { S->Expect(HitsWith == 0, FString::Printf(TEXT("прежний порт: гейта бита нет (контроль, строк %d)"), HitsWith)); }
			S->Expect(HitsWithout == 0, FString::Printf(TEXT("без бита гейт не срабатывает (строк %d)"), HitsWithout));
		}, []() { return true; }, []() {});

		// C2c. `AttackTick`, вход в бой: при бите стоп и удар ждут ответа (эталон: при бите 2 стоп 4008 не шлётся).
		Add(TEXT("C2c бой: вход при бите 2"), 0.35f, 1.f, [S, Reset, StopDistOf, CharZ]()
		{
			Reset();
			S->bAutoServer = false;
			AAsdaPlayerController* PC = S->PC(); UAsdaNetSubsystem* N = S->N(); if (!PC || !N) { return; }
			AAsdaMobActor* M = SpawnMob(S->World.Get(), S->Home, CharZ(), 4323);
			if (!M) { S->Expect(false, TEXT("моб-заглушка не создан")); return; }
			S->Mob = M;
			const FVector2D MG = S->Home + S->DirA * StopDistOf(M);   // в дальности удара
			const FVector2D Ue = AsdaCoord::GameToUe2D(MG);
			M->SetActorLocation(FVector(Ue.X, Ue.Y, CharZ()));
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			PC->TargetActor = M; PC->bAutoAttack = true;
		}, []() { return true; }, [S]()
		{
			const AAsdaPlayerController* PC = S->PC();
			if (S->Key) { S->Expect(PC && !PC->bCombatStarted && !PC->bInCombatStance, TEXT("эталон: при бите 2 в бой не входим (ни стопа 4008, ни заявки 4026)")); }
			else        { S->Expect(PC && PC->bCombatStarted, TEXT("прежний порт: вход в бой сразу (контроль)")); }
		});

		// C2d. `AttackTick`, перезапрос подхода: при бите новый 4006 не шлётся (эталон game.c:68757).
		Add(TEXT("C2d бой: подход при бите 2"), 0.35f, 1.f, [S, Reset, StopDistOf, CharZ]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); UAsdaNetSubsystem* N = S->N(); if (!PC || !N) { return; }
			AAsdaMobActor* M = SpawnMob(S->World.Get(), S->Home, CharZ(), 4324);
			if (!M) { S->Expect(false, TEXT("моб-заглушка не создан")); return; }
			S->Mob = M;
			const FVector2D MG = S->Home + S->DirA * (StopDistOf(M) + 400.f);   // далеко — нужен подход
			const FVector2D Ue = AsdaCoord::GameToUe2D(MG);
			M->SetActorLocation(FVector(Ue.X, Ue.Y, CharZ()));
			S->Mark = S->Caps.Num();
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			PC->TargetActor = M; PC->bAutoAttack = true;
		}, []() { return true; }, [S]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			if (S->Key)
			{
				S->Expect(N6 == 0, FString::Printf(TEXT("эталон: при бите 2 подход не перезапрошен (4006: %d)"), N6));
				if (UAsdaNetSubsystem* N = S->N()) { N->ClearMoveRequestInFlight(TEXT("самотест: ответ пришёл")); }
			}
			else { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: подход уходит сразу (контроль, 4006: %d)"), N6)); }
			S->Mark = S->Caps.Num();
		});
		Add(TEXT("C2d' бой: подход после снятия бита"), 0.05f, 1.5f, []() {}, [S]()
		{
			return !S->Key || S->Count(4006, S->Mark) > 0;
		}, [S]()
		{
			if (S->Key)
			{
				const FCap* L = S->LastOf(4006);
				S->Expect(S->Count(4006, S->Mark) >= 1 && L && L->Tgt == 4324,
					FString::Printf(TEXT("бит снят — подход ушёл, +25 = %d"), L ? L->Tgt : -99));
			}
		});

		// C2e. Подход ради навыка: при бите нет ни стопа, ни нового 4006 (эталон `Player_UpdateAutoActionState` 0xA2595F).
		Add(TEXT("C2e навык: подход при бите 2"), 0.35f, 1.f, [S, Reset, CharZ]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); UAsdaNetSubsystem* N = S->N();
			UAsdaSkillUseSubsystem* SU = N && N->GetGameInstance() ? N->GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
			if (!PC || !N || !SU || S->SkillCode < 0) { S->Expect(false, TEXT("нет подсистемы навыков или навыка")); return; }
			AAsdaMobActor* M = SpawnMob(S->World.Get(), S->Home + S->DirA * (S->SkillRangeU + 300.f), CharZ(), 4325);
			if (!M) { S->Expect(false, TEXT("моб-заглушка не создан")); return; }
			S->Mob = M;
			S->Mark = S->Caps.Num();
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			PC->TargetActor = M;
			SU->BeginSkillApproach(S->SkillCode, S->SkillRangeU, 0, 4325);
		}, []() { return true; }, [S]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			if (S->Key) { S->Expect(N6 == 0, FString::Printf(TEXT("эталон: при бите 2 подход ради навыка ждёт (4006: %d)"), N6)); }
			else        { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: подход ради навыка уходит сразу (контроль, 4006: %d)"), N6)); }
		});

		// C2f. Снятие бита отказом боя 4027 (эталон `Recv_CombatAnimState`: коды 6/7/8/9/20; прочие — нет). Пара — по коду.
		Add(TEXT("C2f снятие отказом боя 4027"), 0.05f, 1.f, [S, Reset, Feed]()
		{
			Reset();
			S->bAutoServer = false;
			UAsdaNetSubsystem* N = S->N(); if (!N) { return; }
			TArray<uint8> B; B.SetNumZeroed(45);
			const int16 U = SelfUidx(*S);
			FMemory::Memcpy(&B[1], &U, 2);
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			B[0] = 2; Feed(4027, B);
			S->Expect(N->bMoveRequestInFlight, TEXT("4027 код 2 бит 2 не снимает (контроль)"));
			B[0] = 6; Feed(4027, B);
			S->Expect(!N->bMoveRequestInFlight, TEXT("4027 код 6 снимает бит 2"));
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			B[0] = 20; Feed(4027, B);
			S->Expect(!N->bMoveRequestInFlight, TEXT("4027 код 20 снимает бит 2"));
		}, []() { return true; }, []() {});

		// C2g. Сброс сессии чистит бит и метку (ответ старой сессии не придёт).
		Add(TEXT("C2g сброс сессии"), 0.05f, 1.f, [S, Reset]()
		{
			Reset();
			S->bAutoServer = false;
			UAsdaNetSubsystem* N = S->N(); if (!N) { return; }
			N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
			const bool bBefore = N->IsMoveRequestInFlight();
			N->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			S->Expect(bBefore && !N->bMoveRequestInFlight && N->MoveRequestStampMs == 0.0,
				FString::Printf(TEXT("до сброса взведён (%d), после — снят, метка 0 (%.0f)"), bBefore, N->MoveRequestStampMs));
		}, []() { return true; }, []() {});
	}

	// ─────────────────────────── Итог ───────────────────────────
	Add(TEXT("итог"), 0.f, 1.f, [S, Net, Reset]()
	{
		Reset();
		// ★ 22.10.2026: сосед-заглушка 77 убирается штатным 4011 (`OnEntityDespawn`) — оставленный сосед ломал следующий самотест пакета
		//   (`asda.Combat.SelfTestObserverSync`: «в реестре соседей остался 1»).
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], (int16)77); Net->DebugFeedPacket(4011, B); }
		AsdaCVarSave::RestoreAll(TEXT("asda.Move.SelfTest4007"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — значение + SetBy
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		Net->bActionPending = false;
		Net->SelfWeaponItemId = S->SavedWeapon;   // оружие — как до прогона
		UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-4007 ИТОГ: ok=%d fail=%d (перехвачено 4006: %d, 4008: %d)"),
			S->Ok, S->Fail, S->Count(4006), S->Count(4008));
	}, []() { return true; }, []() {});

	// ─────────────────────────── Спавн и запуск исполнителя ───────────────────────────
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<decltype(S)::ElementType> WSg = S;   // состояние прогона — общий флаг отложенного старта
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const auto Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте (первый сработавший таймер)
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && !AsdaSelfTestSelfReady::IsReady(Wd) && GEngine)
			{
				if (!P || !P->GetPawn()) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); }   // ★ 01.11.2026: карта — только без павна
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, Delay), false);
	}
	TWeakPtr<FState> WS = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WS, WW]()
	{
		TSharedPtr<FState> St = WS.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }   // ★ СРЕДН-1: отказ на старте — исполнитель не идёт
		const double Now = FPlatformTime::Seconds();
		// Ответы «сервера», чей срок настал (по порядку постановки).
		for (int32 i = 0; i < St->Queue.Num();)
		{
			if (St->Queue[i].AtS <= Now)
			{
				FInject In = MoveTemp(St->Queue[i]);
				St->Queue.RemoveAt(i);
				if (UAsdaNetSubsystem* N = St->N()) { N->DebugFeedPacket(In.Op, In.Body); }
			}
			else { ++i; }
		}
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-4007 шаг [ключ=%d] %s"), St->Key, *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		if (El < Stp.MinSec) { return; }
		const bool bDone = Stp.Poll ? Stp.Poll() : true;
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с (позиция %s)"), Stp.TimeoutSec, *V2(St->Pos()))); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.02f, true, Delay + 2.5f);
}

// ═════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ САМОТЕСТ ХОДА КЛАВИШАМИ `asda.Move.SelfTestKeyboard` (27.09.2026, `Reports/owner_0927/wasd_impl.md`). [ПОРТ]
//  Офлайн, штатные пути: клавиши — через `UPlayerInput` (`InputKey`, как `asda.Keybinds.HoldSim`) → `PollKeys` →
//  тик клавиш; исходящие 4006/4008/4024 — полный перехват сети (в сеть ничего); ответы — штатный приёмник
//  `DebugFeedPacket`. «Сервер» повторяет `CharMove@0x544EB0`/`CharStop@0x545C70` ПОЛНОСТЬЮ, с состоянием: клетка
//  сервера (тайл его точки), стоит/идёт (идёт до 4008), своя точка, интегрируемая по скорости, сравнение «пройдено
//  сервером / клиентом» в мс пути (`sub_523310` = √(Δ²)/скорость — в декомпиле ошибочно `log`, по байт-коду 0x52334D →
//  `_sqrtf@0x5269A0`): сервер впереди → cur = его точка; клиент впереди > 1000 мс → result 5; стоя cur обязан совпасть
//  ТОЧНО (иначе 5), dst == cur → 2; клетка dst == клетке сервера → 0. На 4008 — 4009 в точку клиента (≤ 1000 мс) или
//  серверную. ЛКМ/ПКМ — подмена «сырой» мыши контроллера (физическая мышь в прогон не вмешивается).
//  Контрольные пары: каждый сценарий прогоняется при `asda.Move.KeyboardEtalon` 1 и 0; проверки эталонной схемы
//  ожидают ПРОТИВОПОЛОЖНОЕ при 0 (без пары — только KB2 и KB9, регрессионные; помечены).
// ═════════════════════════════════════════════════════════════════════════════════════════════════════════════
namespace AsdaMoveKbTestLocal
{
	using namespace AsdaMove4007TestLocal;
	// Один перехваченный пакет (4006/4008/4024).
	struct FKCap
	{
		uint16 Op = 0;                          // опкод
		int16 Act = 0;                          // 4024: код действия; 4006: подтип
		int16 Tgt = -1;                         // 4006: +25
		FVector2D Cur = FVector2D::ZeroVector;  // cur тела
		FVector2D Dst = FVector2D::ZeroVector;  // dst тела
		double AtS = 0.0;                       // время перехвата
		float FrameS = 0.f;                     // ★ R5-fix6: длина кадра игры в момент перехвата, с
		bool bMovingAtSend = false;             // персонаж шёл В МОМЕНТ отправки (снимается в перехватчике — не зависит от темпа кадров)
	};
	// Состояние прогона и «сервера».
	struct FKState
	{
		TWeakObjectPtr<UAsdaNetSubsystem> Net;  // сеть
		TWeakObjectPtr<UWorld> World;           // мир
		TArray<FKCap> Caps;                     // перехваты
		TArray<FInject> Queue;                  // отложенные ответы «сервера»
		// ★ 22.10.2026 (ревизия локального старта): «сервер» реалистичнее — пакет клиента принимается через ½RTT (ответ — ещё через ½RTT),
		//   точка сервера шагает тактом `MoveSyncProcess` 100 мс. `bSrvRealistic = false` — прежняя мгновенная модель.
		bool bSrvRealistic = true;
		TArray<FKCap> SrvIn;                    // пакеты клиента «в пути» к серверу
		double SrvExtraUpS = 0.0;               // ★ KB52: добавочная задержка исходящего (Wi-Fi), с
		TArray<FStep> Steps;                    // шаги
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Move.SelfTestKeyboard"), false };   // ★ СРЕДН-1: общий флаг отложенного старта (спавн + исполнитель)
		int32 StepIdx = 0;                      // текущий шаг
		bool bStepStarted = false;              // шаг начат
		double StepStartS = 0.0;                // начало шага
		int32 Ok = 0, Fail = 0;                 // счёт
		int32 Key = 1;                          // текущее значение `asda.Move.KeyboardEtalon`
		uint32 Gen = 0;                         // поколение перехватчика
		bool bWasInGame = false;                // «в мире» до прогона
		FTimerHandle Tick;                      // исполнитель
		bool bQuit = false;                     // выйти по окончании
		double RttS = 0.08;                     // путь пакета туда-обратно
		// ── «Сервер» (`CUser` одного игрока) ──
		bool bAuto = true;                      // автоответчик включён
		bool bForceRefuse = false;              // отвечать на 4006 result 0 всегда (KB3)
		float SrvSpeed = 0.35f;                 // скорость сервера (`sub_5588F0`)
		bool bSrvMoving = false;                // `sub_523ED0` — флаг «идёт» (снимает только 4008 / Revision Pos)
		FVector2D SrvPos = FVector2D::ZeroVector;    // `gap_4954[2938/2942]` — точка сервера
		FVector2D SrvStart = FVector2D::ZeroVector;  // `gap_54F6[2/6]` — начало отрезка
		FVector2D SrvDst = FVector2D::ZeroVector;    // `gap_54F6[10/14]` — цель отрезка
		double SrvStartS = 0.0;                 // момент начала отрезка
		FIntPoint SrvTile = FIntPoint(0, 0);    // `gap_4954[2930/2932]` — клетка сервера
		int32 N5 = 0, N0 = 0, N2 = 0;           // счёт ответов result 5 / 0 / 2
		// ── Геометрия и рабочие поля ──
		FVector2D Home = FVector2D::ZeroVector; // точка старта сценариев
		FVector2D DirA = FVector2D(1, 0);       // проходимое направление (≥ 4000 ед.)
		float YawA = 0.f;                       // курс камеры UE, дающий DirA
		int32 Mark = 0;                         // перехватов на начало шага
		double MarkS = 0.0;                     // момент отметки
		bool bFlag = false;                     // рабочий флаг шага
		bool bProbe = false;                    // снятое значение шага
		uint64 ProbeFrame = 0;                  // ★ R4, запись 9: кадр, в котором шаг KB54 поставил смерть
		float ProbeF = 0.f;                     // снятое число шага
		FVector2D ProbePos = FVector2D::ZeroVector;  // снятая позиция
		bool bWallOk = false;                   // KB19: найдена точка перед стеной
		FVector2D WallHome = FVector2D::ZeroVector;  // KB19: точка старта перед стеной
		FVector2D WallDir = FVector2D(1, 0);    // KB19: направление на стену
		float WallDist = 0.f;                   // KB19: расстояние до первой непроходимой клетки
		TWeakObjectPtr<AAsdaMobActor> Mob;      // KB17: моб-заглушка боя
		double SwitchS = 0.0;                   // ★ 22.10 KB21–KB27: момент смены клавиш сценария
		double SendS = 0.0;                     // ★ 22.10 KB23: момент 4006, после которого ждём смену
		bool bMovedAfter = false;               // ★ 22.10: персонаж шёл хоть в одном опросе после отметки
		bool bSrvStun = false;                  // ★ 22.10 KB32–KB35: «сервер» — на игроке оглушение (`sub_484D20` → `CharMove` result 6)
		bool bSrvStunStopped = false;           // ★ 22.10: «сервер» снял игрока со стека хода наложением стана (`ProcessState_*`)
		int32 NRev = 0;                         // ★ 22.10: ответов 4009 «CharStop Revision Pos» (точка сервера вместо клиентской)
		FVector CamRel0 = FVector::ZeroVector;  // ★ 22.10 KB47: камера − тело до поправки
		float DriftMax = 0.f;                   // ★ 22.10 KB47: наибольший сдвиг тела в кадре после поправки
		bool bWasIgnoringInput = false;         // ★ 22.10: вьюпорт игнорировал ввод до фазы 22.10 (вернуть базовый режим UIOnly в конце)

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		FVector2D Pos() const { AAsdaPlayerCharacter* C = Ch(); return C ? C->GetGamePosXZ() : FVector2D::ZeroVector; }
		// Число перехватов опкода с индекса From.
		int32 Count(uint16 Op, int32 From = 0) const { int32 R = 0; for (int32 i = From; i < Caps.Num(); ++i) { if (Caps[i].Op == Op) { ++R; } } return R; }
		// Первый перехват опкода с индекса From.
		const FKCap* FirstOf(uint16 Op, int32 From) const { for (int32 i = From; i < Caps.Num(); ++i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr; }
		// Одна проверка.
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-КЛАВИШИ %s [ключ=%d] %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Key, Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		// Точка сервера на сейчас: интеграция отрезка по скорости (`MoveSyncProcess@0x5427D0`), клетка — по ней (:54527).
		void SrvIntegrate()
		{
			if (!bSrvMoving) { return; }
			const FVector2D D = SrvDst - SrvStart;
			const double Len = D.Size();
			const double ElS = FPlatformTime::Seconds() - SrvStartS;
			const double StepS = bSrvRealistic ? FMath::FloorToDouble(ElS / 0.1) * 0.1 : ElS;   // такт `MoveSyncProcess` 100 мс
			const double Went = FMath::Min(Len, StepS * 1000.0 * (double)SrvSpeed);
			SrvPos = (Len > 0.0) ? SrvStart + D / Len * Went : SrvStart;
			SrvTile = AsdaCoord::GameToTile(SrvPos);
		}
		// `sub_523310`: мс пути от начала отрезка до точки при скорости сервера.
		double PathMs(const FVector2D& P) const { return FVector2D::Distance(SrvStart, P) / FMath::Max(0.0001, (double)SrvSpeed); }
	};

	// Нажать/отпустить клавишу штатным путём `UPlayerInput` (как `asda.Keybinds.HoldSim`).
	static void KeyEv(APlayerController* PC, const FKey& K, bool bDown)
	{
		if (PC) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
	}
	// Отпустить все четыре клавиши хода.
	static void ReleaseAll(APlayerController* PC)
	{
		KeyEv(PC, EKeys::W, false); KeyEv(PC, EKeys::S, false); KeyEv(PC, EKeys::A, false); KeyEv(PC, EKeys::D, false);
	}
	// ★ 22.10.2026: клавиша ПУТЁМ ИГРОКА — событие Slate (`ProcessKeyDownEvent` → вьюпорт → `UGameViewportClient::InputKey` с его
	//   гейтом `IgnoreInput` → `UPlayerInput`), как у самотестов рыбалки/модалки. `PC->InputKey` идёт мимо гейта вьюпорта и
	//   потерю отпускания под модалкой не показал бы (закон diagnostic-must-use-real-path).
	static void SlateKey(const FKey& K, bool bDown, bool bAlt = false)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		const FModifierKeysState Mods(false, false, false, false, bAlt, false, false, false, false);
		const FKeyEvent Ev(K, Mods, 0, false, 0, 0);
		if (bDown) { FSlateApplication::Get().ProcessKeyDownEvent(Ev); }
		else       { FSlateApplication::Get().ProcessKeyUpEvent(Ev); }
	}
	// Тип виджета с клавиатурным фокусом (для журнала прогона: остался ли фокус на вьюпорте).
	static FString FocusType()
	{
		if (!FSlateApplication::IsInitialized()) { return TEXT("-"); }
		const TSharedPtr<SWidget> Fw = FSlateApplication::Get().GetUserFocusedWidget(0);
		return Fw.IsValid() ? Fw->GetType().ToString() : TEXT("нет");
	}
	// Тело 4007 ответа «сервера».
	static void Reply4007(FKState& S, uint8 Res, int16 Sub, const FVector2D& Cur, const FVector2D& Dst, int16 Tgt)
	{
		FInject In; In.AtS = FPlatformTime::Seconds() + (S.bSrvRealistic ? S.RttS * 0.5 : S.RttS); In.Op = 4007;   // ответ — ещё ½RTT
		const int16 U = S.N() ? (int16)S.N()->SelfUserNum : 0;
		In.Body = MoveBody(Res, U, Sub, Cur, Dst, S.SrvSpeed, Tgt);
		S.Queue.Add(MoveTemp(In));
		if (Res == 5) { ++S.N5; } else if (Res == 0) { ++S.N0; } else if (Res == 2) { ++S.N2; }
	}
	// `CharMove@0x544EB0` (GameServer.c:124434) — ветви в порядке эталона (гейты занятости/навыка/проходимости опущены:
	//   сценарии идут по проходимым клеткам, подтип клавиш всегда 2).
	static void SrvCharMove(FKState& S, FVector2D Cur, const FVector2D& Dst, int16 Sub, int16 Tgt)
	{
		S.SrvIntegrate();
		if (S.bSrvStun) { Reply4007(S, 6, Sub, Cur, Dst, Tgt); return; }   // ★ 22.10: `sub_484D20` — обездвижен (GameServer.c:124511)
		if (S.bForceRefuse) { Reply4007(S, 0, Sub, Cur, Dst, Tgt); return; }
		if (AsdaCoord::GameToTile(Dst) == S.SrvTile) { Reply4007(S, 0, Sub, Cur, Dst, Tgt); return; }   // та же клетка
		if (S.bSrvMoving)
		{
			const double DS = S.PathMs(S.SrvPos), DC = S.PathMs(Cur);
			if (DS >= DC) { Cur = S.SrvPos; }                                  // сервер впереди → его точка
			else if (DC - DS > 1000.0)
			{
				Reply4007(S, 5, Sub, S.SrvPos, Dst, Tgt);                      // «CharReMove Hack Position»
				S.bSrvMoving = false;                                          // «Revision Pos»
				return;
			}
			else { S.SrvPos = Cur; }                                           // клиент впереди ≤ 1000 мс — принят
		}
		else
		{
			if ((float)S.SrvPos.X != (float)Cur.X || (float)S.SrvPos.Y != (float)Cur.Y)
			{
				Reply4007(S, 5, Sub, S.SrvPos, Dst, Tgt);                      // «CharMove Hack Position»
				return;
			}
			if ((float)Dst.X == (float)Cur.X && (float)Dst.Y == (float)Cur.Y) { Reply4007(S, 2, Sub, Cur, Dst, Tgt); return; }
			S.SrvTile = AsdaCoord::GameToTile(Cur);
		}
		S.SrvStart = Cur; S.SrvPos = Cur; S.SrvDst = Dst; S.SrvStartS = FPlatformTime::Seconds(); S.bSrvMoving = true; S.bSrvStunStopped = false;
		Reply4007(S, 1, Sub, Cur, Dst, Tgt);
	}
	// `CharStop@0x545C70` (GameServer.c:124657): клиент впереди > 1000 мс → 4009 в точку сервера, иначе точка клиента.
	static void SrvCharStop(FKState& S, const FVector2D& Cur)
	{
		S.SrvIntegrate();
		FVector2D P = Cur;
		// ★ 22.10: сравнение `CharStop` идёт от начала отрезка и у стоящего сервера (GameServer.c:124715) — моделируем его и после
		//   молчаливой остановки станом (`bSrvStunStopped`); прочие сценарии стоящего сервера — как прежде.
		if ((S.bSrvMoving || S.bSrvStunStopped) && S.PathMs(Cur) > S.PathMs(S.SrvPos) + 1000.0) { P = S.SrvPos; ++S.NRev; }
		S.bSrvMoving = false; S.bSrvStunStopped = false;
		S.SrvPos = P; S.SrvTile = AsdaCoord::GameToTile(P);
		FInject In; In.AtS = FPlatformTime::Seconds() + (S.bSrvRealistic ? S.RttS * 0.5 : S.RttS); In.Op = 4009;
		const int16 U = S.N() ? (int16)S.N()->SelfUserNum : 0;
		In.Body = MoveBody(0, U, 0, P, P, S.SrvSpeed, -1);
		S.Queue.Add(MoveTemp(In));
	}
}

void FAsdaMove4007SelfTest::RunKeyboard(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMoveKbTestLocal;
	// ★ ПЕРВЫМ ОПЕРАТОРОМ — офлайн-гейт (подмена «в мире», перехват всего исходящего, поддельные ответы).
	static const uint16 kKbGuardOps[] = { 4006, 4008, 4024 };
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Move.SelfTestKeyboard"), KitArgs, 0.f, MakeArrayView(kKbGuardOps))) { return; }
	const float Delay = AsdaSelfTestKit::DelayArg(Args);
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaMove4007Test, Warning, TEXT("САМОТЕСТ-КЛАВИШИ: нет подсистемы сети")); return; }

	TSharedRef<FKState> S = MakeShared<FKState>();
	S->Net = Net; S->World = W;
	for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { S->bQuit = true; } }
	S->Gate.bQuit = S->bQuit;   // ★ СРЕДН-1: отказ на отложенном старте при `quit` закрывает процесс
	IConsoleVariable* KeyVar = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Move.KeyboardEtalon"));
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходные KeyboardEtalon / KeyboardNoticeOnce запоминает `AsdaCVarSave::SetForTest`,
	//   итог возвращает `RestoreAll` вместе с SetBy (прежде — снимок KeyDefault с SetByConsole; NoticeOnce — константой 1).
	// Перехват строк лога (нотис автобега, отказы) — тот же приём, что у C2b.
	TSharedRef<FLogGrab> Grab = MakeShared<FLogGrab>();
	Grab->Needle = TEXT("[клавиши] автобег включён");

	// ── Сброс сценария: снап домой, «сервер» стоит дома, бит 2 и незакрытый ход сняты, схема клавиш сброшена ──
	auto Reset = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* N = S->N();
		if (PC)
		{
			PC->CancelRoute();
			PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false;
			PC->SetOptionAlwaysRun(false);
			PC->KbDebugLmb = 0; PC->KbDebugRmb = 0;
			PC->CamYawDeg = S->YawA; PC->CamPitchDeg = -20.f;
		}
		// Сидение (KB11) снимается БЕЗ хвоста вставания: смена состояния на «идёт» перебивает фазу сразу (как у эталона
		//   `CChar_SetState(…, 1)`), затем снап — покой.
		if (C) { C->StopMove(); C->SetAnimState(EAsdaPlayerState::Move); C->NetSnap(S->Home); }
		if (N) { N->ClearMoveRequestInFlight(TEXT("самотест клавиш: сброс")); N->bActionPending = false; N->bSelfDead = false; }
		if (AAsdaMobActor* M = S->Mob.Get()) { M->Destroy(); }
		S->Mob = nullptr;
		if (PC) { PC->TargetActor = nullptr; }
		if (N)
		{
			if (UAsdaEffectsSubsystem* Fx = N->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>())
			{
				Fx->RemoveBySkillState(Fx->SelfUserIdx(), 1, 0);
				Fx->RemoveBySkillState(Fx->SelfUserIdx(), 547, 0);
				Fx->RemoveBySkillState(Fx->SelfUserIdx(), 3, 0);   // ★ 22.10: стан KB32–KB35
			}
			if (UAsdaCastBarSubsystem* Cb = N->GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>()) { if (Cb->IsActive()) { Cb->End(TEXT("самотест клавиш: сброс")); } }
		}
		if (PC) { PC->KbResetState(); PC->bAutoRunActive = false; }
		S->Queue.Reset();
		S->bAuto = true; S->bForceRefuse = false; S->SrvSpeed = 0.35f;
		S->bSrvMoving = false; S->SrvPos = S->Home; S->SrvStart = S->Home; S->SrvDst = S->Home; S->SrvTile = AsdaCoord::GameToTile(S->Home);
		S->N5 = S->N0 = S->N2 = 0;
		S->bSrvStun = false; S->bSrvStunStopped = false; S->NRev = 0;
		S->SrvIn.Reset(); S->SrvExtraUpS = 0.0;
		S->Mark = S->Caps.Num(); S->MarkS = FPlatformTime::Seconds();
		S->bFlag = false; S->bProbe = false; S->ProbeF = 0.f; S->ProbePos = S->Home;
	};
	auto Add = [S](const FString& Name, float MinSec, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.MinSec = MinSec; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Пауза с отпущенными клавишами (ветка отпускания прошлого сценария отрабатывает ДО отметки следующего).
	auto Pause = [S, Add](const FString& Name)
	{
		Add(Name, 0.35f, 1.f, [S]() { if (AAsdaPlayerController* PC = S->PC()) { ReleaseAll(PC); PC->KbDebugLmb = 0; PC->KbDebugRmb = 0; } S->bAuto = true; },
			[]() { return true; }, []() {});
	};
	auto T = [S]() { return FPlatformTime::Seconds() - S->MarkS; };   // секунды от отметки сценария

	// ─────────────────────────── Подготовка ───────────────────────────
	Add(TEXT("подготовка"), 0.f, 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
		if (!C || !PC) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->Home = C->GetGamePosXZ();
		const UAsdaWalkGrid* G = C->WalkGrid;
		bool bFound = false;
		for (int32 a = 0; a < 32 && !bFound; ++a)
		{
			const FVector2D A(FMath::Cos(a * PI / 16.f), FMath::Sin(a * PI / 16.f));
			if (!G || AsdaPath::IsLineWalkable(G, S->Home / AsdaCoord::kTileUnits, (S->Home + A * 4500.f) / AsdaCoord::kTileUnits)) { S->DirA = A; bFound = true; }
		}
		const FVector2D Ue = AsdaCoord::GameDirToUe(S->DirA);
		S->YawA = FMath::RadiansToDegrees(FMath::Atan2(Ue.Y, Ue.X));
		// ★ KB19 (ревизия МЕЛК-3): направление, где на пути к точке хода (≤ 700 ед.) лежит непроходимая клетка, а до неё
		//   ≥ 150 ед. проходимо. Ищем от дома и от ближних точек по сетке 500 ед.
		if (G)
		{
			for (int32 oy = -4; oy <= 4 && !S->bWallOk; ++oy)
			{
				for (int32 ox = -4; ox <= 4 && !S->bWallOk; ++ox)
				{
					const FVector2D P0 = S->Home + FVector2D(ox * 500.f, oy * 500.f);
					if (!G->IsWorldWalkable(P0.X, P0.Y)) { continue; }
					for (int32 a = 0; a < 32 && !S->bWallOk; ++a)
					{
						const FVector2D A(FMath::Cos(a * PI / 16.f), FMath::Sin(a * PI / 16.f));
						for (float d = 25.f; d <= 700.f; d += 25.f)
						{
							const FVector2D Q = P0 + A * d;
							if (!G->IsWorldWalkable(Q.X, Q.Y))
							{
								if (d >= 150.f) { S->WallHome = P0; S->WallDir = A; S->WallDist = d; S->bWallOk = true; }
								break;
							}
						}
					}
				}
			}
		}
		S->Expect(S->bWallOk, FString::Printf(TEXT("стена для KB19: от %s по (%.2f,%.2f) через %.0f ед."), *V2(S->WallHome), S->WallDir.X, S->WallDir.Y, S->WallDist));
		S->Expect(bFound, FString::Printf(TEXT("проходимое направление 4500 ед.: дом %s, A=(%.2f,%.2f), курс камеры %.1f°"),
			*V2(S->Home), S->DirA.X, S->DirA.Y, S->YawA));
		TWeakPtr<FKState> WS = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WS](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FKState> St = WS.Pin();
			if (!St || !Pl) { return; }
			FKCap Cp; Cp.Op = Op; Cp.AtS = FPlatformTime::Seconds();
			Cp.FrameS = (float)FApp::GetDeltaTime();   // ★ R5-fix6: длина кадра — для порога KB28d
			if (AAsdaPlayerCharacter* Cm = St->Ch()) { Cp.bMovingAtSend = Cm->IsMoving(); }
			if ((Op == 4006 || Op == 4008) && Len >= 43)
			{
				const uint8* B = Pl + 16;
				Cp.Act = RdI16(B + 3); Cp.Tgt = RdI16(B + 25);
				Cp.Cur = FVector2D(RdF(B + 5), RdF(B + 9)); Cp.Dst = FVector2D(RdF(B + 13), RdF(B + 17));
				St->Caps.Add(Cp);
				if (!St->bAuto) { return; }
				if (St->bSrvRealistic) { FKCap In = Cp; In.AtS = Cp.AtS + St->RttS * 0.5 + St->SrvExtraUpS; St->SrvIn.Add(In); return; }   // приём через ½RTT
				if (Op == 4006) { SrvCharMove(*St, Cp.Cur, Cp.Dst, Cp.Act, Cp.Tgt); }
				else            { SrvCharStop(*St, Cp.Cur); }
			}
			else if (Op == 4024 && Len >= 29)
			{
				const uint8* B = Pl + 16;
				Cp.Act = RdI16(B + 2); Cp.Cur = FVector2D(RdF(B + 5), RdF(B + 9));
				St->Caps.Add(Cp);
			}
		}, (uint16)0, TEXT("asda.Move.SelfTestKeyboard"));
		// ★ 22.10.2026: прежние сценарии проверяют ход ПО ГРАНТУ (эталон) — локальный старт на их время выключен (`asda.Move.LocalStart 0`);
		//   сам локальный старт проверяет `asda.Move.SelfTestKeyboard` (KB40–KB46).
		AsdaCVarSave::SetForTest(TEXT("asda.Move.LocalStart"), 0);
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
	}, []() { return true; }, []() {});

	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("фаза ключ=%d"), K), 0.f, 1.f, [S, K, KeyVar]()
		{
			S->Key = K;
			if (KeyVar) { AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardEtalon"), K); }
			S->Expect(AAsdaPlayerController::IsKeyboardEtalon() == (K != 0), TEXT("ключ asda.Move.KeyboardEtalon применён"));
		}, []() { return true; }, []() {});

		// KB1. Старт: до гранта 4007 персонаж стоит; точка — 800·cos(наклон) по взгляду камеры.
		Pause(TEXT("пауза KB1"));
		Add(TEXT("KB1 старт по гранту"), 0.45f, 2.f, [S, Reset]()
		{
			Reset();
			KeyEv(S->PC(), EKeys::W, true);
		}, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			if (!S->bFlag && S->Count(4006, S->Mark) > 0) { S->bFlag = true; }
			return true;
		}, [S, K]()
		{
			const FKCap* F = S->FirstOf(4006, S->Mark);
			S->bProbe = F && F->bMovingAtSend;
			const float Len = F ? (float)FVector2D::Distance(F->Cur, F->Dst) : -1.f;
			const float Want = K ? 800.f * FMath::Cos(FMath::DegreesToRadians(20.f)) : 800.f;
			S->Expect(F && Near(F->Cur, S->Home, 2.f) && FMath::Abs(Len - Want) < 2.f
				&& FVector2D::DotProduct((F->Dst - F->Cur).GetSafeNormal(), S->DirA) > 0.999f,
				FString::Printf(TEXT("первый 4006: cur %s, длина %.1f (ожидалось %.1f %s), по взгляду камеры"),
					F ? *V2(F->Cur) : TEXT("-"), Len, Want, K ? TEXT("= 800·cos 20° (эталон)") : TEXT("= 800 (прежний порт)")));
			if (K) { S->Expect(S->bFlag && !S->bProbe, TEXT("в кадр отправки 4006 персонаж СТОИТ (эталон: ход только по гранту)")); }
			else   { S->Expect(S->bFlag && S->bProbe, TEXT("в кадр отправки персонаж уже идёт (прежний порт — контроль)")); }
			AAsdaPlayerCharacter* C = S->Ch();
			if (K) { S->Expect(C && C->IsMoving() && FAsdaMove4007SelfTest::ToPoint(C), TEXT("после гранта идёт К ТОЧКЕ (charMoveToWorldPos)")); }
			else   { S->Expect(C && C->IsMoving() && !FAsdaMove4007SelfTest::ToPoint(C), TEXT("прежний порт: ход «по направлению» (контроль)")); }
		});

		// KB2. Прямо, удержание 2.6 с: повтор раз в 1000 мс. [без пары — регрессия, одинаково при обоих ключах]
		Pause(TEXT("пауза KB2"));
		Add(TEXT("KB2 повтор 1000 мс"), 2.6f, 4.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); },
			[]() { return true; }, [S]()
		{
			TArray<double> Ts; for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4006) { Ts.Add(S->Caps[i].AtS); } }
			bool bGaps = Ts.Num() == 3;
			FString G;
			for (int32 i = 1; i < Ts.Num(); ++i) { const double D = Ts[i] - Ts[i - 1]; G += FString::Printf(TEXT(" %.3f"), D); bGaps &= (D > 0.95 && D < 1.25); }
			S->Expect(bGaps, FString::Printf(TEXT("4006 за 2.6 с: %d шт., промежутки%s с (ждали 3 шт. по ~1 с)"), Ts.Num(), *G));
			S->Expect(FVector2D::Distance(S->Pos(), S->Home) > 700.f, FString::Printf(TEXT("бег непрерывный: пройдено %.0f"), FVector2D::Distance(S->Pos(), S->Home)));
			S->Expect(S->N5 == 0, FString::Printf(TEXT("античит сервера не сработал: result 5 — %d"), S->N5));
		});

		// KB3. Сервер отказывает (result 0): эталон повторяет запрос каждые 10 мс после ответа, персонаж стоит.
		Pause(TEXT("пауза KB3"));
		Add(TEXT("KB3 отказ → повтор 10 мс"), 1.0f, 2.f, [S, Reset]() { Reset(); S->bForceRefuse = true; KeyEv(S->PC(), EKeys::W, true); },
			[]() { return true; }, [S, K]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			const float Went = (float)FVector2D::Distance(S->Pos(), S->Home);
			if (K) { S->Expect(N6 >= 6 && Went < 5.f, FString::Printf(TEXT("4006 за 1 с: %d (≥ 6, повтор после каждого ответа), сдвиг %.0f (стоит)"), N6, Went)); }
			else   { S->Expect(N6 <= 2 && Went > 200.f, FString::Printf(TEXT("прежний порт: 4006 %d (≤ 2), идёт вопреки отказу %.0f (контроль)"), N6, Went)); }
		});

		// KB4. Бит 2 взведён (прошлый 4006 без ответа): попытка клавиш отброшена.
		Pause(TEXT("пауза KB4"));
		Add(TEXT("KB4 бит 2"), 0.4f, 1.f, [S, Reset]()
		{
			Reset(); S->bAuto = false;
			if (UAsdaNetSubsystem* N = S->N()) { N->bMoveRequestInFlight = true; N->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs(); }
			KeyEv(S->PC(), EKeys::W, true);
		}, []() { return true; }, [S, K]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			if (K) { S->Expect(N6 == 0, FString::Printf(TEXT("при взведённом бите 2 4006 не уходит: %d"), N6)); }
			else   { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт шлёт и при бите 2: %d (контроль)"), N6)); }
			if (UAsdaNetSubsystem* N = S->N()) { N->ClearMoveRequestInFlight(TEXT("самотест клавиш")); }
		});

		// KB5. W, затем W+S: пока держатся обе, таймер сбрасывается каждый кадр — 4006 не уходит.
		Pause(TEXT("пауза KB5"));
		Add(TEXT("KB5a W"), 0.5f, 1.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, []() {});
		Add(TEXT("KB5b W+S"), 1.3f, 2.f, [S]() { S->Mark = S->Caps.Num(); KeyEv(S->PC(), EKeys::S, true); }, []() { return true; }, [S, K]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			if (K) { S->Expect(N6 == 0, FString::Printf(TEXT("W+S 1.3 с: 4006 %d (эталон — ни одного)"), N6)); }
			else   { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: 4006 %d (контроль)"), N6)); }
		});

		// KB6. A стоя (без ПКМ): камера крутится; эталон шлёт 4006 В СВОЮ ТОЧКУ раз в 1000 мс.
		Pause(TEXT("пауза KB6"));
		Add(TEXT("KB6 A стоя"), 2.3f, 3.f, [S, Reset]() { Reset(); S->ProbeF = S->PC() ? S->PC()->CamYawDeg : 0.f; KeyEv(S->PC(), EKeys::A, true); },
			[]() { return true; }, [S, K]()
		{
			int32 N6 = 0, NSelf = 0;
			for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4006) { ++N6; if (Near(S->Caps[i].Cur, S->Caps[i].Dst, 0.5f)) { ++NSelf; } } }
			const float Turn = S->PC() ? S->PC()->CamYawDeg - S->ProbeF : 0.f;
			S->Expect(Turn < -150.f && Turn > -260.f, FString::Printf(TEXT("камера повернулась на %.0f° (≈ −91.7°/с)"), Turn));
			if (K) { S->Expect(N6 >= 2 && N6 == NSelf, FString::Printf(TEXT("4006 в свою точку: %d из %d (эталон: стоп через сервер)"), NSelf, N6)); }
			else   { S->Expect(N6 == 0, FString::Printf(TEXT("прежний порт: пакетов нет (%d) — контроль"), N6)); }
		});

		// KB7. Отпускание после разгона: покой сразу, 4006 cur→cur, затем 4008 в ту же точку.
		Pause(TEXT("пауза KB7"));
		Add(TEXT("KB7a разгон"), 0.5f, 1.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, []() {});
		Add(TEXT("KB7b отпускание"), 0.3f, 1.f, [S]()
		{
			S->Mark = S->Caps.Num(); S->bFlag = false;
			KeyEv(S->PC(), EKeys::W, false);
		}, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			if (!S->bFlag && S->Count(4008, S->Mark) > 0) { S->bFlag = true; S->bProbe = C && C->IsMoving(); }
			return true;
		}, [S, K]()
		{
			const FKCap* F6 = S->FirstOf(4006, S->Mark); const FKCap* F8 = S->FirstOf(4008, S->Mark);
			if (K)
			{
				S->Expect(F6 && F8 && F6->AtS <= F8->AtS && Near(F6->Cur, F6->Dst, 0.5f) && Near(F8->Cur, F6->Cur, 0.5f),
					FString::Printf(TEXT("отпускание: 4006 cur→cur %s, затем 4008 %s (эталон game.c:8989-9013)"),
						F6 ? *V2(F6->Cur) : TEXT("-"), F8 ? *V2(F8->Cur) : TEXT("-")));
				S->Expect(S->bFlag && !S->bProbe, TEXT("в кадр 4008 персонаж уже в покое (SetAction(0) сразу)"));
			}
			else
			{
				S->Expect(!F6 && F8, FString::Printf(TEXT("прежний порт: только 4008 (4006: %d, 4008: %d) — контроль"), S->Count(4006, S->Mark), S->Count(4008, S->Mark)));
			}
		});

		// KB8. ЛКМ при удержании W: флаги гаснут (стоп через сервер), после отпускания ЛКМ автоповтор снова взводит W.
		Pause(TEXT("пауза KB8"));
		Add(TEXT("KB8a разгон 1.1 с"), 1.1f, 2.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, []() {});
		Add(TEXT("KB8b ЛКМ"), 0.12f, 1.f, [S]() { S->Mark = S->Caps.Num(); if (AAsdaPlayerController* PC = S->PC()) { PC->KbDebugLmb = 1; } },
			[]() { return true; }, [S, K]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			if (K) { S->Expect(S->Count(4008, S->Mark) == 1 && C && !C->IsMoving(), FString::Printf(TEXT("ЛКМ: 4008 %d, персонаж стоит"), S->Count(4008, S->Mark))); }
			else   { S->Expect(S->Count(4008, S->Mark) == 0 && C && C->IsMoving(), TEXT("прежний порт: ЛКМ ход не прерывает (контроль)")); }
			if (AAsdaPlayerController* PC = S->PC()) { PC->KbDebugLmb = 0; }
			S->Mark = S->Caps.Num();
		});
		Add(TEXT("KB8c ЛКМ отпущена, W держится"), 0.4f, 1.f, []() {}, []() { return true; }, [S, K]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			if (K)
			{
				S->Expect(S->Count(4006, S->Mark) >= 1 && C && C->IsMoving(),
					FString::Printf(TEXT("автоповтор клавиши снова взвёл W: 4006 %d, идёт %d"), S->Count(4006, S->Mark), C && C->IsMoving()));
			}
			else { S->Expect(C && C->IsMoving(), TEXT("прежний порт: ход не прерывался (контроль)")); }
		});

		// KB9. Бег 10 с с автоответчиком-античитом: result 5 не приходит. [без пары — регрессия]
		Pause(TEXT("пауза KB9"));
		Add(TEXT("KB9 бег 10 с"), 10.f, 12.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, [S]()
		{
			const float Went = (float)FVector2D::Distance(S->Pos(), S->Home);
			S->Expect(S->N5 == 0 && Went > 3000.f, FString::Printf(TEXT("result 5: %d, пройдено %.0f (≥ 3000), 4006: %d"), S->N5, Went, S->Count(4006, S->Mark)));
		});

		// KB10. S: грант ведёт к точке позади, персонаж пятится (лицом по камере, клип back01) — [208].
		Pause(TEXT("пауза KB10"));
		Add(TEXT("KB10 пятка"), 0.5f, 1.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::S, true); }, []() { return true; }, [S, K]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			const bool bBack = C && C->bBackpedal;
			if (K) { S->Expect(C && C->IsMoving() && FAsdaMove4007SelfTest::ToPoint(C) && bBack, TEXT("идёт К ТОЧКЕ позади, пятясь (эталон [208])")); }
			else   { S->Expect(C && C->IsMoving() && !FAsdaMove4007SelfTest::ToPoint(C) && bBack, TEXT("прежний порт: пятится «по направлению» (контроль)")); }
			S->Expect(FVector2D::DotProduct(S->Pos() - S->Home, S->DirA) < -30.f, FString::Printf(TEXT("сдвиг назад от взгляда: %s"), *V2(S->Pos() - S->Home)));
		});

		// KB11. Сидит (фаза 8) + W: эталон встаёт пакетом 4024 код 109, хода в этот кадр нет.
		Pause(TEXT("пауза KB11"));
		Add(TEXT("KB11a сесть"), 2.6f, 3.f, [S, Reset]() { Reset(); if (AAsdaPlayerCharacter* C = S->Ch()) { C->SetAnimState(EAsdaPlayerState::Sit); } },
			[]() { return true; }, [S]() { AAsdaPlayerCharacter* C = S->Ch(); S->Expect(C && C->GetSitPhase() == 8, FString::Printf(TEXT("фаза сидения %d (8)"), C ? C->GetSitPhase() : -1)); });
		// Свидетельства: строка лога ветки «встать», фаза «встаёт» (9) у персонажа и сам пакет 4024 — с 27.09.2026
		//   `SendCharStatePos4024` отдаёт тело перехватчику (`pkt4024_facing.md`; до того офлайн выходил до отправки).
		//   В теле +5/+9 — единичный курс (направление взгляда), не позиция.
		Add(TEXT("KB11b W сидя"), 0.2f, 1.f, [S, Grab]()
		{
			S->Mark = S->Caps.Num();
			// ★ 27.09.2026: при `asda.Char.SitToggleEtalon 1` ветку ведёт общая `ToggleSitStand` — строка её лога.
			Grab->Needle = AAsdaPlayerController::IsSitToggleEtalon() ? TEXT("[сесть/встать] (клавиши хода): встать") : TEXT("[клавиши] сидит → встать (4024 код 109)"); Grab->Hits = 0;
			if (GLog) { GLog->AddOutputDevice(&Grab.Get()); }
			KeyEv(S->PC(), EKeys::W, true);
		}, []() { return true; }, [S, K, Grab]()
		{
			if (GLog) { GLog->RemoveOutputDevice(&Grab.Get()); }
			AAsdaPlayerCharacter* C = S->Ch();
			const int32 Ph = C ? C->GetSitPhase() : -1;
			const int32 N6 = S->Count(4006, S->Mark);
			const int32 N24 = S->Count(4024, S->Mark);           // перехвачено 4024 за шаг
			const FKCap* P24 = S->FirstOf(4024, S->Mark);         // первый из них
			if (K) { S->Expect(Grab->Hits == 1 && Ph == 9 && N6 == 0, FString::Printf(TEXT("встать: ветка 4024/109 — %d раз, фаза %d (9 — встаёт), 4006 %d (хода нет)"), Grab->Hits, Ph, N6)); }
			else   { S->Expect(Grab->Hits == 0 && Ph == 0, FString::Printf(TEXT("прежний порт: локальный переход без 4024 (%d), фаза %d — контроль"), Grab->Hits, Ph)); }
			// Пакет: при ключе 1 — ровно один 4024 код 109 с единичным курсом; при ключе 0 — ни одного (контроль).
			if (K) { S->Expect(N24 == 1 && P24 && P24->Act == 109 && FMath::IsNearlyEqual((float)P24->Cur.Size(), 1.f, 0.01f),
				FString::Printf(TEXT("пакет 4024: %d шт., код %d, тело (%.3f,%.3f) — единичный курс"), N24, P24 ? P24->Act : -1, P24 ? P24->Cur.X : 0.0, P24 ? P24->Cur.Y : 0.0)); }
			else   { S->Expect(N24 == 0, FString::Printf(TEXT("прежний порт: пакета 4024 нет (%d) — контроль"), N24)); }
		});

		// KB12. Крутой наклон (−75°): точка в 207 ед., грант доводит до неё быстрее повтора — эталон шлёт 4008 по прибытии,
		//   затем через 10 мс новый 4006 (стоит с W).
		Pause(TEXT("пауза KB12"));
		Add(TEXT("KB12 прибытие → 4008"), 1.6f, 2.5f, [S, Reset]() { Reset(); if (AAsdaPlayerController* PC = S->PC()) { PC->CamPitchDeg = -75.f; } KeyEv(S->PC(), EKeys::W, true); },
			[]() { return true; }, [S, K]()
		{
			const int32 N6 = S->Count(4006, S->Mark), N8 = S->Count(4008, S->Mark);
			if (K) { S->Expect(N8 >= 1 && N6 >= 2 && S->N5 == 0, FString::Printf(TEXT("W держится: 4008 по прибытии %d, 4006 %d, result 5 %d"), N8, N6, S->N5)); }
			else   { S->Expect(N8 == 0, FString::Printf(TEXT("прежний порт: 4008 при удержании нет (%d) — контроль"), N8)); }
		});

		// KB13. Автобег (опция + W > 3 с): эталонный нотис 110507, после отпускания W бег продолжается.
		Pause(TEXT("пауза KB13"));
		Add(TEXT("KB13a автобег"), 3.4f, 4.f, [S, Reset, Grab]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { PC->SetOptionAlwaysRun(true); }
			Grab->Needle = TEXT("[клавиши] автобег включён"); Grab->Hits = 0;
			if (GLog) { GLog->AddOutputDevice(&Grab.Get()); }
			KeyEv(S->PC(), EKeys::W, true);
		}, []() { return true; }, [S]() { KeyEv(S->PC(), EKeys::W, false); });
		Add(TEXT("KB13b W отпущена"), 0.6f, 1.f, []() {}, []() { return true; }, [S, K, Grab]()
		{
			if (GLog) { GLog->RemoveOutputDevice(&Grab.Get()); }
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
			S->Expect(PC && PC->bAutoRunActive && C && C->IsMoving(), TEXT("автобег держит ход после отпускания W"));
			if (K) { S->Expect(Grab->Hits == 1, FString::Printf(TEXT("нотис 110507 «автобег включён»: %d строк"), Grab->Hits)); }
			else   { S->Expect(Grab->Hits == 0, FString::Printf(TEXT("прежний порт без нотиса: %d — контроль"), Grab->Hits)); }
			KeyEv(PC, EKeys::S, true);   // S гасит автобег (следующая пауза отпустит)
		});

		// KB14. Свиток возврата склада (+0x5734, верхний гейт эталона): тик клавиш не исполняется — ни хода, ни нотиса.
		Pause(TEXT("пауза KB14"));
		// ★ 11.11.2026 (D4b): контрольная ветка (ключ 0) ждёт первый 4006 условием (до 3 с), а не фиксированные 0.5 с — в двух
		//   прогонах из трёх ход за 0.5 с не успевал уйти (254/1). Ветка ключа 1 по-прежнему держит шаг 0.5 с: 4006 быть не должно.
		Add(TEXT("KB14 склад-свиток"), 0.5f, 3.f, [S, Reset]()
		{
			Reset();
			if (UAsdaWarehouseSubsystem* Wh = S->N() ? S->N()->GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr) { Wh->OpenRecallPanel(); }
			KeyEv(S->PC(), EKeys::W, true);
		}, [S, K]() { return K != 0 || S->Count(4006, S->Mark) >= 1; }, [S, K]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			if (UAsdaWarehouseSubsystem* Wh = S->N() ? S->N()->GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
			{
				S->Expect(Wh->IsRecallActive(), TEXT("свиток склада был активен весь шаг"));
				Wh->CloseRecallPanel();
			}
			if (K) { S->Expect(N6 == 0, FString::Printf(TEXT("свиток склада: 4006 %d (верхний гейт)"), N6)); }
			else   { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: ход идёт (%d) — контроль"), N6)); }
		});

		// KB15. Военный магазин 67200 открыт → `Party_CanInviteCheck` отказывает с нотисом 110611 в чат; хода нет.
		//   Эталон пишет строку КАЖДЫЙ КАДР (`asda.Move.KeyboardNoticeOnce 0`), порт по умолчанию — одну на удержание.
		Pause(TEXT("пауза KB15"));
		Add(TEXT("KB15a военный магазин, строка один раз"), 0.6f, 1.f, [S, Reset, Grab]()
		{
			Reset();
			if (UAsdaWarShopSubsystem* Ws = S->N()->GetGameInstance()->GetSubsystem<UAsdaWarShopSubsystem>()) { Ws->OpenWindow(); }
			Grab->Needle = TEXT("[клавиши] ход запрещён: нотис 110611"); Grab->Hits = 0;
			if (GLog) { GLog->AddOutputDevice(&Grab.Get()); }
			KeyEv(S->PC(), EKeys::W, true);
		}, []() { return true; }, [S, K, Grab]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			const UAsdaWarShopSubsystem* Ws = S->N()->GetGameInstance()->GetSubsystem<UAsdaWarShopSubsystem>();
			S->Expect(Ws && Ws->IsOpen(), TEXT("военный магазин открыт весь шаг"));
			if (K) { S->Expect(N6 == 0 && Grab->Hits == 1, FString::Printf(TEXT("4006 %d, строк 110611 %d (одна на удержание — [ДЕФЕКТ ОРИГИНАЛА ИСПРАВЛЕН])"), N6, Grab->Hits)); }
			else   { S->Expect(N6 >= 1 && Grab->Hits == 0, FString::Printf(TEXT("прежний порт: ход идёт (%d), отказа нет (%d) — контроль"), N6, Grab->Hits)); }
			Grab->Hits = 0; S->Mark = S->Caps.Num();
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardNoticeOnce"), 0);
		});
		Add(TEXT("KB15b строка каждый кадр (ключ отката исправления)"), 0.4f, 1.f, []() {}, []() { return true; }, [S, K, Grab]()
		{
			if (GLog) { GLog->RemoveOutputDevice(&Grab.Get()); }
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardNoticeOnce"), 1);
			if (UAsdaWarShopSubsystem* Ws = S->N()->GetGameInstance()->GetSubsystem<UAsdaWarShopSubsystem>()) { Ws->CloseWindow(); }
			if (K) { S->Expect(Grab->Hits >= 5 && S->Count(4006, S->Mark) == 0, FString::Printf(TEXT("asda.Move.KeyboardNoticeOnce 0: строк %d за 0.4 с (каждый кадр, как оригинал)"), Grab->Hits)); }
			else   { S->Expect(Grab->Hits == 0, FString::Printf(TEXT("прежний порт: строк нет (%d) — контроль"), Grab->Hits)); }
		});

		// KB16. Окно из `uiCanOpenWindowGate` (элитный магазин 6000) открылось посреди хода → клавиши «не нажаты» →
		//   ветка отпускания (стоп через сервер), без нотиса.
		Pause(TEXT("пауза KB16"));
		Add(TEXT("KB16a разгон"), 0.5f, 1.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, []() {});
		Add(TEXT("KB16b открыт элитный магазин"), 0.4f, 1.f, [S, Grab]()
		{
			S->Mark = S->Caps.Num();
			Grab->Needle = TEXT("[клавиши] ход запрещён"); Grab->Hits = 0;
			if (GLog) { GLog->AddOutputDevice(&Grab.Get()); }
			if (UAsdaPremiumShopSubsystem* Ps = S->N()->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>()) { Ps->OpenWindow(false); }
		}, []() { return true; }, [S, K, Grab]()
		{
			if (GLog) { GLog->RemoveOutputDevice(&Grab.Get()); }
			AAsdaPlayerCharacter* C = S->Ch();
			UAsdaPremiumShopSubsystem* Ps = S->N()->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
			S->Expect(Ps && Ps->IsOpen(), TEXT("элитный магазин открыт"));
			// ★ 24.10.2026 (выход из сидения 1:1, `asda.Char.SitExitEtalon`): вход в элитный магазин — `charBeginAutoMove@0xC3E420` — сам шлёт
			//   4008 на ходу (`getFlag_47551` → `Net_SendActionPacket`) и ставит покой; затем тик клавиш видит окно — ветка отпускания
			//   (4006 «в свою точку» + 4008). У эталона 4008 два; прежде порт шага магазина не делал — ждали один.
			const bool bScene = AAsdaPlayerCharacter::IsSitExitEtalon();
			if (K) { S->Expect(S->Count(4008, S->Mark) == (bScene ? 2 : 1) && C && !C->IsMoving() && Grab->Hits == 0,
				FString::Printf(TEXT("окно гейта: 4008 %d (ждали %d), стоит, нотисов %d (окна — молча)"), S->Count(4008, S->Mark), bScene ? 2 : 1, Grab->Hits)); }
			else if (bScene) { S->Expect(C && !C->IsMoving() && S->Count(4008, S->Mark) == 1,
				FString::Printf(TEXT("прежняя схема клавиш: ход остановил сам вход в магазин (charBeginAutoMove), 4008 %d — ждали 1"), S->Count(4008, S->Mark))); }
			else   { S->Expect(C && C->IsMoving(), TEXT("прежний порт: окно ход не прерывает (контроль)")); }
			if (Ps) { Ps->CloseWindow(); }
		});

		// ══ Исправления ревизии WASD (МЕЛК-1, МЕЛК-3) ══
		// KB8d. Автоповтор Windows — только последней нажатой клавиши: W, коснулись A, ЛКМ → после ЛКМ бег НЕ возобновляется.
		Pause(TEXT("пауза KB8d"));
		Add(TEXT("KB8d-1 разгон W 1.1 с"), 1.1f, 2.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, []() {});
		Add(TEXT("KB8d-2 тап A"), 0.1f, 1.f, [S]() { KeyEv(S->PC(), EKeys::A, true); }, []() { return true; }, [S]() { KeyEv(S->PC(), EKeys::A, false); });
		Add(TEXT("KB8d-3 ЛКМ"), 0.12f, 1.f, [S]() { if (AAsdaPlayerController* PC = S->PC()) { PC->KbDebugLmb = 1; } }, []() { return true; },
			[S]() { if (AAsdaPlayerController* PC = S->PC()) { PC->KbDebugLmb = 0; } S->Mark = S->Caps.Num(); });
		Add(TEXT("KB8d-4 ЛКМ отпущена, W держится"), 0.6f, 1.f, []() {}, []() { return true; }, [S, K]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			if (K) { S->Expect(S->Count(4006, S->Mark) == 0 && C && !C->IsMoving(),
				FString::Printf(TEXT("повтор был у A (последней нажатой) — W не взведена: 4006 %d, идёт %d (как Windows)"), S->Count(4006, S->Mark), C && C->IsMoving())); }
			else   { S->Expect(C && C->IsMoving(), TEXT("прежний порт: уровень W — идёт (контроль)")); }
		});

		// KB17. Бой (автоатака в стойке = эталонное состояние 5): тик клавиш не вызывается — 4006 нет, ЛКМ флаги не гасит.
		Pause(TEXT("пауза KB17"));
		Add(TEXT("KB17 бой: гейт главного цикла"), 0.6f, 1.2f, [S, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
			S->Mob = SpawnMob(C->GetWorld(), S->Home + S->DirA * 60.f, C->GetActorLocation().Z, 4242);
			PC->TargetActor = S->Mob.Get(); PC->bAutoAttack = true; PC->bInCombatStance = true; PC->bCombatStarted = true;
			PC->NextAttackTickTime = FPlatformTime::Seconds() + 30.0;   // замахов в шаге не нужно
			KeyEv(PC, EKeys::W, true);
			PC->KbDebugLmb = 1;
		}, []() { return true; }, [S, K]()
		{
			AAsdaPlayerController* PC = S->PC();
			const int32 N6 = S->Count(4006, S->Mark);
			if (K)
			{
				S->Expect(N6 == 0 && PC && PC->bAutoAttack, FString::Printf(TEXT("в бою: 4006 %d, бой не снят %d (эталон: состояние 5 — тик не вызывается)"), N6, PC && PC->bAutoAttack));
				S->Expect(PC && PC->bKbFlag[0], TEXT("ЛКМ в бою флаг W не гасит (гасит только тик, а он не вызывается)"));
			}
			else { S->Expect(N6 >= 1 && PC && !PC->bAutoAttack, FString::Printf(TEXT("прежний порт: клавиша выводит из боя и идёт (4006 %d) — контроль"), N6)); }
			if (PC) { PC->KbDebugLmb = 0; }
		});

		// KB17b. Оковы (запись «Окаменение» id 1 type 0 в списке состояний): функция не вызывается вовсе.
		Pause(TEXT("пауза KB17b"));
		Add(TEXT("KB17b оковы"), 0.5f, 1.f, [S, Reset]()
		{
			Reset();
			// ★ 22.10: id 1 (камень) входит в набор `asda.Move.StunHardStop` — пара по ключу схемы клавиш снимается при выключенной остановке.
			AsdaCVarSave::SetForTest(TEXT("asda.Move.StunHardStop"), 0);
			if (UAsdaEffectsSubsystem* Fx = S->N()->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>()) { Fx->AddOrUpdateSkill(Fx->SelfUserIdx(), 1, 0, 0, 4, 600, 0); }
			KeyEv(S->PC(), EKeys::W, true);
		}, []() { return true; }, [S, K]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			if (K) { S->Expect(N6 == 0, FString::Printf(TEXT("оковы (id 1): 4006 %d (listHasAnyOfSlotTypes)"), N6)); }
			else   { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: ход идёт (%d) — контроль"), N6)); }
			AsdaCVarSave::SetForTest(TEXT("asda.Move.StunHardStop"), 1);
		});

		// KB18. Молчаливые отказы `Party_CanInviteCheck`: полоса каста в состоянии 14 и запись 547 — 4006 нет, нотисов нет.
		for (const int32 Variant : { 14, 547 })
		{
			Pause(FString::Printf(TEXT("пауза KB18-%d"), Variant));
			Add(FString::Printf(TEXT("KB18 молчаливый отказ %d"), Variant), 0.5f, 1.f, [S, Reset, Grab, Variant]()
			{
				Reset();
				UGameInstance* G = S->N()->GetGameInstance();
				if (Variant == 547) { if (UAsdaEffectsSubsystem* Fx = G->GetSubsystem<UAsdaEffectsSubsystem>()) { Fx->AddOrUpdateSkill(Fx->SelfUserIdx(), 547, 0, 0, 4, 600, 0); } }
				else                { if (UAsdaCastBarSubsystem* Cb = G->GetSubsystem<UAsdaCastBarSubsystem>()) { Cb->Begin(501, 5000.f, 5000.f, Variant); } }
				Grab->Needle = TEXT("[клавиши] ход запрещён"); Grab->Hits = 0;
				if (GLog) { GLog->AddOutputDevice(&Grab.Get()); }
				KeyEv(S->PC(), EKeys::W, true);
			}, []() { return true; }, [S, K, Grab, Variant]()
			{
				if (GLog) { GLog->RemoveOutputDevice(&Grab.Get()); }
				const int32 N6 = S->Count(4006, S->Mark);
				if (K) { S->Expect(N6 == 0 && Grab->Hits == 0, FString::Printf(TEXT("%s: 4006 %d, строк отказа %d (молча)"), Variant == 547 ? TEXT("запись 547") : TEXT("каст, состояние 14"), N6, Grab->Hits)); }
				else   { S->Expect(N6 >= 1, FString::Printf(TEXT("прежний порт: ход идёт (%d) — контроль"), N6)); }
			});
		}

		// KB19. Упор в стену: «сервер» пропускает ход в стену (`sub_4221D0` — только границы карты), клиент упирается →
		//   4008 → через 10 мс снова 4006; античита нет.
		Pause(TEXT("пауза KB19"));
		Add(TEXT("KB19 упор в стену"), 3.8f, 4.5f,   // до стены ≤ 700 ед. = ≤ 2 с бега, затем ≥ 1.5 с цикла упора
			 [S, Reset]()
		{
			Reset();
			if (!S->bWallOk) { return; }
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC(); if (!C || !PC) { return; }
			C->NetSnap(S->WallHome); S->SrvPos = S->WallHome; S->SrvStart = S->WallHome; S->SrvTile = AsdaCoord::GameToTile(S->WallHome);
			const FVector2D Ue = AsdaCoord::GameDirToUe(S->WallDir);
			PC->CamYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Ue.Y, Ue.X));
			KeyEv(PC, EKeys::W, true);
		}, []() { return true; }, [S, K]()
		{
			if (!S->bWallOk) { S->Expect(false, TEXT("стены нет — сценарий не выполнен")); return; }
			const int32 N6 = S->Count(4006, S->Mark), N8 = S->Count(4008, S->Mark);
			const FVector2D P = S->Pos();
			AAsdaPlayerCharacter* C = S->Ch();
			const bool bWalk = !C || !C->WalkGrid || C->WalkGrid->IsWorldWalkable(P.X, P.Y);
			if (K) { S->Expect(N8 >= 2 && N6 >= 3 && S->N5 == 0 && bWalk,
				FString::Printf(TEXT("у стены: 4008 %d, 4006 %d (цикл упор→стоп→запрос), result 5 %d, стоит на проходимой %s"), N8, N6, S->N5, *V2(P))); }
			else   { S->Expect(N8 == 0 && bWalk, FString::Printf(TEXT("прежний порт: упёрся без 4008 (%d), позиция %s — контроль"), N8, *V2(P))); }
		});

		// KB20. Клик по земле при активной клавиатуре — без хода (`Input_WorldClickHandler`, game.c:13678-13684).
		Pause(TEXT("пауза KB20"));
		Add(TEXT("KB20a W"), 0.5f, 1.f, [S, Reset]() { Reset(); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, []() {});
		Add(TEXT("KB20b клик по земле"), 0.2f, 1.f, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
			if (UAsdaNetSubsystem* N = S->N()) { N->ClearMoveRequestInFlight(TEXT("самотест клавиш: клик")); }
			S->Mark = S->Caps.Num();
			S->ProbePos = C->GetGamePosXZ() + FVector2D(-S->DirA.Y, S->DirA.X) * 300.f;
			const FVector2D Ue = AsdaCoord::GameToUe2D(S->ProbePos);
			PC->LastMoveSendTime = 0.0;
			PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
		}, []() { return true; }, [S, K]()
		{
			int32 NClick = 0;
			for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4006 && Near(S->Caps[i].Dst, S->ProbePos, 5.f)) { ++NClick; } }
			if (K) { S->Expect(NClick == 0, FString::Printf(TEXT("клик при активной клавиатуре: 4006 к точке клика %d (без хода)"), NClick)); }
			else   { S->Expect(NClick >= 1, FString::Printf(TEXT("прежний порт: клик ведёт ход (%d) — контроль"), NClick)); }
		});
	}

	// ═══════════ ★ 22.10.2026 — БАГ-РЕПОРТ WASD (`Reports/wasd_2026-10-22/REPORT.md`) ═══════════
	//   Схема оригинала (`asda.Move.KeyboardEtalon 1`). Пара по ключам исправлений: Fx=1 — исправлено, Fx=0 — как было до правки
	//   (воспроизведение дефекта: каждая проверка Fx=1 ожидает ПРОТИВОПОЛОЖНОЕ при Fx=0). KB26/KB27 — регрессия без пары
	//   (поведение движка/Alt+Z, ключи на них не влияют).
	Add(TEXT("фаза 22.10: ключ схемы = 1"), 0.f, 1.f, [S, KeyVar]()
	{
		S->Key = 1;
		if (KeyVar) { AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardEtalon"), 1); }
		S->Expect(AAsdaPlayerController::IsKeyboardEtalon(), TEXT("ключ asda.Move.KeyboardEtalon = 1"));
		// Режим ввода мира, как у игрока (`UAsdaWorldSubsystem::RevealWorld`: базовый режим GameAndUI + фокус на вьюпорт). Офлайн-спавн
		//   (`FakeSelfSpawn`) его не ставит — базовый режим остаётся UIOnly экрана входа, и клавиши Slate вьюпорт игнорирует.
		S->bWasIgnoringInput = GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput();
		if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetBaseInputMode(true); }
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-КЛАВИШИ 22.10: режим ввода мира GameAndUI (до — IgnoreInput=%d, теперь %d)"),
			S->bWasIgnoringInput ? 1 : 0, (GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput()) ? 1 : 0);
	}, []() { return true; }, []() {});
	// Пауза с отпусканием и симулированных клавиш, и клавиш входа вьюпорта (KB25–KB27 жмут W событием Slate).
	auto PauseAll = [S, Add](const FString& Name)
	{
		Add(Name, 0.35f, 1.f, [S]()
		{
			if (AAsdaPlayerController* PC = S->PC())
			{
				ReleaseAll(PC);
				SlateKey(EKeys::W, false); SlateKey(EKeys::S, false); SlateKey(EKeys::A, false); SlateKey(EKeys::D, false);
				PC->KbDebugLmb = 0; PC->KbDebugRmb = 0;
			}
			S->bAuto = true; S->RttS = 0.08;
		}, []() { return true; }, []() {});
	};
	// Первый 4006 после отметки с направлением, близким к Want (единичный вектор), иначе nullptr.
	auto FirstDir = [S](const FVector2D& Want) -> const FKCap*
	{
		for (int32 i = S->Mark; i < S->Caps.Num(); ++i)
		{
			const FKCap& Cp = S->Caps[i];
			if (Cp.Op != 4006 || FVector2D::Distance(Cp.Cur, Cp.Dst) < 100.0) { continue; }
			if (FVector2D::DotProduct((Cp.Dst - Cp.Cur).GetSafeNormal(), Want) > 0.95) { return &Cp; }
		}
		return nullptr;
	};
	// Шаг «бег вперёд W 1.4 с» (общий старт KB21–KB24).
	auto RunW = [S, Add, Reset](const FString& Name, bool bRmb)
	{
		Add(Name, 1.4f, 2.f, [S, Reset, bRmb]()
		{
			Reset(); S->RttS = 0.08;
			if (AAsdaPlayerController* PC = S->PC()) { PC->KbDebugRmb = bRmb ? 1 : 0; KeyEv(PC, EKeys::W, true); }
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(C && C->IsMoving() && S->Count(4006, S->Mark) >= 1, FString::Printf(TEXT("бег по W идёт (4006 %d)"), S->Count(4006, S->Mark)));
		});
	};
	// Шаг «сменить клавиши сразу после очередного 4006» (смена попадает в первые 250 мс после запроса — окно дефекта 1).
	auto SwitchAfterSend = [S, Add](const FString& Name, TFunction<void(AAsdaPlayerController*)> Do)
	{
		Add(Name, 0.f, 2.5f, [S]() { S->bFlag = false; S->Mark = S->Caps.Num(); }, [S, Do]()
		{
			AAsdaPlayerController* PC = S->PC();
			if (!PC || S->Count(4006, S->Mark) == 0) { return false; }
			Do(PC);
			S->SwitchS = FPlatformTime::Seconds(); S->ProbePos = S->Pos(); S->ProbeF = 0.f; S->Mark = S->Caps.Num(); S->bMovedAfter = false;
			return true;
		}, []() {});
	};
	// Опрос «наибольшее удаление от точки смены» (ProbeF) и «шёл ли» после смены.
	auto TrackAfter = [S]() -> bool
	{
		S->ProbeF = FMath::Max(S->ProbeF, (float)FVector2D::Distance(S->Pos(), S->ProbePos));
		if (AAsdaPlayerCharacter* C = S->Ch()) { if (C->IsMoving()) { S->bMovedAfter = true; } }
		return true;
	};

	for (const int32 Fx : { 1, 0 })
	{
		Add(FString::Printf(TEXT("фаза исправлений 22.10 = %d"), Fx), 0.f, 1.f, [S, Fx]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KbDirPending"), Fx);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KbIntentStop"), Fx);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalFlushKeys"), Fx);
			UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-КЛАВИШИ фаза исправлений 22.10 = %d (%s)"), Fx,
				Fx ? TEXT("исправлено") : TEXT("как до правки — воспроизведение"));
		}, []() { return true; }, []() {});

		// KB21. W → A без ПКМ (A = разворот камеры, вектор 0): смена сразу после 4006. Исправлено — покой сразу + 4008, грант
		//   прежнего направления не уводит; до правки — персонаж бежит вперёд по старому гранту (жалоба игроков).
		PauseAll(TEXT("пауза KB21"));
		RunW(TEXT("KB21a бег W"), false);
		SwitchAfterSend(TEXT("KB21b W → A"), [](AAsdaPlayerController* PC) { KeyEv(PC, EKeys::A, true); KeyEv(PC, EKeys::W, false); });
		Add(TEXT("KB21c после смены 1.2 с"), 1.2f, 1.5f, []() {}, TrackAfter, [S, Fx]()
		{
			const int32 N8 = S->Count(4008, S->Mark);
			if (Fx) { S->Expect(S->ProbeF < 120.f && N8 >= 1, FString::Printf(TEXT("W→A: ушёл от точки смены на %.0f ед. (≤120), 4008 %d — остановился сразу [ИСПРАВЛЕНИЕ]"), S->ProbeF, N8)); }
			else    { S->Expect(S->ProbeF > 200.f, FString::Printf(TEXT("до правки: после W→A бежал вперёд ещё %.0f ед. (>200) — дефект воспроизведён"), S->ProbeF)); }
			// ★ Ревизия В-1/М-6: после стопа 4006 «в свою точку» — не больше одного за 1.2 с и не раньше 900 мс (каденция эталона), 4008 — ровно один.
			if (Fx)
			{
				int32 NSelf = 0; double FirstSelf = -1.0;
				for (int32 i = S->Mark; i < S->Caps.Num(); ++i)
				{
					const FKCap& Cp = S->Caps[i];
					if (Cp.Op == 4006 && FVector2D::Distance(Cp.Cur, Cp.Dst) < 1.0) { if (NSelf++ == 0) { FirstSelf = Cp.AtS - S->SwitchS; } }
				}
				S->Expect(N8 == 1 && NSelf <= 1 && (NSelf == 0 || FirstSelf >= 0.9),
					FString::Printf(TEXT("после стопа: 4008 %d (=1), 4006 «в свою точку» %d (≤1), первый через %.0f мс (≥900) [ревизия В-1]"), N8, NSelf, FirstSelf * 1000.0));
			}
		});

		// KB22. W → D со стрейфом (ПКМ): смена сразу после 4006 — новое направление уходит не позже 250 мс (до правки ~1 с).
		PauseAll(TEXT("пауза KB22"));
		RunW(TEXT("KB22a бег W (ПКМ)"), true);
		SwitchAfterSend(TEXT("KB22b W → D (стрейф)"), [](AAsdaPlayerController* PC) { KeyEv(PC, EKeys::D, true); KeyEv(PC, EKeys::W, false); });
		Add(TEXT("KB22c первый 4006 стрейфа"), 1.4f, 1.6f, []() {}, []() { return true; }, [S, Fx, FirstDir]()
		{
			const FVector2D Right(-S->DirA.Y, S->DirA.X);
			const FKCap* A = FirstDir(Right); const FKCap* B = FirstDir(-Right);
			const FKCap* F = A ? A : B;
			const double Dt = F ? F->AtS - S->SwitchS : -1.0;
			if (Fx) { S->Expect(F && Dt < 0.40, FString::Printf(TEXT("стрейф ушёл через %.0f мс после смены (≤400) [ИСПРАВЛЕНИЕ]"), Dt * 1000.0)); }
			else    { S->Expect(F && Dt > 0.70, FString::Printf(TEXT("до правки: стрейф ушёл через %.0f мс (>700 — ждал повтор 1000 мс)"), Dt * 1000.0)); }
		});

		// KB23. То же при RTT 0.4 с: смена через 280 мс после 4006 (бит 2 ещё взведён). Исправлено — уходит сразу после ответа;
		//   до правки попытка при бите 2 съедала таймер — следующий запрос через 1 с.
		PauseAll(TEXT("пауза KB23"));
		RunW(TEXT("KB23a бег W (ПКМ)"), true);
		Add(TEXT("KB23b RTT 0.4 с, W → D при бите 2"), 0.f, 3.f, [S]() { S->RttS = 0.4; S->bFlag = false; S->Mark = S->Caps.Num(); }, [S]()
		{
			AAsdaPlayerController* PC = S->PC();
			if (!PC) { return false; }
			const double Now = FPlatformTime::Seconds();
			if (!S->bFlag) { if (S->Count(4006, S->Mark) > 0) { S->bFlag = true; S->SendS = Now; } return false; }
			if (Now - S->SendS < 0.28) { return false; }
			S->bProbe = S->N() && S->N()->IsMoveRequestInFlight();
			KeyEv(PC, EKeys::D, true); KeyEv(PC, EKeys::W, false);
			S->SwitchS = Now; S->Mark = S->Caps.Num();
			return true;
		}, []() {});
		Add(TEXT("KB23c первый 4006 стрейфа"), 1.6f, 1.8f, []() {}, []() { return true; }, [S, Fx, FirstDir]()
		{
			const FVector2D Right(-S->DirA.Y, S->DirA.X);
			const FKCap* A = FirstDir(Right); const FKCap* B = FirstDir(-Right);
			const FKCap* F = A ? A : B;
			const double Dt = F ? F->AtS - S->SwitchS : -1.0;
			S->Expect(S->bProbe, TEXT("в момент смены бит 2 взведён (4006 в пути) — условие сценария"));
			if (Fx) { S->Expect(F && Dt < 0.30, FString::Printf(TEXT("RTT 0.4: стрейф ушёл через %.0f мс после смены — сразу после ответа (≤300) [ИСПРАВЛЕНИЕ]"), Dt * 1000.0)); }
			else    { S->Expect(F && Dt > 0.70, FString::Printf(TEXT("до правки: стрейф ушёл через %.0f мс (>700 — попытка при бите 2 съела таймер)"), Dt * 1000.0)); }
			S->RttS = 0.08;
		});

		// KB24. W+S: исправлено — стоп сразу (у эталона 4006 не уходит, персонаж добегает грант); затем отпустить W — S уходит сразу.
		PauseAll(TEXT("пауза KB24"));
		RunW(TEXT("KB24a бег W"), false);
		SwitchAfterSend(TEXT("KB24b W → W+S"), [](AAsdaPlayerController* PC) { KeyEv(PC, EKeys::S, true); });
		Add(TEXT("KB24c W+S 0.6 с"), 0.6f, 0.8f, []() {}, TrackAfter, [S, Fx]()
		{
			const int32 N8 = S->Count(4008, S->Mark);
			if (Fx) { S->Expect(S->ProbeF < 120.f && N8 == 1, FString::Printf(TEXT("W+S: ушёл на %.0f ед. (≤120), 4008 %d (=1) — стоп [ИСПРАВЛЕНИЕ]"), S->ProbeF, N8)); }
			else    { S->Expect(S->ProbeF > 150.f, FString::Printf(TEXT("до правки: при W+S бежал вперёд ещё %.0f ед. (>150)"), S->ProbeF)); }
		});
		Add(TEXT("KB24d отпустить W (осталась S)"), 1.3f, 1.5f, [S]()
		{
			KeyEv(S->PC(), EKeys::W, false);
			S->SwitchS = FPlatformTime::Seconds(); S->Mark = S->Caps.Num();
		}, []() { return true; }, [S, Fx, FirstDir]()
		{
			const FKCap* F = FirstDir(-S->DirA);
			const double Dt = F ? F->AtS - S->SwitchS : -1.0;
			if (Fx) { S->Expect(F && Dt < 0.30, FString::Printf(TEXT("назад (S) ушёл через %.0f мс (≤300) [ИСПРАВЛЕНИЕ]"), Dt * 1000.0)); }
			else    { S->Expect(F && Dt > 0.70, FString::Printf(TEXT("до правки: назад ушёл через %.0f мс (>700)"), Dt * 1000.0)); }
		});

		// KB25. Модалка 1500 с зажатой W, W отпущена ПОД модалкой (путь игрока: события Slate → вьюпорт с гейтом IgnoreInput). Исправлено — после закрытия
		//   модалки W не «зажата», персонаж стоит; до правки — отпускание потеряно вьюпортом (IgnoreInput), персонаж бежит сам.
		PauseAll(TEXT("пауза KB25"));
		// Сброс клавиш — отдельным шагом: отпускание, сброс и нажатие в ОДНОМ кадре `UPlayerInput` сводит в «не зажата»
		//   (PressDelta = нажатий − отпусканий кадра, PlayerInput.cpp:1268), — нажатие W только в следующем шаге.
		Add(TEXT("KB250 сброс клавиш"), 0.15f, 0.5f, [S, Reset]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { ReleaseAll(PC); PC->FlushPressedKeys(); }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		}, []() { return true; }, []() {});
		Add(TEXT("KB25a W через Slate"), 1.0f, 1.5f, [S]()
		{
			S->Mark = S->Caps.Num();
			SlateKey(EKeys::W, true);
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(PC && PC->IsInputKeyDown(EKeys::W) && C && C->IsMoving(), FString::Printf(TEXT("метод: W событием Slate зажата и ведёт ход (фокус: %s, IgnoreInput=%d)"),
				*FocusType(), (GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput()) ? 1 : 0));
		});
		Add(TEXT("KB25b открыть модалку 1500"), 0.5f, 1.f, [S]()
		{
			if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ShowGameMessage(TEXT("самотест WASD 22.10: модалка с зажатой W")); }
		}, []() { return true; }, [S]()
		{
			const UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(WM && WM->IsAnyModalOpen() && C && !C->IsMoving(), FString::Printf(TEXT("модалка открыта, ход остановлен (эталон: под модалкой «не нажата»); фокус: %s, IgnoreInput=%d"),
				*FocusType(), (GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput()) ? 1 : 0));
		});
		Add(TEXT("KB25c отпустить W под модалкой"), 0.2f, 0.5f, []() { SlateKey(EKeys::W, false); }, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC();
			UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-КЛАВИШИ KB25: после отпускания под модалкой IsInputKeyDown(W)=%d"), PC && PC->IsInputKeyDown(EKeys::W) ? 1 : 0);
		});
		Add(TEXT("KB25d закрыть модалку"), 1.0f, 1.3f, [S]()
		{
			if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
			S->Mark = S->Caps.Num(); S->bMovedAfter = false; S->ProbePos = S->Pos(); S->ProbeF = 0.f;
		}, TrackAfter, [S, Fx]()
		{
			AAsdaPlayerController* PC = S->PC();
			const UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			const bool bHeld = PC && PC->IsInputKeyDown(EKeys::W);
			const int32 N6 = S->Count(4006, S->Mark);
			S->Expect(WM && !WM->IsAnyModalOpen(), TEXT("модалка закрыта"));
			if (Fx) { S->Expect(!bHeld && N6 == 0 && !S->bMovedAfter, FString::Printf(TEXT("после модалки W не зажата (%d), 4006 %d, шёл %d — сам не бежит [ИСПРАВЛЕНИЕ]"), bHeld, N6, S->bMovedAfter)); }
			else    { S->Expect(bHeld && (N6 >= 1 || S->bMovedAfter), FString::Printf(TEXT("до правки: W «зажата» (%d) без нажатия, 4006 %d, шёл %d — дефект воспроизведён"), bHeld, N6, S->bMovedAfter)); }
		});

		// KB26. Фокус в строку чата с зажатой W (потеря фокуса вьюпорта) — стоп; после закрытия чата сам не бежит. [без пары]
		PauseAll(TEXT("пауза KB26"));
		// Сброс клавиш — отдельным шагом: отпускание, сброс и нажатие в ОДНОМ кадре `UPlayerInput` сводит в «не зажата»
		//   (PressDelta = нажатий − отпусканий кадра, PlayerInput.cpp:1268), — нажатие W только в следующем шаге.
		Add(TEXT("KB260 сброс клавиш"), 0.15f, 0.5f, [S, Reset]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { ReleaseAll(PC); PC->FlushPressedKeys(); }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		}, []() { return true; }, []() {});
		Add(TEXT("KB26a W через Slate"), 1.0f, 1.5f, [S]()
		{
			S->Mark = S->Caps.Num();
			SlateKey(EKeys::W, true);
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(C && C->IsMoving(), TEXT("ход по W (Slate) идёт"));
		});
		Add(TEXT("KB26b фокус в чат"), 0.5f, 1.f, [S]()
		{
			if (UAsdaChatWindowSubsystem* Ch = S->N()->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>()) { Ch->OpenInputWithText(TEXT("")); }
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			const UAsdaChatWindowSubsystem* Cw = S->N()->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>();
			S->Expect(Cw && Cw->IsInputActive() && C && !C->IsMoving(), FString::Printf(TEXT("чат открыт, ход остановлен; W зажата=%d, фокус: %s"),
				PC && PC->IsInputKeyDown(EKeys::W) ? 1 : 0, *FocusType()));
		});
		Add(TEXT("KB26c отпустить W в чате, закрыть чат"), 0.8f, 1.2f, [S]()
		{
			SlateKey(EKeys::W, false);
			if (UAsdaChatWindowSubsystem* Ch = S->N()->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>()) { Ch->CloseInput(); }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
			S->Mark = S->Caps.Num(); S->bMovedAfter = false;
		}, TrackAfter, [S]()
		{
			AAsdaPlayerController* PC = S->PC();
			const int32 N6 = S->Count(4006, S->Mark);
			S->Expect(PC && !PC->IsInputKeyDown(EKeys::W) && N6 == 0 && !S->bMovedAfter,
				FString::Printf(TEXT("после чата W не зажата, 4006 %d, шёл %d — сам не бежит"), N6, S->bMovedAfter));
		});

		// KB27. Alt+Z (скрыть интерфейс) с зажатой W: ход не теряется, отпускание W доходит — стоп. [без пары]
		PauseAll(TEXT("пауза KB27"));
		// Сброс клавиш — отдельным шагом: отпускание, сброс и нажатие в ОДНОМ кадре `UPlayerInput` сводит в «не зажата»
		//   (PressDelta = нажатий − отпусканий кадра, PlayerInput.cpp:1268), — нажатие W только в следующем шаге.
		Add(TEXT("KB270 сброс клавиш"), 0.15f, 0.5f, [S, Reset]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { ReleaseAll(PC); PC->FlushPressedKeys(); }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		}, []() { return true; }, []() {});
		Add(TEXT("KB27a W через Slate"), 1.0f, 1.5f, [S]()
		{
			S->Mark = S->Caps.Num();
			SlateKey(EKeys::W, true);
		}, []() { return true; }, []() {});
		Add(TEXT("KB27b Alt+Z"), 0.6f, 1.f, []() { SlateKey(EKeys::Z, true, true); SlateKey(EKeys::Z, false, true); }, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			const UAsdaUiVisibilitySubsystem* Uv = S->N()->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>();
			S->Expect(Uv && !Uv->IsUiShown() && PC && PC->IsInputKeyDown(EKeys::W) && C && C->IsMoving(),
				FString::Printf(TEXT("Alt+Z: интерфейс скрыт %d, W зажата %d, ход идёт %d"), Uv && !Uv->IsUiShown(), PC && PC->IsInputKeyDown(EKeys::W), C && C->IsMoving()));
		});
		Add(TEXT("KB27c отпустить W"), 0.4f, 0.8f, []() { SlateKey(EKeys::W, false); }, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(PC && !PC->IsInputKeyDown(EKeys::W) && C && !C->IsMoving(), TEXT("при скрытом интерфейсе отпускание W дошло — стоп"));
		});
		Add(TEXT("KB27d Alt+Z обратно"), 0.4f, 0.8f, []() { SlateKey(EKeys::Z, true, true); SlateKey(EKeys::Z, false, true); }, []() { return true; }, [S]()
		{
			const UAsdaUiVisibilitySubsystem* Uv = S->N()->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>();
			S->Expect(Uv && Uv->IsUiShown(), TEXT("Alt+Z: интерфейс снова показан"));
			if (Uv && !Uv->IsUiShown()) { const_cast<UAsdaUiVisibilitySubsystem*>(Uv)->SetUiShown(true, TEXT("самотест клавиш: возврат")); }
		});
		// ★ Ревизия В-1/М-1/М-2/М-6 (REVIEW.md): KB28–KB31.
		// KB28. Отпустил W, держа A, через 0.3 с снова W (RTT 0.3 с). Исправлено: 4006 «в свою точку» сразу после стопа не уходит
		//   (бит 2 не взведён), новое W уходит сразу; до правки персонаж ещё бежит по старому гранту — W ждёт повтор ~1 с.
		PauseAll(TEXT("пауза KB28"));
		RunW(TEXT("KB28a бег W"), false);
		SwitchAfterSend(TEXT("KB28b W → A"), [S](AAsdaPlayerController* PC) { S->RttS = 0.3; KeyEv(PC, EKeys::A, true); KeyEv(PC, EKeys::W, false); });
		Add(TEXT("KB28c держим A 0.3 с"), 0.3f, 0.5f, []() {}, []() { return true; }, [S, Fx]()
		{
			int32 NSelf = 0;
			for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4006 && FVector2D::Distance(S->Caps[i].Cur, S->Caps[i].Dst) < 1.0) { ++NSelf; } }
			if (Fx) { S->Expect(NSelf == 0, FString::Printf(TEXT("после стопа 4006 «в свою точку» за 0.3 с: %d (ждали 0 — каденция 1000 мс от стопа) [ревизия В-1]"), NSelf)); }
		});
		Add(TEXT("KB28d снова W"), 1.4f, 1.6f, [S]()
		{
			KeyEv(S->PC(), EKeys::W, true);
			S->SwitchS = FPlatformTime::Seconds(); S->Mark = S->Caps.Num();
		}, []() { return true; }, [S, Fx]()
		{
			// A ещё держится — камера повёрнута разворотом A/D, поэтому ищем первый 4006 «не в свою точку» (любой курс).
			const FKCap* F = nullptr;
			for (int32 i = S->Mark; i < S->Caps.Num() && !F; ++i) { if (S->Caps[i].Op == 4006 && FVector2D::Distance(S->Caps[i].Cur, S->Caps[i].Dst) > 100.0) { F = &S->Caps[i]; } }
			const double Dt = F ? F->AtS - S->SwitchS : -1.0;
			// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: нажатие видит следующий тик контроллера, 4006 уходит тиком позже (правило
			//   10 мс) — срок это два кадра игры. Порог 100 мс писался под кадр 1/60 с; при кадре ≥ 50 мс выходило «105 мс».
			//   Порог — два кадра + 20 мс, не меньше прежних 100 мс и не больше 190 мс: контроль оригинала ниже ждёт > 200 мс
			//   (интервал 250 мс), различие с ним сохраняется.
			const double Lim = FMath::Clamp(2.0 * (double)(F ? F->FrameS : 0.f) + 0.02, 0.10, 0.19);
			if (Fx) { S->Expect(F && Dt < Lim, FString::Printf(TEXT("W после стопа ушёл через %.0f мс (≤%.0f, бит 2 не взведён лишним 4006) [ревизия В-1]"), Dt * 1000.0, Lim * 1000.0)); }
			// Контроль (ключи 0 = оригинал): персонаж ещё бежит по старому гранту (состояние «идёт» — правила 10 мс нет), A крутит камеру —
			//   новое W ждёт интервал 250 мс от метки нажатия.
			else    { S->Expect(F && Dt > 0.20, FString::Printf(TEXT("оригинал: W ушёл через %.0f мс (>200 — персонаж ещё бежал, интервал 250 мс)"), Dt * 1000.0)); }
			S->RttS = 0.08;
		});

		// KB29. ПКМ зажата, всплыла модалка: сброс клавиш отпускает ПКМ, но меню игрока это отпускание не открывает. [ревизия М-2]
		PauseAll(TEXT("пауза KB29"));
		Add(TEXT("KB29a ПКМ зажата"), 0.3f, 0.6f, [S, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); if (!PC) { return; }
			KeyEv(PC, EKeys::RightMouseButton, true);
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); if (!PC) { return; }
			if (!PC->bRMBDown)
			{
				UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-КЛАВИШИ KB29: курсор прогона над окном — зажим ПКМ взведён вручную (метод)"));
				PC->bRMBDown = true;
			}
			S->ProbeF = (float)PC->RmbFlushReleaseSuppressed;
		});
		Add(TEXT("KB29b модалка 1500 при зажатой ПКМ"), 0.4f, 0.8f, [S]()
		{
			if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ShowGameMessage(TEXT("самотест WASD 22.10: модалка с зажатой ПКМ")); }
		}, []() { return true; }, [S, Fx]()
		{
			AAsdaPlayerController* PC = S->PC(); if (!PC) { return; }
			const int32 Supp = PC->RmbFlushReleaseSuppressed - (int32)S->ProbeF;
			if (Fx) { S->Expect(Supp == 1 && !PC->bRMBDown && !PC->IsInputKeyDown(EKeys::RightMouseButton),
				FString::Printf(TEXT("сброс на модалке отпустил ПКМ (орбита снята %d), отпускание погашено для меню: %d [ревизия М-2]"), !PC->bRMBDown, Supp)); }
			else    { S->Expect(Supp == 0 && PC->IsInputKeyDown(EKeys::RightMouseButton),
				FString::Printf(TEXT("ключ 0: сброса нет — ПКМ «зажата» под модалкой (%d), погашено %d (контроль)"), PC->IsInputKeyDown(EKeys::RightMouseButton), Supp)); }
		});
		Add(TEXT("KB29c закрыть модалку, отпустить ПКМ"), 0.3f, 0.6f, [S]()
		{
			if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
			if (AAsdaPlayerController* PC = S->PC()) { KeyEv(PC, EKeys::RightMouseButton, false); }
		}, []() { return true; }, [S]()
		{
			const UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			S->Expect(WM && !WM->IsAnyModalOpen(), TEXT("модалка закрыта"));
		});

		// KB30. Alt+Z при открытой модалке с зажатой W: Alt+Z отклонён (интерфейс не прячется), W отпущена под модалкой не залипает.
		PauseAll(TEXT("пауза KB30"));
		Add(TEXT("KB300 сброс клавиш"), 0.15f, 0.5f, [S, Reset]()
		{
			Reset();
			if (AAsdaPlayerController* PC = S->PC()) { ReleaseAll(PC); PC->FlushPressedKeys(); }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		}, []() { return true; }, []() {});
		Add(TEXT("KB30a W через Slate"), 0.8f, 1.2f, []() { SlateKey(EKeys::W, true); }, []() { return true; }, []() {});
		Add(TEXT("KB30b модалка 1500"), 0.4f, 0.8f, [S]()
		{
			if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ShowGameMessage(TEXT("самотест WASD 22.10: Alt+Z под модалкой")); }
		}, []() { return true; }, []() {});
		Add(TEXT("KB30c Alt+Z под модалкой, отпустить W"), 0.4f, 0.8f, []() { SlateKey(EKeys::Z, true, true); SlateKey(EKeys::Z, false, true); SlateKey(EKeys::W, false); },
			[]() { return true; }, [S]()
		{
			const UAsdaUiVisibilitySubsystem* Uv = S->N()->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>();
			S->Expect(Uv && Uv->IsUiShown(), TEXT("Alt+Z при модалке отклонён — интерфейс показан (гейт CWindowMgr+0x200, как у эталона)"));
		});
		Add(TEXT("KB30d закрыть модалку"), 0.8f, 1.2f, [S]()
		{
			if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
			S->Mark = S->Caps.Num(); S->bMovedAfter = false;
		}, TrackAfter, [S, Fx]()
		{
			AAsdaPlayerController* PC = S->PC();
			const bool bHeld = PC && PC->IsInputKeyDown(EKeys::W);
			const int32 N6 = S->Count(4006, S->Mark);
			if (Fx) { S->Expect(!bHeld && N6 == 0 && !S->bMovedAfter, FString::Printf(TEXT("после модалки (Alt+Z был под ней) W не зажата %d, 4006 %d, шёл %d [ИСПРАВЛЕНИЕ]"), bHeld, N6, S->bMovedAfter)); }
			else    { S->Expect(bHeld, FString::Printf(TEXT("до правки: W «зажата» (%d) после модалки — дефект воспроизведён"), bHeld)); }
		});

		// KB31. Потолок частоты 4006: W + ПКМ, рыскание камеры дрожит каждый опрос (вектор меняется постоянно) 3 с —
		//   не чаще раза в 250 мс (потолок эталона), при RTT 0.08 и 0.3 с. [ревизия М-1]
		for (const double Rtt : { 0.08, 0.3 })
		{
			PauseAll(FString::Printf(TEXT("пауза KB31 RTT %.2f"), Rtt));
			RunW(FString::Printf(TEXT("KB31a бег W (ПКМ) RTT %.2f"), Rtt), true);
			Add(FString::Printf(TEXT("KB31b дрожание камеры 3 с RTT %.2f"), Rtt), 3.0f, 3.3f, [S, Rtt]()
			{
				S->RttS = Rtt; S->Mark = S->Caps.Num(); S->bFlag = false;
			}, [S]()
			{
				if (AAsdaPlayerController* PC = S->PC()) { PC->CamYawDeg = S->YawA + (S->bFlag ? 0.5f : -0.5f); S->bFlag = !S->bFlag; }
				return true;
			}, [S, Rtt]()
			{
				TArray<double> Ts; for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4006) { Ts.Add(S->Caps[i].AtS); } }
				double MinGap = 99.0; for (int32 i = 1; i < Ts.Num(); ++i) { MinGap = FMath::Min(MinGap, Ts[i] - Ts[i - 1]); }
				S->Expect(Ts.Num() >= 2 && Ts.Num() <= 13 && MinGap >= 0.245,
					FString::Printf(TEXT("RTT %.2f: 4006 за 3 с — %d шт. (≤13), наименьший шаг %.0f мс (≥245) — потолок 4/с [ревизия М-1]"), Rtt, Ts.Num(), MinGap * 1000.0));
				S->RttS = 0.08;
				if (AAsdaPlayerController* PC = S->PC()) { PC->CamYawDeg = S->YawA; }
			});
		}
	}
	// ═══════════ ★ 22.10.2026 — «СТАН»: жёсткая остановка (`asda.Move.StunHardStop`, разбор — `StunHardStopTick`) ═══════════
	//   «Сервер» повторяет `ProcessState_SKILL_STATE_FREEZE_4` (оглушение, id 3): на наложении снимает игрока со стека хода МОЛЧА
	//   (`bSrvMoving = false` в точке сервера), под состоянием `CharMove` отвечает result 6 (`sub_484D20`). Пара: St=1 — исправлено,
	//   St=0 — как оригинал/прежний порт (воспроизведение: бег по старому гранту, после снятия — result 5 и снап).
	auto Stun = [S](bool bOn)
	{
		UAsdaEffectsSubsystem* Fx = S->N() ? S->N()->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		if (!Fx) { return; }
		if (bOn)
		{
			S->SrvIntegrate(); if (S->bSrvMoving) { S->bSrvStunStopped = true; } S->bSrvMoving = false; S->bSrvStun = true;   // `Return_UserMoveStack` без пакета
			Fx->AddOrUpdateSkill(Fx->SelfUserIdx(), 3, 0, 0, 4, 600, 0);
		}
		else { S->bSrvStun = false; Fx->RemoveBySkillState(Fx->SelfUserIdx(), 3, 0); }
	};
	for (const int32 St : { 1, 0 })
	{
		Add(FString::Printf(TEXT("фаза стана 22.10 = %d"), St), 0.f, 1.f, [S, St]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KbDirPending"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KbIntentStop"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalFlushKeys"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.StunHardStop"), St);
		}, []() { return true; }, []() {});

		// KB32. Ход клавишами: стан на бегу → стоп на месте, 4008; под станом W не двигает и 4006 нет; снятие — ход сразу, без снапа.
		PauseAll(TEXT("пауза KB32"));
		RunW(TEXT("KB32a бег W"), false);
		Add(TEXT("KB32b стан на бегу"), 1.0f, 1.3f, [S, Stun]()
		{
			S->N5 = 0; S->NRev = 0;
			Stun(true);
			S->SwitchS = FPlatformTime::Seconds(); S->ProbePos = S->Pos(); S->ProbeF = 0.f; S->Mark = S->Caps.Num(); S->bMovedAfter = false;
		}, TrackAfter, [S, St]()
		{
			const int32 N6 = S->Count(4006, S->Mark), N8 = S->Count(4008, S->Mark);
			AAsdaPlayerCharacter* C = S->Ch();
			if (St) { S->Expect(S->ProbeF < 60.f && N8 == 1 && N6 == 0 && C && !C->IsMoving(),
				FString::Printf(TEXT("стан: ушёл на %.0f ед. (≤60), 4008 %d, 4006 под станом %d (=0), стоит %d [ИСПРАВЛЕНИЕ]"), S->ProbeF, N8, N6, C && !C->IsMoving())); }
			else    { S->Expect(S->ProbeF > 150.f, FString::Printf(TEXT("ключ 0: под станом бежал ещё %.0f ед. (>150) — как оригинал"), S->ProbeF)); }
		});
		Add(TEXT("KB32c снять стан (W держится)"), 1.0f, 1.3f, [S, Stun]()
		{
			Stun(false);
			S->SwitchS = FPlatformTime::Seconds(); S->Mark = S->Caps.Num();
		}, []() { return true; }, [S, St]()
		{
			const FKCap* F = nullptr;
			for (int32 i = S->Mark; i < S->Caps.Num() && !F; ++i) { if (S->Caps[i].Op == 4006 && FVector2D::Distance(S->Caps[i].Cur, S->Caps[i].Dst) > 100.0) { F = &S->Caps[i]; } }
			const double Dt = F ? F->AtS - S->SwitchS : -1.0;
			AAsdaPlayerCharacter* C = S->Ch();
			if (St) { S->Expect(F && Dt < 0.15 && S->N5 == 0 && S->NRev == 0 && C && C->IsMoving(),
				FString::Printf(TEXT("после снятия: 4006 через %.0f мс (≤150), result 5 — %d, 4009 Revision — %d (=0, без снапа), идёт %d [ИСПРАВЛЕНИЕ]"), Dt * 1000.0, S->N5, S->NRev, C && C->IsMoving())); }
			else    { S->Expect(S->N5 + S->NRev >= 1, FString::Printf(TEXT("ключ 0: откат к точке сервера: result 5 — %d, 4009 Revision — %d (≥1 всего): сервер стоял в точке стана — дефект воспроизведён"), S->N5, S->NRev)); }
		});

		// KB33. Опоздавший грант: стан наложен, пока 4006 в пути (RTT 0.3 с) — грант не исполняется. ★ Ревизия стана (мелочь 5): для id 3
		//   порядок модельный (на живом грант запроса до стана приходит раньше 5272 — один сокет); живой случай — id 12 (KB33-12).
		PauseAll(TEXT("пауза KB33"));
		Add(TEXT("KB330 сброс"), 0.2f, 0.5f, [S, Reset]() { Reset(); S->RttS = 0.3; }, []() { return true; }, []() {});
		Add(TEXT("KB33a W и стан до гранта"), 0.f, 1.5f, [S]() { S->Mark = S->Caps.Num(); KeyEv(S->PC(), EKeys::W, true); }, [S, Stun]()
		{
			if (S->Count(4006, S->Mark) == 0) { return false; }
			Stun(true);
			S->bProbe = S->N() && S->N()->IsMoveRequestInFlight();
			S->SwitchS = FPlatformTime::Seconds(); S->ProbePos = S->Pos(); S->ProbeF = 0.f; S->bMovedAfter = false;
			return true;
		}, []() {});
		Add(TEXT("KB33b грант приходит под станом"), 0.9f, 1.2f, []() {}, TrackAfter, [S, St]()
		{
			S->Expect(S->bProbe, TEXT("в момент стана 4006 в пути — условие сценария"));
			if (St) { S->Expect(S->ProbeF < 30.f && !S->bMovedAfter, FString::Printf(TEXT("опоздавший грант не исполнен: ушёл %.0f ед., шёл %d [ИСПРАВЛЕНИЕ]"), S->ProbeF, S->bMovedAfter)); }
			// ★ Реалистичный «сервер» (приём через ½RTT) видит 4006 уже под станом и отказывает (6) — гранта нет при обоих ключах; живой
			//   «опоздавший» грант — только id 12 (KB33-12).
			else    { S->Expect(S->ProbeF < 30.f, FString::Printf(TEXT("ключ 0: сервер принял 4006 уже под станом — отказ 6, гранта нет (%.0f ед.)"), S->ProbeF)); }
			S->RttS = 0.08;
		});
		Add(TEXT("KB33c снять стан"), 0.3f, 0.6f, [S, Stun]() { Stun(false); KeyEv(S->PC(), EKeys::W, false); }, []() { return true; }, []() {});

		// KB34. Ход кликом: стан на ходу → стоп, маршрут снят; клик под станом — без хода (плашка 1019); после снятия клик ведёт.
		PauseAll(TEXT("пауза KB34"));
		Add(TEXT("KB34a клик по земле"), 0.8f, 1.2f, [S, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
			const FVector2D Ue = AsdaCoord::GameToUe2D(S->Home + S->DirA * 1500.f);
			PC->LastMoveSendTime = 0.0;
			PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(C && C->IsMoving(), TEXT("ход по клику идёт"));
		});
		Add(TEXT("KB34b стан на ходу кликом"), 0.8f, 1.1f, [S, Stun]()
		{
			Stun(true);
			S->SwitchS = FPlatformTime::Seconds(); S->ProbePos = S->Pos(); S->ProbeF = 0.f; S->Mark = S->Caps.Num();
		}, TrackAfter, [S, St]()
		{
			AAsdaPlayerController* PC = S->PC();
			const int32 N8 = S->Count(4008, S->Mark);
			if (St) { S->Expect(S->ProbeF < 60.f && N8 == 1 && PC && !PC->HasRoute(),
				FString::Printf(TEXT("клик + стан: ушёл %.0f ед., 4008 %d, маршрут снят %d [ИСПРАВЛЕНИЕ]"), S->ProbeF, N8, PC && !PC->HasRoute())); }
			else    { S->Expect(S->ProbeF > 100.f, FString::Printf(TEXT("ключ 0: под станом шёл по клику ещё %.0f ед."), S->ProbeF)); }
		});
		Add(TEXT("KB34c клик под станом"), 0.5f, 0.8f, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
			S->Mark = S->Caps.Num(); S->ProbePos = S->Pos(); S->ProbeF = 0.f;
			const FVector2D Ue = AsdaCoord::GameToUe2D(S->Pos() - S->DirA * 600.f);
			PC->LastMoveSendTime = 0.0;
			PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
		}, TrackAfter, [S, St]()
		{
			const int32 N6 = S->Count(4006, S->Mark);
			if (St) { S->Expect(N6 == 0 && S->ProbeF < 5.f, FString::Printf(TEXT("клик под станом: 4006 %d, сдвиг %.0f — хода нет (эталон: плашка 1019)"), N6, S->ProbeF)); }
		});
		Add(TEXT("KB34d снять стан, клик"), 0.6f, 1.f, [S, Stun]()
		{
			Stun(false);
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
			S->Mark = S->Caps.Num(); S->N5 = 0;
			const FVector2D Ue = AsdaCoord::GameToUe2D(S->Pos() - S->DirA * 600.f);
			PC->LastMoveSendTime = 0.0;
			PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
		}, []() { return true; }, [S, St]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			if (St) { S->Expect(S->Count(4006, S->Mark) >= 1 && S->N5 == 0 && C && C->IsMoving(),
				FString::Printf(TEXT("после снятия клик ведёт: 4006 %d, result 5 %d (=0), идёт %d"), S->Count(4006, S->Mark), S->N5, C && C->IsMoving())); }
			if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка")); }
		});

		// KB35. Автопуть: стан на ходу — автопуть ждёт (заявка жива дольше 5 попыток «не сдвинулся»), после снятия ведёт дальше.
		PauseAll(TEXT("пауза KB35"));
		Add(TEXT("KB35a автопуть"), 1.2f, 1.6f, [S, Reset]()
		{
			Reset();
			UGameInstance* G = S->N()->GetGameInstance();
			UAsdaMinimapSubsystem* Mm = G->GetSubsystem<UAsdaMinimapSubsystem>();
			const UAsdaLoadManagerSubsystem* Lm = G->GetSubsystem<UAsdaLoadManagerSubsystem>();
			const FVector2D T = S->Home + S->DirA * 3000.f;
			S->bFlag = Mm && Lm && Mm->BeginMapTravel(Lm->CurrentMapId, FMath::FloorToInt(T.X / 100.f), FMath::FloorToInt(T.Y / 100.f));
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(S->bFlag && C && C->IsMoving(), FString::Printf(TEXT("автопуть начат %d и ведёт %d"), S->bFlag, C && C->IsMoving()));
		});
		Add(TEXT("KB35b стан на автопути 3.2 с"), 3.2f, 3.5f, [S, Stun]()
		{
			Stun(true);
			S->SwitchS = FPlatformTime::Seconds(); S->ProbePos = S->Pos(); S->ProbeF = 0.f; S->Mark = S->Caps.Num();
		}, TrackAfter, [S, St]()
		{
			const UAsdaMinimapSubsystem* Mm = S->N()->GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>();
			const int32 N6 = S->Count(4006, S->Mark);
			if (St) { S->Expect(S->ProbeF < 60.f && N6 == 0 && Mm && Mm->IsTravelRequested(),
				FString::Printf(TEXT("автопуть под станом: ушёл %.0f ед., 4006 %d (=0), заявка жива %d (ждёт) [ИСПРАВЛЕНИЕ]"), S->ProbeF, N6, Mm && Mm->IsTravelRequested())); }
			else    { S->Expect(S->ProbeF > 100.f, FString::Printf(TEXT("ключ 0: под станом автопуть вёл ещё %.0f ед."), S->ProbeF)); }
		});
		Add(TEXT("KB35c снять стан"), 1.5f, 1.8f, [S, Stun]() { Stun(false); S->Mark = S->Caps.Num(); S->N5 = 0; }, []() { return true; }, [S, St]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			UAsdaMinimapSubsystem* Mm = S->N()->GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>();
			if (St) { S->Expect(S->Count(4006, S->Mark) >= 1 && S->N5 == 0 && C && C->IsMoving(),
				FString::Printf(TEXT("после снятия автопуть ведёт: 4006 %d, result 5 %d (=0), идёт %d"), S->Count(4006, S->Mark), S->N5, C && C->IsMoving())); }
			if (Mm) { Mm->CancelTravelRequest(); }
			if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка")); }
		});
	}
	// ═══════════ ★ 22.10.2026 — ПРАВКИ РЕВИЗИИ СТАНА (п. 6) ═══════════
	{
		Add(TEXT("фаза ревизии стана"), 0.f, 1.f, [S]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Move.StunHardStop"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.LocalStart"), 0);
		}, []() { return true; }, []() {});
		auto StunId = [S](int32 Id, bool bOn, int32 RemainSec = 600)
		{
			UAsdaEffectsSubsystem* Fx = S->N() ? S->N()->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			if (!Fx) { return; }
			// KindByte 4 — «постоянный» (не тикает, `TickDecrement`); с таймером — 0, остаток убывает раз в секунду.
			if (bOn) { Fx->AddOrUpdateSkill(Fx->SelfUserIdx(), Id, 0, 0, RemainSec >= 600 ? 4 : 0, RemainSec, 0); }
			else     { Fx->RemoveBySkillState(Fx->SelfUserIdx(), Id, 0); }
		};
		auto ClearIds = [StunId]() { for (const int32 Id : { 3, 8, 12, 14 }) { StunId(Id, false); } };
		// KB36. Стан стоящего: пакетов нет.
		PauseAll(TEXT("пауза KB36"));
		Add(TEXT("KB36 стан стоящего"), 0.5f, 0.8f, [S, Reset, StunId]() { Reset(); S->Mark = S->Caps.Num(); StunId(3, true); }, []() { return true; }, [S, ClearIds]()
		{
			S->Expect(S->Count(4008, S->Mark) == 0 && S->Count(4006, S->Mark) == 0,
				FString::Printf(TEXT("стан стоящего: 4008 %d, 4006 %d (ждали 0/0)"), S->Count(4008, S->Mark), S->Count(4006, S->Mark)));
			ClearIds();
		});
		// KB37. Две стопки (оглушение 3 + сон 8): снята одна — стоим; снята вторая — W ведёт.
		PauseAll(TEXT("пауза KB37"));
		Add(TEXT("KB37a две стопки, W"), 0.5f, 0.8f, [S, Reset, StunId]() { Reset(); StunId(3, true); StunId(8, true); KeyEv(S->PC(), EKeys::W, true); S->Mark = S->Caps.Num(); },
			[]() { return true; }, []() {});
		Add(TEXT("KB37b снята одна"), 0.6f, 0.9f, [S, StunId]() { StunId(3, false); S->Mark = S->Caps.Num(); S->ProbePos = S->Pos(); }, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC();
			S->Expect(PC && PC->IsStunHardStopped() && S->Count(4006, S->Mark) == 0 && FVector2D::Distance(S->Pos(), S->ProbePos) < 2.f,
				FString::Printf(TEXT("снята одна стопка из двух: обездвижен %d, 4006 %d — стоит"), PC && PC->IsStunHardStopped(), S->Count(4006, S->Mark)));
		});
		Add(TEXT("KB37c снята вторая"), 0.6f, 0.9f, [S, StunId]() { StunId(8, false); S->Mark = S->Caps.Num(); }, []() { return true; }, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(S->Count(4006, S->Mark) >= 1 && C && C->IsMoving(), FString::Printf(TEXT("снята вторая: 4006 %d, идёт %d"), S->Count(4006, S->Mark), C && C->IsMoving()));
			KeyEv(S->PC(), EKeys::W, false);
		});
		// KB38. Снятие по таймеру (1 с) и по 5306 (сброс всех эффектов своего).
		PauseAll(TEXT("пауза KB38"));
		Add(TEXT("KB38a стан на 1 с, W"), 0.4f, 0.6f, [S, Reset, StunId]() { Reset(); StunId(3, true, 1); KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC();
			S->Expect(PC && PC->IsStunHardStopped(), TEXT("стан с таймером 1 с наложен"));
		});
		// Остаток убывает раз в секунду (`TickDecrement`), снятие — при остатке < 0 (`UpdateExpired`): 1 с → снят к ~2 с + период проверки.
		Add(TEXT("KB38b истёк по таймеру"), 3.0f, 3.3f, [S]() { S->Mark = S->Caps.Num(); }, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(PC && !PC->IsStunHardStopped() && S->Count(4006, S->Mark) >= 1 && C && C->IsMoving(),
				FString::Printf(TEXT("по таймеру снят: обездвижен %d, 4006 %d, идёт %d"), PC && PC->IsStunHardStopped(), S->Count(4006, S->Mark), C && C->IsMoving()));
			KeyEv(S->PC(), EKeys::W, false);
		});
		Add(TEXT("KB38c стан, затем 5306"), 0.5f, 0.8f, [S, Reset, StunId]() { Reset(); StunId(3, true); }, []() { return true; }, [S]()
		{
			UAsdaNetSubsystem* N = S->N();
			UAsdaEffectsSubsystem* Fx = N ? N->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			if (N && Fx) { FAsdaEffectMsg M; M.UserIdx = Fx->SelfUserIdx(); M.Kind = EAsdaEffectMsgKind::ClearAll; M.Opcode = 5306; N->OnEffectMsg.Broadcast(M); }
			AAsdaPlayerController* PC = S->PC();
			S->Expect(PC && !PC->IsStunHardStopped(), TEXT("5306 (сброс всех эффектов своего) снял стан"));
		});
		// KB39. Id 12 (оглушение хранителя) и 14 (абсолютное): стоп на бегу, W не ведёт; id 12 — опоздавший грант (сервер под 12 ход не отклоняет).
		for (const int32 Id : { 12, 14 })
		{
			PauseAll(FString::Printf(TEXT("пауза KB39-%d"), Id));
			RunW(FString::Printf(TEXT("KB39a-%d бег W"), Id), false);
			Add(FString::Printf(TEXT("KB39b-%d стан на бегу"), Id), 0.8f, 1.1f, [S, StunId, Id]()
			{
				StunId(Id, true); S->Mark = S->Caps.Num(); S->ProbePos = S->Pos(); S->ProbeF = 0.f;
			}, TrackAfter, [S, Id, ClearIds]()
			{
				S->Expect(S->ProbeF < 60.f && S->Count(4008, S->Mark) == 1 && S->Count(4006, S->Mark) == 0,
					FString::Printf(TEXT("id %d: ушёл %.0f ед., 4008 %d (=1), 4006 %d (=0)"), Id, S->ProbeF, S->Count(4008, S->Mark), S->Count(4006, S->Mark)));
				ClearIds(); KeyEv(S->PC(), EKeys::W, false);
			});
		}
		PauseAll(TEXT("пауза KB33-12"));
		Add(TEXT("KB33-12 сброс"), 0.2f, 0.5f, [S, Reset]() { Reset(); S->RttS = 0.3; }, []() { return true; }, []() {});
		Add(TEXT("KB33-12 W и id 12 до гранта"), 0.f, 1.5f, [S]() { S->Mark = S->Caps.Num(); KeyEv(S->PC(), EKeys::W, true); }, [S, StunId]()
		{
			if (S->Count(4006, S->Mark) == 0) { return false; }
			StunId(12, true);   // сервер под 12 4006 не отклоняет (`sub_484D20`) — грант придёт под станом
			S->ProbePos = S->Pos(); S->ProbeF = 0.f; S->bMovedAfter = false;
			return true;
		}, []() {});
		Add(TEXT("KB33-12 грант под станом 12"), 0.9f, 1.2f, []() {}, TrackAfter, [S, ClearIds]()
		{
			S->Expect(S->ProbeF < 30.f && !S->bMovedAfter, FString::Printf(TEXT("грант под id 12 не исполнен: ушёл %.0f ед., шёл %d [ИСПРАВЛЕНИЕ]"), S->ProbeF, S->bMovedAfter));
			S->RttS = 0.08; ClearIds(); KeyEv(S->PC(), EKeys::W, false);
		});
		// KB39c. Смерть под станом: после «оживления» стан держится. Смена карты под станом: свой стан переносится, чужие списки сняты.
		PauseAll(TEXT("пауза KB39c"));
		Add(TEXT("KB39c смерть под станом"), 0.4f, 0.7f, [S, Reset, StunId]() { Reset(); StunId(3, true); if (S->N()) { S->N()->bSelfDead = true; } }, []() { return true; }, [S]()
		{
			if (S->N()) { S->N()->bSelfDead = false; }
		});
		Add(TEXT("KB39d после смерти стан держится"), 0.4f, 0.7f, [S]() { KeyEv(S->PC(), EKeys::W, true); S->Mark = S->Caps.Num(); }, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC();
			S->Expect(PC && PC->IsStunHardStopped() && S->Count(4006, S->Mark) == 0, TEXT("после смерти под станом: обездвижен, 4006 нет"));
			KeyEv(S->PC(), EKeys::W, false);
		});
		// ★ Ревизия локального старта ВАЖНО-1: номер меняется на номер соседа прошлой карты, перенос — по своей 4003.
		Add(TEXT("KB39e смена карты под станом"), 0.4f, 0.7f, [S]()
		{
			UAsdaEffectsSubsystem* Fx = S->N()->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>();
			if (!Fx) { return; }
			const int32 Self0 = Fx->SelfUserIdx(), Nb = Self0 + 7;
			Fx->AddOrUpdateSkill(Nb, 8, 0, 0, 4, 600, 0);   // сосед прошлой карты (номер Nb) под сном
			Fx->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			S->bProbe = Fx->GetList(Nb).Num() == 0;          // чужой список снят на смене карты
			S->N()->SelfUserNum = (uint16)Nb;                // новая 4003: свой номер = номер того соседа
			Fx->OnSelfSpawnCarry();
			S->ProbePos = FVector2D((double)Self0, (double)Nb);
		}, []() { return true; }, [S, ClearIds]()
		{
			UAsdaEffectsSubsystem* Fx = S->N()->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>();
			AAsdaPlayerController* PC = S->PC();
			const int32 Self0 = (int32)S->ProbePos.X, Nb = (int32)S->ProbePos.Y;
			const bool bOwn = Fx && Fx->FindByTypeSub(Nb, 3, 0) != nullptr;
			const bool bForeign = Fx && Fx->FindByTypeSub(Nb, 8, 0) != nullptr;
			const int32 NOld = Fx ? Fx->GetList(Self0).Num() : -1;
			S->Expect(S->bProbe && bOwn && !bForeign && NOld == 0 && PC && PC->IsStunHardStopped(),
				FString::Printf(TEXT("смена карты, номер %d → %d: чужой список снят %d, свой стан перенесён %d, чужой сон не унаследован %d, под старым номером %d записей, обездвижен %d [ИСПРАВЛЕНИЕ]"),
					Self0, Nb, S->bProbe, bOwn, !bForeign, NOld, PC && PC->IsStunHardStopped()));
			if (Fx) { Fx->ClearCharEffects(Nb); }
			S->N()->SelfUserNum = (uint16)Self0;
			ClearIds();
		});
		// KB34e. Плашка 1019 у клика под станом; KB29d — обычное отпускание ПКМ после сброса не гасится.
		PauseAll(TEXT("пауза KB34e"));
		Add(TEXT("KB34e плашка 1019"), 0.3f, 0.6f, [S, Reset, StunId, Grab]()
		{
			Reset(); StunId(3, true);
			Grab->Needle = TEXT("плашка 1019"); Grab->Hits = 0;
			if (GLog) { GLog->AddOutputDevice(&Grab.Get()); }
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
			const FVector2D Ue = AsdaCoord::GameToUe2D(S->Pos() + S->DirA * 600.f);
			PC->LastMoveSendTime = 0.0;
			PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
		}, []() { return true; }, [S, Grab, ClearIds]()
		{
			if (GLog) { GLog->Flush(); GLog->RemoveOutputDevice(&Grab.Get()); }
			int32 H = 0; { FScopeLock L(&Grab->Lock); H = Grab->Hits; }
			S->Expect(H >= 1, FString::Printf(TEXT("клик под станом: плашка 1019 показана (%d строк журнала ветки)"), H));
			ClearIds();
		});
		Add(TEXT("KB29d обычное ПКМ после сброса"), 0.3f, 0.6f, [S, Reset]()
		{
			Reset();
			AAsdaPlayerController* PC = S->PC(); if (!PC) { return; }
			S->ProbeF = (float)PC->RmbFlushReleaseSuppressed;
			KeyEv(PC, EKeys::RightMouseButton, true);
			if (!PC->bRMBDown) { PC->bRMBDown = true; }
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerController* PC = S->PC(); if (!PC) { return; }
			KeyEv(PC, EKeys::RightMouseButton, false);
			S->Expect(PC->RmbFlushReleaseSuppressed == (int32)S->ProbeF && !PC->bNextRmbReleaseSynthetic,
				FString::Printf(TEXT("обычное отпускание ПКМ (без сброса) не гасится: погашено %d → %d"), (int32)S->ProbeF, PC->RmbFlushReleaseSuppressed));
		});
	}

	// ═══════════ ★ 22.10.2026 — ЛОКАЛЬНЫЙ СТАРТ (`asda.Move.LocalStart`, разбор — AsdaPlayerKeyboardEtalon.cpp) ═══════════
	//   Пара: Ls=1 — трогается сразу; Ls=0 — как оригинал (по гранту). «Сервер» — тот же `CharMove`/`CharStop` с состоянием.
	for (const int32 Ls : { 1, 0 })
	{
		Add(FString::Printf(TEXT("фаза локального старта = %d"), Ls), 0.f, 1.f, [S, Ls]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Move.LocalStart"), Ls);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.StunHardStop"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KbDirPending"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.KbIntentStop"), 1);
		}, []() { return true; }, []() {});
		// KB40. Старт без ожидания при RTT 0 / 0.15 / 0.35 с; бег 1.5 с, отпускание, снова W — ни result 5, ни Revision.
		for (const double Rtt : { 0.0, 0.15, 0.35 })
		{
			PauseAll(FString::Printf(TEXT("пауза KB40 RTT %.2f"), Rtt));
			Add(FString::Printf(TEXT("KB40a W, RTT %.2f"), Rtt), 1.5f, 1.8f, [S, Reset, Rtt]()
			{
				Reset(); S->RttS = Rtt; S->bFlag = false; S->ProbeF = -1.f;
				S->SwitchS = FPlatformTime::Seconds();
				KeyEv(S->PC(), EKeys::W, true);
			}, [S]()
			{
				AAsdaPlayerCharacter* C = S->Ch();
				if (!S->bFlag && C && C->IsMoving() && FVector2D::Distance(S->Pos(), S->Home) > 1.f) { S->bFlag = true; S->ProbeF = (float)(FPlatformTime::Seconds() - S->SwitchS); }
				return true;
			}, [S, Ls, Rtt]()
			{
				// ★ 11.11.2026 (D4b): от нажатия до первого сдвига проходит не меньше двух-трёх кадров (ввод → решение → сдвиг), поэтому
				//   порог — три кадра, но не меньше 80 мс и не больше 120 мс: при 60 кадрах/с проверка прежняя, при 30 — 100 мс (в повторах
				//   под нагрузкой было 89–101 мс). Смысл тот же: ход начинается сразу, а не по гранту (самый короткий RTT сценария — 150 мс).
				const float StartTol = FMath::Clamp(3.f * (float)FApp::GetDeltaTime(), 0.08f, 0.12f);
				if (Ls) { S->Expect(S->bFlag && S->ProbeF < StartTol, FString::Printf(TEXT("RTT %.2f: тронулся через %.0f мс (≤%.0f, не ждёт гранта)"), Rtt, S->ProbeF * 1000.f, StartTol * 1000.f)); }
				else if (Rtt > 0.01) { S->Expect(S->bFlag && S->ProbeF >= Rtt * 0.85, FString::Printf(TEXT("ключ 0: тронулся через %.0f мс (≥ RTT %.0f — по гранту)"), S->ProbeF * 1000.f, Rtt * 1000.0)); }
				S->Expect(S->N5 == 0 && S->NRev == 0, FString::Printf(TEXT("RTT %.2f: бег 1.5 с — result 5 %d, Revision %d (ждали 0/0)"), Rtt, S->N5, S->NRev));
			});
			Add(FString::Printf(TEXT("KB40b отпустить и снова W, RTT %.2f"), Rtt), 0.8f, 1.1f, [S]() { KeyEv(S->PC(), EKeys::W, false); }, []() { return true; }, [S]() {});
			Add(FString::Printf(TEXT("KB40c снова W, RTT %.2f"), Rtt), 0.8f, 1.1f, [S]() { KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, [S, Rtt]()
			{
				const float D = (float)FVector2D::Distance(S->SrvPos, S->Pos());
				S->Expect(S->N5 == 0 && S->NRev == 0, FString::Printf(TEXT("RTT %.2f: после отпускания и нового W — result 5 %d, Revision %d (0/0), расхождение с сервером %.0f ед."), Rtt, S->N5, S->NRev, D));
				KeyEv(S->PC(), EKeys::W, false); S->RttS = 0.08;
			});
		}
		// KB41. Отказ 0 и 6 (сервер под станом без состояния у клиента): плавный возврат в точку сервера, без качелей.
		for (const int32 Res : { 0, 6 })
		{
			PauseAll(FString::Printf(TEXT("пауза KB41-%d"), Res));
			Add(FString::Printf(TEXT("KB41-%d отказ %d при W"), Res, Res), 1.2f, 1.5f, [S, Reset, Res]()
			{
				Reset(); S->RttS = 0.15;
				if (Res == 0) { S->bForceRefuse = true; } else { S->bSrvStun = true; }
				AAsdaPlayerController* PC = S->PC(); if (!PC) { return; }
				S->ProbeF = 0.f; S->Mark = PC->LocalStartCount; S->ProbePos = FVector2D((double)PC->LocalStartReturnCount, 0.0);
				KeyEv(PC, EKeys::W, true);
			}, [S]() { S->ProbeF = FMath::Max(S->ProbeF, (float)FVector2D::Distance(S->Pos(), S->Home)); return true; }, [S, Ls, Res]()
			{
				AAsdaPlayerController* PC = S->PC(); if (!PC) { return; }
				const int32 NStart = PC->LocalStartCount - S->Mark, NRet = PC->LocalStartReturnCount - (int32)S->ProbePos.X;
				const float End = (float)FVector2D::Distance(S->Pos(), S->Home);
				if (Ls) { S->Expect(NStart == 1 && NRet == 1 && End < 2.f && S->ProbeF > 5.f,
					FString::Printf(TEXT("отказ %d: локальных стартов %d (=1, без качелей), возвратов %d (=1), отошёл до %.0f ед., в конце от точки сервера %.1f ед."), Res, NStart, NRet, S->ProbeF, End)); }
				else    { S->Expect(S->ProbeF < 2.f, FString::Printf(TEXT("ключ 0: при отказе %d не двигался (%.1f ед.)"), Res, S->ProbeF)); }
				KeyEv(PC, EKeys::W, false); S->bForceRefuse = false; S->bSrvStun = false; S->RttS = 0.08;
			});
			Add(FString::Printf(TEXT("KB41-%d после отказа грант снимает ожидание"), Res), 0.8f, 1.1f, [S]() { KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, [S, Ls]()
			{
				AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
				if (Ls) { S->Expect(PC && !PC->bLocalStartSuppressed && C && C->IsMoving(), TEXT("сервер снова разрешает: грант принят, локальный старт снова доступен, идёт")); }
				KeyEv(S->PC(), EKeys::W, false);
			});
		}
		// KB42. result 5: сервер держит другую точку стоянки — снап к ней, затем ход; result 5 ровно один.
		PauseAll(TEXT("пауза KB42"));
		Add(TEXT("KB42 result 5"), 1.2f, 1.5f, [S, Reset]()
		{
			Reset(); S->SrvPos = S->Home + S->DirA * 50.f; S->SrvTile = AsdaCoord::GameToTile(S->SrvPos); S->N5 = 0;
			KeyEv(S->PC(), EKeys::W, true);
		}, []() { return true; }, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(S->N5 == 1 && C && C->IsMoving(), FString::Printf(TEXT("result 5 %d (=1): снап к точке сервера, затем ход %d"), S->N5, C && C->IsMoving()));
			KeyEv(S->PC(), EKeys::W, false);
		});
		// KB43. Тап W 50/120/200 мс при RTT 0.15: двинулся (ключ 1), встал; сервер согласен (точка совпадает, Revision 0), новое W — без result 5.
		for (const float Tap : { 0.05f, 0.12f, 0.2f })
		{
			PauseAll(FString::Printf(TEXT("пауза KB43 %.0f мс"), Tap * 1000.f));
			Add(FString::Printf(TEXT("KB43a тап %.0f мс"), Tap * 1000.f), Tap, Tap + 0.3f, [S, Reset]() { Reset(); S->RttS = 0.15; KeyEv(S->PC(), EKeys::W, true); }, []() { return true; },
				[S]() { KeyEv(S->PC(), EKeys::W, false); });
			Add(FString::Printf(TEXT("KB43b после тапа %.0f мс"), Tap * 1000.f), 0.7f, 1.f, []() {}, []() { return true; }, [S, Ls, Tap]()
			{
				AAsdaPlayerCharacter* C = S->Ch();
				const float Moved = (float)FVector2D::Distance(S->Pos(), S->Home);
				const float Srv = (float)FVector2D::Distance(S->Pos(), S->SrvPos);
				if (Ls) { S->Expect(Moved > 3.f, FString::Printf(TEXT("тап %.0f мс: двинулся на %.0f ед."), Tap * 1000.f, Moved)); }
				S->Expect(C && !C->IsMoving() && Srv < 1.f && S->NRev == 0 && S->N5 == 0,
					FString::Printf(TEXT("тап %.0f мс: стоит %d, расхождение с сервером %.2f ед. (<1), Revision %d, result 5 %d"), Tap * 1000.f, C && !C->IsMoving(), Srv, S->NRev, S->N5));
			});
			Add(FString::Printf(TEXT("KB43c после тапа %.0f мс снова W"), Tap * 1000.f), 0.5f, 0.8f, [S]() { KeyEv(S->PC(), EKeys::W, true); }, []() { return true; }, [S, Tap]()
			{
				S->Expect(S->N5 == 0, FString::Printf(TEXT("тап %.0f мс: новое W — result 5 %d (=0)"), Tap * 1000.f, S->N5));
				KeyEv(S->PC(), EKeys::W, false); S->RttS = 0.08;
			});
		}
		// KB44. Стена + 4009 сервера (P77): упор, затем 4009 в точку сервера чуть позади — снап (хвостом), новое W без result 5.
		PauseAll(TEXT("пауза KB44"));
		Add(TEXT("KB44a бег в стену"), 3.0f, 3.5f, [S, Reset]()
		{
			Reset();
			if (!S->bWallOk) { return; }
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC(); if (!C || !PC) { return; }
			C->NetSnap(S->WallHome); S->SrvPos = S->WallHome; S->SrvStart = S->WallHome; S->SrvTile = AsdaCoord::GameToTile(S->WallHome);
			const FVector2D Ue = AsdaCoord::GameDirToUe(S->WallDir);
			PC->CamYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Ue.Y, Ue.X));
			KeyEv(PC, EKeys::W, true);
		}, []() { return true; }, [S]()
		{
			KeyEv(S->PC(), EKeys::W, false);
		});
		Add(TEXT("KB44a2 пауза — 4008 отпускания дошёл до сервера"), 0.3f, 0.5f, []() {}, []() { return true; }, []() {});
		Add(TEXT("KB44b 4009 сервера (P77)"), 0.6f, 0.9f, [S]()
		{
			if (!S->bWallOk) { return; }
			const FVector2D P = S->Pos() - S->WallDir * 15.f;
			S->SrvIntegrate(); S->bSrvMoving = false; S->SrvPos = P; S->SrvTile = AsdaCoord::GameToTile(P);
			FInject In; In.AtS = FPlatformTime::Seconds() + 0.25; In.Op = 4009;   // после ответа 4009 на 4008 отпускания (RTT 0.08)
			In.Body = MoveBody(0, S->N() ? (int16)S->N()->SelfUserNum : 0, 0, P, P, S->SrvSpeed, -1);
			S->Queue.Add(MoveTemp(In));
			S->ProbePos = P; S->N5 = 0;
		}, []() { return true; }, [S]()
		{
			if (!S->bWallOk) { S->Expect(false, TEXT("стены нет — сценарий не выполнен")); return; }
			S->Expect(FVector2D::Distance(S->Pos(), S->ProbePos) < 1.f, FString::Printf(TEXT("4009: персонаж в точке сервера (%.1f ед.)"), FVector2D::Distance(S->Pos(), S->ProbePos)));
			const FVector2D Away = -S->WallDir;
			const FVector2D Ue = AsdaCoord::GameDirToUe(Away);
			if (AAsdaPlayerController* PC = S->PC()) { PC->CamYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Ue.Y, Ue.X)); KeyEv(PC, EKeys::W, true); }
		});
		Add(TEXT("KB44c ход от стены"), 0.6f, 0.9f, []() {}, []() { return true; }, [S]()
		{
			if (!S->bWallOk) { return; }
			S->Expect(S->N5 == 0, FString::Printf(TEXT("после 4009 ход от стены: result 5 %d (=0)"), S->N5));
			KeyEv(S->PC(), EKeys::W, false);
		});
		// KB45. Стан во время локального старта (4006 в пути, RTT 0.3): стоп, 4008, грант не исполнен, после снятия — ход без снапа.
		PauseAll(TEXT("пауза KB45"));
		Add(TEXT("KB45 сброс"), 0.2f, 0.5f, [S, Reset]() { Reset(); S->RttS = 0.3; }, []() { return true; }, []() {});
		Add(TEXT("KB45a W и стан до гранта"), 0.f, 1.5f, [S]() { S->Mark = S->Caps.Num(); KeyEv(S->PC(), EKeys::W, true); }, [S, Stun]()
		{
			if (S->Count(4006, S->Mark) == 0) { return false; }
			if (FPlatformTime::Seconds() - S->Caps.Last().AtS < 0.1) { return false; }   // локальный старт успел отойти
			Stun(true); S->N5 = 0; S->NRev = 0;
			S->ProbePos = S->Pos(); S->ProbeF = 0.f; S->bMovedAfter = false; S->Mark = S->Caps.Num();
			return true;
		}, []() {});
		Add(TEXT("KB45b под станом"), 0.9f, 1.2f, []() {}, TrackAfter, [S]()
		{
			S->Expect(S->ProbeF < 30.f && !S->bMovedAfter && S->Count(4008, S->Mark) == 1,
				FString::Printf(TEXT("стан во время старта: ушёл %.0f ед., шёл %d, 4008 %d (=1)"), S->ProbeF, S->bMovedAfter, S->Count(4008, S->Mark)));
		});
		Add(TEXT("KB45c снять стан"), 0.8f, 1.1f, [S, Stun]() { Stun(false); }, []() { return true; }, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(S->N5 == 0 && S->NRev == 0 && C && C->IsMoving(), FString::Printf(TEXT("после снятия: result 5 %d, Revision %d (0/0), идёт %d"), S->N5, S->NRev, C && C->IsMoving()));
			KeyEv(S->PC(), EKeys::W, false); S->RttS = 0.08;
		});
		// KB46. Клик по земле: старт сразу (ключ 1) / по гранту (ключ 0), RTT 0.25.
		PauseAll(TEXT("пауза KB46"));
		Add(TEXT("KB46 клик"), 0.6f, 0.9f, [S, Reset]()
		{
			Reset(); S->RttS = 0.25; S->bFlag = false; S->ProbeF = -1.f; S->SwitchS = FPlatformTime::Seconds();
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
			const FVector2D Ue = AsdaCoord::GameToUe2D(S->Home + S->DirA * 1500.f);
			PC->LastMoveSendTime = 0.0;
			PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
		}, [S]()
		{
			if (!S->bFlag && FVector2D::Distance(S->Pos(), S->Home) > 1.f) { S->bFlag = true; S->ProbeF = (float)(FPlatformTime::Seconds() - S->SwitchS); }
			return true;
		}, [S, Ls]()
		{
			const float ClickTol = FMath::Clamp(3.f * (float)FApp::GetDeltaTime(), 0.08f, 0.12f);   // ★ 11.11.2026 (D4b): три кадра, 80…120 мс — см. KB40a
			if (Ls) { S->Expect(S->bFlag && S->ProbeF < ClickTol, FString::Printf(TEXT("клик: тронулся через %.0f мс (≤%.0f)"), S->ProbeF * 1000.f, ClickTol * 1000.f)); }
			else    { S->Expect(S->bFlag && S->ProbeF >= 0.2f, FString::Printf(TEXT("ключ 0: клик — тронулся через %.0f мс (по гранту, ≥200)"), S->ProbeF * 1000.f)); }
			S->Expect(S->N5 == 0, FString::Printf(TEXT("клик: result 5 %d (=0)"), S->N5));
			if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка")); }
			S->RttS = 0.08;
		});
		// ═══ ★ Ревизия локального старта (ВАЖНО-1…5): покрытие при ключе 1 ═══
		if (Ls)
		{
			// Продвижение вдоль DirA и наибольший откат назад (снап «назад» — то, что видит игрок).
			auto TrackBack = [S]() -> bool
			{
				const float Pr = (float)FVector2D::DotProduct(S->Pos() - S->Home, S->DirA);
				S->ProbeF = FMath::Max(S->ProbeF, S->DriftMax - Pr);   // DriftMax здесь — наибольшее продвижение
				S->DriftMax = FMath::Max(S->DriftMax, Pr);
				return true;
			};
			// KB49. Повторное W быстрее RTT (тап 80 мс, снова через 60 мс, RTT 0.3): ответ на 4008 тапа не ставит назад.
			PauseAll(TEXT("пауза KB49"));
			Add(TEXT("KB49a тап W"), 0.08f, 0.3f, [S, Reset]() { Reset(); S->RttS = 0.3; S->ProbeF = 0.f; S->DriftMax = 0.f; KeyEv(S->PC(), EKeys::W, true); }, TrackBack,
				[S]() { KeyEv(S->PC(), EKeys::W, false); });
			Add(TEXT("KB49b пауза 60 мс"), 0.06f, 0.3f, []() {}, TrackBack, [S]() { KeyEv(S->PC(), EKeys::W, true); });
			Add(TEXT("KB49c W держится 1.2 с"), 1.2f, 1.5f, []() {}, TrackBack, [S]()
			{
				S->Expect(S->ProbeF < 5.f && S->DriftMax > 200.f && S->N5 == 0,
					FString::Printf(TEXT("повторное W быстрее RTT: откат назад %.1f ед. (<5), продвинулся %.0f, result 5 %d [ревизия ВАЖНО-2]"), S->ProbeF, S->DriftMax, S->N5));
				KeyEv(S->PC(), EKeys::W, false); S->RttS = 0.08;
			});
			// KB50. Отказ на бегу (сервер отказал новому направлению, прежний отрезок продолжается): добегаем прежний грант, без возврата.
			PauseAll(TEXT("пауза KB50"));
			RunW(TEXT("KB50a бег W (ПКМ)"), true);
			Add(TEXT("KB50b отказ стрейфу на бегу"), 0.f, 2.f, [S]() { S->Mark = S->Caps.Num(); S->bForceRefuse = false; }, [S]()
			{
				if (S->Count(4006, S->Mark) == 0) { return false; }   // сразу после очередного 4006 вперёд
				AAsdaPlayerController* PC = S->PC(); if (!PC) { return true; }
				S->bForceRefuse = true;
				S->ProbePos = FVector2D((double)PC->LocalStartReturnCount, 0.0);
				S->SendS = FVector2D::DotProduct(S->Pos() - S->Home, S->DirA);
				S->ProbeF = 0.f; S->DriftMax = (float)S->SendS;
				KeyEv(PC, EKeys::D, true);   // W + D со ПКМ — новое направление (диагональ)
				return true;
			}, []() {});
			Add(TEXT("KB50c после отказа"), 1.0f, 1.3f, []() {}, TrackBack, [S]()
			{
				AAsdaPlayerController* PC = S->PC();
				const int32 NRet = PC ? PC->LocalStartReturnCount - (int32)S->ProbePos.X : -1;
				const float Gain = S->DriftMax - (float)S->SendS;
				S->Expect(NRet >= 1 && S->ProbeF < 5.f && Gain > 100.f && S->N5 == 0,
					FString::Printf(TEXT("отказ на бегу: обработан %d, откат назад %.1f ед. (<5), вперёд ещё %.0f ед. (прежний грант), result 5 %d [ревизия ВАЖНО-3]"), NRet, S->ProbeF, Gain, S->N5));
				S->bForceRefuse = false; ReleaseAll(S->PC());
			});
			// KB51. Расхождение скоростей: клиент стартует 0.35 при сервере 0.5 и 0.6 при сервере 0.35 — ни result 5, ни Revision.
			for (const int32 Var : { 0, 1 })
			{
				PauseAll(FString::Printf(TEXT("пауза KB51-%d"), Var));
				Add(FString::Printf(TEXT("KB51-%d скорости %s"), Var, Var ? TEXT("клиент 0.6 / сервер 0.35") : TEXT("клиент 0.35 / сервер 0.5")), 1.6f, 1.9f, [S, Reset, Var]()
				{
					Reset(); S->SrvSpeed = Var ? 0.35f : 0.5f;
					if (S->N()) { S->N()->LastServerSpeed = Var ? 0.6f : 0.35f; }
					KeyEv(S->PC(), EKeys::W, true);
				}, []() { return true; }, [S]() { KeyEv(S->PC(), EKeys::W, false); });
				Add(FString::Printf(TEXT("KB51-%d после отпускания"), Var), 0.6f, 0.9f, []() {}, []() { return true; }, [S, Var]()
				{
					S->Expect(S->N5 == 0 && S->NRev == 0, FString::Printf(TEXT("расхождение скоростей (%d): result 5 %d, Revision %d (0/0)"), Var, S->N5, S->NRev));
					if (S->N()) { S->N()->LastServerSpeed = 0.35f; } S->SrvSpeed = 0.35f;
				});
			}
			// KB52. Исходящий задержан на 1.2 с (Wi-Fi): без гранта персонаж не уходит дальше потолка 0.6 с, затем ход по гранту без отката.
			PauseAll(TEXT("пауза KB52"));
			Add(TEXT("KB52a задержка исходящего 1.2 с"), 1.15f, 1.4f, [S, Reset]()
			{
				Reset(); S->RttS = 0.1; S->SrvExtraUpS = 1.2; S->ProbeF = 0.f;
				if (AAsdaPlayerController* PC = S->PC()) { S->ProbePos = FVector2D((double)PC->LocalStartCapCount, 0.0); }
				KeyEv(S->PC(), EKeys::W, true);
			}, [S]() { S->ProbeF = FMath::Max(S->ProbeF, (float)FVector2D::Distance(S->Pos(), S->Home)); return true; }, [S]()
			{
				AAsdaPlayerController* PC = S->PC();
				const int32 NCap = PC ? PC->LocalStartCapCount - (int32)S->ProbePos.X : -1;
				S->Expect(NCap == 1 && S->ProbeF < 0.6f * 350.f + 40.f,
					FString::Printf(TEXT("до гранта: потолок сработал %d (=1), ушёл %.0f ед. (≤ 0.6 с·скорость) [ревизия ВАЖНО-4]"), NCap, S->ProbeF));
			});
			Add(TEXT("KB52b ход по гранту 3 с"), 3.0f, 3.3f, []() {}, []() { return true; }, [S]() { KeyEv(S->PC(), EKeys::W, false); });
			Add(TEXT("KB52c после отпускания"), 2.8f, 3.2f, []() {}, []() { return true; }, [S]()
			{
				AAsdaPlayerCharacter* C = S->Ch();
				S->Expect(S->N5 == 0 && S->NRev == 0 && C && !C->IsMoving(),
					FString::Printf(TEXT("задержка исходящего: result 5 %d, Revision %d (0/0), встал %d"), S->N5, S->NRev, C && !C->IsMoving()));
				S->SrvExtraUpS = 0.0; S->RttS = 0.08;
			});
			// KB53. Клик при бите 2 (отложенный отрезок): второй клик, пока ответа на первый нет — отрезок уходит после ответа, со стартом.
			PauseAll(TEXT("пауза KB53"));
			Add(TEXT("KB53a клик A"), 0.2f, 0.5f, [S, Reset]()
			{
				Reset(); S->RttS = 0.4;
				AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
				const FVector2D Ue = AsdaCoord::GameToUe2D(S->Home + S->DirA * 1500.f);
				PC->LastMoveSendTime = 0.0;
				PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
			}, []() { return true; }, [S]()
			{
				AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
				S->bProbe = S->N() && S->N()->IsMoveRequestInFlight();
				S->ProbePos = S->Pos() + FVector2D(-S->DirA.Y, S->DirA.X) * 800.f;
				S->Mark = S->Caps.Num();
				const FVector2D Ue = AsdaCoord::GameToUe2D(S->ProbePos);
				PC->LastMoveSendTime = 0.0;
				PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
				S->DriftMax = (float)FVector2D::Distance(S->Pos(), S->ProbePos);
			});
			Add(TEXT("KB53b отложенный отрезок к B"), 1.0f, 1.3f, []() {}, []() { return true; }, [S]()
			{
				int32 NB = 0; for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4006 && FVector2D::Distance(S->Caps[i].Dst, S->ProbePos) < 120.0) { ++NB; } }
				const float D = (float)FVector2D::Distance(S->Pos(), S->ProbePos);
				S->Expect(S->bProbe && NB >= 1 && D < S->DriftMax - 150.f && S->N5 == 0,
					FString::Printf(TEXT("клик при бите 2 (%d): отрезок к B ушёл %d раз, до B %.0f ед. (было %.0f), result 5 %d"), S->bProbe, NB, D, S->DriftMax, S->N5));
				if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка")); }
				S->RttS = 0.08;
			});
			// KB54. Смерть при ожидании ответа: признак локального старта снят (устаревшая точка не используется).
			PauseAll(TEXT("пауза KB54"));
			Add(TEXT("KB54 смерть при ожидании ответа"), 0.f, 1.5f, [S, Reset]() { Reset(); S->RttS = 0.4; S->Mark = S->Caps.Num(); KeyEv(S->PC(), EKeys::W, true); }, [S]()
			{
				AAsdaPlayerController* PC = S->PC(); if (!PC) { return true; }
				if (S->Count(4006, S->Mark) == 0) { return false; }
				if (S->N() && !S->N()->bSelfDead) { S->bProbe = PC->bLocalStartPending; S->N()->bSelfDead = true; S->ProbeFrame = GFrameCounter; return false; }
				// ★ R4, доля a, запись 9 (15.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: приговор — не раньше СЛЕДУЮЩЕГО кадра. Опрос идёт looping-таймером
				//   0,02 с; после короткого кадра он срабатывает дважды в одном кадре, и второй опрос выносил приговор до тика
				//   контроллера, который и снимает признак по смерти (красный прогон `rx_r4`: кадры 13 и 24 мс).
				return GFrameCounter > S->ProbeFrame;   // следующий опрос — после тика смерти
			}, [S]()
			{
				AAsdaPlayerController* PC = S->PC();
				S->Expect(S->bProbe && PC && !PC->bLocalStartPending, FString::Printf(TEXT("ожидание до смерти %d → после смерти %d (ждали 1 → 0) [ревизия ВАЖНО-5]"), S->bProbe, PC && PC->bLocalStartPending));
				if (S->N()) { S->N()->bSelfDead = false; }
				KeyEv(S->PC(), EKeys::W, false); S->RttS = 0.08;
			});
		}
	}

	// ═══════════ ★ 22.10.2026 — ПЛАВНОСТЬ ПОПРАВКИ СВОЕГО (ревизия плавности ВАЖНО-А/Б): что видит игрок ═══════════
	for (const int32 Ct : { 1, 0 })
	{
		PauseAll(FString::Printf(TEXT("пауза KB47 ключ камеры %d"), Ct));
		// Ключ — ДО сброса, и пауза: снап сброса домой сам оставляет хвост, который сложился бы с поправкой сценария.
		Add(FString::Printf(TEXT("KB47 подготовка, хвост камеры %d"), Ct), 0.8f, 1.1f, [S, Reset, Ct]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Anim.SelfCameraTail"), Ct);
			Reset();
		}, []() { return true; }, []() {});
		Add(FString::Printf(TEXT("KB47 поправка 200 ед., хвост камеры %d"), Ct), 1.0f, 1.3f, [S]()
		{
			AAsdaPlayerCharacter* C = S->Ch(); if (!C || !C->CameraArm || !C->MeshRoot) { return; }
			const FVector CamBefore = C->CameraArm->GetComponentLocation();
			const FVector RelBefore = CamBefore - C->MeshRoot->GetComponentLocation();
			const FVector ActBefore = C->GetActorLocation();
			C->NetSnap(S->Pos() + S->DirA * 200.f);
			const FVector CamAfter = C->CameraArm->GetComponentLocation();
			// ★ 26.10.2026: диагностика поправки (что обнулило хвост, если камера прыгнула).
			UE_LOG(LogAsdaMove4007Test, Log, TEXT("KB47 диагностика: актор сдвинут на %.1f, хвост %.1f, скрыт %d, MeshRoot rel %s, камера rel %s, контроллер %s"),
				FVector::Dist2D(C->GetActorLocation(), ActBefore), C->GetVisualTail().Size(), C->IsHidden() ? 1 : 0,
				*C->MeshRoot->GetRelativeLocation().ToString(), *C->CameraArm->GetRelativeLocation().ToString(),
				C->GetController() ? *C->GetController()->GetName() : TEXT("нет"));
			const FVector RelAfter = CamAfter - C->MeshRoot->GetComponentLocation();
			S->ProbeF = (float)FVector::Dist2D(CamAfter, CamBefore);
			S->ProbePos = FVector2D(FVector::Dist(RelAfter, RelBefore), 0.0);
			S->CamRel0 = RelBefore; S->DriftMax = (float)S->ProbePos.X;
		}, [S]()
		{
			// Что видит игрок: положение тела в кадре (камера − тело) от кадра к кадру, пока хвост гаснет.
			AAsdaPlayerCharacter* C = S->Ch();
			if (C && C->CameraArm && C->MeshRoot) { S->DriftMax = FMath::Max(S->DriftMax, (float)FVector::Dist(C->CameraArm->GetComponentLocation() - C->MeshRoot->GetComponentLocation(), S->CamRel0)); }
			return true;
		}, [S, Ct]()
		{
			AAsdaPlayerCharacter* C = S->Ch(); if (!C || !C->CameraArm) { return; }
			if (Ct) { S->Expect(S->DriftMax < 2.f, FString::Printf(TEXT("тело в кадре не смещалось, пока хвост гас: наибольший сдвиг %.2f ед. (<2)"), S->DriftMax)); }
			else    { S->Expect(S->DriftMax > 100.f, FString::Printf(TEXT("ключ 0: тело в кадре отставало от камеры до %.0f ед. (контроль)"), S->DriftMax)); }
			if (Ct) { S->Expect(S->ProbeF < 5.f && S->ProbePos.X < 1.0, FString::Printf(TEXT("в кадр поправки камера сдвинулась на %.1f ед. (≤5), тело в кадре на месте (%.2f) — экран не прыгнул [ИСПРАВЛЕНИЕ]"), S->ProbeF, S->ProbePos.X)); }
			else    { S->Expect(S->ProbeF > 150.f, FString::Printf(TEXT("ключ 0: камера прыгнула на %.0f ед. (контроль)"), S->ProbeF)); }
			const FVector Rel = C->CameraArm->GetComponentLocation() - C->GetActorLocation();
			S->Expect(C->GetVisualTail().IsZero() && FMath::Abs(Rel.Z - 120.0) < 1.0 && FVector::Dist2D(Rel, FVector::ZeroVector) < 1.0,
				FString::Printf(TEXT("через 1 с хвоста нет, камера на месте над актором (%.1f, %.1f, %.1f)"), Rel.X, Rel.Y, Rel.Z));
		});
	}
	PauseAll(TEXT("пауза KB48"));
	Add(TEXT("KB48 перенос 4056 — чистый снап"), 0.2f, 0.5f, [S, Reset]()
	{
		Reset();
		AsdaCVarSave::SetForTest(TEXT("asda.Anim.SelfCameraTail"), 1);
		AAsdaPlayerCharacter* C = S->Ch(); if (!C) { return; }
		C->NetSnap(S->Pos() + S->DirA * 150.f);          // хвост есть
		C->FaceGameDir(-S->DirA);                         // доворот идёт
		C->NetSnapHard(S->Home + S->DirA * 300.f);
		S->ProbeF = (float)C->GetVisualTail().Size();
		S->ProbePos = FVector2D(FMath::Abs(FMath::FindDeltaAngleDegrees(C->GetVisualFacingYawUE(), C->FacingYawLogical - C->MeshYawOffset)), 0.0);
	}, []() { return true; }, [S]()
	{
		S->Expect(S->ProbeF < 0.01f && S->ProbePos.X < 0.5, FString::Printf(TEXT("4056: хвост %.2f ед. (=0), курс модели к логическому %.2f° (=0) — без «проезда»"), S->ProbeF, S->ProbePos.X));
	});
	// ═══════════ ★ 29.10.2026 — МЕТКА ЦЕЛИ АВТОБЕГА НА КАРТАХ (ключ asda.Map.DestMarkClear; баг игроков «Moving does not remove
	//   the auto move icon»). Ожидания — по эталону: нажатие клавиш (у порта маршрут стирается сразу) и отпускание (math_geom.c:689)
	//   снимают метку; клик по земле ставит её в клетку клика (input.c:678); смерть (состояние 6, LABEL_111 game_main.c:1707) — снимает.
	//   Контроль ключом 0 — метка остаётся (прежний порт). ═══════════
	{
		auto Mini = [S]() { return S->N() ? S->N()->GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr; };
		// Метка «как от клика по карте» — в 10 клетках по направлению A от дома.
		auto SetMark = [S, Mini]()
		{
			if (UAsdaMinimapSubsystem* M = Mini())
			{
				const FIntPoint T = AsdaCoord::GameToTile(S->Home + S->DirA * 1000.f);
				M->SetDestinationTile(T.X, T.Y, true);
			}
		};
		auto MarkOn = [Mini]() { const UAsdaMinimapSubsystem* M = Mini(); return M && M->IsDestinationActive(); };
		for (const int32 Dk : { 1, 0 })
		{
			PauseAll(FString::Printf(TEXT("пауза KB60 ключ метки %d"), Dk));
			// KB60a: метка стоит, нажали W — маршрут снят, метка снята (ключ 0 — осталась).
			Add(FString::Printf(TEXT("KB60a нажатие W при метке, ключ %d"), Dk), 0.6f, 1.0f, [S, Reset, SetMark, Dk]()
			{
				Reset();
				AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardEtalon"), 1);
				AsdaCVarSave::SetForTest(TEXT("asda.Map.DestMarkClear"), Dk);
				SetMark();
				KeyEv(S->PC(), EKeys::W, true);
			}, []() { return true; }, [S, MarkOn, Dk]()
			{
				const bool bOn = MarkOn();
				if (Dk) { S->Expect(!bOn, TEXT("KB60a: нажатие W — метка цели автобега снята [ПОРТ: маршрут стёрт]")); }
				else    { S->Expect(bOn, TEXT("KB60a контроль, ключ 0: метка осталась (прежний порт)")); }
			});
			// KB60b: W держится, метку поставили заново (клик по карте) — отпускание снимает (эталон math_geom.c:689).
			Add(FString::Printf(TEXT("KB60b отпускание W, ключ %d"), Dk), 0.4f, 0.8f, [S, SetMark]()
			{
				SetMark();
				KeyEv(S->PC(), EKeys::W, false);
			}, []() { return true; }, [S, MarkOn, Dk]()
			{
				const bool bOn = MarkOn();
				if (Dk) { S->Expect(!bOn, TEXT("KB60b: отпускание W — метка снята (math_geom.c:689)")); }
				else    { S->Expect(bOn, TEXT("KB60b контроль, ключ 0: метка осталась")); }
			});
			// KB60c: клик по земле — метка в клетке клика (ключ 0 — клик метку не ставит).
			Add(FString::Printf(TEXT("KB60c клик по земле, ключ %d"), Dk), 0.3f, 0.6f, [S, Reset, Mini]()
			{
				Reset();
				if (UAsdaMinimapSubsystem* M = Mini()) { M->SetDestinationTile(0, 0, false); }
				AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); if (!PC || !C) { return; }
				S->ProbePos = S->Home + S->DirA * 1500.f;
				const FVector2D Ue = AsdaCoord::GameToUe2D(S->ProbePos);
				PC->LastMoveSendTime = 0.0;
				PC->GroundClickMove(FVector(Ue.X, Ue.Y, C->GetActorLocation().Z));
			}, []() { return true; }, [S, Mini, MarkOn, Dk]()
			{
				const UAsdaMinimapSubsystem* M = Mini();
				const FIntPoint Want = AsdaCoord::GameToTile(S->ProbePos);
				const FIntPoint Got = M ? M->GetDestinationTile() : FIntPoint(-1, -1);
				if (Dk)
				{
					S->Expect(MarkOn() && Got == Want, FString::Printf(TEXT("KB60c: клик по земле — метка %d в клетке (%d,%d), клик (%d,%d) (input.c:678)"),
						MarkOn() ? 1 : 0, Got.X, Got.Y, Want.X, Want.Y));
				}
				else { S->Expect(!MarkOn(), TEXT("KB60c контроль, ключ 0: клик по земле метку на картах не ставит (прежний порт)")); }
				if (AAsdaPlayerController* PC = S->PC()) { PC->StopMoveAndNotify(TEXT("самотест: уборка KB60c")); }
			});
			// KB60d: смерть (состояние 6 набора LABEL_111) — тик контроллера снимает метку.
			Add(FString::Printf(TEXT("KB60d смерть при метке, ключ %d"), Dk), 0.3f, 0.6f, [S, Reset, SetMark]()
			{
				Reset();
				SetMark();
				if (S->N()) { S->N()->bSelfDead = true; }
			}, []() { return true; }, [S, MarkOn, Dk]()
			{
				const bool bOn = MarkOn();
				if (Dk) { S->Expect(!bOn, TEXT("KB60d: смерть — метка снята (LABEL_111, game_main.c:1707)")); }
				else    { S->Expect(bOn, TEXT("KB60d контроль, ключ 0: метка пережила смерть")); }
				if (S->N()) { S->N()->bSelfDead = false; }
				if (UAsdaMinimapSubsystem* M = S->N() ? S->N()->GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr) { M->SetDestinationTile(0, 0, false); }
			});
		}
		// Ключ метки вернёт `RestoreAll` шага «итог».
	}
	PauseAll(TEXT("пауза после 22.10"));
	Add(TEXT("22.10: режим ввода — как до прогона"), 0.f, 1.f, [S]()
	{
		if (S->bWasIgnoringInput)
		{
			if (UAsdaWindowSubsystem* WM = S->N()->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetBaseInputMode(false); }
		}
	}, []() { return true; }, []() {});
	Pause(TEXT("пауза итог"));
	Add(TEXT("итог"), 0.f, 1.f, [S, Net, Reset]()
	{
		Reset();
		if (AAsdaPlayerController* PC = S->PC()) { PC->KbDebugLmb = -1; PC->KbDebugRmb = -1; }
		AsdaCVarSave::RestoreAll(TEXT("asda.Move.SelfTestKeyboard"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — значение + SetBy
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		Net->bActionPending = false;
		UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-КЛАВИШИ ИТОГ: ok=%d fail=%d (перехвачено 4006: %d, 4008: %d, 4024: %d)"),
			S->Ok, S->Fail, S->Count(4006), S->Count(4008), S->Count(4024));
	}, []() { return true; }, []() {});

	// ── Спавн и исполнитель (как у `Run`) ──
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<decltype(S)::ElementType> WSg = S;   // состояние прогона — общий флаг отложенного старта
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const auto Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте (первый сработавший таймер)
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && !AsdaSelfTestSelfReady::IsReady(Wd) && GEngine)
			{
				if (!P || !P->GetPawn()) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); }   // ★ 01.11.2026: карта — только без павна
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, Delay), false);
	}
	TWeakPtr<FKState> WS = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WS, WW]()
	{
		TSharedPtr<FKState> St = WS.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }   // ★ СРЕДН-1: отказ на старте — исполнитель не идёт
		const double Now = FPlatformTime::Seconds();
		// ★ 22.10.2026: пакеты клиента, дошедшие до «сервера» (по порядку отправки — один сокет).
		while (St->SrvIn.Num() > 0 && St->SrvIn[0].AtS <= Now)
		{
			const FKCap In = St->SrvIn[0];
			St->SrvIn.RemoveAt(0);
			if (In.Op == 4006) { SrvCharMove(*St, In.Cur, In.Dst, In.Act, In.Tgt); }
			else if (In.Op == 4008) { SrvCharStop(*St, In.Cur); }
		}
		for (int32 i = 0; i < St->Queue.Num();)
		{
			if (St->Queue[i].AtS <= Now)
			{
				FInject In = MoveTemp(St->Queue[i]);
				St->Queue.RemoveAt(i);
				if (UAsdaNetSubsystem* N = St->N()) { N->DebugFeedPacket(In.Op, In.Body); }
			}
			else { ++i; }
		}
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaMove4007Test, Log, TEXT("САМОТЕСТ-КЛАВИШИ шаг [ключ=%d] %s"), St->Key, *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		if (El < Stp.MinSec) { if (Stp.Poll) { Stp.Poll(); } return; }
		const bool bDone = Stp.Poll ? Stp.Poll() : true;
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с (позиция %s)"), Stp.TimeoutSec, *V2(St->Pos()))); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.02f, true, Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMoveKeyboard(
	TEXT("asda.Move.SelfTestKeyboard"),
	TEXT("Самотест хода клавишами при обоих значениях asda.Move.KeyboardEtalon: старт по гранту и длина точки (800·cos наклона), "
	     "повтор 1000 мс, отказ сервера → повтор 10 мс, бит 2, W+S, A стоя (4006 в свою точку), отпускание (4006 cur→cur + 4008), "
	     "ЛКМ и автоповтор, бег 10 с без античита, пятка, вставание 4024/109, прибытие → 4008, автобег и нотис, свиток склада, отказ 110611 (строка один раз / каждый кадр), окно гейта посреди хода. "
	     "Сервер-автоответчик повторяет CharMove/CharStop с состоянием. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Move.SelfTestKeyboard"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		FAsdaMove4007SelfTest::RunKeyboard(A, W);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMove4007(
	TEXT("asda.Move.SelfTest4007"),
	TEXT("Самотест своего хода 4006→4007/4009→4008 при обоих значениях asda.Move.EtalonSpeedSync: клик-мув, маршрут из "
	     "трёх узлов, античит result=5, подмена точки гранта, 4009, подход с +25, сосед +25, отказ, бит 2 (сторож, отложенный "
	     "отрезок), опоздавший грант, «Паника», клавиатура, 4062, гейт смерти; подход в своей клетке 100×100 (бой, навык, "
	     "клик-контроль) и все читатели бита 2 (клик по миру, гейт навыков, бой ×2, подход ради навыка, отказ 4027, сброс "
	     "сессии). Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Move.SelfTest4007"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		FAsdaMove4007SelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
