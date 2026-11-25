// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «АНИМАЦИИ И ЭФФЕКТЫ НАВЫКОВ ПО ВСЕМ ВИДАМ ОРУЖИЯ И ПОЛАМ» (01.11.2026, жалоба игроков «у навыков двуручного мечника
//   не хватает анимаций»): `asda.Skill.SelfTestAnimAudit [w=3,1] [g=m|f|mf] [skills=507,508] [self] [fight] [approach0] [exit400] [shot=<папка>] [shotw=3] [quit]`. [ПОРТ]
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока (закон diagnostic-must-use-real-path): кастер — СОСЕД, появившийся пакетом 4005
//   с настоящим оружием нужного подтипа и полом (сборка внешности → `SetWeaponFromItem` → `EquippedWeaponSubType`), навык —
//   пакетом 5257 сервера (тот же приём, что у живого соседа: `PlaySkillVisual` из разбора ответа), цель — моб 4013 или сам кастер
//   (навыки «на себя», targetSlotType 1).
//   Список навыков на подтип оружия — по данным клиента: строки DT_SkillClips есть, навык активный (`.sif` +0x2C ∈ {1,2,5}),
//   маска оружия `.sif` +0x83 содержит бит подтипа (бит = подтип), либо маски нет и дерево (+0xBD) — полосы этого оружия
//   (воин 1..3/10, лучник 4..6/11, маг 7..9/12; навыки Хранителя 1228..1254 — по своему дереву).
//   На каждый навык меряется ПО КАДРАМ, до конца клипа:
//     • показ стартовал (`IsSkillVisualActive`);
//     • на скелете кастера стоит ТОТ клип, который выбирает таблица эталона (`ResolveRow` → anim_NN нужного пола);
//     • клип не перебит раньше своего конца (что встало вместо — пишем);
//     • поза ДВИЖЕТСЯ: среднее смещение костей (компонентное пространство) от первого кадра > порога — клип не пустой и не T-поза;
//     • родились эффект-акторы (новые `AAsdaEffectActor` за время показа) против числа эффектов в строке таблицы.
//   `self` — кастер СВОЙ персонаж (свой пол, оружие надевается общей точкой ответа 5001 `ApplySelfEquipPart`, 5257 с кастером = свой
//   номер): проверка, что у своего клип навыка не перебивают свои системы (ход, замах, сторожа состояния).
//   `fight` (вместе с `self`) — НАСТОЯЩИЙ поток боя: свой бьёт моба автоатакой (клик `EngageMobFromClick` → 4026 → ответ 4029), посреди
//   замаха навык нажимается как с панели (`DispatchByKind` → 5256 уходит в перехват → ответ 5257) — проверка, что выход из атаки
//   (4024), сторожа состояния (LatchGuard) и цикл замаха не обрывают клип навыка. Нет 5256 — «ЗАЯВКА_НЕ_УШЛА» (это гейт, не анимация).
//   Кадры (`shot=`, подтипы `shotw=`): на 0.12 с, на кадре удара COMBO0 и на 75 % клипа — `w<подтип>_<пол>_<навык>_<фаза>.png`.
//   Строка на навык: «АНИМ-АУДИТ <подтип> <пол> <навык> …», итог: «АНИМ-АУДИТ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "AsdaTestPackets.h"                   // запись 4013 (спаун моба-цели)
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "Skill/AsdaSkillClipRow.h"
#include "UI/AsdaEffectsSubsystem.h"            // ★ 10.11.2026 `states=`: строка DT_BuffFx состояния (визуал третьего канала)
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaSkillUseSubsystem.h"          // DispatchByKind — вход навыка игрока (вариант fight)
#include "World/AsdaPlayerController.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Test/AsdaSelfTestKit.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAnimAudit, Log, All);

namespace AsdaSkillAnimAuditTest
{
	static const TCHAR* kCmd = TEXT("asda.Skill.SelfTestAnimAudit");   // имя команды
	constexpr int32 kMe = 4321;          // свой userNum (4003)
	constexpr int32 kPeerBase = 62;      // userIdx соседей-кастеров: 62, 63, … (по одному на пару «оружие × пол»)
	constexpr int32 kMobRid = 30991;     // моб-цель навыков «по цели»
	constexpr int32 kMobType = 1;        // волк alpen (как у прочих самотестов)
	constexpr int32 kVictim = 77;        // `tgtchar`: userIdx соседа-ЦЕЛИ (персонаж вместо моба — путь PvP/дуэли)
	constexpr int32 kVictim2 = 78;       // ★ 10.11.2026 `tgtchar2`: userIdx ВТОРОЙ цели-игрока (доп. запись ответа 5257 — площадь по игрокам)
	constexpr float kMoveMin = 2.0f;     // порог «поза движется»: среднее смещение костей, юн. UE
	constexpr float kTailSec = 0.35f;    // запас после конца клипа (доиграть, погасить)

	// Пара «подтип оружия × пол», на которой гоняется список навыков.
	struct FCase { int32 W = 0; uint8 Sex = 1; int32 Item = 0; int32 Peer = 0; bool bSelf = false; TArray<int32> Skills; };

	// Шаг сценария: true — шаг завершён. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Замер одного показа.
	struct FRun
	{
		int32 Skill = 0;                                     // код навыка
		bool bCast = false;                                  // 5257 уже подан
		bool bStarted = false;                               // показ стартовал
		FString Expect;                                      // пакет ассета клипа по таблице (anim_NN пола)
		FString ExpectClip;                                  // имя клипа эталона
		float LenS = 0.f;                                    // реальная длительность показа (клип / темп), с
		float Combo0S = 0.f;                                 // кадр удара COMBO0, реальные с
		bool bAssetOk = false;                               // на 0.05..0.2 с стоял ожидаемый клип
		FString FirstAsset;                                  // что стояло на первом замере
		float OverT = -1.f;                                  // момент перебивания (−1 — не перебит)
		FString OverBy;                                      // чем перебит
		TArray<FVector> Pose0;                               // поза первого замера (компонентное пространство)
		float MaxMove = 0.f;                                 // максимум среднего смещения костей
		TSet<TWeakObjectPtr<AActor>> Before;                 // эффект-акторы до каста
		TSet<TWeakObjectPtr<AActor>> NewFx;                  // родившиеся за показ
		int32 ExpectFx = 0;                                  // эффектов в строке таблицы (каст + удары)
		int32 ShotMask = 0;                                  // снятые кадры (биты фаз)
		TMap<TWeakObjectPtr<AActor>, float> FxNearTarget;   // эффекты, рождённые у цели (≤ 150 юн), → наибольшее расстояние до неё за показ
		TMap<TWeakObjectPtr<AActor>, float> FxNearTarget2;  // ★ 10.11.2026 `tgtchar2`: то же у второй цели-игрока
		int32 NearTargetAfterCombo0 = 0;
		TArray<TPair<int32, bool>> MainHits;                // ★ 10.11.2026 (ревизия 5): выпуски урона по ОСНОВНОЙ цели — (урон, «MISS») по порядку
		bool bLateSent = false;                             // ★ 10.11.2026 `late`: записи (5270) уже поданы после порога
		double LateSentAtS = 0.0;                           // ★ 10.11.2026 `late`: время мира подачи 5270                    // ★ 10.11.2026 `miss1`: эффектов у цели, рождённых с порога COMBO0 (эффекты каста у цели — раньше)
		TArray<FString> FollowJebs;                         // ★ 10.11.2026: `.jeb` строки с привязкой «follow:target» (BP_Fx_<имя>) — их отрыв проверяется
		double CastAtS = 0.0;                               // ★ 10.11.2026: время мира подачи 5257 (отсчёт задержки отложенных записей)
		float MaxFrameDt = 0.f;                             // ★ 10.11.2026: самый длинный кадр показа (запись выпускается первым кадром после срока)
		TArray<FString> Extra2Jebs;                         // ★ 10.11.2026 (ревизия 3): `.jeb` видов xchar/rchar — у второй цели-игрока при попадании есть, при промахе нет
		uint8 Sex = 1;                                       // пол кастера на замере
		int32 FightPhase = 0;                                // fight: 0 клик, 1 ждём 4026, 2 замах, 3 ждём 5256, 4 замер
		double PhaseT0 = 0.0;                                // fight: начало фазы (время мира)
		bool bNoRequest = false;                             // fight: 5256 так и не ушёл
		bool bGuardianReq = false;                           // fight: заявка ушла опкодом Хранителя 6158 — ответ 6159
		bool bPartyRefused = false;                          // fight: групповой навык вне отряда — отказ нотисом 1223 (как эталон)
		bool bDash = false;                                  // рывок (`CPlayerSkill_SetupSkill046_Rush`): эталон обрывает клип на COMBO0 (фаза 3)
		bool bPlayerOnly = false;                            // fight: навык «только на игрока» (castType 2) — по мобу заявки нет и у эталона
		int32 ExitWay = -1;                                  // exit400: способ выхода во время подхода (−1 — не проверяли)
		double ExitT0 = 0.0;                                 // exit400: момент выхода (время мира)
	};

