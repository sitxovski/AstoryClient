// Copyright (c) 2026 @sitxovski (Discord)
// Серверные часы обычной атаки — разбор, адреса и границы применения у объявления (`Public/World/AsdaAttackRhythmSubsystem.h`).
#include "World/AsdaAttackRhythmSubsystem.h"

#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR — дев-ключи вне Shipping
#include "AsdaNetSubsystem.h"               // надетое оружие своего персонажа (SelfWeaponItemId)
#include "Items/AsdaItemTableSubsystem.h"   // ItemSubType / AttackRangeTiles оружия
#include "UI/AsdaEffectsSubsystem.h"        // список состояний своего персонажа
#include "World/AsdaWorldSubsystem.h"       // величина состояния (единая формула с темпом)
#include "World/AsdaPlayerCharacter.h"      // снятие цикла замаха при навыке
#include "World/AsdaPlayerController.h"     // боевой тик: проверка возобновления каждый кадр после навыка
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "World/AsdaCombatTrace.h"      // ★ 18.10.2026: журнал боя — решения серверных часов атаки

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRhythm, Log, All);

// Таблицы NpcServer байт в байт (генератор `Tools/gen_server_anim_tables.py`, md5 источника — в шапке файла).
#include "AsdaServerAnimTables.inl"

// ★ Ключ часов. 1 — 4028 уходит не раньше серверной длительности, анимация замаха идёт по ней же; 0 — прежнее
//   поведение порта (ритм от прохода клипа, без серверного гейта). Откат целиком, без пересборки.
static ASDA_DEV_CVAR(int32, CVarServerRhythm,
	TEXT("asda.Combat.ServerRhythm"), 1,
	TEXT("1 = обычный удар 4028 не раньше серверной длительности (Combo.lst/Skill.lst NpcServer), замах идёт по ней же; ")
	TEXT("0 = как прежде (ритм от клипа, без гейта)."),
	ECVF_Default);

