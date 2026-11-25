// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ЖУРНАЛ БОЯ — полный цикл действий клиента в бою одной лентой (18.10.2026). [ПОРТ]
//
// ЗАЧЕМ. Разрозненные строки `[бой]`/`[лента]`/`[HP]` в LogAsdaWorld/LogAsdaPC/LogAsdaNet не давали увидеть ЦИКЛ целиком:
//   клик по мобу → бег → дальность → заявка → ответ сервера → замах → кадр удара → урон → смена отображаемого HP → смерть.
//   Журнал боя пишет ВСЕ эти шаги одним форматом, с номером цикла и временем от его начала.
//
// ГДЕ ЕСТЬ. Только редактор и Development (`ASDA_COMBAT_TRACE` = !UE_BUILD_SHIPPING). В Shipping макросы — пустые,
//   аргументы не вычисляются, строк форматов и имён ключей в бинаре нет.
//
// КУДА ПИШЕТ. (1) Категория `LogAsdaCombatTrace` основного журнала — вперемешку с остальным, для контекста;
//   (2) отдельный файл `Saved/Logs/AsdaCombat_<дата-время>_<pid>.log` (UTF-8, построчная запись в кэш ОС без fsync —
//   переживает падение процесса и не тормозит кадр; хранятся 10 последних). Отдельный файл на процесс — два клиента/редактор+игра не пишут в один файл.
//
// ФОРМАТ СТРОКИ:
//   [БОЙ] 0123.456 к=000123456 ц#0007 +0.512 ЭТАП      | текст
//         │        │           │      │      │
//         │        │           │      │      └ этап цикла (EAsdaCT), ширина 9
//         │        │           │      └ секунд от начала текущего цикла (у строк вне цикла — «  -   »)
//         │        │           └ номер цикла: новый цикл начинает выбор цели для атаки (BeginCycle)
//         │        └ номер кадра движка (GFrameCounter) — строки одного кадра видны сразу
//         └ секунд с запуска процесса
//
// КЛЮЧИ (дев, в Shipping нет):
//   asda.Combat.Trace       0 — выкл; 1 — все события цикла (умолч.); 2 — ещё и частые (бег по тикам, плавные полосы).
//   asda.Combat.TraceFile   1 — писать отдельный файл (умолч.); 0 — только основной журнал.
//   asda.Combat.TraceFileAsync  1 — файл пишет фоновый поток, игровой кладёт строку в очередь (умолч.; R4, запись 10: при занятом
//                           диске запись в самом вызове останавливала кадр до 1,7 с); 0 — прежняя запись в самом вызове.
//                           Самотест `asda.Combat.SelfTestTraceFile`, счётчики `asda.Combat.TraceFileStats`.
//   asda.Combat.TraceMark <текст> — вставить метку в журнал боя («тут моб дёрнулся»).
//
// КАК ПИСАТЬ (правило для всех мест боя):
//   ASDA_CTRACE(EAsdaCT::Damage, TEXT("rid=%d урон=%d крит=%d HP %d→%d"), Rid, Dmg, bCrit ? 1 : 0, Was, Now);
//   ASDA_CTRACE_V(EAsdaCT::Move, TEXT("бег: до цели %.0f юн"), Dist);   // частые — только при asda.Combat.Trace 2
//   ASDA_CTRACE_MOB(Rid, …) / ASDA_CTRACE_UID(UserIdx, …) / ASDA_CTRACE_IF(bOurs, …) — события ОБЩИХ каналов (по всем
//     мобам/игрокам сектора): уровень 1 только для нашего боя, чужие — уровень 2. Правило: всё, что приходит не только
//     про наш бой (4013/4015/4017/4011/4058/4059, соседи, мобы бьют других, эффекты других), — через эти макросы.
//   Значения, нужные ТОЛЬКО журналу, вычислять внутри аргументов макроса или под `#if ASDA_COMBAT_TRACE`
//   (иначе в Shipping — неиспользуемая переменная). Побочных действий в аргументах — НЕТ (в Shipping не вычисляются).
//   Строки: имя моба — `*M->MobName`; булевы — `bX ? TEXT("да") : TEXT("нет")`.
#pragma once

#include "CoreMinimal.h"

// 1 — журнал боя собран (редактор, Development, Debug, Test); 0 — Shipping.
#ifndef ASDA_COMBAT_TRACE
	#define ASDA_COMBAT_TRACE (!UE_BUILD_SHIPPING)
#endif

