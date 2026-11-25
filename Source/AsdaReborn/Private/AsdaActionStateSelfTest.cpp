// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ ЕДИНОГО СОСТОЯНИЯ ДЕЙСТВИЯ `asda.Action.SelfTest` (дедуп №2, волна В3, S7; 12.10.2026). [ПОРТ]
//
// ★ ЗАЧЕМ. «Занят» своего персонажа прежде определяли 4 признака (эмоция / рыбалка / 68 «добыча» / лопата 40/43), их гасили
//   друг другом вручную в 6 местах, преамбула «стоп хода / сидения / замаха» была в трёх копиях, правило «Alt/F10» фильтра
//   рыбалки — в трёх, «идёт рыбалка» — в 2 обёртках и 5 копиях. Теперь: одно поле `AAsdaPlayerCharacter::BusyAction`
//   (`EAsdaActionState`, 1:1 с `CChar_SetState@0xA13D50`), `SetBusyAction`/`StopMotionForAction`/`BeginEtalonAction`,
//   `AAsdaPlayerController::IsFishSysKey`/`ShouldPassFishSysKey`, `UAsdaFishingSubsystem::IsFishingNow`.
//   Отчёт: `Reports/dedup_audit2/wave3_S7.md`.
//
// ★ ЧТО ПРОВЕРЯЕТ (офлайн, свой персонаж, настоящие входы):
//   A. Цепочка эмоция 33 → 68 → лопата 40 → рыбалка 60 → эмоция 34 → ход: в каждый момент стоит РОВНО одно «занятое»
//      состояние, таймер прежнего снят, `KbEtalonActionState` = номер эталона (68 тоже), оружие за спиной, инструмент — только в 40/43.
//   B. Одна преамбула: ход, сидение и цикл замаха снимаются входом в 68 / 40 / 60.
//   C. ★ ИСПРАВЛЕНИЕ танцпола Alpen: в 68 и во время своей эмоции танец не запускается (4024 112/113 не уходит); контрольная
//      пара — ключ отката `asda.Action.LegacyBusyGates 1` (танец поверх 68, отказ эмоции → пауза 1 с); положительный контроль —
//      в покое танец идёт.
//   D. Правило «системной» клавиши рыбалки (Alt/F10) и пропуск по `asda.Fish.EatSysKeys`; единая точка «идёт рыбалка».
//
// Консоль: `asda.Action.SelfTest [delay=N] [quit]`. Только офлайн (`AsdaSelfTestKit::RefuseStart`). Строки — «САМОТЕСТ-ДЕЙСТВИЕ»,
//   итог — «САМОТЕСТ-ДЕЙСТВИЕ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, ключи, итог, выход
#include "World/AsdaPlayerCharacter.h"         // единое состояние действия (друг самотеста)
#include "World/AsdaPlayerController.h"        // KbEtalonActionState, IsFishSysKey (друг самотеста)
#include "World/AsdaFishingSubsystem.h"        // IsFishingNow — единая точка
#include "World/AsdaCoord.h"                   // точка танцпола в осях UE
#include "World/AsdaWorldSubsystem.h"          // LocalPlayerActor — танцпол работает только у «своего» актора мира
#include "AsdaBytes.h"                         // чтение тела 4024
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaActionTest, Log, All);

