// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЧАСТЫЕ КЛИКИ НЕ ТЕРЯЮТСЯ» `asda.Input.SelfTestClickQueue` (05.11.2026, баг игроков «второй клик двойного отменяется,
//   при частых кликах теряется много нажатий»). [ПОРТ] Только офлайн (`-AsdaNoNet`): мировой клик — `OnLeftClick` с подменённой
//   экранной точкой (путь мыши, `AsdaSkillCastLock::DebugWorldClick`; второе нажатие двойного — событие `bDouble`, как IE_DoubleClick при
//   `OnLeftPressedInput`), исходящие — перехват настоящего отправителя 4006, ответ «сервера» 4007 подаётся штатным приёмом
//   `DebugFeedPacket` через RTT. Разбор эталона и ключа — `World/AsdaSkillCastLock.h` (`asda.Input.ClickQueue`).
//   Сценарии: серии из 6 кликов по земле с шагом 60/120/250/450 мс при RTT 120 мс — ключ 1: персонаж идёт в ПОСЛЕДНЮЮ точку, два 4006
//   одновременно в пути не бывают, между 4006 не меньше 150 мс; ключ 0 (контроль): последняя точка теряется; быстрый двойной клик
//   в той же точке — один 4006 (второе нажатие двойного хода не даёт — решение владельца 09.10); клик по мобу при 4006 в пути — цель выбрана после ответа 4007.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "World/AsdaSkillCastLock.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWalkGrid.h"
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"          // R5-fix7: окно игры — экранная точка клика в координатах Slate
#include "UI/AsdaUiVisibilitySubsystem.h"      // R5-fix7/8: точка клика под окном интерфейса — интерфейс скрыть на время прогона
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "UI/AsdaWindowSubsystem.h"            // R5-fix7: точка клика не под окном интерфейса
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClickQueueTest, Log, All);

