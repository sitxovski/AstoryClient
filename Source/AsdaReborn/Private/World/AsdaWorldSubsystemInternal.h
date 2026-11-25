// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystemInternal.h — ★ 23.10.2026 (разнос AsdaWorldSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaWorldSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала LogAsdaWorld (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaWorldSubsystem.cpp), блок журнала боя (#if ASDA_COMBAT_TRACE) и чистые static-константы/хелперы без состояния —
//   текст байт в байт из AsdaWorldSubsystem.cpp, у каждой части своя копия. Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "AsdaDevConsole.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ R1: AsdaLedgerIsSelf ниже зовёт метод сети; раньше класс приходил через AsdaWorldSubsystem.h
#include "World/AsdaCombatTrace.h"

// Категория журнала мира: объявление; определение — AsdaWorldSubsystem.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaWorld, Log, All);
// Ключи и флаги, которые читают несколько частей: один экземпляр в AsdaWorldSubsystem.cpp (там же разбор), здесь extern.
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaHitFrameAllChannels;   // asda.Combat.HitFrameAllChannels
extern TAutoConsoleVariable<int32> CVarAsdaObserverSync;          // asda.Combat.ObserverSync
#else
extern TAsdaConstCVar<int32> CVarAsdaHitFrameAllChannels;         // Shipping: константа на месте ключа
extern TAsdaConstCVar<int32> CVarAsdaObserverSync;                // Shipping: константа на месте ключа
#endif
extern bool GAsdaLegacyMobDeath;   // asda.Skill.LegacyDeath (переменная ключа-ссылки; определение без static и до разноса)
extern bool GAsdaHoldMobDeath;     // ключ-ссылка придерживающего гейта смерти моба (определение без static и до разноса)


// ═══ ★ 18.10.2026 ЖУРНАЛ БОЯ — помощники этого файла (только редактор/Development; в Shipping их нет) ═══
#if ASDA_COMBAT_TRACE
namespace AsdaCtWorld
{
	// Метка моба для строки журнала: «rid=12 'Волк'» (актора нет — имя «?»).
	[[maybe_unused]] static FString Mob(int32 Rid, const AAsdaMobActor* M)
	{
		return FString::Printf(TEXT("rid=%d '%s'"), Rid, M ? *M->MobName : TEXT("?"));
	}
	// Разобранное значение очереди результатов удара: «45», «45 КРИТ», «MISS(-2)», «DODGE(-1)», «IMMUNE(-4)».
	//   Таблица кодов — `ShowHitResult`/`ShowPlayerHit`: −1 уклонение, −2/0 промах, −3…−5 неуязвимость, −6/−7 промах, бит 31 — крит.
	[[maybe_unused]] static FString Hit(int32 V)
	{
		if (V == -1) { return TEXT("DODGE(-1)"); }
		if (V == -2 || V == 0 || V == -6 || V == -7) { return FString::Printf(TEXT("MISS(%d)"), V); }
		if (V >= -5 && V <= -3) { return FString::Printf(TEXT("IMMUNE(%d)"), V); }
		const bool bCrit = ((uint32)V & 0x80000000u) != 0;   // бит 31 — крит
		return FString::Printf(TEXT("%d%s"), V & 0x7FFFFFFF, bCrit ? TEXT(" КРИТ") : TEXT(""));
	}
	// Очередь значений в разобранном виде: «[45, MISS(-2), 60 КРИТ]».
	[[maybe_unused]] static FString Queue(const TArray<int32>& Q)
	{
		FString S;   // собираемый текст
		for (int32 i = 0; i < Q.Num(); ++i) { if (i > 0) { S += TEXT(", "); } S += Hit(Q[i]); }
		return FString::Printf(TEXT("[%s]"), *S);
	}
	// Задержки до кадров удара: «[+0.412, +0.833]».
	[[maybe_unused]] static FString Delays(const TArray<float>& D)
	{
		FString S;   // собираемый текст
		for (int32 i = 0; i < D.Num(); ++i) { if (i > 0) { S += TEXT(", "); } S += FString::Printf(TEXT("+%.3f"), D[i]); }
		return FString::Printf(TEXT("[%s]"), *S);
	}
	// ★ Волна 2 (18.10.2026): фильтр шума общих каналов — «наш» ли удар/событие для ASDA_CTRACE_IF.
	//   Моб rid в нашем бою (цель цикла/задет нами) ИЛИ игрок uidx наш (я/цель PvP/участник). Свой атакующий — всегда наш
	//   (IsUserRelevant(свой userIdx)); вызывающие с известным `bOurs` добавляют его явно.
	[[maybe_unused]] static bool Ours(int32 Rid, int32 Uidx)
	{
		return AsdaCombatTrace::IsEngaged(Rid) || AsdaCombatTrace::IsUserRelevant(Uidx);
	}
	// PvP и прочие события пары игроков: наш, если наш хотя бы один из двух.
	[[maybe_unused]] static bool Users(int32 UidxA, int32 UidxB)
	{
		return AsdaCombatTrace::IsUserRelevant(UidxA) || AsdaCombatTrace::IsUserRelevant(UidxB);
	}
	// Навык: кастер наш ИЛИ цель наша (TargetKind 0 — моб rid, иначе игрок uidx).
	[[maybe_unused]] static bool SkillOurs(int32 CasterUidx, int32 TargetKind, int32 TargetIdx)
	{
		return AsdaCombatTrace::IsUserRelevant(CasterUidx)
			|| (TargetKind == 0 ? AsdaCombatTrace::IsEngaged(TargetIdx) : AsdaCombatTrace::IsUserRelevant(TargetIdx));
	}
}
// ASDA_CTRACE_MOB / ASDA_CTRACE_UID / ASDA_CTRACE_IF — общие, в World/AsdaCombatTrace.h.
// Отметить окончательную смерть моба — цели ТЕКУЩЕГО цикла. Цикл здесь НЕ закрывается: его закрывает контроллер при
//   выходе из боевой стойки/снятии цели, чтобы труп, опыт и добыча попали в тот же цикл. Why — причина (TEXT).
#define ASDA_CTRACE_END_IF_TARGET(Rid, Why) \
	do { if ((Rid) >= 0 && (Rid) == AsdaCombatTrace::CurrentCycleTarget()) { ASDA_CTRACE(EAsdaCT::Death, TEXT("★ цель цикла rid=%d умерла окончательно (%s) — цикл открыт до выхода из стойки (труп/награда)"), (int32)(Rid), Why); } } while (0)
