// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ КООРДИНАТ ПАКЕТА 4008 CharStop (26.09.2026, отчёт `Reports/stop4008_fix.md`). [ПОРТ]
//
// ★ ЗАЧЕМ. Ревизия офлайн-защиты (`Reports/offline_guard.md`, «замечено попутно») нашла три отправителя 4008 —
//   окно ремесла 76000 (`UAsdaCraftWindowSubsystem::OpenAfterGates`), копание (`UAsdaInventorySubsystem::
//   ApplyShovelingTool`) и вход в сцену гачи (`UAsdaPremiumGachaSubsystem::OpenScene`), — которые клали в тело
//   сырые `GetActorLocation().X/.Y` ДВИЖКА. Эталон `Net_SendActionPacket@0x9F9C00` (network.c:4998) пишет в
//   msg[5]/msg[9] (и копией в msg[13]/msg[17]) ИГРОВЫЕ `worldPos` X и Z персонажа. Исправлено общей точкой
//   `UAsdaNetSubsystem::SendStop4008AtActor` (перевод — `AsdaCoord::UeToGame2D`).
//
// ★ КАК ПРОВЕРЯЕТ. Зовёт НАСТОЯЩИЕ входы трёх мест (не копии), перед каждой пробой взводит эталонный
//   `bActionPending` и ставит УЗКИЙ перехват 4008 (`ArmSendCapture(…, 4008)`): тело уходит в перехватчик, в сеть —
//   ничего. Раскладка тела — `FillMoveBody`: [0..15] MD5, [16] тип, [17..18] userNum, [19..20] флаг, [21] curX,
//   [25] curZ, [29] dstX, [33] dstZ (float LE). Ожидание — `AsdaCoord::UeToGame2D(позиция персонажа)`.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): (а) правильное место `StopMoveAndNotify` обязано
//   пройти; (б) «мутация» — прямой `SendStop4008(UE.X, UE.Y)`, как было до правки, — проверка ОБЯЗАНА её отличить.
//   Если игровые и UE-координаты персонажа совпадают (нельзя отличить), самотест сам отводит персонажа в точку,
//   где они различаются, и возвращает обратно в конце.
//
// Консоль: `asda.Net.SelfTestStop4008 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-4008», итог — «САМОТЕСТ-4008 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "AsdaBytes.h"   // ★ N6 (В1 дедупа №2): единые читалки little-endian

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "World/AsdaCoord.h"                   // единый перевод UE→игра (ожидание пробы)
#include "World/AsdaPlayerController.h"        // StopMoveAndNotify — правильное место (контроль)
#include "UI/AsdaCraftWindowSubsystem.h"       // место 1: открытие окна 76000
#include "UI/AsdaInventorySubsystem.h"         // место 2: копание (5029 код 11)
#include "UI/AsdaPremiumGachaSubsystem.h"      // место 3: вход в сцену гачи
#include "UI/AsdaWindowSubsystem.h"            // ★ В3 S1: возврат AlwaysTop кнопки дня гачи
#include "Items/AsdaItemTableSubsystem.h"      // поиск инструмента копания (подтип 33/34)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStop4008Test, Log, All);

// Друг `UAsdaNetSubsystem` (объявлен в заголовке): нужен перехватчик и флаг `bActionPending`.
struct FAsdaStop4008SelfTest
{
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args, UWorld* W);
};

namespace AsdaStop4008TestLocal
{
	// Один перехваченный 4008.
	struct FCap
	{
		FString Label;          // какая проба его вызвала
		uint16  UserNum = 0;    // body[1..2]
		uint16  Flag = 0;       // body[3..4] (эталон 0)
		float   CurX = 0.f, CurZ = 0.f, DstX = 0.f, DstZ = 0.f;
		int32   Len = 0;
	};
	// Общее состояние прогона (живёт в лямбдах таймеров).
	struct FState
	{
		TArray<FCap> Caps;
		FString CurLabel;       // подпись текущей пробы — перехватчик кладёт её в запись
		uint32 Gen = 0;         // поколение СВОЕГО перехватчика
		int32 Ok = 0, Fail = 0;
		bool bMoved = false;    // персонажа отводили — вернуть в конце
		FVector SavedLoc = FVector::ZeroVector;
	};

	// ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`; прежние имена и типы — переходники.
	static float RdF(const uint8* P) { return AsdaBytes::RdF32(P); }
	static uint16 RdU16(const uint8* P) { return (uint16)AsdaBytes::RdU16(P); }