namespace AsdaClickQueueTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Input.SelfTestClickQueue");   // имя команды
	const TCHAR* const kTag = TEXT("САМОТЕСТ-ОЧЕРЕДЬ-КЛИКОВ");         // префикс строк журнала
	const TCHAR* const kKey = TEXT("asda.Input.ClickQueue");            // проверяемый ключ
	constexpr int32 kSelfFallback = 4325;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen
	constexpr int32 kRidB = 31401;           // моб для клика при 4006 в пути
	constexpr double kRttS = 0.120;          // задержка ответа 4007 «сервера»

	struct FCap { uint16 Op = 0; double T = 0.0; FVector2D Cur = FVector2D::ZeroVector, Dst = FVector2D::ZeroVector; int16 Sub = 0, Tgt = -1; };
	struct FCqReply { double AtS = 0.0; TArray<uint8> Body; };   // отложенный ответ 4007
	struct FStep { FString Name; float TimeoutSec = 10.f; TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check; };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0; TArray<FCap> Caps; TArray<FCqReply> Replies;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false;
		int32 Self = -1; FVector2D Home = FVector2D::ZeroVector; FIntPoint HomeTile = FIntPoint::ZeroValue;
		// Серия кликов
		int32 CapFrom = 0;                     // исходящие сценария начиная с индекса
		int32 Clicks = 0, ClicksDone = 0;      // сколько кликов в серии и сколько сделано
		double StepMs = 0.0, NextClickS = 0.0; // шаг серии и время следующего клика
		TArray<FVector2D> ClickGame;           // точки кликов (game XZ)
		double LastClickS = 0.0;               // время последнего клика серии
		int32 Phase = 0;
		int32 MaxInFlight = 0;                 // наибольшее число 4006 без ответа одновременно
		double MinGap = 1e9;                   // наименьший промежуток между 4006 сценария

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		double Now() const { return FPlatformTime::Seconds(); }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaClickQueueTest, Log, TEXT("%s %s %s: %s"), kTag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		int32 Count4006() const { int32 C = 0; for (int32 i = CapFrom; i < Caps.Num(); ++i) { C += (Caps[i].Op == 4006) ? 1 : 0; } return C; }
		const FCap* Last4006() const { for (int32 i = Caps.Num() - 1; i >= CapFrom; --i) { if (Caps[i].Op == 4006) { return &Caps[i]; } } return nullptr; }
		// Сброс между сценариями: стоим дома, маршрута нет, ответы досланы, ключ — как задан.
		void Prepare(int32 KeyValue)
		{
			AsdaCVarSave::SetForTest(kKey, KeyValue);
			AsdaSkillCastLock::Reset(TEXT("самотест очереди кликов: новый сценарий"));
			for (FCqReply& R : Replies) { Feed(4007, R.Body); }
			Replies.Reset();
			AAsdaPlayerController* P = PC(); AAsdaPlayerCharacter* C = Ch();
			if (P) { P->CancelRoute(); P->bAutoAttack = false; P->SetTarget(nullptr); }
			if (UAsdaNetSubsystem* Nn = N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест очереди кликов: сброс")); }
			if (C) { C->StopMove(); C->NetSnap(Home); }
			CapFrom = Caps.Num(); Clicks = ClicksDone = 0; ClickGame.Reset(); Phase = 0; MaxInFlight = 0; MinGap = 1e9;
		}
		// Экранная точка земли (проходимая клетка, попадание «земля») в клетке Tile.
		bool ScreenOfTile(const FIntPoint& Tile, FVector2D& Out, FVector2D& OutGame) const
		{
			AAsdaPlayerController* P = PC(); const AAsdaPlayerCharacter* C = Ch();
			if (!P || !C) { return false; }
			const FVector2D G = AsdaCoord::TileCenterGame(Tile);
			if (C->WalkGrid && !C->WalkGrid->IsWorldWalkable(G.X, G.Y)) { return false; }
			const FVector2D U = AsdaCoord::GameToUe2D(G);
			const FVector W(U.X, U.Y, C->GetActorLocation().Z - C->GetSimpleCollisionHalfHeight());
			FVector2D S = FVector2D::ZeroVector;
			if (!P->ProjectWorldLocationToScreen(W, S)) { return false; }
			AActor* Hit = nullptr;
			if (AsdaSkillCastLock::DebugPickKindAt(P, S, Hit) != (uint8)AAsdaPlayerController::EAsdaPickKind::Ground) { return false; }
			// R5-fix7 (14.11.2026): точка под окном интерфейса не годится — мировой клик там гасится (игрок туда и не кликнет). Прежде
			//   такие точки проходили только потому, что курсор Slate стоял не в точке клика.
			if (IsOverUi(S)) { return false; }
			// ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: ожидаемая точка хода — настоящая точка попадания клика, а не центр клетки.
			//   Экранная точка считается по центру клетки на высоте ног персонажа; на склоне луч клика упирается в рельеф в другом
			//   месте (до 250 юнитов в сторону), и проверка «персонаж идёт в последнюю точку серии» сверялась не с той точкой
			//   (10 кадров/с, тест вторым в игре на склоне: 28/1).
			FVector PickUe = FVector::ZeroVector;
			AsdaSkillCastLock::DebugPickPointAt(P, S, PickUe);
			Out = S; OutGame = AsdaCoord::UeToGame2D(PickUe);
			return true;
		}
		// Абсолютная точка экрана Slate по точке вьюпорта игры.
		static FVector2D AbsOf(const FVector2D& ScreenPx)
		{
			const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
			const TSharedPtr<SWindow> Win = VP ? VP->GetWindow() : nullptr;
			return (Win.IsValid() ? FVector2D(Win->GetWindowGeometryInScreen().GetAbsolutePosition()) : FVector2D::ZeroVector) + ScreenPx;
		}
		// Точка вьюпорта лежит под окном интерфейса (тот же реестр зон, что у гейта мирового клика).
		bool IsOverUi(const FVector2D& ScreenPx) const
		{
			const UGameInstance* G = GI();
			const UAsdaWindowSubsystem* WM = G ? G->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			FName Zone;
			return WM && WM->IsScreenPointOverAnyUiWindow(AbsOf(ScreenPx), Zone);
		}
		// ★ R5-fix7/R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: гейт мирового клика «над окном интерфейса» при подменённой точке клика
		//   берёт ЭТУ точку (шов R5-fix8 в `AAsdaPlayerController::IsCursorOverUi`; раньше — настоящий курсор ОС, и итог теста
		//   зависел от положения мыши пользователя). Точка клика теста задаётся геометрией сцены (клетка, моб) и может лежать под
		//   окном интерфейса — такой клик игра законно гасит. Тест проверяет разбор клика по миру, а не этот гейт: если точка под
		//   окном, интерфейс скрывается до конца прогона (хит-тест окон молчит), как в самотесте зон рыбалки.
		bool bUiHiddenForCursor = false;   // интерфейс скрыт тестом — точка клика лежала под окном (вернуть в конце)
		// Перед кликом теста: точка клика под окном интерфейса — скрыть интерфейс до конца прогона.
		void GuardPoint(const FVector2D& ScreenPx)
		{
			const UAsdaWindowSubsystem* WM = GI() ? GI()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			FName Zn;
			if (bUiHiddenForCursor || !WM || !WM->IsScreenPointOverAnyUiWindow(AbsOf(ScreenPx), Zn)) { return; }
			UAsdaUiVisibilitySubsystem* Uv = GI() ? GI()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
			if (!Uv) { return; }
			Uv->SetUiShown(false, TEXT("самотест: точка клика под окном интерфейса"));
			bUiHiddenForCursor = true;
			UE_LOG(LogAsdaClickQueueTest, Warning, TEXT("  точка клика под окном «%s» — интерфейс скрыт до конца прогона (геометрия сцены теста)"), *Zn.ToString());
		}
		// В конце прогона: вернуть интерфейс, если его скрывал тест.
		void RestoreUi()
		{
			if (!bUiHiddenForCursor) { return; }
			if (UAsdaUiVisibilitySubsystem* Uv = GI() ? GI()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr) { Uv->SetUiShown(true, TEXT("самотест: возврат интерфейса")); }
			bUiHiddenForCursor = false;
		}
		// Клик по земле в i-й точке серии (клетки по дуге вокруг дома, радиус 3–4), путь мыши.
		bool ClickSeries(int32 I, bool bDouble)
		{
			static const FIntPoint kPts[] = { {3, 0}, {3, 2}, {2, 3}, {0, 3}, {-2, 3}, {-3, 2}, {-3, 0}, {-3, -2} };
			for (int32 Alt = 0; Alt < 6; ++Alt)   // R5-fix7: запасных точек больше — часть клеток лежит под окнами интерфейса
			{
				const FIntPoint T = HomeTile + kPts[(I + Alt * 3) % UE_ARRAY_COUNT(kPts)] + FIntPoint(Alt, Alt);
				FVector2D S, G;
				if (!ScreenOfTile(T, S, G)) { continue; }
				GuardPoint(S);
				AsdaSkillCastLock::DebugWorldClick(PC(), S, bDouble);
				ClickGame.Add(G); LastClickS = Now();
				return true;
			}
			Expect(false, FString::Printf(TEXT("точка земли для клика %d не найдена"), I));
			return false;
		}
		// Ответы «сервера» по сроку; учёт 4006 без ответа.
		void Pump()
		{
			const double T = Now();
			for (int32 i = 0; i < Replies.Num();)
			{
				if (Replies[i].AtS <= T) { const TArray<uint8> B = Replies[i].Body; Replies.RemoveAt(i); Feed(4007, B); }
				else { ++i; }
			}
			MaxInFlight = FMath::Max(MaxInFlight, Replies.Num());
		}
		// Конечная точка, куда идёт персонаж: маршрут, иначе последний 4006.
		bool FinalDst(FVector2D& Out) const
		{
			if (AsdaSkillCastLock::DebugRouteFinal(PC(), Out)) { return true; }
			if (const FCap* L = Last4006()) { Out = L->Dst; return true; }
			return false;
		}
	};

	// 4007 (27 б): код@0, userNum@1, подтип@3, cur@5/@9, dst@13/@17, скорость@21, цель@25 — эхо тела 4006 (код 1 — ход принят).
	static TArray<uint8> Body4007(uint8 Res, int16 U, int16 Sub, const FVector2D& Cur, const FVector2D& Dst, int16 Tgt)
	{
		TArray<uint8> B; B.SetNumZeroed(27);
		B[0] = Res; FMemory::Memcpy(&B[1], &U, 2); FMemory::Memcpy(&B[3], &Sub, 2);
		const float Cx = (float)Cur.X, Cz = (float)Cur.Y, Dx = (float)Dst.X, Dz = (float)Dst.Y, Spd = 0.35f;
		FMemory::Memcpy(&B[5], &Cx, 4); FMemory::Memcpy(&B[9], &Cz, 4); FMemory::Memcpy(&B[13], &Dx, 4); FMemory::Memcpy(&B[17], &Dz, 4);
		FMemory::Memcpy(&B[21], &Spd, 4); FMemory::Memcpy(&B[25], &Tgt, 2);
		return B;
	}
	static float RdF(const uint8* P) { float V = 0.f; FMemory::Memcpy(&V, P, 4); return V; }
	static int16 RdI16(const uint8* P) { int16 V = 0; FMemory::Memcpy(&V, P, 2); return V; }
}