// Этап боевого цикла — колонка «ЭТАП» в журнале. Порядок — примерный порядок цикла.
enum class EAsdaCT : uint8
{
	Input,      // ВВОД     — клик мышью/клавиша: по мобу, по земле, по игроку, горячая клавиша навыка, отмена
	Target,     // ЦЕЛЬ     — выбор/смена/снятие цели, атака по цели, подсветка/кольцо
	Move,       // БЕГ      — старт бега к цели, перестройка пути, остановка, прибытие, догон убегающей цели
	Range,      // ДАЛЬН    — проверка дальности атаки: дистанция, предел, вошли/вышли
	Send,       // ОТПР     — отправка боевого пакета (опкод и поля)
	Recv,       // ПРИЁМ    — приём боевого пакета (опкод и разобранные поля)
	Queue,      // ОЧЕРЕДЬ  — очередь результатов ударов: пополнение, съём, сброс, отброс
	Swing,      // ЗАМАХ    — анимация атаки: старт клипа, конец прохода, прерывание, стойка
	HitFrame,   // КАДР     — кадр удара (момент попадания по анимации/прилёт снаряда)
	Damage,     // УРОН     — применённый урон: значение, крит, промах, источник, по кому
	DmgText,    // ЦИФРА    — показанная цифра урона над целью (текст, стиль, цвет)
	HpTarget,   // HP-ЦЕЛИ  — отображаемое HP в панели цели: было → стало, источник
	HpPlate,    // HP-ПЛАШК — HP моба/игрока у модели (плашка/полоса над головой, CurHp актёра)
	HpSelf,     // HP-СВОЙ  — своё HP/MP в HUD: было → стало, канал
	Death,      // СМЕРТЬ   — смерть цели/своя: источник, отложена/применена/отменена
	Corpse,     // ТРУП     — клип смерти, фейд, удаление актёра
	Reward,     // НАГРАДА  — опыт, добыча, деньги за убийство
	Exit,       // ВЫХОД    — выход из боя: клик по земле, смена цели, цель вне досягаемости, смерть цели
	Refuse,     // ОТКАЗ    — отказ сервера/клиента: код, причина, гейт обездвиживания
	MobAttack,  // МОБ-БЬЁТ — моб бьёт игрока: замах, урон, промах
	Skill,      // НАВЫК    — навык: нажатие, проверка, каст, отправка, результат, откат
	Peer,       // СОСЕД    — чужой игрок бьёт моба (наблюдатель)
	Pvp,        // PvP      — удары игрок↔игрок
	Fx,         // ЭФФЕКТ   — визуальные эффекты боя (импакт, крит, эффекты состояний), звук удара
	State,      // СОСТ     — состояние персонажа: боевая стойка, занят, обездвижен, ключи отката
	Note,       // МЕТКА    — ручная метка asda.Combat.TraceMark, служебное
};

namespace AsdaCombatTrace
{
#if ASDA_COMBAT_TRACE
	// Включён ли журнал на уровне Level (1 — события, 2 — частые).
	ASDAREBORN_API bool IsOn(int32 Level = 1);
	// Записать строку: этап + готовый текст. Зовётся макросами; напрямую — только при готовой строке.
	ASDAREBORN_API void Emit(EAsdaCT Stage, const FString& Text);
	// Начать новый цикл по МОБУ (выбор цели под атаку). Возвращает номер цикла. Why — что начало цикл.
	ASDAREBORN_API int32 BeginCycle(int32 TargetRid, const TCHAR* TargetName, const TCHAR* Why);
	// Начать новый цикл по ИГРОКУ (PvP): цель — userIdx (номера userIdx и rid мобов пересекаются — храним вид цели).
	ASDAREBORN_API int32 BeginCyclePlayer(int32 TargetUserIdx, const TCHAR* TargetName, const TCHAR* Why);
	// Закрыть текущий цикл (выход из боя/смена цели). Повторный вызов без открытого цикла — пусто.
	ASDAREBORN_API void EndCycle(const TCHAR* Why);
	// Номер текущего цикла (0 — цикла нет).
	ASDAREBORN_API int32 CurrentCycle();
	// rid моба — цели текущего цикла (-1 — цикла нет или цикл по игроку).
	ASDAREBORN_API int32 CurrentCycleTarget();
	// userIdx игрока — цели текущего PvP-цикла (-1 — цикла нет или цикл по мобу).
	ASDAREBORN_API int32 CurrentCyclePlayer();
	// Свой userIdx (ставит сеть при входе в мир; -1 — неизвестен). Нужен фильтру «мой ли это бой».
	ASDAREBORN_API void SetSelfUserIdx(int32 UserIdx);
	// Отметить МОБА rid, по которому мы ударили/кастовали (вне цикла тоже: навык по стороннему мобу).
	ASDAREBORN_API void NoteEngaged(int32 Rid);
	// В бою ли мы с мобом rid: цель текущего цикла или отмечен NoteEngaged за последние 30 с.
	ASDAREBORN_API bool IsEngaged(int32 Rid);
	// Отметить ИГРОКА userIdx как участника нашего боя (бьёт нашу цель/бьёт нас/мы бьём его) — 30 с.
	ASDAREBORN_API void NoteEngagedUser(int32 UserIdx);
	// Наш ли это игрок для журнала: я, цель PvP-цикла или отмечен NoteEngagedUser за последние 30 с.
	ASDAREBORN_API bool IsUserRelevant(int32 UserIdx);
	// ★ R4, запись 10: полный путь файла журнала боя (пусто — файл ещё не открыт или не открылся).
	ASDAREBORN_API FString FilePath();
	// ★ R4, запись 10: дописать в файл строки, ждущие фоновый поток (нужно перед чтением файла; из игры не зовётся).
	ASDAREBORN_API void FlushFile();
	// ★ R4, запись 10: счётчики фоновой записи — строк записано фоновым потоком, наибольшая очередь, самая долгая запись пачки (с).
	ASDAREBORN_API void FileWriterStats(int64& OutLines, int32& OutMaxQueued, double& OutMaxWriteS);
#endif
}

