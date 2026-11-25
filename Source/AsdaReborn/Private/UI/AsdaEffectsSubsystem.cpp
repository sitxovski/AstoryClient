// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaEffectsSubsystem — реализация. Полный разбор эталона — в шапке AsdaEffectsSubsystem.h
//   (первоисточник: Reports/ministate_win_re_B.md + RE/etalon/{skill_buff,entity_char,network,ui_core}.c).
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Data/AsdaFxLoopRows.h"   // ★ дедуп №2 В2-C (F3): AsdaFxLoop::kTablePath
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaSoulStoneSubsystem.h"   // ★ 06.11.2026: цвет нотиса 8155
#include "UI/AsdaSoulSlotSubsystem.h"   // ★ 06.11.2026: запуск и конец слотов Дружбы
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                      // ★ Аудит №3 (A5): единые читалки сырой записи вместо своей
#include "UI/AsdaMessageSubsystem.h"
#include "AsdaEffectActor.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaCombatTrace.h"          // журнал боя: эффекты состояний (наложены/сняты/истекли)
#include "AsdaEffectsSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaEffectsSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaBuff);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

const TArray<FAsdaEffectEntry> UAsdaEffectsSubsystem::EmptyList;

// ★ 25.10.2026 [ОРИГИНАЛ] ключ восстановления своих состояний из снимка 1021 (баг «зелья на час пропадают после перезапуска»).
//   1 — как эталон `char_restoreSkillEffectsAndMounts@0xA2C670`: 28 состояний навыков и 30 функциональных предметов с остатком
//   из БД ложатся на первой своей 4003, нулевые записи пропускаются ([ИСПРАВЛЕНИЕ] М-1, см. `AsdaEnterWorldSkipZeroRecords`);
//   2 — то же строго как оригинал (нулевые тоже накладываются); 0 — прежнее поведение порта (снимок не читается, список пуст).
static ASDA_DEV_CVAR(int32, CVarBuffEnterWorldRestore, TEXT("asda.Buff.EnterWorldRestore"), 1,
	TEXT("Вход в мир: восстановить свои действующие состояния (зелья, баффы навыков, транспорт) из снимка 1021 ")
	TEXT("(1 = как оригинал + [ИСПРАВЛЕНИЕ] нулевые записи пропускаются, 2 = строго как оригинал — нулевые тоже накладываются, ")
	TEXT("0 = прежнее поведение порта — не восстанавливать)"), ECVF_Default);

// Включено ли восстановление (аксессор — имя ключа в рабочем коде не ищется по строке).
static bool AsdaEnterWorldRestoreOn()
{
	return CVarBuffEnterWorldRestore.GetValueOnGameThread() != 0;
}

// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (ревизия волны, М-1): пропускать ли нулевые записи снимка 1021. LoginManager копирует 336/300 байт
//   из обнулённого буфера БЕЗУСЛОВНО (`for j<336`, `for k<300`), а `UPCharStatus` — без `IF EXISTS`: нет строки `CharStatus` —
//   28 записей приходят нулями, и эталон (`id >= 0`) накладывает состояние id 0 (оно в наборе контроля `HasControlState`) с
//   остатком 0; то же с нулевым слотом предмета (код 0 проходит гейт `>= 0`). Ключ 1 — пропуск (по умолчанию), 2 — как оригинал.
static bool AsdaEnterWorldSkipZeroRecords()
{
	return CVarBuffEnterWorldRestore.GetValueOnGameThread() == 1;
}

// ★★★ 04.11.2026 (status_effect_expiry) [ИСПРАВЛЕНИЕ ДЕФЕКТА] asda.Buff.MobStaleHitGate: 1 — the hit-frame state of our skill
//   (`Skill_ApplyEffectResult`) is NOT re-added when the server's 4017 already dropped it after the cast result arrived
//   (otherwise it stayed forever: 4017 comes only on change); 0 — as the original/previous port (always add).
// ★ 06.11.2026 [ОРИГИНАЛ]: 5306 снимает и слоты урона по времени. Эталон: `netClearAllCharacterBuffsPacket@0xF4F550` →
//   `charClearAllSkillEffects@0xA2BB20` (entity_char.c:25697): для КАЖДОЙ записи списка гасит эффекты и обнуляет 28-байтный слот
//   DoT с индексом id записи. Порт снимал только запись, слот 6079 жил дальше: после итога дуэли (сервер шлёт 5306 обоим,
//   `sub_47A830(…, 10)` снял все состояния) урон по времени продолжал тикать и вычитать HP (живой журнал 01.11 11:43:36+).
// ★ 06.11.2026 [ОРИГИНАЛ]: завершение слотов Дружбы на 5306 и 5230. Эталон: `CCharFx_SoulRite_EndAllSlots@0xAD3CB0` →
//   `CCharFx_SoulRite_EndSlot@0xAD3750` по слотам 0…29; 5230 зовёт `EndSlot` для одного слота (`netRemoveSkillBuffState@0xF50440`).
//   Что из него есть в порте: у слота 0 в состоянии 2 у своего персонажа — нотис 8155 в чат. Остальное нечем выполнять: запуск
//   слотов (`skillEffectInitAnimEventTable@0xAD34A0`, 5229 → `invSlotActivateAndDispatch@0xAD3630`) не перенесён, поэтому нет
//   объекта `g_pSoulBaseMgr` (эффект `soulritec-a_04` и звук `S_c_SMSoulRecal03` стоят в его позиции) и хэндлов слота 3 (+113224/+113254).
//   Окно-призрак 9125 (`uiDlg_ClearControlGroup`) гасит `UAsdaSoulSlotSubsystem::EndSlot`. 1 — как эталон; 0 — прежний порт, нотиса нет.
static ASDA_DEV_CVAR(int32, CVarSoulSlotEnd, TEXT("asda.Soul.SlotEnd"), 1,
	TEXT("[ОРИГИНАЛ] Завершение слота Дружбы 0 на 5306/5230: 1 = нотис 8155 своему, как эталон; 0 = прежний порт."),
	ECVF_Default);

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] (решение владельца): нотис 8155 «Твой Друг вошел в игру. Дух исчезает.» только на 5230.
//   Оригинал выводит его и на 5306 (итог дуэли, смерть), хотя Друг никуда не входил. 1 — исправлено; 0 — как оригинал, нотис и на 5306.
static ASDA_DEV_CVAR(int32, CVarSoulNoNoticeOn5306, TEXT("asda.Soul.NoNoticeOn5306"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Нотис 8155 на 5306: 1 = не выводить (ложный текст), 0 = выводить, как оригинал."),
	ECVF_Default);

// ★ 06.11.2026 [ОРИГИНАЛ]: 5306 гасит эффекты и у записей типа 2, сами записи живут. Эталон `charClearAllSkillEffects@0xA2BB20`:
//   цикл по ВСЕМ записям гасит rec+24/rec+28, и только потом `effectListUpdateStopByOwner@0xF36830` удаляет всё, кроме типа 2.
//   Заново эффект записи не поднимается: тик подвешенных эффектов берёт только тип 0. 1 — как эталон; 0 — прежний порт, эффект живёт.
static ASDA_DEV_CVAR(int32, CVarFxStopType2On5306, TEXT("asda.Fx.StopType2On5306"), 1,
	TEXT("[ОРИГИНАЛ] 5306: 1 = гасить эффекты и у записей типа 2, как эталон; 0 = прежний порт (эффект записи типа 2 живёт)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarDotClearOn5306, TEXT("asda.Dot.ClearOn5306"), 1,
	TEXT("[ОРИГИНАЛ] 5306 (сброс всех эффектов персонажа): 1 = слоты урона по времени записей списка обнуляются (charClearAllSkillEffects); ")
	TEXT("0 = прежний порт: слот 6079 тикает до конца срока."),
	ECVF_Default);

// Все байты записи снимка нулевые — «строки в БД нет», а не настоящее состояние.
static bool AsdaAllZero(const uint8* P, int32 N)
{
	for (int32 i = 0; i < N; ++i) { if (P[i] != 0) { return false; } }
	return true;
}

// ═════════════════════════════ жизненный цикл ══════════════════════════════════════════════════
// ★ Дедуп №2, В2-C (F3): удержание DT_FxLoop на сессию — разбор у объявления. Путь — тот же, что у эффект-актора.
int32 UAsdaEffectsSubsystem::HoldFxLoopTable()
{
	static const FName Group(TEXT("FxLoop"));
	if (HeldTables.IsHeld(Group)) { return HeldTables.GroupNum(Group); }   // быстрый путь: зовётся при каждом чтении таблицы
	const TCHAR* const Paths[] = { AsdaFxLoop::kTablePath };
	return HeldTables.Hold(Group, Paths);
}

void UAsdaEffectsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());

	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->OnEffectMsg.AddUObject(this, &UAsdaEffectsSubsystem::OnEffectMsg);
		Net->OnEventBonus.AddUObject(this, &UAsdaEffectsSubsystem::OnEventBonusPkt);
		Net->OnEventSet.AddUObject(this, &UAsdaEffectsSubsystem::ApplyEventSet);   // 6039/6041
		// ★ Носитель ушёл из AoI / умер / вышел из игры (4011) — эталон гасит его эффекты состояний
		//   вместе с ним: у сущности `Skill_Buff_TickAll@0xF83A00` (state == 10 ⇒ все хэндлы в
		//   `CEffectMgr_StopByOwner`), у ИГРОКА — `Recv_LogOutPacket@0xF3AE00` → `CharManager_RemoveNode`,
		//   который убивает сам объект персонажа вместе со списком состояний.
		//   ★★★ Раньше здесь висел ТОЛЬКО `ClearMobEffects` ⇒ у ушедшего игрока эффекты состояний
		//   не гасились никогда (П6): текли и записи `Lists`, и акторы.
		Net->OnEntityDespawn.AddUObject(this, &UAsdaEffectsSubsystem::OnEntityDespawn);
		// ★★★ ВОЛНА 2: 5473 — «сосед применил гачу». Это ЕДИНСТВЕННЫЙ канал, которым эффект удачи
		//   доезжает до окружающих: сервер сам его не рассылает, он ждёт от автора запрос 5474
		//   (`Recv_GachaItemEffect@0x4F58D0` → `SendPacket_ClippingSectorInOtherUser@0x41EA80`,
		//   9 секторов, отправитель исключён). Гейта «свой/чужой» в приёмнике эталона нет и не нужно:
		//   свой эффект клиент играет локально, а сюда приходят только чужие.
		Net->OnOtherPlayerBoost.AddUObject(this, &UAsdaEffectsSubsystem::OnOtherPlayerBoost);
		Net->OnSelfSpawn.AddUObject(this, &UAsdaEffectsSubsystem::OnSelfSpawnCarry);   // ★ 22.10.2026: перенос своего списка (ВАЖНО-1)
		Net->OnVitalsStateFx.AddUObject(this, &UAsdaEffectsSubsystem::OnVitalsStateFx);   // ★ R5-fix6 (13.11.2026): эффект состояния из 4058/4059
	}
	LoadEventSchedule();
}

void UAsdaEffectsSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnEffectMsg.RemoveAll(this);
			Net->OnEventBonus.RemoveAll(this);
			Net->OnEventSet.RemoveAll(this);
			Net->OnEntityDespawn.RemoveAll(this);
			Net->OnOtherPlayerBoost.RemoveAll(this);
		}
	}
	// Эффекты состояний живут дольше кадра — гасим их явно, иначе останутся висеть в мире.
	for (TPair<int32, FAsdaEffectList>& It : Lists)    { for (FAsdaEffectEntry& E : It.Value.Items) { StopBuffFx(E); } }
	for (TPair<int32, FAsdaEffectList>& It : MobLists) { for (FAsdaEffectEntry& E : It.Value.Items) { StopBuffFx(E); } }
	Lists.Reset();
	MobLists.Reset();
	ForgetMobServerStates(-1);   // ★ 04.11.2026 (status_effect_expiry)
	BuffFxWaitSince.Reset();
	Super::Deinitialize();
}

// ★★★ К-3 / находка №3 отчёта `17_effects.md`: визуал состояний не переживал смену карты.
//   Оригиналу этого не требуется: там объект персонажа гибнет вместе со списком эффектов
//   (`Recv_LogOutPacket@0xF3AE00` → `CharManager_RemoveNode`), а травела UE в оригинале нет вовсе.
//   У нас подсистема живёт на `GameInstance`, эффект-акторы умирают с уровнем — и записи
//   оставались с `bFxSpawned == true` и протухшими слабыми `Fx0/Fx1`, из-за чего `SpawnBuffFx`
//   выходил первой же строкой (`:967`) и визуал не восстанавливался НИКОГДА.
void UAsdaEffectsSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ 29.10.2026: реквизит эмоций — разовые акторы уровня; при любой смене карты/сессии гаснуть им уже нечем и незачем.
	EmoteProps.Reset();
	// ★ 21.11.2026 [ИСПРАВЛЕНИЕ] (ревизия 49 С-1, P128): значок события Event.dat (6039/6041) относится к КАРТЕ — после портала
	//   (у Astory это новое соединение) и при полном сбросе он гаснет; сервер P128 на первом такте нового входа сам пришлёт состояние
	//   новой карты. Прежде значок монет с прошлой карты горел до конца сеанса на карте без события.
	bSetExpOn = false; SetExpMult = 1.f; bSetDropOn = false; SetDropMult = 1.f;
	if (AsdaIsFullSessionReset(Reason))
	{
		// полный сброс: и счастливый час (4301 придёт заново при следующем входе в мир)
		bHourExpOn = false; HourExpMult = 1.f; bHourDropOn = false; HourDropMult = 1.f;
		bEventActive = false; EventType = 0; EventRemainMin = 0; EventRate = 1.f;
	}
	RecombineEventBonus();
	OnEventBonusChanged.Broadcast();
	if (!AsdaIsFullSessionReset(Reason))
	{
		// СМЕНА КАРТЫ. Сами состояния сервер сохраняет (после портального логона он их
		// не переприсылает), поэтому СПИСКИ НЕ ТРОГАЕМ — иначе игрок потеряет действующие баффы.
		// Снимаем ровно признак «визуал уже создан»: акторы умерли вместе с уровнем, и следующий
		// проход `TickBuffFx` создаст их заново на новом уровне.
		int32 N = 0;
		for (TPair<int32, FAsdaEffectList>& It : Lists)
		{
			for (FAsdaEffectEntry& E : It.Value.Items)
			{
				if (E.bFxSpawned) { ++N; }
				E.bFxSpawned = false;
				E.Fx0.Reset();
				E.Fx1.Reset();
			}
		}
		// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] (ревизия стана, п. 10): списки ЧУЖИХ игроков прошлой карты снимаются. Прежде они переживали
		//   портал под старыми номерами, и если свой номер на новой карте совпадал с номером соседа с прошлой, его стан/заморозка
		//   «цеплялись» к своему персонажу (`asda.Move.StunHardStop`, гейты обездвиживания). Свой список переносится на новый номер
		//   (`CarriedSelfIdx`, `Tick`). Соседи новой карты приходят со своими состояниями заново (спавн 4011/5417).
		{
			const int32 SelfIdx = SelfUserIdx();
			int32 Dropped = 0;
			for (auto It = Lists.CreateIterator(); It; ++It)
			{
				if (It.Key() == SelfIdx) { continue; }
				Dropped += It.Value().Items.Num();
				It.RemoveCurrent();
			}
			CarriedSelfIdx = SelfIdx;
			if (Dropped > 0)
			{
				UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] смена карты: сняты состояния чужих игроков прошлой карты (%d записей); свой список (номер %d) переносится"),
					Dropped, SelfIdx);
			}
		}
		// Список мобов самолечится (`TickMobEffects` не находит носителя и зовёт `ClearMobEffects`),
		// но акторы прошлой карты нам всё равно не нужны — на новой карте мобы приезжают заново.
		MobLists.Reset();
		ForgetMobServerStates(-1);   // ★ 04.11.2026 (status_effect_expiry): mobs of the old map are gone
		BuffFxWaitSince.Reset();
		// ★ bowfx: мобы прошлой карты ушли вместе с их слотами урона; чужие игроки — тоже (их списки сняты выше). Свой слот
		//   переносится на новый номер вместе со списком (`OnSelfSpawnCarry`) — у эталона объект своего персонажа переживает карту.
		MobDotSlots.Reset();
		for (auto It = CharDotSlots.CreateIterator(); It; ++It) { if (It.Key() != SelfUserIdx()) { It.RemoveCurrent(); } }
		if (N > 0)
		{
			UE_LOG(LogAsdaBuff, Log,
				TEXT("[состояния] смена карты: признак «визуал создан» снят у %d записей — эффекты будут пересозданы"), N);
		}
		return;
	}

	// ПОЛНЫЙ СБРОС: сессия персонажа кончилась — состояний больше нет ни у кого.
	// ★ Свой индекс снимаем ДО очистки: он читается из `UAsdaNetSubsystem::SelfUserNum`, а сеть
	//   по порядку рассылки сбрасывается ПОСЛЕДНЕЙ (см. `UAsdaSessionResetSubsystem`), то есть
	//   здесь ещё валиден. ⚠ Ноль — ЗАКОННЫЙ номер (закон `self-usernum-zero-is-valid`),
	//   поэтому проверки «если 0 — пропустить» тут быть не должно.
	const int32 SelfIdx = SelfUserIdx();
	UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] сброс сессии (%s): списков игроков %d, мобов %d"),
		AsdaSessionResetReasonName(Reason), Lists.Num(), MobLists.Num());
	for (TPair<int32, FAsdaEffectList>& It : Lists)    { for (FAsdaEffectEntry& E : It.Value.Items) { StopBuffFx(E); } }
	for (TPair<int32, FAsdaEffectList>& It : MobLists) { for (FAsdaEffectEntry& E : It.Value.Items) { StopBuffFx(E); } }
	Lists.Reset();
	MobLists.Reset();
	ForgetMobServerStates(-1);   // ★ 04.11.2026 (status_effect_expiry)
	BuffFxWaitSince.Reset();
	MobDotSlots.Reset();    // ★ bowfx: слоты урона по времени — часть объектов сущностей/персонажей, сессия кончилась
	CharDotSlots.Reset();
	CarriedSelfIdx = INDEX_NONE;
	NextExpireCheckS = 0.0;
	NextClockS = 0.0;
	// ★ 25.10.2026 [ОРИГИНАЛ]: сброс по 1021 — снимок нового входа уже записан сетью, его состояния ложатся на первой своей 4003
	//   (`OnSelfSpawnCarry`). Любой другой полный сброс (разрыв, выход в выбор) прежнюю заявку снимает.
	bEnterWorldRestorePending = (Reason == EAsdaSessionResetReason::EnteringWorld) && AsdaEnterWorldRestoreOn();
	if (bEnterWorldRestorePending)
	{
		UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] вход в мир (1021): действующие состояния из снимка лягут на первой своей 4003"));
	}
	// Плашка состояний должна опустеть немедленно. Делегат несёт (индекс игрока, с таймерами),
	//   как в остальных точках файла — сигнатуру не меняем.
	OnEffectsChanged.Broadcast(SelfIdx, /*bWithTimers=*/true);
}