struct FAsdaClickQueueSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaClickQueueSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaClickQueueTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4024, 4026 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (!St || !Pl || Len < 43 || (Op != 4006 && Op != 4008)) { return; }
			const uint8* T = Pl + 16;   // тело после MD5-шапки
			FCap Cp; Cp.Op = Op; Cp.T = FPlatformTime::Seconds();
			Cp.Sub = RdI16(T + 3); Cp.Cur = FVector2D(RdF(T + 5), RdF(T + 9)); Cp.Dst = FVector2D(RdF(T + 13), RdF(T + 17)); Cp.Tgt = RdI16(T + 25);
			if (Op == 4006)
			{
				for (int32 i = St->Caps.Num() - 1; i >= St->CapFrom; --i)
				{
					if (St->Caps[i].Op == 4006) { St->MinGap = FMath::Min(St->MinGap, Cp.T - St->Caps[i].T); break; }
				}
				FCqReply R; R.AtS = Cp.T + kRttS; R.Body = Body4007(1, (int16)St->Self, Cp.Sub, Cp.Cur, Cp.Dst, Cp.Tgt);
				St->Replies.Add(MoveTemp(R));
			}
			St->Caps.Add(Cp);
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRidB, kMobType, S->HomeTile + FIntPoint(-2, 1), 1, 250));
	}, [S]() { return S->El() > 1.0 && S->Mob(kRidB); }, [S]()
	{
		S->Expect(S->Mob(kRidB) != nullptr, TEXT("моб на месте"));
	});

	// Серия из N кликов с шагом StepMs при ключе KeyValue.
	auto AddSeries = [S, Add](int32 N, double StepMs, int32 KeyValue)
	{
		Add(FString::Printf(TEXT("серия %d кликов шаг %.0f мс, ключ %d"), N, StepMs, KeyValue), 12.f, [S, N, StepMs, KeyValue]()
		{
			S->Prepare(KeyValue);
			S->Clicks = N; S->StepMs = StepMs; S->NextClickS = S->Now() + 0.2;
		}, [S]()
		{
			S->Pump();
			if (S->ClicksDone < S->Clicks && S->Now() >= S->NextClickS)
			{
				S->ClickSeries(S->ClicksDone, false);
				++S->ClicksDone; S->NextClickS = S->Now() + S->StepMs / 1000.0;
			}
			return S->ClicksDone >= S->Clicks && S->Now() - S->LastClickS > 1.2 && S->Replies.Num() == 0;
		}, [S, N, StepMs, KeyValue]()
		{
			FVector2D Fin = FVector2D::ZeroVector;
			const bool bFin = S->FinalDst(Fin);
			const FVector2D Last = S->ClickGame.Num() ? S->ClickGame.Last() : FVector2D::ZeroVector;
			const float D = bFin ? FVector2D::Distance(Fin, Last) : -1.f;
			const int32 N06 = S->Count4006();
			UE_LOG(LogAsdaClickQueueTest, Log, TEXT("%s серия %d×%.0f мс ключ %d: кликов %d, 4006 %d, в пути одновременно ≤ %d, мин. промежуток %.0f мс, до последней точки %.0f"),
				kTag, N, StepMs, KeyValue, S->ClickGame.Num(), N06, S->MaxInFlight, S->MinGap < 1e8 ? S->MinGap * 1000.0 : -1.0, D);
			S->Expect(S->MaxInFlight <= 1, FString::Printf(TEXT("защита оригинала сохранена: 4006 в пути одновременно не больше одного (%d)"), S->MaxInFlight));
			S->Expect(N06 <= 1 || S->MinGap >= 0.145, FString::Printf(TEXT("между 4006 не меньше 150 мс (мин. %.0f мс)"), S->MinGap < 1e8 ? S->MinGap * 1000.0 : -1.0));
			if (KeyValue != 0)
			{
				const FVector2D Prev = S->ClickGame.Num() >= 2 ? S->ClickGame[S->ClickGame.Num() - 2] : Last;
				const float DPrev = bFin ? FVector2D::Distance(Fin, Prev) : -1.f;
				S->Expect(bFin && D < DPrev && D < 300.f, FString::Printf(TEXT("ключ 1: персонаж идёт в ПОСЛЕДНЮЮ точку серии (%.0f, до предпоследней %.0f)"), D, DPrev));
				if (StepMs >= 300.0) { S->Expect(N06 == N, FString::Printf(TEXT("ключ 1, редкие клики: каждый клик дал 4006 (%d из %d)"), N06, N)); }
			}
			else if (StepMs < 150.0)
			{
				S->Expect(N06 < N, FString::Printf(TEXT("ключ 0 (контроль): частые клики теряются — 4006 %d из %d кликов (прежний дефект виден)"), N06, N));
			}
		});
	};
	AddSeries(6, 60.0, 0);
	AddSeries(6, 60.0, 1);
	AddSeries(6, 120.0, 1);
	AddSeries(6, 250.0, 1);
	AddSeries(5, 450.0, 1);

	// Быстрый двойной клик в ТОЙ ЖЕ точке (как его признаёт ОС: ~4 px): ход один — второе нажатие лишнего 4006 не шлёт (решение 09.10).
	Add(TEXT("двойной клик в той же точке — один 4006"), 6.f, [S]() { S->Prepare(1); S->NextClickS = S->Now() + 0.2; }, [S]()
	{
		S->Pump();
		if (S->Phase == 0 && S->Now() >= S->NextClickS) { S->ClickSeries(1, false); S->Phase = 1; S->NextClickS = S->Now() + 0.04; }
		else if (S->Phase == 1 && S->Now() >= S->NextClickS) { S->ClickSeries(1, true); S->Phase = 2; }
		return S->Phase == 2 && S->Now() - S->LastClickS > 1.0 && S->Replies.Num() == 0;
	}, [S]()
	{
		FVector2D Fin = FVector2D::ZeroVector;
		const bool bFin = S->FinalDst(Fin);
		const float D = (bFin && S->ClickGame.Num()) ? FVector2D::Distance(Fin, S->ClickGame[0]) : -1.f;
		S->Expect(S->Count4006() == 1, FString::Printf(TEXT("двойной в той же точке: ровно один 4006 (%d)"), S->Count4006()));
		S->Expect(bFin && D < 300.f, FString::Printf(TEXT("идёт в точку двойного клика (%.0f)"), D));
	});

	// Клик по мобу, пока 4006 ждёт ответа: цель выбирается после ответа.
	Add(TEXT("клик по мобу при 4006 в пути"), 5.f, [S]() { S->Prepare(1); S->NextClickS = S->Now() + 0.2; }, [S]()
	{
		if (S->Phase == 0 && S->Now() >= S->NextClickS) { S->ClickSeries(0, false); S->Phase = 1; S->NextClickS = S->Now() + 0.03; return false; }
		if (S->Phase == 1 && S->Now() >= S->NextClickS)
		{
			AAsdaPlayerController* P = S->PC(); AAsdaMobActor* M = S->Mob(kRidB);
			FVector Org, Ext; FVector2D Sc = FVector2D::ZeroVector;
			bool bHit = false;
			if (P && M)
			{
				M->GetActorBounds(false, Org, Ext);
				if (P->ProjectWorldLocationToScreen(Org, Sc))
				{
					for (int32 R = 0; R <= 30 && !bHit; R += 3)
					{
						for (int32 dx = -R; dx <= R && !bHit; dx += 3)
						{
							AActor* Hit = nullptr; const FVector2D Q(Sc.X + dx, Sc.Y + R);
							if (AsdaSkillCastLock::DebugPickKindAt(P, Q, Hit) == (uint8)AAsdaPlayerController::EAsdaPickKind::Mob && Hit == M) { Sc = Q; bHit = true; }
						}
					}
				}
			}
			const bool bFlight = S->N() && S->N()->IsMoveRequestInFlight();
			S->Expect(bHit, TEXT("экранная точка моба найдена"));
			S->Expect(bFlight, TEXT("клик по мобу — пока 4006 без ответа (бит 2)"));
			if (bHit) { S->GuardPoint(Sc); AsdaSkillCastLock::DebugWorldClick(P, Sc, false); }
			S->Expect(!P || P->TargetActor.Get() != M, TEXT("до ответа 4007 цель не сменилась (бит 2 — как оригинал)"));
			S->Phase = 2; S->LastClickS = S->Now();
			return false;
		}
		S->Pump();
		return S->Phase == 2 && S->Now() - S->LastClickS > 0.6 && S->Replies.Num() == 0;
	}, [S]()
	{
		const AAsdaPlayerController* P = S->PC();
		S->Expect(P && P->TargetActor.Get() == S->Mob(kRidB), TEXT("после ответа 4007 клик исполнен — выбран моб"));
	});

	// ── ★ R5-fix8 (14.11.2026): ШОВ ГЕЙТА МИРОВОГО КЛИКА — при подменённой точке клика гейт «над окном интерфейса» берёт её, а не
	//   курсор ОС (`AAsdaPlayerController::IsCursorOverUi`). Клик в точку земли ПОД окном интерфейса гасится (4006 нет) при любом
	//   положении мыши; клик в свободную точку земли проходит. До шва итог зависел от того, где лежала мышь пользователя.
	Add(TEXT("шов гейта: клик в точку под окном интерфейса гасится, в свободную — проходит"), 6.f, [S]()
	{
		S->Prepare(1);
		S->RestoreUi();   // шагу нужен показанный интерфейс: если сторож точки клика скрыл его раньше — вернуть (замечание ревизора)
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		AAsdaPlayerController* P = S->PC();
		S->Expect(P != nullptr, TEXT("контроллер есть"));
		if (!P) { return; }
		FVector2D VpSize(1024.0, 768.0);   // размер вьюпорта игры
		if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(VpSize); }
		bool bUnder = false; FVector2D Under = FVector2D::ZeroVector;   // точка земли под окном интерфейса
		for (int32 iy = 1; iy < 24 && !bUnder; ++iy)
		{
			for (int32 ix = 1; ix < 32 && !bUnder; ++ix)
			{
				const FVector2D Q(VpSize.X * ix / 32.0, VpSize.Y * iy / 24.0);
				AActor* Hit = nullptr;
				if (S->IsOverUi(Q) && AsdaSkillCastLock::DebugPickKindAt(P, Q, Hit) == (uint8)AAsdaPlayerController::EAsdaPickKind::Ground) { Under = Q; bUnder = true; }
			}
		}
		S->Expect(bUnder, TEXT("найдена точка земли под окном интерфейса"));
		if (!bUnder) { return; }
		const int32 Before = S->Count4006();
		AsdaSkillCastLock::DebugWorldClick(P, Under, false);
		S->Expect(S->Count4006() == Before, FString::Printf(TEXT("клик в точку (%.0f,%.0f) под окном интерфейса погашен гейтом: 4006 %d → %d (ждём без изменений)"),
			Under.X, Under.Y, Before, S->Count4006()));
		bool bFree = false; FVector2D Free = FVector2D::ZeroVector, FreeGame = FVector2D::ZeroVector;   // свободная точка земли
		for (const FIntPoint D : { FIntPoint(3, 0), FIntPoint(0, 3), FIntPoint(-3, 0), FIntPoint(0, -3), FIntPoint(3, 3), FIntPoint(-3, 3), FIntPoint(4, 1), FIntPoint(1, 4) })
		{
			if (S->ScreenOfTile(S->HomeTile + D, Free, FreeGame)) { bFree = true; break; }
		}
		S->Expect(bFree, TEXT("найдена свободная точка земли (не под окном)"));
		if (!bFree) { return; }
		AsdaSkillCastLock::DebugWorldClick(P, Free, false);
		S->Expect(S->Count4006() == Before + 1, FString::Printf(TEXT("клик в свободную точку земли прошёл: 4006 %d → %d (ждём +1)"), Before, S->Count4006()));
	});

	Add(TEXT("итог"), 3.f, [S]()
	{
		S->Prepare(1);
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kRidB); S->Feed(4011, B);
		// R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: пакет 4011 удаляет только игроков (оригинал `Recv_LogOutPacket@0xF3AE00`) — тестовый моб оставался в мире и доставался следующему тесту той же игры; актор удаляется явно.
		if (AAsdaPlayerController* Pc = S->PC()) { Pc->SetTarget(nullptr); }
		if (AAsdaMobActor* Mm = S->Mob(kRidB)) { Mm->Destroy(); }
	}, [S]() { return S->El() > 0.4; }, [S, Net]()
	{
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		S->RestoreUi();   // ★ R5-fix7: интерфейс, скрытый из-за курсора ОС над окном, — вернуть
		Net->bInGameConnected = S->bWasInGame;
		Net->ClearMoveRequestInFlight(TEXT("самотест очереди кликов: итог"));
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaClickQueueTest, kTag, S->Ok, S->Fail);
	});

	// ── спавн и исполнитель (как у `asda.Skill.SelfTestCastLock`) ──
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H; TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get(); const TSharedPtr<FState> Sg = WSg.Pin();
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
		if (!St->Steps.IsValidIndex(St->StepIdx)) { Wd->GetTimerManager().ClearTimer(St->Tick); AsdaSelfTestKit::Quit(Wd, St->bQuit); return; }
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaClickQueueTest, Log, TEXT("%s шаг %s"), kTag, *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestClickQueue(
	TEXT("asda.Input.SelfTestClickQueue"),
	TEXT("Самотест «частые клики не теряются» (05.11): серии кликов по земле 60/120/250/450 мс при RTT 120 мс — идёт в последнюю точку, 4006 "
	     "в пути не больше одного и не чаще 150 мс; двойной в той же точке — один 4006; клик по мобу при 4006 в пути; контроль asda.Input.ClickQueue 0. "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Input.SelfTestClickQueue"));
		FAsdaClickQueueSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
