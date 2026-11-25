// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishAnimSubsystem — см. заголовок (рыбалка, волна В5, 08.10.2026).
#include "World/AsdaFishAnimSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "World/AsdaFishingSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishAnim, Log, All);

void UAsdaFishAnimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaFishingSubsystem::StaticClass());
	Bind();
}

void UAsdaFishAnimSubsystem::Deinitialize()
{
	if (UAsdaFishingSubsystem* F = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
	{
		F->OnSelfFishAction.Remove(HSelf);
		F->OnPeerFishState.Remove(HPeer);
	}
	bBound = false;
	Super::Deinitialize();
}

void UAsdaFishAnimSubsystem::Bind()
{
	if (bBound) { return; }
	UAsdaFishingSubsystem* F = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	if (!F) { return; }
	HSelf = F->OnSelfFishAction.AddUObject(this, &UAsdaFishAnimSubsystem::OnSelfAction);
	HPeer = F->OnPeerFishState.AddUObject(this, &UAsdaFishAnimSubsystem::OnPeerState);
	bBound = true;
}

void UAsdaFishAnimSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.FishAnim");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!bBound) { Bind(); }
	PushSelfFields();
}

// Свои поля `CCharacter[1].gap_A42` живут в автомате (их пишут 6168/6172/FSM 108/0x20/0x21/128) — персонажу каждый кадр.
void UAsdaFishAnimSubsystem::PushSelfFields()
{
	UAsdaFishingSubsystem* F = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = F ? F->GetSelfCharacter() : nullptr;
	if (AAsdaPlayerCharacter* Old = LastSelf.Get())
	{
		if (Old != Ch) { Old->SetFishSelfOwner(false); }
	}
	LastSelf = Ch;
	if (!Ch) { return; }
	Ch->SetFishSelfOwner(true);
	Ch->SetFishFields(F->GetSelfFishingFlag(), F->GetSelfHasFish(), F->GetSelfFishSizePct(), F->GetSelfFishCode());
}

// `charEmoteSabzilAction@0xFC5590` (g:604289): клип удочки (`sub_935007`) и `CCharacter_SetAction(action)`.
void UAsdaFishAnimSubsystem::OnSelfAction(int32 Action)
{
	UAsdaFishingSubsystem* F = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = F ? F->GetSelfCharacter() : nullptr;
	if (!Ch)
	{
		// ★ В5fix (ревизия В5, МЕЛК-5): своего актора нет — конец клипа некому сообщить; автомат доводит проход таймером
		//   длительности .jbb, иначе стоял бы в 112/113/126/127 навсегда. [ПОРТ]
		if (F && Action >= 60 && Action <= 67) { F->ArmClipBridgeFallback(Action, TEXT("нет своего персонажа")); }
		return;
	}
	PushSelfFields();   // флаг/рыба к моменту клипа уже актуальны (вставка удочки в тот же кадр)
	if (Action >= 60 && Action <= 67) { Ch->PlayFishAction(Action, /*bSelf*/true); }
	else { Ch->StopFishAction(TEXT("своё действие 0")); }
}

// `charApplyStateFromList@0xFCAC60` (g:606528-606600): фаза 3 → 63/64 (поля не трогаются); иначе фаза ≠ 0 → 61, фаза 2 → 0,
//   [15036] = (фаза ≠ 0 && ≠ 2), [15076] = @5, [15068] = @9, [15066] = (@5 ≠ 0). Поворот к зоне сделан автоматом.
void UAsdaFishAnimSubsystem::OnPeerState(AActor* Char, const FAsdaFishPeerState& St)
{
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Char);
	if (!Ch) { return; }
	// ★ В5fix (ревизия В5, МЕЛК-10): порядок 1:1 с эталоном (g:606588-606600) — сначала состояние, потом поля; при фазе 2
	//   эталон ставит 61 ПЕРЕД 0 (`CChar_SetState(61)` → клип удочки 1, [15100] = 61; затем `CChar_SetState(0)`).
	if (St.Phase == 3) { Ch->PlayFishAction(St.Action, /*bSelf*/false); }
	else
	{
		if (St.Phase != 0) { Ch->PlayFishAction(61, /*bSelf*/false); }
		if (St.Phase == 2) { Ch->StopFishAction(TEXT("6173 фаза 2")); }
		Ch->SetFishFields(St.bFishingFlag, St.bHasFish, St.SizePct, St.FishCode);
	}
	UE_LOG(LogAsdaFishAnim, Log, TEXT("[рыбалка] 6173 → %s: фаза %d, действие %d, флаг %d, рыба %d (%d%%)"),
		*GetNameSafe(Ch), St.Phase, St.Action, St.bFishingFlag ? 1 : 0, St.FishCode, St.SizePct);
}