// ★★★ УДАЛЕНО 2026-09-02 (правка К-1, аудит `audit_2026-09-02`): здесь лежал ВТОРОЙ порт той же
//   эталонной `charHasAnyEquippedItemType@0xA38E40` — `HasControlDebuff`. Он сверял ТОЛЬКО `E.Id`
//   и не смотрел `E.Type`, тогда как эталон ищет через `CList_FindByTypeSub@0xF36D90`
//   (`RE/etalon/entity_char.c:52813-52836`), а та сверяет И `node+18` (id), И `node+20` (type):
//       if ( node && *(u8*)(node + 20) == a3 && *(i16*)(node + 18) == a2 ) return node;
//   Записи type 1 (соулмейт) и type 2 (предметный усилитель) лежат в ТОМ ЖЕ списке
//   (`charStatusAddType1@0xF35B90`, `charStatusAddType2@0xF35CC0`), поэтому у 28 записей
//   `special_itemlist` с SubType 0/1/3/4 — зелья физ. атаки (60/61/62/251/261/328-330), зелья
//   защиты (66-70), ускорение (194) — совпадал один только id, и клиент считал себя обездвиженным.
//   Следствие: выпил зелье → гейт перед 4028 (`AsdaNetSubsystem.cpp`) перестаёт слать удары.
//   Дубль снят целиком (закон дедупа общих точек), единственная точка проверки — `HasControlState`
//   ниже: она зовёт `FindByTypeSub(UserIdx, Id, 0)`, то есть сверяет ОБА поля, как эталон.
int32 UAsdaEffectsSubsystem::SelfUserIdx() const
{
	// ★ Волна 1 (14.09.2026, В9 / исправление E2): общая точка `AsdaSelf::GetUserIdxOr`. До пакета 4003
	//   своего номера нет — INDEX_NONE, а не 0 (0 — законный номер ЧУЖОГО игрока, закон `charid-vs-usernum`).
	//   Ключ отката ядра `asda.Self.StrictUnknownNum` на выборку номера не действует (`AsdaSelf.h`).
	return AsdaSelf::GetUserIdxOr(GetGameInstance(), INDEX_NONE);
}

FAsdaEffectList& UAsdaEffectsSubsystem::ListFor(int32 UserIdx)
{
	return Lists.FindOrAdd(UserIdx);
}

const TArray<FAsdaEffectEntry>& UAsdaEffectsSubsystem::GetList(int32 UserIdx) const
{
	if (const FAsdaEffectList* L = Lists.Find(UserIdx)) { return L->Items; }
	return EmptyList;
}