// ★★ ЗАПАС ОТПРАВКИ (ревизия СРЕДН-4). Удар уходил ровно в серверный дедлайн, и весь допуск сервера (сток 100 мс, N16 ещё
//   не выложен) уходил на джиттер: сервер мерит интервал по GetTickCount с квантом 15.6 мс (ошибка измерения до одного
//   кванта), а задержка ОДНОГО пакета в сети/очереди Process_Queue сжимает следующий интервал. 30 мс = два кванта
//   GetTickCount. Сверху запас ограничен долей длительности (3.5 %), чтобы честный бой оставался ≤ +5 % и у самых коротких
//   длительностей (×0.5: 450 мс → запас 15.75 мс ≈ один квант). После выкладки N16 (допуск 15 %) запас можно снять ключом 0.
static ASDA_DEV_CVAR(float, CVarServerRhythmMarginMs,
	TEXT("asda.Combat.ServerRhythmMarginMs"), 30.f,
	TEXT("Запас отправки удара 4028 к серверной длительности, мс (умолчание 30 = два кванта GetTickCount сервера). 0 — без запаса."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarServerRhythmMarginMaxPct,
	TEXT("asda.Combat.ServerRhythmMarginMaxPct"), 3.5f,
	TEXT("Потолок запаса отправки в процентах серверной длительности (умолчание 3.5 — честный бой не медленнее +5 %)."),
	ECVF_Default);

// ★ Строка лога на каждый принятый удар/навык (шаг, база, B, итог) — для живой сверки с журналом сервера.
static ASDA_DEV_CVAR(int32, CVarServerRhythmLog,
	TEXT("asda.Combat.ServerRhythmLog"), 1,
	TEXT("1 = печатать каждый удар/навык серверных часов атаки (шаг, база, бонус, длительность); 0 = молча."),
	ECVF_Default);

bool UAsdaAttackRhythmSubsystem::IsEnabled()
{
	return CVarServerRhythm.GetValueOnGameThread() != 0;
}

// ═══ Таблицы ═══════════════════════════════════════════════════════════════════════════════════════

int32 UAsdaAttackRhythmSubsystem::ComboBaseMs(int32 Wt, int32 Step, int32 Row)
{
	// Сервер индексирует без проверок (`sub_401900`); за пределами таблицы у него чужие данные — у нас 0 («проверки нет»).
	if (Row < 0 || Row > 1 || Wt < 0 || Wt >= 18 || Step < 0 || Step >= 6) { return 0; }
	return kAsdaServerCombo[Row][Wt][Step];
}

int32 UAsdaAttackRhythmSubsystem::ComboStepCount(int32 Wt, int32 Row)
{
	if (Row < 0 || Row > 1 || Wt < 0 || Wt >= 18) { return 0; }
	return kAsdaServerCombo[Row][Wt][6];
}

int32 UAsdaAttackRhythmSubsystem::SkillLockMs(int32 SkillCode)
{
	// Массив сервера — 1200 dword, файл заполняет 0…599, хвост 600…1199 обнулён (`sub_44FF20`). Коды ≥ 1200 сервер читает
	//   за массивом (содержимое памяти неизвестно) — считаем 0; записано в отчёт как нерешённое.
	if (SkillCode < 0 || SkillCode >= 1200) { return 0; }
	int32 Lo = 0, Hi = kAsdaServerSkillLockCount - 1;
	while (Lo <= Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		const int32 Code = kAsdaServerSkillLock[Mid][0];
		if (Code == SkillCode) { return kAsdaServerSkillLock[Mid][1]; }
		if (Code < SkillCode) { Lo = Mid + 1; } else { Hi = Mid - 1; }
	}
	return 0;
}

// ★ x86 `ftol` (усечение к нулю; NaN/±∞/вне диапазона → 0x80000000 «целое неопределённое») — так сервер кладёт double в int.
static int32 AsdaFtol(double X)
{
	if (!FMath::IsFinite(X) || X >= 2147483648.0 || X <= -2147483649.0) { return MIN_int32; }
	return (int32)X;
}

int32 UAsdaAttackRhythmSubsystem::ServerFormulaMs(int32 BaseMs, int32 BonusPct, bool bHalfMelee, bool bHalfRanged, int32 RangeTiles)
{
	// Дословно NpcServer.exe.c:63139-63163 (`sub_45F3A0`):
	//   v69 = B * dur;  v21 = (int)((double)(dur*dur) / ((double)v69 / 100.0 + (double)dur));  +0xFE94 = (i16)v21;
	//   if (+8601 == 1 && класс <= 3)  +0xFE94 = (int)((double)v21 * 0.5);
	//   if (+10933 == 1 && класс > 3)  +0xFE94 = (int)(0.5 * (double)(i16)+0xFE94);
	const int32 Dur = (int32)(int16)BaseMs;
	const int32 V69 = BonusPct * Dur;
	const int32 V21 = AsdaFtol((double)(Dur * Dur) / ((double)V69 / 100.0 + (double)Dur));
	int16 Stored = (int16)V21;
	const int8 Cls = (int8)RangeTiles;   // сервер сравнивает БАЙТ со знаком: `*(char *)(v8 + 30854) <= 3`
	if (bHalfMelee && Cls <= 3) { Stored = (int16)AsdaFtol((double)V21 * 0.5); }
	if (bHalfRanged && Cls > 3) { Stored = (int16)AsdaFtol(0.5 * (double)Stored); }
	return (int32)Stored;
}

// ═══ Свой персонаж ═════════════════════════════════════════════════════════════════════════════════

void UAsdaAttackRhythmSubsystem::GetSelfWeapon(int32& OutWt, int32& OutRangeTiles) const
{
	OutWt = 0; OutRangeTiles = 0;   // без оружия: GameServer не заполняет поля оружия в 8101 — нули
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Net && IT && Net->SelfWeaponItemId > 0)
	{
		if (const FAsdaItemRecord* R = IT->GetItem(Net->SelfWeaponItemId))
		{
			OutWt = R->ItemSubType;           // Item_Table столбец 17 → NpcServer +0xFE5F (сырой подтип, как у сервера)
			OutRangeTiles = R->AttackRangeTiles;   // Item_Table столбец 20 → NpcServer +0x7886 («класс»)
		}
	}
	if (TestWtOverride >= 0) { OutWt = TestWtOverride; }
	if (TestRangeOverride >= 0) { OutRangeTiles = TestRangeOverride; }
}

