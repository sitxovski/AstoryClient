// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectsSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaEffectsSubsystem.cpp, дословный перенос): дамп и дев-команды бафов.
//   Общее для частей — в AsdaEffectsSubsystem_Private.h; шапка и реверс — в AsdaEffectsSubsystem.cpp.
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд

#include "AsdaNetSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaEffectsSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaEffectsSubsystem*.cpp

// ═════════════════════════════ дамп ════════════════════════════════════════════════════════════
FString UAsdaEffectsSubsystem::DumpEffects() const
{
	FString S;
	const int32 Self = SelfUserIdx();
	S += FString::Printf(TEXT("=== АКТИВНЫЕ ЭФФЕКТЫ === свой userIdx=%d, списков в памяти=%d\n"), Self, Lists.Num());
	for (const TPair<int32, FAsdaEffectList>& It : Lists)
	{
		S += FString::Printf(TEXT("  userIdx=%d%s: записей %d\n"),
			It.Key, (It.Key == Self) ? TEXT(" (СВОЙ)") : TEXT(""), It.Value.Items.Num());
		int32 i = 0;
		for (const FAsdaEffectEntry& E : It.Value.Items)
		{
			const TCHAR* TypeName = (E.Type == 0) ? TEXT("скилл") : (E.Type == 1) ? TEXT("соулмейт") : TEXT("предмет");
			const FAsdaSkillEffectMeta& M = (E.Type == 0) ? GetSkillMeta(E.Id) : EmptyMeta;
			S += FString::Printf(
				TEXT("   [%02d] тип=%d(%s) id=%d itemCode=%d остаток=%d с %s стек=%d вид(+6)=%d флаг(+2)=%d%s buff0SubType=%d (%s)\n"),
				i++, E.Type, TypeName, E.Id, E.ItemCode, E.RemainSec,
				E.IsPermanent() ? TEXT("[ПОСТОЯННЫЙ]") : TEXT(""),
				E.Stack, E.KindByte, E.IdFlag,
				(E.IdFlag >= 1201) ? TEXT(" [таймер скрыт: >=1201]") : TEXT(""),
				M.bValid ? M.Buff0SubType : -1,
				(E.Type == 2) ? TEXT("предмет→зелёный таймер")
				              : (M.bValid && M.Buff0SubType >= 2) ? TEXT("ДЕБАФФ→красный таймер")
				                                                  : TEXT("бафф→обычный таймер"));
		}
	}
	bool bCont = false; int32 Ex = -1, Dr = -1;
	GetEventIconIndices(bCont, Ex, Dr);
	static const TCHAR* kExpArt[7]  = { TEXT("×0.5"), TEXT("×1.5"), TEXT("×2"), TEXT("×2.5"), TEXT("×3"), TEXT("×3.5"), TEXT("×4") };
	static const TCHAR* kGoldArt[3] = { TEXT("×2"), TEXT("×3"), TEXT("×4") };
	S += FString::Printf(
		TEXT("  ИВЕНТ-БОНУС: вид=%d (%s) активен=%d осталось=%d мин множитель=×%.2f (бонус %.2f)\n"),
		EventType,
		EventType == 1 ? TEXT("опыт") : EventType == 2 ? TEXT("золото") : EventType == 3 ? TEXT("опыт+золото") : TEXT("нет"),
		bEventActive ? 1 : 0, EventRemainMin, EventRate, PktBonus);
	S += FString::Printf(
		TEXT("   пакет 4301: часы сервера %02d.%02d %02d:%02d:%02d; строка расписания %04d-%02d-%02d %02d..%02d ч\n"),
		ClockDay, ClockMonth, ClockHour, ClockMinute, PktSecond,
		PktSchedYear, PktSchedMonth, PktSchedDay, PktSchedStartHour, PktEndHour);
	S += FString::Printf(
		TEXT("   локальное расписание act_secession3: строк=%d совпадение=%d%s\n"),
		EventSchedule.Num(), bScheduleHit ? 1 : 0,
		bEventFallbackToPacketRate ? TEXT(" (★ при отсутствии совпадения берём множитель ИЗ ПАКЕТА — решение юзера)") : TEXT(""));
	S += FString::Printf(
		TEXT("   множители: ОПЫТ ×%.2f (вкл=%d) | ДРОП ×%.2f (вкл=%d)  [дроп = шанс выпадения предметов + больше золота]\n"),
		ExpMultiplier, bExpEventOn ? 1 : 0, DropMultiplier, bDropEventOn ? 1 : 0);
	// ★ ревизия 49 С-2: источники итога раздельно (событие 6039/6041 важнее часа 4301)
	S += FString::Printf(
		TEXT("   источники: событие 6039 опыт ×%.2f (вкл=%d) дроп ×%.2f (вкл=%d) | час 4301 опыт ×%.2f (вкл=%d) дроп ×%.2f (вкл=%d)\n"),
		SetExpMult, bSetExpOn ? 1 : 0, SetDropMult, bSetDropOn ? 1 : 0, HourExpMult, bHourExpOn ? 1 : 0, HourDropMult, bHourDropOn ? 1 : 0);
	S += FString::Printf(
		TEXT("   иконки: контейнер=%d | ОПЫТ idx=%d %s | ДРОП idx=%d %s%s\n"),
		bCont ? 1 : 0,
		Ex, (Ex >= 0 && Ex < 7) ? kExpArt[Ex] : TEXT("(скрыт)"),
		Dr, (Dr >= 0 && Dr < 3) ? kGoldArt[Dr] : TEXT("(скрыт)"),
		bHideEventIconAtX1 ? TEXT("  [при ×1 значок не показываем — решение юзера]") : TEXT(""));
	S += TEXT("   ★ у значка дропа есть только ×2/×3/×4; дробный множитель округляется вниз (эталон)\n");
	return S;
}

