// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ДРУГ ВНЕ ИГРЫ ≠ ДРУГА НЕТ» (06.11.2026): `asda.Soulmate.OfflineGateSelfTest [en] [quit]`. [ИСПРАВЛЕНИЕ]
//   Жалоба: если Друг офлайн, игра считает, что Друга нет, и парные действия (жонглирование, рука, ухаживание) закрыты нотисом 6035.
//   Причина и разбор — у ключа `asda.Soul.SlotTruth` (AsdaHotbarSubsystem.cpp). Офлайн (`-AsdaNoNet`), карта мира, путь игрока:
//   порядок пакетов журнала Player52 05.11 — 5220 (слот 8, @25 = 0) ДО 4003 при неизвестном своём номере; действия — `PlayEmotion`
//   (общая точка окна, панели и чата), слот навыка — `ActivateSlot`, разрыв — 5207.
//     A) ключ 0 (прежний порт): баг воспроизведён — пары «нет», 6035;
//     B) ключ 1, Друг вне игры: пара есть, не в игре; парные 9/10/11 играются, 6035 нет; иконка без `_g`; навык серый, слот — 8162,
//        заголовок подсказки красный;
//     C) Друг вошёл (5220 @25 = 1): навык цветной, 8162 нет, заголовок не красный;
//     D) 5207 код 2 «Друг вне игры»: пара есть, навык снова серый;
//     E) 5207 код 1 «разрыв»: пары нет, 6035, иконка `_g`, навыки закрыты (`SelfSoulIdx` снят).
//   `en` — строки 6035/8162 английские. Итог: «ДРУГ-ОФЛАЙН ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulOffGate, Log, All);

namespace AsdaSoulOffGate
{
	static const TCHAR* kCmd = TEXT("asda.Soulmate.OfflineGateSelfTest");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Soul.SlotTruth");                 // ключ отката
	constexpr int32 kMeChar = 75230;      // свой charId (журнал Player52)
	constexpr int32 kMateChar = 75214;    // charId Друга (журнал Player52)
	constexpr int32 kMe = 321;            // свой userNum на прогон
	constexpr int32 kSlot = 8;            // номер пары (журнал Player52)
	constexpr int32 kSoulSkill = 31;      // навык Дружбы для иконки и слота
	constexpr int32 kAbs = 0;             // слот панели под навык