// ★★★ ЭТАЛОННАЯ ГЕОМЕТРИЯ КВАДОВ/КОЛЕЦ У КАНАЛА ПРЕДМЕТОВ (2026-08-14, `item_fx_audit.md` §10).
//   ЧТО ВКЛЮЧАЕТ. Акторный флаг `bStrictQuadAttach` переводит квад и кольцо на ветку, реконструи-
//   рованную ПО ДИЗАСМУ, и снимает два наследия картовой ветки:
//     (A) `if (bAttachedQuad && !bStrictAtt) G = RY(G, PI)` — «швартовка» на 180°;
//     (B) `bEtalonMapPath = … || bStrictQuadAttach` — офсет кольца начинает вращаться канальными
//         углами (двойное R), а не прибавляться после них.
//   ДОКАЗАТЕЛЬСТВО (обе draw-функции прочитаны целиком, см. §10.1 отчёта):
//     `effectQuad_updateAndDrawBillboard@0x120AEB0` — мировая матрица квада собирается как
//       `roll(356)·RotX(364)·RotY(372)·RotZ(380)·N48·N520`; ни одной константы π в теле нет,
//       а ВЕРШИНЫ строятся ТОЛЬКО из scale (`spawnPosCur` = ±gap_234[332]/[340]) ⇒ pos-канал в
//       геометрию не входит вовсе и идёт отдельной трансляцией `gap_234[224..232]`.
//     `effectTrailRender@0x1215C10` (кольцо) — `roll·S·RotX·RotY·RotZ·N48·N520`, π тоже нет, а
//       pos-канал ЯВНО прогоняется через матрицы `RotX·RotY·RotZ` (дважды) и `N48`, после чего
//       складывается с basePos, attach-позицией и дрейфом узла — хвост трансляции у обеих функций
//       БАЙТ-В-БАЙТ одинаков, то есть модель подтверждена двумя независимыми функциями.
//   ПОЧЕМУ ЭТО ЗАКОННО ДЛЯ ПРЕДМЕТОВ. В оригинале ветка отрисовки квада/кольца ОДНА и от кода
//   привязки (5/7/10) НЕ ЗАВИСИТ — attach выбирает лишь матрицу узла N520. Тот же довод уже принят
//   для канала НАВЫКОВ (`asda.Skill.StrictFx`) и для свечения заточки.
//   ОТКАТ: `asda.Item.StrictFx 0` — флаг ставится ТОЛЬКО на акторы, спавнимые `SpawnItemUseFx` /
//   `SpawnDummyBoneFx` (канал предметов); карты, крылья, соулмейт, маунт и башня alpen не затронуты.
bool GAsdaItemFxStrictQuad = true;
static FAutoConsoleVariableRef GItemFxStrictQuadRef(
	TEXT("asda.Item.StrictFx"),
	GAsdaItemFxStrictQuad,
	TEXT("Предметы: эталонная геометрия квадов/колец (1 = канон 0x120AEB0/0x1215C10 — двойное R у ")
	TEXT("офсета кольца и без π-швартовки квада; 0 = прежняя картовая ветка)"));

// Эталон: buffListTickDecrement каждый кадр (entity_char.c:7695) + charBuffUpdateExpired раз в 1000 мс
//   (entity_char.c:7681) + gameUi_updateScheduledClockDisplay раз в 60 000 мс (game_main.c:1428).
// ★ 22.10.2026 (ревизия локального старта, ВАЖНО-1) — разбор у объявления. Пока 4003 не пришла, список лежит под номером прошлой
//   карты, и `SelfUserIdx()` (номер ещё старый) находит его — поведение до прихода 4003 прежнее.
void UAsdaEffectsSubsystem::OnSelfSpawnCarry()
{
	const int32 Self = SelfUserIdx();
	if (CarriedSelfIdx != INDEX_NONE)
	{
		const int32 Was = CarriedSelfIdx;
		CarriedSelfIdx = INDEX_NONE;
		FAsdaEffectList Carried;
		if (Self >= 0 && Self != Was && Lists.RemoveAndCopyValue(Was, Carried))
		{
			FAsdaEffectList& Dst = Lists.FindOrAdd(Self);
			Dst.Items.Append(Carried.Items);
			UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] 4003: свой номер %d → %d — перенесено записей %d"), Was, Self, Carried.Items.Num());
		}
		// ★ bowfx: свои слоты урона по времени едут вместе со списком (тот же объект персонажа у эталона).
		TMap<int32, FAsdaDotSlot> CarriedDots;
		if (Self >= 0 && Self != Was && CharDotSlots.RemoveAndCopyValue(Was, CarriedDots)) { CharDotSlots.Add(Self, MoveTemp(CarriedDots)); }
	}
	// ★ 25.10.2026 [ОРИГИНАЛ]: первая своя 4003 после 1021 — действующие состояния из снимка 1021 (разбор у объявления в заголовке).
	//   Эталон делает это при создании локального игрока (`game_main.c:2813-2816`), то есть когда свой персонаж появляется в мире;
	//   у нас номер своего персонажа для ключа списка известен только с этой 4003 (`SelfUserNum` пишется строкой выше рассылки).
	if (bEnterWorldRestorePending)
	{
		bEnterWorldRestorePending = false;
		UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (Net && Self >= 0 && AsdaEnterWorldRestoreOn())
		{
			ApplyEnterWorldStates(Self, Net->EnterWorldSnapshot);
		}
		else
		{
			UE_LOG(LogAsdaBuff, Warning, TEXT("[состояния] 4003 после 1021: восстановление не выполнено (сеть %s, свой номер %d)"),
				Net ? TEXT("есть") : TEXT("нет"), Self);
		}
	}
	// ★★★ 30.10.2026 [ОРИГИНАЛ] ТРАНСПОРТ ИЗ СВОЕГО СПИСКА (`asda.Mount.PortalReapply`; баг «прошёл портал верхом — транспорт пропал»).
	//   Портальный загрузчик оригинала, шаг 12 (game_main.c:3762), зовёт `CCharacter_StopAllActiveBuffActions@0xA2C480`
	//   (entity_char.c:25924): для записи type 2 с id 15 — `CCharacter_SetParts_RidingSystem(char, node+32)`, то есть транспорт
	//   локального игрока вешается заново из его же списка; сервер своему клиенту транспорт при портале не присылает
	//   (`CharPortalMake@0x5444A0` шлёт себе только 4003). Список у нас переехал на новый номер строкой выше, меш вешает мир
	//   по тому же делегату, что 5453/6154/1021 (павна ещё нет — мир запомнит код и применит при спавне).
	if (Self >= 0 && AsdaMountPortalReapplyOn())
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const FAsdaEffectList* L = Lists.Find(Self);
		for (int32 i = 0; Net && L && i < L->Items.Num(); ++i)
		{
			const FAsdaEffectEntry& E = L->Items[i];
			if (E.Type != 2 || E.Id != 15 || E.ItemCode <= 0) { continue; }
			UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] 4003: транспорт из своего списка — предмет %d (свой номер %d)"), E.ItemCode, Self);
			Net->OnMountState.Broadcast(Self, E.ItemCode, true);
			break;
		}
	}
	// ★ 06.11.2026 [ОРИГИНАЛ]: тот же шаг загрузчика зовёт обработчики записей типа 1 (разбор в `AsdaSoulSlotSubsystem.h`).
	if (UAsdaSoulSlotSubsystem* Slots = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr) { Slots->OnSelfSpawnCarry(Self); }
	// `inventoryFindEquippedType32Storage` после загрузки карты; свой номер слота уже перенесён.
	if (UAsdaSoulStoneSubsystem* Stone = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr) { Stone->OnWorldReady(); }
}