// ═════════════════════════════ КОНСОЛЬ (проверка БЕЗ сервера) ══════════════════════════════════
static UAsdaEffectsSubsystem* BuffOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
}

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
// Вывод — в системный журнал чата (юзер смотрит В ИГРЕ, а не в файл лога): тот же маршрут, что у
//   asda.Mini.* (UAsdaMessageSubsystem::PostText, категория Tip).
// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
static void BuffEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaBuff);
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffDump(
	TEXT("asda.Buff.Dump"),
	TEXT("Активные эффекты: все записи 40-байтной модели + состояние ивент-бонуса"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaEffectsSubsystem* E = BuffOf(W)) { BuffEcho(W, E->DumpEffects()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffAdd(
	TEXT("asda.Buff.Add"),
	TEXT("asda.Buff.Add <skillId> [сек] [стек] [вид(+6): 4=ПОСТОЯННЫЙ] [флаг(+2): >=1201 прячет таймер] — "
	     "эмуляция опкода 5272 (наложение скиллового баффа)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 5272: эффекты 132/547 меняют навык в 5256 и блокируют 6158.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Buff.Add"))) { return; }
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.Add <skillId> [сек] [стек] [вид] [флаг]")); return; }
		const int32 Id    = FCString::Atoi(*A[0]);
		const int32 Sec   = (A.Num() > 1) ? FCString::Atoi(*A[1]) : 120;
		const int32 Stack = (A.Num() > 2) ? FCString::Atoi(*A[2]) : 0;
		const int32 Kind  = (A.Num() > 3) ? FCString::Atoi(*A[3]) : 0;
		const int32 Flag  = (A.Num() > 4) ? FCString::Atoi(*A[4]) : 0;
		E->AddOrUpdateSkill(E->SelfUserIdx(), Id, Flag, Stack, Kind, Sec, 0);
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ВОЛНА 2. Офлайн-проверка двух визуальных каналов, добавленных вместе с гачей и премиумом.
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffBoost(
	TEXT("asda.Buff.Boost"),
	TEXT("asda.Buff.Boost <редкий 0/1> [userNum=свой] — эмуляция 5473/успеха гачи: эффект удачи "
	     "(eff_lucky_a_00/01) + звук (s_item_boostnormal/rare) на кости M_DUMMY/G_DUMMY"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.Boost <редкий 0/1> [userNum]")); return; }
		const bool  bRare = (FCString::Atoi(*A[0]) != 0);
		const int32 Idx   = (A.Num() > 1) ? FCString::Atoi(*A[1]) : E->SelfUserIdx();
		E->SpawnBoostFx(Idx, bRare);
		BuffEcho(W, FString::Printf(TEXT("эффект удачи (%s) на userIdx=%d"),
			bRare ? TEXT("редкий") : TEXT("обычный"), Idx));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffPm(
	TEXT("asda.Buff.PmFx"),
	TEXT("asda.Buff.PmFx <подтип 1/2/3/6/7/16/17> [userNum=свой] — эмуляция визуала премиум-баффа "
	     "(eff_0_char_99_PMbuff_a_00..06); прочие подтипы визуала не имеют, как и в эталоне"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.PmFx <подтип> [userNum]")); return; }
		const int32 Sub = FCString::Atoi(*A[0]);
		const int32 Idx = (A.Num() > 1) ? FCString::Atoi(*A[1]) : E->SelfUserIdx();
		BuffEcho(W, E->SpawnPmBuffFx(Idx, Sub)
			? FString::Printf(TEXT("эффект подтипа %d показан на userIdx=%d"), Sub, Idx)
			: FString::Printf(TEXT("у подтипа %d визуала нет (эталонный default)"), Sub));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffSoul(
	TEXT("asda.Buff.Soul"),
	TEXT("asda.Buff.Soul <слот 0..29> [сек] — эмуляция опкода 5229 (соулмейт-эффект)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.Soul <слот> [сек]")); return; }
		E->AddType1(E->SelfUserIdx(), FCString::Atoi(*A[0]), (A.Num() > 1) ? FCString::Atoi(*A[1]) : 300);
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffItem(
	TEXT("asda.Buff.Item"),
	TEXT("asda.Buff.Item <itemCode> [сек] — эмуляция опкода 5453 (предметный усилитель; 0 сек = бессрочный)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный предмет-бафф: подтип 15 отправил бы 5455 с этим кодом.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Buff.Item"))) { return; }
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.Item <itemCode> [сек]")); return; }
		const int32 Code = FCString::Atoi(*A[0]);
		int32 SubType = -1, Flag = 0, Cell = 0; FString Icon;
		if (!E->GetTable2(Code, SubType, Flag, Icon, Cell))
		{
			BuffEcho(W, FString::Printf(TEXT("в special_itemlist нет записи itemCode=%d"), Code));
			return;
		}
		E->AddType2(E->SelfUserIdx(), SubType, (A.Num() > 1) ? FCString::Atoi(*A[1]) : 600, Code);
		BuffEcho(W, FString::Printf(TEXT("предмет %d: subType=%d flag116=%d иконка='%s' ячейка=%d"), Code, SubType, Flag, *Icon, Cell));
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffDel(
	TEXT("asda.Buff.Remove"),
	TEXT("asda.Buff.Remove <id> [тип 0/1/2] — снять эффект (эмуляция 5268/5273/5230/5454)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.Remove <id> [тип]")); return; }
		const bool bOk = E->RemoveBySkillState(E->SelfUserIdx(), FCString::Atoi(*A[0]), (A.Num() > 1) ? FCString::Atoi(*A[1]) : 0);
		BuffEcho(W, bOk ? TEXT("снято") : TEXT("такой записи нет"));
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffClear(
	TEXT("asda.Buff.ClearAll"),
	TEXT("Эмуляция опкода 5306 — сброс всех эффектов КРОМЕ предметных (type 2 переживает очистку)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E) { return; }
		E->ClearAllStopFx(E->SelfUserIdx());
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffClearMode(
	TEXT("asda.Buff.ClearMode"),
	TEXT("asda.Buff.ClearMode <режим> — эмуляция опкода 5307 (10 = все дебаффы buff0SubType==2, "
	     "иначе снять buff0SubType 2/4/6 с buff0DamageType == режим)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.ClearMode <режим>")); return; }
		E->ClearByMode(E->SelfUserIdx(), FCString::Atoi(*A[0]));
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffEvent(
	TEXT("asda.Buff.Event"),
	TEXT("asda.Buff.Event <тип 1=EXP 2=Drop 3=оба> <бонус, напр. 1 = ×2> [часДо] — эмуляция опкода 4301"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 2) { BuffEcho(W, TEXT("использование: asda.Buff.Event <1|2|3> <бонус> [часДо]")); return; }
		FAsdaEventBonusPkt P;
		P.EventType = FCString::Atoi(*A[0]);
		P.BonusRate = FCString::Atof(*A[1]);
		const FDateTime N = FDateTime::Now();
		P.Month = N.GetMonth(); P.Day = N.GetDay(); P.Hour = N.GetHour(); P.Minute = N.GetMinute();
		P.EndHour = (A.Num() > 2) ? FCString::Atoi(*A[2]) : (N.GetHour() + 1);
		E->ApplyEventBonusPacket(P);
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// Полная матрица «вид бонуса × множитель → какая иконка» — доказательная команда: показывает,
//   какие комбинации ВООБЩЕ существуют в данных игры, и что покажет клиент на каждой.
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffEventTable(
	TEXT("asda.Buff.EventTable"),
	TEXT("Матрица ивент-бонусов: вид (1 опыт / 2 золото / 3 оба) × множитель → какая иконка покажется"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		static const TCHAR* kExpArt[7]  = { TEXT("EXP ×0.5"), TEXT("EXP ×1.5"), TEXT("EXP ×2"), TEXT("EXP ×2.5"),
		                                    TEXT("EXP ×3"),   TEXT("EXP ×3.5"), TEXT("EXP ×4") };
		static const TCHAR* kGoldArt[3] = { TEXT("дроп ×2"), TEXT("дроп ×3"), TEXT("дроп ×4") };
		FString S;
		S += TEXT("=== ИВЕНТ-БОНУСЫ: что реально есть в данных ===\n");
		S += TEXT(" ДВА вида: ОПЫТ и ДРОП (дроп = шанс выпадения предметов; вместе с ним больше падает\n");
		S += TEXT("   и золота — потому на значке и нарисованы монеты).\n");
		S += TEXT(" Механика живёт в BattleServer (NpcServer): ./Event/Event.ini, секции Item_* (дроп)\n");
		S += TEXT("   и Exp_* (опыт); *_Value в ПРОЦЕНТАХ (200 = x2, 150 = x1.5), окно дат/часов,\n");
		S += TEXT("   маска карт *_Map, MinLevel/MaxLevel. Рантайм-управление — опкоды 6039 SET / 6041 OFF.\n");
		S += TEXT(" Пакет 4301 — ОТДЕЛЬНЫЙ канал (таблица GameServerEvent.txt): один float + байт вида,\n");
		S += TEXT("   и сервер этой сборки ставит там вид ВСЕГДА 1 (опыт).\n");
		S += TEXT(" Иконки в игре: 7 значков ОПЫТА (x0.5..x4) и 3 значка ДРОПА (x2/x3/x4).\n");
		S += TEXT(" множ.  | опыт (idx: арт)        | дроп (idx: арт)\n");
		for (int32 i = 0; i <= 8; ++i)
		{
			const float Rate = 0.5f * (float)(i + 2);       // 1.0, 1.5, 2.0 … 5.0
			const int32 Ex = FMath::Max((int32)(Rate / 0.5f - 2.f), 0);
			const int32 Dr = FMath::Max((int32)(Rate - 2.f), 0);
			S += FString::Printf(TEXT(" ×%-5.2f| %-2d: %-18s | %-2d: %s\n"),
				Rate,
				Ex, (Ex < 7) ? kExpArt[Ex] : TEXT("— (за пределом, кламп на ×4)"),
				Dr, (Dr < 3) ? kGoldArt[Dr] : TEXT("— (за пределом, кламп на ×4)"));
		}
		S += TEXT(" ★ дробные множители для ДРОПА арта не имеют: x2.5 округляется вниз до значка x2.\n");
		S += TEXT(" ★ при множителе x1 значок НЕ показываем (решение юзера) — заодно снимается\n");
		S += TEXT("   расхождение оригинала, где idx 0 подписан x0.5, а выбирался при x1.\n");
		S += TEXT(" Проверить вживую:\n");
		S += TEXT("   asda.Buff.EventSet <0 дроп|1 опыт> <множитель>  — эмуляция 6039 (настоящий канал)\n");
		S += TEXT("   asda.Buff.EventSetOff <0|1>                     — эмуляция 6041\n");
		S += TEXT("   asda.Buff.Event <1|2|3> <бонус>                 — эмуляция 4301 (бонус = множитель-1)\n");
		BuffEcho(W, S);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ Эмуляция НАСТОЯЩЕГО канала ивентов — опкод 6039 (BattleServer): вид 0 = ДРОП, 1 = ОПЫТ.
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffEventSet(
	TEXT("asda.Buff.EventSet"),
	TEXT("asda.Buff.EventSet <0 дроп | 1 опыт> <множитель, напр. 2> — эмуляция опкода 6039 (EVENT SET)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 2) { BuffEcho(W, TEXT("использование: asda.Buff.EventSet <0 дроп|1 опыт> <множитель>")); return; }
		E->ApplyEventSet(/*bOk=*/true, FCString::Atoi(*A[0]), FCString::Atof(*A[1]), /*bOn=*/true);
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffEventSetOff(
	TEXT("asda.Buff.EventSetOff"),
	TEXT("asda.Buff.EventSetOff <0 дроп | 1 опыт> — эмуляция опкода 6041 (EVENT OFF)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.EventSetOff <0|1>")); return; }
		E->ApplyEventSet(/*bOk=*/true, FCString::Atoi(*A[0]), 1.f, /*bOn=*/false);
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffEventOff(
	TEXT("asda.Buff.EventOff"),
	TEXT("Выключить ивент-бонус (иконки EXP_Buff/Drop_Buff гаснут)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E) { return; }
		FAsdaEventBonusPkt P;   // EventType 0, EndHour 0 ⇒ остаток < 0 ⇒ bEventActive = false
		P.Hour = 1;
		E->ApplyEventBonusPacket(P);
		E->UpdateScheduledClock(9);
		BuffEcho(W, E->DumpEffects());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffCancel(
	TEXT("asda.Buff.Cancel"),
	TEXT("asda.Buff.Cancel <id> [typeFlag 0=скилл 2=предмет] — отправить опкод 6142 (отмена баффа), "
	     "как по клику на иконке"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.Cancel <id> [0|2]")); return; }
		const int32 Id = FCString::Atoi(*A[0]);
		const int32 Tf = (A.Num() > 1) ? FCString::Atoi(*A[1]) : 0;
		BuffEcho(W, E->RequestCancel(Tf, Id) ? TEXT("6142 отправлен") : TEXT("6142 НЕ отправлен (см. лог)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ═══════════ ★★★ ПРОВЕРКА ТРЕТЬЕГО КАНАЛА В ИГРЕ (эффекты состояний) ══════════════════════════
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffFxDump(
	TEXT("asda.Buff.FxDump"),
	TEXT("Состояния с визуалом: что висит на игроках и мобах, живы ли эффект-акторы, какая программа показа"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaEffectsSubsystem* E = BuffOf(W)) { BuffEcho(W, E->DumpBuffFx()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// Наложить состояние на СЕБЯ без сервера — эмуляция опкода 5272 (тот же путь, что у настоящего пакета).
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffFxApply(
	TEXT("asda.Buff.Fx"),
	TEXT("asda.Buff.Fx <id состояния 0..349> [секунды=60] — наложить состояние на себя (эмуляция 5272) "
	     "и показать его эффект"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.Fx <id 0..349> [секунды]")); return; }
		const int32 Id = FCString::Atoi(*A[0]);
		const int32 Sec = (A.Num() > 1) ? FCString::Atoi(*A[1]) : 60;
		if (Id < 0 || Id >= 350) { BuffEcho(W, TEXT("id состояния вне 0..349 (эталон: return 0)")); return; }
		const FAsdaBuffFxRow* R = E->GetBuffFxRow(Id);
		E->AddOrUpdateSkill(E->SelfUserIdx(), Id, /*IdFlag=*/0, /*Stack=*/0, /*KindByte=*/0, Sec, /*Byte9=*/99,
			/*CasterUserIdx=*/E->SelfUserIdx());
		BuffEcho(W, FString::Printf(TEXT("состояние %d «%s»: программа игрока = %s"),
			Id, R ? *R->BuffName : TEXT("нет в DT_BuffFx"),
			(R && !R->PlayerFx.IsEmpty()) ? *R->PlayerFx : TEXT("(пусто)")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// Наложить состояние на МОБА по renderId — эмуляция ветки сущности пакета 4102.
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffFxMob(
	TEXT("asda.Buff.FxMob"),
	TEXT("asda.Buff.FxMob <renderId> <id состояния 0..349> — наложить состояние на сущность "
	     "(эмуляция ветки моба в 4102) и показать его эффект"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 2) { BuffEcho(W, TEXT("использование: asda.Buff.FxMob <renderId> <id 0..349>")); return; }
		const int32 Rid = FCString::Atoi(*A[0]);
		const int32 Id  = FCString::Atoi(*A[1]);
		if (Id < 0 || Id >= 350) { BuffEcho(W, TEXT("id состояния вне 0..349")); return; }
		const FAsdaBuffFxRow* R = E->GetBuffFxRow(Id);
		E->AddOrUpdateMobSkill(Rid, Id, /*Stack=*/0, /*DurationSec=*/0, E->SelfUserIdx());
		BuffEcho(W, FString::Printf(TEXT("сущность %d ← состояние %d «%s»: программа сущности = %s"),
			Rid, Id, R ? *R->BuffName : TEXT("нет в DT_BuffFx"),
			(R && !R->MobFx.IsEmpty()) ? *R->MobFx : TEXT("(пусто)")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// Снять состояние (эмуляция 5268/5273) — проверка ГАШЕНИЯ.
static FAutoConsoleCommandWithWorldAndArgs GAsdaBuffFxStop(
	TEXT("asda.Buff.FxStop"),
	TEXT("asda.Buff.FxStop <id состояния> [renderId сущности] — снять состояние и погасить его эффект"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEffectsSubsystem* E = BuffOf(W);
		if (!E || A.Num() < 1) { BuffEcho(W, TEXT("использование: asda.Buff.FxStop <id> [renderId]")); return; }
		const int32 Id = FCString::Atoi(*A[0]);
		const bool bOk = (A.Num() > 1)
			? E->RemoveMobBySkillState(FCString::Atoi(*A[1]), Id)
			: E->RemoveBySkillState(E->SelfUserIdx(), Id, 0);
		BuffEcho(W, bOk ? TEXT("снято, эффект погашен") : TEXT("такого состояния в списке нет"));
	}));
#endif // ASDA_DEV_CONSOLE
