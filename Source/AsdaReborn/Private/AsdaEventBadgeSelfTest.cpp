// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЗНАЧОК СОБЫТИЯ» `asda.Buff.SelfTestEventBadge` (21.11.2026, ревизия 49 С-1 / С-2 к серверному P128). [ПОРТ]
//   Только логика `UAsdaEffectsSubsystem` (сеть не нужна, `-AsdaNoNet`): те же функции, что зовут приём 6039/6041 (`ApplyEventSet`),
//   4301 (`ApplyEventBonusPacket`), таймер часов (`UpdateScheduledClock`) и сброс сессии (`ResetSessionState`). Итог значка —
//   `GetEventIconIndices` (его же рисует мини-статус). Состояние ивент-бонуса до теста сохраняется и возвращается.
//   Сценарии:
//     1) монеты ×3 по 6039 вида 0; смена карты (MapChanged) гасит их (С-1 — прежде горели до конца сеанса);
//     2) час 4301 (+1.0, вид 1) → опыт ×2; событие 6039 ×3 сверху → ×3; шаг часов клиента НЕ перетирает ×3 (С-2);
//     3) 6041 опыта → значок часа ×2 остаётся (С-2 — прежде гас);
//     4) час кончается по часам клиента (остаток минут из 4301) → опыт гаснет (С-2 — прежде горел до перезахода);
//     5) смена карты час не гасит (4301 часы — не карта), полный сброс гасит всё.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaSessionReset.h"
#include "Net/AsdaNetEffectTypes.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEventBadgeTest, Log, All);

namespace AsdaEventBadgeLocal
{
	const TCHAR* const kCmd = TEXT("asda.Buff.SelfTestEventBadge");   // имя команды

	// Снимок полей ивент-бонуса (вернуть после теста).
	struct FSnap
	{
		bool bSetExpOn, bSetDropOn, bHourExpOn, bHourDropOn, bEventActive;
		float SetExpMult, SetDropMult, HourExpMult, HourDropMult, EventRate;
		int32 EventType, EventRemainMin, ClockMonth, ClockDay, ClockHour, ClockMinute;
	};
	FSnap Take(const UAsdaEffectsSubsystem* F)
	{
		return { F->bSetExpOn, F->bSetDropOn, F->bHourExpOn, F->bHourDropOn, F->bEventActive,
		         F->SetExpMult, F->SetDropMult, F->HourExpMult, F->HourDropMult, F->EventRate,
		         F->EventType, F->EventRemainMin, F->ClockMonth, F->ClockDay, F->ClockHour, F->ClockMinute };
	}
	void Put(UAsdaEffectsSubsystem* F, const FSnap& S)
	{
		F->bSetExpOn = S.bSetExpOn; F->bSetDropOn = S.bSetDropOn; F->bHourExpOn = S.bHourExpOn; F->bHourDropOn = S.bHourDropOn;
		F->bEventActive = S.bEventActive; F->SetExpMult = S.SetExpMult; F->SetDropMult = S.SetDropMult;
		F->HourExpMult = S.HourExpMult; F->HourDropMult = S.HourDropMult; F->EventRate = S.EventRate; F->EventType = S.EventType;
		F->EventRemainMin = S.EventRemainMin; F->ClockMonth = S.ClockMonth; F->ClockDay = S.ClockDay; F->ClockHour = S.ClockHour;
		F->ClockMinute = S.ClockMinute;
		F->RecombineEventBonus();
		F->OnEventBonusChanged.Broadcast();
	}
	// 4301: час с 10 до 12, сейчас 10:00, бонус +Bonus (вид 1 — опыт)
	FAsdaEventBonusPkt Hour(float Bonus)
	{
		FAsdaEventBonusPkt P;
		P.EventType = 1; P.Month = 6; P.Day = 15; P.Hour = 10; P.Minute = 0; P.Second = 0;
		P.SchedYear = 2026; P.SchedMonth = 6; P.SchedDay = 15; P.SchedStartHour = 10; P.EndHour = 12; P.BonusRate = Bonus;
		return P;
	}
}

