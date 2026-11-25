// Copyright (c) 2026 @sitxovski (Discord)
// Общий каркас самотестов — разбор, назначение и граница применения у объявления (`Public/Test/AsdaSelfTestKit.h`).
#include "Test/AsdaSelfTestKit.h"

#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"
#include "AsdaSelfTestBatch.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSelfTestKit, Log, All);

#if !UE_BUILD_SHIPPING
// ★ Офлайн-заглушка «живое соединение» для проверки пути отказа каркаса БЕЗ подключения к серверу: при 1 `RefuseStart`
//   отвечает так же, как при живом соединении (итог ОТКАЗ + `quit`), сеть не трогается. Только для проверок каркаса.
static TAutoConsoleVariable<int32> CVarSelfTestSimulateOnline(
	TEXT("asda.SelfTest.SimulateOnline"),
	0,
	TEXT("Заглушка для проверки каркаса самотестов: 1 — AsdaSelfTestKit::RefuseStart отказывает, как при живом соединении ")
	TEXT("(строка «ИТОГ: ok=0 fail=1 (ОТКАЗ: онлайн …)» и quit), к серверу ничего не подключается. 0 — обычная работа."),
	ECVF_Default);
#endif

// Друг `UAsdaNetSubsystem` (объявлен в его заголовке): доступ каркаса к перехватчику отправок.
struct FAsdaSelfTestKitNet
{
	// Занят ли перехват (поле перехватчика взведено).
	static bool Busy(const UAsdaNetSubsystem* Net) { return Net && static_cast<bool>(Net->SendCaptureForStand); }
	// Взвести перехватчик (штатный `ArmSendCapture`).
	static uint32 Arm(UAsdaNetSubsystem* Net, AsdaSelfTestKit::FCaptureFn Fn, uint16 Only) { return Net->ArmSendCapture(MoveTemp(Fn), Only); }
	// Снять перехватчик своего поколения (штатный `DisarmSendCapture`).
	static bool Disarm(UAsdaNetSubsystem* Net, uint32 Gen) { return Net && Net->DisarmSendCapture(Gen); }
	// Текущее поколение перехватчика (для снятия утечки пакетом).
	static uint32 Gen(const UAsdaNetSubsystem* Net) { return Net ? Net->SendCaptureGeneration : 0; }
};

// ═══ Аргументы ═══════════════════════════════════════════════════════════════════════════════════

FAsdaSelfTestArgs FAsdaSelfTestArgs::Parse(const TArray<FString>& Args)
{
	FAsdaSelfTestArgs R;
	for (const FString& A : Args)
	{
		if (A.StartsWith(TEXT("delay="), ESearchCase::IgnoreCase)) { R.Delay = FMath::Max(0.f, FCString::Atof(*A.Mid(6))); R.bHasDelay = true; }
		else if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { R.bQuit = true; }
		else if (A.Equals(TEXT("online"), ESearchCase::IgnoreCase)) { R.bOnline = true; }
		else { R.Pos.Add(A); }
	}
	return R;
}

int32 FAsdaSelfTestArgs::Int(int32 I, int32 Def) const { return Pos.IsValidIndex(I) ? FCString::Atoi(*Pos[I]) : Def; }

float FAsdaSelfTestArgs::Float(int32 I, float Def) const { return Pos.IsValidIndex(I) ? FCString::Atof(*Pos[I]) : Def; }

FString FAsdaSelfTestArgs::Str(int32 I, const TCHAR* Def) const { return Pos.IsValidIndex(I) ? Pos[I] : FString(Def ? Def : TEXT("")); }

bool FAsdaSelfTestArgs::Has(const TCHAR* Word) const
{
	return Pos.ContainsByPredicate([Word](const FString& P) { return P.Equals(Word, ESearchCase::IgnoreCase); });
}

// ═══ Старт, отказ, итог, выход ═══════════════════════════════════════════════════════════════════

namespace AsdaSelfTestKit
{
	// Мир из контекста (для консольной `quit`): мир, GI, подсистема GI или объект с миром.
	static UWorld* WorldOf(const UObject* Ctx)
	{
		if (!Ctx) { return nullptr; }
		if (const UWorld* W = Cast<UWorld>(Ctx)) { return const_cast<UWorld*>(W); }
		if (const UGameInstance* GI = Cast<UGameInstance>(Ctx)) { return GI->GetWorld(); }
		if (const UGameInstanceSubsystem* S = Cast<UGameInstanceSubsystem>(Ctx)) { return S->GetGameInstance() ? S->GetGameInstance()->GetWorld() : nullptr; }
		return Ctx->GetWorld();
	}

	UAsdaNetSubsystem* NetOf(const UObject* Ctx)
	{
		if (!Ctx) { return nullptr; }
		const UGameInstance* GI = Cast<UGameInstance>(Ctx);
		if (!GI) { if (const UGameInstanceSubsystem* S = Cast<UGameInstanceSubsystem>(Ctx)) { GI = S->GetGameInstance(); } }
		if (!GI) { if (const UWorld* W = WorldOf(Ctx)) { GI = W->GetGameInstance(); } }
		return GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	}