	// Состояние прогона.
	struct FState : public TSharedFromThis<FState>
	{
		TWeakObjectPtr<UWorld> W;                     // мир
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров (пусто — без кадров)
		TSet<int32> ShotW;                            // подтипы оружия, у которых снимать кадры
		TArray<int32> Weapons;                        // подтипы оружия прогона
		TArray<uint8> Sexes;                          // полы прогона (1 м, 2 ж)
		TSet<int32> OnlySkills;                       // ограничение списка навыков (пусто — все подходящие)
		TArray<int32> States;                         // ★ 10.11.2026 `states=301,302`: состояния, визуал которых проверяется на соседе пакетом 5272
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага (время мира)
		FDelegateHandle H;                            // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи — вернуть после прогона (`approach0` — контроль прежнего порта)
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TWeakObjectPtr<UAsdaSkillFxSubsystem> Fx;     // визуал навыков
		TWeakObjectPtr<UAsdaSkillDatabase> Db;        // данные навыков (.sif)
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items;// таблицы предметов
		TArray<int32> Peers;                          // заведённые соседи (для ухода 4011)
		FRun Cur;                                     // текущий замер
		TMap<FString, int32> Groups;                  // сводка по причинам провала
		bool bAbort = false;                          // прервать сценарий (нет данных)
		bool bSelf = false;                           // `self` — кастер свой персонаж
		int32 OrigWeapon = -1;                        // своё оружие до прогона (−1 — не трогали)
		bool bFight = false;                          // `fight` — навык посреди автоатаки, путь панели
		int32 MapId = 3;                              // `map=N` — карта данных (Asda.Load.CurrentMap), по умолчанию alpen
		FIntPoint HomeTile = FIntPoint(256, 256);     // `tile=X,Y` — клетка своего персонажа (4003)
		bool bMobMove = false;                        // `mobmove` — перед каждым навыком моб-цель заново идёт поперёк кадра (4013 с целью хода)
		bool bTgtChar = false;                        // `tgtchar` — цель навыка персонаж-сосед (5257 вид 1), а не моб
		bool bTgtChar2 = false;                       // ★ 10.11.2026 `tgtchar2` (вместе с tgtchar): вторая цель-игрок второй записью 5257
		bool bMiss2 = false;                          // ★ 10.11.2026 `miss2`: у второй записи флаг попадания 0 (промах)
		int32 Dmg = 1;                                // ★ 10.11.2026 `dmg=N`: урон записей 5257 (поле @3) — деление волн 911, «MISS» нулевых долей
		bool bLate = false;                           // ★ 10.11.2026 `late`: 5257 без записей (запуск показа), записи — 5270 после порога COMBO0 + 0.15 с
		FDelegateHandle HitH;                         // ★ 10.11.2026: подписка на выпуск урона (OnSkillHitMoment)
		TArray<uint8> LateBody;                       // ★ 10.11.2026 `late`: тело 5257 с записями — записи уйдут 5270 после порога
		bool bMiss1 = false;                          // ★ 10.11.2026 `miss1`: у ОСНОВНОЙ записи флаг попадания 0 — у цели ни одного эффекта (тики на помощниках удара)
		int32 MobType = kMobType;                     // `mob=N` — тип моба-цели (234 — класс размера 2, 220 %: подъём attach-13 +600, масштаб ×2.2)
		bool bExit400 = false;                        // `exit400` — во время подхода ради навыка выйти из боя и проверить, что 4024/400 НЕ ушёл
		int32 Exit400N = 0;                           // exit400: номер очередного способа выхода (снятие цели / клик по земле / цель исчезла)
		uint32 CapGen = 0;                            // fight: поколение перехвата исходящих
		TArray<uint16> Outs;                          // fight: перехваченные исходящие опкоды
		TArray<TPair<uint16, TArray<uint8>>> Replies; // fight: ответы «сервера» на ход (4007/4009) — подаются в тике, не из перехвата
		bool bWasInGame = false, bWasStandSkill = false; // fight: флаги сети до прогона

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaAnimAudit, Log,   TEXT("АНИМ-АУДИТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaAnimAudit, Error, TEXT("АНИМ-АУДИТ FAIL %s"), *What); }
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* N = Net.Get()) { N->DebugFeedPacket(Op, B); } }
	};

	// Полоса по подтипу оружия: 1 воин, 2 лучник, 3 маг (0 — вне полос).
	static int32 BandOfWeapon(int32 W)
	{
		if (W >= 1 && W <= 3) { return 1; }
		if (W >= 9 && W <= 11) { return 2; }
		if (W == 5) { return 3; }
		return 0;
	}
	// Полоса по дереву навыка (.sif +0xBD): 1..3/10 воин, 4..6/11 лучник, 7..9/12 маг.
	static int32 BandOfGrade(int32 G)
	{
		if ((G >= 1 && G <= 3) || G == 10) { return 1; }
		if ((G >= 4 && G <= 6) || G == 11) { return 2; }
		if ((G >= 7 && G <= 9) || G == 12) { return 3; }
		return 0;
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	static TArray<uint8> Body4003(int32 Me, const FIntPoint& Tile = FIntPoint(256, 256))
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Me);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Tile.X);
		AsdaBytes::WrI16(B.GetData() + 4, (int16)Tile.Y);
		B[8] = 1;
		return B;
	}
	// Тело 4005 (151 б): сосед с полом `Sex`, оружие в слоте 9 (код i32 + заточка u8), позиция game XZ.
	static TArray<uint8> Body4005(int32 Uidx, uint8 Sex, int32 WeaponId, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;                                             // шлем показан
		AsdaBytes::WrI32(B.GetData() + 11, 970000 + Uidx);    // charId — заведомо не свой
		const char* Name = "AnimAudit";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = Sex; B[38] = 1; B[40] = 60;                   // пол, класс, уровень
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 9, WeaponId); // слот 9 — оружие
		return B;
	}
	// 5257 (шапка 8 б + 29 б записи): [0] результат, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень; запись — вид@0
	//   (0 = моб, 1 = персонаж), idx@1, урон@3, флаги@7, HP после@11, −1@19 (раскладка — AsdaSkillDmgTextSelfTest.cpp).
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, uint8 Kind, int32 Idx)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		uint8* R = B.GetData() + 8;
		R[0] = Kind; AsdaBytes::WrI16(R + 1, Idx); AsdaBytes::WrI32(R + 3, 1); R[7] = 0x01;
		AsdaBytes::WrI32(R + 11, 900); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}
	// ★ 10.11.2026 `tgtchar2`: две записи — основная цель-игрок и вторая цель-игрок с флагом попадания `Flag2` (0 — промах).
	static TArray<uint8> Body5257Two(int32 Caster, int32 Skill, int32 Idx1, int32 Idx2, uint8 Flag2)
	{
		TArray<uint8> B = Body5257(Caster, Skill, 1, Idx1);
		// шапка та же (уровень @6); число записей приёмник берёт из размера тела ((размер − 8) / 29), см. AsdaNetSubsystem.cpp 5257
		B.AddZeroed(29);
		uint8* R = B.GetData() + 8 + 29;
		R[0] = 1; AsdaBytes::WrI16(R + 1, Idx2); AsdaBytes::WrI32(R + 3, 1); R[7] = Flag2;   // как основная запись; отличается только флаг попадания @7
		AsdaBytes::WrI32(R + 11, 900); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}
	static TArray<uint8> Body4011(int32 Idx) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), (int16)Idx); return B; }
	// 4029 (49 б): атакующий, rid, HP после, очередь i32[4], sub@24, i16[4] −1 (раскладка — AsdaCombatHitFrameSelfTest.cpp).
	static TArray<uint8> Body4029(int32 AtkU, int32 Rid, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)AtkU); AsdaBytes::WrI16(&B[2], (int16)Rid); AsdaBytes::WrI32(&B[4], Hp);
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], 3); AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}
	// Ответ «сервера» на свой ход: 4006 → 4007 (result 1, в свою клетку 0), 4008 → 4009; тело 27 б — раскладка `Recv_EntityMove@0xF3C2C0`.
	static TArray<uint8> MoveReply(uint16 Op, const uint8* T)
	{
		float Cx, Cz, Dx, Dz; FMemory::Memcpy(&Cx, T + 5, 4); FMemory::Memcpy(&Cz, T + 9, 4); FMemory::Memcpy(&Dx, T + 13, 4); FMemory::Memcpy(&Dz, T + 17, 4);
		const bool bSameCell = AsdaCoord::GameToTile(FVector2D(Dx, Dz)) == AsdaCoord::GameToTile(FVector2D(Cx, Cz));
		TArray<uint8> B; B.SetNumZeroed(27);
		B[0] = (Op == 4006) ? (bSameCell ? 0 : 1) : 0;
		FMemory::Memcpy(&B[1], T + 1, 4);                                  // userIdx + подтип хода
		FMemory::Memcpy(&B[5], &Cx, 4); FMemory::Memcpy(&B[9], &Cz, 4);
		if (Op == 4006) { FMemory::Memcpy(&B[13], &Dx, 4); FMemory::Memcpy(&B[17], &Dz, 4); }
		else            { FMemory::Memcpy(&B[13], &Cx, 4); FMemory::Memcpy(&B[17], &Cz, 4); }
		const float Spd = 0.35f; FMemory::Memcpy(&B[21], &Spd, 4);         // скорость сервера (как автоответчик хода)
		FMemory::Memcpy(&B[25], T + 25, 2);                                // renderId цели
		return B;
	}
	// 5251 — изученные навыки (34 б на запись: код, уровень 5, «можно вкачивать»).
	using AsdaTestPackets::Body5251;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}
	static AAsdaPlayerCharacter* PeerOf(const FState& S, int32 Idx)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(Idx)) : nullptr;
	}
	// Клип, который сейчас стоит на скелете персонажа (одноузловой инстанс порта `UAsdaBlendAnimInstance`).
	static UAnimationAsset* CurAsset(const AAsdaPlayerCharacter* C)
	{
		if (!C || !C->BaseSkel) { return nullptr; }
		const UAnimSingleNodeInstance* I = C->BaseSkel->GetSingleNodeInstance();
		return I ? I->GetAnimationAsset() : nullptr;
	}
	static FString PkgOf(const UObject* O) { return O ? O->GetOutermost()->GetName() : FString(TEXT("—")); }
	// Поза: компонентные позиции всех костей.
	static void PoseOf(const AAsdaPlayerCharacter* C, TArray<FVector>& Out)
	{
		Out.Reset();
		if (!C || !C->BaseSkel) { return; }
		for (const FTransform& T : C->BaseSkel->GetComponentSpaceTransforms()) { Out.Add(T.GetLocation()); }
	}
	static float MeanMove(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		const int32 N = FMath::Min(A.Num(), B.Num());
		if (N == 0) { return 0.f; }
		double Sum = 0.0;
		for (int32 i = 0; i < N; ++i) { Sum += FVector::Dist(A[i], B[i]); }
		return (float)(Sum / N);
	}
	static void LiveFx(UWorld* W, TSet<TWeakObjectPtr<AActor>>& Out)
	{
		Out.Reset();
		if (!W) { return; }
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It) { if (!It->IsPendingKillPending()) { Out.Add(*It); } }
	}

	// Повернуть кастера правым боком к камере.
	static void Aim(FState& S, AActor* Target)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || !Target) { return; }
		Target->SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw + 180.f + 50.f, 0.f));
	}
	static void Shot(const FState& S, const FString& Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
	}

	// Подходит ли навык подтипу оружия (см. шапку).
	static bool SkillFits(const FAsdaSkillDef& D, int32 W)
	{
		if (D.SubType != 1 && D.SubType != 2 && D.SubType != 5) { return false; }
		if (D.WeaponMask != 0) { return ((D.WeaponMask >> W) & 1) != 0; }
		return BandOfGrade(D.Grade) != 0 && BandOfGrade(D.Grade) == BandOfWeapon(W);
	}

	// Оружие подтипа `W` с моделью (ItemType 9; удочки/буры subType 10 — Variant 1/2 — не берём).
	static int32 PickWeapon(const UAsdaItemTableSubsystem* Items, int32 W)
	{
		if (!Items) { return 0; }
		for (const FAsdaItemRecord& R : Items->GetAllItems())
		{
			if (R.ItemType == 9 && R.ItemSubType == W && R.ItemVariant == 0) { return R.ItemCode; }
		}
		return 0;
	}

	// Конец прогона: соседи и моб уходят (4011), свой номер возвращается.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UAsdaSkillFxSubsystem* FxH = S.Fx.Get()) { FxH->OnSkillHitMoment.Remove(S.HitH); }   // ★ 10.11.2026
		for (int32 P : S.Peers) { S.Feed(4011, Body4011(P)); }
		S.Feed(4011, Body4011(kMobRid));
		S.Feed(4011, Body4011(kVictim));
		S.Feed(4011, Body4011(kVictim2));
		if (AAsdaPlayerCharacter* Me = MeOf(S)) { Me->SetActorHiddenInGame(false); }
		if (S.OrigWeapon >= 0) { if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(9, S.OrigWeapon, 0); } }
		FString Tail;
		for (const TPair<FString, int32>& G : S.Groups) { Tail += FString::Printf(TEXT(" %s=%d"), *G.Key, G.Value); }
		UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ сводка провалов по причинам:%s"), Tail.IsEmpty() ? TEXT(" нет") : *Tail);
		if (UAsdaNetSubsystem* N = S.Net.Get(); N && S.CapGen)
		{
			AsdaSelfTestKit::DisarmCapture(N, S.CapGen);
			N->bInGameConnected = S.bWasInGame; N->bStandCaptureSkillUse = S.bWasStandSkill;
		}
		if (UWorld* Wd = S.W.Get()) { if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController())) { PC->SetTarget(nullptr); } }
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaAnimAudit, TEXT("АНИМ-АУДИТ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		// Ответы «сервера» на ход — вне колбэка перехвата (закон no-reentrant-net-callbacks).
		if (S.Replies.Num() > 0)
		{
			TArray<TPair<uint16, TArray<uint8>>> Q = MoveTemp(S.Replies);
			for (const TPair<uint16, TArray<uint8>>& P : Q) { S.Feed(P.Key == 4006 ? 4007 : 4009, P.Value); }
		}
		while (S.Steps.IsValidIndex(S.Idx) && !S.bAbort)
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Итог одного навыка: проверки + строка журнала.
	static void Report(FState& S, const FCase& C)
	{
		FRun& R = S.Cur;
		const TCHAR* G = (R.Sex == 2) ? TEXT("f") : TEXT("m");
		// Эффекты у цели: наибольшее расстояние до движущегося моба (> 120 юн — эффект отстал от цели).
		float DriftMax = 0.f;
		for (const TPair<TWeakObjectPtr<AActor>, float>& It : R.FxNearTarget) { DriftMax = FMath::Max(DriftMax, It.Value); }
		if (S.bMobMove && R.FxNearTarget.Num() > 0)
		{
			UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ следование %d: эффектов у цели %d, наибольший отрыв от моба %.0f юн"), R.Skill, R.FxNearTarget.Num(), DriftMax);
		}
		// ★ 10.11.2026 `miss1`: промах по основной цели — эффектов, рождённых у неё, нет (только «MISS»; тики на помощниках удара).
		if (S.bMiss1 && !C.bSelf)
		{
			// ★ 10.11.2026 (ревизия 6): навык без отложенных записей — каждый выпуск по основной цели: урон 0 и «MISS» (ветка промаха эталона).
			TArray<TPair<float, int32>> WvM;
			if (!UAsdaSkillFxSubsystem::SkillRecordWaves(R.Skill, 1, WvM))
			{
				bool bMissOk = R.MainHits.Num() > 0;
				FString ML;
				for (const TPair<int32, bool>& H : R.MainHits) { bMissOk &= (H.Key == 0 && H.Value); ML += FString::Printf(TEXT(" %d%s"), H.Key, H.Value ? TEXT("(MISS)") : TEXT("")); }
				S.Check(bMissOk, FString::Printf(TEXT("промах по основной цели %d (урон записи %d): выпуски [%s] — ждём 0(MISS), без цифры"), R.Skill, S.Dmg, *ML));
			}
			// Навыки с отложенными записями (907) рождают эффект записи у основной цели и при промахе (вид 13 — вне `& 1`) — не проверяем.
			if (!UAsdaSkillFxSubsystem::SkillRecordWaves(R.Skill, 1, WvM))
			S.Check(R.NearTargetAfterCombo0 == 0, FString::Printf(TEXT("промах по основной цели %d: эффектов удара у цели %d (ждём 0; всего у цели %d — с эффектами каста)"),
				R.Skill, R.NearTargetAfterCombo0, R.FxNearTarget.Num()));
		}
		// ★ 10.11.2026 `tgtchar2`: эффекты у второй цели-игрока (виды xchar/rchar) — есть при попадании, нет при промахе; с `mobmove` идут за ней.
		if (S.bTgtChar2 && !C.bSelf)
		{
			float Drift2 = 0.f;
			for (const TPair<TWeakObjectPtr<AActor>, float>& It : R.FxNearTarget2) { Drift2 = FMath::Max(Drift2, It.Value); }
			UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ цель2 %d: %s, эффектов у второй цели %d, наибольший отрыв %.0f юн"),
				R.Skill, S.bMiss2 ? TEXT("промах") : TEXT("попадание"), R.FxNearTarget2.Num(), Drift2);
			// Эффекты видов xchar/rchar у второй цели: при попадании должны быть, при промахе — нет.
			//   Из них «follow:target» с `mobmove` — отрыв ≤ 120 юн (идут за целью).
			if (R.Extra2Jebs.Num() > 0)
			{
				int32 NExtra = 0, NFollow = 0; float FollowDrift = 0.f;
				for (const TPair<TWeakObjectPtr<AActor>, float>& It : R.FxNearTarget2)
				{
					const AActor* FA = It.Key.Get();
					if (!FA) { continue; }
					const FString Cls = FA->GetClass()->GetName();
					for (const FString& J : R.Extra2Jebs) { if (Cls.Contains(J)) { ++NExtra; break; } }
					for (const FString& J : R.FollowJebs) { if (Cls.Contains(J)) { ++NFollow; FollowDrift = FMath::Max(FollowDrift, It.Value); break; } }
				}
				const bool bFollowRow = R.FollowJebs.Num() > 0;
				const bool bOkF = S.bMiss2 ? (NExtra == 0)
					: (NExtra > 0 && (!bFollowRow || !S.bMobMove || (NFollow > 0 && FollowDrift <= 120.f)));
				S.Check(bOkF, FString::Printf(TEXT("цель2 %d: эффектов xchar/rchar у второй цели %d (ждём %s)%s"),
					R.Skill, NExtra, S.bMiss2 ? TEXT("0 — промах") : TEXT(">0"),
					*(bFollowRow ? FString::Printf(TEXT(", follow:target %d, отрыв %.0f юн%s"), NFollow, FollowDrift, S.bMobMove ? TEXT(" (цель идёт, порог 120)") : TEXT("")) : FString())));
			}
		}
		FString Why;
		auto Bad = [&](const TCHAR* Key) { Why += Why.IsEmpty() ? Key : *FString::Printf(TEXT(",%s"), Key); S.Groups.FindOrAdd(Key)++; };
		if (R.bPartyRefused || R.bPlayerOnly) { }   // ожидаемо: групповой без отряда / «только на игрока» по мобу — заявки нет и у эталона
		else if (R.bNoRequest) { Bad(TEXT("ЗАЯВКА_НЕ_УШЛА")); }
		else if (!R.bStarted) { Bad(TEXT("ПОКАЗ_НЕ_СТАРТОВАЛ")); }
		else
		{
			if (!R.bAssetOk) { Bad(TEXT("НЕ_ТОТ_КЛИП")); }
			// Рывок: эталон закрывает показ на фазе 3 — клип обрывается на прибытии (COMBO0), это не перебивание.
			if (R.OverT >= 0.f && !(R.bDash && R.OverT >= R.Combo0S - 0.05f)) { Bad(TEXT("КЛИП_ПЕРЕБИТ")); }
			if (R.MaxMove < kMoveMin) { Bad(TEXT("ПОЗА_СТОИТ")); }
			if (R.ExpectFx > 0 && R.NewFx.Num() == 0 && !S.bMiss1) { Bad(TEXT("ЭФФЕКТОВ_НЕТ")); }   // ★ 10.11: при `miss1` эффектов может не быть вовсе
			// ★ 10.11.2026 (job4-vfx): ОТЛОЖЕННЫЕ ЗАПИСИ эталона — столько выпусков, сколько записей (группы по задержке), и каждая
			//   через свою задержку от порога COMBO0 (допуск: кадр порога фазы + кадр выпуска, 0.12 с).
			TArray<TPair<float, int32>> Waves;
			if (UAsdaSkillFxSubsystem::SkillRecordWaves(R.Skill, 1100, Waves))
			{
				const UAsdaSkillFxSubsystem* FxR = S.Fx.Get();
				TArray<float> Rel;
				// Задержка = выпуск − взвод (порог фазы 0): сам момент порога ведёт клип (кадр COMBO0), его здесь не меряем.
				if (FxR) { for (int32 k = 0; k < FxR->DebugRecordFireTimes.Num() && k < FxR->DebugRecordArmTimes.Num(); ++k) { Rel.Add(float(FxR->DebugRecordFireTimes[k] - FxR->DebugRecordArmTimes[k])); } }
				bool bTimes = Rel.Num() == Waves.Num();
				FString L;
				for (int32 k = 0; k < Rel.Num(); ++k)
				{
					const float Want = k < Waves.Num() ? Waves[k].Key / 1000.f : -1.f;
					// допуск сверху — самый длинный кадр показа (кадр со снимком экрана бывает ~0.2 с) + 0.05; не меньше 0.12
					bTimes &= (Rel[k] >= Want - 0.02f && Rel[k] <= Want + FMath::Max(0.12f, R.MaxFrameDt + 0.05f));
					L += FString::Printf(TEXT("%s%.2f"), k ? TEXT(" ") : TEXT(""), Rel[k]);
				}
				UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ записи %d: выпусков %d (ждём %d), задержки от взвода [%s] с, ждём от %.2f шагом %.2f (самый длинный кадр %.3f с)"),
					R.Skill, Rel.Num(), Waves.Num(), *L, Waves[0].Key / 1000.f, Waves.Num() > 1 ? (Waves[1].Key - Waves[0].Key) / 1000.f : 0.f, R.MaxFrameDt);
				if (!bTimes) { Bad(TEXT("ЗАПИСИ_НЕ_ВОВРЕМЯ")); }
				// `late`: запись взведена ПОСЛЕ порога фазы 0 (от прихода 5270), а не на пороге.
				// (ревизия 5) взвод — в кадре подачи 5270 (± кадр), а не на пороге и не через сторожа полки 0.35 с.
				if (S.bLate && (!FxR || FxR->DebugRecordArmTimes.Num() == 0 || R.LateSentAtS <= 0.0
					|| FxR->DebugRecordArmTimes[0] < R.LateSentAtS - 0.001 || FxR->DebugRecordArmTimes[0] > R.LateSentAtS + R.MaxFrameDt + 0.05)) { Bad(TEXT("ЗАПИСЬ_НЕ_ПОЗДНЯЯ")); }
				// ★ Ревизия 5: доли урона и «MISS» по основной цели — как эталон (SkillRecordWaves с уроном `dmg=`; вид 9 рисует «MISS» на нулевой
				//   доле при попадании, вид 13 «MISS» не рисует вовсе).
				// (ревизия 5) ЖЁСТКИЕ числа эталона (эмуляция effects_particles.c:8440-8477 на 16 битах, сверена ревизором), не та же функция:
				//   урон → (доля волн 0..7, доля волны 8). Другой урон в `dmg=` — проверка «ДОЛИ_НЕТ_ЭТАЛОНА».
				TArray<TPair<float, int32>> Exp;
				if (R.Skill == 907) { Exp.Emplace(400.f, S.Dmg); }
				else if (R.Skill == 911)
				{
					static const int32 kTab[][3] = { {1, 0, 1}, {10, 0, 10}, {11, 1, 3}, {1100, 100, 300}, {65536, 5957, 17880}, {120200, 10927, -32752} };
					bool bTab = false;
					for (const auto& Row : kTab)
					{
						if (Row[0] != S.Dmg) { continue; }
						bTab = true;
						for (int32 n = 0; n < 9; ++n) { Exp.Emplace(400.f + 100.f * n, n < 8 ? Row[1] : Row[2]); }
					}
					if (!bTab) { Bad(TEXT("ДОЛИ_НЕТ_ЭТАЛОНА")); }
				}
				const bool bHitMain = !S.bMiss1;
				bool bVals = R.MainHits.Num() == Exp.Num();
				FString VL;
				for (int32 k = 0; k < R.MainHits.Num(); ++k)
				{
					const bool bWantMiss = (R.Skill == 911) && bHitMain && k < Exp.Num() && Exp[k].Value == 0;
					// `miss1` (бит попадания 0): у отложенных записей — ни цифры, ни «MISS», урон выпуска 0 (asda.Skill.MissNoDamageText).
					const int32 WantVal = bHitMain ? (k < Exp.Num() ? Exp[k].Value : 0) : 0;
					if (k < Exp.Num()) { bVals &= (R.MainHits[k].Key == WantVal) && (R.MainHits[k].Value == bWantMiss); }
					VL += FString::Printf(TEXT("%s%d%s"), k ? TEXT(" ") : TEXT(""), R.MainHits[k].Key, R.MainHits[k].Value ? TEXT("(MISS)") : TEXT(""));
				}
				UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ доли %d: урон %d → выпуски по основной цели [%s] (ждём %d шт.)"), R.Skill, S.Dmg, *VL, Exp.Num());
				if (!bVals) { Bad(TEXT("ДОЛИ_НЕ_ТЕ")); }
			}
		}
		S.Check(Why.IsEmpty(), FString::Printf(
			TEXT("%d %s %d клип=%s (%s) длит=%.2fс COMBO0=%.2fс | стоял=%s перебит=%s%s движение=%.1f эффектов=%d/%d%s%s"),
			C.W, *FString::Printf(TEXT("%s%s"), G, C.bSelf ? TEXT(" свой") : TEXT("")), R.Skill, *R.ExpectClip, *FPaths::GetBaseFilename(R.Expect), R.LenS, R.Combo0S,
			*FPaths::GetBaseFilename(R.FirstAsset), R.OverT >= 0.f ? *FString::Printf(TEXT("%.2fс"), R.OverT) : TEXT("нет"),
			R.OverT >= 0.f ? *FString::Printf(TEXT(" (%s)"), *FPaths::GetBaseFilename(R.OverBy)) : TEXT(""),
			R.MaxMove, R.NewFx.Num(), R.ExpectFx, Why.IsEmpty() ? (R.bPartyRefused ? TEXT(" — групповой вне отряда: отказ 1223, как эталон") : (R.bPlayerOnly ? TEXT(" — только на игрока: по мобу эталон не применяет") : TEXT(""))) : TEXT(" — "), *Why));
	}

	// Шаг «навык на кастере»: подать 5257, мерить до конца клипа.
	static FStep SkillStep(FState* S, FCase C, int32 Skill)
	{
		return FStep([S, C, Skill](float T) -> bool
		{
			UWorld* W = S->W.Get();
			UAsdaSkillFxSubsystem* Fx = S->Fx.Get();
			AAsdaPlayerCharacter* P = C.bSelf ? MeOf(*S) : PeerOf(*S, C.Peer);
			FRun& R = S->Cur;
			// ── fight: клик по мобу → 4026 → 4029 → навык с панели посреди замаха → 5256 → 5257 ──
			if (S->bFight && C.bSelf && !R.bCast)
			{
				const double Now = W ? W->GetTimeSeconds() : 0.0;
				AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
				AActor* Mob = S->WS.IsValid() ? Cast<AActor>(S->WS->FindNetMobByRid(kMobRid)) : nullptr;
				if (R.Skill != Skill)
				{
					R = FRun(); R.Skill = Skill; R.Sex = P ? P->Sex : C.Sex; R.PhaseT0 = Now; S->Outs.Reset();
					if (PC && Mob) { PC->SetTarget(Mob); PC->EngageMobFromClick(Mob); }
					R.FightPhase = 1;
					return false;
				}
				const float Pt = (float)(Now - R.PhaseT0);
				if (R.FightPhase == 1)
				{
					if (S->Outs.Contains(4026) || S->Outs.Contains(4028))
					{
						S->Feed(4029, Body4029(kMe, kMobRid, 900)); R.FightPhase = 2; R.PhaseT0 = Now;
					}
					else if (Pt > 3.f) { R.FightPhase = 2; R.PhaseT0 = Now; }   // замаха не вышло — навык всё равно жмём
					return false;
				}
				if (R.FightPhase == 2)
				{
					if (Pt < 0.6f) { return false; }   // середина замаха
					S->Outs.Reset();
					if (UAsdaSkillUseSubsystem* U = W->GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>()) { U->DispatchByKind(Skill, -1); }
					R.FightPhase = 3; R.PhaseT0 = Now;
					return false;
				}
				if (R.FightPhase == 3 && S->bExit400)
				{
					UAsdaSkillUseSubsystem* U = W->GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>();
					if (R.ExitWay < 0 && U && U->HasSkillApproach())
					{
						// Выход из боя ВО ВРЕМЯ подхода ради навыка: эталон в 102, действия атаки 5 нет — 4024/400 уходить не должен.
						R.ExitWay = S->Exit400N++ % 3; R.ExitT0 = Now; S->Outs.Reset();
						if (R.ExitWay == 0 && PC) { PC->SetTarget(nullptr); }
						else if (R.ExitWay == 1 && PC && P) { PC->RequestMoveTo(P->GetGamePosXZ() + FVector2D(-300.f, 0.f)); }
						else if (R.ExitWay == 2) { S->Feed(4011, Body4011(kMobRid)); }
						return false;
					}
					if (R.ExitWay >= 0)
					{
						if (Now - R.ExitT0 < 0.5) { return false; }
						static const TCHAR* kWays[3] = { TEXT("снятие цели"), TEXT("клик по земле"), TEXT("цель исчезла (4011)") };
						S->Check(!S->Outs.Contains(4024), FString::Printf(TEXT("%d свой %d: выход из боя во время подхода ради навыка (%s) — 4024/400 %s"),
							C.W, Skill, kWays[R.ExitWay], S->Outs.Contains(4024) ? TEXT("УШЁЛ (ложный)") : TEXT("не ушёл (эталон: 102, не действие 5)")));
						if (R.ExitWay == 2 && P) { S->Feed(4013, AsdaTestPackets::MobSpawn4013(kMobRid, S->MobType, AsdaCoord::GameToTile(P->GetGamePosXZ()) + FIntPoint(3, 0), 1, 250)); }
						if (PC) { PC->SetTarget(nullptr); }
						R.Skill = 0; R.FightPhase = 0;
						return true;
					}
				}
				if (R.FightPhase == 3)
				{
					if (!S->Outs.Contains(5256) && !S->Outs.Contains(6158))
					{
						if (Pt < 3.0f) { return false; }
						const FAsdaSkillDef* Dp = S->Db.IsValid() ? S->Db->Get(Skill) : nullptr;
						R.bPartyRefused = Dp && Dp->TargetSlotType == 2;   // групповой: вне отряда эталон шлёт нотис 1223 и заявку не строит
						R.bPlayerOnly = Dp && Dp->CastType == 2;           // «только на игрока»: по мобу эталон не применяет (`if (castType != 2)`)
						R.bNoRequest = true; Report(*S, C); R.Skill = 0; R.FightPhase = 0;
						if (PC) { PC->SetTarget(nullptr); }
						return true;
					}
					R.bGuardianReq = !S->Outs.Contains(5256) && S->Outs.Contains(6158);
					R.FightPhase = 4;   // дальше — общий замер; ответ (5257 или 6159 Хранителя) подаёт он
				}
			}
			if (!R.bCast)
			{
				const int32 KeepPhase = R.FightPhase;
				const bool bKeepGuard = R.bGuardianReq;
				R = FRun();
				R.Skill = Skill;
				R.FightPhase = KeepPhase;
				R.bGuardianReq = bKeepGuard;
				R.Sex = P ? P->Sex : C.Sex;
				if (!P || !Fx) { S->Check(false, FString::Printf(TEXT("%d навык %d: нет кастера/подсистемы"), C.W, Skill)); return true; }
				P->SetActorHiddenInGame(false);
				Aim(*S, P);
				if (const FAsdaSkillClipRow* Row = Fx->ResolveRow(Skill, P->Sex, P->EquippedWeaponSubType))
				{
					R.ExpectClip = Row->Clip;
					R.Expect = AsdaAssetPaths::SkelAnim(AsdaAssetPaths::kSkelRootCharacters, AsdaAssetPaths::CharPreviewName(P->Sex != 2), Row->AnimIdx);
					R.Combo0S = Row->Combo0Ms * 0.001f;
					R.bDash = Row->Loader.Contains(TEXT("Rush"));
					TArray<FAsdaSkillFxSpawn> L;
					for (const FString* Cell : { &Row->FxCast, &Row->FxHit0, &Row->FxHit1, &Row->FxHit2 })
					{
						L.Reset(); UAsdaSkillFxSubsystem::ParseFxList(*Cell, L); R.ExpectFx += L.Num();
						for (const FAsdaSkillFxSpawn& Sp : L)
						{
							if (Sp.Anchor == FName(TEXT("follow")) && Sp.Origin == FName(TEXT("target"))) { R.FollowJebs.AddUnique(Sp.Jeb.Replace(TEXT("-"), TEXT("_"))); }
							if (Sp.TargetKind == FName(TEXT("xchar")) || Sp.TargetKind == FName(TEXT("rchar"))) { R.Extra2Jebs.AddUnique(Sp.Jeb.Replace(TEXT("-"), TEXT("_"))); }
						}
					}
				}
				LiveFx(W, R.Before);
				// `mobmove`: моб-цель рождается заново и идёт 6 клеток поперёк — эффект «у цели» обязан ехать за ним (привязка эталона 9).
				if (S->bTgtChar && !C.bSelf)
				{
					// `tgtchar`: моба нет, цель — сосед kVictim (4005) в 3 клетках; с `mobmove` он идёт поперёк кадра пакетом 4007
					//   (`Recv_EntityMove@0xF3C2C0`: result 1, userIdx, cur/dst game XZ float, скорость 0.35).
					S->Feed(4011, Body4011(kMobRid));
					const FIntPoint T0 = AsdaCoord::GameToTile(P->GetGamePosXZ());
					const FVector2D From = AsdaCoord::TileCenterGame(T0.X + 3, T0.Y + (S->bMobMove ? -3 : 0));
					const FVector2D To = AsdaCoord::TileCenterGame(T0.X + 3, T0.Y + 3);
					S->Feed(4011, Body4011(kVictim));
					S->Feed(4005, Body4005(kVictim, 1, 0, From.X, From.Y));
					if (S->bTgtChar2)
					{
						// Вторая цель — в 2 клетках от первой; с `mobmove` идёт параллельно ей (тот же 4007).
						const FVector2D From2 = From + FVector2D(AsdaCoord::TileCenterGame(2, 0).X - AsdaCoord::TileCenterGame(0, 0).X, 0.f);
						const FVector2D To2 = To + (From2 - From);
						S->Feed(4011, Body4011(kVictim2));
						S->Feed(4005, Body4005(kVictim2, 2, 0, From2.X, From2.Y));
						if (S->bMobMove)
						{
							TArray<uint8> M2; M2.SetNumZeroed(27);
							M2[0] = 1; AsdaBytes::WrI16(&M2[1], (int16)kVictim2);
							const float Ax = From2.X, Az = From2.Y, Bx = To2.X, Bz = To2.Y, Spd2 = 0.35f;
							FMemory::Memcpy(&M2[5], &Ax, 4); FMemory::Memcpy(&M2[9], &Az, 4);
							FMemory::Memcpy(&M2[13], &Bx, 4); FMemory::Memcpy(&M2[17], &Bz, 4); FMemory::Memcpy(&M2[21], &Spd2, 4);
							AsdaBytes::WrI16(&M2[25], -1);
							S->Feed(4007, M2);
						}
					}
					if (S->bMobMove)
					{
						TArray<uint8> M; M.SetNumZeroed(27);
						M[0] = 1; AsdaBytes::WrI16(&M[1], (int16)kVictim);
						const float Fx0 = From.X, Fz0 = From.Y, Tx = To.X, Tz = To.Y, Spd = 0.35f;
						FMemory::Memcpy(&M[5], &Fx0, 4); FMemory::Memcpy(&M[9], &Fz0, 4);
						FMemory::Memcpy(&M[13], &Tx, 4); FMemory::Memcpy(&M[17], &Tz, 4); FMemory::Memcpy(&M[21], &Spd, 4);
						AsdaBytes::WrI16(&M[25], -1);
						S->Feed(4007, M);
					}
				}
				else if (S->bMobMove && !C.bSelf)
				{
					const FIntPoint T0 = AsdaCoord::GameToTile(P->GetGamePosXZ());
					S->Feed(4013, AsdaTestPackets::MobSpawn4013Move(kMobRid, S->MobType, T0 + FIntPoint(3, -3), T0 + FIntPoint(3, 3), /*State*/2, 250));
				}
				const FAsdaSkillDef* D = S->Db.IsValid() ? S->Db->Get(Skill) : nullptr;
				const bool bSelf = D && D->TargetSlotType == 1;
				// Ответ «сервера»: 5257, у навыков Хранителя (заявка 6158) — 6159 той же раскладки (`Recv_SkillCast@0xF4E370`).
				// ★ 10.11.2026 `miss1`: основная запись — промах (флаг @7 первой записи = 0).
				// ★ 10.11.2026 `dmg=N`: урон каждой записи (@3, i32).
				auto Miss1 = [S](TArray<uint8> B)
				{
					if (S->bMiss1 && B.Num() >= 8 + 29) { B[8 + 7] = 0; }
					for (int32 Off = 8; S->Dmg != 1 && Off + 29 <= B.Num(); Off += 29) { AsdaBytes::WrI32(B.GetData() + Off + 3, S->Dmg); }
					return B;
				};
				TArray<uint8> Body = bSelf ? Body5257(C.Peer, Skill, 1, C.Peer)
					: Miss1(S->bTgtChar ? (S->bTgtChar2 ? Body5257Two(C.Peer, Skill, kVictim, kVictim2, S->bMiss2 ? 0 : 1) : Body5257(C.Peer, Skill, 1, kVictim))
						: Body5257(C.Peer, Skill, 0, kMobRid));
				// ★ 10.11.2026 `late`: 5257 без записей запускает показ; записи придут 5270 после порога (см. ниже).
				if (S->bLate && !bSelf) { S->LateBody = Body; Body.SetNum(8); }
				S->Feed(R.bGuardianReq ? 6159 : 5257, Body);
				R.bCast = true;
				R.CastAtS = W ? W->GetTimeSeconds() : 0.0;
				R.MainHits.Reset(); R.bLateSent = false;
				if (UAsdaSkillFxSubsystem* FxR = S->Fx.Get()) { FxR->DebugRecordFireTimes.Reset(); FxR->DebugRecordArmTimes.Reset(); }   // ★ 10.11.2026: отложенные записи этого каста
				S->StepT0 = W ? W->GetTimeSeconds() : 0.0;
				return false;
			}
			if (!P) { Report(*S, C); R.bCast = false; return true; }
			if (W) { R.MaxFrameDt = FMath::Max(R.MaxFrameDt, W->GetDeltaSeconds()); }   // ★ 10.11.2026: допуск выпуска записи
			// ★ 10.11.2026 `late`: записи этого каста приходят пакетом 5270 уже ПОСЛЕ порога фазы 0 (шапка 7 б: uidx, навык, уровень).
			if (S->bLate && !C.bSelf && !R.bLateSent && R.bStarted && T >= R.Combo0S + 0.15f && S->LateBody.Num() > 8)
			{
				TArray<uint8> B5270; B5270.SetNumZeroed(7);
				AsdaBytes::WrI16(B5270.GetData() + 0, (int16)C.Peer); AsdaBytes::WrI16(B5270.GetData() + 2, (int16)Skill); AsdaBytes::WrI16(B5270.GetData() + 4, 1);
				B5270.Append(S->LateBody.GetData() + 8, S->LateBody.Num() - 8);
				S->Feed(5270, B5270);
				R.bLateSent = true;
				R.LateSentAtS = W ? W->GetTimeSeconds() : 0.0;
			}
			// Сбор нового эффекта (любого — в том числе удара у цели).
			TSet<TWeakObjectPtr<AActor>> Now; LiveFx(W, Now);
			for (const TWeakObjectPtr<AActor>& A : Now) { if (!R.Before.Contains(A)) { R.NewFx.Add(A); } }
			// Следование за целью: эффект, рождённый у моба, — его расстояние до моба на каждом кадре показа (наибольшее).
			if (AActor* Mob = !S->WS.IsValid() ? nullptr
				: (S->bTgtChar ? Cast<AActor>(PeerOf(*S, kVictim)) : Cast<AActor>(S->WS->FindNetMobByRid(kMobRid))))
			{
				for (const TWeakObjectPtr<AActor>& A : R.NewFx)
				{
					if (!A.IsValid()) { continue; }
					const float Dd = FVector::Dist2D(A->GetActorLocation(), Mob->GetActorLocation());
					if (float* Cur = R.FxNearTarget.Find(A)) { *Cur = FMath::Max(*Cur, Dd); }
					else if (Dd <= 150.f)
					{
						R.FxNearTarget.Add(A, Dd);
						if (T >= R.Combo0S - 0.05f) { ++R.NearTargetAfterCombo0; }   // ★ 10.11.2026: эффект удара (не каста) — с порога фазы 0
						// Рождение у цели: масштаб и высота над мобом (attach-13 — подъём +200/400/600 и масштаб цели; track — ×1, без подъёма).
						UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ у цели %d: %s масштаб %.2f, над целью %.0f юн, цель %s"), R.Skill,
							*A->GetName(), A->GetActorScale3D().X, A->GetActorLocation().Z - Mob->GetActorLocation().Z,
							S->bTgtChar ? TEXT("персонаж") : *FString::Printf(TEXT("моб тип %d"), S->MobType));
					}
				}
			}
			// ★ 10.11.2026 `tgtchar2`: эффекты, рождённые у второй цели-игрока, и их отрыв от неё.
			if (AActor* V2 = (S->bTgtChar2 && S->WS.IsValid()) ? Cast<AActor>(PeerOf(*S, kVictim2)) : nullptr)
			{
				for (const TWeakObjectPtr<AActor>& A2 : R.NewFx)
				{
					if (!A2.IsValid()) { continue; }
					const float D2 = FVector::Dist2D(A2->GetActorLocation(), V2->GetActorLocation());
					if (float* Cur2 = R.FxNearTarget2.Find(A2)) { *Cur2 = FMath::Max(*Cur2, D2); }
					else if (D2 <= 150.f) { R.FxNearTarget2.Add(A2, D2); }
				}
			}
			const bool bActive = Fx && Fx->IsSkillVisualActive(P);
			if (!R.bStarted && bActive)
			{
				R.bStarted = true;
				R.LenS = FMath::Max(0.f, (float)(P->SkillAnimUntilS - W->GetTimeSeconds())) + T;
			}
			UAnimationAsset* A = CurAsset(P);
			if (R.bStarted && T >= 0.05f && R.FirstAsset.IsEmpty())
			{
				R.FirstAsset = PkgOf(A);
				R.bAssetOk = (R.FirstAsset == R.Expect);
				PoseOf(P, R.Pose0);
			}
			if (!R.FirstAsset.IsEmpty() && T < R.LenS - 0.1f)
			{
				TArray<FVector> Pose; PoseOf(P, Pose);
				R.MaxMove = FMath::Max(R.MaxMove, MeanMove(R.Pose0, Pose));
				if (R.OverT < 0.f && PkgOf(A) != R.FirstAsset) { R.OverT = T; R.OverBy = PkgOf(A); }
			}
			if (S->ShotW.Contains(C.W) && R.bStarted)
			{
				const TCHAR* G = (R.Sex == 2) ? TEXT("f") : (C.bSelf ? TEXT("self") : TEXT("m"));
				const float Marks[3] = { 0.12f, FMath::Clamp(R.Combo0S, 0.2f, FMath::Max(0.2f, R.LenS - 0.15f)), R.LenS * 0.75f };
				for (int32 k = 0; k < 3; ++k)
				{
					if (!(R.ShotMask & (1 << k)) && T >= Marks[k])
					{
						R.ShotMask |= (1 << k);
						Shot(*S, FString::Printf(TEXT("w%d_%s_%d_%d"), C.W, G, Skill, k));
					}
				}
			}
			// ★ 10.11.2026: у навыка с отложенными записями ждём и последнюю (COMBO0 + задержка + запас), даже если клип короче.
			float RecTailS = 0.f;
			{
				TArray<TPair<float, int32>> Wv;
				if (UAsdaSkillFxSubsystem::SkillRecordWaves(Skill, 1100, Wv) && Wv.Num() > 0) { RecTailS = R.Combo0S + Wv.Last().Key / 1000.f + 0.3f; }
			}
			const bool bDone = R.bStarted ? (T >= FMath::Max(R.LenS + kTailSec, RecTailS)) : (T >= 2.0f);
			if (!bDone) { return false; }
			Report(*S, C);
			if (Fx) { Fx->StopSkillVisual(P); }
			R.bCast = false;
			R.Skill = 0; R.FightPhase = 0;
			if (S->bFight) { if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { PC->SetTarget(nullptr); } }
			return true;
		});
	}

	// ★ 10.11.2026 (job4-vfx): тело 5272 «состояние наложено» (раскладка как у `asda.Fx.StatusSelfTest`): i16 userIdx@0, u16 id@2,
	//   u8 стек@9, i16 остаток (сек)@11, u8 @13.
	static TArray<uint8> Body5272(int32 UserIdx, int32 Id, int32 Sec)
	{
		TArray<uint8> B; B.SetNumZeroed(16);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)UserIdx);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Id);
		B[9] = 1;
		AsdaBytes::WrI16(B.GetData() + 11, (int16)Sec);
		B[13] = 99;
		return B;
	}
	// Тело 5268 «состояние снято»: i16 userIdx@0, u16 id@2.
	static TArray<uint8> Body5268(int32 UserIdx, int32 Id)
	{
		TArray<uint8> B; B.SetNumZeroed(4);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)UserIdx);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Id);
		return B;
	}

	// ★ 10.11.2026 (job4-vfx, `states=`): ВИЗУАЛ СОСТОЯНИЯ на соседе путём игрока — штатный 5272 → обработчик третьего канала
	//   (`SpawnBuffFx` по строке DT_BuffFx, у 4-й профессии — `CharFx_SpawnEquipBoneEffects@0xA73C10`, эффекты `abSkillInfo.inl`).
	//   Ожидание — каждое имя `.jeb` из PlayerFx строки состояния родилось новым эффект-актором (класс BP_Fx_<имя>); кадр через 0.6 с;
	//   затем 5268 снимает состояние. Строка «АНИМ-АУДИТ состояние <id> …» с найденными и недостающими именами.
	static FStep StateStep(FState* S, FCase C, int32 StateId)
	{
		struct FMark { bool bFed = false; bool bShot = false; TSet<TWeakObjectPtr<AActor>> Before; TSet<FString> Seen; TSet<FString> SeenVis; };   // Seen — родились, SeenVis — хоть раз с видимой секцией (вершины есть)
		TSharedRef<FMark> M = MakeShared<FMark>();
		return FStep([S, C, StateId, M](float T)
		{
			UWorld* W = S->W.Get();
			if (!W) { return true; }
			if (!M->bFed)
			{
				LiveFx(W, M->Before);
				if (AAsdaPlayerCharacter* P = PeerOf(*S, C.Peer)) { Aim(*S, P); }
				S->Feed(5272, Body5272(C.Peer, StateId, 30));
				M->bFed = true;
				return false;
			}
			// Новые эффект-акторы за время показа — имена классов (живут разово: копим, пока не исчезли).
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				if (It->IsPendingKillPending() || M->Before.Contains(*It)) { continue; }
				M->Seen.Add(It->GetClass()->GetName());
				for (int32 i = 0; i < It->Emitters.Num(); ++i)
				{
					FVector Cn; float Rd = 0.f; int32 Vt = 0;
					if (It->EmitterSectionStats(i, Cn, Rd, Vt) && Vt > 0) { M->SeenVis.Add(It->GetClass()->GetName()); break; }
				}
			}
			if (!M->bShot && T >= 0.6f)
			{
				M->bShot = true;
				if (S->ShotW.Contains(C.W)) { Shot(*S, FString::Printf(TEXT("st_w%d_%s_%d"), C.W, C.Sex == 2 ? TEXT("f") : TEXT("m"), StateId)); }
			}
			if (T < 1.2f) { return false; }
			// Ожидаемые имена — из той же строки таблицы, что читает обработчик (первое поле записей PlayerFx).
			TArray<FString> Want;
			const UGameInstance* GI = W->GetGameInstance();
			const UAsdaEffectsSubsystem* FxS = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			const FAsdaBuffFxRow* Row = FxS ? FxS->GetBuffFxRow(StateId) : nullptr;
			if (Row)
			{
				TArray<FString> Ents; Row->PlayerFx.ParseIntoArray(Ents, TEXT(";"), true);
				for (const FString& E : Ents) { FString J, Rest; if (!E.Split(TEXT("|"), &J, &Rest)) { J = E; } if (!J.IsEmpty()) { Want.Add(J); } }
			}
			TArray<FString> Miss, Got, Dark;   // Dark — актор родился, но видимых секций не было ни разу
			for (const FString& J : Want)
			{
				const FString Cls = FString(TEXT("BP_Fx_")) + J.Replace(TEXT("-"), TEXT("_"));
				bool bHit = false, bVis = false;
				for (const FString& N : M->Seen) { if (N.Contains(Cls)) { bHit = true; break; } }
				for (const FString& N : M->SeenVis) { if (N.Contains(Cls)) { bVis = true; break; } }
				(bHit ? Got : Miss).Add(J);
				if (bHit && !bVis) { Dark.Add(J); }
			}
			S->Check(Row && Want.Num() > 0 && Miss.Num() == 0 && Dark.Num() == 0,
				FString::Printf(TEXT("состояние %d (пол %d, подтип %d): обработчик %s, ждём %d, родились [%s], нет [%s], без видимых секций [%s]"),
					StateId, C.Sex, C.W, Row ? *Row->PlayerFn : TEXT("—(нет строки DT_BuffFx)"), Want.Num(),
					*FString::Join(Got, TEXT(", ")), *FString::Join(Miss, TEXT(", ")), *FString::Join(Dark, TEXT(", "))));
			S->Feed(5268, Body5268(C.Peer, StateId));
			return true;
		});
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };

		// ═══ Свой персонаж и моб-цель ═══
		S->Steps.Add(Do([S]()
		{
			UWorld* W = S->W.Get();
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), S->MapId));
			S->Feed(4003, Body4003(kMe, S->HomeTile));
		}));
		S->Steps.Add(Wait(4.0f));   // эффект появления ≈3 с
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me != nullptr, TEXT("свой павн 4003"));
			if (!Me) { S->bAbort = true; return; }
			Me->SetActorHiddenInGame(!S->bSelf);   // в кадре только кастер
			const FIntPoint Tile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())) + FIntPoint(3, 0);
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(kMobRid, S->MobType, Tile, 1, 250));
		}));

		// ═══ Пары «оружие × пол» ═══
		int32 PeerN = 0;
		for (int32 W : S->Weapons)
		{
			const int32 Item = PickWeapon(S->Items.Get(), W);
			if (!Item) { S->Check(false, FString::Printf(TEXT("подтип %d: в таблице предметов нет оружия"), W)); continue; }
			TArray<int32> List;
			if (const UAsdaSkillDatabase* Db = S->Db.Get())
			{
				for (const TPair<int32, FAsdaSkillDef>& It : Db->GetAll())
				{
					if (!S->OnlySkills.IsEmpty() && !S->OnlySkills.Contains(It.Key)) { continue; }
					if (!SkillFits(It.Value, W)) { continue; }
					TArray<const FAsdaSkillClipRow*> Rows; S->Fx->CollectRows(It.Key, Rows);
					if (Rows.Num() > 0) { List.Add(It.Key); }
				}
			}
			List.Sort();
			if (S->bSelf)
			{
				FCase C; C.W = W; C.Item = Item; C.Peer = kMe; C.bSelf = true; C.Skills = List;
				S->Steps.Add(Do([S, C, Num = List.Num()]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					if (!Me) { S->bAbort = true; return; }
					if (S->OrigWeapon < 0) { S->OrigWeapon = Me->GetEquippedWeaponItemId(); }
					Me->SetActorHiddenInGame(false);
					S->WS->ApplySelfEquipPart(9, C.Item, 0);   // общая точка ответа 5001: модель оружия + подтип
					if (S->bFight && S->Net.IsValid())
					{
						UAsdaNetSubsystem* N = S->Net.Get();
						if (!S->CapGen)
						{
							TWeakPtr<FState> WSp = S->AsShared();
							S->CapGen = AsdaSelfTestKit::ArmCapture(N, [WSp](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
							{
								TSharedPtr<FState> St = WSp.Pin();
								if (!St) { return; }
								St->Outs.Add(Op);
								// Автоответчик хода, как `CharMove@0x544EB0`/`CharStop@0x545C70` (раскладка — AsdaMove4007SelfTest.cpp):
								//   4006 → 4007 эхо (в свою клетку — result 0), 4008 → 4009. Тело заявки — с +16, ответ 27 б.
								if ((Op == 4006 || Op == 4008) && Pl && Len >= 43) { St->Replies.Emplace(Op, MoveReply(Op, Pl + 16)); }
							}, (uint16)0, kCmd);
							S->bWasInGame = N->bInGameConnected; N->bInGameConnected = true;
							S->bWasStandSkill = N->bStandCaptureSkillUse; N->bStandCaptureSkillUse = true;
						}
						S->Feed(5251, Body5251(C.Skills));
						N->SetVitalsMp(kMe, 99999, 99999, kCmd, true);
					}
					UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ свой: подтип %d, пол %d, оружие %d, навыков %d"), C.W, Me->Sex, C.Item, Num);
				}));
				S->Steps.Add(Wait(1.0f));
				S->Steps.Add(Do([S, C]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					S->Check(Me && Me->EquippedWeaponSubType == C.W,
						FString::Printf(TEXT("свой: подтип оружия %d (ждём %d)"), Me ? Me->EquippedWeaponSubType : -1, C.W));
				}));
				for (int32 Skill : List) { S->Steps.Add(SkillStep(S, C, Skill)); }
				continue;
			}
			for (uint8 Sex : S->Sexes)
			{
				FCase C; C.W = W; C.Sex = Sex; C.Item = Item; C.Peer = kPeerBase + PeerN++;
				S->Peers.Add(C.Peer);
				S->Steps.Add(Do([S, C, Num = List.Num()]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					const FVector2D G = Me ? AsdaCoord::UeToGame2D(Me->GetActorLocation()) : AsdaCoord::TileCenterGame(256, 256);
					for (int32 Prev : S->Peers) { if (Prev != C.Peer) { if (AActor* A = S->WS->ActorByUserIdx(Prev)) { A->SetActorHiddenInGame(true); } } }
					S->Feed(4005, Body4005(C.Peer, C.Sex, C.Item, G.X, G.Y));
					UE_LOG(LogAsdaAnimAudit, Log, TEXT("АНИМ-АУДИТ пара: подтип %d, пол %d, оружие %d, сосед %d, навыков %d"),
						C.W, C.Sex, C.Item, C.Peer, Num);
				}));
				S->Steps.Add(Wait(1.5f));
				S->Steps.Add(Do([S, C]()
				{
					AAsdaPlayerCharacter* P = PeerOf(*S, C.Peer);
					S->Check(P && P->EquippedWeaponSubType == C.W && P->Sex == C.Sex,
						FString::Printf(TEXT("сосед %d: пол %d, подтип оружия %d (ждём %d/%d)"), C.Peer,
							P ? P->Sex : -1, P ? P->EquippedWeaponSubType : -1, C.Sex, C.W));
					// Контрольный кадр без эффектов — опора замеров яркости (asda_skillglow_metrics.py: кадр навыка минус контроль).
					if (P && S->ShotW.Contains(C.W)) { Aim(*S, P); Shot(*S, FString::Printf(TEXT("w%d_%s_control"), C.W, C.Sex == 2 ? TEXT("f") : TEXT("m"))); }
				}));
				S->Steps.Add(Wait(1.0f));
				for (int32 Skill : List) { S->Steps.Add(SkillStep(S, C, Skill)); }
				for (int32 St : S->States) { S->Steps.Add(StateStep(S, C, St)); S->Steps.Add(Wait(0.5f)); }   // ★ 10.11.2026 `states=`
				S->Steps.Add(Do([S, C]() { S->Feed(4011, Body4011(C.Peer)); }));
				S->Steps.Add(Wait(0.3f));
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillAnimAudit(
	TEXT("asda.Skill.SelfTestAnimAudit"),
	TEXT("Самотест анимаций и эффектов навыков путём игрока: сосед 4005 с оружием подтипа и полом, навык пакетом 5257, замер по кадрам "
	     "(показ стартовал, тот клип по таблице, не перебит, поза движется, эффекты родились). Офлайн, карта мира. "
	     "`w=3,1` подтипы оружия (по умолчанию 3), `g=m|f|mf` (mf), `skills=507,508`, `self` (кастер — свой), `fight` (свой: навык посреди автоатаки с панели), `approach0` (контроль: asda.Skill.ApproachOverAttack 0), `preset=N` (scalability), `map=N tile=X,Y` (карта данных и клетка своего; уровень — картой пакета), `nolight` (без эффект-света), `mobmove` (моб-цель идёт во время навыка, замер отрыва эффектов «у цели»), `mob=N` (тип моба-цели, по умолчанию 1), `tgtchar` (цель — персонаж-сосед, с `mobmove` он идёт), `exit400` (выход из боя во время подхода — 4024/400 не уходит), `shot=<папка>` и `shotw=3` — кадры, `states=301,302` (визуал состояний на соседе пакетом 5272, кадр st_w<подтип>_<пол>_<id>), `tgtchar2` (вторая цель-игрок), `miss2` (промах по второй), `miss1` (промах по основной), `dmg=N` (урон записей; доли 911), `late` (записи 5270 после порога), `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSkillAnimAuditTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaSkillFxSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
		UAsdaSkillDatabase* Db = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!Net || !WS || !Fx || !Db || !Items)
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/визуала навыков/базы навыков/предметов"));
			return;
		}
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Fx = Fx; SP->Db = Db; SP->Items = Items;
		auto Ints = [](const FString& V, auto& Out) { TArray<FString> P; V.ParseIntoArray(P, TEXT(","), true); for (const FString& X : P) { Out.Add(FCString::Atoi(*X)); } };
		FString Gs = TEXT("mf");
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else if (P.StartsWith(TEXT("shotw="), ESearchCase::IgnoreCase)) { TArray<int32> L; Ints(P.Mid(6), L); for (int32 X : L) { SP->ShotW.Add(X); } }
			else if (P.StartsWith(TEXT("w="), ESearchCase::IgnoreCase))     { Ints(P.Mid(2), SP->Weapons); }
			else if (P.StartsWith(TEXT("g="), ESearchCase::IgnoreCase))     { Gs = P.Mid(2).ToLower(); }
			else if (P.Equals(TEXT("self"), ESearchCase::IgnoreCase))     { SP->bSelf = true; }
			else if (P.Equals(TEXT("fight"), ESearchCase::IgnoreCase))    { SP->bSelf = true; SP->bFight = true; }
			else if (P.Equals(TEXT("mobmove"), ESearchCase::IgnoreCase))  { SP->bMobMove = true; }
			else if (P.Equals(TEXT("tgtchar"), ESearchCase::IgnoreCase))  { SP->bTgtChar = true; }
			else if (P.Equals(TEXT("tgtchar2"), ESearchCase::IgnoreCase)) { SP->bTgtChar = true; SP->bTgtChar2 = true; }   // ★ 10.11.2026: вторая цель-игрок
			else if (P.Equals(TEXT("miss2"), ESearchCase::IgnoreCase))    { SP->bMiss2 = true; }
			else if (P.Equals(TEXT("miss1"), ESearchCase::IgnoreCase))    { SP->bMiss1 = true; }
			else if (P.StartsWith(TEXT("dmg="), ESearchCase::IgnoreCase))  { SP->Dmg = FCString::Atoi(*P.Mid(4)); }                   // ★ 10.11.2026
			else if (P.Equals(TEXT("late"), ESearchCase::IgnoreCase))     { SP->bLate = true; }                                       // ★ 10.11.2026                            // ★ 10.11.2026: промах по основной цели                            // ★ 10.11.2026: промах по второй цели
			else if (P.Equals(TEXT("exit400"), ESearchCase::IgnoreCase))  { SP->bSelf = true; SP->bFight = true; SP->bExit400 = true; }
			else if (P.StartsWith(TEXT("skills="), ESearchCase::IgnoreCase)){ TArray<int32> L; Ints(P.Mid(7), L); for (int32 X : L) { SP->OnlySkills.Add(X); } }
			else if (P.StartsWith(TEXT("states="), ESearchCase::IgnoreCase)){ Ints(P.Mid(7), SP->States); }   // ★ 10.11.2026: визуал состояний (5272 на соседа)
		}
		if (SP->Weapons.IsEmpty()) { SP->Weapons.Add(3); }
		if (Gs.Contains(TEXT("m"))) { SP->Sexes.Add(1); }
		if (Gs.Contains(TEXT("f"))) { SP->Sexes.Add(2); }
		if (SP->Sexes.IsEmpty()) { SP->Sexes.Add(1); }
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		// `approach0` — контрольный прогон прежнего порта: подход ради навыка ждёт стоп-точку автоатаки (дефект виден как ЗАЯВКА_НЕ_УШЛА).
		SP->CVars->Set(TEXT("asda.Skill.ApproachOverAttack"), KA.Pos.Contains(TEXT("approach0")) ? 0 : 1);
		// ★ 04.11.2026 (яркость эффектов навыков): `nolight` — эффект-свет выключен (вклад материала отдельно от освещения пола);
		//   `preset=N` — пресет качества 0..4 (`scalability N`) на время прогона.
		if (KA.Pos.Contains(TEXT("nolight"))) { SP->CVars->Set(TEXT("asda.Fx.Light"), 0); }
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("map="), ESearchCase::IgnoreCase)) { SP->MapId = FCString::Atoi(*P.Mid(4)); }
			if (P.StartsWith(TEXT("mob="), ESearchCase::IgnoreCase)) { SP->MobType = FMath::Max(1, FCString::Atoi(*P.Mid(4))); }
			if (P.StartsWith(TEXT("tile="), ESearchCase::IgnoreCase))
			{
				FString X, Y; if (P.Mid(5).Split(TEXT(","), &X, &Y)) { SP->HomeTile = FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)); }
			}
			if (P.StartsWith(TEXT("preset="), ESearchCase::IgnoreCase) && GEngine)
			{
				GEngine->Exec(W, *FString::Printf(TEXT("scalability %d"), FCString::Atoi(*P.Mid(7))));
			}
		}
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
		// ★ 10.11.2026 (ревизия 5): выпуски урона по ОСНОВНОЙ цели текущего навыка — проверка долей 911 и «MISS».
		{
			TWeakPtr<FState> WSp = SP;
			SP->HitH = Fx->OnSkillHitMoment.AddLambda([WSp](const UAsdaNetSubsystem::FAsdaSkillHitRec& Rec, bool bMiss)
			{
				TSharedPtr<FState> St = WSp.Pin();
				if (!St || Rec.SkillId != St->Cur.Skill) { return; }
				const bool bMain = St->bTgtChar ? (Rec.TargetKind == 1 && Rec.TargetIdx == kVictim) : (Rec.TargetKind == 0 && Rec.TargetIdx == kMobRid);
				if (bMain) { St->Cur.MainHits.Emplace(Rec.Damage, bMiss); }
			});
		}
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
