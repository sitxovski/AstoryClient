// Copyright (c) 2026 @sitxovski (Discord)
// САМОТЕСТ «СЛОТЫ ДРУЖБЫ» — `asda.Soul.SlotSelfTest`. Только офлайн (`-AsdaNoNet`). Входящие — штатный приём `DebugFeedPacket`.
//   Сценарии: 16 обработчиков по таблице эталона, повтор 5229, 5230, 5306, истечение записи, снапшот 6154, дух (модель, кость шеи,
//   следование, бег и покой, HP ниже 35 %), дух соседа и его уход, смена карты, контрольная пара `asda.Soul.SlotRun 0`, `asda.Soul.Spirit 0`.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"
#include "AsdaSessionReset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Test/AsdaSelfTestKit.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSoulSlotSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaCoord.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaSoulBaseActor.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulSlotTest, Log, All);

namespace AsdaSoulSlotTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Soul.SlotSelfTest");   // имя команды

	// Один шаг сценария.
	struct FStep
	{
		FString Name;                 // имя шага
		float TimeoutSec = 3.f;       // потолок
		TFunction<void()> Start;      // начало
		TFunction<bool()> Poll;       // true — шаг завершён
		TFunction<void()> Check;      // проверки
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;           // мир
		TWeakObjectPtr<UAsdaNetSubsystem> Net;  // сеть
		TArray<FStep> Steps;                    // шаги
		int32 StepIdx = 0;                      // текущий шаг
		bool bStepStarted = false;              // шаг начат
		double StepStartS = 0.0;                // начало шага
		FTimerHandle Tick;                      // таймер исполнителя
		bool bQuit = false;                     // выйти после прогона
		FAsdaSelfTestRunGate Gate{ kCmd, false };   // офлайн-гейт
		int32 Ok = 0, Fail = 0;                 // счётчики
		TUniquePtr<FAsdaNetSelfScope> SelfScope;    // свой номер вернётся
		int32 Self = -1;                        // свой номер
		int32 Peer = 7821;                      // сосед
		FVector Home = FVector::ZeroVector;     // позиция своего актора
		int32 MoveFrames = 0;                   // кадров хода
		int32 MaxQueue = 0;                     // наибольшая очередь на ходу
		float MaxLag = 0.f;                     // наибольшее отставание на ходу
		bool bSawRun = false;                   // видели дорожку 2
		FString ShotDir;                        // папка кадров (`shot=<папка>`), пусто — без кадров

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSoulSlotSubsystem* SS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* FX() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaSoulSlotTest, Log, TEXT("САМОТЕСТ-СЛОТЫ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		// Кадр в папку прогона.
		void Shot(const TCHAR* Name) const
		{
			if (ShotDir.IsEmpty()) { return; }
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(ShotDir, FString(Name) + TEXT(".png")), /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		}
		// 5229 или 5230: i16 userIdx, i16 слот, i16 остаток.
		void Slot(int32 Op, int32 User, int32 Id, int32 Dur = 600)
		{
			TArray<uint8> B; B.SetNumZeroed(6);
			AsdaBytes::WrI16(&B[0], (int16)User); AsdaBytes::WrI16(&B[2], (int16)Id); AsdaBytes::WrI16(&B[4], (int16)Dur);
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); }
		}
		// 5306 по номеру персонажа.
		void ClearAll(int32 User)
		{
			const TArray<uint8> Hdr = { 0x89 };
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(5306, Hdr, (uint16)User); }
		}
		// Показанное с прошлой чистки, через пробел.
		FString Shown() const
		{
			const UAsdaSoulSlotSubsystem* S = SS();
			return S ? FString::Join(S->DebugLog(), TEXT(" ")) : FString();
		}
		// Показано ли всё перечисленное и ровно столько записей.
		bool ShownExactly(const TArray<FString>& Want) const
		{
			const UAsdaSoulSlotSubsystem* S = SS();
			if (!S || S->DebugLog().Num() != Want.Num()) { return false; }
			for (const FString& W : Want) { if (!S->DebugLog().Contains(W)) { return false; } }
			return true;
		}
		// Снять всё со своего и соседа.
		void Calm()
		{
			ClearAll(Self);
			ClearAll(Peer);
			if (UAsdaSoulSlotSubsystem* S = SS()) { S->DebugClearLog(); }
		}
	};
}

