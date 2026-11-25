// Copyright (c) 2026 @sitxovski (Discord)
// ★ RAII «мир своего персонажа» для самотестов (31.10.2026) — разбор в `Public/Test/AsdaSelfTestWorldScope.h`. [ПОРТ]

#if !UE_BUILD_SHIPPING
#include "Test/AsdaSelfTestWorldScope.h"

#include "AsdaSessionReset.h"
#include "Pet/AsdaPetSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "World/AsdaPetActor.h"
#include "World/AsdaPlayerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSelfTestWorldScope, Log, All);

void FAsdaWorldSelfScope::Capture(UWorld* W, const TCHAR* InWho)
{
	if (bArmed || !W) { return; }
	UGameInstance* GI = W->GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaPetSubsystem* Pets = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
	WWorld = W;
	Who = InWho ? InWho : TEXT("?");
	SelfActor = (WS && IsValid(WS->LocalPlayerActor)) ? WS->LocalPlayerActor.Get() : nullptr;
	bPetsEmpty = Pets && Pets->GetSlots().Num() == 0 && !Pets->HasActivePet();
	PawnsBefore.Reset();
	for (TActorIterator<AAsdaPlayerCharacter> It(W); It; ++It) { if (IsValid(*It)) { PawnsBefore.Add(*It); } }
	bArmed = true;
}

void FAsdaWorldSelfScope::Restore()
{
	if (!bArmed) { return; }
	bArmed = false;
	UWorld* W = WWorld.Get();
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	if (!GI) { return; }
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// Свой павн: сброс сессии обнулил ссылку, а уровень не перегружался — павн жив. Если прогон заспавнил нового
	//   (`Asda.World.FakeSelfSpawn` после сброса), им владеет контроллер — он и есть свой; иначе — павн до прогона.
	AActor* Was = SelfActor.Get();
	AActor* Me = nullptr;
	int32 Strays = 0;
	const bool bSelfBack = WS && !IsValid(WS->LocalPlayerActor) && (IsValid(Was) || W->GetFirstPlayerController());
	if (bSelfBack)
	{
		const APlayerController* PC = W->GetFirstPlayerController();
		AAsdaPlayerCharacter* Pawn = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		Me = IsValid(Pawn) ? static_cast<AActor*>(Pawn) : (IsValid(Was) ? Was : nullptr);
		if (Me) { WS->LocalPlayerActor = Me; }
		// Павн до прогона остался сиротой (ссылки на него нет, контроллер у нового) — до прогона свой был один, убираем.
		if (Me && IsValid(Was) && Was != Me) { Was->Destroy(); ++Strays; }
	}
	if (WS && IsValid(WS->LocalPlayerActor)) { Me = WS->LocalPlayerActor.Get(); }
	// Павн игрока, заспавненный за прогон и оставшийся ничьим: не свой, не сосед из реестра мира, его не было до прогона.
	for (TActorIterator<AAsdaPlayerCharacter> It(W); It; ++It)
	{
		AAsdaPlayerCharacter* P = *It;
		if (!IsValid(P) || P == Me) { continue; }
		if (PawnsBefore.ContainsByPredicate([P](const TWeakObjectPtr<AActor>& B) { return B.Get() == P; })) { continue; }
		if (WS && WS->FindUserIdxByActor(P) >= 0) { continue; }   // зарегистрированный сосед
		P->Destroy();
		++Strays;
	}
	// Питомцы: до прогона их не было — снимаем список и призыв тем же сбросом, что выход в выбор персонажа.
	UAsdaPetSubsystem* Pets = GI->GetSubsystem<UAsdaPetSubsystem>();
	const int32 PetsWas = Pets ? Pets->GetSlots().Num() : 0;
	const bool bPetsBack = bPetsEmpty && Pets && (PetsWas > 0 || Pets->HasActivePet());
	int32 PetActors = 0;
	if (bPetsBack)
	{
		Pets->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		if (UAsdaPetCageSubsystem* Cage = GI->GetSubsystem<UAsdaPetCageSubsystem>()) { Cage->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		for (TActorIterator<AAsdaPetActor> It(W); It; ++It)
		{
			// питомец своего павна или павна, убранного выше (владелец уже недействителен)
			if (IsValid(*It) && (!It->OwnerChar.IsValid() || (Me && It->OwnerChar.Get() == Me))) { It->Destroy(); ++PetActors; }
		}
	}
	if (bSelfBack || bPetsBack || Strays > 0)
	{
		UE_LOG(LogAsdaSelfTestWorldScope, Log, TEXT("[самотест] %s: мир своего персонажа возвращён — ссылка на павн %s (%s, сирот убрано %d), питомцев снято %d (акторов %d)"),
			*Who, (bSelfBack && Me) ? TEXT("восстановлена") : TEXT("цела"), Me ? *Me->GetName() : TEXT("нет"), Strays, bPetsBack ? PetsWas : 0, PetActors);
	}
}

#endif // !UE_BUILD_SHIPPING