UAsdaAttackRhythmSubsystem::FDurParts UAsdaAttackRhythmSubsystem::ComputeDur(int32 InStep) const
{
	FDurParts P;
	GetSelfWeapon(P.Wt, P.RangeTiles);
	P.BaseMs = ComboBaseMs(P.Wt, InStep);
	UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	const int32 Uidx = Fx ? Fx->SelfUserIdx() : INDEX_NONE;
	if (WS && Fx && Uidx != INDEX_NONE)
	{
		// ★ Политика «в сомнении медленнее» (величина состояния у сервера — сила кастера, клиенту недоступна):
		//   ускорения ×1, замедления × max(1, стек) — сервер при повторном наложении складывает значения.
		// ★ Величина не ниже 0 — сервер зажимает силу навыка снизу нулём (`GameServer.c:47938-47940`). Положительная оценка
		//   ДОКАЗАНА равной серверной: сила = уровень · S + база из `SkillInfo.sif` сервера, и слоты 72/89/106 (состояние,
		//   база, за уровень) у всех 1120 записей сервера побайтно равны клиентскому `skill_info` (сверка 15.10.2026);
		//   модификаторы параметров 92/84/328 сервер применяет только к состояниям 50/51/190/210/147/148/47
		//   (`GameServer.c:48676-48702`), к нашим — нет. Сверх оценки у сервера может быть только сложение при повторном
		//   наложении — ускорение от этого лишь сильнее, значит клиент не раньше.
		auto Pos = [WS, Uidx](int32 Id) { return FMath::Max(0, WS->GetStateBonusPct(Uidx, Id)); };
		auto Neg = [WS, Uidx](int32 Id) { int32 St = 0; const int32 V = FMath::Max(0, WS->GetStateBonusPct(Uidx, Id, &St)); return V * FMath::Max(1, St); };
		int32 B = 0;
		B += Pos(27);                          // +0x5AF
		B -= Neg(28);                          // −0x5E4
		if (P.Wt == 11) { B += Pos(174); }     // +0x241E при баллисте
		if (P.Wt == 3)  { B += Pos(233); }     // +0x3055 при двуручном мече
		B -= Neg(247);                         // −0x333B
		B += Pos(52);                          // +0xADC  (у клиента оригинала этого слагаемого нет)
		B -= Neg(12);                          // −0x294
		// +0x1026A (GameServer параметр 106) — происхождение не установлено, только ускоряет сервер → не берём.
		P.BonusPct = FMath::Max(B, kMinBonusPct);   // ★ СРЕДН-6: монотонность — гейт не исчезает от переоценки замедления
		P.bHalf162 = Fx->FindByTypeSub(Uidx, 162, 0) != nullptr;   // +0x2199
		P.bHalf206 = Fx->FindByTypeSub(Uidx, 206, 0) != nullptr;   // +0x2AB5
	}
	P.DurMs = (P.BaseMs != 0) ? ServerFormulaMs(P.BaseMs, P.BonusPct, P.bHalf162, P.bHalf206, P.RangeTiles) : 0;
	return P;
}

AAsdaPlayerCharacter* UAsdaAttackRhythmSubsystem::SelfChar() const
{
	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	return PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
}

// ═══ Часы ══════════════════════════════════════════════════════════════════════════════════════════

void UAsdaAttackRhythmSubsystem::Mark(int32 Op, double AtS)
{
	FSendMark M; M.T = AtS; M.Op = Op; M.RequiredMs = RequiredMs;
	if (SendLog.Num() >= 64) { SendLog.RemoveAt(0, SendLog.Num() - 63); }
	SendLog.Add(M);
}

void UAsdaAttackRhythmSubsystem::Accept(int32 Op, double AtS)
{
	const double Now = AtS;
	const double Gap = (LastAcceptS > 0.0) ? (Now - LastAcceptS) * 1000.0 : -1.0;
	const int32 PrevReq = EffectiveRequiredMs();
	const int32 UsedStep = Step;
	const FDurParts P = ComputeDur(Step);                 // dur СЛЕДУЮЩЕГО удара — по шагу ДО продвижения (:63122-63131)
	RequiredMs = FMath::Max(0, P.DurMs);                  // ≤ 0 — у сервера проверки нет
	LastAcceptS = Now;
	LastAcceptStep = UsedStep;
	bLastAcceptWasHit = true;
	PendingAtS = 0.0;
	const int32 N = ComboStepCount(P.Wt);
	Step = (Step + 1 >= N) ? 0 : Step + 1;                // :63168
	Mark(Op, Now);
	ASDA_CTRACE(EAsdaCT::Send, TEXT("часы атаки: %d учтён, от прошлого %.0f мс (нужно было %d) | оружие wt=%d дальн=%d, шаг %d→%d, база %d мс, бонус %d%%%s%s → следующий 4028 не раньше %d мс (+запас %.1f)"),
		Op, Gap, PrevReq, P.Wt, P.RangeTiles, UsedStep, Step, P.BaseMs, P.BonusPct,
		P.bHalf162 ? TEXT(" ×0.5(162)") : TEXT(""), P.bHalf206 ? TEXT(" ×0.5(206)") : TEXT(""), RequiredMs, MarginMsFor(RequiredMs));
#if ASDA_COMBAT_TRACE
	if (Gap >= 0.0 && PrevReq > 0 && Op == 4028 && Gap + 0.5 < (double)PrevReq)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("часы атаки: 4028 ушёл РАНЬШЕ серверной длительности на %.0f мс (%.0f < %d) — сервер может отвергнуть"),
			(double)PrevReq - Gap, Gap, PrevReq);
	}