// ★★★ 25.10.2026 [ОРИГИНАЛ] — разбор у объявления. Раскладка снимка 1021 (тело, 686 б) — по ДВУМ независимым источникам:
//   отправитель LoginManager `sub_40AD30` (`login_work/LoginManager.c:4496-4520`: `v107` = тело+0x20, `UPCharStatus` 28 записей
//   по 12 б, затем `v107[84]` = тело+368 — `UPCharItemStatus` 30 записей по 10 б, БЕЗ уплотнения, индекс = слот) и приёмник
//   эталона (`netSendEnterWorldHandshake@0x971E10`: `memcpy(unk_290658F, тело, 0x2AE)`; `g_aSkillEffectTbl@0x29065AF` = +0x20,
//   `g_aCharSelectEnterWorld@0x29066FF` = +0x170 = +368). Поля записи навыка — колонки `CharStatus` (`Skillidx` i16 @0,
//   `Mskillidx` i16 @2, `Sub` @4, `Count` u8 @5, `Type` u8 @6, `Time` i16 @7, `Lvl` u8 @9, `Evalue` @10), их же читает
//   `buffList_AddOrUpdate@0xF35810` (+2/+5/+6/+7/+9). Запись предмета — `CharSkillItem` (`Code` i32, `Time` i32 сек, `Value` i16);
//   пустой слот сервер отдаёт `-1, 0, -1`.
int32 UAsdaEffectsSubsystem::ApplyEnterWorldStates(int32 SelfIdx, const TArray<uint8>& S)
{
	constexpr int32 kSkillOff = 32;    // начало 28 записей состояний навыков
	constexpr int32 kSkillNum = 28;    // записей состояний навыков
	constexpr int32 kSkillRec = 12;    // байт на запись
	constexpr int32 kItemOff  = 368;   // начало 30 слотов функциональных предметов
	constexpr int32 kItemNum  = 30;    // слотов (индекс = тип-слот, 15 — транспорт)
	constexpr int32 kItemRec  = 10;    // байт на слот
	if (SelfIdx < 0) { return 0; }
	if (S.Num() < kItemOff + kItemNum * kItemRec)
	{
		UE_LOG(LogAsdaBuff, Warning, TEXT("[состояния] снимок 1021 короче %d б (%d) — восстанавливать нечего"), kItemOff + kItemNum * kItemRec, S.Num());
		return 0;
	}
	const uint8* B = S.GetData();
	int32 NSkill = 0, NItem = 0, NZero = 0;
	const bool bSkipZero = AsdaEnterWorldSkipZeroRecords();
	// Эталон, первый цикл `char_restoreSkillEffectsAndMounts`: `if (g_aSkillEffectTbl[6*i] >= 0) cChar_restartSkillEffectByType(...)`
	//   — пустые записи ПРОПУСКАЮТСЯ (терминатора, как у 5418, здесь нет); гейт самого `restart` — id < 350 и id != 177.
	for (int32 i = 0; i < kSkillNum; ++i)
	{
		const uint8* R = B + kSkillOff + kSkillRec * i;
		const int32 Id = AsdaBytes::RdI16(R);
		if (Id < 0 || Id >= 350 || Id == 177) { continue; }
		if (bSkipZero && AsdaAllZero(R, kSkillRec)) { ++NZero; continue; }   // ★ [ИСПРАВЛЕНИЕ] М-1: нулевая запись — строки CharStatus нет
		AddOrUpdateSkill(SelfIdx, Id, AsdaBytes::RdI16(R + 2), (int32)R[5], (int32)R[6], AsdaBytes::RdI16(R + 7), (int32)R[9]);
		++NSkill;
		UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] 1021 → состояние навыка %d: стек=%d вид=%d остаток=%d с"), Id, (int32)R[5], (int32)R[6], AsdaBytes::RdI16(R + 7));
	}
	// Второй цикл эталона: `if (*(int*)(+1288 + 10*j) >= 0) cCharStatusAddTypeMount(this, j, остаток, код)` — ключ записи = СЛОТ j
	//   (как у 5418), таблица `special_itemlist` не участвует; визуал зелья (`CharEffect_SpawnPMBuff_*`) эталон здесь НЕ зовёт.
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	for (int32 k = 0; k < kItemNum; ++k)
	{
		const uint8* R = B + kItemOff + kItemRec * k;
		const int32 Code = AsdaBytes::RdI32(R);
		if (Code < 0) { continue; }
		if (bSkipZero && AsdaAllZero(R, kItemRec)) { ++NZero; continue; }    // ★ [ИСПРАВЛЕНИЕ] М-1: нулевой слот — строки CharSkillItem нет
		const int32 Dur = AsdaBytes::RdI32(R + 4);
		AddType2(SelfIdx, k, Dur, Code);
		++NItem;
		UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] 1021 → слот %d: предмет %d, остаток %d с, величина %d"), k, Code, Dur, (int32)AsdaBytes::RdI16(R + 8));
		// Слот 15 — транспорт: эталон цепляет меш в `cCharStatusAddTypeMount` (после загрузки модели — повтором
		//   `charReapplyActiveSkillEffects`, `entity_char.c:21249`). У нас меш вешает мир по тому же делегату, что и у 5418/6155;
		//   актора ещё нет — запомнит и применит при спавне.
		if (k == 15 && Net) { Net->OnMountState.Broadcast(SelfIdx, Code, true); }
	}
	UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] вход в мир: восстановлено из 1021 — состояний навыков %d, функциональных предметов %d, нулевых записей пропущено %d (свой номер %d)"),
		NSkill, NItem, NZero, SelfIdx);
	ASDA_CTRACE_UID(SelfIdx, EAsdaCT::Fx, TEXT("вход в мир: из 1021 восстановлено состояний навыков %d, предметов %d"), NSkill, NItem);
	OnEffectsChanged.Broadcast(SelfIdx, /*bWithTimers=*/true);
	return NSkill + NItem;
}

void UAsdaEffectsSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Effects");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	const int32 Self = SelfUserIdx();
	if (Self >= 0) { TickDecrement(Self); }
	// ★ 26.10.2026 (bowfx) [ОРИГИНАЛ]: слоты урона по времени тикают ПОКАДРОВО, накопитель в мс (`Skill_Buff_TickAll` / `charDot_TickAll`).
	TickDotSlots(DeltaTime * 1000.f);

	// ★ Отложенный спавн эффектов состояний: сетевой актор носителя мог ещё не появиться в момент
	//   прихода пакета (гонка порта, у эталона её нет — см. FAsdaEffectEntry::bFxSpawned).
	RetryPendingBuffFx();
	// ★ 29.10.2026: реквизит эмоций гаснет, как только носитель ушёл из своей эмоции (`CCharacter_UpdateHeldItemEffect`).
	TickEmoteProps();

	const double Now = FPlatformTime::Seconds();
	if (Now >= NextExpireCheckS)
	{
		NextExpireCheckS = Now + (double)FMath::Max(ExpireCheckPeriodSec, 0.05f);
		// Состояния СУЩНОСТЕЙ тикают независимо от локального игрока — эталон Skill_Buff_TickAll.
		TickMobEffects();
		if (Self >= 0)
		{
			UpdateExpired(Self);
			// Эталон: charBuffUpdateExpired ЗАВЕРШАЕТСЯ вызовом HUD_PlayerBuffTray_Update — то есть
			//   иконки И таймеры перерисовываются раз в секунду, а не покадрово.
			OnEffectsChanged.Broadcast(Self, /*bWithTimers=*/true);
		}
	}
	if (Now >= NextClockS)
	{
		NextClockS = Now + (double)FMath::Max(EventClockPeriodSec, 1.f);
		UpdateScheduledClock(-1);
	}
}