	void Refused(const UObject* Ctx, const TCHAR* Cmd, bool bQuit, const TCHAR* Why)
	{
		UE_LOG(LogAsdaSelfTestKit, Warning, TEXT("%s ИТОГ: ok=0 fail=1 (ОТКАЗ: %s)"), Cmd ? Cmd : TEXT("?"), Why ? Why : TEXT("?"));
		Quit(WorldOf(Ctx), bQuit);
	}

	bool RefuseStart(const UObject* Ctx, const TCHAR* Cmd, const FAsdaSelfTestArgs& Args, float GuardSeconds, TArrayView<const uint16> ExtraOps)
	{
		// Опкоды окна: метка «все» + свои опкоды самотеста (они и так входят во «все», но оставляют след в окне — так
		//   `IsDebugSendWindowOpen(Net, Op)` прежних самотестов, например ремесла, отвечает как раньше).
		TArray<uint16> Ops;
		Ops.Add(kGuardAllOpcodes);
		Ops.Append(ExtraOps.GetData(), ExtraOps.Num());
		const float Sec = Args.Delay + (GuardSeconds > 0.f ? GuardSeconds : kDefaultGuardSec);
#if !UE_BUILD_SHIPPING
		if (CVarSelfTestSimulateOnline.GetValueOnGameThread() != 0)
		{
			Refused(Ctx, Cmd, Args.bQuit, TEXT("онлайн — заглушка asda.SelfTest.SimulateOnline 1 (живого соединения нет, ничего не отправлено)"));
			return true;
		}
#endif
		if (!FAsdaOutgoingGate::RefuseDebugWhenOnline(Ctx, Cmd, Sec, MakeArrayView(Ops))) { return false; }
		const UAsdaNetSubsystem* Net = NetOf(Ctx);
		Refused(Ctx, Cmd, Args.bQuit, Net ? TEXT("онлайн — есть живое соединение с сервером, ничего не отправлено")
		                                  : TEXT("нет сетевой подсистемы — состояние соединения неизвестно"));
		return true;
	}

	bool RefuseDeferred(const UObject* Ctx, const TCHAR* Cmd, bool bQuit)
	{
		return FAsdaOutgoingGate::RefuseDeferredStartWhenOnline(Ctx, Cmd, bQuit);
	}

	void Summary(const FLogCategoryBase& Cat, const TCHAR* Prefix, int32 Ok, int32 Fail, const TCHAR* Tail, bool bWarning)
	{
		if (bWarning)
		{
			UE_LOG_REF(Cat, Warning, TEXT("%s ИТОГ: ok=%d fail=%d%s"), Prefix ? Prefix : TEXT("?"), Ok, Fail, Tail ? Tail : TEXT(""));
		}
		else
		{
			UE_LOG_REF(Cat, Log, TEXT("%s ИТОГ: ok=%d fail=%d%s"), Prefix ? Prefix : TEXT("?"), Ok, Fail, Tail ? Tail : TEXT(""));
		}
	}

	void Quit(UWorld* W, bool bQuit)
	{
		if (!bQuit) { return; }
#if !UE_BUILD_SHIPPING
		AsdaSelfTestBatch::Quit(W);
#else
		if (GEngine) { GEngine->Exec(W, TEXT("quit")); }
#endif
	}

	void Finish(UWorld* W, const TCHAR* Cmd, bool bQuit, const FLogCategoryBase& Cat, const TCHAR* Prefix, int32 Ok, int32 Fail,
		const TCHAR* Tail, bool bWarning)
	{
		Summary(Cat, Prefix, Ok, Fail, Tail, bWarning);
		if (AsdaCVarSave::Registry().Num() > 0) { AsdaCVarSave::RestoreAll(Cmd ? Cmd : TEXT("?")); }
		Quit(W, bQuit);
	}

	uint32 ArmCapture(UAsdaNetSubsystem* Net, FCaptureFn Fn, uint16 OnlyOpcode, const TCHAR* Who)
	{
		if (!Net) { return 0; }
		if (FAsdaSelfTestKitNet::Busy(Net))
		{
			UE_LOG(LogAsdaSelfTestKit, Warning,
				TEXT("[самотест] %s: перехват отправок занят другим самотестом/стендом — свой не взведён, чужой не тронут (N1(в))"),
				Who ? Who : TEXT("?"));
			return 0;
		}
		return FAsdaSelfTestKitNet::Arm(Net, MoveTemp(Fn), OnlyOpcode);
	}

	bool DisarmCapture(UAsdaNetSubsystem* Net, uint32 Generation) { return FAsdaSelfTestKitNet::Disarm(Net, Generation); }

	bool IsCaptureBusy(const UAsdaNetSubsystem* Net) { return FAsdaSelfTestKitNet::Busy(Net); }