static void AsdaRunEventBadgeSelfTest(const TArray<FString>& A, UWorld* W)
{
	using namespace AsdaEventBadgeLocal;
	const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
	UAsdaEffectsSubsystem* F = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!F) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет UAsdaEffectsSubsystem")); return; }
	const FSnap Saved = Take(F);
	const TArray<FAsdaEventScheduleRow> SavedSched = F->EventSchedule;
	const bool SavedFallback = F->bEventFallbackToPacketRate;
	F->EventSchedule.Reset();                    // расписание act_secession3 не мешает (как у живой игры: даты 2011)
	F->bEventFallbackToPacketRate = true;
	int32 Ok = 0, Fail = 0;
	auto Expect = [&](bool c, const FString& What)
	{
		UE_LOG(LogAsdaEventBadgeTest, Log, TEXT("САМОТЕСТ-EVENTBADGE %s %s"), c ? TEXT("OK  ") : TEXT("FAIL"), *What);
		if (c) { ++Ok; } else { ++Fail; }
	};
	auto Icons = [&](int32& Ex, int32& Dr) { bool b = false; F->GetEventIconIndices(b, Ex, Dr); return b; };
	int32 Ex = -1, Dr = -1;

	F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
	Icons(Ex, Dr); Expect(Ex < 0 && Dr < 0, TEXT("0 полный сброс: значков нет"));
	// 1) монеты по 6039, смена карты гасит
	F->ApplyEventSet(true, 0, 3.f, true);
	Icons(Ex, Dr); Expect(Dr == 1 && Ex < 0, FString::Printf(TEXT("1 6039 вид 0 ×3: монеты ×3 (idx %d)"), Dr));
	F->ResetSessionState(EAsdaSessionResetReason::MapChanged);
	Icons(Ex, Dr); Expect(Dr < 0 && !F->bDropEventOn, TEXT("1 ревизия 49 С-1: смена карты гасит значок события (сервер пришлёт состояние новой карты)"));
	// 2) час + событие: событие важнее, шаг часов не перетирает
	F->ApplyEventBonusPacket(Hour(1.f));
	Icons(Ex, Dr); Expect(Ex == 2 && F->bHourExpOn, FString::Printf(TEXT("2 4301 час +1.0: опыт ×2 (idx %d)"), Ex));
	F->ApplyEventSet(true, 1, 3.f, true);
	Icons(Ex, Dr); Expect(Ex == 4, FString::Printf(TEXT("2 6039 опыт ×3 (сервер: событие ×2 + час): значок ×3 (idx %d)"), Ex));
	F->UpdateScheduledClock(-1);
	Icons(Ex, Dr); Expect(Ex == 4 && FMath::IsNearlyEqual(F->ExpMultiplier, 3.f),
		FString::Printf(TEXT("2 ревизия 49 С-2: шаг часов клиента не перетирает множитель события (×%.2f)"), F->ExpMultiplier));
	// 3) 6041 опыта — значок часа остаётся
	F->ApplyEventSet(true, 1, 1.f, false);
	Icons(Ex, Dr); Expect(Ex == 2 && F->bExpEventOn, FString::Printf(TEXT("3 ревизия 49 С-2: 6041 опыта не гасит значок часа — снова ×2 (idx %d)"), Ex));
	// 4) час кончается по часам клиента (остаток из 4301: 60·(12 − 10) − 0 = 120 мин)
	for (int32 i = 0; i < 125 && F->bEventActive; ++i) { F->UpdateScheduledClock(-1); }
	Icons(Ex, Dr); Expect(!F->bEventActive && Ex < 0 && !F->bExpEventOn, TEXT("4 ревизия 49 С-2: час кончился по времени — значок опыта погас"));
	// 5) смена карты час не гасит, полный сброс — гасит
	F->ApplyEventBonusPacket(Hour(1.f));
	F->ApplyEventSet(true, 0, 2.f, true);
	F->ResetSessionState(EAsdaSessionResetReason::MapChanged);
	Icons(Ex, Dr); Expect(Ex == 2 && Dr < 0, FString::Printf(TEXT("5 смена карты: монеты события погасли, значок часа ×2 остался (exp %d, drop %d)"), Ex, Dr));
	F->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
	Icons(Ex, Dr); Expect(Ex < 0 && Dr < 0 && !F->bEventActive, TEXT("5 полный сброс (выход в выбор персонажа): значков нет"));

	F->EventSchedule = SavedSched;
	F->bEventFallbackToPacketRate = SavedFallback;
	Put(F, Saved);
	AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaEventBadgeTest, TEXT("САМОТЕСТ-EVENTBADGE"), Ok, Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEventBadge(
	TEXT("asda.Buff.SelfTestEventBadge"),
	TEXT("Самотест значка события (21.11, ревизия 49 к P128): 6039/6041 и час 4301 хранятся раздельно, смена карты гасит событие, "
	     "час гаснет по времени, полный сброс — всё. Без сети. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaRunEventBadgeSelfTest(A, W); }));

#endif // !UE_BUILD_SHIPPING