// ═════════════════════════════ модель списка ═══════════════════════════════════════════════════
FAsdaEffectEntry* UAsdaEffectsSubsystem::FindByTypeSub(int32 UserIdx, int32 Id, int32 Type)
{
	if (FAsdaEffectList* L = Lists.Find(UserIdx))
	{
		for (FAsdaEffectEntry& E : L->Items)
		{
			if (E.Type == Type && E.Id == Id) { return &E; }
		}
	}
	return nullptr;
}

const FAsdaEffectEntry* UAsdaEffectsSubsystem::FindByTypeSub(int32 UserIdx, int32 Id, int32 Type) const
{
	return const_cast<UAsdaEffectsSubsystem*>(this)->FindByTypeSub(UserIdx, Id, Type);
}

// ★★★ П11: «персонаж под контролем» — дословный порт `charHasAnyEquippedItemType@0xA38E40`
//   (entity_char.c:26480-26494). Порядок проверок сохранён байт в байт; обоснование имени-миснейма
//   и разбор `this + 638` — в объявлении (.h).
bool UAsdaEffectsSubsystem::HasControlState(int32 UserIdx) const
{
	// Ровно шесть id, ровно в этом порядке, все с Type == 0.
	static const int32 kIds[] = { 0, 1, 3, 14, 8, 4 };
	for (int32 Id : kIds)
	{
		if (FindByTypeSub(UserIdx, Id, 0)) { return true; }
	}
	return false;
}

// Эталон buffList_AddOrUpdate@0xF35810 (skill_buff.c:38179) — обе ветки (обновление и вставка) 1:1.
void UAsdaEffectsSubsystem::AddOrUpdateSkill(int32 UserIdx, int32 SkillId, int32 IdFlag, int32 Stack,
                                             int32 KindByte, int32 RemainSec, int32 Byte9, int32 CasterUserIdx)
{
	FAsdaEffectEntry* E = FindByTypeSub(UserIdx, SkillId, 0);
	ASDA_CTRACE_UID(UserIdx, EAsdaCT::Fx, TEXT("состояние %d %s у userIdx=%d%s: стек=%d вид=%d остаток=%d с байт9=%d idFlag=%d кастер=%d"),
		SkillId, E ? TEXT("ОБНОВЛЕНО") : TEXT("НАЛОЖЕНО"), UserIdx, UserIdx == SelfUserIdx() ? TEXT(" (Я)") : TEXT(""),
		Stack, KindByte, RemainSec, Byte9, IdFlag, CasterUserIdx);
	if (!E)
	{
		FAsdaEffectList& L = ListFor(UserIdx);
		E = &L.Items.AddDefaulted_GetRef();
	}
	// ★★★ ПЕРЕЗАПУСК ВИЗУАЛА СОСТОЯНИЯ 1:1 с `cChar_restartSkillEffectByType@0xA2AD60`: СНАЧАЛА
	//   гасим прежние хэндлы (`rec+24`/`rec+28` → `CEffectMgr_StopByOwner`, поля в −1), и только
	//   потом обновляем запись и зовём обработчик состояния заново.
	StopBuffFx(*E);
	E->bFxSpawned = false;
	E->CasterUserIdx = CasterUserIdx;
	E->Id        = SkillId;   // +18
	E->Type      = 0;         // +20
	E->IconStack = 0;         // +17
	E->IdFlag    = IdFlag;    // +2
	E->Byte9     = Byte9;     // +9
	E->RemainSec = RemainSec; // +7
	E->KindByte  = KindByte;  // +6
	E->Stack     = Stack;     // +5
	if (RemainSec < 60) { E->ShortFlag = 1; }   // +21 (эталон ставит 1 и НЕ снимает)
	E->LastTickMs = AsdaNowMs();                // +12
	// ★★★ ТРЕТИЙ КАНАЛ: обработчик состояния (`pChar + 52304 + buffId`) — спавн `.jeb` по DT_BuffFx.
	SpawnBuffFx(*E, UserIdx, /*bMob=*/false);
	OnEffectsChanged.Broadcast(UserIdx, /*bWithTimers=*/false);
}

// Эталон charStatusAddType1@0xF35B90 (entity_char.c:52662): kind=0, byte9=99, stack=0, idFlag=0.
void UAsdaEffectsSubsystem::AddType1(int32 UserIdx, int32 Slot, int32 DurationSec)
{
	FAsdaEffectEntry* E = FindByTypeSub(UserIdx, Slot, 1);
	if (!E)
	{
		FAsdaEffectList& L = ListFor(UserIdx);
		E = &L.Items.AddDefaulted_GetRef();
	}
	E->Id = Slot; E->Type = 1; E->IconStack = 0; E->IdFlag = 0; E->Byte9 = 99;
	E->RemainSec = DurationSec; E->KindByte = 0; E->Stack = 0;
	if (E->RemainSec < 60) { E->ShortFlag = 1; }
	E->LastTickMs = AsdaNowMs();
	OnEffectsChanged.Broadcast(UserIdx, false);
}

// Эталон charStatusAddType2@0xF35CC0 (entity_char.c:52708): kind=0, byte9=99, stack=0, idFlag=0;
//   ★ остаток переписывается ТОЛЬКО при DurationSec != 0 (0 = бессрочный усилитель).
void UAsdaEffectsSubsystem::AddType2(int32 UserIdx, int32 SubTypeId, int32 DurationSec, int32 ItemCode)
{
	FAsdaEffectEntry* E = FindByTypeSub(UserIdx, SubTypeId, 2);
	if (!E)
	{
		FAsdaEffectList& L = ListFor(UserIdx);
		E = &L.Items.AddDefaulted_GetRef();
	}
	E->Id = SubTypeId; E->Type = 2; E->IconStack = 0; E->IdFlag = 0; E->Byte9 = 99;
	if (DurationSec != 0) { E->RemainSec = DurationSec; }
	E->KindByte = 0; E->Stack = 0; E->ItemCode = ItemCode;
	E->LastTickMs = AsdaNowMs();
	OnEffectsChanged.Broadcast(UserIdx, false);
}

// Эталон buffListRemoveBySkillState@0xF36640 — удаляет ПЕРВУЮ запись с совпадением (id, type).
bool UAsdaEffectsSubsystem::RemoveBySkillState(int32 UserIdx, int32 Id, int32 Type)
{
	FAsdaEffectList* L = Lists.Find(UserIdx);
	if (!L) { return false; }
	for (int32 i = 0; i < L->Items.Num(); ++i)
	{
		if (L->Items[i].Type == Type && L->Items[i].Id == Id)
		{
			// ★★★ Эталон buffStopEffectsBySkillId@0xF83930 гасит `rec+24`/`rec+28` ПЕРЕД удалением
			//   записи — иначе эффект состояния остался бы висеть навсегда.
			ASDA_CTRACE_UID(UserIdx, EAsdaCT::Fx, TEXT("состояние %d (type %d) СНЯТО у userIdx=%d%s: остаток был %d с, эффект %s"),
				Id, Type, UserIdx, UserIdx == SelfUserIdx() ? TEXT(" (Я)") : TEXT(""), L->Items[i].RemainSec,
				(L->Items[i].Fx0.IsValid() || L->Items[i].Fx1.IsValid()) ? TEXT("погашен") : TEXT("не висел"));
			StopBuffFx(L->Items[i]);
			BuffFxWaitSince.Remove(AsdaBuffFxKey(UserIdx, /*bMob=*/false, Id));
			// ★ 26.10.2026 (bowfx) [ОРИГИНАЛ] `cChar_removeSkillEffect@0xA2B010` (entity_char.c:25601-25608): снятие состояния обнуляет
			//   и его слот урона по времени (`char + 226704 + 28·id`) — тики яда/крови кончаются вместе с состоянием.
			if (Type == 0) { ClearDotSlots(/*bMob=*/false, UserIdx, Id); }
			L->Items.RemoveAt(i);   // порядок остальных сохраняется = порядок STL-списка
			OnEffectsChanged.Broadcast(UserIdx, false);
			return true;
		}
	}
	return false;
}