// Друг `AAsdaPlayerCharacter` и `AAsdaPlayerController` (объявлен в их заголовках): закрытые поля и `TickAutoDanceZone`.
struct FAsdaActionStateSelfTest
{
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args, UWorld* W);

	int32 Ok = 0, Fail = 0;              // счёт проверок
	TArray<int32> Sent4024;              // stateId перехваченных 4024 (второй u16 тела после MD5)
	uint32 CapGen = 0;                   // поколение своего перехвата отправок

	// Одна проверка.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaActionTest, Log, TEXT("САМОТЕСТ-ДЕЙСТВИЕ ok   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaActionTest, Error, TEXT("САМОТЕСТ-ДЕЙСТВИЕ FAIL %s"), *What); }
	}
	// Таймер активен.
	static bool TimerOn(UWorld* W, const FTimerHandle& H) { return W && W->GetTimerManager().IsTimerActive(H); }
	// Инструмент лопаты виден (компонента нет — «не виден»).
	static bool ToolVisible(const AAsdaPlayerCharacter* C) { return C->ShovelToolComp && C->ShovelToolComp->IsVisible(); }
	// Ушёл ли 4024 танца (112/113) после отметки From.
	bool DanceSentFrom(int32 From) const
	{
		for (int32 i = From; i < Sent4024.Num(); ++i) { if (Sent4024[i] == 112 || Sent4024[i] == 113) { return true; } }
		return false;
	}

	// Весь прогон (персонаж уже стоит).
	void Body(UWorld* W, AAsdaPlayerCharacter* C, AAsdaPlayerController* PC, UAsdaNetSubsystem* Net, FAsdaCVarScope& Cv);
};