#else
#define ASDA_CTRACE_END_IF_TARGET(Rid, Why) do { } while (0)
#endif
// ★ Ф1: «атакующий — свой игрок» для ленты попаданий (единая точка E2 — `UAsdaNetSubsystem::IsSelfUserIdx`).
static bool AsdaLedgerIsSelf(const UGameInstance* GI, int32 UserIdx)
{
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->IsSelfUserIdx(UserIdx);
}
// ★ Объектный путь `DT_RideEffect` — одно объявление на файл (15.09.2026, dt_gc_hold): читает `LookupRideFx`,
//   удерживает на сессию `HoldSpawnTables(Mount)`.
static const TCHAR* const kRideEffectDT = TEXT("/Game/AsdaAssets/Data/DT_RideEffect.DT_RideEffect");
// ★ N12 (дедуп №2, В3, 12.10.2026): объектный путь таблицы спец-предметов — читает `LookupMount` (индекс транспорта),
//   удерживает на сессию `HoldSpawnTables(MountIndex)`. Путь прежний (был литералом в `LookupMount`).
static const TCHAR* const kSpecialItemlistDT = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_special_itemlist.DT_special_itemlist");

// ★★★ [ОРИГИНАЛ] Показ/скрытие персонажа по GM-невидимости. Единая точка для всех трёх каналов
//   (4025 состояние 300/301, 4103 состояние при входе в зону, 6006 — про себя самого).
//   В оригинале это ОДИН бит флагов персонажа `char + 0x40C` (`field_40C`), который рендер
//   проверяет через виртуальный геттер `get_o103@0x979FF0`; ни коллизия, ни физика, ни сетевой
//   реестр им не трогаются. Ближайший аналог в UE — `SetActorHiddenInGame`: он гасит ВСЕ
//   примитивы актора, включая над-головную плашку, и не трогает ни коллизию, ни движение.
//   ⚠ Реестр `NetPlayers` НЕ чистим: скрытый персонаж по-прежнему существует и продолжает
//   присылать движение — иначе при показе он «телепортировался» бы из старой точки.
// ★★★ [ПОРТ] ПОДВЕСНЫЕ ЭФФЕКТЫ ГАСЯТСЯ ВМЕСТЕ С ПЕРСОНАЖЕМ.
//   [ОРИГИНАЛ] у эталона это бесплатно: крылья, свечение оружия и ауры — не отдельные объекты, а
//   узлы того же `CCharacter`, и один бит `char+0x40C` убирает их вместе с телом.
//   [ПОРТ] у нас каждый такой эффект — САМОСТОЯТЕЛЬНЫЙ актор (`AAsdaEffectActor`), лишь
//   приаттаченный к персонажу, а `SetActorHiddenInGame` на родителя чужие акторы не трогает.
//   Без этой функции скрытый оператор оставлял бы в воздухе висящие крылья и свечение меча —
//   то есть выдавал бы себя ровно тем, что должен был спрятать.
//   Рекурсивно: часть эффектов висит вторым ярусом (эффект на эффекте, см. AsdaSkillFxSubsystem).
// ★ R5 (12.11.2026): тело `AsdaSetAttachedFxHidden` — в узком заголовке `World/AsdaAttachedFxHide.h` (одна копия на мир и
//   невидимость; раньше невидимость держала свою дословную копию `AsdaStealthSetAttachedFxHidden`).
#include "World/AsdaAttachedFxHide.h"