	// Шаг сценария: true — завершён, false — ждёт кадра. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                          // мир
		bool bQuit = false;                                // `quit` в конце
		bool bEn = false;                                  // прогон на английском
		int32 Ok = 0, Fail = 0;                            // счётчики проверок
		TArray<FStep> Steps;                               // сценарий
		int32 Idx = 0;                                     // текущий шаг
		double StepT0 = 0.0;                               // начало шага (время мира)
		FDelegateHandle H;                                 // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;            // свой номер/charId/уровень — вернуть
		TUniquePtr<FAsdaCVarScope> CVars;                  // ключи — вернуть
		TWeakObjectPtr<UAsdaNetSubsystem> Net;             // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;            // мир
		TWeakObjectPtr<UAsdaHotbarSubsystem> Hb;           // панель
		TWeakObjectPtr<UAsdaCharaInfoSubsystem> CI;        // действия
		TWeakObjectPtr<UAsdaChatSubsystem> Chat;           // журнал 2572
		TWeakObjectPtr<UAsdaSoulmateSubsystem> Sm;         // карточка Друга
		TWeakObjectPtr<UAsdaSkillUseSubsystem> Use;        // навыки
		TWeakObjectPtr<UAsdaSkillWindowSubsystem> Sw;      // подсказка навыка
		FAsdaHotbarSlot SavedSlot;                         // слот панели до прогона
		bool bSlotSaved = false;                           // слот снят

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSoulOffGate, Log,   TEXT("ДРУГ-ОФЛАЙН ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoulOffGate, Error, TEXT("ДРУГ-ОФЛАЙН FAIL %s"), *What); }
		}
		// Сколько строк журнала с текстом нотиса `LocId`.
		int32 Lines(int32 LocId) const
		{
			const UAsdaChatSubsystem* C = Chat.Get();
			return C ? C->DebugCountSystemLines(AsdaNotice::Loc(W.IsValid() ? W->GetGameInstance() : nullptr, LocId)) : -1;
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	static TArray<uint8> Body4003()
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kMe);
		AsdaBytes::WrI16(B.GetData() + 2, 256);
		AsdaBytes::WrI16(B.GetData() + 4, 256);
		B[8] = 1;
		return B;
	}
	// Своя запись 5220 (101 б) как в журнале: charId, userIdx, слот, Друг, его userIdx (−1 вне игры), @25 «Друг в игре».
	static TArray<uint8> Body5220(bool bInGame)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(B.GetData() + 0, kMeChar);
		AsdaBytes::WrI16(B.GetData() + 4, (int16)kMe);
		AsdaBytes::WrI32(B.GetData() + 6, kSlot);
		B[18] = 4;   // уровень Дружбы: навыку 31 нужен 4 — иначе заголовок подсказки красный и при Друге в игре
		AsdaBytes::WrI32(B.GetData() + 19, kMateChar);
		AsdaBytes::WrI16(B.GetData() + 23, (int16)(bInGame ? 77 : -1));
		B[25] = bInGame ? 1 : 0;
		return B;
	}
	// 5207: код и имя.
	static TArray<uint8> Body5207(uint8 Sub)
	{
		TArray<uint8> B; B.SetNumZeroed(10);
		B[0] = Sub;
		FMemory::Memcpy(B.GetData() + 5, "Pal", 3);
		return B;
	}

	// Вход в мир в порядке журнала: свой номер неизвестен, 5220 раньше 4003.
	static void EnterWorld(FState& S)
	{
		UAsdaNetSubsystem* N = S.Net.Get();
		N->bSelfUserNumKnown = false;
		N->SelectedCharId = kMeChar;
		N->DebugFeedPacket(5220, Body5220(false));
		N->DebugFeedPacket(4003, Body4003());
	}

	// Шаг: дождаться покоя персонажа (клип действия кончился), не дольше `MaxSec`.
	static FStep WaitIdle(FState* S, float MaxSec)
	{
		return [S, MaxSec](float T)
		{
			const AAsdaPlayerCharacter* Me = MeOf(*S);
			return T >= MaxSec || (T >= 0.2f && Me && Me->GetBusyActionCode() == 0 && Me->AnimState == EAsdaPlayerState::Idle);
		};
	}

	// Конец прогона.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UAsdaNetSubsystem* N = S.Net.Get()) { N->DebugFeedPacket(5207, Body5207(1)); }
		if (UAsdaHotbarSubsystem* Hb = S.Hb.Get(); Hb && S.bSlotSaved)
		{
			Hb->SetSlotContent(kAbs, (uint8)S.SavedSlot.Type, (uint8)S.SavedSlot.SubType, S.SavedSlot.Index, S.SavedSlot.Count, S.SavedSlot.Id);
		}
		S.CVars.Reset();
		S.NetScope.Reset();
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaSoulOffGate, TEXT("ДРУГ-ОФЛАЙН"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;   // своя ссылка: `Finish` снимает подписку вместе с копией в лямбде
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx))
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		auto Icon = [S](int32 Idx) { FString T; bool bFull = false; S->Hb->ResolveEmotionIcon(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, Idx), T, bFull); return T; };
		auto Red = [S]() { return S->Sw->DebugSoulTooltipTitle(kSoulSkill).Contains(TEXT("FFFF0000")); };

		// ═══ Подготовка: строки, карта, пары нет ═══
		S->Steps.Add(Do([S]()
		{
			const UGameInstance* GI = S->W->GetGameInstance();
			for (const int32 Id : { 6035, 8162 })
			{
				const FString T = AsdaNotice::Loc(GI, Id);
				bool bCyr = false;
				for (const TCHAR Ch : T) { if (Ch >= 0x400 && Ch <= 0x4FF) { bCyr = true; break; } }
				S->Check(!T.IsEmpty() && bCyr != S->bEn, FString::Printf(TEXT("строка %d: «%s»"), Id, *T));
			}
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			S->CVars->Set(kKey, 1);
			S->Net->SelfLevel = 150;
			S->Net->DebugFeedPacket(5207, Body5207(1));
		}));
		S->Steps.Add(Wait(0.3f));

		// ═══ A. Ключ 0: баг воспроизведён ═══
		S->Steps.Add(Do([S]() { S->CVars->Set(kKey, 0); EnterWorld(*S); }));
		S->Steps.Add(Wait(4.0f));   // эффект появления ≈3 с
		S->Steps.Add(Do([S, Icon]()
		{
			S->Check(MeOf(*S) != nullptr, TEXT("A0 свой павн есть"));
			const int32 N6035 = S->Lines(6035);
			const bool bPlayed = S->CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 9));
			S->Check(!S->Hb->HasSoulmate() && !bPlayed && S->Lines(6035) == N6035 + 1 && Icon(9).EndsWith(TEXT("_g")),
				FString::Printf(TEXT("A1 ключ 0 (баг): пара %d, действие %d, строк 6035 +%d, иконка %s"),
					S->Hb->HasSoulmate() ? 1 : 0, bPlayed ? 1 : 0, S->Lines(6035) - N6035, *Icon(9)));
		}));

		// ═══ B. Ключ 1: Друг вне игры ═══
		S->Steps.Add(Do([S]() { S->CVars->Set(kKey, 1); S->Net->DebugFeedPacket(5207, Body5207(1)); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { EnterWorld(*S); }));
		S->Steps.Add(Wait(4.0f));
		S->Steps.Add(Do([S, Icon, Red]()
		{
			S->Check(S->Hb->HasSoulmate() && !S->Hb->IsFriendInGame() && S->Use->HasSoulmate() && S->Sm->GetFriendSlot() == kSlot,
				FString::Printf(TEXT("B1 5220 до 4003, @25 = 0: пара %d, в игре %d, слот %d"),
					S->Hb->HasSoulmate() ? 1 : 0, S->Hb->IsFriendInGame() ? 1 : 0, S->Sm->GetFriendSlot()));
			S->Check(!Icon(9).EndsWith(TEXT("_g")) && !Icon(10).EndsWith(TEXT("_g")) && !Icon(11).EndsWith(TEXT("_g")),
				FString::Printf(TEXT("B2 иконки парных цветные: %s, %s, %s"), *Icon(9), *Icon(10), *Icon(11)));
			S->Check(S->Hb->DebugSoulSkillIcon(kSoulSkill).EndsWith(TEXT("-g")),
				FString::Printf(TEXT("B3 иконка навыка серая: %s"), *S->Hb->DebugSoulSkillIcon(kSoulSkill)));
			S->Check(Red(), FString::Printf(TEXT("B4 заголовок подсказки красный: %s"), *S->Sw->DebugSoulTooltipTitle(kSoulSkill)));
			S->SavedSlot = S->Hb->GetSlot(kAbs);
			S->bSlotSaved = true;
			S->Hb->SetSlotContent(kAbs, (uint8)EAsdaHotbarSlotType::SkillAuto, 0, 0, 0, kSoulSkill);
			const int32 N8162 = S->Lines(8162);
			S->Hb->ActivateSlot(kAbs);
			S->Check(S->Lines(8162) == N8162 + 1, FString::Printf(TEXT("B5 слот навыка: строк 8162 +%d (ждём 1)"), S->Lines(8162) - N8162));
		}));
		const int32 kIdx[3] = { 9, 10, 11 };
		const int32 kAct[3] = { 45, 46, 44 };
		for (int32 i = 0; i < 3; ++i)
		{
			const int32 Idx = kIdx[i], Act = kAct[i];
			S->Steps.Add(Do([S, Idx, Act]()
			{
				const int32 N6035 = S->Lines(6035);
				const bool bPlayed = S->CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, Idx));
				const AAsdaPlayerCharacter* Me = MeOf(*S);
				S->Check(bPlayed && S->Lines(6035) == N6035 && Me && Me->GetBusyActionCode() == Act,
					FString::Printf(TEXT("B6 парное %d при Друге вне игры: принято %d, строк 6035 +%d, состояние %d (ждём %d)"),
						Idx, bPlayed ? 1 : 0, S->Lines(6035) - N6035, Me ? Me->GetBusyActionCode() : -1, Act));
			}));
			S->Steps.Add(WaitIdle(S, 12.f));
		}

		// ═══ C. Друг вошёл ═══
		S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(5220, Body5220(true)); }));
		S->Steps.Add(Wait(0.2f));
		S->Steps.Add(Do([S, Red]()
		{
			S->Check(S->Hb->IsFriendInGame() && !S->Hb->DebugSoulSkillIcon(kSoulSkill).EndsWith(TEXT("-g")) && !Red(),
				FString::Printf(TEXT("C1 5220 @25 = 1: в игре %d, иконка %s, заголовок %s"), S->Hb->IsFriendInGame() ? 1 : 0,
					*S->Hb->DebugSoulSkillIcon(kSoulSkill), *S->Sw->DebugSoulTooltipTitle(kSoulSkill)));
			const int32 N8162 = S->Lines(8162);
			S->Hb->ActivateSlot(kAbs);
			S->Check(S->Lines(8162) == N8162, TEXT("C2 слот навыка: отказа 8162 нет"));
		}));

		// ═══ D. 5207 код 2: Друг вышел ═══
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(5207, Body5207(2)); }));
		S->Steps.Add(Wait(0.2f));
		S->Steps.Add(Do([S]()
		{
			S->Check(S->Hb->HasSoulmate() && !S->Hb->IsFriendInGame() && S->Use->HasSoulmate()
				&& S->Hb->DebugSoulSkillIcon(kSoulSkill).EndsWith(TEXT("-g")),
				FString::Printf(TEXT("D1 5207 код 2: пара %d, в игре %d, иконка %s"), S->Hb->HasSoulmate() ? 1 : 0,
					S->Hb->IsFriendInGame() ? 1 : 0, *S->Hb->DebugSoulSkillIcon(kSoulSkill)));
		}));

		// ═══ E. 5207 код 1: разрыв ═══
		S->Steps.Add(Wait(7.0f));   // показ навыка из шага C2 держит замок действий (`AsdaSkillCastLock`), действие молчало бы
		S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(5207, Body5207(1)); }));
		S->Steps.Add(Wait(0.2f));
		S->Steps.Add(Do([S, Icon]()
		{
			const int32 N6035 = S->Lines(6035);
			const bool bPlayed = S->CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 9));
			S->Check(!S->Hb->HasSoulmate() && !S->Use->HasSoulmate() && !bPlayed && S->Lines(6035) == N6035 + 1 && Icon(9).EndsWith(TEXT("_g")),
				FString::Printf(TEXT("E1 разрыв: пара %d, навыки %d, действие %d, строк 6035 +%d, иконка %s"), S->Hb->HasSoulmate() ? 1 : 0,
					S->Use->HasSoulmate() ? 1 : 0, bPlayed ? 1 : 0, S->Lines(6035) - N6035, *Icon(9)));
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulmateOfflineGateSelfTest(
	TEXT("asda.Soulmate.OfflineGateSelfTest"),
	TEXT("Самотест «Друг вне игры ≠ Друга нет» (ключ asda.Soul.SlotTruth): 5220 до 4003, парные действия, навык Дружбы 8162, 5207 коды 2 и 1. "
	     "Офлайн, карта мира. `en` — английские строки, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSoulOffGate;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit;
		if (GI)
		{
			SP->Net = GI->GetSubsystem<UAsdaNetSubsystem>();
			SP->WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
			SP->Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>();
			SP->CI = GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
			SP->Chat = GI->GetSubsystem<UAsdaChatSubsystem>();
			SP->Sm = GI->GetSubsystem<UAsdaSoulmateSubsystem>();
			SP->Use = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
			SP->Sw = GI->GetSubsystem<UAsdaSkillWindowSubsystem>();
		}
		if (!SP->Net.IsValid() || !SP->WS.IsValid() || !SP->Hb.IsValid() || !SP->CI.IsValid() || !SP->Chat.IsValid() || !SP->Sm.IsValid()
			|| !SP->Use.IsValid() || !SP->Sw.IsValid() || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем или ключа отката"));
			return;
		}
		for (const FString& P : KA.Pos) { if (P.Equals(TEXT("en"), ESearchCase::IgnoreCase)) { SP->bEn = true; } }
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(SP->Net.Get(), kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