	bool DisarmLeakedCapture(UAsdaNetSubsystem* Net)
	{
		return FAsdaSelfTestKitNet::Busy(Net) && FAsdaSelfTestKitNet::Disarm(Net, FAsdaSelfTestKitNet::Gen(Net));
	}

	bool NeedSelfSpawn(const UWorld* W, const TCHAR* WantName)
	{
		if (!W) { return false; }
		const APlayerController* P = W->GetFirstPlayerController();
		if (!P || !P->GetPawn()) { return true; }
		const UAsdaWorldSubsystem* WS = W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		// ★ 06.11.2026: синтетический сброс сессии (LeftWorld в самотесте) обнуляет `LocalPlayerActor`, а павн у контроллера остаётся.
		//   Такой павн не «свой» (`IsLocalPlayerCharacter` ложно): его тик не опрашивает клавиши, и следующий самотест пакета падал
		//   на настоящем вводе (CrouchMode 22/14 после WhisperReply).
		if (WS && WS->LocalPlayerActor.Get() != P->GetPawn()) { return true; }
		if (!WantName) { return false; }
		return WS && WS->GetSelfCharName() != WantName;
	}
}

// ═══ RAII ═════════════════════════════════════════════════════════════════════════════════════════

void FAsdaNetSelfScope::Capture(UAsdaNetSubsystem* Net, const TCHAR* InWho)
{
	if (bArmed || !Net) { return; }
	WNet = Net;
	Who = InWho ? InWho : TEXT("?");
	SelfUserNum = Net->SelfUserNum;
	bSelfUserNumKnown = Net->bSelfUserNumKnown;
	SelectedCharId = Net->SelectedCharId;
	SelfLevel = Net->SelfLevel;
	SelfWeightCur = Net->SelfWeightCur;
	bSelfDead = Net->bSelfDead;
	bArmed = true;
}

void FAsdaNetSelfScope::Restore()
{
	if (!bArmed) { return; }
	bArmed = false;
	UAsdaNetSubsystem* Net = WNet.Get();
	if (!Net) { return; }
	FString What;   // что вернулось (для строки лога)
	auto Note = [&What](const TCHAR* Field, const FString& From, const FString& To)
	{
		What += FString::Printf(TEXT("%s%s %s→%s"), What.IsEmpty() ? TEXT("") : TEXT(", "), Field, *From, *To);
	};
	if (Net->SelfUserNum != SelfUserNum) { Note(TEXT("userNum"), FString::FromInt(Net->SelfUserNum), FString::FromInt(SelfUserNum)); Net->SelfUserNum = SelfUserNum; }
	if (Net->bSelfUserNumKnown != bSelfUserNumKnown)
	{
		Note(TEXT("номер известен"), Net->bSelfUserNumKnown ? TEXT("1") : TEXT("0"), bSelfUserNumKnown ? TEXT("1") : TEXT("0"));
		Net->bSelfUserNumKnown = bSelfUserNumKnown;
	}
	if (Net->SelectedCharId != SelectedCharId) { Note(TEXT("charId"), FString::FromInt(Net->SelectedCharId), FString::FromInt(SelectedCharId)); Net->SelectedCharId = SelectedCharId; }
	if (Net->SelfLevel != SelfLevel) { Note(TEXT("уровень"), FString::FromInt(Net->SelfLevel), FString::FromInt(SelfLevel)); Net->SelfLevel = SelfLevel; }
	if (Net->SelfWeightCur != SelfWeightCur) { Note(TEXT("вес"), FString::FromInt(Net->SelfWeightCur), FString::FromInt(SelfWeightCur)); Net->SelfWeightCur = SelfWeightCur; }
	if (Net->bSelfDead != bSelfDead)
	{
		Note(TEXT("смерть"), Net->bSelfDead ? TEXT("1") : TEXT("0"), bSelfDead ? TEXT("1") : TEXT("0"));
		Net->bSelfDead = bSelfDead;
		// Был жив — оставлен мёртвым: воскрешение тем же делегатом, что приёмник 4055/4056 (поза смерти, меню 25000).
		if (!bSelfDead) { Net->OnCharRevived.Broadcast(); }
	}
	if (!What.IsEmpty())
	{
		UE_LOG(LogAsdaSelfTestKit, Log, TEXT("[самотест] %s: своё состояние возвращено (N1(г)): %s"), *Who, *What);
	}
}

FAsdaCVarScope::FAsdaCVarScope(const UWorld* W, const TCHAR* InWho)
	: Who(InWho ? InWho : TEXT("?"))
{
	AsdaCVarSave::ArmWorldGuard(W, *Who);
}

void FAsdaCVarScope::Restore()
{
	if (bDone) { return; }
	bDone = true;
	if (AsdaCVarSave::Registry().Num() > 0) { AsdaCVarSave::RestoreAll(*Who); }
	else { AsdaCVarSave::DisarmWorldGuard(*Who); }
}
