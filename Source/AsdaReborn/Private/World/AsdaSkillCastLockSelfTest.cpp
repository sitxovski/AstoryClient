// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ВО ВРЕМЯ НАВЫКА НЕ ХОДИТ, НЕ МЕНЯЕТ ЦЕЛЬ, НЕ ПЬЁТ ЗЕЛЬЕ» `asda.Skill.SelfTestCastLock` (05.11.2026, сессия skill-cast-lock).
//   [ПОРТ] Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket` (= `HandlePacket` сокета: 5257/5270/5271/4053/
//   4056/5272/5268/5029), исходящие — перехват (`AsdaSelfTestKit::ArmCapture`: настоящие отправители 4006/4024/5028/5256). Путь игрока:
//   навык — `DispatchByKind` (вход быстрой панели), мировой клик — `OnLeftClick` с подменённой экранной точкой (как мышь), клавиши —
//   `ExecuteCommand`, зелье — `UseByDoubleClick`/`UseFromBagWindow` (панель/окно сумки), приказ карты — `MoveToGamePoint`, эмоция —
//   `PlayEmotion`. Разбор эталона — `World/AsdaSkillCastLock.h`.
//   Сценарии: контроль ключей 0 (прежние дефекты видны); земля в 25 — без хода; земля в 24 — ход после клипа; моб и Tab в 24 — цель
//   после клипа; зелье в 24 (панель) и в 25 (сумка) — после навыка; каст 5270 (14): земля игнорирована, моб — после 5271; отказ 5257 —
//   отложенное сразу; смерть 4053 посреди клипа — всё сброшено; оглушение 5272 посреди клипа — ход снят; рывок 537 — клик после рывка;
//   приказ карты в 24 — после клипа; эмоция в 24 — нет; состояние действия для клавиш хода 25/24.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "World/AsdaSkillCastLock.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"      // выбор расходника
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"           // ячейка с зельем
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"      // R5-fix7/8: точка клика под окном интерфейса — интерфейс скрыть на время прогона
#include "UI/AsdaWindowSubsystem.h"            // R5-fix7
#include "UI/AsdaCharaInfoSubsystem.h"         // эмоция
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWalkGrid.h"               // проходимость клетки клика
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"          // R5-fix8: окно игры — экранная точка клика в координатах Slate
#include "Widgets/SWindow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCastLockTest, Log, All);