#endif
	if (CVarServerRhythmLog.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaRhythm, Log, TEXT("[ритм] %d ушёл: от прошлого %.0f мс (нужно было %d) | wt=%d дальн=%d шаг %d→%d база=%d B=%d%s%s → следующий не раньше %d мс"),
			Op, Gap, PrevReq, P.Wt, P.RangeTiles, UsedStep, Step, P.BaseMs, P.BonusPct,
			P.bHalf162 ? TEXT(" ×0.5(162)") : TEXT(""), P.bHalf206 ? TEXT(" ×0.5(206)") : TEXT(""), RequiredMs);
	}
}

void UAsdaAttackRhythmSubsystem::NoteSent4026(bool bMobReply, int32 TargetRid)
{
	// `sub_45FCD0`: шаг := 0, прошлый := 0 → тот же приём; 4026 сервер принимает всегда.
	ASDA_CTRACE(EAsdaCT::Send, TEXT("часы атаки: 4026 — новая серия (шаг %d → 0, прошлый удар забыт)"), Step);
	bSeries = true;
	Step = 0;
	// ★ 25.10.2026 (фантомный урон): 4026 сервер пересчитывает очередь заново (`sub_45FCD0` → `sub_45CEC0(…, 1)`, курсор 0) —
	//   непримененный хвост прошлой серии выброшен. Прошлая серия закрывается со своим счётом, новая начинается с одного удара.
	if (SeriesGen != 0)
	{
		ClosedSeries[ClosedNext].Gen = SeriesGen;
		ClosedSeries[ClosedNext].Accepted = SeriesAccepted;
		ClosedSeries[ClosedNext].Range = SeriesRange;
		ClosedNext = (ClosedNext + 1) % UE_ARRAY_COUNT(ClosedSeries);
	}
	++SeriesGen;
	SeriesAccepted = 1;
	SeriesTaken = 0;
	{
		int32 Wt = 0;
		GetSelfWeapon(Wt, SeriesRange);   // ★ ревизия М-3: `+65117` сервер посчитал на этом 4026 по дальности оружия сейчас
	}
	if (bMobReply) { PushAwait(TargetRid, 4026); }
	Accept(4026, FPlatformTime::Seconds());
}

void UAsdaAttackRhythmSubsystem::NoteSent4028(bool bMobReply, int32 TargetRid)
{
	++SeriesAccepted;   // ★ 25.10.2026: удар серии (принят, пока ответ не скажет обратное — `ResolveMobReply`)
	if (bMobReply) { PushAwait(TargetRid, 4028); }
	Accept(4028, FPlatformTime::Seconds());
}

void UAsdaAttackRhythmSubsystem::NoteSent4028At(double SentAtSec, int32 TargetRid)
{
	++SeriesAccepted;   // ★ 25.10.2026: удар серии (принят, пока ответ не скажет обратное — `ResolveMobReply`)
	PushAwait(TargetRid, 4028);
	Accept(4028, SentAtSec > 0.0 ? SentAtSec : FPlatformTime::Seconds());
}

// ★ Ревизия 25.10 (М-1). Ответ сервера приходит за время обмена (живые журналы — десятки–сотни мс); заявка без ответа дольше
//   kAwaitReplyMaxS отброшена сервером молча (ранний удар, ход) и забывается. Новая серия (4026) забывает и заявки старше
//   kAwaitReplyAtNewSeriesS: иначе ответ новой серии снял бы безответную заявку прошлой и был бы принят за поздний.
static constexpr double kAwaitReplyMaxS = 2.0;
static constexpr double kAwaitReplyAtNewSeriesS = 1.0;