void FAsdaActionStateSelfTest::Body(UWorld* W, AAsdaPlayerCharacter* C, AAsdaPlayerController* PC, UAsdaNetSubsystem* Net, FAsdaCVarScope& Cv)
{
	auto Kb = [PC]() { return PC ? PC->KbEtalonActionState() : -1; };
	// ★ 12.10.2026 (ревизия В3): предусловие «покой» — снять и фазу сидения/вставания, и ход, и замах (одна преамбула).
	//   Прежде павн, оставленный предыдущим самотестом пакета во вставании (`asda.Trade.SelfTestStallPlate` → фаза 9), давал
	//   A1 «KbEtalonActionState = 9» — самотест зависел от места в пакете.
	C->StopMotionForAction();
	C->SetAnimState(EAsdaPlayerState::Idle);
	C->StopEmotionClip();

	// ── A. Цепочка состояний ──
	Expect(C->GetBusyActionCode() == 0 && !C->IsEtalonActionBusy(), TEXT("A0 покой: «занятого» состояния нет"));
	const float LE = C->PlayEmotionClip(33);
	Expect(LE > 0.f && C->GetBusyActionCode() == 33 && C->GetEmotionState() == 33 && TimerOn(W, C->EmotionTimer),
		FString::Printf(TEXT("A1 эмоция 33: состояние %d, клип %.2f с, таймер конца"), C->GetBusyActionCode(), LE));
	Expect(Kb() == 33, FString::Printf(TEXT("A1 KbEtalonActionState = 33 (%d) — номер эмоции эталона"), Kb()));
	Expect(C->bWeaponMotionHidden, TEXT("A1 оружие за спиной (bActionBusy = 1)"));
	Expect(C->PlayEmotionClip(34) <= 0.f && C->GetBusyActionCode() == 33, TEXT("A1 КОНТРОЛЬ: вторая эмоция отклонена, пока идёт первая"));

	C->EnterEtalonState68();
	Expect(C->GetBusyActionCode() == 68 && C->IsEtalonState68() && C->GetEmotionState() < 0 && !TimerOn(W, C->EmotionTimer)
		&& C->EmotionUntilS == 0.0, FString::Printf(TEXT("A2 68 переписал эмоцию: состояние %d, таймер эмоции снят"), C->GetBusyActionCode()));
	Expect(Kb() == 68, FString::Printf(TEXT("A2 KbEtalonActionState = 68 (%d) — прежде не выводилось"), Kb()));
	Expect(C->bWeaponMotionHidden && C->IsEtalonActionBusy() && !C->IsActionClipOwned(),
		TEXT("A2 68: занят, оружие за спиной; клип не «свой» (в порту — покой)"));

	C->EnterEtalonShovelState(40, EAsdaShovelEntry::Peer4025);
	Expect(C->GetBusyActionCode() == 40 && C->GetEtalonShovelState() == 40 && !C->IsEtalonState68() && C->GetEmotionState() < 0,
		FString::Printf(TEXT("A3 лопата 40 переписала 68: состояние %d"), C->GetBusyActionCode()));
	Expect(Kb() == 40 && ToolVisible(C) && C->IsActionClipOwned(), FString::Printf(TEXT("A3 Kb = 40 (%d), инструмент в руке"), Kb()));

	const float LF = C->PlayFishAction(60, /*bSelf*/false);
	Expect(LF > 0.f && C->GetBusyActionCode() == 60 && C->GetFishAction() == 60 && C->GetEtalonShovelState() == 0 && TimerOn(W, C->FishClipTimer),
		FString::Printf(TEXT("A4 рыбалка 60 переписала лопату: состояние %d, клип %.2f с"), C->GetBusyActionCode(), LF));
	Expect(!ToolVisible(C) && !TimerOn(W, C->ShovelClipTimer) && Kb() == 60, FString::Printf(TEXT("A4 инструмент убран, Kb = 60 (%d)"), Kb()));

	// Сосед: эмоция переписывает рыбалку (`CChar_SetState` безусловен); снятие идёт штатным `StopFishAction` — с его строкой журнала.
	const float LE2 = C->PlayEmotionClip(34);
	Expect(LE2 > 0.f && C->GetBusyActionCode() == 34 && !C->IsFishActionActive() && !TimerOn(W, C->FishClipTimer) && !C->FishClipAsset.IsValid(),
		FString::Printf(TEXT("A5 эмоция 34 переписала рыбалку «чужого»: состояние %d, таймер и сторож рыбалки сняты"), C->GetBusyActionCode()));

	C->SetAnimState(EAsdaPlayerState::Move);
	Expect(C->GetBusyActionCode() == 0 && C->GetEmotionState() < 0 && !TimerOn(W, C->EmotionTimer),
		TEXT("A6 ход переписал эмоцию: «занятого» нет, таймер снят"));
	C->SetAnimState(EAsdaPlayerState::Idle);

	// ── B. Одна преамбула ──
	auto Dirty = [C, W]()
	{
		C->bMoving = true; C->MoveDirGame = FVector2D(1.f, 0.f);
		C->AnimState = EAsdaPlayerState::Sit; C->SitPhase = 8;
		C->AttackLoopClipIdx = 20; C->AttackAnimUntilS = W->GetTimeSeconds() + 5.0;
	};
	auto Clean = [C]()
	{
		return !C->bMoving && C->MoveDirGame.IsNearlyZero() && C->SitPhase == 0 && C->AnimState == EAsdaPlayerState::Idle
			&& C->AttackLoopClipIdx == -1 && C->AttackAnimUntilS == 0.0;
	};
	Dirty(); C->EnterEtalonState68();
	Expect(Clean() && C->GetBusyActionCode() == 68, TEXT("B1 68: ход, сидение и цикл замаха сняты одной преамбулой"));
	Dirty(); C->EnterEtalonShovelState(43, EAsdaShovelEntry::Peer4025);
	Expect(Clean() && C->GetBusyActionCode() == 43, TEXT("B2 лопата 43: ход, сидение и цикл замаха сняты"));
	Dirty(); C->PlayFishAction(61, false);
	Expect(Clean() && C->GetBusyActionCode() == 61, TEXT("B3 рыбалка 61: ход, сидение и цикл замаха сняты"));
	C->SetAnimState(EAsdaPlayerState::Move);
	C->SetAnimState(EAsdaPlayerState::Idle);
	Expect(C->GetBusyActionCode() == 0, TEXT("B4 ход → покой: «занятого» нет"));

	// ── C. ★ ИСПРАВЛЕНИЕ: танцпол Alpen ──
	const FVector SavedLoc = C->GetActorLocation();
	const int32 SavedMap = AsdaTestNet::EnterMapId(Net);
	// Зона работает только у «своего» актора мира (`IsLocalPlayerCharacter`); в пакетном прогоне павн контроллера мог остаться
	//   от другого офлайн-спавна — на время раздела делаем его «своим» и возвращаем.
	UAsdaWorldSubsystem* WS = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
	AActor* SavedLocal = WS ? WS->LocalPlayerActor.Get() : nullptr;
	if (WS) { WS->LocalPlayerActor = C; }
	AsdaTestNet::EnterMapId(Net) = 3;                                                   // alpen (`getField3036 == 3`)
	const FVector2D Ue = AsdaCoord::GameToUe2D(FVector2D(15950.f, 37500.f));   // середина прямоугольника танцпола (игровые X, Z)
	C->SetActorLocation(FVector(Ue.X, Ue.Y, SavedLoc.Z), false, nullptr, ETeleportType::TeleportPhysics);
	auto Dance = [C]() { C->AutoDanceCooldown = 0.f; C->TickAutoDanceZone(0.f); };
	auto IsDance = [C]() { const int32 S = C->GetBusyActionCode(); return S == 41 || S == 42; };

	Cv.Set(TEXT("asda.Action.LegacyBusyGates"), 0);
	int32 Mark = Sent4024.Num();
	C->EnterEtalonState68(); Dance();
	// Предусловие: без него C1/C2 прошли бы и при неработающей зоне (самотест был бы слеп).
	Expect(C->bInAutoDanceZone && C->IsLocalPlayerCharacter(), TEXT("C0 предусловие: свой персонаж в зоне танцпола Alpen"));
	Expect(C->GetBusyActionCode() == 68 && !DanceSentFrom(Mark), TEXT("C1 ★ 68 на танцполе: танец НЕ запускается, 4024 112/113 не ушёл (эталон: ветка состояния 0)"));
	C->SetAnimState(EAsdaPlayerState::Idle);
	C->PlayEmotionClip(33); Dance();
	Expect(C->GetBusyActionCode() == 33 && C->AutoDanceCooldown == 0.f && !DanceSentFrom(Mark),
		TEXT("C2 ★ своя эмоция на танцполе: попытки танца нет (пауза 0, 4024 не ушёл)"));
	C->StopEmotionClip();
	Mark = Sent4024.Num(); Dance();
	Expect(IsDance() && DanceSentFrom(Mark), FString::Printf(TEXT("C3 положительный контроль: в покое танец идёт (состояние %d, 4024 112/113)"), C->GetBusyActionCode()));
	C->StopEmotionClip();

	Cv.Set(TEXT("asda.Action.LegacyBusyGates"), 1);
	Mark = Sent4024.Num();
	C->EnterEtalonState68(); Dance();
	Expect(IsDance() && DanceSentFrom(Mark), TEXT("C4 КОНТРОЛЬ ключ 1 (прежний порт): танец запускается поверх 68 — проверка C1 различает"));
	C->StopEmotionClip();
	C->PlayEmotionClip(33); Dance();
	Expect(C->GetBusyActionCode() == 33 && C->AutoDanceCooldown == 1.f, TEXT("C5 КОНТРОЛЬ ключ 1: во время эмоции попытка с отказом (пауза 1 с) — проверка C2 различает"));
	C->StopEmotionClip();
	Cv.Set(TEXT("asda.Action.LegacyBusyGates"), 0);
	AsdaTestNet::EnterMapId(Net) = SavedMap;
	C->SetActorLocation(SavedLoc, false, nullptr, ETeleportType::TeleportPhysics);
	C->AutoDanceCooldown = 0.f;
	C->TickAutoDanceZone(0.f);   // выход из зоны (сброс признака «внутри»)
	if (WS) { WS->LocalPlayerActor = SavedLocal; }
	C->SetAnimState(EAsdaPlayerState::Move);
	C->SetAnimState(EAsdaPlayerState::Idle);

	// ── D. Фильтр клавиш рыбалки и единая точка «идёт рыбалка» ──
	using APC = AAsdaPlayerController;
	Expect(APC::IsFishSysKey(EKeys::LeftAlt, false) && APC::IsFishSysKey(EKeys::RightAlt, false) && APC::IsFishSysKey(EKeys::F10, false)
		&& APC::IsFishSysKey(EKeys::A, true) && !APC::IsFishSysKey(EKeys::A, false) && !APC::IsFishSysKey(EKeys::NumPadEight, false),
		TEXT("D1 «системная» клавиша = Alt / F10 / любая при зажатом Alt (WM_SYSKEYDOWN)"));
	Cv.Set(TEXT("asda.Fish.EatSysKeys"), 1);
	Expect(!APC::ShouldPassFishSysKey(EKeys::F10, false) && !APC::ShouldPassFishSysKey(EKeys::A, true),
		TEXT("D2 asda.Fish.EatSysKeys 1: Alt/F10 съедаются (порт)"));
	Cv.Set(TEXT("asda.Fish.EatSysKeys"), 0);
	Expect(APC::ShouldPassFishSysKey(EKeys::F10, false) && APC::ShouldPassFishSysKey(EKeys::A, true) && !APC::ShouldPassFishSysKey(EKeys::A, false),
		TEXT("D3 asda.Fish.EatSysKeys 0: Alt/F10 проходят, обычная клавиша — нет (оригинал)"));
	const UAsdaFishingSubsystem* F = W->GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>();
	const bool bF = F && F->IsFishing();
	Expect(UAsdaFishingSubsystem::IsFishingNow(W->GetGameInstance()) == bF && UAsdaFishingSubsystem::IsFishingNow(W) == bF
		&& UAsdaFishingSubsystem::IsFishingNow(C) == bF && !UAsdaFishingSubsystem::IsFishingNow(nullptr) && PC->IsFishingNow() == bF,
		FString::Printf(TEXT("D4 «идёт рыбалка» одна точка: GameInstance / мир / актор / контроллер = %d, nullptr = 0"), bF ? 1 : 0));
}

void FAsdaActionStateSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	static const TCHAR* kCmd = TEXT("asda.Action.SelfTest");
	const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
	if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
	TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
	TWeakObjectPtr<UWorld> WW(W);
	const bool bQuit = A.bQuit;
	// t = delay: офлайн-спавн своего павна штатными крюками (как у `asda.Net.SelfTestStop4008`).
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
	// t = delay + 2: прогон.
	FTimerHandle H1;
	W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, bQuit]()
	{
		UWorld* Wd = WW.Get();
		if (!Wd || !Gate->Pass(Wd)) { return; }
		AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
		AAsdaPlayerCharacter* C = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
		if (!C || !Net) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сети")); return; }
		FAsdaActionStateSelfTest T;
		FAsdaCVarScope Cv(Wd, kCmd);
		T.CapGen = AsdaSelfTestKit::ArmCapture(Net, [&T](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (Op == 4024 && P && Len >= 20) { T.Sent4024.Add((int32)AsdaBytes::RdU16(P + 18)); }
		}, 0, kCmd);
		if (T.CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
		T.Body(Wd, C, PC, Net, Cv);
		AsdaSelfTestKit::DisarmCapture(Net, T.CapGen);
		Cv.Restore();
		AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaActionTest, TEXT("САМОТЕСТ-ДЕЙСТВИЕ"), T.Ok, T.Fail,
			*FString::Printf(TEXT(" (перехвачено 4024: %d)"), T.Sent4024.Num()));
	}), A.Delay + 2.f, false);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaActionStateSelfTest(
	TEXT("asda.Action.SelfTest"),
	TEXT("Самотест единого состояния действия (дедуп №2, В3 S7): цепочка эмоция→68→лопата→рыбалка→эмоция→ход, одна преамбула, "
	     "★ танцпол Alpen в «занятом» состоянии (контроль asda.Action.LegacyBusyGates 1), фильтр Alt/F10 рыбалки, «идёт рыбалка». "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		FAsdaActionStateSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