struct FAsdaSoulSlotSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaSoulSlotSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaSoulSlotTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	for (const FString& A : Args) { if (A.StartsWith(TEXT("shot="))) { S->ShotDir = A.Mid(5); } }

	// `spirit` — без шагов 1 и 2: на кадрах духа не остаётся эффектов других слотов.
	const bool bSpiritOnly = KitArgs.Pos.Contains(TEXT("spirit"));
	auto Add = [S, bSpiritOnly](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		if (bSpiritOnly && (Name.StartsWith(TEXT("1 ")) || Name.StartsWith(TEXT("2 ")))) { return; }
		FStep St; St.Name = Name; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Шаг без ожидания.
	auto Now = [Add, S](const FString& Name, TFunction<void()> Body)
	{
		Add(Name, 1.f, MoveTemp(Body), [S]() { return S->El() > 0.05; }, nullptr);
	};

	Add(TEXT("подготовка"), 6.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		UAsdaWorldSubsystem* WSs = S->WS();
		if (!C || !WSs) { S->Expect(false, TEXT("нет своего персонажа после FakeSelfSpawn")); return; }
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		S->Home = C->GetActorLocation();
		const FVector2D Tile = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(2, 0));
		WSs->DebugSpawnFakePeer(S->Peer, TEXT("СлотСосед"), Tile);
		Net->SetVitalsHp(S->Self, 1000, 1000, TEXT("самотест слотов"), /*bServerSnapshot=*/true);
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.SlotRun"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.Spirit"), 1);
	}, [S]()
	{
		const UAsdaWorldSubsystem* Wd = S->WS();
		return S->El() > 0.6 && Wd && Wd->GetPlayerActorByUserNum(S->Peer) != nullptr;
	}, [S]()
	{
		S->Expect(S->SS() != nullptr && S->FX() != nullptr, TEXT("подсистемы на месте"));
		S->Calm();
		const AAsdaPlayerCharacter* C = S->Ch();
		USkeletalMeshComponent* K = C ? C->BaseSkel.Get() : nullptr;
		UE_LOG(LogAsdaSoulSlotTest, Log, TEXT("САМОТЕСТ-СЛОТЫ кости: 4=%d 8_End=%d _8_End=%d"), K && K->DoesSocketExist(TEXT("4")) ? 1 : 0,
			K && K->DoesSocketExist(TEXT("8_End")) ? 1 : 0, K && K->DoesSocketExist(TEXT("_8_End")) ? 1 : 0);
	});

	// Таблица обработчиков: слот → что показано.
	struct FRow { int32 Slot; TArray<FString> Show; int32 State; };
	TArray<FRow> Rows;
	Rows.Add({ 3, { TEXT("fx:eff_1_char_99_evasiong-a_02"), TEXT("fx:eff_1_char_99_evasionc-a_03"), TEXT("fx:eff_1_char_99_evasionc-a_04"), TEXT("snd:S_c_OddIndexSM003a"), TEXT("snd:S_c_OddIndexSM003b") }, 2 });
	Rows.Add({ 4, {}, 2 });
	Rows.Add({ 5, { TEXT("fx:eff_1_char_99_skigurdg-a_02"), TEXT("fx:eff_1_char_99_skigurdu-a_03"), TEXT("snd:S_c_OddIndexSM005") }, 2 });
	Rows.Add({ 6, { TEXT("fx:eff_1_char_99_perdmgg-a_02"), TEXT("fx:eff_1_char_99_perdmgu-a_03"), TEXT("snd:S_c_OddIndexSM006b") }, 2 });
	Rows.Add({ 7, { TEXT("fx:eff_1_char_99_redmgu-a_01"), TEXT("snd:S_c_OddIndexSM007a") }, 2 });
	Rows.Add({ 8, { TEXT("fx:eff_1_char_99_skigurdc-a_04"), TEXT("snd:S_c_OddIndexSM008") }, 2 });
	Rows.Add({ 9, { TEXT("notice:8128"), TEXT("fx:eff_1_char_99_soulritec-a_01"), TEXT("snd:S_c_SMSoulRecal01") }, 2 });
	Rows.Add({ 10, { TEXT("fx:eff_1_char_99_damchgg-a_03") }, 2 });
	for (int32 i = 11; i <= 15; ++i) { Rows.Add({ i, { TEXT("snd:S_c_SMSoulRecal02") }, 2 }); }
	Rows.Add({ 16, {}, 1 });
	Rows.Add({ 29, {}, 1 });

	Now(TEXT("1 обработчики слотов"), [S, Rows]()
	{
		UAsdaSoulSlotSubsystem* SS = S->SS();
		if (!SS) { return; }
		for (const FRow& R : Rows)
		{
			SS->DebugClearLog();
			S->Slot(5229, S->Self, R.Slot);
			S->Expect(S->ShownExactly(R.Show) && SS->GetSlotState(S->Self, R.Slot) == R.State,
				FString::Printf(TEXT("слот %d: состояние %d, показано [%s]"), R.Slot, SS->GetSlotState(S->Self, R.Slot), *S->Shown()));
			SS->DebugClearLog();
			S->Slot(5229, S->Self, R.Slot);
			if (R.State == 2) { S->Expect(SS->DebugLog().Num() == 0, FString::Printf(TEXT("слот %d: повтор 5229 ничего не показывает"), R.Slot)); }
		}
		// Слоты на костях оружия: у персонажа без кости эффект не появляется, звук есть.
		for (const int32 Slot : { 1, 2 })
		{
			SS->DebugClearLog();
			S->Slot(5229, S->Self, Slot);
			S->Expect(SS->DebugLog().Contains(Slot == 1 ? TEXT("snd:S_c_OddIndexSM001") : TEXT("snd:S_c_OddIndexSM002b")),
				FString::Printf(TEXT("слот %d: состояние %d, показано [%s]"), Slot, SS->GetSlotState(S->Self, Slot), *S->Shown()));
		}
		S->Expect(SS->DebugPersistentFxAlive(S->Self, 3, 0) && SS->DebugPersistentFxAlive(S->Self, 3, 1), TEXT("слот 3: оба постоянных эффекта живы"));
		S->Expect(!AsdaNotice::Loc(S->GI(), 8128).IsEmpty(), FString::Printf(TEXT("строка 8128: «%s»"), *AsdaNotice::Loc(S->GI(), 8128)));
		S->Expect(!AsdaNotice::Loc(S->GI(), 8139).IsEmpty(), FString::Printf(TEXT("строка 8139: «%s»"), *AsdaNotice::Loc(S->GI(), 8139)));
	});

	Now(TEXT("2 конец слотов"), [S]()
	{
		UAsdaSoulSlotSubsystem* SS = S->SS();
		if (!SS) { return; }
		S->Slot(5230, S->Self, 3);
		S->Expect(SS->GetSlotState(S->Self, 3) == 3 && !SS->DebugPersistentFxAlive(S->Self, 3, 0) && !SS->DebugPersistentFxAlive(S->Self, 3, 1),
			TEXT("5230 слота 3: состояние 3, постоянные эффекты погашены"));
		// Истечение записи.
		if (FAsdaEffectEntry* E = S->FX()->FindByTypeSub(S->Self, 7, 1)) { E->RemainSec = -1; }
		S->FX()->UpdateExpired(S->Self);
		S->Expect(SS->GetSlotState(S->Self, 7) == 3 && !S->FX()->FindByTypeSub(S->Self, 7, 1), TEXT("истёкшая запись слота 7: состояние 3, запись снята"));
		// 5306 завершает все.
		S->ClearAll(S->Self);
		bool bAll = true;
		for (int32 i = 0; i < 30; ++i) { bAll = bAll && SS->GetSlotState(S->Self, i) == 3; }
		S->Expect(bAll, TEXT("5306: все 30 слотов в состоянии 3"));
		// После конца слот запускается снова.
		SS->DebugClearLog();
		S->Slot(5229, S->Self, 7);
		S->Expect(SS->DebugLog().Contains(TEXT("snd:S_c_OddIndexSM007a")), TEXT("после конца слот 7 запускается снова"));
		S->Calm();
	});

	Add(TEXT("3 дух: запуск"), 3.f, [S]()
	{
		S->Calm();
		S->Slot(5229, S->Self, 0);
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		UAsdaSoulSlotSubsystem* SS = S->SS();
		const AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr;
		S->Expect(Soul != nullptr && SS->GetSlotState(S->Self, 0) == 2, FString::Printf(TEXT("слот 0: дух есть, показано [%s]"), *S->Shown()));
		S->Expect(SS && SS->DebugLog().Contains(TEXT("notice:8139")) && SS->DebugLog().Contains(TEXT("snd:S_c_SMSoulRecal02")) && SS->DebugLog().Contains(TEXT("soul+")),
			TEXT("слот 0 своему: окно 8139, звук, дух"));
		if (!Soul) { return; }
		S->Expect(Soul->DebugHasMesh() && Soul->DebugHasNeck(), TEXT("модель загружена, кость Bip01 Neck есть"));
		S->Expect(Soul->DebugIdleFxOn() && !Soul->DebugHpFxOn(), TEXT("HP полное: эффект покоя"));
		const AAsdaPlayerCharacter* C = S->Ch();
		const float Dz = C ? (float)(Soul->GetActorLocation().Z - C->GetActorLocation().Z) : 0.f;
		const float Dxy = C ? (float)FVector::Dist2D(Soul->GetActorLocation(), C->GetActorLocation()) : 0.f;
		S->Expect(FMath::IsNearlyEqual(Dz, 150.f, 1.f) && FMath::IsNearlyEqual(Dxy, 30.f, 1.f), FString::Printf(TEXT("смещение от владельца: высота %.1f, вбок %.1f"), Dz, Dxy));
	});

	Add(TEXT("3б дух: кадр покоя"), 3.f, [S]() {}, [S]() { return S->El() > 1.2; }, [S]()
	{
		S->Shot(TEXT("soul_idle"));
		const UAsdaSoulSlotSubsystem* SS = S->SS();
		const AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr;
		const AAsdaPlayerCharacter* C = S->Ch();
		if (Soul && C) { UE_LOG(LogAsdaSoulSlotTest, Log, TEXT("САМОТЕСТ-СЛОТЫ владелец %s, дух: %s"), *C->GetActorLocation().ToCompactString(), *Soul->DebugDescribe()); }
	});
	Add(TEXT("3в дух: кадр записан"), 3.f, [S]() {}, [S]() { return S->El() > 0.6; }, nullptr);
	// Кадры по частям: только эффекты и только тело. Нужны лишь при `shot=`.
	Add(TEXT("3г дух: кадр без тела"), 3.f, [S]()
	{
		const UAsdaSoulSlotSubsystem* SS = S->SS();
		if (AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr) { Soul->Mesh->SetVisibility(false); }
	}, [S]() { return S->El() > 0.3; }, [S]() { S->Shot(TEXT("soul_fx_only")); });
	Add(TEXT("3д дух: кадр записан"), 3.f, [S]() {}, [S]() { return S->El() > 0.6; }, [S]()
	{
		const UAsdaSoulSlotSubsystem* SS = S->SS();
		if (AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr) { Soul->Mesh->SetVisibility(true); Soul->DebugHideFx(true); }
	});
	Add(TEXT("3е дух: кадр без эффектов"), 3.f, [S]() {}, [S]() { return S->El() > 0.3; }, [S]() { S->Shot(TEXT("soul_body_only")); });
	Add(TEXT("3ж дух: кадр записан"), 3.f, [S]() {}, [S]() { return S->El() > 0.6; }, [S]()
	{
		const UAsdaSoulSlotSubsystem* SS = S->SS();
		if (AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr) { Soul->DebugHideFx(false); }
	});

	Add(TEXT("4 дух: ход"), 8.f, [S]()
	{
		S->MoveFrames = 0; S->MaxQueue = 0; S->MaxLag = 0.f; S->bSawRun = false;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		UAsdaSoulSlotSubsystem* SS = S->SS();
		const AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr;
		if (!C || !Soul) { return true; }
		C->SetActorLocation(C->GetActorLocation() + FVector(3.f, 0.f, 0.f));
		++S->MoveFrames;
		S->MaxQueue = FMath::Max(S->MaxQueue, Soul->DebugQueueNum());
		S->MaxLag = FMath::Max(S->MaxLag, (float)FVector::Dist2D(Soul->GetActorLocation(), C->GetActorLocation()));
		S->bSawRun = S->bSawRun || Soul->DebugTrack() == 2;
		return S->bSawRun && S->MoveFrames > 30;
	}, [S]()
	{
		S->Expect(S->MaxQueue >= 10, FString::Printf(TEXT("на ходу очередь %d, отставание от владельца до %.1f"), S->MaxQueue, S->MaxLag));
		S->Expect(S->bSawRun, TEXT("на ходу дорожка 2 (бег)"));
	});

	Add(TEXT("5 дух: остановка и HP"), 8.f, [S]()
	{
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->SetVitalsHp(S->Self, 300, 1000, TEXT("самотест слотов"), /*bServerSnapshot=*/true); }
	}, [S]()
	{
		const UAsdaSoulSlotSubsystem* SS = S->SS();
		const AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr;
		return !Soul || (Soul->DebugTrack() != 2 && Soul->DebugHpFxOn() && S->El() > 1.5);
	}, [S]()
	{
		const UAsdaSoulSlotSubsystem* SS = S->SS();
		const AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr;
		S->Expect(Soul && Soul->DebugTrack() != 2 && Soul->DebugQueueNum() < 11, FString::Printf(TEXT("стоя покой: дорожка %d, очередь %d"), Soul ? Soul->DebugTrack() : -1, Soul ? Soul->DebugQueueNum() : -1));
		S->Expect(Soul && Soul->DebugHpFxOn() && !Soul->DebugIdleFxOn(), TEXT("HP 30 %: эффект тревоги вместо покоя"));
		S->Shot(TEXT("soul_low_hp"));
	});
	Add(TEXT("5б дух: кадр записан"), 3.f, [S]() {}, [S]() { return S->El() > 0.6; }, [S]()
	{
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->SetVitalsHp(S->Self, 1000, 1000, TEXT("самотест слотов"), /*bServerSnapshot=*/true); }
	});

	Add(TEXT("6 дух: скрытие и конец"), 3.f, [S]()
	{
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->SetActorHiddenInGame(true); }
	}, [S]() { return S->El() > 0.2; }, [S]()
	{
		UAsdaSoulSlotSubsystem* SS = S->SS();
		AAsdaSoulBaseActor* Soul = SS ? SS->FindSoul(S->Self) : nullptr;
		S->Expect(Soul && Soul->IsHidden(), TEXT("владелец скрыт: дух скрыт"));
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->SetActorHiddenInGame(false); }
		if (!SS) { return; }
		SS->DebugClearLog();
		const int32 N0 = S->FX()->DebugSoulRecallEndNotices();
		S->Slot(5230, S->Self, 0);
		S->Expect(!SS->FindSoul(S->Self) && SS->GetSlotState(S->Self, 0) == 3, TEXT("5230 слота 0: духа нет, состояние 3"));
		S->Expect(S->ShownExactly({ TEXT("fx:eff_1_char_99_soulritec-a_04"), TEXT("snd:S_c_SMSoulRecal03"), TEXT("soul-") }), FString::Printf(TEXT("показано [%s]"), *S->Shown()));
		S->Expect(S->FX()->DebugSoulRecallEndNotices() - N0 == 1, TEXT("нотис 8155 один"));
		S->Slot(5229, S->Self, 0);
		SS->DebugClearLog();
		const int32 N1 = S->FX()->DebugSoulRecallEndNotices();
		S->ClearAll(S->Self);
		S->Expect(!SS->FindSoul(S->Self) && SS->DebugLog().Contains(TEXT("snd:S_c_SMSoulRecal03")) && S->FX()->DebugSoulRecallEndNotices() == N1,
			TEXT("5306: дух снят с эффектом и звуком, нотиса 8155 нет (`asda.Soul.NoNoticeOn5306`)"));
	});

	Add(TEXT("7 сосед"), 3.f, [S]()
	{
		S->Calm();
		// Снапшот 6154: заголовок i16 userIdx, запись 12 б: тип 1, вид, слот @2, остаток @6.
		TArray<uint8> B; B.SetNumZeroed(2 + 12);
		AsdaBytes::WrI16(&B[0], (int16)S->Peer);
		B[2] = 1; AsdaBytes::WrI16(&B[4], 0); AsdaBytes::WrI16(&B[8], 600);
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(6154, B); }
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		UAsdaSoulSlotSubsystem* SS = S->SS();
		if (!SS) { return; }
		S->Expect(SS->FindSoul(S->Peer) != nullptr, FString::Printf(TEXT("снапшот 6154 у соседа: дух есть, показано [%s]"), *S->Shown()));
		S->Expect(!SS->DebugLog().Contains(TEXT("notice:8139")), TEXT("окна 8139 у чужого слота нет"));
		const int32 N0 = S->FX()->DebugSoulRecallEndNotices();
		SS->DebugClearLog();
		S->FX()->ClearCharEffects(S->Peer);
		S->Expect(!SS->FindSoul(S->Peer) && SS->GetSlotState(S->Peer, 0) == 3 && !SS->DebugLog().Contains(TEXT("snd:S_c_SMSoulRecal03")),
			FString::Printf(TEXT("сосед ушёл: дух убран молча, показано [%s]"), *S->Shown()));
		S->Expect(S->FX()->DebugSoulRecallEndNotices() == N0, TEXT("нотиса 8155 нет"));
		// Запуск раньше актора: слот ждёт носителя.
		SS->DebugClearLog();
		S->Slot(5229, 7999, 7);
		S->Expect(SS->GetSlotState(7999, 7) == 1 && SS->DebugLog().Num() == 0, TEXT("носителя нет: слот ждёт, показа нет"));
		S->Slot(5230, 7999, 7);
		S->Expect(SS->GetSlotState(7999, 7) == 3, TEXT("5230 снимает ожидание"));
	});

	Add(TEXT("8 смена карты"), 3.f, [S]()
	{
		S->Calm();
		S->Slot(5229, S->Self, 0);
		S->Slot(5229, S->Self, 3);
		S->Slot(5229, S->Self, 7);
		S->Slot(5229, S->Self, 11);
		UAsdaSoulSlotSubsystem* SS = S->SS();
		if (!SS) { return; }
		SS->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		S->Expect(!SS->FindSoul(S->Self) && SS->GetSlotState(S->Self, 3) == 2, TEXT("смена карты: актора нет, состояния живут"));
		SS->DebugClearLog();
		SS->OnSelfSpawnCarry(S->Self);
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		UAsdaSoulSlotSubsystem* SS = S->SS();
		if (!SS) { return; }
		const AAsdaSoulBaseActor* Soul = SS->FindSoul(S->Self);
		S->Expect(Soul && !Soul->DebugBirthFxOn(), TEXT("дух вернулся без вспышки"));
		S->Expect(SS->DebugPersistentFxAlive(S->Self, 3, 0) && SS->DebugPersistentFxAlive(S->Self, 3, 1), TEXT("постоянные эффекты слота 3 вернулись"));
		S->Expect(SS->DebugLog().Contains(TEXT("snd:S_c_OddIndexSM007a")), FString::Printf(TEXT("обработчик слота 7 повторён, показано [%s]"), *S->Shown()));
		S->Expect(!SS->DebugLog().Contains(TEXT("snd:S_c_SMSoulRecal02")) && !SS->DebugLog().Contains(TEXT("notice:8139")), TEXT("слоты 0 и 11 молчат"));
		S->Calm();
	});

	Now(TEXT("9 контроль: asda.Soul.Spirit 0"), [S]()
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.Spirit"), 0);
		S->Calm();
		S->Slot(5229, S->Self, 0);
		UAsdaSoulSlotSubsystem* SS = S->SS();
		S->Expect(SS && !SS->FindSoul(S->Self) && SS->GetSlotState(S->Self, 0) == 2 && SS->DebugLog().Contains(TEXT("snd:S_c_SMSoulRecal02")),
			FString::Printf(TEXT("духа нет, слот действует, показано [%s]"), *S->Shown()));
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.Spirit"), 1);
		S->Calm();
	});

	Now(TEXT("10 контроль: asda.Soul.SlotRun 0"), [S]()
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.SlotRun"), 0);
		S->Calm();
		UAsdaSoulSlotSubsystem* SS = S->SS();
		if (!SS) { return; }
		for (const int32 Slot : { 0, 3, 7, 11 })
		{
			S->Slot(5229, S->Self, Slot);
			S->Expect(SS->GetSlotState(S->Self, Slot) == 3 && S->FX()->FindByTypeSub(S->Self, Slot, 1) != nullptr,
				FString::Printf(TEXT("слот %d: запись есть, слот не запущен"), Slot));
		}
		S->Expect(SS->DebugLog().Num() == 0 && !SS->FindSoul(S->Self), FString::Printf(TEXT("прежний порт: показа нет, духа нет [%s]"), *S->Shown()));
		const int32 N0 = S->FX()->DebugSoulRecallEndNotices();
		S->Slot(5230, S->Self, 0);
		S->Expect(S->FX()->DebugSoulRecallEndNotices() - N0 == 1 && SS->DebugLog().Num() == 0, TEXT("прежний порт: конец слота 0 — только нотис 8155"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.SlotRun"), 1);
		S->Calm();
	});

	Add(TEXT("итог"), 1.f, [S]()
	{
		S->ClearAll(S->Self);
		if (UAsdaEffectsSubsystem* Fx = S->FX()) { Fx->ClearCharEffects(S->Peer); }
		// ★ 06.11.2026: окна-сообщения прогона (ONEBUT/TWOBUT) держали ввод модалкой — в пакете следующий самотест не получал наведения и щелчков.
		if (UGameInstance* GIc = S->World.IsValid() ? S->World->GetGameInstance() : nullptr)
		{
			if (UAsdaWindowSubsystem* WMc = GIc->GetSubsystem<UAsdaWindowSubsystem>())
			{
				WMc->ClearMsgBoxQueue(kCmd);
				S->Expect(!WMc->IsAnyModalOpen(), TEXT("итог: модальных окон не осталось"));
			}
		}
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaSoulSlotTest, TEXT("САМОТЕСТ-СЛОТЫ"), S->Ok, S->Fail);
	}, nullptr, nullptr);

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
		TSharedPtr<FState> St = WSt.Pin();
		UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			return;
		}
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double NowS = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true;
			St->StepStartS = NowS;
			UE_LOG(LogAsdaSoulSlotTest, Log, TEXT("САМОТЕСТ-СЛОТЫ шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && NowS - St->StepStartS < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx;
		St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulSlotSelfTest(
	TEXT("asda.Soul.SlotSelfTest"),
	TEXT("Самотест слотов Дружбы: 16 обработчиков, 5230, 5306, истечение, снапшот, дух, сосед, смена карты, контрольные пары. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Soul.SlotSelfTest"));
		FAsdaSoulSlotSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