namespace AsdaCastLockTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestCastLock");   // имя команды
	const TCHAR* const kTag = TEXT("САМОТЕСТ-НАВЫК-ЗАНЯТ");          // префикс строк журнала
	constexpr int32 kSelfFallback = 4324;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kRidA = 31301;           // цель навыка (4 клетки)
	constexpr int32 kRidB = 31302;           // вторая цель (ближе — её даёт «ближайший враг»)
	constexpr int32 kSkill = 705;            // «Многократный выстрел» (лук): ответ 5257, клип
	constexpr int32 kCast = 787;             // «Зазубренная стрела» (лук): 5270 → 5271, каст 1200 мс
	constexpr int32 kDash = 537;             // «Натиск»: рывок к цели по 5257
	constexpr int32 kCell = 5;               // ячейка расходников (контейнер 2) с зельем

	struct FOut { uint16 Op = 0; double T = 0.0; };   // перехваченный исходящий и момент отправки

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
		uint32 Gen = 0; TArray<FOut> Outs;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false, bWasStand = false;
		int32 Self = -1; FVector2D Home = FVector2D::ZeroVector; FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 WasWeaponSub = 99;
		int32 Potion = 0, Potion2 = 0;       // два расходника (разные коды)
		TArray<uint8> SavedCell;             // прежняя запись ячейки (вернуть в итоге)
		// Наблюдение эпизода
		int32 Phase = 0;                     // шаг сценария
		double TAct = -1.0, TFree = -1.0;    // момент действия игрока / освобождения навыка
		int32 OutAtAct = 0;                  // индекс исходящих на момент действия
		int32 ExecAtAct = 0, DropAtAct = 0;  // счётчики гейта на момент действия
		int32 PhaseAtAct = -1;               // фаза навыка в момент действия
		int32 PendAtAct = -1; bool bItemAtAct = false;   // слоты сразу после действия
		bool bMovedDuring = false;           // персонаж пошёл, пока навык не окончен
		bool bTgtChangedDuring = false;      // цель сменилась, пока навык не окончен
		TWeakObjectPtr<AActor> TgtAtAct;     // цель в момент действия
		FVector2D GroundGame = FVector2D::ZeroVector;   // точка клика по земле (game XZ)
		bool bEmoteOkDuring = true;          // эмоция во время навыка сыграла
		int32 ActStateAt25 = -1, ActStateAt24 = -1;      // состояние действия для клавиш

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		UAsdaInventorySubsystem* Inv() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		double Now() const { return FPlatformTime::Seconds(); }
		int32 SkPhase() const { return AsdaSkillCastLock::SelfSkillPhase(GI()); }
		AActor* Tgt() const { const AAsdaPlayerController* P = PC(); return P ? P->TargetActor.Get() : nullptr; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaCastLockTest, Log, TEXT("%s %s %s: %s"), kTag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish` (RestoreAll)
		// Исходящие опкода Op начиная с индекса From (и момент первого).
		int32 Count(int32 Op, int32 From, double* OutFirstT = nullptr) const
		{
			int32 C = 0;
			for (int32 i = From; i < Outs.Num(); ++i) { if (Outs[i].Op == Op) { if (C == 0 && OutFirstT) { *OutFirstT = Outs[i].T; } ++C; } }
			return C;
		}
		// Сброс между сценариями: стоим дома, маршрута и боя нет, навык свободен, цель — A.
		void Prepare()
		{
			AAsdaPlayerController* P = PC(); AAsdaPlayerCharacter* C = Ch();
			AsdaSkillCastLock::Reset(TEXT("самотест: новый сценарий"));
			if (P)
			{
				P->bAutoAttack = false; P->bCombatStarted = false; P->bInCombatStance = false; P->bWaitingCombatAck = false;
				P->CancelRoute();
				P->ExitCombatStance();
			}
			if (UAsdaNetSubsystem* Nn = N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест навык занят: сброс сценария")); Nn->bSelfDead = false; }
			if (C) { C->StopMove(); C->StopAttackAnim(); C->SkillAnimUntilS = 0.0; C->SkillFaceUntilS = 0.0; C->NetSnap(Home); C->EquippedWeaponSubType = 10; }
			if (UAsdaSkillUseSubsystem* U = SU()) { U->DebugSetCastState(0, -1); }
			// Мобы пропадают после воскрешения 4056 (мир переспавнивается) — вернуть недостающих.
			if (!Mob(kRidA)) { Feed(4013, AsdaTestPackets::MobSpawn4013(kRidA, kMobType, HomeTile + FIntPoint(4, 0), 1, kSpdDenom)); }
			if (!Mob(kRidB)) { Feed(4013, AsdaTestPackets::MobSpawn4013(kRidB, kMobType, HomeTile + FIntPoint(-2, 1), 1, kSpdDenom)); }
			if (P) { P->SetTarget(Mob(kRidA)); }
			Phase = 0; TAct = TFree = -1.0; OutAtAct = Outs.Num(); ExecAtAct = AsdaSkillCastLock::DebugExecutedCount();
			DropAtAct = AsdaSkillCastLock::DebugDroppedCount(); PhaseAtAct = -1; PendAtAct = -1; bItemAtAct = false;
			bMovedDuring = false; bTgtChangedDuring = false; TgtAtAct = nullptr; bEmoteOkDuring = true;
		}
		// Момент действия игрока: снимок счётчиков.
		void MarkAct()
		{
			TAct = Now(); OutAtAct = Outs.Num(); PhaseAtAct = SkPhase(); TgtAtAct = Tgt();
			ExecAtAct = AsdaSkillCastLock::DebugExecutedCount(); DropAtAct = AsdaSkillCastLock::DebugDroppedCount();
		}
		void NoteSlots() { PendAtAct = AsdaSkillCastLock::DebugPendingActionKind(); bItemAtAct = AsdaSkillCastLock::DebugHasPendingItem(); }
		// Пока навык не окончен — следим, не пошёл ли персонаж и не сменилась ли цель.
		void Watch()
		{
			if (TAct < 0.0 || TFree >= 0.0) { return; }
			if (SkPhase() != 0)
			{
				if (const AAsdaPlayerCharacter* C = Ch(); C && C->IsMoving()) { bMovedDuring = true; }
				if (Tgt() != TgtAtAct.Get()) { bTgtChangedDuring = true; }
			}
			else { TFree = Now(); }
		}
		// Экранная точка, в которой мировой клик попадает в актора A (кольцо вокруг проекции центра).
		bool ScreenOfActor(AActor* A, FVector2D& Out) const
		{
			AAsdaPlayerController* P = PC();
			if (!P || !A) { return false; }
			int32 VX = 0, VY = 0; P->GetViewportSize(VX, VY);
			FVector Org, Ext; A->GetActorBounds(false, Org, Ext);
			FVector2D S = FVector2D::ZeroVector;
			if (!P->ProjectWorldLocationToScreen(Org, S)) { return false; }
			for (int32 R = 0; R <= 60; R += 3)
			{
				for (int32 dy = -R; dy <= R; dy += 3)
				{
					for (int32 dx = -R; dx <= R; dx += 3)
					{
						if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R) { continue; }
						const FVector2D Q(S.X + dx, S.Y + dy);
						if (Q.X < 4 || Q.Y < 4 || Q.X > VX - 4 || Q.Y > VY - 4) { continue; }
						AActor* Hit = nullptr;
						const uint8 K = AsdaSkillCastLock::DebugPickKindAt(P, Q, Hit);
						if (K == (uint8)AAsdaPlayerController::EAsdaPickKind::Mob && Hit == A) { Out = Q; return true; }
					}
				}
			}
			return false;
		}
		// Экранная точка земли в DTiles клетках от дома (попадание — «земля»); OutGame — точка попадания.
		bool ScreenOfGround(const FIntPoint& DTiles, FVector2D& Out, FVector2D& OutGame) const
		{
			AAsdaPlayerController* P = PC(); const AAsdaPlayerCharacter* C = Ch();
			if (!P || !C) { return false; }
			const FVector2D G = AsdaCoord::TileCenterGame(HomeTile + DTiles);
			if (C->WalkGrid && !C->WalkGrid->IsWorldWalkable(G.X, G.Y)) { return false; }   // непроходимая клетка — клик дал бы 1013, берём соседнюю
			const FVector2D U = AsdaCoord::GameToUe2D(G);
			const FVector W(U.X, U.Y, C->GetActorLocation().Z - C->GetSimpleCollisionHalfHeight());
			FVector2D S = FVector2D::ZeroVector;
			if (!P->ProjectWorldLocationToScreen(W, S)) { return false; }
			AActor* Hit = nullptr;
			const uint8 K = AsdaSkillCastLock::DebugPickKindAt(P, S, Hit);
			if (K != (uint8)AAsdaPlayerController::EAsdaPickKind::Ground) { return false; }
			Out = S; OutGame = G;
			return true;
		}
		// Клик по земле путём мыши.
		bool ClickGround(const FIntPoint& DTiles)
		{
			FVector2D S = FVector2D::ZeroVector;
			// Первая клетка может оказаться под персонажем/мобом или за краем экрана (рывок сдвинул камеру) — соседние.
			bool bFound = false;
			for (int32 R = 0; R <= 5 && !bFound; ++R)   // кольца вокруг DTiles; дом в 0 клеток не берём (там сам персонаж)
			{
				for (int32 dy = -R; dy <= R && !bFound; ++dy)
				{
					for (int32 dx = -R; dx <= R && !bFound; ++dx)
					{
						const FIntPoint T = DTiles + FIntPoint(dx, dy);
						if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R || FMath::Max(FMath::Abs(T.X), FMath::Abs(T.Y)) < 2) { continue; }
						bFound = ScreenOfGround(T, S, GroundGame);
					}
				}
			}
			if (!bFound) { Expect(false, TEXT("точка земли под клик не найдена (проекция)")); return false; }
			GuardPoint(S);
			AsdaSkillCastLock::DebugWorldClick(PC(), S, false);
			return true;
		}
		// Клик по мобу путём мыши (одиночный).
		bool ClickMob(int32 Rid)
		{
			FVector2D S = FVector2D::ZeroVector;
			if (!ScreenOfActor(Mob(Rid), S)) { Expect(false, FString::Printf(TEXT("экранная точка моба %d не найдена"), Rid)); return false; }
			GuardPoint(S);
			AsdaSkillCastLock::DebugWorldClick(PC(), S, false);
			return true;
		}
		// Заявка навыка путём игрока (быстрая панель): цель A, лук.
		void Press(int32 Code)
		{
			AAsdaPlayerController* P = PC(); UAsdaSkillUseSubsystem* U = SU();
			if (!P || !U) { return; }
			P->SetTarget(Mob(kRidA));
			U->DispatchByKind(Code, -1);
		}
		bool Sent5256() const { return Count(5256, OutAtAct) > 0; }
		// ★ R5-fix7/R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: гейт мирового клика «над окном интерфейса» при подменённой точке клика
		//   берёт ЭТУ точку (шов R5-fix8 в `AAsdaPlayerController::IsCursorOverUi`; раньше — настоящий курсор ОС, и итог теста
		//   зависел от положения мыши пользователя). Точка клика теста задаётся геометрией сцены (клетка, моб) и может лежать под
		//   окном интерфейса — такой клик игра законно гасит. Тест проверяет разбор клика по миру, а не этот гейт: если точка под
		//   окном, интерфейс скрывается до конца прогона (хит-тест окон молчит), как в самотесте зон рыбалки.
		bool bUiHiddenForCursor = false;   // интерфейс скрыт тестом — точка клика лежала под окном (вернуть в конце)
		// Абсолютная точка экрана Slate по точке вьюпорта игры.
		static FVector2D ClickAbs(const FVector2D& ScreenPx)
		{
			const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
			const TSharedPtr<SWindow> Win = VP ? VP->GetWindow() : nullptr;
			return (Win.IsValid() ? FVector2D(Win->GetWindowGeometryInScreen().GetAbsolutePosition()) : FVector2D::ZeroVector) + ScreenPx;
		}
		// Перед кликом теста: точка клика под окном интерфейса — скрыть интерфейс до конца прогона.
		void GuardPoint(const FVector2D& ScreenPx)
		{
			const UAsdaWindowSubsystem* WM = GI() ? GI()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			FName Zn;
			if (bUiHiddenForCursor || !WM || !WM->IsScreenPointOverAnyUiWindow(ClickAbs(ScreenPx), Zn)) { return; }
			UAsdaUiVisibilitySubsystem* Uv = GI() ? GI()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
			if (!Uv) { return; }
			Uv->SetUiShown(false, TEXT("самотест: точка клика под окном интерфейса"));
			bUiHiddenForCursor = true;
			UE_LOG(LogAsdaCastLockTest, Warning, TEXT("  точка клика под окном «%s» — интерфейс скрыт до конца прогона (геометрия сцены теста)"), *Zn.ToString());
		}
		// В конце прогона: вернуть интерфейс, если его скрывал тест.
		void RestoreUi()
		{
			if (!bUiHiddenForCursor) { return; }
			if (UAsdaUiVisibilitySubsystem* Uv = GI() ? GI()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr) { Uv->SetUiShown(true, TEXT("самотест: возврат интерфейса")); }
			bUiHiddenForCursor = false;
		}
	};

	// 5257 (8 + 29·N): [0] код, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень; запись по мобу.
	static TArray<uint8> Body5257(int32 Res, int32 Caster, int32 Skill, int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(Res == 1 ? 8 + 29 : 8);
		B[0] = (uint8)Res; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		if (Res == 1)
		{
			uint8* R = B.GetData() + 8; R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI32(R + 3, 1); R[7] = 0x01;
			AsdaBytes::WrI32(R + 11, 900); AsdaBytes::WrI16(R + 19, -1);
		}
		return B;
	}
	// 5270 (7 + 29·N): [0..1] кастер, [2..3] навык, [4..5] уровень, [6] записей; запись по мобу.
	static TArray<uint8> Body5270(int32 Caster, int32 Skill, int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(7 + 29);
		AsdaBytes::WrI16(&B[0], Caster); AsdaBytes::WrI16(&B[2], Skill); AsdaBytes::WrI16(&B[4], 1); B[6] = 1;
		uint8* R = B.GetData() + 7; R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}
	// 5271 (7 б): [0] код, [1..2] кастер, [3..4] навык, [5..6] флаг.
	static TArray<uint8> Body5271(int32 Res, int32 Caster, int32 Skill)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		B[0] = (uint8)Res; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); AsdaBytes::WrI16(&B[5], 1);
		return B;
	}
	// 4053 — смерть: i16 userIdx, i16 killerIdx.
	static TArray<uint8> Body4053(int32 U) { TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], U); AsdaBytes::WrI16(&B[2], -1); return B; }
	// 4056 — воскрешение на месте: uidx@0, тайл @2/@4, HP@6, MP@10.
	static TArray<uint8> Body4056(int32 U, const FIntPoint& T)
	{
		TArray<uint8> B; B.SetNumZeroed(22); AsdaBytes::WrI16(&B[0], U); AsdaBytes::WrI16(&B[2], T.X); AsdaBytes::WrI16(&B[4], T.Y);
		AsdaBytes::WrI32(&B[6], 300); AsdaBytes::WrI16(&B[10], 50); return B;
	}
	// 5272 (14 б): userIdx@0, состояние@2, вид@9 = 1, остаток с@11.
	static TArray<uint8> Body5272(int32 U, int32 St, int32 Sec)
	{
		TArray<uint8> B; B.SetNumZeroed(14); AsdaBytes::WrI16(&B[0], U); AsdaBytes::WrI16(&B[2], St); B[9] = 1; AsdaBytes::WrI16(&B[11], Sec); return B;
	}
	// 5268: снятие состояния.
	static TArray<uint8> Body5268(int32 U, int32 St) { TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], U); AsdaBytes::WrI16(&B[2], St); return B; }
	// 5251 — изученные навыки.
	using AsdaTestPackets::Body5251;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// Ответ «неизвестный код» на 5028/5000 — снимает ожидание ответа инвентаря (как у `asda.Item.SelfTestStateLockGate`).
	static void ClearInvPending(FState& S)
	{
		TArray<uint8> U; U.SetNumZeroed(24); U[0] = 99; S.Feed(5029, U);
		TArray<uint8> M; M.SetNumZeroed(24); M[0] = 99; S.Feed(5001, M);
	}
	// Два расходника (тип 11, обычное применение) — выбор как у самотеста гейта 7007.
	static void PickPotions(FState& S)
	{
		const UAsdaItemTableSubsystem* IT = S.GI() ? S.GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IT) { return; }
		static const TSet<int32> kSkipSub = { 8, 9, 12, 15, 16, 32, 33, 34, 38, 64, 106, 107, 200, 201, 202, 203, 204, 205, 206, 208, 209, 210 };
		for (int32 Code = 2001; Code <= 79999 && !S.Potion2; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (!R || R->ItemType != 11 || R->LinkCode <= 0 || R->LinkCode == 101 || R->LinkCode == 102 || kSkipSub.Contains(R->ItemSubType)) { continue; }
			if (IT->IsBoosterItem(Code) || (R->ItemSubType >= 0x2C && R->ItemSubType <= 0x35) || R->TooltipFlag200 == 1) { continue; }
			if (!S.Potion) { S.Potion = Code; } else if (Code != S.Potion) { S.Potion2 = Code; }
		}
	}
}

struct FAsdaSkillCastLockSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaSkillCastLockSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaCastLockTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4024, 5028, 5256 };
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
	auto SetAllKeys = [S](int32 V)
	{
		S->SetKey(TEXT("asda.Skill.CastLockMove"), V); S->SetKey(TEXT("asda.Skill.CastLockTarget"), V);
		S->SetKey(TEXT("asda.Skill.CastLockItems"), V); S->SetKey(TEXT("asda.Skill.CastLockEmote"), V);
	};
	auto PutPotion = [S](int32 Code)
	{
		if (UAsdaInventorySubsystem* I = S->Inv()) { I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, 2, kCell, 50), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		ClearInvPending(*S);
	};

	// ── 0. подготовка ──
	Add(TEXT("подготовка"), 6.f, [S, Net, SetAllKeys]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* P = S->PC();
		if (!C || !P || !S->WS() || !S->Inv()) { S->Expect(false, TEXT("нет своего персонажа/мира/инвентаря после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			FOut& O = St->Outs.AddDefaulted_GetRef(); O.Op = Op; O.T = FPlatformTime::Seconds();
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = 10;
		S->Feed(5251, Body5251({ kSkill, kCast, kDash }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест навык занят"), true);
		S->SetKey(TEXT("asda.Skill.LatchGuard"), 1);
		SetAllKeys(1);
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRidA, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom));
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRidB, kMobType, S->HomeTile + FIntPoint(-2, 1), 1, kSpdDenom));
		PickPotions(*S);
		if (const FAsdaItemInstance* X = S->Inv()->GetCell(2, kCell)) { if (!X->IsEmpty()) { S->SavedCell = X->Raw; } }
	}, [S]() { return S->El() > 1.0 && S->Mob(kRidA) && S->Mob(kRidB); }, [S]()
	{
		S->Expect(S->Mob(kRidA) && S->Mob(kRidB), TEXT("мобы A и B на месте"));
		S->Expect(S->Potion > 0 && S->Potion2 > 0, FString::Printf(TEXT("расходники: %d и %d"), S->Potion, S->Potion2));
		FVector2D Sc = FVector2D::ZeroVector, G = FVector2D::ZeroVector;
		const bool bG = S->ScreenOfGround(FIntPoint(0, 3), Sc, G);
		S->Expect(bG, FString::Printf(TEXT("клик по земле в 3 клетках попадает в землю (%.0f,%.0f px)"), Sc.X, Sc.Y));
		const bool bM = S->ScreenOfActor(S->Mob(kRidB), Sc);
		S->Expect(bM, FString::Printf(TEXT("клик по мобу B попадает в моба (%.0f,%.0f px)"), Sc.X, Sc.Y));
	});

	// Общий эпизод навыка 705: заявка путём игрока → 5256 → (Resp) → в нужной фазе Act → ждём конца навыка (+Tail с).
	//   ActPhase 25 — сразу после 5256 (до ответа); 24 — после 5257 успеха.
	auto AddEpisode = [S, Add](const FString& Name, int32 ActPhase, TFunction<void()> Setup, TFunction<void()> Act, TFunction<void()> Check,
		float Tail = 0.6f, TFunction<void()> AfterAct = nullptr)
	{
		Add(Name, 10.f, [S, Setup]() { S->Prepare(); if (Setup) { Setup(); } S->Press(kSkill); }, [S, ActPhase, Act, Tail, AfterAct]()
		{
			S->Watch();
			if (S->Phase == 0 && S->Sent5256())
			{
				if (ActPhase == 25)
				{
					S->ActStateAt25 = AsdaSkillCastLock::DebugActionState(S->PC());
					S->MarkAct(); Act(); S->NoteSlots();
					S->Feed(5257, Body5257(1, S->Self, kSkill, kRidA));   // ответ после действия: 25 → 24 → клип
					if (AfterAct) { AfterAct(); }
					S->Phase = 2;
				}
				else
				{
					S->Feed(5257, Body5257(1, S->Self, kSkill, kRidA));
					S->Phase = 1;
				}
				return false;
			}
			if (S->Phase == 1 && S->SkPhase() == 24)
			{
				S->ActStateAt24 = AsdaSkillCastLock::DebugActionState(S->PC());
				S->MarkAct(); Act(); S->NoteSlots();
				if (AfterAct) { AfterAct(); }
				S->Phase = 2;
				return false;
			}
			return S->Phase == 2 && S->TFree > 0.0 && S->Now() - S->TFree > Tail;
		}, MoveTemp(Check));
	};

	// ── 1. контроль: ключи 0 — прежнее поведение порта (дефекты видны) ──
	AddEpisode(TEXT("1 контроль ключей 0: клик по земле в клипе — ход сразу"), 24, [SetAllKeys]() { SetAllKeys(0); },
		[S]() { S->ClickGround(FIntPoint(0, 3)); }, [S, SetAllKeys]()
	{
		double T1 = 0.0;
		const int32 N = S->Count(4006, S->OutAtAct, &T1);
		S->Expect(S->PhaseAtAct == 24, FString::Printf(TEXT("клик пришёлся на клип навыка (фаза %d)"), S->PhaseAtAct));
		S->Expect(N > 0 && (S->TFree < 0.0 || T1 < S->TFree), FString::Printf(TEXT("ключ 0: 4006 ушёл посреди клипа (%d) — прежний дефект воспроизведён"), N));
		SetAllKeys(1);
	});
	AddEpisode(TEXT("1б контроль ключей 0: клик по мобу B в клипе — цель сразу B"), 24, [SetAllKeys]() { SetAllKeys(0); },
		[S]() { S->ClickMob(kRidB); }, [S, SetAllKeys]()
	{
		S->Expect(S->bTgtChangedDuring || S->Tgt() == S->Mob(kRidB), TEXT("ключ 0: цель сменилась на B посреди клипа — прежнее поведение воспроизведено"));
		SetAllKeys(1);
	});
	AddEpisode(TEXT("1в контроль ключей 0: зелье в клипе — 5028 сразу"), 24, [S, SetAllKeys, PutPotion]() { SetAllKeys(0); PutPotion(S->Potion); },
		[S]() { if (UAsdaInventorySubsystem* I = S->Inv()) { I->UseByDoubleClick(2, kCell); } }, [S, SetAllKeys]()
	{
		double T1 = 0.0;
		const int32 N = S->Count(5028, S->OutAtAct, &T1);
		S->Expect(N == 1 && (S->TFree < 0.0 || T1 < S->TFree), FString::Printf(TEXT("ключ 0: 5028 ушёл посреди клипа (%d) — прежнее поведение"), N));
		ClearInvPending(*S);
		SetAllKeys(1);
	});

	// ── 2. земля в 25 (заявка в пути) — без хода, без очереди ──
	AddEpisode(TEXT("2 земля в 25 — без хода"), 25, nullptr, [S]() { S->ClickGround(FIntPoint(0, 3)); }, [S]()
	{
		S->Expect(S->PhaseAtAct == 25, FString::Printf(TEXT("клик пришёлся на заявку (фаза %d)"), S->PhaseAtAct));
		S->Expect(S->Count(4006, S->OutAtAct) == 0, FString::Printf(TEXT("4006 не ушёл ни во время навыка, ни после (%d)"), S->Count(4006, S->OutAtAct)));
		S->Expect(S->PendAtAct == 0 && AsdaSkillCastLock::DebugDroppedCount() > S->DropAtAct, TEXT("клик отброшен, не отложен (эталон input.c:568)"));
		S->Expect(!S->bMovedDuring, TEXT("персонаж не пошёл"));
		S->Expect(S->ActStateAt25 == 25, FString::Printf(TEXT("состояние действия для клавиш хода в заявке = %d (ждём 25)"), S->ActStateAt25));
	});

	// ── 3. земля в 24 (клип) — ход сразу после клипа ──
	AddEpisode(TEXT("3 земля в 24 — ход после клипа"), 24, nullptr, [S]() { S->ClickGround(FIntPoint(0, 3)); }, [S]()
	{
		double T1 = 0.0;
		const int32 N = S->Count(4006, S->OutAtAct, &T1);
		S->Expect(S->PhaseAtAct == 24 && S->PendAtAct == 1, FString::Printf(TEXT("клик в клипе отложен (фаза %d, слот %d)"), S->PhaseAtAct, S->PendAtAct));
		S->Expect(!S->bMovedDuring, TEXT("во время клипа персонаж не пошёл (клип не оборван)"));
		S->Expect(N >= 1 && T1 >= S->TFree - 0.02, FString::Printf(TEXT("4006 ушёл после конца клипа (%d, через %.3f с)"), N, N ? T1 - S->TFree : -1.0));
		S->Expect(N >= 1 && T1 - S->TFree < 0.25, FString::Printf(TEXT("ход начат без задержки: %.3f с после клипа (ждём < 0.25)"), N ? T1 - S->TFree : -1.0));
		FVector2D Fin = FVector2D::ZeroVector;
		const bool bRoute = AsdaSkillCastLock::DebugRouteFinal(S->PC(), Fin);
		const AAsdaPlayerCharacter* C = S->Ch();
		const float D = bRoute ? FVector2D::Distance(Fin, S->GroundGame) : (C ? FVector2D::Distance(C->GetGamePosXZ(), S->GroundGame) : -1.f);
		S->Expect(D >= 0.f && D < 150.f, FString::Printf(TEXT("идёт в точку клика (отклонение %.0f юн)"), D));
		S->Expect(S->ActStateAt24 == 24, FString::Printf(TEXT("состояние действия для клавиш хода в клипе = %d (ждём 24)"), S->ActStateAt24));
	}, 0.6f);

	// ── 4. клик по мобу B в 24 — цель после клипа ──
	AddEpisode(TEXT("4 клик по мобу B в 24 — цель после клипа"), 24, nullptr, [S]() { S->ClickMob(kRidB); }, [S]()
	{
		S->Expect(S->PendAtAct == 1, FString::Printf(TEXT("клик по мобу отложен (слот %d)"), S->PendAtAct));
		S->Expect(!S->bTgtChangedDuring, TEXT("во время клипа цель осталась A"));
		S->Expect(S->Tgt() == S->Mob(kRidB), TEXT("после клипа выбрана цель B"));
		S->Expect(S->Count(4006, S->OutAtAct) == 0 || !S->bMovedDuring, TEXT("во время клипа хода не было"));
	});

	// ── 5. Tab (60 «ближайший враг») в 24 — после клипа ──
	AddEpisode(TEXT("5 клавиша 60 в 24 — цель после клипа"), 24, nullptr, [S]() { AsdaSkillCastLock::DebugExecuteCommand(S->PC(), 60); }, [S]()
	{
		S->Expect(S->PendAtAct == 3, FString::Printf(TEXT("клавиша отложена (слот %d)"), S->PendAtAct));
		S->Expect(!S->bTgtChangedDuring, TEXT("во время клипа цель осталась A"));
		S->Expect(S->Tgt() == S->Mob(kRidB), TEXT("после клипа «ближайший враг» выбрал B"));
	});

	// ── 6. зелье с панели в 24 — после клипа ──
	AddEpisode(TEXT("6 зелье с панели в 24 — после клипа"), 24, [S, PutPotion]() { PutPotion(S->Potion); },
		[S]() { if (UAsdaInventorySubsystem* I = S->Inv()) { I->UseByDoubleClick(2, kCell); } }, [S]()
	{
		double T1 = 0.0;
		const int32 N = S->Count(5028, S->OutAtAct, &T1);
		S->Expect(S->bItemAtAct, TEXT("зелье отложено"));
		S->Expect(N == 1 && T1 >= S->TFree - 0.02, FString::Printf(TEXT("5028 ушёл один раз и после клипа (%d, через %.3f с)"), N, N ? T1 - S->TFree : -1.0));
		ClearInvPending(*S);
	});

	// ── 7. зелье из сумки в 25 + клик по мобу B — после отказа сервера ──
	Add(TEXT("7 отказ 5257 — отложенные зелье и клик исполняются сразу"), 8.f, [S, PutPotion]() { S->Prepare(); PutPotion(S->Potion2); S->Press(kSkill); }, [S]()
	{
		S->Watch();
		if (S->Phase == 0 && S->Sent5256())
		{
			S->MarkAct();
			if (UAsdaInventorySubsystem* I = S->Inv()) { I->UseFromBagWindow(2, kCell); }
			S->ClickMob(kRidB);
			S->NoteSlots();
			S->Feed(5257, Body5257(0, S->Self, kSkill, kRidA));   // отказ сервера (код 0)
			S->Phase = 2;
			return false;
		}
		return S->Phase == 2 && S->TFree > 0.0 && S->Now() - S->TFree > 0.5;
	}, [S]()
	{
		S->Expect(S->PhaseAtAct == 25 && S->bItemAtAct && S->PendAtAct == 1, FString::Printf(TEXT("в заявке отложены зелье и клик (фаза %d, предмет %d, слот %d)"),
			S->PhaseAtAct, S->bItemAtAct ? 1 : 0, S->PendAtAct));
		S->Expect(S->Count(5028, S->OutAtAct) == 1, FString::Printf(TEXT("после отказа 5028 ушёл (%d)"), S->Count(5028, S->OutAtAct)));
		S->Expect(S->Tgt() == S->Mob(kRidB), TEXT("после отказа выбрана цель B"));
		ClearInvPending(*S);
	});

	// ── 8. каст 787 (5270 → 14): земля игнорирована, клик по B — после 5271 ──
	Add(TEXT("8 каст 5270: земля игнорирована, моб B — после каста"), 9.f, [S]() { S->Prepare(); S->Press(kCast); }, [S]()
	{
		S->Watch();
		if (S->Phase == 0 && S->Count(5256, S->OutAtAct) > 0) { S->Feed(5270, Body5270(S->Self, kCast, kRidA)); S->Phase = 1; return false; }
		if (S->Phase == 1 && S->SkPhase() == 14)
		{
			S->MarkAct();
			S->ClickGround(FIntPoint(0, 3));
			S->ClickMob(kRidB);
			S->NoteSlots();
			S->Phase = 2;
			return false;
		}
		if (S->Phase == 2 && S->Now() - S->TAct > 0.6) { S->Feed(5271, Body5271(1, S->Self, kCast)); S->Phase = 3; return false; }
		return S->Phase == 3 && S->TFree > 0.0 && S->Now() - S->TFree > 0.5;
	}, [S]()
	{
		S->Expect(S->PhaseAtAct == 14, FString::Printf(TEXT("клики пришлись на каст (фаза %d)"), S->PhaseAtAct));
		S->Expect(S->Count(4006, S->OutAtAct) == 0, FString::Printf(TEXT("4006 нет (%d): клик по земле в касте игнорирован"), S->Count(4006, S->OutAtAct)));
		S->Expect(S->PendAtAct == 1, TEXT("клик по мобу B отложен"));
		S->Expect(!S->bTgtChangedDuring && S->Tgt() == S->Mob(kRidB), TEXT("цель — B только после каста"));
	});

	// ── 10. оглушение посреди клипа — отложенный ход снят ──
	AddEpisode(TEXT("10 оглушение 5272 посреди клипа — ход снят"), 24, nullptr, [S]() { S->ClickGround(FIntPoint(0, 3)); }, [S]()
	{
		S->Expect(S->PendAtAct == 1, TEXT("до оглушения клик отложен"));
		S->Expect(S->Count(4006, S->OutAtAct) == 0, FString::Printf(TEXT("под оглушением отложенного хода нет (4006 %d)"), S->Count(4006, S->OutAtAct)));
		S->Feed(5268, Body5268(S->Self, 3));
	}, 0.6f, [S]() { S->Feed(5272, Body5272(S->Self, 3, 30)); });

	// ── 11. эмоция в клипе — не играет; приказ карты в клипе — после клипа ──
	AddEpisode(TEXT("11 эмоция и приказ карты в 24"), 24, nullptr, [S]()
	{
		if (UAsdaCharaInfoSubsystem* CI = S->GI()->GetSubsystem<UAsdaCharaInfoSubsystem>()) { S->bEmoteOkDuring = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 0)); }
		S->GroundGame = AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(3, 3));
		if (AAsdaPlayerController* P = S->PC()) { P->MoveToGamePoint(S->GroundGame); }
	}, [S]()
	{
		S->Expect(!S->bEmoteOkDuring, TEXT("эмоция во время клипа не сыграла"));
		S->Expect(S->PendAtAct == 2, FString::Printf(TEXT("приказ карты отложен (слот %d)"), S->PendAtAct));
		double T1 = 0.0;
		const int32 N4006 = S->Count(4006, S->OutAtAct, &T1);
		S->Expect(!S->bMovedDuring && N4006 >= 1 && T1 >= S->TFree - 0.02, FString::Printf(TEXT("ход по приказу карты — после клипа (4006 %d)"), N4006));
		FVector2D Fin = FVector2D::ZeroVector;
		S->Expect(AsdaSkillCastLock::DebugRouteFinal(S->PC(), Fin) && FVector2D::Distance(Fin, S->GroundGame) < 350.f,
			FString::Printf(TEXT("маршрут ведёт к точке приказа (%.0f юн; цель в стене путь сдвигает на соседнюю клетку)"), FVector2D::Distance(Fin, S->GroundGame)));
	});
	Add(TEXT("11б эмоция после навыка — играет"), 3.f, [S]() { S->Prepare(); }, [S]() { return S->El() > 0.3; }, [S]()
	{
		UAsdaCharaInfoSubsystem* CI = S->GI()->GetSubsystem<UAsdaCharaInfoSubsystem>();
		S->Expect(CI && CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 0)), TEXT("без навыка эмоция играет"));
	});

	// ── 12. рывок 537 (5257 с перемещением к цели): клик по земле во время рывка — после него ──
	Add(TEXT("12 рывок 537: клик по земле — после рывка"), 9.f, [S]() { S->Prepare(); S->Feed(5257, Body5257(1, S->Self, kDash, kRidA)); }, [S]()
	{
		S->Watch();
		if (S->Phase == 0 && S->SkPhase() == 24 && S->El() > 0.05)
		{
			S->MarkAct();
			S->ClickGround(FIntPoint(0, -3));
			S->NoteSlots();
			S->Phase = 2;
			return false;
		}
		return S->Phase == 2 && S->TFree > 0.0 && S->Now() - S->TFree > 0.6;
	}, [S]()
	{
		double T1 = 0.0;
		const int32 N = S->Count(4006, S->OutAtAct, &T1);
		S->Expect(S->PendAtAct == 1, TEXT("клик во время рывка отложен"));
		S->Expect(N == 0 || T1 >= S->TFree - 0.02, FString::Printf(TEXT("во время рывка 4006 нет (%d всего, первый через %.3f с после рывка)"), N, N ? T1 - S->TFree : -1.0));
		S->Expect(AsdaSkillCastLock::DebugExecutedCount() > S->ExecAtAct, TEXT("после рывка отложенный клик исполнен (ход — если клетка достижима с точки прибытия)"));
	});

	// ── 13. смерть посреди клипа — отложенное сброшено ──
	AddEpisode(TEXT("13 смерть 4053 посреди клипа — отложенное сброшено"), 24, [S, PutPotion]() { PutPotion(S->Potion); },
		[S]() { S->ClickGround(FIntPoint(0, 3)); if (UAsdaInventorySubsystem* I = S->Inv()) { I->UseByDoubleClick(2, kCell); } }, [S]()
	{
		S->Expect(S->PendAtAct == 1 && S->bItemAtAct, TEXT("до смерти клик и зелье отложены"));
		S->Expect(AsdaSkillCastLock::DebugPendingActionKind() == 0 && !AsdaSkillCastLock::DebugHasPendingItem(), TEXT("после смерти слоты пусты"));
		S->Expect(S->Count(4006, S->OutAtAct) == 0 && S->Count(5028, S->OutAtAct) == 0,
			FString::Printf(TEXT("мёртвый не пошёл и не выпил (4006 %d, 5028 %d)"), S->Count(4006, S->OutAtAct), S->Count(5028, S->OutAtAct)));
		S->Feed(4056, Body4056(S->Self, S->HomeTile));
		ClearInvPending(*S);
	}, 0.6f, [S]() { S->Feed(4053, Body4053(S->Self)); });

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S, Net]()
	{
		S->Prepare();
		for (const int32 R : { kRidA, kRidB }) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], R); S->Feed(4011, B); }
		// R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: пакет 4011 удаляет только игроков (оригинал `Recv_LogOutPacket@0xF3AE00`) — тестовый моб оставался в мире и доставался следующему тесту той же игры; актор удаляется явно.
		if (AAsdaPlayerController* Pc = S->PC()) { Pc->SetTarget(nullptr); }
		for (const int32 R : { kRidA, kRidB }) { if (AAsdaMobActor* Mm = S->Mob(R)) { Mm->Destroy(); } }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		if (UAsdaInventorySubsystem* I = S->Inv())
		{
			if (S->SavedCell.Num() >= 57) { I->ApplyExternalRecord(S->SavedCell, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { I->ClearCell(2, kCell); }
		}
		ClearInvPending(*S);
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		S->RestoreUi();   // ★ R5-fix7: интерфейс, скрытый из-за курсора ОС над окном, — вернуть
		Net->bInGameConnected = S->bWasInGame; Net->bStandCaptureSkillUse = S->bWasStand;
		Net->ClearMoveRequestInFlight(TEXT("самотест навык занят: итог"));
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaCastLockTest, kTag, S->Ok, S->Fail);
	});

	// ── спавн и исполнитель (как у `asda.Combat.SelfTestStunAttack`) ──
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
			UE_LOG(LogAsdaCastLockTest, Log, TEXT("%s шаг %s"), kTag, *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillCastLock(
	TEXT("asda.Skill.SelfTestCastLock"),
	TEXT("Самотест «во время навыка не ходит, не меняет цель, не пьёт зелье» (05.11): клик по земле в 25 — без хода, в 24 — ход после клипа; "
	     "клик по мобу и клавиша 60 — цель после навыка; зелье с панели/из сумки — после навыка; каст 5270; отказ 5257; смерть и оглушение "
	     "посреди клипа; рывок 537; эмоция и приказ карты; контроль ключей asda.Skill.CastLock* 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestCastLock"));
		FAsdaSkillCastLockSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