#if ASDA_COMBAT_TRACE
	// Событие цикла (уровень 1).
	#define ASDA_CTRACE(Stage, Fmt, ...) \
		do { if (AsdaCombatTrace::IsOn(1)) { AsdaCombatTrace::Emit((Stage), FString::Printf(Fmt, ##__VA_ARGS__)); } } while (0)
	// Частое событие (уровень 2: тики бега, шаги плавных полос).
	#define ASDA_CTRACE_V(Stage, Fmt, ...) \
		do { if (AsdaCombatTrace::IsOn(2)) { AsdaCombatTrace::Emit((Stage), FString::Printf(Fmt, ##__VA_ARGS__)); } } while (0)
	// Событие по сущности из ЧАСТОГО/общего канала (пакеты по всему сектору): уровень 1 — если это НАШ бой
	//   (моб — цель цикла или задет нами; игрок — я/цель PvP/участник нашего боя), иначе уровень 2.
	#define ASDA_CTRACE_MOB(Rid, Stage, Fmt, ...) 		do { if (AsdaCombatTrace::IsOn(1) && (AsdaCombatTrace::IsOn(2) || AsdaCombatTrace::IsEngaged(Rid))) { AsdaCombatTrace::Emit((Stage), FString::Printf(Fmt, ##__VA_ARGS__)); } } while (0)
	#define ASDA_CTRACE_UID(UserIdx, Stage, Fmt, ...) 		do { if (AsdaCombatTrace::IsOn(1) && (AsdaCombatTrace::IsOn(2) || AsdaCombatTrace::IsUserRelevant(UserIdx))) { AsdaCombatTrace::Emit((Stage), FString::Printf(Fmt, ##__VA_ARGS__)); } } while (0)
	// То же с готовым условием (смешанные случаи: «моб бьёт игрока» — наш, если жертва я ИЛИ моб наш).
	//   Условие вычисляется лениво: при выключенном журнале и при уровне 2 (пишется всё) — не вычисляется.
	#define ASDA_CTRACE_IF(bOurs, Stage, Fmt, ...) 		do { if (AsdaCombatTrace::IsOn(1) && (AsdaCombatTrace::IsOn(2) || (bOurs))) { AsdaCombatTrace::Emit((Stage), FString::Printf(Fmt, ##__VA_ARGS__)); } } while (0)
	// Начало/конец цикла. Использовать только отдельной инструкцией.
	#define ASDA_CTRACE_BEGIN(Rid, Name, Why) AsdaCombatTrace::BeginCycle((Rid), (Name), (Why))
	#define ASDA_CTRACE_BEGIN_PVP(UserIdx, Name, Why) AsdaCombatTrace::BeginCyclePlayer((UserIdx), (Name), (Why))
	#define ASDA_CTRACE_END(Why) AsdaCombatTrace::EndCycle(Why)
#else
	#define ASDA_CTRACE(Stage, Fmt, ...) do { } while (0)
	#define ASDA_CTRACE_V(Stage, Fmt, ...) do { } while (0)
	#define ASDA_CTRACE_MOB(Rid, Stage, Fmt, ...) do { } while (0)
	#define ASDA_CTRACE_UID(UserIdx, Stage, Fmt, ...) do { } while (0)
	#define ASDA_CTRACE_IF(bOurs, Stage, Fmt, ...) do { } while (0)
	#define ASDA_CTRACE_BEGIN(Rid, Name, Why) do { } while (0)
	#define ASDA_CTRACE_BEGIN_PVP(UserIdx, Name, Why) do { } while (0)
	#define ASDA_CTRACE_END(Why) do { } while (0)
#endif