// ★★★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ]: БЕЗОТВЕТНАЯ ЗАЯВКА УДАРА И НОВАЯ СЕРИЯ (ключ `asda.Combat.AwaitReplyByTarget`).
//   Дефект порта: заявка 4028, на которую сервер промолчал, оставалась в очереди до 2 с; новый бой, начатый раньше чем через
//   секунду, получал первый ответ, а очередь отдавала ему старую заявку — ответ считался «поздним, прошлой серии», его значения
//   не копились и не показывались, и вся новая серия шла со сдвигом на заявку.
//   Когда сервер молчит (декомпил, `Send_CharNormalMonsterAttackNext@0x4C5120`, GameServer.c:78499; 4026 — тот же порядок):
//     1) эхо проверки состояния 6001 (`sub_52BFF0`) с причиной 1…5 — заявка отклонена, боевого ответа нет;
//     2) после эха 6001 «ОК» — флаг пользователя `sub_523ED0 → sub_542250` (CUserMoveManager) поднят: молча, без пакета;
//     3) ранний удар: NpcServer (`sub_45F3A0`, NpcServer.exe.c:63292-63301, «SpeedHack Check», ветка `v12 < v11`) ответа 8204 не
//        шлёт — эхо 6001 было «ОК», цель та же: ни (а), ни (б) этот случай не различают, остаётся правило сроков;
//     4) выходы ДО эха 6001 (GameServer.c:78524-78531): оператор при поле +244795 == 300, провал `sub_524390`, провал
//        `sub_523F00` — нет ни эха, ни ответа; такая заявка тоже ждёт срока (учёт эха сдвигается на одну заявку).
//   По УМЕРШЕЙ цели сервер НЕ молчит: NpcServer (`Process_AttackedToMonster`, sub_45F3A0@0x45F3A0, NpcServer.exe.c:63093) отвечает
//   8204 с кодом 2, GameServer пересылает его клиенту ответом 4029 с этим кодом (`sub_4C5540`, GameServer.c:78645) — ответ есть,
//   заявку снимает обычный приём 4029.
//   Правка: (а) 6001 с причиной ≠ 0 на 4026/4028 снимает самую старую заявку этого опкода, ещё не получившую эхо (эхо идёт на
//   каждую заявку по порядку и раньше боевого ответа); (б) заявка хранит номер цели: ответ по мобу X снимает стоящие перед
//   заявкой по X заявки по другим целям — их сервер отбросил молча (ветка 2). Бой по тому же мобу веткой (б) не различим —
//   там остаётся прежнее правило сроков. 1 = правка; 0 = как было.
static ASDA_DEV_CVAR(int32, CVarAsdaAwaitReplyByTarget,
	TEXT("asda.Combat.AwaitReplyByTarget"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = заявка удара по мобу хранит цель и вид: отказ состояния 6001 и ответ сервера по другой цели ")
	TEXT("снимают безответные заявки (первый ответ нового боя не принимается за поздний). 0 = как было: заявка ждёт только срока."),
	ECVF_Default);

void UAsdaAttackRhythmSubsystem::DropAwaitAt(int32 Index, const TCHAR* Why)
{
	if (!AwaitReply.IsValidIndex(Index)) { return; }
	const FAwaitReply R = AwaitReply[Index];
	AwaitReply.RemoveAt(Index);
	if (int32* A = AcceptedRef(R.Gen)) { *A = FMath::Max(0, *A - 1); }
	ASDA_CTRACE(EAsdaCT::Queue, TEXT("часы атаки: заявка %d серии #%u по rid=%d снята БЕЗ ответа (%s), принятых ударов серии стало %d"),
		R.Op, R.Gen, R.Rid, Why ? Why : TEXT("?"), AcceptedRef(R.Gen) ? *AcceptedRef(R.Gen) : -1);
}

void UAsdaAttackRhythmSubsystem::NoteServerCheck(int32 Opcode, uint8 Reason)
{
	if (CVarAsdaAwaitReplyByTarget.GetValueOnGameThread() == 0) { return; }
	if (Opcode != 4026 && Opcode != 4028) { return; }
	for (int32 i = 0; i < AwaitReply.Num(); ++i)
	{
		if (AwaitReply[i].Op != Opcode || AwaitReply[i].bChecked) { continue; }
		if (Reason == 0) { AwaitReply[i].bChecked = true; }
		else { DropAwaitAt(i, TEXT("6001: сервер отклонил заявку по состоянию")); }
		return;
	}
}

void UAsdaAttackRhythmSubsystem::PushAwait(int32 TargetRid, int32 Op)
{
	const double Now = FPlatformTime::Seconds();
	const bool bNewSeries = AwaitReply.Num() > 0 && AwaitReply.Last().Gen != SeriesGen;   // заявка открывает новую серию
	const double MaxAge = bNewSeries ? kAwaitReplyAtNewSeriesS : kAwaitReplyMaxS;
	AwaitReply.RemoveAll([Now, MaxAge](const FAwaitReply& R) { return Now - R.AtS > MaxAge; });
	if (AwaitReply.Num() >= 16) { AwaitReply.RemoveAt(0); }   // предел: сервер отвечает на каждую заявку, хвост — потери
	FAwaitReply& R = AwaitReply.AddDefaulted_GetRef();
	R.Gen = SeriesGen; R.AtS = Now; R.Rid = TargetRid; R.Op = Op;
}

int32* UAsdaAttackRhythmSubsystem::AcceptedRef(uint32 Gen)
{
	if (Gen == 0) { return nullptr; }
	if (Gen == SeriesGen) { return &SeriesAccepted; }
	for (FClosedSeries& C : ClosedSeries) { if (C.Gen == Gen) { return &C.Accepted; } }
	return nullptr;
}

uint32 UAsdaAttackRhythmSubsystem::ResolveMobReply(bool bConsumed, const TCHAR* Why, int32 ReplyRid)
{
	const double Now = FPlatformTime::Seconds();
	AwaitReply.RemoveAll([Now](const FAwaitReply& R) { return Now - R.AtS > kAwaitReplyMaxS; });   // молча отброшенные сервером
	// ★ R5-fix8: ответ по мобу ReplyRid — заявки по ДРУГИМ целям, стоящие перед заявкой по этой, сервер отбросил молча.
	if (CVarAsdaAwaitReplyByTarget.GetValueOnGameThread() != 0 && ReplyRid >= 0 && AwaitReply.Num() > 1
		&& AwaitReply[0].Rid >= 0 && AwaitReply[0].Rid != ReplyRid)
	{
		const int32 Match = AwaitReply.IndexOfByPredicate([ReplyRid](const FAwaitReply& R) { return R.Rid == ReplyRid; });
		for (int32 i = 0; i < Match; ++i) { DropAwaitAt(0, TEXT("ответ сервера пришёл по другой цели — эту заявку он отбросил молча")); }
	}
	if (AwaitReply.Num() == 0) { return 0; }
	const uint32 Gen = AwaitReply[0].Gen;
	AwaitReply.RemoveAt(0);
	if (!bConsumed)
	{
		if (int32* A = AcceptedRef(Gen)) { *A = FMath::Max(0, *A - 1); }
		ASDA_CTRACE(EAsdaCT::Queue, TEXT("часы атаки: ответ на удар серии #%u — очередь НЕ расходовалась (%s), принятых ударов серии стало %d"),
			Gen, Why ? Why : TEXT("?"), AcceptedRef(Gen) ? *AcceptedRef(Gen) : -1);
	}
	return Gen;
}

void UAsdaAttackRhythmSubsystem::NoteSeriesAbandoned(const TCHAR* Why)
{
	if (SeriesGen == 0 || MoveAbandonGen == SeriesGen) { return; }
	MoveAbandonGen = SeriesGen;
	ASDA_CTRACE(EAsdaCT::Queue, TEXT("часы атаки: серия #%u брошена (%s; принято ударов %d — её неприменённые значения сервер не спишет)"),
		SeriesGen, Why ? Why : TEXT("?"), SeriesAccepted);
}

int32 UAsdaAttackRhythmSubsystem::ServerHitsPerSwing(int32 RangeTiles)
{
	// [ОРИГИНАЛ] NpcServer `sub_45CEC0` :61950-61956: `+65116 = 4; +65117 = (класс > 3) ? (класс ≤ 16) : 2`, где «класс» (+0x7886) —
	//   дальность оружия в тайлах (см. шапку). Дальше 16 сервер не расходует очередь вовсе (цикл расхода выходит сразу).
	if (RangeTiles > 3) { return (RangeTiles <= 16) ? 1 : 0; }
	return 2;
}

void UAsdaAttackRhythmSubsystem::NoteFreeMove()
{
	NoteSeriesAbandoned(TEXT("ход не к цели"));
}

int32 UAsdaAttackRhythmSubsystem::TakeSeriesOrdinals(int32 Count)
{
	const int32 First = SeriesTaken;          // номер первого снятого значения в серии
	SeriesTaken += FMath::Max(0, Count);
	return First;
}

int32 UAsdaAttackRhythmSubsystem::ServerCommittedEntries(uint32 Gen) const
{
	int32 Accepted = -1;   // принятых ударов серии Gen
	int32 Range = 0;       // ★ ревизия М-3: дальность оружия на начале серии Gen
	if (Gen == SeriesGen) { Accepted = SeriesAccepted; Range = SeriesRange; }
	else
	{
		for (const FClosedSeries& C : ClosedSeries) { if (C.Gen == Gen && Gen != 0) { Accepted = C.Accepted; Range = C.Range; break; } }
	}
	if (Accepted < 0) { return -1; }
	return Accepted * ServerHitsPerSwing(Range);
}

void UAsdaAttackRhythmSubsystem::NotePending4028(double DeadlineSec)
{
	ASDA_CTRACE(EAsdaCT::Send, TEXT("часы атаки: 4028 поставлен к дедлайну через %.1f мс (шаг %d)"),
		(DeadlineSec - FPlatformTime::Seconds()) * 1000.0, Step);
	PendingAtS = DeadlineSec;
}

void UAsdaAttackRhythmSubsystem::ClearPending4028()
{
	if (PendingAtS > 0.0)
	{
		ASDA_CTRACE(EAsdaCT::Send, TEXT("часы атаки: поставленный 4028 снят (дедлайн был через %.1f мс)"), (PendingAtS - FPlatformTime::Seconds()) * 1000.0);
	}
	PendingAtS = 0.0;
}

// ★ СРЕДН-3: сервер считает длительность в момент ПРИЁМА удара по своему состоянию; о снятии ускорения, наложении
//   замедления или смене оружия клиент узнаёт на RTT позже. Пока идёт интервал, берём максимум из посчитанного при ударе
//   и пересчитанного сейчас по тому же шагу. Ускорение, наложенное после удара, требование не сокращает (максимум) —
//   это не более одного удара медленнее, зато никогда раньше.
int32 UAsdaAttackRhythmSubsystem::EffectiveRequiredMs() const
{
	if (!bLastAcceptWasHit) { return RequiredMs; }
	return FMath::Max(RequiredMs, FMath::Max(0, ComputeDur(LastAcceptStep).DurMs));
}

double UAsdaAttackRhythmSubsystem::MarginMsFor(int32 ReqMs)
{
	if (ReqMs <= 0) { return 0.0; }
	const double Abs = FMath::Max(0.0, (double)CVarServerRhythmMarginMs.GetValueOnGameThread());
	const double Pct = FMath::Max(0.0, (double)CVarServerRhythmMarginMaxPct.GetValueOnGameThread());
	return FMath::Min(Abs, (double)ReqMs * Pct * 0.01);
}

double UAsdaAttackRhythmSubsystem::EarliestNext4028S() const
{
	if (!IsEnabled() || LastAcceptS <= 0.0) { return 0.0; }
	const int32 Req = EffectiveRequiredMs();
	if (Req <= 0) { return 0.0; }
	return LastAcceptS + ((double)Req + MarginMsFor(Req)) * 0.001;
}

double UAsdaAttackRhythmSubsystem::PlanSegmentEndS(double NowS) const
{
	const FDurParts P = ComputeDur(Step);
	const double NomS = (P.DurMs > 0) ? ((double)P.DurMs + MarginMsFor(P.DurMs)) * 0.001 : 0.0;
	if (PendingAtS > 0.0) { return (NomS > 0.0) ? PendingAtS + NomS : 0.0; }
	const double E = EarliestNext4028S();
	if (E > NowS + 0.005) { return E; }
	return (NomS > 0.0) ? NowS + NomS : 0.0;
}

void UAsdaAttackRhythmSubsystem::NoteSkillStart(int32 SkillCode)
{
	// `SkillStart` :17110-17116: прошлый := такт, нужно := Skill.lst[навык]; шаг серии не трогается.
	// ★ НИЗК-1 (ревизия): если GameServer отверг навык (откат, MP, состояние — `GameServer.c:45955-46150`), у NpcServer
	//   остаётся прежний `+0xFE94`. Поэтому дедлайн только УДЛИНЯЕМ (максимум с текущим), а коды ≥ 1200 (за таблицей
	//   Skill.lst, сервер читает чужую память) его не трогают вовсе.
	const double Now = FPlatformTime::Seconds();
	const int32 Lock = SkillLockMs(SkillCode);
	const double OldEndMs = (LastAcceptS > 0.0) ? LastAcceptS * 1000.0 + (double)EffectiveRequiredMs() : 0.0;
	const bool bReplace = SkillCode >= 0 && SkillCode < 1200 && (Now * 1000.0 + (double)Lock >= OldEndMs);
	if (bReplace)
	{
		RequiredMs = Lock;
		LastAcceptS = Now;
		bLastAcceptWasHit = false;
	}
	PendingAtS = 0.0;   // поставленный удар отправитель уже снял до заявки (`SendSkillUseBody`)
	Mark(5256, Now);
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("часы атаки: навык %d, Skill.lst %d мс — %s; следующий 4028 не раньше %d мс от отметки, шаг серии %d сохранён; цикл замаха %s"),
		SkillCode, Lock, bReplace ? TEXT("дедлайн продлён") : TEXT("прежний дедлайн позже/код вне таблицы — оставлен"),
		EffectiveRequiredMs(), Step, IsEnabled() ? TEXT("снимается (состояние 25), отложенный 4028 снят") : TEXT("не трогается (asda.Combat.ServerRhythm 0)"));
	// ★ 17.10.2026 (ревизия правки боя «HP и смерть цели раньше выстрела», п.6): заявка навыка снимает цикл замаха
	//   (эталон: состояние 25) — оценка попадания по отправке удара больше не действует. Окно «наш замах в полёте»
	//   сокращается до попадания, назначенного ответом сервера, или закрывается (`UAsdaWorldSubsystem::CutOurImpactWindow`).
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		WS->CutOurImpactWindow(TEXT("заявка навыка"));
	}
	if (CVarServerRhythmLog.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaRhythm, Log, TEXT("[ритм] навык %d: Skill.lst %d мс — %s; следующий 4028 не раньше %d мс от отметки, шаг серии %d сохранён"),
			SkillCode, Lock, bReplace ? TEXT("дедлайн продлён") : TEXT("прежний дедлайн позже/код вне таблицы — оставлен"),
			EffectiveRequiredMs(), Step);
	}
	// Эталон: каждая из восьми заявок навыка переводит персонажа в состояние 25 (skill_buff.c:2237 и др.) — тик замаха
	//   (состояние 5) больше не идёт, 4028 из него не уходит. Порт: снимаем цикл замаха своего персонажа; возобновит
	//   его боевой тик контроллера, когда показ навыка кончится и часы позволят (`AAsdaPlayerController::AttackTick`).
	if (IsEnabled())
	{
		if (AAsdaPlayerCharacter* Ch = SelfChar())
		{
			Ch->SuspendAttackLoopForSkill();
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Ch->GetController()))
			{
				PC->NextAttackTickTime = 0.0;   // боевой тик проверяет «навык свободен, часы позволяют» каждый кадр
				PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);   // отложенный 4028 после заявки не уходит
			}
		}
	}
}