	static APawn* SelfPawn(UWorld* W)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	// Итог одной пробы. bExpectGame = true — тело обязано нести игровые координаты (исправленные места);
	//   false — мутация «сырые UE», проверка обязана её отличить (иначе самотест слеп).
	static void Judge(FState& S, int32 Before, const TCHAR* Label, const FVector& UeLoc, bool bExpectGame)
	{
		const FVector2D G = AsdaCoord::UeToGame2D(UeLoc);
		auto Line = [](const FString& T) { UE_LOG(LogAsdaStop4008Test, Log, TEXT("%s"), *T); };
		if (S.Caps.Num() != Before + 1)
		{
			++S.Fail;
			Line(FString::Printf(TEXT("САМОТЕСТ-4008 ПРОВАЛ [%s]: перехвачено пакетов 4008 %d (ожидался ровно 1)"),
				Label, S.Caps.Num() - Before));
			return;
		}
		const FCap& C = S.Caps.Last();
		const bool bGame  = FMath::IsNearlyEqual(C.CurX, (float)G.X, 0.5f) && FMath::IsNearlyEqual(C.CurZ, (float)G.Y, 0.5f);
		const bool bDstEq = FMath::IsNearlyEqual(C.DstX, C.CurX, 0.01f) && FMath::IsNearlyEqual(C.DstZ, C.CurZ, 0.01f);
		const bool bHdr   = (C.Flag == 0) && (C.Len == 43);
		const bool bPass  = bExpectGame ? (bGame && bDstEq && bHdr) : !bGame;   // мутация: проверка её ЛОВИТ
		if (bPass) { ++S.Ok; } else { ++S.Fail; }
		Line(FString::Printf(
			TEXT("САМОТЕСТ-4008 %s [%s]: тело cur=(%.1f,%.1f) dst=(%.1f,%.1f) flag=%u userNum=%u len=%d | ")
			TEXT("персонаж UE=(%.1f,%.1f) → игра=(%.1f,%.1f) | %s"),
			bPass ? TEXT("OK") : TEXT("ПРОВАЛ"), Label, C.CurX, C.CurZ, C.DstX, C.DstZ, C.Flag, C.UserNum, C.Len,
			UeLoc.X, UeLoc.Y, G.X, G.Y,
			bExpectGame ? (bGame ? TEXT("ИГРОВЫЕ координаты") : TEXT("НЕ игровые координаты"))
			            : (bGame ? TEXT("мутация НЕ отличена — самотест слеп") : TEXT("мутация (сырые UE) отличена, как и должно"))));
	}
}

