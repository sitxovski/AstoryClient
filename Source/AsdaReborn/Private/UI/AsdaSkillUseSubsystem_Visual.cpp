// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillUseSubsystem_Visual.cpp — часть `AsdaSkillUseSubsystem.cpp`: визуал навыка, нотисы отказа, тайминги, симуляция, дампы, дев-команды.
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaSkillUseSubsystem.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaSkillUseSubsystemInternal.h`. Шапка системы и ссылки на эталон — в `AsdaSkillUseSubsystem.cpp` и заголовке класса.

#include "UI/AsdaSkillUseSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaNoticeColors.h"   // ★ 08.11.2026: золото плашки отказа навыка (−11514)
#include "World/AsdaPossessGate.h"   // ★ гейт действий под GM-«скрыться» (AsdaGmHide)

#include "UI/AsdaCastBarSubsystem.h"
#include "AsdaBytes.h"                  // ★ 31.10.2026 (баг 7): разбор шапки 5232
#include "UI/AsdaSoulmateSkillSubsystem.h"   // ★ 31.10.2026 (баг 7): вид навыка соулмейта rec[50] для гейта 5231
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайлы
#include "World/AsdaPlayerController.h"
// ★ Сетка проходимости карты — наш аналог `GroundTarget_PickOnMesh_thunk` для точки отброса.
#include "World/AsdaWalkGrid.h"
#include "World/AsdaPathfinder.h"          // ★ 08.11.2026: рывок 537/597 — земля на пути до цели (13219)
#include "World/AsdaWorldSubsystem.h"
// ★ Источник максимума полосы применения (COMBO0) — таблица клипов навыков DT_SkillClips.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "Skill/AsdaSkillClipRow.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — нажатие навыка, гейты, подход, заявка, ответ, отказ

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaPvpRelation.h"          // ★ 26.10.2026 (bowfx): отношение к цели-персонажу (PvP, дуэль, команда, свои)
#include "AsdaSkillUseSubsystemInternal.h"   // ★ R2: общие помощники и объявления частей AsdaSkillUseSubsystem*.cpp (после включений пролога)

// ★★★ ЗАПУСК ВИЗУАЛЬНОГО СЛОЯ НА УСПЕШНОМ КАСТЕ (эталон `Recv_SkillCast@0xF4E370`: загрузчик
//   массива 0 + `CChar_SetState(pChar, 24)`; дальше показ ведёт ТИК массива 5).
//   Что именно показывается — клип применения, эффекты каста на кости оружия, пороги фаз
//   COMBO0/1/2 с эффектом и звуком удара, носители снарядов — целиком живёт в
//   `UAsdaSkillFxSubsystem` (реверс: `Reports/skill_fx_anim.md` §11).
//   ⚠ Прежде здесь стояла честная запись «визуального слоя нет»: на 2026-08-09 не были сняты ни
//   таблица загрузчиков, ни клипы, ни эффекты. Все три источника сняты (§11.6, 1664 строки
//   `DT_SkillClips`, 203/203 клипа), поэтому запись заменена на настоящий вызов; предупреждение
//   осталось ровно для тех навыков, у которых строки или ассета всё ещё нет.
// ★★★ КАСТЕР БЕРЁТСЯ ПО ИНДЕКСУ ИЗ ПАКЕТА — порт `CharManager_FindByUserIdx`. Раньше здесь стоял
//   `GetLocalChar()`, и это была ровно та строка, из-за которой чужие касты были невидимы (§28.8):
//   записи урона соседа приезжали (секторный броадкаст), но разбирать их было некому, и они
//   выпускались сторожем полки через 0.35 с — без клипа, без эффектов, без привязки к кадру удара.
void UAsdaSkillUseSubsystem::StartSkillVisual(int32 CasterUserIdx, int32 SkillCode)
{
	UAsdaSkillFxSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
	AAsdaPlayerCharacter* C = CharByUserIdx(CasterUserIdx);
	const bool bMine = (CasterUserIdx == SelfUserIdx());
	if (!Fx || !C)
	{
		// Сосед вне зоны видимости / ещё не заспавнен — у эталона `CharManager_FindByUserIdx` тоже
		//   вернёт null и обработчик выйдет. Пишем в лог, чтобы «немой» каст не выглядел загадкой.
		UE_LOG(LogAsdaSkillUse, Warning,
			TEXT("[навыки] навык %d: показ не запущен — %s (кастер uidx=%d%s)"), SkillCode,
			Fx ? TEXT("персонажа-кастера нет в мире") : TEXT("нет подсистемы визуала навыков"),
			CasterUserIdx, bMine ? TEXT(" мой") : TEXT(" сосед"));
		ASDA_CTRACE(EAsdaCT::Fx, TEXT("показ навыка %d у userIdx=%d НЕ запущен: %s"), SkillCode, CasterUserIdx,
			Fx ? TEXT("кастера нет в мире") : TEXT("нет подсистемы визуала"));
		return;
	}
	const bool bTrPlayed = Fx->PlaySkillVisual(C, SkillCode, CasterUserIdx);
	if (bMine && bTrPlayed) { SelfShownSkill = SkillCode; }   // ★ баг 7 (ревизия п.3): чей показ сейчас у своего персонажа
	ASDA_CTRACE(EAsdaCT::Fx, TEXT("показ навыка %d у userIdx=%d%s: %s"), SkillCode, CasterUserIdx, bMine ? TEXT(" (я)") : TEXT(" (сосед)"),
		bTrPlayed ? TEXT("ЗАПУЩЕН (клип + эффекты, урон привяжется к кадру удара)") : TEXT("НЕТ показа — урон по приходу пакета, без кадра удара"));
	if (!bTrPlayed)
	{
		// Причину печатает сама подсистема (строки нет / клип не импортирован / ассет не загрузился).
		UE_LOG(LogAsdaSkillUse, Log,
			TEXT("[навыки] навык %d (кастер uidx=%d%s): показа нет — урон применится по приходу пакета ")
			TEXT("(без привязки к кадру удара)"), SkillCode, CasterUserIdx,
			bMine ? TEXT(" мой") : TEXT(" сосед"));
	}
}

// Погасить показ (отказ сервера / прерывание). Эталон: `effectSlots[100].field_4 = −1` — и он тоже
//   работает по кастеру ИЗ ПАКЕТА, а не по локальному игроку.
void UAsdaSkillUseSubsystem::StopSkillVisual(int32 CasterUserIdx, const TCHAR* Why)
{
	UAsdaSkillFxSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
	AAsdaPlayerCharacter* C = CharByUserIdx(CasterUserIdx);
	if (!Fx || !C) { return; }
	if (!Fx->IsSkillVisualActive(C)) { return; }
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] показ навыка погашен у uidx=%d%s: %s"),
		CasterUserIdx, (CasterUserIdx == SelfUserIdx()) ? TEXT(" (мой)") : TEXT(" (сосед)"), Why);
	ASDA_CTRACE(EAsdaCT::Fx, TEXT("показ навыка у userIdx=%d%s ПОГАШЕН: %s"), CasterUserIdx,
		(CasterUserIdx == SelfUserIdx()) ? TEXT(" (я)") : TEXT(" (сосед)"), Why);
	Fx->StopSkillVisual(C);
}

// Публичная обёртка для канала 6161 Хранителя (объявление и доказательство — в заголовке).
void UAsdaSkillUseSubsystem::CancelSkillVisualFor(int32 CasterUserIdx, const TCHAR* Why)
{
	StopSkillVisual(CasterUserIdx, Why);
}

// Персонаж по сессионному индексу. Свой павн и сосед из реестра — один и тот же класс
//   `AAsdaPlayerCharacter`; сам поиск — единственный в проекте `UAsdaWorldSubsystem::ActorByUserIdx`.
AAsdaPlayerCharacter* UAsdaSkillUseSubsystem::CharByUserIdx(int32 UserIdx) const
{
	if (UserIdx < 0) { return GetLocalChar(); }   // сентинел пакета −1 = «ответ лично мне»
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return nullptr; }
	return Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(UserIdx));
}

void UAsdaSkillUseSubsystem::PostSkillNotice(int32 LocId, const FString& Fallback) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): фолбэк необязателен — пусто и нет строки → плейсхолдер маршрутизатора «[#id]».
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::SkillRefused, LocId, Fallback);
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] нотис %d: %s"), LocId, *Fallback);
}

// ★ 08.11.2026 [ОРИГИНАЛ] (решение владельца «Да, исправить», пачка 16): канал серверного отказа навыка. Оба пути эталона —
//   `Recv_SkillCast@0xF4E370` (5257, network.c:44962-44968) и `recvOtherCharSkillUse@0xF4D7D0` (5271, :44633-44639) — на ЛЮБОЙ
//   код ≠ 1 и только у своего персонажа (`CClass_GetField47565` = флаг выбора) делают одно и то же:
//   `UI_FormatLocalizedNotice(dword_15B5D78[код])` → `CControl_SetTooltipText(g_pChatWnd, текст, −11514, 10000)` — ПЛАШКА
//   (золото, 10 с, сверху по центру), без строки чата, без особых веток по кодам и без подавления повторов. Порт выводил весь
//   отказ строкой системного чата (`AsdaNotice::Chat`). 0 — прежний порт (строка чата).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillFailNoticePlank, TEXT("asda.Skill.FailNoticePlank"), 1,
	TEXT("[ОРИГИНАЛ 08.11.2026] 1 = серверный отказ навыка (5257/5271, таблица dword_15B5D78) — плашкой сверху по центру, как оригинал; "
	     "0 = прежний порт (строка системного чата)."),
	ECVF_Default);

// Таблица 35 кодов отказа `dword_15B5D78` живёт в `UAsdaNetSubsystem::SkillFailNoticeLocId`
//   (единственная копия — закон дедупа).
void UAsdaSkillUseSubsystem::PostFailNotice(int32 ResultCode, int32 SkillCode) const
{
	const int32 LocId = UAsdaNetSubsystem::SkillFailNoticeLocId(ResultCode);
	ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: отказ сервера код %d → нотис %d%s"), SkillCode, ResultCode, LocId,
		LocId == 0 ? TEXT(" (в эталонной таблице нотиса нет — молча)") : TEXT(""));
	if (LocId == 0)
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] отказ по навыку %d: код %d — в эталонной таблице нотиса нет (0)"),
			SkillCode, ResultCode);
		return;
	}
	if (CVarAsdaSkillFailNoticePlank.GetValueOnGameThread() != 0)
	{
		AsdaNotice::PlankLoc(GetGameInstance(), EAsdaMsgCategory::SkillRefused, LocId, AsdaNoticeColor::Gold, 10000.f, LocId);
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] отказ по навыку %d: код %d → плашка %d (как оригинал)"), SkillCode, ResultCode, LocId);
		return;
	}
	PostSkillNotice(LocId);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка (нет строки → «[#id]»)
}

// ══════════════════════════════ ТАЙМИНГИ ПОЛОСЫ ════════════════════════════════════════════════

// Тайминги из `combo time.bin` → `DT_combo_time`. Столбцы `FrameTimeA*` = блок `+48` (ПОЛНАЯ
//   длительность фазы), `FrameTimeB*` = блок `+84` (КАДРЫ УДАРА, они же COMBO0..2).
//   Проверено на ground truth отчёта §1.7: `ch4_m_01_dswing_00` → B0 = 20 кадров × 33.333 = 666.7 мс
//   (в отчёте «удар[0] 666.7»), A0 = 45 × 33.333 = 1500 мс («полная длит. 1500»).
// ★★★ Аудит №3 (A3/Дд2): своей загрузки таблицы и своей кэш-карты `ComboByClip` здесь БОЛЬШЕ НЕТ.
//   Загрузка идёт через `UAsdaItemTableSubsystem` (таблица в его манифесте), индекс «имя клипа →
//   строка» — один на порт (`AAsdaPlayerCharacter::ComboTimeIndex`); там же живёт кадр→мс
//   (`kFrameToMs`, Дд6 — прежде та же константа была под двумя именами).
FAsdaSkillCastTiming UAsdaSkillUseSubsystem::TimingFromComboRow(const FString& ClipName) const
{
	FAsdaSkillCastTiming T;
	const FAsdaComboTimeRow* R = AAsdaPlayerCharacter::FindComboTimeRow(this, ClipName);
	if (!R) { return T; }
	T.ClipName = R->ClipName;
	T.Combo0Ms = R->FrameTimeB0 * AAsdaPlayerCharacter::kFrameToMs;
	T.FullMs   = R->FrameTimeA0 * AAsdaPlayerCharacter::kFrameToMs;
	T.bResolved = (T.Combo0Ms > 0.f);
	return T;
}

void UAsdaSkillUseSubsystem::SetSkillClip(int32 SkillCode, const FString& ClipName)
{
	if (ClipName.IsEmpty()) { SkillClipOverride.Remove(SkillCode); return; }
	SkillClipOverride.Add(SkillCode, ClipName);
}

// ★★★ ИСТОЧНИК МАКСИМУМА ПОЛОСЫ. Порядок 1:1 с эталоном (отчёт §1.7):
//   (A) навыки с записью в `SkillInfo.inl` — COMBO0 лежит прямо в записи (+12044).
//       В порт `SkillInfo.inl` НЕ СКОПИРОВАН (отчёт §8.3 п.6), и в этом срезе клиента 38 из
//       41 записи всё равно НЕДОСТИЖИМЫ (§7.6) — источник даёт максимум 3 навыка (902/903/905).
//   (B) остальные ~99 % — ХАРДКОД-загрузчики: имя клипа зашито в код, тайминги берутся из
//       `combo time.bin`. Таблица «код навыка → загрузчик → имя клипа»
//       (`CharSkillEffect_InitDispatchTable@0xA89510`) в срезе НЕ СНЯТА — честная дыра №1 отчёта.
//   ⇒ ★★★ ТАБЛИЦА ЗАГРУЗЧИКОВ СНЯТА (2026-08-09, `DT_SkillClips` — 1664 строки, источник
//     `Reports/skill_clip_table.tsv`) и живёт в `UAsdaSkillFxSubsystem`. С 2026-08-10 максимум
//     полосы берётся ИМЕННО ОТТУДА — `ResolveRow(навык, пол, подтип оружия)` → `Combo0Ms`
//     (кадр первого удара, мс) и `PhaseDurMs` (полная длительность фазы, мс).
//   ⚠ ДЕФЕКТ, КОТОРЫЙ ЭТИМ ЗАКРЫТ: до правки функция читала ТОЛЬКО ручную карту `SkillClipOverride`
//     (пустую, если не звать `asda.Skill.SetClip`), поэтому на КАЖДЫЙ навык возвращался ноль,
//     полоса получала `max = 0` → подмену `max = 1 мс` → мгновенно заполнялась на 100 % и стояла.
//     В живом логе 2026-08-10 это видно построчно: навыки 707/723/784/787/791 → «COMBO0 = 0», хотя
//     в `DT_SkillClips` у них 500 / 2133.3 / 400 / 400 / 400 мс соответственно.
//   Ручная привязка `SkillClipOverride` СОХРАНЕНА и имеет ПРИОРИТЕТ — ею отлаживают строки,
//     которых в таблице нет (её ставит консоль `asda.Skill.SetClip`).
FAsdaSkillCastTiming UAsdaSkillUseSubsystem::ResolveTiming(int32 SkillCode) const
{
	FAsdaSkillCastTiming Out;
	const FString* Clip = SkillClipOverride.Find(SkillCode);
	if (!Clip)
	{
		// Путь (B): таблица хардкод-загрузчиков. Ветка выбирается полом и подтипом надетого оружия —
		//   ровно теми же двумя полями, по которым ветвится эталонный загрузчик
		//   (`pChar->gender`, `pChar->equippedWeaponType`).
		const UAsdaSkillFxSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
		const AAsdaPlayerCharacter* Ch = GetLocalChar();
		if (Fx)
		{
			const int32 Sex  = Ch ? Ch->Sex : 1;
			const int32 Wsub = Ch ? Ch->EquippedWeaponSubType : -1;
			if (const FAsdaSkillClipRow* R = Fx->ResolveRow(SkillCode, Sex, Wsub))
			{
				Out.ClipName  = R->Clip;
				Out.Combo0Ms  = R->Combo0Ms;
				// ★★★ П9 (2026-08-11): «ПОЛНАЯ ДЛИТЕЛЬНОСТЬ» ОБЯЗАНА ПОКРЫВАТЬ ВСЕ ФАЗЫ.
				//   Эталонный признак «доиграл» — `Game_IsPausedOrMenu@0x99ED60` = `obj[13] >= obj[11]`,
				//   где `obj+0x2C` — полная длительность ЗАГРУЖЕННОГО `.JBB` (`game_main.c:6751`).
				//   Порт брал `PhaseDurMs` = `combo time.bin +48[0]` — конец ФАЗЫ 0, и у многофазных
				//   клипов полоса гасла ДО последнего удара. Пострадавшие (skill_clip_table.tsv):
				//   508 drgnfly_01 (COMBO2 933 > 667), 707 runfire_01 (1500 > 600),
				//   907/594 bigmeto_01 (3200 > 2000) — оба пола.
				//   Пока полной длительности `.JBB` в таблице нет, берём НИЖНЮЮ ДОКАЗАННУЮ границу —
				//   максимум из конца фазы 0 и кадров всех трёх ударов: раньше последнего удара
				//   клип закончиться не может по определению.
				Out.FullMs    = FMath::Max(FMath::Max(R->PhaseDurMs, R->Combo0Ms),
				                           FMath::Max(R->Combo1Ms, R->Combo2Ms));
				// ★ Ноль здесь — ЗНАЧАЩИЙ (174 строки из 1664 имеют COMBO0 == 0): строка ЕСТЬ,
				//   а максимум нулевой. Эталон в этом случае честно ставит max = 1 мс.
				Out.bResolved = true;
				return Out;
			}
		}
		return Out;
	}

	// ★ Аудит №3 (A3/Дд2): строка берётся из ЕДИНОГО индекса `combo_time`, своей карты больше нет.
	{
		const FAsdaSkillCastTiming T = TimingFromComboRow(*Clip);
		if (!T.ClipName.IsEmpty()) { return T; }
	}
	// Женские/мужские записи лежат парами (`ch4_m_*` / `ch4_f_*`) и в проверенных случаях совпадают
	//   по числам — если точного имени нет, пробуем парное.
	FString Alt = *Clip;
	if (Alt.Contains(TEXT("_m_"))) { Alt = Alt.Replace(TEXT("_m_"), TEXT("_f_")); }
	else if (Alt.Contains(TEXT("_f_"))) { Alt = Alt.Replace(TEXT("_f_"), TEXT("_m_")); }
	{
		const FAsdaSkillCastTiming T = TimingFromComboRow(Alt);
		if (!T.ClipName.IsEmpty()) { return T; }
	}

	UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] %d: клип «%s» в combo time.bin не найден"), SkillCode, **Clip);
	return Out;
}

// ══════════════════════════════ ЭМУЛЯЦИЯ СЕРВЕРА ═══════════════════════════════════════════════

void UAsdaSkillUseSubsystem::SimulateCastResult5257(int32 Result, int32 SkillCode)
{
	FPendingNetEvent E; E.Opcode = 5257; E.Result = Result; E.UserIdx = SelfUserIdx(); E.SkillCode = SkillCode;
	NetQueue.Add(E);
}

void UAsdaSkillUseSubsystem::SimulateCastIdle5270(int32 SkillCode)
{
	FPendingNetEvent E; E.Opcode = 5270; E.Result = 1; E.UserIdx = SelfUserIdx(); E.SkillCode = SkillCode;
	NetQueue.Add(E);
}

void UAsdaSkillUseSubsystem::SimulateCastEnd5271(int32 Result, int32 SkillCode)
{
	FPendingNetEvent E; E.Opcode = 5271; E.Result = Result; E.UserIdx = SelfUserIdx(); E.SkillCode = SkillCode;
	E.ArrivalSeq = AsdaNextCdArrivalSeq(GetGameInstance());   // ★ ревизия 1.1: эмуляция — тем же счётчиком прихода
	NetQueue.Add(E);
}

// ══════════════════════════════ ДАМПЫ ══════════════════════════════════════════════════════════

FString UAsdaSkillUseSubsystem::DumpState() const
{
	FString S;
	S += TEXT("── БОЕВОЙ ДИСПЕТЧЕР НАВЫКОВ ──\n");
	S += FString::Printf(TEXT("  активный навык: %d   состояние действия: %d   отложенное действие: %d (навык %d)\n"),
		ActiveSkill, CharState, (int32)PendingAction, PendingSkill);
	S += FString::Printf(TEXT("  соулмейт: %s (soulIdx %d)   свой userIdx: %d\n"),
		HasSoulmate() ? TEXT("есть") : TEXT("нет"), SelfSoulIdx, SelfUserIdx());
	S += FString::Printf(TEXT("  сеть: →5256 %d, →5231 %d, ←5257 %d, ←5270 %d, ←5271 %d, отказов %d (последний код %d у навыка %d)\n"),
		Sent5256, Sent5231, Recv5257, Recv5270, Recv5271, Refused, LastRefuseCode, LastRefuseSkill);
	S += FString::Printf(TEXT("  предохранитель защёлок: срабатываний %d, отсчёт состояния %d с %.1f с\n"),   // ★ баг 7
		LatchGuardFired, LatchState, LatchSinceS > 0.0 ? FPlatformTime::Seconds() - LatchSinceS : 0.0);
	S += FString::Printf(TEXT("  combo time.bin: клипов %d   ручных привязок навык→клип: %d\n"),
		AAsdaPlayerCharacter::ComboTimeIndex(this).Num(), SkillClipOverride.Num());   // ★ Аудит №3 (A3/Д2)
	S += TEXT("  ★ таблица «код навыка → хардкод-загрузчик → имя клипа» в срезе НЕ снята (дыра №1 отчёта):\n");
	S += TEXT("    без неё максимум полосы (COMBO0) неизвестен. Привязать вручную: asda.Skill.SetClip <id> <клип>\n");
	return S;
}

FString UAsdaSkillUseSubsystem::DumpSkill(int32 SkillCode) const
{
	const FAsdaSkillDef* D = GetDef(SkillCode);
	if (!D) { return FString::Printf(TEXT("Навык %d: записи нет"), SkillCode); }
	const UAsdaSkillWindowSubsystem* SW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	const FAsdaSkillCastTiming T = ResolveTiming(SkillCode);
	int32 WType = -1;
	const bool bWeapon = IsWeaponAllowed(*D, WType);
	FString S;
	S += FString::Printf(TEXT("Навык %d «%s»\n"), SkillCode, *D->Name);
	S += FString::Printf(TEXT("  Kind(+0x2A)=%d → %s | SubType(+0x2C)=%d | TargetSlotType(+0x34)=%d | ResourceType(+0x36)=%d\n"),
		D->Kind, D->Kind == 2 ? TEXT("боевой (combatHandleSkillUse)") : D->Kind == 1 ? TEXT("по цели (skillUseOnTargetDispatch)") : TEXT("не применяется"),
		D->SubType, D->TargetSlotType, D->ResourceType);
	S += FString::Printf(TEXT("  ВИД НАВЫКА (+0x34) = %d «%s»; CastType(+0x33)=%d (%s)\n"),
		D->TargetSlotType, TargetSlotTypeName(D->TargetSlotType), D->CastType,
		D->CastType == 0 ? TEXT("само-каст: цель НЕ читается вовсе")
		: D->CastType == 2 ? TEXT("цель — только ПЕРСОНАЖ")
		: D->CastType == 3 ? TEXT("цель — моб ИЛИ персонаж")
		: TEXT("этим путём эталон не применяет"));
	S += FString::Printf(TEXT("  уровень: %d | ОМ: %d (текущая %d) | кулдаун: %d мс | дальность: %d\n"),
		SW ? SW->GetLearnedLevel(SkillCode) : 0, GetMpCost(SkillCode, *D), GetCurrentMp(), D->CooldownMs, D->RangeMeters);
	S += FString::Printf(TEXT("  маска оружия: 0x%llX, надето тип %d → %s\n"),
		(long long)D->WeaponMask, WType, bWeapon ? TEXT("подходит") : TEXT("НЕ подходит (нотис 1020)"));
	S += FString::Printf(TEXT("  полоса применения: клип «%s», COMBO0 = %.0f мс, полная длит. %.0f мс%s\n"),
		*T.ClipName, T.Combo0Ms, T.FullMs, T.bResolved ? TEXT("") : TEXT("  ← источника нет (дыра №1)"));
	// ★★★ ПЛОЩАДНОЙ ЛИ НАВЫК — единственный признак это `resourceType` (+0x36). Поле `AoeRadius`
	//   (+0x37) задаёт ТОЛЬКО размер бокса и на одиночных путях не читается: порога вида
	//   «радиус > N ⇒ площадь» в эталоне нет ни одного (проверено по всем 20 чтениям поля).
	{
		const bool bEff132 = HasAreaEffect132();
		const bool bAreaRes = (D->ResourceType == 2 || D->ResourceType == 3);
		const bool bArea = (D->SubType != 5) && (bAreaRes || (bEff132 && D->Kind == 2));
		const int32 Rad = AreaRadiusTiles(SkillCode, *D);
		S += FString::Printf(TEXT("  ПЛОЩАДЬ: %s — resourceType=%d%s; AoeRadius(+0x37)=%d тайл(ов)%s\n"),
			bArea ? TEXT("ДА") : TEXT("нет"), D->ResourceType,
			bEff132 ? TEXT(" + висит эффект 132 (переключатель площади)") : TEXT(""),
			D->AoeRadius,
			bArea ? *FString::Printf(TEXT(" ⇒ бокс %d×%d тайлов (±%d) вокруг %s"),
			            2 * Rad + 1, 2 * Rad + 1, Rad,
			            (D->CastType == 0) ? TEXT("СЕБЯ") : TEXT("ЦЕЛИ"))
			      : TEXT(" (на одиночном пути НЕ читается)"));
	}
	// ★★★ РЕАКЦИЯ ЦЕЛИ НА УДАР — `.sif +0x8B`. Печатаем отдельной строкой, потому что именно её
	//   раньше принимали за «наземный площадной навык»: `+0x8B == 0` — ЕДИНСТВЕННАЯ ветка, пишущая
	//   наземные тайлы `rec[+15]/[+17]`, но эти тайлы — ТОЧКА ПРИЗЕМЛЕНИЯ ОТБРОСА, а не зона урона
	//   (`charApplySkillImpact` skill_buff.c:4654 и `skillSetupTargetPos@0xA29C00` skill_buff.c:4779:
	//    во всех case результат применяется К ЦЕЛИ — move-target + состояние 14).
	S += FString::Printf(TEXT("  ОТБРОС ЦЕЛИ (+0x8B)=%d — %s\n"), D->BehaviorType,
		D->BehaviorType == 0 ? TEXT("сильный: цель летит на 450 ед. по лучу «кастер→цель», скорость 200; ")
		                       TEXT("ЭТА точка и уходит в наземные тайлы rec[+15]/[+17] (не зона поражения)")
		: (D->BehaviorType == 1 || D->BehaviorType == 2) ? TEXT("слабый: скорость 100, состояние 14, пика земли нет")
		: D->BehaviorType == 3 ? TEXT("без сдвига: взводится флаг цели +18800")
		: D->BehaviorType == 4 ? TEXT("нет: цель не реагирует, эффект удара тоже гасится (behaviorType != 4)")
		: TEXT("только разворот к кастеру"));
	return S;
}

// ══════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════

static UAsdaSkillUseSubsystem* UseOf(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillUseDump(
	TEXT("asda.Skill.Use.State"),
	TEXT("Навыки: состояние боевого диспетчера (сеть, активный навык, тайминги)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaSkillUseSubsystem* S = UseOf(W)) { UE_LOG(LogAsdaSkillUse, Display, TEXT("%s"), *S->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ДУБЛЬ КОНСОЛЬНОГО КЛЮЧА `asda.Skill.Info` СНЯТ ПЕРЕИМЕНОВАНИЕМ (аудит 2026-09-02).
//   Было: `asda.Skill.Info`. Ровно то же имя занято в `AsdaSkillWindowSubsystem.cpp` под ДРУГУЮ
//   диагностику — сырую строку таблицы `skill_info.sif` (иконка, сетка, цены, описание).
//   `FConsoleManager::AddConsoleObject` для КОМАНД делает `ConsoleObjects.Add(Name, Cmd);
//   ExistingCmd->Release();` — побеждает зарегистрированная ПОСЛЕДНЕЙ; по порядку .obj в rsp
//   линкера `AsdaSkillUseSubsystem` идёт раньше `AsdaSkillWindowSubsystem`, значит ЭТА команда
//   уничтожалась при старте и не отрабатывала ни разу.
//   Стало: `asda.Skill.Use.Info` — рядом с `asda.Skill.Use` и `asda.Skill.Use.State` того же
//   боевого диспетчера. Обе диагностики теперь доступны: `asda.Skill.Info` — что лежит в таблице,
//   `asda.Skill.Use.Info` — почему навык применится или нет ЗДЕСЬ И СЕЙЧАС (гейты, тайминги, MP).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillUseInfo(
	TEXT("asda.Skill.Use.Info"),
	TEXT("Навыки: разбор записи навыка и ВСЕХ гейтов применения (вид/каст/оружие/уровень/ОМ/кулдаун/")
	TEXT("площадь/отброс/полоса применения) — asda.Skill.Use.Info <skillId>. ")
	TEXT("Сырую строку таблицы skill_info.sif показывает asda.Skill.Info"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSkillUseSubsystem* S = UseOf(W);
		if (!S) { return; }
		UE_LOG(LogAsdaSkillUse, Display, TEXT("%s"), *S->DumpSkill(A.Num() > 0 ? FCString::Atoi(*A[0]) : 501));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillUseGo(
	TEXT("asda.Skill.Use"),
	TEXT("Навыки: применить (тот же путь, что клик по слоту панели) — asda.Skill.Use <skillId>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSkillUseSubsystem* S = UseOf(W);
		if (!S) { return; }
		const int32 Id = A.Num() > 0 ? FCString::Atoi(*A[0]) : 501;
		const bool bOk = S->DispatchByKind(Id, -1);
		UE_LOG(LogAsdaSkillUse, Display, TEXT("asda.Skill.Use %d → %s"), Id,
			bOk ? TEXT("гейты пройдены, действие 102 поставлено") : TEXT("отказ (см. лог/чат)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillUseSoulmate(
	TEXT("asda.Skill.Soulmate"),
	TEXT("Навыки: применить навык соулмейта (5231) — asda.Skill.Soulmate <id 30..44>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSkillUseSubsystem* S = UseOf(W);
		if (!S) { return; }
		S->CastSoulmateSkill(A.Num() > 0 ? FCString::Atoi(*A[0]) : 30);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillSetClip(
	TEXT("asda.Skill.SetClip"),
	TEXT("Навыки: привязать навык к клипу combo time.bin (максимум полосы) — asda.Skill.SetClip <skillId> <имя клипа>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSkillUseSubsystem* S = UseOf(W);
		if (!S || A.Num() < 1) { return; }
		const int32 Id = FCString::Atoi(*A[0]);
		S->SetSkillClip(Id, A.Num() > 1 ? A[1] : FString());
		UE_LOG(LogAsdaSkillUse, Display, TEXT("%s"), *S->DumpSkill(Id));
	}));
#endif // ASDA_DEV_CONSOLE

// ── Эмуляция серверных ответов (проверка без сервера) ──
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillSim5257(
	TEXT("asda.Skill.Sim.5257"),
	TEXT("Навыки: эмулировать ответ 5257 — asda.Skill.Sim.5257 <skillId> [код результата, 1 = успех]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: вброс 5257: код≠1 сбрасывает ActiveSkill и сторож отправки (снятие замка).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Skill.Sim.5257"))) { return; }
		UAsdaSkillUseSubsystem* S = UseOf(W);
		if (!S) { return; }
		S->SimulateCastResult5257(A.Num() > 1 ? FCString::Atoi(*A[1]) : 1, A.Num() > 0 ? FCString::Atoi(*A[0]) : 501);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillSim5270(
	TEXT("asda.Skill.Sim.5270"),
	TEXT("Навыки: эмулировать «каст начался» 5270 — asda.Skill.Sim.5270 <skillId>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: вброс 5270 в очередь навыков — поддельное состояние каста.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Skill.Sim.5270"))) { return; }
		UAsdaSkillUseSubsystem* S = UseOf(W);
		if (!S) { return; }
		S->SimulateCastIdle5270(A.Num() > 0 ? FCString::Atoi(*A[0]) : 501);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkillSim5271(
	TEXT("asda.Skill.Sim.5271"),
	TEXT("Навыки: эмулировать «каст кончился» 5271 — asda.Skill.Sim.5271 <skillId> [код результата, 1 = успех]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: вброс 5271 — поддельный откат навыка.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Skill.Sim.5271"))) { return; }
		UAsdaSkillUseSubsystem* S = UseOf(W);
		if (!S) { return; }
		S->SimulateCastEnd5271(A.Num() > 1 ? FCString::Atoi(*A[1]) : 1, A.Num() > 0 ? FCString::Atoi(*A[0]) : 501);
	}));
#endif // ASDA_DEV_CONSOLE