// Эталон buffListClearAllStopFx@0xF36700 — удаляет ВСЁ, КРОМЕ type == 2.
void UAsdaEffectsSubsystem::ClearAllStopFx(int32 UserIdx)
{
	FAsdaEffectList* L = Lists.Find(UserIdx);
	if (!L) { return; }
	// ★ 06.11.2026: у эталона `EndAllSlots` идёт первым, запись слота 0 ещё в списке (нотис на 5306 — разбор у `asda.Soul.NoNoticeOn5306`).
	if (CVarSoulNoNoticeOn5306.GetValueOnGameThread() == 0) { EndSoulRecallSlot(UserIdx); }
	// ★ 06.11.2026 [ОРИГИНАЛ]: `CCharFx_SoulRite_EndAllSlots@0xAD3CB0` идёт до очистки списка (`netClearAllCharacterBuffsPacket@0xF4F550`).
	if (UAsdaSoulSlotSubsystem* Slots = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr) { Slots->EndAllSlots(UserIdx); }
	const int32 Was = L->Items.Num();
	// ★★★ Эталон buffListClearAllStopFx@0xF36700 — «StopFx» в имени не для красоты: перед удалением
	//   каждой записи он гасит её `rec+24`/`rec+28`.
	for (FAsdaEffectEntry& E : L->Items)
	{
		// ★ 06.11.2026 [ОРИГИНАЛ]: слот DoT — у каждой записи списка, без отбора по типу (разбор у `asda.Dot.ClearOn5306`).
		if (CVarDotClearOn5306.GetValueOnGameThread() != 0) { ClearDotSlots(/*bMob=*/false, UserIdx, E.Id); }
		if (E.Type != 2 || CVarFxStopType2On5306.GetValueOnGameThread() != 0)   // ★ 06.11.2026: разбор у ключа
		{
			StopBuffFx(E);
			BuffFxWaitSince.Remove(AsdaBuffFxKey(UserIdx, /*bMob=*/false, E.Id));
		}
	}
	L->Items.RemoveAll([](const FAsdaEffectEntry& E) { return E.Type != 2; });
	ASDA_CTRACE_UID(UserIdx, EAsdaCT::Fx, TEXT("сброс всех состояний у userIdx=%d%s: было %d, осталось %d (type 2 переживает)"),
		UserIdx, UserIdx == SelfUserIdx() ? TEXT(" (Я)") : TEXT(""), Was, L->Items.Num());
	UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] сброс всех у userIdx=%d: было %d, осталось %d (type 2 переживает очистку)"),
		UserIdx, Was, L->Items.Num());
	OnEffectsChanged.Broadcast(UserIdx, true);
}

// ★ 06.11.2026 Слот Дружбы 0 завершён (разбор у `asda.Soul.SlotEnd`). Отдельного состояния не держим: у своего персонажа состояние 2
//   слота 0 существует ровно столько, сколько запись типа 1 с id 0 (ставят 5229 и снапшот 6154, снимают 5230 и 5306; по времени она
//   не истекает). Зовётся ДО снятия записи. Текст, цвет и время — эталон: 8155, −11514, 10 000 мс, только чат.
void UAsdaEffectsSubsystem::EndSoulRecallSlot(int32 UserIdx)
{
	if (CVarSoulSlotEnd.GetValueOnGameThread() == 0 || UserIdx != SelfUserIdx() || !FindByTypeSub(UserIdx, 0, 1)) { return; }
	++SoulRecallEndNotices;
	const UGameInstance* GI = GetGameInstance();
	const FString Text = AsdaNotice::Loc(GI, 8155);
	if (Text.IsEmpty()) { AsdaNotice::Chat(GI, EAsdaMsgCategory::System, 8155); }
	else { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, AsdaNoticeColor::Gold, 10000.f); }
	UE_LOG(LogAsdaBuff, Log, TEXT("[состояния] слот Дружбы 0 завершён у userIdx=%d: нотис 8155"), UserIdx);
}

// Эталон netClearCharacterBuffsPacket@0xF4F5E0 (network.c:45100).
//   mode == 10: пропустить id 0x40/0x42/0x44/0x45 (64/66/68/69), снять всё с buff0SubType == 2;
//   иначе:      снять всё с buff0SubType ∈ {2,4,6} И buff0DamageType == mode.
void UAsdaEffectsSubsystem::ClearByMode(int32 UserIdx, int32 Mode)
{
	FAsdaEffectList* L = Lists.Find(UserIdx);
	if (!L) { return; }
	TArray<int32> ToRemove;
	for (const FAsdaEffectEntry& E : L->Items)
	{
		if (E.Type != 0) { continue; }   // эталон читает SkillRecord ⇒ работает только со скилловыми
		const FAsdaSkillEffectMeta& M = GetSkillMeta(E.Id);
		if (!M.bValid) { continue; }
		if (Mode == 10)
		{
			if (E.Id == 64 || E.Id == 66 || E.Id == 68 || E.Id == 69) { continue; }   // case '@','B','D','E'
			if (M.Buff0SubType == 2) { ToRemove.Add(E.Id); }
		}
		else
		{
			if ((M.Buff0SubType == 2 || M.Buff0SubType == 4 || M.Buff0SubType == 6)
			    && M.Buff0DamageType == Mode)
			{
				ToRemove.Add(E.Id);
			}
		}
	}
	ASDA_CTRACE_UID(UserIdx, EAsdaCT::Fx, TEXT("5307 сброс состояний по режиму %d у userIdx=%d: снимается %d"), Mode, UserIdx, ToRemove.Num());
	for (int32 Id : ToRemove) { RemoveBySkillState(UserIdx, Id, 0); }
	UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] 5307 режим=%d у userIdx=%d: снято %d"), Mode, UserIdx, ToRemove.Num());
	OnEffectsChanged.Broadcast(UserIdx, true);
}

// Эталон buffListTickDecrement@0xF36930 1:1, включая «остаток мс» (lastTickMs = now − (dt % 1000)).
void UAsdaEffectsSubsystem::TickDecrement(int32 UserIdx)
{
	FAsdaEffectList* L = Lists.Find(UserIdx);
	if (!L) { return; }
	const int64 Now = AsdaNowMs();
	for (FAsdaEffectEntry& E : L->Items)
	{
		if (E.KindByte == 4) { continue; }             // ★ постоянный эффект не тикает
		const int64 Dt = Now - E.LastTickMs;
		if (Dt >= 1000)
		{
			E.RemainSec -= (int32)(Dt / 1000);
			E.LastTickMs = Now - (Dt % 1000);
			// Эталон buffList_AddOrUpdate ставит shortFlag при остатке < 60 — держим согласованным.
			if (E.RemainSec < 60) { E.ShortFlag = 1; }
		}
	}
}