double UAsdaAttackRhythmSubsystem::SecondsUntilNext4028() const
{
	const double E = EarliestNext4028S();
	return (E > 0.0) ? FMath::Max(0.0, E - FPlatformTime::Seconds()) : 0.0;
}

bool UAsdaAttackRhythmSubsystem::CanSend4028(double* OutWaitS) const
{
	const double W = SecondsUntilNext4028();
	if (OutWaitS) { *OutWaitS = W; }
	return W <= 0.0;
}

double UAsdaAttackRhythmSubsystem::NominalNextIntervalS() const
{
	const FDurParts P = ComputeDur(Step);
	return (P.DurMs > 0) ? ((double)P.DurMs + MarginMsFor(P.DurMs)) * 0.001 : 0.0;
}

void UAsdaAttackRhythmSubsystem::DebugReset()
{
	bSeries = false; Step = 0; RequiredMs = 0; LastAcceptS = 0.0; LastAcceptStep = 0; bLastAcceptWasHit = false; PendingAtS = 0.0;
	SendLog.Reset();
	// ★ 25.10.2026 (фантомный урон): сброс открывает новую ПУСТУЮ серию (номер не обнуляется — удары до сброса не совпадут номером
	//   с новыми и при показе сочтутся хвостом прошлой серии); прошлые серии забываются.
	for (FClosedSeries& C : ClosedSeries) { C = FClosedSeries(); }
	ClosedNext = 0;
	++SeriesGen;
	SeriesAccepted = 0; SeriesTaken = 0; MoveAbandonGen = 0;
	AwaitReply.Reset();   // ★ ревизия М-1: заявки прошлой сессии ответов не получат
	{
		int32 Wt = 0;
		GetSelfWeapon(Wt, SeriesRange);
	}
}

void UAsdaAttackRhythmSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	DebugReset();
	ASDA_CTRACE(EAsdaCT::Note, TEXT("часы атаки: сброс сессии (%s) — серии нет, часы обнулены"), AsdaSessionResetReasonName(Reason));
	UE_LOG(LogAsdaRhythm, Log, TEXT("[ритм] сброс сессии (%s): серии нет, часы обнулены"), AsdaSessionResetReasonName(Reason));
}
