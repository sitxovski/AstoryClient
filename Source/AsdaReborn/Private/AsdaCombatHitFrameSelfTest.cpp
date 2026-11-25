// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «HP И СМЕРТЬ ЦЕЛИ — НЕ РАНЬШЕ ПОПАДАНИЯ НАШЕГО ЗАМАХА» (17.10.2026, `Reports/combat_hitframe_2026-10-17/REPORT.md`).
//   [ПОРТ]
//
// ★ ЗАЧЕМ. Жалоба владельца 17.10 (дальнобойное, моб на один выстрел): «добежал, пока взводится — полоса HP уходит вниз и моб
//   умирает, а персонаж потом стреляет в труп». С 15.10 первый выстрел дальнобойной серии идёт после полного натяжения
//   (`asda.Combat.EvenSwingTempo`), а серверная смерть приходит по вшитым часам NpcServer (моб в бою — 1000 мс после удара,
//   живое HP — сразу). Окно «наш замах в полёте» (`UAsdaWorldSubsystem::ArmOurImpactWindow`) теперь держит HP и смерть ВСЕХ
//   каналов — 4015, 4017, 4013/4019 — до попадания. Самотест проверяет штатным путём (свой персонаж, бой кликом, исходящие —
//   в перехватчик, входящие — `DebugFeedPacket` = тот же `HandlePacket`, что у сокета):
//   • дальнобойное (баллиста, арбалет, лук): смертельный ответ 4029 + 4015 HP=0 через 0.1 с + 4017 flags=0 через 1.0 с —
//     полоса и труп НЕ раньше прилёта снаряда и не позже прилёта + 0.6 с;
//   • ответ не пришёл (только 4015 HP=0 и 4017 flags=0): смерть не раньше ФАКТИЧЕСКОГО кадра удара и окно закрывается само;
//   • не смертельный удар с HP из 4017/4015 до выстрела: полоса уходит на прилёте, итог = серверное HP;
//   • ответ «−2» (сервер урон не показал) + 4015 HP=0: смерть на прилёте первого снаряда;
//   • ближний бой (меч): смерть и полоса не раньше ФАКТИЧЕСКОГО показа цифры удара и сразу за ним;
//   • обратные проверки: чужой моб (нашего замаха нет) — HP и смерть сразу; подъём HP (реген) без замаха — сразу.
//   ★ Ревизия 17.10 (находки 1–10 ревизии правки): опора — наблюдаемое, а не формула кода (кадр удара — позиция клипа замаха
//   прошла кадр удара из combo_time; цифра — момент `ShowHitResult`; попадание — исчезновение снаряда). Добавлены сценарии:
//   снимок HP без ударов (4029 с пустой очередью — полоса и цифра на кадре удара), 4013/4019 с HP=0 во время окна, смена цели
//   при отложенной смерти (без ответа — окно закрывается; со смертельным ответом — смерть на назначенном попадании), серия
//   4028 с придержанным HP (абсолютный потолок 4.5 с), отказ 4029, `asda.Combat.DeathOnHitFrame 0`, 4015 раньше 4029 при
//   живом ответе, реген/респаун (4017 flags=4, 4013 HP>0) при отложенной смерти.
//   ★ 18.10.2026 (ревизия Ф4, ВАЖНО 3): «цифры прошлого замаха не переносятся на новый взвод» — меч, ответ с 4 ударами, посреди
//   ожидания цифр замах обрывается (выход из цикла — смена цели/убийство; снятие цикла навыком) и сразу «взводится» новый: таймеры
//   цифр прошлого замаха обязаны остаться на своих моментах, цифры — выйти не позже них. Контроль — `asda.Combat.OwnHitSwingGuard 0`
//   (таймеры уезжают на кадры нового замаха — дефект виден).
//   ★ 18.10.2026 (плавный бой, `Reports/combat_trace_2026-10-18/bugs_death_and_ledger.md` §3): меч, двойной старт серии
//   (4029 [−2 −1 9 10] и [9 9 9 9] подряд) + смертельный ответ без ударов — одно значение на кадр удара, показано ровно применённое
//   сервером, смерть на ударе, доводящем полосу до 0, замах снят сразу; с 4015 kind 2 со старым HP между ответами — полоса не выше
//   35, MaxHp прежний; HP > MaxHp (реген выше максимума типа) — максимум не растягивается. Контроли — ключи
//   `asda.Combat.DeathAtLastPendingHit` / `OwnHitsFifo` / `LedgerRaiseBareOnly` / `MobMaxHpFixed` = 0.
//   Контрольная пара (закон pixel-metric-needs-control-pairs): `asda.Combat.HitFrameAllChannels 0` — баллиста, смертельный
//   ответ: труп ложится раньше прилёта (дефект виден, проверка не слепая). Если ключ уже 0 на старте (`--pre`), все бои идут
//   с ключом 0, а проверки — обычные: КОНТРОЛЬНЫЙ ПРОГОН, провалы ожидаемы и показывают дефект.
//
// Консоль: `asda.Combat.SelfTestHitFrame [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-КАДРУДАРА», итог — «… ИТОГ».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 01.11.2026: сброс серверных часов атаки в итоге
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaPlayerController.h"        // боевой тик, клик по цели
#include "World/AsdaPlayerCharacter.h"         // свой персонаж, план замаха
#include "World/AsdaWorldSubsystem.h"          // FindNetMobByRid, окно «замах в полёте», показ цифр
#include "World/AsdaProjectileActor.h"         // снаряд: вылет и прилёт
#include "World/AsdaCoord.h"                   // игра ↔ UE, клетки
#include "Items/AsdaItemTableSubsystem.h"      // оружие каждого типа
#include "Npc/AsdaMobActor.h"                  // моб
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/App.h"                          // R5-fix7: длина кадра игры — в сроках, которые замечает опрос
#include "EngineUtils.h"                       // TActorIterator — снаряды в мире
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHitFrameTest, Log, All);

namespace AsdaHitFrameTestLocal
{
	constexpr int32 kMobType = 1;         // тип моба (Mob list: волк alpen) — как у `asda.Mob.SelfTestLivePos`
	constexpr int32 kSpdDenom = 250;      // знаменатель скорости (бег сервера)
	constexpr int32 kMobHp = 100;         // HP моба из записи 4013 (`AsdaTestPackets::MobSpawn4013`)
	constexpr double kTol = 0.08;         // допуск «не раньше» (с): кадр опроса + кадр снаряда (уничтожается в тике после прилёта)
	constexpr double kLateMax = 0.6;      // «не позже» (с) после попадания: оценка полёта + 0.05 + кадр
	constexpr double kHoldCapS = 4.5;     // потолок удержания окна (с) — `kImpactHoldMaxS` в `AsdaWorldSubsystem.cpp`

	// Вариант ответа сервера в сценарии.
	enum class EVar : uint8
	{
		LethalReply,     // 4029 HP=0, удар [100]; 4015 HP=0 через 0.1 с; 4017 flags=0 через 1.0 с
		NoReply,         // 4029 не приходит; 4015 HP=0 через 0.1 с; 4017 flags=0 через 1.0 с
		NonLethal4017,   // 4029 HP=40, удар [60]; 4017 flags=3 HP=40 через 0.1 с; 4015 HP=40 через 0.15 с
		NoShowThenZero,  // 4029 HP=100, удар [−2]; 4015 HP=0 через 0.1 с; 4017 flags=0 через 1.0 с
		SnapNoHits,      // 4029 HP=70, очередь пуста — снимок HP без ударов (ревизия п.2): полоса и цифра на кадре удара
		Repeat4013Zero,  // 4029 HP=0, удар [100]; повторная 4013 HP=0 через 0.1 с
		Loop4019Zero,    // 4029 HP=0, удар [100]; 4019 HP=0 через 0.1 с
		                 // ★ 17.10.2026 (Ф1): ответ СМЕРТЕЛЬНЫЙ. Лента попаданий держит серверную смерть до СМЕРТЕЛЬНОЙ записи;
		                 //   смерть при известном НЕ смертельном ответе (HP 40) — «смерть без записи», не дольше 0.5 с (проект §3.2).
		SwitchNoReply,   // 4029 нет; 4015 HP=0 через 0.1 с; смена цели на второго моба в 0.3 с — окно закрывается
		SwitchLethal,    // 4029 HP=0, удар [100]; 4015 HP=0 через 0.1 с; смена цели в 0.3 с — смерть на назначенном попадании
		CapSeries,       // 4029 HP=90, удар [10]; 4015 HP=30 через 0.4 с; с 0.2 с окно продлевается каждый кадр (`ArmOurImpactWindowFromSend`
		                 //   — модель 4028, уходящих раньше попадания прежнего): придержанное обязано примениться на потолке 4.5 с
		                 //   ★ Ф1: снимок приходит, когда исход заявки ещё неизвестен (иначе сверка «только вниз» опустила бы непокрытую
		                 //   известным уроном часть сразу — проект §3.3); удержание считается с ответа 4029 (полоса выше снимка).
		Refused,         // 4029 отказ (sub 5); 4015 HP=0 через 0.1 с — окно закрыто отказом, смерть сразу
		NoDeathOnHit,    // как LethalReply, но `asda.Combat.DeathOnHitFrame 0` — смерть из 4015 сразу
		Hp4015First,     // 4015 HP=40 в кадре 4026 ДО ответа; 4029 HP=40, удар [60] через 0.05 с
		Regen4017,       // 4029 нет; 4015 HP=0 через 0.1 с; 4017 flags=4 HP=80 через 0.4 с — отложенная смерть отменена
		Regen4013,       // 4029 нет; 4015 HP=0 через 0.1 с; повторная 4013 HP=80 через 0.4 с — отложенная смерть отменена
		// ★ 17.10.2026 (Ф2, ревизия Ф1 п.4): правило ленты «известен только НЕ смертельный ответ + серверная смерть» — смерть до
		//   ближайшего попадания, но не дольше 0.5 с после серверной смерти (проект §3.2). Прежде его проверяли Repeat4013Zero и
		//   Loop4019Zero; в Ф1 их ответ стал смертельным — правило вернулось отдельными сценариями.
		NonLethal4013Zero,   // 4029 HP=40, удар [60]; повторная 4013 HP=0 через 0.1 с
		NonLethal4019Zero,   // 4029 HP=40, удар [60]; 4019 HP=0 через 0.1 с
	};

	// Сценарий заканчивается смертью цели (труп виден — шаг можно завершать).
	static bool IsKillVar(EVar V)
	{
		return V == EVar::LethalReply || V == EVar::NoReply || V == EVar::NoShowThenZero || V == EVar::Repeat4013Zero
			|| V == EVar::Loop4019Zero || V == EVar::SwitchNoReply || V == EVar::SwitchLethal || V == EVar::Refused
			|| V == EVar::NoDeathOnHit || V == EVar::NonLethal4013Zero || V == EVar::NonLethal4019Zero;
	}