// Эталон charBuffUpdateExpired@0xA2BCF0 (entity_char.c:25745): всё с remainSec < 0 снимается.
void UAsdaEffectsSubsystem::UpdateExpired(int32 UserIdx)
{
	FAsdaEffectList* L = Lists.Find(UserIdx);
	if (!L) { return; }
	TArray<TPair<int32, int32>> Dead;   // (id, type)
	for (FAsdaEffectEntry& E : L->Items)
	{
		if (E.RemainSec >= 0) { continue; }
		if (E.Type == 0)
		{
			const FAsdaSkillEffectMeta& M = GetSkillMeta(E.Id);
			if (M.bValid && M.SkillSubType != 5)
			{
				if (E.RemainSec < 5) { E.IconStack = 1; }   // эталон: rec+17 = 1
				Dead.Emplace(E.Id, 0);
			}
		}
		else if (E.Type == 1 && E.Id != 0)
		{
			if (E.RemainSec < 5) { E.IconStack = 1; }
			Dead.Emplace(E.Id, 1);
		}
		else if (E.Type == 2)
		{
			// Эталон снимает по истечении ТОЛЬКО эти четыре id (зелья скорости / свитки возврата).
			if (E.Id == 265 || E.Id == 266 || E.Id == 52 || E.Id == 53) { Dead.Emplace(E.Id, 2); }
		}
	}
	if (Dead.Num() > 0)
	{
		ASDA_CTRACE_UID(UserIdx, EAsdaCT::Fx, TEXT("у userIdx=%d истекло состояний: %d — снимаются"), UserIdx, Dead.Num());
	}
	for (const TPair<int32, int32>& D : Dead)
	{
		// ★ 06.11.2026 [ОРИГИНАЛ]: истёкший слот Дружбы завершается (`charBuffUpdateExpired`, entity_char.c:25786).
		if (D.Value == 1) { if (UAsdaSoulSlotSubsystem* Slots = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr) { Slots->EndSlot(UserIdx, D.Key); } }
		RemoveBySkillState(UserIdx, D.Key, D.Value);
	}
	if (Dead.Num() > 0)
	{
		UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] истекло и снято: %d (userIdx=%d)"), Dead.Num(), UserIdx);
	}
}

// ═════════════════════════════ приём пакетов ═══════════════════════════════════════════════════
void UAsdaEffectsSubsystem::OnEffectMsg(const FAsdaEffectMsg& Msg)
{
	switch (Msg.Kind)
	{
	case EAsdaEffectMsgKind::SkillApply:
		// Эталон cChar_restartSkillEffectByType@0xA2AD60: id вне [0,350) или == 177 — игнор.
		if (Msg.Id < 0 || Msg.Id >= 350 || Msg.Id == 177) { break; }
		AddOrUpdateSkill(Msg.UserIdx, Msg.Id, Msg.IdFlag, Msg.Stack, Msg.KindByte, Msg.DurationSec, Msg.Byte9,
			Msg.CasterIdx);
		break;

	case EAsdaEffectMsgKind::MobSkillApply:
		// Эталон skillBuffAdd@0xF836E0: те же два отсева, что и у персонажа (>= 0x15E и == 177).
		if (Msg.Id < 0 || Msg.Id >= 350 || Msg.Id == 177) { break; }
		AddOrUpdateMobSkill(Msg.UserIdx, Msg.Id, Msg.Stack, Msg.DurationSec, Msg.CasterIdx);
		break;

	case EAsdaEffectMsgKind::SkillRemove:
		RemoveBySkillState(Msg.UserIdx, Msg.Id, 0);
		break;

	case EAsdaEffectMsgKind::SoulApply:
		AddType1(Msg.UserIdx, Msg.Id, Msg.DurationSec);
		// ★ 06.11.2026 [ОРИГИНАЛ]: `invSlotActivateAndDispatch@0xAD3630` — и из 5229, и из снапшота (разбор в `AsdaSoulSlotSubsystem.h`).
		if (UAsdaSoulSlotSubsystem* Slots = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr) { Slots->ActivateSlot(Msg.UserIdx, Msg.Id); }
		break;

	case EAsdaEffectMsgKind::SoulRemove:
		if (Msg.Id == 0) { EndSoulRecallSlot(Msg.UserIdx); }   // ★ 06.11.2026: `EndSlot` из `netRemoveSkillBuffState`; запись нужна ему, поэтому до снятия
		if (UAsdaSoulSlotSubsystem* Slots = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr) { Slots->EndSlot(Msg.UserIdx, Msg.Id); }   // ★ 06.11.2026: разбор в `AsdaSoulSlotSubsystem.h`
		RemoveBySkillState(Msg.UserIdx, Msg.Id, 1);
		break;

	case EAsdaEffectMsgKind::ItemApply:
	{
		// Эталон charBuff_ApplyPMBuffFromPacket@0xF26110: ключ записи = SpecialItemRecord.subType.
		int32 SubType = -1, Flag116 = 0, Cell = 0; FString Icon;
		const bool bHaveRow = GetTable2(Msg.ItemCode, SubType, Flag116, Icon, Cell);
		// ★★★ 16.09.2026. Если НОМЕР СЛОТА пришёл В ПАКЕТЕ (5418 и 6155 — снапшот и точечная посадка
		//   на транспорт), он главнее таблицы: у эталона в этой ветке ключ = слот из пакета
		//   (`cCharStatusAddTypeMount(char, k, …)`), а к `special_itemlist` она не обращается вовсе.
		//   Иначе расходящаяся или неполная строка таблицы МОЛЧА теряла состояние, которое оригинал
		//   наложил бы безусловно. Для 5453/6154 слота в пакете нет — там всё по-прежнему из таблицы.
		if (Msg.Id >= 0) { SubType = Msg.Id; }
		if (SubType < 0)
		{
			UE_LOG(LogAsdaBuff, Warning, TEXT("[эффекты] %d: в special_itemlist нет записи itemCode=%d и слот в пакете не пришёл — эффект не наложен (эталон: if (!rec) выход)"), Msg.Opcode, Msg.ItemCode);
			break;
		}
		if (!bHaveRow)
		{
			UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] %d: в special_itemlist нет записи itemCode=%d — слот %d взят ИЗ ПАКЕТА, визуал может не появиться"), Msg.Opcode, Msg.ItemCode, SubType);
		}
		AddType2(Msg.UserIdx, SubType, Msg.DurationSec, Msg.ItemCode);
		// ★★★ ВОЛНА 2: ВИЗУАЛ ПРЕМИУМ-БАФФА. Эталон после `cCharStatusAddTypeMount` идёт в
		//   `switch (rec->subType)` и спавнит один из семи классов `CharEffect_SpawnPMBuff_*` —
		//   раньше в порту этой половины не было вовсе (баф вешался «невидимым»).
		//   Гейта «свой/чужой» здесь нет и в оригинале: 5453 сервер рассылает по всему сектору
		//   (`sub_52E630` → `SendPacket_ClippingSectorInAllUser`), персонаж ищется по userNum
		//   из пакета — значит эффект обязан появляться и у соседей.
		SpawnPmBuffFx(Msg.UserIdx, SubType);
		UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] предметный усилитель: userIdx=%d itemCode=%d subType=%d остаток=%d с (0 = бессрочно)"),
			Msg.UserIdx, Msg.ItemCode, SubType, Msg.DurationSec);
		break;
	}

	case EAsdaEffectMsgKind::ItemRemove:
	{
		int32 SubType = -1, Flag116 = 0, Cell = 0; FString Icon;
		if (GetTable2(Msg.ItemCode, SubType, Flag116, Icon, Cell))
		{
			RemoveBySkillState(Msg.UserIdx, SubType, 2);
		}
		break;
	}

	case EAsdaEffectMsgKind::ClearAll:
		ClearAllStopFx(Msg.UserIdx);
		break;

	case EAsdaEffectMsgKind::ClearByMode:
		ClearByMode(Msg.UserIdx, Msg.Mode);
		break;
	}
}