void FAsdaStop4008SelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaStop4008TestLocal;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaStop4008Test, Warning, TEXT("САМОТЕСТ-4008: нет подсистемы сети")); return; }
	bool bQuit = false;
	float Delay = 0.f;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
	}
	Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
	// Только офлайн; окно защиты 4008 на время прохода (второй барьер шлюза).
	static const uint16 kGuardOps[] = { 4008 };
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Net.SelfTestStop4008"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }

	TSharedRef<FState> S = MakeShared<FState>();
	TWeakObjectPtr<UAsdaNetSubsystem> WNet(Net);
	TWeakObjectPtr<UWorld> WW(W);
	// Все пробы идут через kLead секунд после старта: столько даём офлайн-спавну своего павна (шаг ниже).
	constexpr float kLead = 2.f;
	// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
	//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
	// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
	TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Net.SelfTestStop4008"), bQuit);   // общий флаг отказа всех шагов прогона
	auto GateRefused = [Gate, WW]() { return !Gate->Pass(WW.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
	auto At = [W, Net, Delay, kLead, GateRefused](float T, TFunction<void()> Fn)
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [GateRefused, Fn = MoveTemp(Fn)]()
		{
			if (GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
			Fn();
		}), T + Delay + kLead, false);
	};

	// ── t=−kLead: своего павна нет (офлайн) — штатный крюк `Asda.World.FakeSelfSpawn` (та же точка, что 4003).
	//   В строке запуска он не годится: в кадре 0 уровень ещё не загружен и спавн откладывается навсегда
	//   («4003 до загрузки уровня — спавн отложен до PostLoadMap»). Тайл 256,256 — как у прочих живых проверок.
	//   Номер карты: при открытии уровня напрямую менеджер загрузки его не ставит (−1), а без него `OnSelfSpawn`
	//   считает, что уровень не загружен, — штатный крюк `Asda.Load.CurrentMap 3` (alpen = id 3 в реестре карт).
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, GateRefused]()
		{
			UWorld* Wd = WW.Get();
			if (Wd && GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
			if (Wd && !SelfPawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, Delay), false);
	}

	// ── t=0: подготовка — перехват, позиция, различимость UE/игра ──
	At(0.1f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get();
		APawn* P = SelfPawn(Wd);
		if (!N || !P) { ++S->Fail; UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 ПРОВАЛ [подготовка]: нет персонажа")); return; }
		TWeakPtr<FState> WS = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(N, [WS](uint16 Op, const uint8* Pl, int32 Len, bool /*bPad*/, TCHAR /*Env*/)
		{
			TSharedPtr<FState> St = WS.Pin();
			if (!St || Op != 4008 || !Pl || Len < 43) { return; }
			FCap C;
			C.Label = St->CurLabel; C.Len = Len;
			C.UserNum = RdU16(Pl + 17); C.Flag = RdU16(Pl + 19);
			C.CurX = RdF(Pl + 21); C.CurZ = RdF(Pl + 25); C.DstX = RdF(Pl + 29); C.DstZ = RdF(Pl + 33);
			St->Caps.Add(C);
		}, (uint16)4008, TEXT("asda.Net.SelfTestStop4008"));
		// Различимость: если игровые (Y, −X) совпадают с UE (X, Y), проба ничего не докажет — отвести персонажа.
		const FVector L = P->GetActorLocation();
		const FVector2D G = AsdaCoord::UeToGame2D(L);
		if (FMath::Abs(G.X - L.X) < 50.0 && FMath::Abs(G.Y - L.Y) < 50.0)
		{
			S->SavedLoc = L; S->bMoved = true;
			P->SetActorLocation(L + FVector(-1234.f, 567.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
		UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 подготовка: перехват 4008 поколение %u, персонаж UE=(%.1f,%.1f)%s"),
			S->Gen, P->GetActorLocation().X, P->GetActorLocation().Y, S->bMoved ? TEXT(" (отведён для различимости)") : TEXT(""));
	});

	// ── t=0.5: контроль — правильное место (`AAsdaPlayerController::StopMoveAndNotify`, GetGamePosXZ) ──
	At(0.5f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get(); APawn* P = SelfPawn(Wd);
		AAsdaPlayerController* PC = Wd ? Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController()) : nullptr;
		if (!N || !P || !PC) { ++S->Fail; UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 ПРОВАЛ [контроль]: нет контроллера")); return; }
		const int32 Before = S->Caps.Num(); const FVector L = P->GetActorLocation();
		S->CurLabel = TEXT("контроль: StopMoveAndNotify");
		// `StopMoveAndNotify` шлёт только «в мире» — на одну синхронную пробу поднимаем флаг и сразу возвращаем.
		const bool bWasIn = N->bInGameConnected;
		N->bInGameConnected = true; N->bActionPending = true;
		PC->StopMoveAndNotify(TEXT("самотест 4008: контроль"));
		N->bInGameConnected = bWasIn;
		Judge(*S, Before, *S->CurLabel, L, true);
	});

	// ── t=1.0: контрольная пара — мутация «как было до правки»: сырые UE X/Y прямо в SendStop4008 ──
	At(1.0f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); APawn* P = SelfPawn(WW.Get());
		if (!N || !P) { ++S->Fail; return; }
		const int32 Before = S->Caps.Num(); const FVector L = P->GetActorLocation();
		S->CurLabel = TEXT("мутация: сырые UE");
		N->bActionPending = true;
		N->SendStop4008((float)L.X, (float)L.Y);
		Judge(*S, Before, *S->CurLabel, L, false);
	});

	// ── t=1.5: место 1 — окно ремесла 76000 (`GateAndToggle` → `OpenAfterGates`) ──
	At(1.5f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get(); APawn* P = SelfPawn(Wd);
		UAsdaCraftWindowSubsystem* Win = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
		if (!N || !P || !Win) { ++S->Fail; UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 ПРОВАЛ [ремесло 76000]: нет подсистемы")); return; }
		if (Win->IsOpen()) { Win->HideAndCleanup(); }
		const int32 Before = S->Caps.Num(); const FVector L = P->GetActorLocation();
		S->CurLabel = TEXT("ремесло 76000 (OpenAfterGates)");
		N->bActionPending = true;
		Win->GateAndToggle(TEXT("самотест 4008"));
		Judge(*S, Before, *S->CurLabel, L, true);
		if (Win->IsOpen()) { Win->HideAndCleanup(); }   // окно гачи не откроется при открытом ремесле
	});

	// ── t=2.5: место 2 — вход в сцену гачи (`OpenScene`, шаг 8) ──
	At(2.5f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get(); APawn* P = SelfPawn(Wd);
		UAsdaPremiumGachaSubsystem* G = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
		if (!N || !P || !G) { ++S->Fail; UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 ПРОВАЛ [гача]: нет подсистемы")); return; }
		if (G->IsSceneOpen()) { G->CloseScene(); }
		const int32 Before = S->Caps.Num(); const FVector L = P->GetActorLocation();
		S->CurLabel = TEXT("гача (OpenScene)");
		N->bActionPending = true;
		// ★ 11.10.2026 (В3 S1, N1(г)): вход в сцену ставит кнопке дня гачи AlwaysTop (premiumGacha_BeginRoll, world_map.c:4724),
		//   и флаг живёт до конца процесса — в пакетном прогоне `asda.UI.SelfTestRaiseClick` F9 падал. Вернуть как было.
		UAsdaWindowSubsystem* WMg = Wd->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
		const FName kDayBtn(TEXT("Premium_PickBAR_daybtn_X_B"));
		const bool bDayTopWas = WMg && WMg->IsWindowAlwaysTop(kDayBtn);
		G->OpenScene();
		Judge(*S, Before, *S->CurLabel, L, true);
		if (G->IsSceneOpen()) { G->CloseScene(); }
		if (WMg) { WMg->SetWindowAlwaysTop(kDayBtn, bDayTopWas); }
	});

	// ── t=3.5: место 3 — копание: 5029 код 11 боевым приёмником (очередь → Tick → ApplyShovelingTool) ──
	TSharedRef<FVector> ShovelLoc = MakeShared<FVector>(FVector::ZeroVector);
	TSharedRef<int32> ShovelBefore = MakeShared<int32>(-1);
	At(3.5f, [S, WNet, WW, ShovelLoc, ShovelBefore]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get(); APawn* P = SelfPawn(Wd);
		UGameInstance* G = Wd ? Wd->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* Inv = G ? G->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaItemTableSubsystem* IT = G ? G->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!N || !P || !Inv || !IT) { ++S->Fail; UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 ПРОВАЛ [копание]: нет подсистемы")); return; }
		int32 Code = 0;
		for (const FAsdaItemRecord& R : IT->GetAllItems()) { if (R.ItemSubType == 33) { Code = R.ItemCode; break; } }
		if (Code == 0) { ++S->Fail; UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 ПРОВАЛ [копание]: в таблице нет предмета подтипа 33")); return; }
		// Тело 5029 — раскладка `CharSpendItemUse@0x4D8C40` (как `asda.Inven.Recv5029`): +0 результат 11,
		//   +1 userNum ЦЕЛИ (свой), +7 itemCode, +15 контейнер 2, +16 остаток 1.
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = 11;
		const uint16 UNum = N->SelfUserNum; FMemory::Memcpy(&B[1], &UNum, 2);
		FMemory::Memcpy(&B[7], &Code, 4);
		B[15] = 2;
		const int32 One = 1; FMemory::Memcpy(&B[16], &One, 4);
		*ShovelBefore = S->Caps.Num(); *ShovelLoc = P->GetActorLocation();
		S->CurLabel = FString::Printf(TEXT("копание (ApplyShovelingTool, предмет %d)"), Code);
		N->bActionPending = true;
		Inv->SimulateUseResult(B);
		// Разбор очереди в `Tick` у инвентаря ждёт «в мире»; офлайн — тем же разбором через штатный отладочный крюк.
		Inv->DebugPumpPendingPackets();
	});
	At(4.5f, [S, ShovelLoc, ShovelBefore]()
	{
		if (*ShovelBefore < 0) { return; }   // проба не запускалась (провал уже засчитан)
		Judge(*S, *ShovelBefore, *S->CurLabel, *ShovelLoc, true);
	});

	// ── t=5.0: снять перехват, вернуть персонажа, итог ──
	At(5.0f, [S, WNet, WW, bQuit]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get();
		if (N)
		{
			AsdaSelfTestKit::DisarmCapture(N, S->Gen);
			N->bActionPending = false;
		}
		if (S->bMoved) { if (APawn* P = SelfPawn(Wd)) { P->SetActorLocation(S->SavedLoc, false, nullptr, ETeleportType::TeleportPhysics); } }
		UE_LOG(LogAsdaStop4008Test, Log, TEXT("САМОТЕСТ-4008 ИТОГ: ok=%d fail=%d (перехвачено 4008: %d)"), S->Ok, S->Fail, S->Caps.Num());
		AsdaSelfTestKit::Quit(Wd, bQuit);
	});
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestStop4008(
	TEXT("asda.Net.SelfTestStop4008"),
	TEXT("Самотест координат 4008: контроль StopMoveAndNotify, мутация «сырые UE», ремесло 76000, гача, копание — "
	     "узкий перехват тела, сеть не нужна (только офлайн). `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		FAsdaStop4008SelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