	struct FStep
	{
		FString Name;                  // имя шага (в строках журнала)
		float MinSec = 0.f;            // не завершать шаг раньше (с реального времени)
		float TimeoutSec = 10.f;       // потолок шага (с реального времени) — дальше провал «не завершился»
		TFunction<void()> Start;       // начало шага (один раз)
		TFunction<bool()> Poll;        // опрос каждый тик: true — шаг завершён
		TFunction<void()> Check;       // проверки после завершения (или таймаута)
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0;
		bool bStepStarted = false;
		double StepStartS = 0.0;
		FTimerHandle Tick;
		bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Combat.SelfTestHitFrame"), false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0;
		int32 N26 = 0, N28 = 0;             // перехвачено за сценарий
		FVector2D Home = FVector2D::ZeroVector;
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 SavedWeapon = 0, SavedEquipped = 99;
		bool bWasInGame = false;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TArray<int32> Rids;                 // мобы прогона (убрать в итоге)
		int32 NextRid = 30951;
		bool bControlAll = false;           // ключ HitFrameAllChannels был 0 на старте: все бои с ключом 0 (контрольный прогон)
		// Сценарий.
		int32 Rid = -1, Rid2 = -1;
		int32 Phase = 0;
		double T0 = -1.0;                   // мир-время ухода 4026
		float PlannedHitS = -1.f;           // план кадра удара на момент 4026 (с) — только в журнал и как опора контроля без замаха
		bool bFed005 = false, bFed01 = false, bFed015 = false, bFedSwitch = false, bFed04 = false, bFed10 = false;   // поданы пакеты по расписанию
		bool bFedOther = false, bFedOther2 = false;   // чужому мобу поданы 4015 / 4017
		double TShot = -1.0, TImpact = -1.0, THpDrop = -1.0, TDeath = -1.0;   // наблюдения (мир-время)
		double THitFrame = -1.0;            // ФАКТИЧЕСКИЙ кадр удара: позиция клипа замаха прошла первый кадр удара combo_time
		double TDigit = -1.0;               // ФАКТИЧЕСКИЙ показ первой цифры удара по цели (`ShowHitResult`)
		int32 DigitsAtStart = 0;            // счётчик показанных результатов удара на момент 4026
		int32 Digits = 0;                   // показано результатов удара за сценарий
		double TOtherHp = -1.0, TOtherDeath = -1.0;   // чужой моб: применение HP и смерть
		bool bSawProjectile = false;
		uint64 N26Frame = 0;                // R5-fix7: кадр игры, в котором ушёл последний 4026
		// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: вылет снаряда ловится СОБЫТИЕМ рождения актора. Прежде — только опросом «есть ли
		//   снаряд в мире» раз в кадр: при длинном кадре короткий полёт (баллиста, арбалет вблизи) целиком умещался между опросами,
		//   снаряд не был виден ни разу («снаряд вылетел (-1.00 с) и долетел (-1.00 с)»), и за ним краснели проверки «труп / полоса
		//   не раньше попадания».
		FDelegateHandle ProjSpawnHandle;    // подписка на рождение акторов мира
		double ProjSpawnS = -1.0;           // мир-время рождения первого снаряда сценария (-1 — не рождался)
		double PrevPollS = -1.0;            // мир-время прошлого опроса сценария
		double HpDropLagS = 0.0;            // шаг опроса, на котором замечено падение полосы
		double CapLagS = 0.0;               // шаг опроса, на котором замечено применение придержанного HP
		double DeathLagS = 0.0;             // ★ R5-fix7: шаг опроса, на котором замечена смерть моба
		double StanceLagS = 0.0;            // ★ R5-fix7: длина кадра сверх 1/60 с на опросе, заметившем снятый замах
		double RearmMaxFrameS = 0.0;        // ★ R5-fix7: наибольшая длина кадра сверх 1/60 с, пока ждали цифры прошлого замаха
		int32 FinalHp = -1;
		bool bFinalServerDead = false;      // метка «обречён» на конце сценария
		bool bWindowAtSend = false;         // окно «замах в полёте» открылось по отправке 4026 (до ответа)
		bool bWindowAfterRefuse = true;     // окно открыто сразу после отказа 4029
		bool bWindowAt4 = false;            // окно ещё открыто в 4.0 с (серия 4028 продлевает)
		bool bWindowAtEnd = false;          // окно открыто на конце сценария
		double TCap = -1.0;                 // момент применения придержанного HP=30 (серия 4028)
		int32 NExtend = 0;                  // сколько раз окно продлено отправкой за удержание (серия)
		int32 NExtendAtCap = -1;            // продлений к моменту применения
		// ★ 18.10.2026 (ВАЖНО 3): сценарий «новый взвод».
		TArray<float> RemPre;               // оставшееся время таймеров цифр своего замаха перед обрывом (с; −1 — не активен)
		TArray<float> RemPost;              // то же сразу после нового «взвода» (тот же кадр)
		TArray<double> DueAt;               // ожидаемые моменты (мир-время) таймеров, ждавших на обрыве
		TArray<double> DigitAt;             // фактические показы цифр по цели после обрыва (мир-время)
		double TInt = -1.0;                 // момент обрыва (мир-время)
		int32 ShownMark = 0;                // счётчик показанных результатов удара на прошлом опросе
		float MaxShift = 0.f;               // наибольший сдвиг таймера на «взводе» (с)
		// ★ 18.10.2026 (плавный бой): сценарии «двойной старт серии + смертельный ответ без ударов» и «HP > MaxHp».
		int32 SeenFrom = 0;                 // начало наблюдений `HitResultsSeen` сценария
		int32 BarMax = -1;                  // наибольшее HP полосы за сценарий (подъём полосы)
		int32 MaxHp0 = -1, MaxHpEnd = -1;   // MaxHp моба после спауна и на конце сценария
		int32 TypeMaxHp = -1;               // MaxHp типа (DT_MobStats) моба сценария
		double TStance = -1.0;              // первый опрос после смерти, когда замах своего персонажа снят (мир-время)
		double T3 = -1.0;                   // приход смертельного ответа без ударов (мир-время)
		bool bFed3 = false, bFed4017 = false, bFed2 = false, bFedMid = false;   // поданы: ответ №3, 4017 flags=0, ответ №2, 4015 между
		bool bFed3b = false;                // ★ ревизия 18.10: подан повторный смертельный ответ
		TArray<double> ShotAt;              // ★ ревизия 18.10: моменты выстрелов своего дальнобойного по цели (мир-время)
		int32 HpPrev = -1, HpRises = 0, HpRiseMax = 0;   // ★ 18.10.2026 (пол HP): полоса на прошлом опросе, число и наибольший подъём
		int32 NextEv = 0;                   // ★ 18.10.2026: следующее событие расписания живой последовательности

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		double Now() const { const UWorld* W = World.Get(); return W ? W->GetTimeSeconds() : 0.0; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		int32 NewRid() { const int32 R = NextRid++; Rids.Add(R); return R; }
		// Момент относительно 4026 (с); −1 — не было.
		double Rel(double T) const { return (T >= 0.0 && T0 >= 0.0) ? T - T0 : -1.0; }
	};

	// Тело 4015 (37 б, раскладка `Send_SectorInUpdateMonster_NPC_Info`): @4 rid, @6 kind, @7/@9 cur, @11/@13 dst, @25 HP.
	//   ★ 18.10.2026: Kind — вид 4015 (1 — стоп на клетке, 2 — ход cur → dst; здесь dst = cur).
	static TArray<uint8> Body4015(int32 Rid, const FIntPoint& Tile, int32 Hp, uint8 Kind = 1)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], -1);
		AsdaBytes::WrI16(&B[2], (int16)kMobType);
		AsdaBytes::WrI16(&B[4], (int16)Rid);
		B[6] = Kind;
		AsdaBytes::WrI16(&B[7], (int16)Tile.X);  AsdaBytes::WrI16(&B[9], (int16)Tile.Y);
		AsdaBytes::WrI16(&B[11], (int16)Tile.X); AsdaBytes::WrI16(&B[13], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[21], -1);
		AsdaBytes::WrI32(&B[25], Hp);
		B[29] = 0xFF;
		AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
	// Тело 4017 (100 б): @0 тип, @2 rid, @4 флаги (0 — смерть, 3 — живой в бою, 4 — реген с клеткой), @8 28×i16 id (−1),
	//   @92 HP, @96/@98 клетка.
	static TArray<uint8> Body4017(int32 Rid, const FIntPoint& Tile, int32 Flags, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], (int16)kMobType);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], Flags);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], Hp);
		AsdaBytes::WrI16(&B[96], (int16)Tile.X); AsdaBytes::WrI16(&B[98], (int16)Tile.Y);
		return B;
	}
	// Тело 4029 (49 б, `Recv_CastState@0xF41620`): @0 i16 атакующий, @2 i16 rid, @4 i32 HP цели, @8 i32[4] очередь (0 — пусто),
	//   @24 sub (0 — удар принят, иначе отказ), @25 i16[4] второй канал (−1), @33 i32[4] (0).
	static TArray<uint8> Body4029(int32 AtkU, int32 Rid, int32 Hp, const TArray<int32>& Q, uint8 Sub = 0)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)AtkU);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], Hp);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], Q[i]); }
		B[24] = Sub;
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}
	// ★ 18.10.2026: тело 4027 case 1 (53 б, `Recv_CombatAnimState@0xF3EEB0`): @0 case, @1 атакующий, @3 rid, @5/@7 живая клетка цели,
	//   @9 HP (сырое, без вычета накопителя), @13 i32[4] очередь, @29 i16[4] второй канал (−1), @37 i32[4].
	static TArray<uint8> Body4027(int32 AtkU, int32 Rid, const FIntPoint& Tile, int32 Hp, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], (int16)AtkU);
		AsdaBytes::WrI16(&B[3], (int16)Rid);
		AsdaBytes::WrI16(&B[5], (int16)Tile.X); AsdaBytes::WrI16(&B[7], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[9], Hp);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[13 + 4 * i], Q[i]); }
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[29 + 2 * i], -1); }
		return B;
	}
	// Запись 4013/4019 (одна, шаг 174) с заданным HP @9 — повтор по известному мобу.
	static TArray<uint8> Body4013Hp(int32 Rid, const FIntPoint& Tile, int32 Hp)
	{
		TArray<uint8> B = AsdaTestPackets::MobSpawn4013(Rid, kMobType, Tile, /*State*/1, kSpdDenom);
		AsdaBytes::WrI32(&B[9], Hp);
		return B;
	}

	// Первое оружие подтипа (тип 9, не удочка) с дальностью. 0 — нет.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9 && R->ItemSubType == Wt && R->ItemVariant != 1 && R->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}

	static int32 CountProjectiles(UWorld* W)
	{
		int32 C = 0;
		if (W) { for (TActorIterator<AAsdaProjectileActor> It(W); It; ++It) { if (IsValid(*It)) { ++C; } } }
		return C;
	}

	static const TCHAR* VarName(EVar V)
	{
		switch (V)
		{
		case EVar::LethalReply:    return TEXT("смертельный ответ 4029 + 4015 HP=0 (0.1 с) + 4017 flags=0 (1.0 с)");
		case EVar::NoReply:        return TEXT("ответа 4029 нет; 4015 HP=0 (0.1 с) + 4017 flags=0 (1.0 с)");
		case EVar::NonLethal4017:  return TEXT("не смертельный 4029 HP=40 + 4017 HP=40 (0.1 с) + 4015 HP=40 (0.15 с)");
		case EVar::NoShowThenZero: return TEXT("ответ «−2» (урон не показан) + 4015 HP=0 (0.1 с) + 4017 flags=0 (1.0 с)");
		case EVar::SnapNoHits:     return TEXT("снимок без ударов: 4029 HP=70, очередь пуста");
		case EVar::Repeat4013Zero: return TEXT("смертельный 4029 + повторная 4013 HP=0 (0.1 с)");
		case EVar::Loop4019Zero:   return TEXT("смертельный 4029 + 4019 HP=0 (0.1 с)");
		case EVar::SwitchNoReply:  return TEXT("ответа нет; 4015 HP=0 (0.1 с); смена цели (0.3 с)");
		case EVar::SwitchLethal:   return TEXT("смертельный ответ + 4015 HP=0 (0.1 с); смена цели (0.3 с)");
		case EVar::CapSeries:      return TEXT("серия продлений: 4029 HP=90 + 4015 HP=30 (0.4 с), окно продлевается отправкой каждый кадр");
		case EVar::Refused:        return TEXT("отказ 4029 (sub 5) + 4015 HP=0 (0.1 с)");
		case EVar::NoDeathOnHit:   return TEXT("смертельный ответ + 4015 HP=0 (0.1 с), DeathOnHitFrame 0");
		case EVar::Hp4015First:    return TEXT("4015 HP=40 раньше ответа, 4029 HP=40 (0.05 с)");
		case EVar::Regen4017:      return TEXT("ответа нет; 4015 HP=0 (0.1 с); реген 4017 flags=4 HP=80 (0.4 с)");
		case EVar::Regen4013:      return TEXT("ответа нет; 4015 HP=0 (0.1 с); повторная 4013 HP=80 (0.4 с)");
		case EVar::NonLethal4013Zero: return TEXT("НЕ смертельный 4029 HP=40 + повторная 4013 HP=0 (0.1 с): смерть ≤ 0.5 с");
		case EVar::NonLethal4019Zero: return TEXT("НЕ смертельный 4029 HP=40 + 4019 HP=0 (0.1 с): смерть ≤ 0.5 с");
		}
		return TEXT("?");
	}
}

struct FAsdaCombatHitFrameSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaCombatHitFrameSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaHitFrameTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Combat.SelfTestHitFrame"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaHitFrameTest, Warning, TEXT("САМОТЕСТ-КАДРУДАРА: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	// Ключ уже 0 на старте (`--pre "asda.Combat.HitFrameAllChannels 0"`) — контрольный прогон: все бои с ключом 0.
	if (const IConsoleVariable* CvAll = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Combat.HitFrameAllChannels")))
	{
		S->bControlAll = CvAll->GetInt() == 0;
	}

	auto Add = [S](const FString& Name, float MinSec, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.MinSec = MinSec; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(Op, B); } };
	auto T = [S](int32 Dx, int32 Dz) { return S->HomeTile + FIntPoint(Dx, Dz); };
	// Сброс боя и сценария (мобы прошлых сценариев убираются).
	auto Reset = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (PC)
		{
			PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
			PC->bArmImpactOnSwingStart = false;
			PC->NextAttackTickTime = 0.0; PC->TargetActor = nullptr;
			PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);
			PC->CancelRoute();
		}
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест кадра удара: сброс сценария")); }
		if (C) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->NetSnap(S->Home); }
		for (const int32 R : S->Rids) { if (AAsdaMobActor* M = S->Mob(R)) { M->Destroy(); } }
		// R5-fix7 (14.11.2026): серверные часы атаки — в чистое состояние перед КАЖДЫМ сценарием, а не только в конце прогона.
		//   Сценарий кончается обрывом замаха или смертью цели, и его последняя заявка удара может остаться без ответа; первый ответ
		//   следующего сценария игра сверяла с этой заявкой («свой ответ 4029 — ПОЗДНИЙ, серии #24 (сейчас #25)») и не показывала:
		//   при кадре 100 мс так краснел контроль «новый взвод» («на обрыве ждали 0 таймеров»).
		if (UAsdaNetSubsystem* NnR = S->N()) { if (UGameInstance* GiR = NnR->GetGameInstance()) { if (UAsdaAttackRhythmSubsystem* RhR = GiR->GetSubsystem<UAsdaAttackRhythmSubsystem>()) { RhR->DebugReset(); } } }
		S->Rids.Reset();
		S->Rid = -1; S->Rid2 = -1; S->Phase = 0; S->T0 = -1.0; S->PlannedHitS = -1.f;
		S->bFed005 = S->bFed01 = S->bFed015 = S->bFedSwitch = S->bFed04 = S->bFed10 = false;
		S->bFedOther = S->bFedOther2 = false;
		S->TShot = S->TImpact = S->THpDrop = S->TDeath = -1.0; S->THitFrame = S->TDigit = -1.0;
		S->TOtherHp = S->TOtherDeath = -1.0; S->DigitsAtStart = 0; S->Digits = 0;
		S->bSawProjectile = false; S->FinalHp = -1; S->bFinalServerDead = false; S->N26 = 0; S->N28 = 0;
		S->ProjSpawnS = -1.0; S->PrevPollS = -1.0; S->HpDropLagS = 0.0; S->CapLagS = 0.0;   // ★ R5-fix6
		S->DeathLagS = 0.0; S->StanceLagS = 0.0; S->RearmMaxFrameS = 0.0; S->N26Frame = 0;   // ★ R5-fix7 (ревизия): поля сроков — тоже
		S->bWindowAtSend = false; S->bWindowAfterRefuse = true; S->bWindowAt4 = false; S->bWindowAtEnd = false;
		S->TCap = -1.0; S->NExtend = 0; S->NExtendAtCap = -1;
	};

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 0.f, 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
		if (!C || !PC) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		S->SavedWeapon = Net->SelfWeaponItemId;
		S->SavedEquipped = C->EquippedWeaponSubType;
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, TEXT("asda.Combat.SelfTestHitFrame"));
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		TWeakPtr<FState> WSp = S;
		// ★ R5-fix6: рождение снаряда — событием мира (снимается в итоге прогона).
		if (UWorld* Wsp = S->World.Get())
		{
			S->ProjSpawnHandle = Wsp->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([WSp](AActor* A)
			{
				TSharedPtr<FState> St = WSp.Pin();
				// ★ Ревизия: первый снаряд ПОСЛЕ ухода 4026 сценария (снаряд, родившийся раньше, событие не занимает).
				if (St && St->T0 >= 0.0 && St->ProjSpawnS < 0.0 && Cast<AAsdaProjectileActor>(A)) { St->ProjSpawnS = St->Now(); }
			}));
		}
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (!St) { return; }
			// R5-fix7 (14.11.2026): кадр игры, в котором ушёл 4026. Ответ сценария подаётся не раньше следующего кадра: замах своего
			//   персонажа стартует тиком после отправки, и ответ, поданный в том же кадре (опрос теста идёт после тика), игра принимала
			//   как «ответ без показанных ударов» — значения первого ответа терялись (при кадре 100 мс: показано 9 9 9 9 вместо
			//   −2 −1 9 10 9 9; «на обрыве ждали 0 таймеров»). У настоящего сервера ответ в кадре отправки не приходит.
			if (Op == 4026) { ++St->N26; St->N26Frame = GFrameCounter; }
			if (Op == 4028) { ++St->N28; }
		}, (uint16)0, TEXT("asda.Combat.SelfTestHitFrame"));
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RhythmFromAnim"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.EvenSwingTempo"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.HitFrameAllChannels"), S->bControlAll ? 0 : 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.DeathOnHitFrame"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.HpSnapshotOnHitFrame"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), 1);
		UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА дом: клетка (%d,%d)%s"), S->HomeTile.X, S->HomeTile.Y,
			S->bControlAll ? TEXT(" | КОНТРОЛЬНЫЙ ПРОГОН: ключ asda.Combat.HitFrameAllChannels = 0 на старте — все бои с ключом 0, провалы ожидаемы (дефект виден)") : TEXT(""));
	}, []() { return true; }, []() {});

	// ─────────────────────── Бой: сценарий «вариант ответа сервера × оружие» ───────────────────────
	auto AddFight = [S, Add, Feed, T, Reset](int32 Wt, const TCHAR* WName, EVar Var, int32 AllChannels, bool bWithOther)
	{
		const bool bRanged = UAsdaWorldSubsystem::IsRangedWeapon(Wt);
		const bool bSecondMob = bWithOther || Var == EVar::SwitchNoReply || Var == EVar::SwitchLethal;   // нужен второй моб
		Add(FString::Printf(TEXT("%s: %s, HitFrameAllChannels %d"), WName, VarName(Var), AllChannels), 0.f, 12.f,
		[S, Feed, T, Reset, Wt, Var, AllChannels, bRanged, bSecondMob]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.HitFrameAllChannels"), S->bControlAll ? 0 : AllChannels);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.DeathOnHitFrame"), Var == EVar::NoDeathOnHit ? 0 : 1);
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
			if (!G || !C || !Nn) { return; }
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), Wt);
			C->EquippedWeaponSubType = Wt;
			S->Expect(Nn->SelfWeaponItemId > 0, FString::Printf(TEXT("оружие подтипа %d: предмет %d"), Wt, Nn->SelfWeaponItemId));
			S->Rid = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(bRanged ? 3 : 1, 0), /*State*/1, kSpdDenom));
			if (bSecondMob)
			{
				// Второй моб: чужой (нашего замаха нет — его HP и смерть обязаны применяться сразу) или новая цель при смене.
				S->Rid2 = S->NewRid();
				Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid2, kMobType, T(-3, 2), /*State*/1, kSpdDenom));
			}
		},
		[S, Feed, T, Var, bRanged, bWithOther, Wt]()
		{
			AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			UAsdaWorldSubsystem* WSs = S->WS();
			UWorld* Wd = S->World.Get();
			const double Now = S->Now();
			const UAsdaNetSubsystem* Nn = S->N();
			const int32 Self = Nn ? (int32)Nn->SelfUserNum : 0;
			const FIntPoint MobTile = T(bRanged ? 3 : 1, 0);
			if (S->Phase == 0 && M && PC && FPlatformTime::Seconds() - S->StepStartS > 0.6)
			{
				PC->SetTarget(M);
				PC->EngageMobFromClick(M);   // второй клик: бой
				S->Phase = 1;
			}
			if (S->Phase == 1 && S->N26 > 0 && GFrameCounter > S->N26Frame)
			{
				S->T0 = Now;
				S->Phase = 2;
				S->DigitsAtStart = WSs ? WSs->HitResultsShown : 0;
				S->bWindowAtSend = WSs && WSs->IsOurImpactWindowOpenFor(S->Rid);   // окно по отправке — до любого ответа
				// План кадра удара на момент 4026 — только для журнала и как опора контроля, где замах обрывается смертью цели.
				if (C)
				{
					const AAsdaPlayerCharacter::FAsdaComboTiming CT = C->GetComboTiming(C->EquippedWeaponSubType);
					S->PlannedHitS = (CT.bValid && CT.HitTimesSec.Num() > 0) ? FMath::Max(0.f, CT.HitTimesSec[0] - C->GetAttackSwingElapsedSec()) : -1.f;
				}
				// Ответ на заявку (как живой сервер: ~30 мс после 4026 — здесь в том же кадре).
				switch (Var)
				{
				case EVar::LethalReply: case EVar::SwitchLethal: case EVar::NoDeathOnHit: case EVar::Repeat4013Zero: case EVar::Loop4019Zero:
					Feed(4029, Body4029(Self, S->Rid, 0, { 100 })); break;
				case EVar::NonLethal4017: case EVar::NonLethal4013Zero: case EVar::NonLethal4019Zero:
					Feed(4029, Body4029(Self, S->Rid, 40, { 60 })); break;
				case EVar::NoShowThenZero:
					Feed(4029, Body4029(Self, S->Rid, kMobHp, { -2 })); break;
				case EVar::SnapNoHits:
					Feed(4029, Body4029(Self, S->Rid, 70, {})); break;
				case EVar::CapSeries:
					Feed(4029, Body4029(Self, S->Rid, 90, { 10 })); break;
				case EVar::Refused:
					Feed(4029, Body4029(Self, S->Rid, kMobHp, {}, /*Sub=*/5));
					S->bWindowAfterRefuse = WSs && WSs->IsOurImpactWindowOpenFor(S->Rid);
					break;
				case EVar::Hp4015First:
					Feed(4015, Body4015(S->Rid, MobTile, 40)); break;   // живое HP раньше ответа
				default: break;
				}
			}
			if (S->Phase >= 2)
			{
				const double R = Now - S->T0;
				// Серверные каналы, обгоняющие наш выстрел (живое HP — сразу; смерть моба в бою — через 1000 мс).
				if (!S->bFed005 && R >= 0.05)
				{
					S->bFed005 = true;
					if (Var == EVar::Hp4015First) { Feed(4029, Body4029(Self, S->Rid, 40, { 60 })); }
				}
				if (!S->bFed01 && R >= 0.10)
				{
					S->bFed01 = true;
					switch (Var)
					{
					case EVar::NonLethal4017:  Feed(4017, Body4017(S->Rid, MobTile, 3, 40)); break;
					case EVar::Repeat4013Zero: case EVar::NonLethal4013Zero: Feed(4013, Body4013Hp(S->Rid, MobTile, 0)); break;
					case EVar::Loop4019Zero:   case EVar::NonLethal4019Zero: Feed(4019, Body4013Hp(S->Rid, MobTile, 0)); break;
					case EVar::SnapNoHits: case EVar::Hp4015First: case EVar::CapSeries: break;
					default:                   Feed(4015, Body4015(S->Rid, MobTile, 0)); break;
					}
				}
				if (!S->bFed015 && R >= 0.15)
				{
					S->bFed015 = true;
					if (Var == EVar::NonLethal4017) { Feed(4015, Body4015(S->Rid, MobTile, 40)); }
				}
				if (!S->bFedSwitch && R >= 0.30)
				{
					S->bFedSwitch = true;
					if ((Var == EVar::SwitchNoReply || Var == EVar::SwitchLethal) && PC)
					{
						if (AAsdaMobActor* M2 = S->Mob(S->Rid2)) { PC->SetTarget(M2); }   // смена цели при отложенной смерти
					}
				}
				if (!S->bFed04 && R >= 0.40)
				{
					S->bFed04 = true;
					if (Var == EVar::Regen4017) { Feed(4017, Body4017(S->Rid, MobTile, 4, 80)); }
					if (Var == EVar::Regen4013) { Feed(4013, Body4013Hp(S->Rid, MobTile, 80)); }
					if (Var == EVar::CapSeries) { Feed(4015, Body4015(S->Rid, MobTile, 30)); }   // ★ Ф1: при продлениях (исход неизвестен)
				}
				if (!S->bFed10 && R >= 1.0)
				{
					S->bFed10 = true;
					if (Var == EVar::LethalReply || Var == EVar::NoReply || Var == EVar::NoShowThenZero || Var == EVar::SwitchNoReply
					    || Var == EVar::SwitchLethal || Var == EVar::NoDeathOnHit)
					{
						Feed(4017, Body4017(S->Rid, MobTile, 0, 0));
					}
				}
				// Серия продлений: штатная точка отправки удара (`ArmOurImpactWindowFromSend`, её зовёт контроллер на 4026/4028)
				//   каждый кадр, пока придержанное не применено, — окно всё время уходит вперёд, его конец даёт только потолок.
				if (Var == EVar::CapSeries && R >= 0.2 && S->TCap < 0.0 && WSs && C && M)
				{
					WSs->ArmOurImpactWindowFromSend(C, M, Wt, TEXT("самотест: продление серией"));
					++S->NExtend;
				}
				if (bWithOther && !S->bFedOther && R >= 0.12)
				{
					S->bFedOther = true;
					Feed(4015, Body4015(S->Rid2, T(-3, 2), 50));
				}
				if (bWithOther && !S->bFedOther2 && R >= 0.25)
				{
					S->bFedOther2 = true;
					Feed(4017, Body4017(S->Rid2, T(-3, 2), 0, 0));
				}
				// Наблюдения.
				const int32 NP = CountProjectiles(Wd);
				// ★ R5-fix6: вылет — по событию рождения снаряда после ухода 4026 (опрос остаётся запасным путём).
				if (S->TShot < 0.0 && S->ProjSpawnS >= 0.0 && S->T0 >= 0.0 && S->ProjSpawnS >= S->T0 - 1e-3) { S->TShot = S->ProjSpawnS; S->bSawProjectile = true; }
				if (NP > 0 && S->TShot < 0.0) { S->TShot = Now; S->bSawProjectile = true; }
				if (S->bSawProjectile && NP == 0 && S->TImpact < 0.0) { S->TImpact = Now; }
				// Фактический кадр удара: позиция клипа (клип-секунды) прошла первый кадр удара combo_time (клип-секунды =
				//   реальные × темп, которым они поделены) — то, что видно на экране, а не расчёт момента на 4026.
				if (C && S->THitFrame < 0.0 && C->IsAttackAnimActive())
				{
					const AAsdaPlayerCharacter::FAsdaComboTiming CTn = C->GetComboTiming(C->EquippedWeaponSubType);
					if (CTn.bValid && CTn.HitTimesSec.Num() > 0 && C->GetAttackClipPositionSec() + 1e-3f >= CTn.HitTimesSec[0] * CTn.TempoMul)
					{
						S->THitFrame = Now;
					}
				}
				if (WSs)
				{
					S->Digits = WSs->HitResultsShown - S->DigitsAtStart;
					if (S->TDigit < 0.0 && S->Digits > 0 && WSs->LastHitResultShownRid == S->Rid) { S->TDigit = WSs->LastHitResultShownS; }
				}
				const double PollLagS = (S->PrevPollS >= 0.0) ? FMath::Max(0.0, (Now - S->PrevPollS) - 1.0 / 60.0) : 0.0;   // ★ R5-fix6: шаг опроса сверх кадра 1/60 с
				S->PrevPollS = Now;
				if (M && S->THpDrop < 0.0 && M->CurHp < kMobHp) { S->THpDrop = Now; S->HpDropLagS = PollLagS; }
				if (M && S->TDeath < 0.0 && M->IsDead()) { S->TDeath = Now; S->DeathLagS = PollLagS; }
				if (M) { S->FinalHp = M->CurHp; S->bFinalServerDead = M->bServerDead; }
				if (Var == EVar::CapSeries)
				{
					if (M && S->TCap < 0.0 && M->CurHp <= 30) { S->TCap = Now; S->NExtendAtCap = S->NExtend; S->CapLagS = PollLagS; }
					if (!S->bWindowAt4 && R >= 4.0 && S->TCap < 0.0 && WSs) { S->bWindowAt4 = WSs->IsOurImpactWindowOpenFor(S->Rid); }
				}
				if (bWithOther)
				{
					AAsdaMobActor* M2 = S->Mob(S->Rid2);
					if (M2 && S->TOtherHp < 0.0 && M2->CurHp == 50) { S->TOtherHp = Now; }
					if (M2 && S->TOtherDeath < 0.0 && M2->IsDead()) { S->TOtherDeath = Now; }
				}
				S->bWindowAtEnd = WSs && WSs->IsOurImpactWindowOpenFor(S->Rid);
				// Конец: смерть/итог видны и прошло достаточно (окно ≤ 4 с + запас).
				const bool bEndKill = IsKillVar(Var) && S->TDeath >= 0.0 && Now - S->TDeath > 0.3 && R > 1.2;
				const bool bEndHit = (Var == EVar::NonLethal4017 || Var == EVar::Hp4015First) && R > 4.0;
				const bool bEndSnap = Var == EVar::SnapNoHits && R > 3.5;
				const bool bEndCap = Var == EVar::CapSeries && R > 5.5;
				const bool bEndRegen = (Var == EVar::Regen4017 || Var == EVar::Regen4013) && R > 5.0;
				return bEndKill || bEndHit || bEndSnap || bEndCap || bEndRegen || R > 7.0;
			}
			return false;
		},
		[S, Var, bRanged, bWithOther, AllChannels, Wt]()
		{
			UE_LOG(LogAsdaHitFrameTest, Log,
				TEXT("САМОТЕСТ-КАДРУДАРА замер (от 4026, с): план кадра удара %.2f | кадр удара факт %.2f | вылет снаряда %.2f | прилёт %.2f | цифра %.2f (показано %d) | полоса вниз %.2f | труп %.2f | итог HP %d | 4028 за сценарий %d"),
				S->PlannedHitS, S->Rel(S->THitFrame), S->Rel(S->TShot), S->Rel(S->TImpact), S->Rel(S->TDigit), S->Digits,
				S->Rel(S->THpDrop), S->Rel(S->TDeath), S->FinalHp, S->N28);
			if (Var == EVar::NoDeathOnHit) { AsdaCVarSave::SetForTest(TEXT("asda.Combat.DeathOnHitFrame"), 1); }
			if (AllChannels == 0)
			{
				// Контроль: прежнее окно — труп раньше выстрела (дефект виден). Снаряд при этом может не вылететь вовсе: цель
				//   умерла до кадра удара, и замах по трупу не стреляет — тогда опора — наблюдённый кадр удара, а если замах
				//   оборвался раньше него — запланированный на 4026.
				const double Hit = (bRanged && S->TImpact >= 0.0) ? S->TImpact
					: (S->THitFrame >= 0.0 ? S->THitFrame : (S->T0 + (double)FMath::Max(0.f, S->PlannedHitS)));
				S->Expect(S->TDeath >= 0.0 && S->T0 >= 0.0 && S->TDeath < Hit - kTol,
					FString::Printf(TEXT("контроль, HitFrameAllChannels 0: труп %.2f с РАНЬШЕ выстрела/попадания %.2f с (снаряд %s) — дефект жалобы виден, проверка не слепая"),
						S->Rel(S->TDeath), S->Rel(Hit), S->bSawProjectile ? TEXT("вылетел") : TEXT("не вылетел — стрелять было уже не в кого")));
				AsdaCVarSave::SetForTest(TEXT("asda.Combat.HitFrameAllChannels"), S->bControlAll ? 0 : 1);
				return;
			}
			// Окно открывается отправкой 4026 — до ответа сервера (кроме отказа: там оно открыто тоже, до прихода отказа).
			S->Expect(S->bWindowAtSend, FString::Printf(TEXT("окно «замах в полёте» открылось по отправке 4026 (%s)"), S->bWindowAtSend ? TEXT("да") : TEXT("нет")));
			// Попадание — наблюдаемое: прилёт снаряда (дальнобойное) или показ цифры удара (ближний).
			const double Impact = bRanged ? S->TImpact : S->TDigit;
			if (bRanged && (Var == EVar::LethalReply || Var == EVar::NonLethal4017 || Var == EVar::NoShowThenZero || Var == EVar::Repeat4013Zero
			    || Var == EVar::Loop4019Zero || Var == EVar::SwitchLethal || Var == EVar::Hp4015First))
			{
				S->Expect(S->TShot >= 0.0 && S->TImpact >= 0.0, FString::Printf(TEXT("снаряд вылетел (%.2f с) и долетел (%.2f с)"), S->Rel(S->TShot), S->Rel(S->TImpact)));
			}
			switch (Var)
			{
			case EVar::NonLethal4017:
			case EVar::Hp4015First:
			{
				S->Expect(S->THpDrop >= 0.0 && Impact >= 0.0 && S->THpDrop >= Impact - kTol,
					FString::Printf(TEXT("полоса ушла вниз на попадании (%.2f с), не раньше (попадание %.2f с) — HP %s придержан"),
						S->Rel(S->THpDrop), S->Rel(Impact), Var == EVar::Hp4015First ? TEXT("из 4015, пришедший раньше ответа,") : TEXT("из 4017/4015")));
				S->Expect(S->FinalHp == 40 && S->TDeath < 0.0, FString::Printf(TEXT("итог: HP = серверным 40 (%d), моб жив"), S->FinalHp));
				return;
			}
			case EVar::NoReply:
			{
				S->Expect(S->THitFrame >= 0.0 && S->TDeath >= 0.0 && S->TDeath >= S->THitFrame - kTol,
					FString::Printf(TEXT("ответа нет: труп %.2f с — не раньше фактического кадра удара %.2f с (план на 4026 — %.2f)"),
						S->Rel(S->TDeath), S->Rel(S->THitFrame), S->PlannedHitS));
				S->Expect(S->TDeath >= 0.0 && S->Rel(S->TDeath) <= 4.0 + 1.0,
					FString::Printf(TEXT("окно закрылось само: труп через %.2f с (≤ потолка окна 4 с + запас), не застрял"), S->Rel(S->TDeath)));
				S->Expect(S->THpDrop < 0.0 || (S->THitFrame >= 0.0 && S->THpDrop >= S->THitFrame - kTol),
					FString::Printf(TEXT("полоса не ушла раньше кадра удара (%.2f с)"), S->Rel(S->THpDrop)));
				S->Expect(!S->bWindowAtEnd, TEXT("окно на конце закрыто"));
				return;
			}
			case EVar::SnapNoHits:
			{
				S->Expect(S->THitFrame >= 0.0 && S->THpDrop >= 0.0 && S->THpDrop >= S->THitFrame - kTol && S->THpDrop <= S->THitFrame + kLateMax,
					FString::Printf(TEXT("снимок без ударов: полоса вниз %.2f с — на фактическом кадре удара %.2f с + полёт, не на ответе"),
						S->Rel(S->THpDrop), S->Rel(S->THitFrame)));
				// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: момент цифры — точный (событие показа), а падение полосы замечает опрос
				//   ПОСЛЕ него: настоящий момент полосы лежит в пределах шага опроса до замера. Допуск 0,05 с прежний; снизу к нему
				//   прибавлен шаг опроса сверх кадра 1/60 с (при длинном кадре было «1.34 против 1.39/1.41»).
				S->Expect(S->Digits == 1 && S->TDigit >= 0.0 && S->TDigit <= S->THpDrop + 0.05 && S->TDigit >= S->THpDrop - 0.05 - S->HpDropLagS,
					FString::Printf(TEXT("цифра урона по дельте показана ровно одна (%d) вместе с полосой (%.2f против %.2f, шаг опроса %.2f с) — ревизия п.2"),
						S->Digits, S->Rel(S->TDigit), S->Rel(S->THpDrop), S->HpDropLagS));
				S->Expect(S->FinalHp == 70 && S->TDeath < 0.0, FString::Printf(TEXT("итог: HP = 70 из снимка (%d), моб жив"), S->FinalHp));
				return;
			}
			case EVar::SwitchNoReply:
			{
				S->Expect(S->TDeath >= 0.0 && S->Rel(S->TDeath) >= 0.30 - 0.02 && S->Rel(S->TDeath) <= 0.30 + 0.15,
					FString::Printf(TEXT("смена цели при отложенной смерти без ответа: труп %.2f с — сразу при смене (0.30 с), окно закрыто"), S->Rel(S->TDeath)));
				return;
			}
			case EVar::Refused:
			{
				S->Expect(!S->bWindowAfterRefuse, TEXT("отказ 4029 закрыл окно «замах в полёте»"));
				S->Expect(S->TDeath >= 0.0 && S->Rel(S->TDeath) <= 0.10 + 0.12,
					FString::Printf(TEXT("после отказа смерть 4015 HP=0 применена сразу (%.2f с, пакет в 0.10 с)"), S->Rel(S->TDeath)));
				return;
			}
			case EVar::NoDeathOnHit:
			{
				S->Expect(S->TDeath >= 0.0 && S->Rel(S->TDeath) <= 0.10 + 0.12,
					FString::Printf(TEXT("DeathOnHitFrame 0: смерть 4015 HP=0 не откладывается (%.2f с, пакет в 0.10 с)"), S->Rel(S->TDeath)));
				return;
			}
			case EVar::NonLethal4013Zero:
			case EVar::NonLethal4019Zero:
			{
				// Серверная смерть в 0.10 с, известен только НЕ смертельный ответ: смерть на ближайшем попадании, но не позже 0.10 + 0.5 с.
				// ★ R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: срок истекает на первом тике игры после него, а труп замечает первый
				//   опрос после этого — к прежней границе прибавлены два шага опроса сверх кадра 1/60 с (при кадре 100 мс было 0,70 с).
				S->Expect(S->TDeath >= 0.0 && S->Rel(S->TDeath) >= 0.10 - 0.02 && S->Rel(S->TDeath) <= 0.10 + 0.5 + kTol + 2.0 * S->DeathLagS,
					FString::Printf(TEXT("известен не смертельный ответ + HP=0 из %s: труп %.2f с — не позже серверной смерти + 0.5 с (0.60 с)"),
						Var == EVar::NonLethal4013Zero ? TEXT("4013") : TEXT("4019"), S->Rel(S->TDeath)));
				S->Expect(Impact < 0.0 || S->TDeath <= Impact + kLateMax,
					FString::Printf(TEXT("труп не позже попадания + %.1f с (%.2f против %.2f)"), kLateMax, S->Rel(S->TDeath), S->Rel(Impact)));
				return;
			}
			case EVar::CapSeries:
			{
				// ★ Ф2 (ревизия Ф1 п.1): удержание — снова с ПЕРВОГО ПРИДЕРЖАННОГО пакета (4015 HP=30 в 0.40 с). Ответ 4029 HP=90 с
				//   ударом [10] полосу не «держит»: 100 ≤ снимок 90 + непоказанный урон 10 (полоса выше снимка до показа ударов — норма).
				const double CapAt = 0.40 + kHoldCapS;
				S->Expect(S->bWindowAt4, TEXT("серия продлевает окно: в 4.0 с оно ещё открыто (без потолка держало бы дальше)"));
				// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: потолок срабатывает на первом тике игры после срока, а тест замечает его
				//   первым опросом после этого — два шага сверх кадра 1/60 с прибавлены к прежнему допуску +0,12 с (при длинном кадре
				//   было 5,04 и 5,17 с при границе 5,02). Нижняя граница прежняя.
				S->Expect(S->TCap >= 0.0 && S->Rel(S->TCap) <= CapAt + 0.12 + 2.0 * S->CapLagS && S->Rel(S->TCap) >= CapAt - 0.05,
					FString::Printf(TEXT("абсолютный потолок: придержанное HP=30 применено в %.2f с (потолок %.2f с от первого придержанного, шаг опроса %.2f с)"),
						S->Rel(S->TCap), CapAt, S->CapLagS));
				S->Expect(S->NExtendAtCap >= 20, FString::Printf(TEXT("к потолку окно продлено %d раз (серия)"), S->NExtendAtCap));
				return;
			}
			case EVar::Regen4017:
			case EVar::Regen4013:
			{
				S->Expect(S->TDeath < 0.0 && !S->bFinalServerDead && S->FinalHp == 80,
					FString::Printf(TEXT("реген при отложенной смерти: моб жив (%s), метка «обречён» снята (%s), HP = 80 (%d)"),
						S->TDeath < 0.0 ? TEXT("да") : TEXT("НЕТ"), S->bFinalServerDead ? TEXT("НЕТ") : TEXT("да"), S->FinalHp));
				return;
			}
			default: break;
			}
			// Смертельный вариант, «−2», 4013/4019 HP=0, смена цели со смертельным ответом: труп и полоса — на попадании.
			S->Expect(S->TDeath >= 0.0 && Impact >= 0.0 && S->TDeath >= Impact - kTol,
				FString::Printf(TEXT("труп %.2f с — не раньше попадания %.2f с (%s; серверная смерть пришла в 0.10 с)"),
					S->Rel(S->TDeath), S->Rel(Impact), bRanged ? TEXT("прилёт снаряда") : TEXT("показ цифры удара")));
			S->Expect(S->TDeath >= 0.0 && Impact >= 0.0 && S->TDeath <= Impact + kLateMax,
				FString::Printf(TEXT("труп не позже попадания + %.1f с (%.2f против %.2f) — окно не застревает"), kLateMax, S->Rel(S->TDeath), S->Rel(Impact)));
			if (Var == EVar::LethalReply || Var == EVar::SwitchLethal)
			{
				S->Expect(S->THpDrop >= 0.0 && Impact >= 0.0 && S->THpDrop >= Impact - kTol,
					FString::Printf(TEXT("полоса ушла вниз на попадании (%.2f с), не раньше (%.2f с)"), S->Rel(S->THpDrop), S->Rel(Impact)));
			}
			S->Expect(!S->bWindowAtEnd, TEXT("окно на конце закрыто"));
			if (bWithOther)
			{
				S->Expect(S->TOtherHp >= 0.0 && S->Rel(S->TOtherHp) <= 0.12 + 0.1,
					FString::Printf(TEXT("обратная: чужой моб (нашего замаха нет) — HP из 4015 применён сразу (%.2f с, пакет в 0.12 с)"), S->Rel(S->TOtherHp)));
				S->Expect(S->TOtherDeath >= 0.0 && S->Rel(S->TOtherDeath) <= 0.25 + 0.1,
					FString::Printf(TEXT("обратная: чужой моб — смерть 4017 flags=0 сразу (%.2f с, пакет в 0.25 с)"), S->Rel(S->TOtherDeath)));
			}
			(void)Wt;
		});
	};

	struct FW { int32 Wt; const TCHAR* Name; };
	const FW Ranged[] = { {11, TEXT("баллиста")}, {9, TEXT("арбалет")}, {10, TEXT("лук")} };
	for (const FW& Wp : Ranged) { AddFight(Wp.Wt, Wp.Name, EVar::LethalReply, 1, Wp.Wt == 11); }
	AddFight(11, TEXT("баллиста"), EVar::NoReply, 1, false);
	AddFight(10, TEXT("лук"), EVar::SnapNoHits, 1, false);            // после «ответа нет»: цифр очереди за 1 с до него нет
	AddFight(10, TEXT("лук"), EVar::NonLethal4017, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::NoShowThenZero, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::Repeat4013Zero, 1, false);
	AddFight(9, TEXT("арбалет"), EVar::Loop4019Zero, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::SwitchNoReply, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::SwitchLethal, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::Refused, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::NoDeathOnHit, 1, false);
	AddFight(9, TEXT("арбалет"), EVar::Hp4015First, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::Regen4017, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::Regen4013, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::CapSeries, 1, false);
	AddFight(1, TEXT("одноручный меч"), EVar::NonLethal4013Zero, 1, false);   // ★ Ф2 (ревизия Ф1 п.4)
	AddFight(1, TEXT("одноручный меч"), EVar::NonLethal4019Zero, 1, false);
	AddFight(1, TEXT("одноручный меч"), EVar::LethalReply, 1, false);
	AddFight(1, TEXT("одноручный меч"), EVar::NonLethal4017, 1, false);
	AddFight(11, TEXT("баллиста"), EVar::LethalReply, 0, false);   // контроль

	// ─────────────────────── ★ 18.10.2026 (ВАЖНО 3): цифры прошлого замаха и новый «взвод» ───────────────────────
	//   bSkill — обрыв снятием цикла навыком (`SuspendAttackLoopForSkill`), иначе выход из цикла (`StopAttackAnim`: смена цели,
	//   убийство). Key — `asda.Combat.OwnHitSwingGuard` (0 — контроль: дефект обязан быть виден).
	auto AddRearm = [S, Add, Feed, T, Reset](bool bSkill, int32 Key)
	{
		Add(FString::Printf(TEXT("меч: новый взвод после %s, OwnHitSwingGuard %d%s"), bSkill ? TEXT("навыка") : TEXT("смены цели/убийства"),
			Key, Key == 0 ? TEXT(" (контроль)") : TEXT("")), 0.f, 9.f,
		[S, Feed, T, Reset, Key]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitSwingGuard"), Key);
			S->RemPre.Reset(); S->RemPost.Reset(); S->DueAt.Reset(); S->DigitAt.Reset(); S->TInt = -1.0; S->ShownMark = 0; S->MaxShift = 0.f;
			S->RearmMaxFrameS = 0.0;
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
			if (!G || !C || !Nn) { return; }
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 1);
			C->EquippedWeaponSubType = 1;
			S->Expect(Nn->SelfWeaponItemId > 0, FString::Printf(TEXT("меч: предмет %d"), Nn->SelfWeaponItemId));
			S->Rid = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(1, 0), /*State*/1, kSpdDenom));
		},
		[S, Feed, bSkill]()
		{
			AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			UAsdaWorldSubsystem* WSs = S->WS();
			const UAsdaNetSubsystem* Nn = S->N();
			const double Now = S->Now();
			if (!WSs || !C || !PC || !Nn) { return true; }
			if (S->Phase == 0 && M && FPlatformTime::Seconds() - S->StepStartS > 0.6)
			{
				PC->SetTarget(M);
				PC->EngageMobFromClick(M);
				S->Phase = 1;
			}
			if (S->Phase == 1 && S->N26 > 0 && GFrameCounter > S->N26Frame)
			{
				// Ответ: 4 удара (значения выходят на кадрах удара — часть за перемоткой, в следующем проходе), моб жив.
				S->T0 = Now;
				Feed(4029, Body4029((int32)Nn->SelfUserNum, S->Rid, 50, { 11, 12, 13, 14 }));
				WSs->DebugOwnHitTimersRemaining(S->RemPre);
				S->Phase = 2;
			}
			if (S->Phase == 2)
			{
				// Обрыв — на половине пути до ближайшей цифры (таймеры ещё ждут).
				float MinRem = -1.f;
				for (const float R : S->RemPre) { if (R > 0.f && (MinRem < 0.f || R < MinRem)) { MinRem = R; } }
				if (MinRem < 0.f) { S->Phase = 4; return true; }
				if (Now - S->T0 >= 0.5 * (double)MinRem && Now > S->T0)
				{
					WSs->DebugOwnHitTimersRemaining(S->RemPre);   // тот же кадр, что и обрыв
					S->DueAt.Reset();
					for (const float R : S->RemPre) { if (R > 0.f) { S->DueAt.Add(Now + (double)R); } }
					S->ShownMark = WSs->HitResultsShown;
					if (bSkill) { C->SuspendAttackLoopForSkill(); } else { C->StopAttackAnim(); }
					C->StartAttackSwing(1, /*HoldSec=*/3.f);   // новый «взвод» (смена скорости отрезка → перестановка таймеров)
					WSs->DebugOwnHitTimersRemaining(S->RemPost);
					for (int32 i = 0; i < S->RemPre.Num() && i < S->RemPost.Num(); ++i)
					{
						if (S->RemPre[i] > 0.f && S->RemPost[i] > 0.f) { S->MaxShift = FMath::Max(S->MaxShift, FMath::Abs(S->RemPost[i] - S->RemPre[i])); }
					}
					S->TInt = Now;
					S->Phase = 3;
				}
			}
			if (S->Phase == 3)
			{
				// Наблюдение: показы цифр по цели после обрыва.
				S->RearmMaxFrameS = FMath::Max(S->RearmMaxFrameS, FMath::Max(0.0, FApp::GetDeltaTime() - 1.0 / 60.0));
				const int32 Shown = WSs->HitResultsShown;
				if (Shown > S->ShownMark && WSs->LastHitResultShownRid == S->Rid)
				{
					for (int32 k = S->ShownMark; k < Shown; ++k) { S->DigitAt.Add(WSs->LastHitResultShownS); }
				}
				S->ShownMark = Shown;
				double LastDue = S->TInt;
				for (const double D : S->DueAt) { LastDue = FMath::Max(LastDue, D); }
				return Now > LastDue + 0.6;   // все ждавшие моменты прошли (+ запас на опоздание при контроле)
			}
			return false;
		},
		[S, Key]()
		{
			FString Dues, Digs;
			for (const double D : S->DueAt) { Dues += FString::Printf(TEXT("%.3f "), D - S->TInt); }
			for (const double D : S->DigitAt) { Digs += FString::Printf(TEXT("%.3f "), D - S->TInt); }
			UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА замер взвода (от обрыва, с): ждали %d таймер(ов) [%s] | показы [%s] | сдвиг таймеров на взводе %.3f с"),
				S->DueAt.Num(), *Dues, *Digs, S->MaxShift);
			S->Expect(S->DueAt.Num() > 0, FString::Printf(TEXT("на обрыве ждали цифры прошлого замаха (%d таймер(ов))"), S->DueAt.Num()));
			if (Key == 0)
			{
				S->Expect(S->MaxShift > 0.02f, FString::Printf(TEXT("КОНТРОЛЬ ключ 0: дефект виден — таймеры прошлого замаха уехали на взвод нового (сдвиг %.3f с)"), S->MaxShift));
			}
			else
			{
				S->Expect(S->MaxShift <= 0.002f, FString::Printf(TEXT("взвод нового замаха не двигает таймеры цифр прошлого (сдвиг %.4f с)"), S->MaxShift));
				// Каждая ждавшая цифра вышла не позже своего момента (+ кадр опроса); раньше — тоже нет.
				bool bOnTime = S->DigitAt.Num() >= S->DueAt.Num();
				for (int32 i = 0; i < S->DueAt.Num() && i < S->DigitAt.Num(); ++i)
				{
					// ★ R5-fix7 [ИСПРАВЛЕНИЕ ТЕСТА]: таймер цифры срабатывает на первом тике игры после срока — опоздание не больше
					//   длины кадра; к прежним +0,08 с прибавлена наибольшая длина кадра сверх 1/60 с (при кадре 100 мс было +0,09).
					bOnTime &= S->DigitAt[i] >= S->DueAt[i] - 0.03 && S->DigitAt[i] <= S->DueAt[i] + 0.08 + S->RearmMaxFrameS;
				}
				S->Expect(bOnTime, FString::Printf(TEXT("цифры прошлого замаха вышли в свои моменты: ждали [%s], показы [%s] (от обрыва, с)"), *Dues, *Digs));
			}
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitSwingGuard"), 1);
		});
	};
	AddRearm(/*bSkill=*/false, 1);
	AddRearm(/*bSkill=*/true, 1);
	AddRearm(/*bSkill=*/false, 0);   // контроль

	// ─────────────────────── ★ 18.10.2026 (плавный бой): двойной старт серии + смертельный ответ без ударов ───────────────────────
	//   Живой журнал `AsdaCombat_20261018-184820` (меч, моб на 35 HP; разбор `Reports/combat_trace_2026-10-18/bugs_death_and_ledger.md`):
	//   4029 №1 hp=35 [−2 −1 9 10] и №2 hp=16 [9 9 9 9] подряд (ответы на 4026 и первый 4028), затем №3 hp=0 без ударов. Сервер
	//   применил по 2 значения за обмен: MISS, DODGE, 9, 10, 9, 9 (два последних 9 — уже нет, моб мёртв). Ждём: одно значение на кадр
	//   удара (FIFO), показано ровно −2 −1 9 10 9 9, смерть на шестом (удар, доводящий полосу до 0), удары позже него по трупу не
	//   показаны, замах снят сразу. Вариант с 4015 kind 2 HP=35 (живое поле ДО ударов) между ответами: полоса ни разу не выше 35, MaxHp
	//   не меняется. Контроли — ключи 0 (дефект обязан быть виден, проверка не слепая).
	//   ★ Ревизия 18.10: Retime — после смертельного ответа скорость отрезка своего замаха меняется (перестановка таймеров
	//   `RetimeOwnHits`): смерть обязана дождаться показа смертельного удара, он не гасится; TwoLethal — второй смертельный ответ
	//   0.1 с спустя срок смерти не отодвигает.
	enum class ESer : uint8 { Base, Raise4015, CtrlDeath, CtrlFifo, CtrlRaise, Retime, TwoLethal };   // вариант сценария серии
	auto AddSeries = [S, Add, Feed, T, Reset](ESer V)
	{
		const TCHAR* VName = V == ESer::Base ? TEXT("ключи 1")
			: V == ESer::Raise4015 ? TEXT("4015 kind 2 HP=35 между ответами, ключи 1")
			: V == ESer::CtrlDeath ? TEXT("КОНТРОЛЬ asda.Combat.DeathAtLastPendingHit 0")
			: V == ESer::CtrlFifo ? TEXT("КОНТРОЛЬ asda.Combat.OwnHitsFifo 0")
			: V == ESer::Retime ? TEXT("скорость отрезка сменилась после смертельного ответа, ключи 1")
			: V == ESer::TwoLethal ? TEXT("второй смертельный ответ 0.1 с спустя, ключи 1")
			: TEXT("4015 kind 2 HP=35 между ответами, КОНТРОЛЬ asda.Combat.LedgerRaiseBareOnly 0");
		const bool bMid = V == ESer::Raise4015 || V == ESer::CtrlRaise;   // 4015 со «старым» HP между ответами
		Add(FString::Printf(TEXT("меч: двойной старт серии + смертельный ответ без ударов (%s)"), VName), 0.f, 14.f,
		[S, Feed, T, Reset, V]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.DeathAtLastPendingHit"), V == ESer::CtrlDeath ? 0 : 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitsFifo"), V == ESer::CtrlFifo ? 0 : 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.LedgerRaiseBareOnly"), V == ESer::CtrlRaise ? 0 : 1);
			// ★ 18.10.2026: контроль подъёма — прежний путь целиком (пол HP `asda.Combat.HpFloorFromServer` подъём в бою тоже держит).
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.HpFloorFromServer"), V == ESer::CtrlRaise ? 0 : 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.MobMaxHpFixed"), 1);
			S->BarMax = -1; S->MaxHp0 = -1; S->MaxHpEnd = -1; S->TypeMaxHp = -1; S->TStance = -1.0; S->T3 = -1.0;
			S->bFed3 = S->bFed4017 = S->bFed2 = S->bFedMid = S->bFed3b = false;
			S->RemPre.Reset(); S->RemPost.Reset(); S->MaxShift = 0.f;
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N(); UAsdaWorldSubsystem* WSs = S->WS();
			if (!G || !C || !Nn || !WSs) { return; }
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 1);
			C->EquippedWeaponSubType = 1;
			S->Expect(Nn->SelfWeaponItemId > 0, FString::Printf(TEXT("меч: предмет %d"), Nn->SelfWeaponItemId));
			WSs->bRecordHitResults = true;
			S->SeenFrom = WSs->HitResultsSeen.Num();
			S->Rid = S->NewRid();
			Feed(4013, Body4013Hp(S->Rid, T(1, 0), 35));   // моб на 35 HP (как FieldWolf журнала)
		},
		[S, Feed, T, bMid, V]()
		{
			AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			UAsdaWorldSubsystem* WSs = S->WS(); const UAsdaNetSubsystem* Nn = S->N();
			if (!WSs || !C || !PC || !Nn) { return true; }
			const double Now = S->Now();
			const int32 Self = (int32)Nn->SelfUserNum;   // свой userIdx (атакующий ответов)
			if (M && S->MaxHp0 < 0) { S->MaxHp0 = M->MaxHp; S->TypeMaxHp = M->MobTypeMaxHp; }
			if (S->Phase == 0 && M && FPlatformTime::Seconds() - S->StepStartS > 0.6)
			{
				PC->SetTarget(M);
				PC->EngageMobFromClick(M);   // второй клик: бой
				S->Phase = 1;
			}
			if (S->Phase == 1 && S->N26 > 0 && GFrameCounter > S->N26Frame)
			{
				S->T0 = Now; S->Phase = 2;
				Feed(4029, Body4029(Self, S->Rid, 35, { -2, -1, 9, 10 }));   // №1 — ответ на 4026
			}
			if (S->Phase >= 2)
			{
				const double R = Now - S->T0;
				if (bMid && !S->bFedMid && R >= 0.02) { S->bFedMid = true; Feed(4015, Body4015(S->Rid, T(1, 0), 35, /*Kind=*/2)); }   // HP ДО ударов
				if (!S->bFed2 && R >= 0.05) { S->bFed2 = true; Feed(4029, Body4029(Self, S->Rid, 16, { 9, 9, 9, 9 })); }   // №2 — ответ на первый 4028
				int32 Shown = 0;   // показано значений по цели (не по трупу)
				for (int32 i = S->SeenFrom; i < WSs->HitResultsSeen.Num(); ++i)
				{
					const UAsdaWorldSubsystem::FAsdaHitResultSeen& E = WSs->HitResultsSeen[i];
					if (E.Rid == S->Rid && !E.bOnCorpse) { ++Shown; }
				}
				if (S->bFed2 && !S->bFed3 && Shown >= 4)
				{
					S->bFed3 = true; S->T3 = Now; Feed(4029, Body4029(Self, S->Rid, 0, {}));   // №3 — HP 0, очередь пуста
					if (V == ESer::Retime)
					{
						// Смена скорости отрезка своего замаха (как от серверных часов): таймеры цифр переставляются позже плана смерти.
						WSs->DebugOwnHitTimersRemaining(S->RemPre);
						C->AttackClipTempo = FMath::Max(0.1f, C->AttackClipTempo * 0.7f);
						C->OnSegmentRateChanged.Broadcast();
						WSs->DebugOwnHitTimersRemaining(S->RemPost);
						for (int32 i = 0; i < S->RemPre.Num() && i < S->RemPost.Num(); ++i)
						{
							if (S->RemPre[i] > 0.f && S->RemPost[i] > 0.f) { S->MaxShift = FMath::Max(S->MaxShift, S->RemPost[i] - S->RemPre[i]); }
						}
					}
				}
				if (V == ESer::TwoLethal && S->bFed3 && !S->bFed3b && Now - S->T3 >= 0.10) { S->bFed3b = true; Feed(4029, Body4029(Self, S->Rid, 0, {})); }   // повторный смертельный
				if (S->bFed3 && !S->bFed4017 && Now - S->T3 >= 0.15) { S->bFed4017 = true; Feed(4017, Body4017(S->Rid, T(1, 0), 0, 0)); }   // серверная смерть
				if (M)
				{
					S->BarMax = FMath::Max(S->BarMax, M->CurHp);
					S->MaxHpEnd = M->MaxHp;
					if (S->TDeath < 0.0 && M->IsDead()) { S->TDeath = M->GetDeathAtS(); }
				}
				if (S->TDeath >= 0.0 && S->TStance < 0.0 && !C->IsAttackAnimActive()) { S->TStance = Now; S->StanceLagS = FMath::Max(0.0, FApp::GetDeltaTime() - 1.0 / 60.0); }
				// Конец: смерть видна и прошло 3 с (удары прошлых ответов позже смертельного успели бы выйти), иначе потолок.
				return (S->TDeath >= 0.0 && Now - S->TDeath > 3.0) || R > 11.0;
			}
			return false;
		},
		[S, V, bMid]()
		{
			UAsdaWorldSubsystem* WSs = S->WS();
			if (!WSs) { S->Expect(false, TEXT("нет подсистемы мира")); return; }
			TArray<int32> Vals;    // показанные значения по цели (не по трупу), по порядку
			TArray<double> At;     // их моменты (мир-время)
			int32 NCorpse = 0;     // погашено ударов по трупу позже смертельного
			int32 Sum = 0;         // урон показанных
			for (int32 i = S->SeenFrom; i < WSs->HitResultsSeen.Num(); ++i)
			{
				const UAsdaWorldSubsystem::FAsdaHitResultSeen& E = WSs->HitResultsSeen[i];
				if (E.Rid != S->Rid) { continue; }
				if (E.bOnCorpse) { ++NCorpse; continue; }
				Vals.Add(E.Raw); At.Add(E.AtS);
				if (E.Raw > 0) { Sum += E.Raw & 0x7FFFFFFF; }
			}
			double MinGap = 99.0;   // наименьший интервал между соседними цифрами (две на кадре — ≈ 0)
			for (int32 i = 1; i < At.Num(); ++i) { MinGap = FMath::Min(MinGap, At[i] - At[i - 1]); }
			FString L; for (int32 i = 0; i < Vals.Num(); ++i) { L += FString::Printf(TEXT(" %d@%.3f"), Vals[i], S->Rel(At[i])); }
			const double TCross = At.Num() >= 6 ? At[5] : -1.0;   // шестое значение — удар, доводящий полосу 35 до 0
			UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА замер серии (от 4026, с): показано [%s ] | погашено по трупу %d | урон %d | мин. интервал %.3f | №3 %.3f | труп %.3f | стойка %.3f | полоса макс %d | MaxHp %d → %d (тип %d) | 4028 %d"),
				*L, NCorpse, Sum, MinGap, S->Rel(S->T3), S->Rel(S->TDeath), S->Rel(S->TStance), S->BarMax, S->MaxHp0, S->MaxHpEnd, S->TypeMaxHp, S->N28);
			const TArray<int32> Want = { -2, -1, 9, 10, 9, 9 };   // применено сервером: по 2 значения за обмен (FIFO)
			switch (V)
			{
			case ESer::Base:
			case ESer::Raise4015:
			case ESer::Retime:
			case ESer::TwoLethal:
				if (V == ESer::Retime)
				{
					S->Expect(S->MaxShift > 0.05f, FString::Printf(TEXT("перестановка после смертельного ответа сдвинула таймеры цифр позже (на %.3f с)"), S->MaxShift));
				}
				S->Expect(S->TDeath >= 0.0 && TCross >= 0.0 && S->TDeath >= TCross - 0.005,
					FString::Printf(TEXT("смерть не раньше показа смертельного удара: труп %.3f, смертельная цифра %.3f"), S->Rel(S->TDeath), S->Rel(TCross)));
				S->Expect(Vals == Want, FString::Printf(TEXT("показано ровно применённое сервером −2 −1 9 10 9 9 (по порядку, без потерь и дублей): [%s ]"), *L));
				S->Expect(MinGap >= 0.1, FString::Printf(TEXT("одно значение на кадр удара (FIFO): наименьший интервал между цифрами %.3f с"), MinGap));
				S->Expect(Sum >= 35 && Sum - 9 < 35, FString::Printf(TEXT("урон показанных %d: полосу 35 до 0 доводит последний показанный удар (дельты сервера 0/19/16)"), Sum));
				S->Expect(S->TDeath >= 0.0 && TCross >= 0.0 && S->TDeath >= TCross - kTol && S->TDeath <= TCross + kLateMax,
					FString::Printf(TEXT("смерть на ударе, доводящем полосу до 0: труп %.3f, шестая цифра %.3f (не позже + %.1f с) — без «остатка прохода»"),
						S->Rel(S->TDeath), S->Rel(TCross), kLateMax));
				S->Expect(S->TStance >= 0.0 && S->TStance - S->TDeath <= 0.1 + S->StanceLagS,
					FString::Printf(TEXT("замах снят сразу после смерти: стойка через %.3f с после трупа"), S->TStance - S->TDeath));
				S->Expect(S->BarMax <= 35 && S->MaxHpEnd == S->MaxHp0,
					FString::Printf(TEXT("полоса ни разу не выше 35 (макс %d), MaxHp не изменился (%d → %d)%s"), S->BarMax, S->MaxHp0, S->MaxHpEnd,
						bMid ? TEXT(" — 4015 kind 2 со старым HP полосу не поднял") : TEXT("")));
				break;
			case ESer::CtrlDeath:
				S->Expect(Vals.Num() > 6 || S->TDeath < 0.0 || TCross < 0.0 || S->TDeath > TCross + kLateMax,
					FString::Printf(TEXT("КОНТРОЛЬ DeathAtLastPendingHit 0: дефект виден — показано %d значений (лишние после смертельного) или труп %.3f позже шестой цифры %.3f + %.1f с"),
						Vals.Num(), S->Rel(S->TDeath), S->Rel(TCross), kLateMax));
				break;
			case ESer::CtrlFifo:
				S->Expect(MinGap < 0.05, FString::Printf(TEXT("КОНТРОЛЬ OwnHitsFifo 0: дефект виден — две цифры на одном кадре удара (наименьший интервал %.3f с)"), MinGap));
				break;
			case ESer::CtrlRaise:
				S->Expect(S->BarMax > 35, FString::Printf(TEXT("КОНТРОЛЬ LedgerRaiseBareOnly 0: дефект виден — 4015 со старым HP поднял полосу до %d (выше 35)"), S->BarMax));
				break;
			}
			WSs->bRecordHitResults = false;
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.DeathAtLastPendingHit"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitsFifo"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.LedgerRaiseBareOnly"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.HpFloorFromServer"), 1);
		});
	};
	AddSeries(ESer::Base);
	AddSeries(ESer::Raise4015);
	AddSeries(ESer::CtrlDeath);   // контроль
	AddSeries(ESer::CtrlFifo);    // контроль
	AddSeries(ESer::CtrlRaise);   // контроль
	AddSeries(ESer::Retime);      // ★ ревизия 18.10 (п.1)
	AddSeries(ESer::TwoLethal);   // ★ ревизия 18.10 (п.2)

	// ─────────────────────── ★ 18.10.2026 (пол HP, `asda.Combat.HpFloorFromServer`): ЖИВАЯ последовательность rid=161 ───────────────────────
	//   Журнал `AsdaCombat_20261018-205020_45284.log`, цикл #2 (меч, combo 6, FieldWolf 35 HP), время от 4027:
	//     0.000 4027 hp=35 [9 −2 −2 9] · 0.906 4029 hp=26 [] · 1.531 4017 flags=1 HP=33 (реген в бою) · 1.874 4029 hp=24 [−2 10 10 9] ·
	//     2.031 4017 flags=3 HP=24 · 2.750 4029 hp=14 [] · 3.718 4029 hp=0 [10 10 9 9] · 4.718 4017 flags=0 (смерть).
	//   Было в живом бою: полоса 17 → 24 (4017) и 0 → 24 (попадание ответа). Ждём: полоса монотонно не растёт весь бой (35 → … → 0),
	//   ни одного подъёма; цифры −2/9/10 — из очереди, как прежде; смерть на смертельном ударе. Контроль ключом 0 — подъём виден.
	for (const int32 Key : { 1, 0 })
	{
		Add(FString::Printf(TEXT("живой бой rid=161 (меч): полоса за весь бой не растёт, HpFloorFromServer %d%s"), Key, Key == 0 ? TEXT(" (контроль)") : TEXT("")), 0.f, 14.f,
		[S, Feed, T, Reset, Key]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.HpFloorFromServer"), Key);
			S->HpPrev = -1; S->HpRises = 0; S->HpRiseMax = 0; S->NextEv = 0; S->TStance = -1.0; S->BarMax = -1;
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N(); UAsdaWorldSubsystem* WSs = S->WS();
			if (!G || !C || !Nn || !WSs) { return; }
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 1);
			C->EquippedWeaponSubType = 1;
			S->Expect(Nn->SelfWeaponItemId > 0, FString::Printf(TEXT("меч: предмет %d"), Nn->SelfWeaponItemId));
			WSs->bRecordHitResults = true;
			S->SeenFrom = WSs->HitResultsSeen.Num();
			S->Rid = S->NewRid();
			Feed(4013, Body4013Hp(S->Rid, T(1, 0), 35));
		},
		[S, Feed, T]()
		{
			AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			UAsdaWorldSubsystem* WSs = S->WS(); const UAsdaNetSubsystem* Nn = S->N();
			if (!WSs || !C || !PC || !Nn) { return true; }
			const double Now = S->Now();
			const int32 Self = (int32)Nn->SelfUserNum;   // свой userIdx
			const FIntPoint MobTile = T(1, 0);
			if (M && S->TDeath < 0.0)
			{
				if (S->HpPrev >= 0 && M->CurHp > S->HpPrev) { ++S->HpRises; S->HpRiseMax = FMath::Max(S->HpRiseMax, M->CurHp - S->HpPrev); }
				S->HpPrev = M->CurHp; S->BarMax = FMath::Max(S->BarMax, M->CurHp);
			}
			if (S->Phase == 0 && M && FPlatformTime::Seconds() - S->StepStartS > 0.6) { PC->SetTarget(M); PC->EngageMobFromClick(M); S->Phase = 1; }
			if (S->Phase == 1 && S->N26 > 0 && GFrameCounter > S->N26Frame) { S->T0 = Now; S->Phase = 2; }
			if (S->Phase >= 2)
			{
				const double R = Now - S->T0;
				struct FEv { double At; int32 Op; TArray<uint8> Body; };   // событие расписания
				const TArray<FEv> Evs = {
					{ 0.000, 4027, Body4027(Self, S->Rid, MobTile, 35, { 9, -2, -2, 9 }) },
					{ 0.906, 4029, Body4029(Self, S->Rid, 26, {}) },
					{ 1.531, 4017, Body4017(S->Rid, MobTile, 1, 33) },
					{ 1.874, 4029, Body4029(Self, S->Rid, 24, { -2, 10, 10, 9 }) },
					{ 2.031, 4017, Body4017(S->Rid, MobTile, 3, 24) },
					{ 2.750, 4029, Body4029(Self, S->Rid, 14, {}) },
					{ 3.718, 4029, Body4029(Self, S->Rid, 0, { 10, 10, 9, 9 }) },
					{ 4.718, 4017, Body4017(S->Rid, MobTile, 0, 0) },
				};
				while (S->NextEv < Evs.Num() && R >= Evs[S->NextEv].At) { Feed(Evs[S->NextEv].Op, Evs[S->NextEv].Body); ++S->NextEv; }
				if (M && S->TDeath < 0.0 && M->IsDead()) { S->TDeath = M->GetDeathAtS(); }
				if (S->TDeath >= 0.0 && S->TStance < 0.0 && !C->IsAttackAnimActive()) { S->TStance = Now; S->StanceLagS = FMath::Max(0.0, FApp::GetDeltaTime() - 1.0 / 60.0); }
				return (S->TDeath >= 0.0 && Now - S->TDeath > 2.5) || R > 11.0;
			}
			return false;
		},
		[S, Key]()
		{
			UAsdaWorldSubsystem* WSs = S->WS();
			if (!WSs) { S->Expect(false, TEXT("нет подсистемы мира")); return; }
			TArray<int32> Vals; TArray<double> At;   // показанные значения (не по трупу) и моменты
			for (int32 i = S->SeenFrom; i < WSs->HitResultsSeen.Num(); ++i)
			{
				const UAsdaWorldSubsystem::FAsdaHitResultSeen& E = WSs->HitResultsSeen[i];
				if (E.Rid == S->Rid && !E.bOnCorpse) { Vals.Add(E.Raw); At.Add(E.AtS); }
			}
			FString L; for (int32 i = 0; i < Vals.Num(); ++i) { L += FString::Printf(TEXT(" %d@%.3f"), Vals[i], S->Rel(At[i])); }
			const double TLast = At.Num() > 0 ? At.Last() : -1.0;   // последний показанный удар (смертельный)
			UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА замер живого боя rid=161 (от 4026, с): показано [%s ] | подъёмов полосы %d (наибольший %d) | полоса макс %d | труп %.3f | стойка %.3f"),
				*L, S->HpRises, S->HpRiseMax, S->BarMax, S->Rel(S->TDeath), S->Rel(S->TStance));
			if (Key == 0)
			{
				S->Expect(S->HpRises > 0, FString::Printf(TEXT("КОНТРОЛЬ HpFloorFromServer 0: дефект виден — полоса поднималась %d раз (наибольший подъём %d)"), S->HpRises, S->HpRiseMax));
			}
			else
			{
				S->Expect(S->HpRises == 0 && S->BarMax <= 35, FString::Printf(TEXT("полоса за весь бой монотонно не растёт: подъёмов %d (наибольший %d), максимум %d"), S->HpRises, S->HpRiseMax, S->BarMax));
				// Порядок очереди (FIFO) без потерь и дублей — префикс полной очереди; где он кончается, решает правило смерти: удар, на
				//   котором полоса дошла до 0 (последний «9» прошлого пакета может сработать до или после смертельного 4029 — оба верны).
				const TArray<int32> Full = { 9, -2, -2, 9, -2, 10, 10, 9, 10, 10, 9, 9 };
				bool bPrefix = Vals.Num() >= 9 && Vals.Num() <= Full.Num();
				for (int32 i = 0; bPrefix && i < Vals.Num(); ++i) { bPrefix = Vals[i] == Full[i]; }
				S->Expect(bPrefix, FString::Printf(TEXT("цифры — очередь по порядку (FIFO), без потерь и дублей, по трупу ничего: [%s ]"), *L));
				S->Expect(S->HpPrev == 0, FString::Printf(TEXT("полоса дошла до 0 ровно на последнем показанном ударе (полоса при смерти %d)"), S->HpPrev));
				S->Expect(S->TDeath >= 0.0 && TLast >= 0.0 && S->TDeath >= TLast - 0.005 && S->TDeath <= TLast + 0.25,
					FString::Printf(TEXT("смерть на смертельном ударе: труп %.3f, последняя цифра %.3f"), S->Rel(S->TDeath), S->Rel(TLast)));
				S->Expect(S->TStance >= 0.0 && S->TStance - S->TDeath <= 0.1 + S->StanceLagS, FString::Printf(TEXT("замах снят сразу после смерти (через %.3f с)"), S->TStance - S->TDeath));
			}
			WSs->bRecordHitResults = false;
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.HpFloorFromServer"), 1);
		});
	}

	// ─────────────────────── ★ Ревизия 18.10 (п.4): дальний бой — смерть на прилёте смертельного, выстрелов после стойки нет ───────────────────────
	//   Лук, моб на 20 HP: 4029 №1 hp=20 [9 11] (дальнобойное — одно значение за ответ: 9 на кадре 1, 11 — в очереди), сразу №2 hp=0
	//   без новых ударов (из очереди — 11 на следующем кадре, FIFO) + 4015 HP=0 (0.1 с) и 4017 flags=0 (1.0 с). Ждём: показано 9 и 11,
	//   труп — на прилёте второго снаряда (не раньше, не позже + 0.25 с), замах снят сразу, выстрелов после смерти нет.
	Add(TEXT("лук: смертельный ответ без новых ударов — смерть на прилёте, выстрелов после стойки нет"), 0.f, 12.f, [S, Feed, T, Reset]()
	{
		Reset();
		S->TStance = -1.0; S->bFed2 = S->bFed3 = S->bFed4017 = false; S->ShotAt.Reset();
		UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N(); UAsdaWorldSubsystem* WSs = S->WS();
		if (!G || !C || !Nn || !WSs) { return; }
		Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 10);
		C->EquippedWeaponSubType = 10;
		S->Expect(Nn->SelfWeaponItemId > 0, FString::Printf(TEXT("лук: предмет %d"), Nn->SelfWeaponItemId));
		WSs->bRecordHitResults = true;
		S->SeenFrom = WSs->HitResultsSeen.Num();
		S->ShownMark = WSs->ShotsSeen.Num();   // начало наблюдений выстрелов
		S->Rid = S->NewRid();
		Feed(4013, Body4013Hp(S->Rid, T(3, 0), 20));
	},
	[S, Feed, T]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		UAsdaWorldSubsystem* WSs = S->WS(); const UAsdaNetSubsystem* Nn = S->N();
		if (!WSs || !C || !PC || !Nn) { return true; }
		const double Now = S->Now();
		const int32 Self = (int32)Nn->SelfUserNum;   // свой userIdx
		if (S->Phase == 0 && M && FPlatformTime::Seconds() - S->StepStartS > 0.6) { PC->SetTarget(M); PC->EngageMobFromClick(M); S->Phase = 1; }
		if (S->Phase == 1 && S->N26 > 0 && GFrameCounter > S->N26Frame) { S->T0 = Now; S->Phase = 2; Feed(4029, Body4029(Self, S->Rid, 20, { 9, 11 })); }
		if (S->Phase >= 2)
		{
			const double R = Now - S->T0;
			if (!S->bFed2 && R >= 0.05) { S->bFed2 = true; Feed(4029, Body4029(Self, S->Rid, 0, {})); }
			if (!S->bFed3 && R >= 0.10) { S->bFed3 = true; Feed(4015, Body4015(S->Rid, T(3, 0), 0)); }
			if (!S->bFed4017 && R >= 1.0) { S->bFed4017 = true; Feed(4017, Body4017(S->Rid, T(3, 0), 0, 0)); }
			if (M && S->TDeath < 0.0 && M->IsDead()) { S->TDeath = M->GetDeathAtS(); }
			if (S->TDeath >= 0.0 && S->TStance < 0.0 && !C->IsAttackAnimActive()) { S->TStance = Now; S->StanceLagS = FMath::Max(0.0, FApp::GetDeltaTime() - 1.0 / 60.0); }
			return (S->TDeath >= 0.0 && Now - S->TDeath > 3.0) || R > 10.0;
		}
		return false;
	},
	[S]()
	{
		UAsdaWorldSubsystem* WSs = S->WS();
		if (!WSs) { S->Expect(false, TEXT("нет подсистемы мира")); return; }
		TArray<int32> Vals; TArray<double> At;   // показанные значения и моменты
		for (int32 i = S->SeenFrom; i < WSs->HitResultsSeen.Num(); ++i)
		{
			const UAsdaWorldSubsystem::FAsdaHitResultSeen& E = WSs->HitResultsSeen[i];
			if (E.Rid == S->Rid && !E.bOnCorpse) { Vals.Add(E.Raw); At.Add(E.AtS); }
		}
		int32 NShots = 0, NShotsAfter = 0;   // выстрелы по цели, из них после смерти
		FString Sh;
		for (int32 i = S->ShownMark; i < WSs->ShotsSeen.Num(); ++i)
		{
			const UAsdaWorldSubsystem::FAsdaShotSeen& E = WSs->ShotsSeen[i];
			if (E.Rid != S->Rid) { continue; }
			++NShots; Sh += FString::Printf(TEXT(" %d@%.3f"), E.Raw, S->Rel(E.AtS));
			if (S->TDeath >= 0.0 && E.AtS > S->TDeath + 0.001) { ++NShotsAfter; }
		}
		FString L; for (int32 i = 0; i < Vals.Num(); ++i) { L += FString::Printf(TEXT(" %d@%.3f"), Vals[i], S->Rel(At[i])); }
		const double TLethal = At.Num() >= 2 ? At[1] : -1.0;   // прилёт второго снаряда — удар, доводящий полосу 20 до 0
		UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА замер лука (от 4026, с): показано [%s ] | выстрелы [%s ] | труп %.3f | стойка %.3f"),
			*L, *Sh, S->Rel(S->TDeath), S->Rel(S->TStance));
		const TArray<int32> Want = { 9, 11 };
		S->Expect(Vals == Want, FString::Printf(TEXT("показано 9 и 11 по порядку (по одному на кадр выстрела): [%s ]"), *L));
		S->Expect(S->TDeath >= 0.0 && TLethal >= 0.0 && S->TDeath >= TLethal - 0.005 && S->TDeath <= TLethal + 0.25,
			FString::Printf(TEXT("смерть на прилёте смертельного снаряда (с полётом): труп %.3f, прилёт %.3f"), S->Rel(S->TDeath), S->Rel(TLethal)));
		S->Expect(S->TStance >= 0.0 && S->TStance - S->TDeath <= 0.1 + S->StanceLagS, FString::Printf(TEXT("замах снят сразу после смерти (через %.3f с)"), S->TStance - S->TDeath));
		S->Expect(NShots == 2 && NShotsAfter == 0, FString::Printf(TEXT("выстрелов по цели 2 (%d), после смерти — ни одного (%d)"), NShots, NShotsAfter));
		WSs->bRecordHitResults = false;
	});

	// ─────────────────────── ★ 18.10.2026: HP > MaxHp — боевой канал максимум не растягивает, канал спауна — да ───────────────────────
	//   [ОРИГИНАЛ] max — только из таблицы типа (`sub_932483`), при cur > max полоса не растягивается (`ui_core.c:3828`). Боевой 4017
	//   flags=3 с HP на 20 выше максимума типа: при `asda.Combat.MobMaxHpFixed 1` max прежний, HP хранится как есть (полоса — 100 %,
	//   кламп доли в отрисовке), в журнале строка «АНОМАЛИЯ: HP > MaxHp»; контроль ключом 0 — max растянут. ★ Ревизия 18.10 (п.5):
	//   DT_MobStats синхронизирован с сервером, поэтому канал спауна/респауна (4017 flags=4) HP выше таблицы считает серверным
	//   максимумом — max поднимается (страховка для типов без синхронизации).
	for (const int32 Key : { 1, 0 })
	{
		Add(FString::Printf(TEXT("HP > MaxHp: боевой 4017 flags=3, затем реген 4017 flags=4 выше максимума типа, MobMaxHpFixed %d%s"), Key, Key == 0 ? TEXT(" (контроль)") : TEXT("")), 0.f, 4.f,
		[S, Reset, Feed, T, Key]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.MobMaxHpFixed"), Key);
			S->MaxHp0 = -1; S->MaxHpEnd = -1; S->TypeMaxHp = -1; S->BarMax = -1;
			S->Rid = S->NewRid();
			Feed(4013, Body4013Hp(S->Rid, T(-2, 2), 35));
		},
		[S, Feed, T]()
		{
			AAsdaMobActor* M = S->Mob(S->Rid);
			const double El = FPlatformTime::Seconds() - S->StepStartS;
			if (S->Phase == 0 && M && El > 0.3)
			{
				S->MaxHp0 = M->MaxHp; S->TypeMaxHp = M->MobTypeMaxHp;
				Feed(4017, Body4017(S->Rid, T(-2, 2), 3, M->MaxHp + 20));   // боевой канал выше максимума
				S->Phase = 1;
			}
			else if (S->Phase == 1 && M)
			{
				S->BarMax = M->MaxHp; S->FinalHp = M->CurHp;            // после боевого канала: max и HP
				Feed(4017, Body4017(S->Rid, T(-2, 2), 4, S->MaxHp0 + 30));   // канал спауна/респауна выше максимума
				S->Phase = 2;
			}
			else if (S->Phase == 2 && M) { S->MaxHpEnd = M->MaxHp; return true; }
			return false;
		},
		[S, Key]()
		{
			UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА замер HP > MaxHp: max после спауна %d (тип %d) | боевой 4017 HP %d → HP %d / max %d | реген 4017 flags=4 HP %d → max %d"),
				S->MaxHp0, S->TypeMaxHp, S->MaxHp0 + 20, S->FinalHp, S->BarMax, S->MaxHp0 + 30, S->MaxHpEnd);
			S->Expect(S->TypeMaxHp > 0, FString::Printf(TEXT("у типа моба %d есть MaxHp таблицы (%d) — правило проверяемо"), kMobType, S->TypeMaxHp));
			if (Key == 1)
			{
				S->Expect(S->BarMax == S->MaxHp0 && S->FinalHp == S->MaxHp0 + 20,
					FString::Printf(TEXT("боевой канал HP > MaxHp: максимум не растянут (%d → %d), HP хранится как есть (%d), полоса — 100 %%"), S->MaxHp0, S->BarMax, S->FinalHp));
			}
			else
			{
				S->Expect(S->BarMax == S->MaxHp0 + 20, FString::Printf(TEXT("КОНТРОЛЬ MobMaxHpFixed 0: дефект виден — боевой пакет растянул максимум (%d → %d)"), S->MaxHp0, S->BarMax));
			}
			S->Expect(S->MaxHpEnd == S->MaxHp0 + 30, FString::Printf(TEXT("канал спауна/респауна (4017 flags=4) HP выше таблицы — серверный максимум: max %d (ждём %d)"), S->MaxHpEnd, S->MaxHp0 + 30));
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.MobMaxHpFixed"), 1);
		});
	}

	// ─────────────────────── Обратная: подъём HP (реген) без нашего замаха — сразу ───────────────────────
	Add(TEXT("обратная: реген моба без замаха"), 0.f, 4.f, [S, Reset, Feed, T]()
	{
		Reset();
		S->Rid = S->NewRid();
		Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(-2, -2), /*State*/1, kSpdDenom));
	}, [S, Feed, T]()
	{
		AAsdaMobActor* M = S->Mob(S->Rid);
		const double El = FPlatformTime::Seconds() - S->StepStartS;
		if (S->Phase == 0 && M && El > 0.4) { Feed(4015, Body4015(S->Rid, T(-2, -2), 30)); S->Phase = 1; S->FinalHp = M->CurHp; }
		else if (S->Phase == 1 && M) { S->THpDrop = M->CurHp; Feed(4017, Body4017(S->Rid, T(-2, -2), 3, 80)); S->Phase = 2; }
		else if (S->Phase == 2 && M) { S->FinalHp = M->CurHp; return true; }
		return false;
	}, [S]()
	{
		S->Expect((int32)S->THpDrop == 30 && S->FinalHp == 80,
			FString::Printf(TEXT("без замаха: 4015 HP=30 применён сразу (%d), 4017 HP=80 поднял полосу сразу (%d)"), (int32)S->THpDrop, S->FinalHp));
	});

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 0.f, 1.f, [S, Reset, Net]()
	{
		Reset();
		// ★ 01.11.2026 (изоляция пакета): серверные часы атаки — в чистое состояние. Сценарии шлют свои 4026/4028 и кончаются ходом
		//   или смертью цели, и серия остаётся «брошенной» (`MoveAbandonGen == SeriesGen`). Следующий самотест, чья своя роль новую
		//   серию не открывает (`asda.Combat.SelfTestObserverSync`, роль атакующего Ф1), видел бы свои значения «фантомом» и не
		//   показывал их (244/12 в пакете против 256/0 отдельно). `DebugReset` открывает новую пустую серию.
		if (UAsdaAttackRhythmSubsystem* Rh = Net->GetGameInstance() ? Net->GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->DebugReset(); }
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		if (UWorld* Wsp = S->World.Get()) { Wsp->RemoveOnActorSpawnedHandler(S->ProjSpawnHandle); }   // ★ R5-fix6: подписка на рождение снаряда снята
		Net->bInGameConnected = S->bWasInGame;
		Net->SelfWeaponItemId = S->SavedWeapon;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), TEXT("asda.Combat.SelfTestHitFrame"), false, LogAsdaHitFrameTest, TEXT("САМОТЕСТ-КАДРУДАРА"),
			S->Ok, S->Fail, S->bControlAll ? TEXT(" (КОНТРОЛЬНЫЙ прогон с ключом HitFrameAllChannels 0: провалы ожидаемы — дефект виден)") : TEXT(""));
	}, []() { return true; }, []() {});

	// ─────────────────────── Спавн и исполнитель ───────────────────────
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaHitFrameTest, Log, TEXT("САМОТЕСТ-КАДРУДАРА шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = El >= Stp.MinSec && (!Stp.Poll || Stp.Poll());
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCombatHitFrame(
	TEXT("asda.Combat.SelfTestHitFrame"),
	TEXT("Самотест «HP и смерть цели не раньше попадания нашего замаха»: дальнобойное (баллиста/арбалет/лук) и меч; смертельный "
	     "ответ + 4015 HP=0 + 4017 flags=0 до выстрела, ответа нет, не смертельный с HP из 4017/4015, ответ «−2», снимок без ударов, "
	     "4013/4019 HP=0, смена цели, серия 4028 (потолок), отказ 4029, DeathOnHitFrame 0, 4015 раньше ответа, реген при отложенной "
	     "смерти; меч: двойной старт серии + смертельный ответ без ударов (FIFO, смерть на ударе до 0, 4015 со старым HP), HP > MaxHp; "
	     "обратные — чужой моб и реген сразу. Контрольная пара asda.Combat.HitFrameAllChannels 0 (ключ 0 на старте — "
	     "контрольный прогон). Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Combat.SelfTestHitFrame"));
		FAsdaCombatHitFrameSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