// ── ★ ТРАНСПОРТ (5453/5454) — mount_system_re.md ──

// Данные транспорта по itemCode: таблица спец-предметов, записи с SubType==15.
//   ModelEntry = меш (itm1_n_96_vehicle_NNNN), EffectEntry = набор СИДЯЧИХ анимаций (Ch1_n_sit_NNNN),
//   HeightOffset = подъём плашки имени (на позицию персонажа НЕ влияет — эталон).
// Шлейф/звуки езды по коду предмета. ★ ИСТОЧНИК = АССЕТ `/Game/AsdaAssets/Data/DT_RideEffect`
//   (импортёр Tools/asda_build_ride_effect_dt.py из Ride_effect_table.bin: header 20 + 488×260 —
//   Code i32@0, эффект char[64]@4, звук движения char[64]@68, звуки покоя char[64]@132 и char[64]@196).
//   Потребители полей: @+4/@+68 — Item_UpdateSpecialEffectAndSound@0xF272E0 (state 1),
//   @+132/@+196 — charItemSoundChargeTick@0xF27740 (state 0).
// ★ ПОЧЕМУ НЕ ФАЙЛ: .bin в Content/ кукером в пак НЕ пакуется — в шипнутой сборке чтение вернуло бы
//   пустоту (в редакторе баг незаметен). Рантайм читает данные ТОЛЬКО из ассетов.
// Ключ строки == Code (проверено импортёром: во всех 488 записях `_row == Code`) ⇒ прямой FindRow,
//   линейный перебор не нужен.
static bool LookupRideFx(int32 ItemCode, FString& OutEffect, FString& OutSound,
	FString& OutSound2, FString& OutSound3)
{
	OutEffect.Reset(); OutSound.Reset(); OutSound2.Reset(); OutSound3.Reset();
	// ★ N12 (дедуп №2, В3, 12.10.2026): своя пара `static TWeakObjectPtr` + `LoadObject` → общая точка `AsdaDt::RowByInt`
	//   (та же слабая ленивая загрузка с теми же флагами, тот же `FindRow` без предупреждения; удержание — `HoldSpawnTables(Mount)`).
	const FAsdaRideEffectRow* R = AsdaDt::RowByInt<FAsdaRideEffectRow>(kRideEffectDT, ItemCode, TEXT("LookupRideFx"));
	if (!R) { return false; }
	// Импортёр уже срезал расширение и привёл к нижнему регистру (ассеты именованы так же).
	OutEffect = R->EffectName;
	OutSound  = R->SoundName;
	OutSound2 = R->SoundName2;
	OutSound3 = R->SoundName3;
	return true;
}

// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] Цвет строки чата «по тебе урон» эталона −33517 = D3DCOLOR 0xFFFF7D13 (`push 0xffff7d13`,
//   combatFloatingDamageText@0xA4D1A0: 0xA4D444 и др.) — оранжевый (FF,7D,13). Порт писал 0xFF7B93 (ошибка перевода числа).
//   Ключ `asda.Chat.HitColorFix` (0 — прежний 0xFF7B93). Определение — AsdaWorldSubsystem_Damage.cpp.
FColor AsdaChatColHitOnMe();
