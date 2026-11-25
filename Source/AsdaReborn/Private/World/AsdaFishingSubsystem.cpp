// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishingSubsystem — реализация сети и автомата рыбалки (волна В1, 08.10.2026).
//   Обоснование каждого числа — шапка `AsdaFishingSubsystem.h` и `Reports/owner_1008/fishing_reverse.md`.
//   Здесь комментируются места реализации; адрес эталона стоит у каждой портированной ветки (`g:N` =
//   строка `RE/ida/export/AsdaStory_game.c`).
#include "World/AsdaFishingSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaNetSubsystem.h"
#include "AsdaDataTableCache.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaFishZoneSubsystem.h"   // ★ ревизия В4 МЕЛК-5: cmd 8 перебирает созданные зоны (единый источник с пикингом)
#include "World/AsdaCoord.h"
#include "Subsystems/SubsystemCollection.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущему — только ход и предметы (тело — AsdaPlayerCharacter_Move.cpp).
bool AsdaSwimActionLocked(const class AAsdaPlayerCharacter* Ch);

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFish, Log, All);

// ═════════════════════ ПУТИ ТАБЛИЦ (собирает Tools/asda_build_fishing_dt.py) ═════════════════════
static const TCHAR* kFishDataPath  = TEXT("/Game/AsdaAssets/Data/DT_FishData.DT_FishData");     // Fish_data.BIN
static const TCHAR* kFishNumPath   = TEXT("/Game/AsdaAssets/Data/DT_FishNum.DT_FishNum");       // Fish_num.BIN
static const TCHAR* kFishSpotsPath = TEXT("/Game/AsdaAssets/Data/DT_FishSpots.DT_FishSpots");   // Map_info + Fish_area_data

// ═════════════════════ НОТИСЫ (сводка §5; id сверены с GameText.csv) ═════════════════════════════
static constexpr int32 kLocTooFar      = 110700;   // «Расстояние не позволяет здесь рыбачить.» — чат
static constexpr int32 kLocNoGear      = 110701;   // «Не удалось воспользоваться предметом. Нет удочки или наживки.» — чат
static constexpr int32 kLocAlready     = 111041;   // 6168 код 2 — msgbox
static constexpr int32 kLocBagFull     = 1011;     // 6168 код 3 — msgbox
static constexpr int32 kLocForbidden   = 111042;   // 6168 код 4 — msgbox
static constexpr int32 kLocNoRod       = 111043;   // 6168 код 5 — msgbox; `invUseFirstEquipType10Item` — плашка 221
static constexpr int32 kLocNoBait      = 111044;   // 6168 код 6 — msgbox; `invUseFirstDyeItem` — плашка 221
static constexpr int32 kLocStopAsk     = 111046;   // диалог 80 «Прекратить рыбалку?»
static constexpr int32 kLocLowSkill    = 111049;   // 6168 код 7 (%d требование) — msgbox
static constexpr int32 kLocCountdown   = 111050;   // FSM 121 «Рыбалка начнется через [%d] сек.» — плашка 221
static constexpr int32 kLocFishHere    = 111057;   // диалог 81 «Хочешь здесь порыбачить?»
static constexpr int32 kLocAutoRod     = 111058;   // диалог 82 (%s удочка)
static constexpr int32 kLocAutoBait    = 111059;   // диалог 83 (%s наживка)
static constexpr int32 kLocSkillUp     = 111076;   // 6172 коды 0..2 (%d навык) — чат
static constexpr int32 kLocRodWorn     = 111077;   // 6168 код 8 — msgbox
// ★ fishfix 09.10 (ревизия МЕЛК-1): строки Р2 — отказ сервера в ожидании результата (118). Клиент оригинала эти ответы
//   игнорирует (вечное 118), поэтому своих нотисов у него нет; берутся строки ОРИГИНАЛА по смыслу серверной ветки
//   `Recv_EndFishing` (GameServer.c:7082-7239). [ПОРТ] — вместе с Р2, ключ отката тот же `asda.Fish.Legacy118`.
static constexpr int32 kLocSrvDesync   = 111045;   // «Проблемы связи с сервером. Рыбалка прекращается.» — 6172{3} «Not Fishing», 6183{4} «не предмет»
static constexpr int32 kLocBaitChanged = 111056;   // «При смене наживки или удочки ловля прерывается.» — 6172{8} «Wrong BaitItem»
static constexpr int32 kLocOverweight  = 2252;     // «Не удалось сохранить предмет - превышен вес.» — 6183{6} перевес

// ★ fishfix 09.10 (МЕЛК-1): loc-id нотиса Р2 по опкоду и коду серверной ветки; 0 — строки по смыслу нет («%d», как
//   ветка `default` эталона 6168). 6172{7} «Not FishingSpot» = смысл 6168{4} → его строка 111042.
static int32 FishLegacy118LocId(int32 Op, int32 Code)
{
	if (Op == 6172)
	{
		if (Code == 3) { return kLocSrvDesync; }      // GS:7089-7095 — у сервера рыбалки нет (isFishing = 0)
		if (Code == 7) { return kLocForbidden; }      // GS:7188-7195 — номер места ≥ 10
		if (Code == 8) { return kLocBaitChanged; }    // GS:7214-7220 — наживка в слоте не та, что при старте
		return 0;
	}
	if (Op == 6183)
	{
		if (Code == 4) { return kLocSrvDesync; }      // GS:7151-7155 — код улова не предмет (данные сервера)
		if (Code == 6) { return kLocOverweight; }     // GS:7181-7184 — вес рюкзака + вес рыбы > предела
		return 0;
	}
	return 0;
}
static constexpr int32 kLocDlgOk       = 108510;   // подпись «OK» диалогов 78…83 (`CItem_BuildTooltipText` класс 1)
static constexpr int32 kLocDlgCancel   = 108511;   // подпись «Отмена»

// Длительности клипов действия 60…67, мс — из `Fs_ch_{m,f}_ani_*.jbb` (CharMotion.baz, 30 к/с; у м и ж
//   одинаковые; разбор `asda_skinned_to_gltf.parse_jbb`, 08.10.2026): start_00 3.6667 с, idle_00 4.0,
//   end_00 2.0, idle_01 1.6667, idle_02 1.6667, end_01 3.3333, end_02 2.3333, end_00 (67) 2.0.
static constexpr int32 kClipMs[8] = { 3667, 4000, 2000, 1667, 1667, 3333, 2333, 2000 };

// ═════════════════════ КЛЮЧИ ОТКАТА (§8 сводки) ═══════════════════════════════════════════════════
static ASDA_DEV_CVAR(int32, CVarFishClipBridge,
	TEXT("asda.Fish.ClipBridge"), 0,
	TEXT("[ПОРТ] Рыбалка: 1 = откат на мост волны В1 — конец клипа действия 60…67 «наступает» по таймеру длительности ")
	TEXT("клипа из .jbb (персонаж конец своего клипа автомату не отдаёт); 0 (по умолчанию с волны В5) = конец клипа ")
	TEXT("сообщает персонаж (`AAsdaPlayerCharacter::OnFishClipEnd` → OnFishingClipEnd)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishLegacy107,
	TEXT("asda.Fish.Legacy107"), 0,
	TEXT("Р1. Рыбалка: 1 = как оригинал — 6167 не ушёл (нет зоны), а автомат всё равно ждёт 6168 в 107 (тупик); ")
	TEXT("0 (по умолчанию) = не уходить в 107, сброс 128."),
	ECVF_Default);
// ★ Ревизия В1 ВАЖН-2 (08.10.2026, волна В2): 109 без выбранной зоны — вечный тупик (эталон держит `g_pPickedMapObject`
//   вечно, порт обнуляет зону при сбросе/смене карты; ячейка хотбара типа 5 до первого выбора зоны оставила бы автомат
//   в 109 с заблокированным вводом).
static ASDA_DEV_CVAR(int32, CVarFishLegacy109,
	TEXT("asda.Fish.Legacy109"), 0,
	TEXT("Рыбалка: 1 = как оригинал — вход 109 (хотбар типа 5 / доп-меню) без выбранной зоны ставит 109 и ждёт вечно; ")
	TEXT("0 (по умолчанию) = [ПОРТ] как ветка «зоны нет» targetSelectNearestEntity: чат 110700 и автомат 0."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishLegacy118,
	TEXT("asda.Fish.Legacy118"), 0,
	TEXT("Р2. Рыбалка: 1 = как оригинал — в ожидании результата (118) ответы 6172 с кодом ≥3 и 6183 коды 4/6 из ")
	TEXT("Recv_EndFishing игнорируются (тупик); 0 (по умолчанию) = отказ → 128 + msgbox: строка оригинала по смыслу ")
	TEXT("серверной ветки (6172: 3 → 111045, 7 → 111042, 8 → 111056; 6183: 4 → 111045, 6 → 2252), прочие коды — «%d»."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishLegacyCancelSpam,
	TEXT("asda.Fish.LegacyCancelSpam"), 0,
	TEXT("Р3. Рыбалка: 1 = как оригинал — при смерти 6171(2) вызывается КАЖДЫЙ кадр (на провод всё равно не ")
	TEXT("уходит повтор — защёлка Р4 без ключа); 0 (по умолчанию) = один вызов на эпизод. Ветка эталона «флаг «рыбачит» ")
	TEXT("снят → автомат 128» идёт каждый кадр при любом значении ключа (fishfix 09.10, СРЕДН-1)."),
	ECVF_Default);
// ★ fishfix 09.10 (ревизия МЕЛК-5). Реверс: флаги авто-снаряжения лежат в `g_pInventoryMgr` (0x35202F8, +8 удочка / +9
//   наживка); пишут их ТОЛЬКО `invUseFirstEquipType10Item@0xFC7A90` / `invUseFirstDyeItem@0xFC7C20` (=1) и
//   `invFlaggedSlotTriggerUse@0xFC7920` (=0, возврат) — 22 обращения `mov ecx, 0x35202F8` в байтах exe, писателей
//   `[reg+0x890/0x891]` у методов CMap нет; `cmapInitReset` их не трогает и зовётся один раз за процесс. Т.е. у оригинала
//   портал флаги и автомат НЕ сбрасывает: рыбалка идёт дальше и ближайшее 128 возвращает прежнее оружие/наживку.
//   Порт на смене карты обнуляет автомат ([ПОРТ], см. `ResetSessionState`) — чтобы снасть всё равно вернулась, флаги
//   сохраняются и в покое, когда мир готов, ставится 128 (та же ветка возврата, что у эталона).
static ASDA_DEV_CVAR(int32, CVarFishMapReturnAuto,
	TEXT("asda.Fish.MapReturnAuto"), 1,
	TEXT("1 (по умолчанию) = как оригинал: авто-надетые удочка/наживка, если смена карты случилась посреди рыбалки, ")
	TEXT("возвращаются (ветка 128 в покое, когда мир загружен); 0 = прежний порт — флаги забываются, удочка остаётся в руках."),
	ECVF_Default);
// ★ fishfix2 09.10 (ревизия `fishing_fixes_REVIEW.md`, СРЕДН-1). У эталона `charEmoteSabzilAction@0xFC5590` (g:604314)
//   повтор сравнивается с `Net_GetCurrentMapType` (= 6 у трупа), а `CChar_SetState` пишет 60…67 безусловно (g:76456):
//   автомат 112/113/126/127 у мёртвого ставит клип рыбалки на ТРУП и шлёт 4024 соседям; конец 128 у трупа клипа не меняет —
//   тело застывает стоя в последнем кадре клипа. Это дефект оригинала — порт его исправляет: мёртвому клип 60…67 не
//   ставится и 4024 не уходит, проход автомата доводит страховка по таймеру .jbb, поза трупа остаётся. [ПОРТ]
static ASDA_DEV_CVAR(int32, CVarFishLegacyDeadClip,
	TEXT("asda.Fish.LegacyDeadClip"), 0,
	TEXT("1 = как оригинал — у мёртвого персонажа автомат рыбалки ставит клип 60…67 на труп (и свой, и соседа по 4025) ")
	TEXT("и шлёт 4024 соседям; 0 (по умолчанию) = [ПОРТ] клип не ставится, 4024 нет, проход доводит страховка по таймеру .jbb, ")
	TEXT("поза трупа сохраняется."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishLegacyOtherQueue,
	TEXT("asda.Fish.LegacyOtherQueue"), 0,
	TEXT("Р5. Рыбалка: 1 = как оригинал — записи 6173 о ненайденном charId остаются в очереди навсегда; ")
	TEXT("0 (по умолчанию) = удалять через asda.Fish.OtherQueueTtlMs."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarFishOtherQueueTtlMs,
	TEXT("asda.Fish.OtherQueueTtlMs"), 10000,
	TEXT("Р5. Рыбалка: сколько мс держать запись 6173, чей персонаж ещё не найден (сервер шлёт 6173 сразу за данными ")
	TEXT("появления — персонаж может заспавниться позже)."),
	ECVF_Default);

// ═════════════════════ ВСТРОЕННЫЙ ЭМУЛЯТОР МИНИ-ИГРЫ ([ПОРТ] до волны В2; объявление и смысл — в заголовке) ═══

void FAsdaFishMiniGameEmu::FishWinShow()
{
	bWinShown = true;
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка-эмуль] окно 71300 «показано» (окна В2 ещё нет)"));
}

void FAsdaFishMiniGameEmu::GuideStart(int32 InRounds)
{
	Rounds = InRounds; CurRound = 0; State = 1; Answers.Reset();
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка-эмуль] гайд запущен: раундов %d (раунд закрывает asda.Fish.EmuRound)"), Rounds);
}

int32 FAsdaFishMiniGameEmu::GuideTick()
{
	if (State == 8) { State = 0; return 8; }          // гайд state 8 → 0, вернуть 8
	if (State != 1 || Answers.Num() == 0) { return 0; }
	const FIntPoint A = Answers[0];
	Answers.RemoveAt(0);
	++CurRound;                                          // гайд state 5: ++curPage
	if (Owner) { Owner->SendPhaseResult(A.X, A.Y); }     // гайд state 5: 6169
	State = (CurRound < Rounds) ? 1 : 8;
	return 5;
}

void FAsdaFishMiniGameEmu::ShowCatchPopup(int32 ItemId)
{
	bPopup = true;
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка-эмуль] всплывашка улова 71400: предмет %d"), ItemId);
}

bool FAsdaFishMiniGameEmu::QueueRound(int32 Total, int32 Correct)
{
	if (State == 0) { return false; }
	Answers.Add(FIntPoint(Total, Correct));
	return true;
}

// ═════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

// ★ В3 S7 (N14): единая точка «идёт рыбалка» — разбор у объявления.
bool UAsdaFishingSubsystem::IsFishingNow(const UObject* Ctx)
{
	const UGameInstance* GI = Cast<UGameInstance>(Ctx);
	if (!GI && Ctx)
	{
		if (const UWorld* W = Ctx->GetWorld()) { GI = W->GetGameInstance(); }
	}
	const UAsdaFishingSubsystem* F = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	return F && F->IsFishing();
}

void UAsdaFishingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Сеть должна существовать ДО подписки: 6164 приходит в потоке загрузки персонажа.
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Emu = MakeUnique<FAsdaFishMiniGameEmu>(this);
	BindNet();
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] подсистема поднята (сеть %s)"), bNetBound ? TEXT("подписана") : TEXT("НЕТ"));
}

void UAsdaFishingSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnFishSkill6164.RemoveAll(this);
			Net->OnFishStart6168.RemoveAll(this);
			Net->OnFishGauge6170.RemoveAll(this);
			Net->OnFishResult6172.RemoveAll(this);
			Net->OnFishPeer6173.RemoveAll(this);
			Net->OnFishBookReward6183.RemoveAll(this);
		}
	}
	MiniGame = nullptr;
	Emu.Reset();
	Super::Deinitialize();
}

void UAsdaFishingSubsystem::BindNet()
{
	if (bNetBound) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	Net->OnFishSkill6164.AddUObject(this, &UAsdaFishingSubsystem::OnNet6164);
	Net->OnFishStart6168.AddUObject(this, &UAsdaFishingSubsystem::OnNet6168);
	Net->OnFishGauge6170.AddUObject(this, &UAsdaFishingSubsystem::OnNet6170);
	Net->OnFishResult6172.AddUObject(this, &UAsdaFishingSubsystem::OnNet6172);
	Net->OnFishPeer6173.AddUObject(this, &UAsdaFishingSubsystem::OnNet6173);
	Net->OnFishBookReward6183.AddUObject(this, &UAsdaFishingSubsystem::OnNet6183);
	bNetBound = true;
}

// ★★★ СБРОС СЕССИИ. Эталон: `cmapInitReset@0xFC3170` ОДИН раз за процесс (fishState 0, пауза 0, автоповтор 1),
//   а при разрыве оригинал закрывает клиент — у нас полный сброс обязателен (original-quits-client-on-disconnect).
//   Смена карты в оригинале fishState НЕ трогает (cmapInitReset на неё не зовётся; ходить/порталить во время
//   рыбалки нельзя, а сервер при уходе на другой процесс чистит блок рыбалки целиком `sub_4A2DC0`) —
//   [ПОРТ] при MapChanged автомат тоже обнуляется (мир, зоны и выбранная зона умерли; висячий
//   `g_pPickedMapObject` эталона — дефект), но настройки окна (автоповтор/пауза) сохраняются, как в оригинале.
//   Пакеты не шлются (закон: сброс локален). Навык 6164 приходит в потоке загрузки персонажа — при
//   EnteringWorld/MapChanged он и его записи в очереди СОХРАНЯЮТСЯ (session-reset-drops-early-data).
void UAsdaFishingSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	const bool bFull = AsdaIsFullSessionReset(Reason);
	const bool bKeepSkill = (Reason == EAsdaSessionResetReason::EnteringWorld || Reason == EAsdaSessionResetReason::MapChanged);
	const int32 WasState = FishState;

	// Очередь пакетов: при сохранении навыка — оставить только 6164, остальное умерло вместе с сессией/миром.
	{
		// ★ Ревизия В1 ВАЖН-1: на MapChanged сохраняются и 6173 — сервер шлёт их при появлении рыбака в видимости, и
		//   приходят они ДО `PostLoadMap` (как 4003). Записи ключуются charId, лишние уберёт TTL Р5.
		// ★ N10 (В1 дедупа №2): фильтр на месте под замком очереди (`RemoveIf`) вместо «Drain → RemoveAll → Push» — итог тот же.
		const bool bKeepPeers = (Reason == EAsdaSessionResetReason::MapChanged);
		if (bKeepSkill) { PendingPkts.RemoveIf([bKeepPeers](const FPkt& P) { return P.Op != 6164 && !(bKeepPeers && P.Op == 6173); }); }
		else { PendingPkts.Clear(); }
	}
	// Диалоги рыбалки (1, 80…83): при смене карты очередь окон сохраняется — снять свои, иначе их ответ
	//   переставит автомат уже после сброса. Полный сброс чистит очередь сам (молча, без колбэков).
	if (!bFull)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
			{
				for (int32 Ctx = 80; Ctx <= 83; ++Ctx) { WM->RemoveMsgBoxEntries(1, Ctx, TEXT("рыбалка: смена карты"), /*bRedisplayNow*/false); }
			}
		}
	}

	FishState = 0;
	Fs1Ms = Fs2Ms = Fs3Ms = 0;
	CountdownNoticeMs = 0;
	PickedSpot = -1;
	Gauge = 0.f; GaugeAux = 0.f; GaugeMax = kGaugeMax;
	SelectedFishCode = 0;
	Last6168.Reset(); Last6172.Reset();
	CaughtItemId = 0; CaughtSizeCm = 0;
	b6171InFlight = false;
	bDeathCancelCalled = false;
	// ★ fishfix 09.10 (МЕЛК-5): на смене карты флаги авто-снаряжения эталона переживают портал — сохраняем и возвращаем
	//   снасть ближайшим 128 в покое (`TickReturnAutoAfterMap`). Полный сброс/вход в мир — забыть (оригинал при разрыве
	//   закрывает клиент, сервер хранит снаряжение как есть).
	const bool bKeepAuto = Reason == EAsdaSessionResetReason::MapChanged && (bAutoRod || bAutoBait)
		&& CVarFishMapReturnAuto.GetValueOnGameThread() != 0;
	if (bKeepAuto)
	{
		bReturnAutoAfterMap = true;
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] смена карты при авто-снаряжении (удочка %d ячейка %d, наживка %d ячейка %d) — вернём в покое"),
			bAutoRod ? 1 : 0, AutoRodBagCell, bAutoBait ? 1 : 0, AutoBaitCell);
	}
	else
	{
		bAutoRod = false; AutoRodBagCell = -1;
		bAutoBait = false; AutoBaitCell = -1;
		bReturnAutoAfterMap = false;
	}
	bSelfFishingFlag = false; bSelfHasFish = false; SelfFishSizePct = 100; SelfFishCode = 0;
	BridgeAction = -1; BridgeEndMs = 0; bBridgeForced = false;
	// ★ Ревизия В1 ВАЖН-1: очередь 6173 на смене карты не чистится (ранние записи о соседях новой карты); снимает TTL Р5.
	if (bFull) { PeerQueue.Reset(); }
	PeerStates.Reset();
	if (SelfAction != 0) { SelfAction = 0; OnSelfFishAction.Broadcast(0); }
	if (bFull)
	{
		// `cmapInitReset`: пауза 0, автоповтор 1 (0x35202F4 / 0x35202F5).
		bPaused = false;
		bAutoRepeat = true;
	}
	if (!bKeepSkill)
	{
		FishingSkill = 0;
		bSkillKnown = false;
		OnFishSkillChanged.Broadcast(0);
	}
	if (Emu) { Emu->Reset(); }
	// ★ Ревизия В1 МЕЛК-7: точка сброса зарегистрированной мини-игры (окно В2).
	if (IAsdaFishMiniGame* Reg = GetRegisteredMiniGame()) { Reg->OnFishSessionReset(bFull); }
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] сброс сессии (%s): автомат %d → 0, навык %s"),
		AsdaSessionResetReasonName(Reason), WasState, bKeepSkill ? TEXT("сохранён") : TEXT("обнулён"));
}

// Драйвер `charUpdateFishingAndState@0xFC3270`: FSM → разбор очереди чужих. Пакеты разбираем ДО автомата
//   (у эталона приём идёт в сетевом цикле того же кадра раньше игрового тика).
void UAsdaFishingSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Fishing");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!bNetBound) { BindNet(); }
	if (bTestNoNetDrain) { return; }   // самотест ведёт шаги сам
	DrainPackets();
	UpdateFsm();
	ApplyPeerQueue();
	TickClipBridge();
}

// ═════════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════

// Строки DT в массив указателей, упорядоченный по колонке Order (порядок исходного .BIN).
template<typename TRow>
static void AsdaFishCollect(const TCHAR* Path, TArray<const TRow*>& Out)
{
	Out.Reset();
	UDataTable* Dt = AsdaDt::Cached(Path);
	if (!Dt) { return; }
	TArray<TRow*> Rows;
	Dt->GetAllRows<TRow>(TEXT("Fishing"), Rows);
	for (TRow* R : Rows) { if (R) { Out.Add(R); } }
	Out.Sort([](const TRow& A, const TRow& B) { return A.Order < B.Order; });
}

void UAsdaFishingSubsystem::EnsureTables() const
{
	if (bTablesTried) { return; }
	bTablesTried = true;
	UAsdaFishingSubsystem* Self = const_cast<UAsdaFishingSubsystem*>(this);
	const TCHAR* Paths[] = { kFishDataPath, kFishNumPath, kFishSpotsPath };
	Self->HeldTables.Hold(FName(TEXT("Fishing")), Paths);

	// Строки в порядке ФАЙЛА (Order) — эталонные поиски берут первую подходящую.
	AsdaFishCollect<FAsdaFishDataRow>(kFishDataPath, Self->FishRows);
	AsdaFishCollect<FAsdaFishNumRow>(kFishNumPath, Self->NumRows);
	AsdaFishCollect<FAsdaFishSpotRow>(kFishSpotsPath, Self->SpotRows);
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] таблицы: Fish_data %d, Fish_num %d, споты %d%s"),
		FishRows.Num(), NumRows.Num(), SpotRows.Num(),
		(FishRows.Num() && NumRows.Num() && SpotRows.Num()) ? TEXT("") : TEXT(" — ★ НЕПОЛНО: собери Tools/asda_build_fishing_dt.py"));
}

const FAsdaFishDataRow* UAsdaFishingSubsystem::FindFishData(int32 FishCode) const
{
	EnsureTables();
	for (const FAsdaFishDataRow* R : FishRows) { if (R && R->FishCode == FishCode) { return R; } }   // первая по порядку
	return nullptr;
}

int32 UAsdaFishingSubsystem::LookupFishNum(int32 V) const
{
	EnsureTables();
	for (const FAsdaFishNumRow* R : NumRows) { if (R && V >= R->Min && V <= R->Max) { return R->Value; } }
	return 1;   // `rangeTableLookupValue`: вне диапазонов → 1
}

const FAsdaFishSpotRow* UAsdaFishingSubsystem::GetSpot(int32 SpotIdx) const
{
	EnsureTables();
	return SpotRows.IsValidIndex(SpotIdx) ? SpotRows[SpotIdx] : nullptr;
}

void UAsdaFishingSubsystem::GetSpotsOfMap(int32 MapId, TArray<int32>& OutIdx) const
{
	EnsureTables();
	OutIdx.Reset();
	for (int32 i = 0; i < SpotRows.Num(); ++i) { if (SpotRows[i] && SpotRows[i]->MapId == MapId) { OutIdx.Add(i); } }
}

int32 UAsdaFishingSubsystem::GetRoundLimitMs() const
{
	const FAsdaFishDataRow* F = FindFishData(SelectedFishCode);
	return F ? F->RoundMs : kNoFishDataRoundMs;   // `g:605260-605264`
}

int32 UAsdaFishingSubsystem::ComputeRoundRemainMs(int32 ElapsedMs) const
{
	return FMath::Max(0, GetRoundLimitMs() - ElapsedMs);   // ★ Р9: кламп 0 (эталон рисует мусор при < 0)
}

int32 UAsdaFishingSubsystem::GetKeysPerRound() const
{
	// `Input_BuildKeySchemeString@0xFC7120` (g:605118): ключ — `*(inputMgr + 16)` = умение выбранной зоны.
	const FAsdaFishSpotRow* S = GetSpot(PickedSpot);
	return LookupFishNum(S ? S->Skill : 0);
}

// ═════════════════════════════════ ВРЕМЯ, ПОМОЩНИКИ ═══════════════════════════════════════════════

uint32 UAsdaFishingSubsystem::NowMs() const
{
	return (uint32)((int64)(FPlatformTime::Seconds() * 1000.0) + TestClockOffsetMs);   // `timeGetTime()`
}

IAsdaFishMiniGame* UAsdaFishingSubsystem::MG() const
{
	IAsdaFishMiniGame* Reg = GetRegisteredMiniGame();
	return Reg ? Reg : static_cast<IAsdaFishMiniGame*>(Emu.Get());
}

// ★ Ревизия В1 МЕЛК-7: зарегистрированная реализация годна, только пока жив её владелец-UObject.
IAsdaFishMiniGame* UAsdaFishingSubsystem::GetRegisteredMiniGame() const
{
	if (!MiniGame) { return nullptr; }
	if (bMiniGameHasOwner && !MiniGameOwner.IsValid()) { return nullptr; }
	return MiniGame;
}

AAsdaPlayerCharacter* UAsdaFishingSubsystem::SelfChar() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (AAsdaPlayerCharacter* Ch = (WS && IsValid(WS->LocalPlayerActor)) ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr) { return Ch; }
	// Офлайн-мир (без 4003) свой павн в реестр мира не записан — берём павн контроллера (как самотесты 4024/4007).
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	return PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
}

FString UAsdaFishingSubsystem::LocFmt(int32 Id, const TArray<FString>& Args) const
{
	const FString F = AsdaNotice::LocRaw(GetGameInstance(), Id, /*bDebugFallback*/true);
	return Args.Num() ? AsdaUi::FormatPrintfArgs(F, Args) : AsdaUi::FormatPrintfArgs(F, {});
}

void UAsdaFishingSubsystem::NoteNotice(const TCHAR* Channel, int32 LocId, const FString& Text)
{
	if (RecentNotices.Num() >= 64) { RecentNotices.RemoveAt(0); }
	RecentNotices.Add(FString::Printf(TEXT("%s:%d:%s"), Channel, LocId, *Text));
}

void UAsdaFishingSubsystem::ChatGold(const FString& Text, int32 LocId)
{
	AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, Text, UAsdaChatSubsystem::ColGold(), 10000.f);
	NoteNotice(TEXT("chat"), LocId, Text);
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] чат: %s"), *Text);
}

void UAsdaFishingSubsystem::PlankGold(const FString& Text, int32 LocId)
{
	// ★ Ревизия В1 МЕЛК-9: `SrcId` = loc-id (как у прочих вызывающих плашки), а не опкод 6171.
	AsdaNotice::Plank(GetGameInstance(), EAsdaMsgCategory::System, Text, UAsdaChatSubsystem::ColGold(), 10000.f, LocId);
	NoteNotice(TEXT("plank"), LocId, Text);
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] плашка 221: %s"), *Text);
}

void UAsdaFishingSubsystem::NoticePlankLoc(int32 LocId)
{
	PlankGold(LocFmt(LocId), LocId);
}

void UAsdaFishingSubsystem::MsgBox1(const FString& Text, int32 LocId)
{
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::System, Text);
	NoteNotice(TEXT("msgbox"), LocId, Text);
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] msgbox: %s"), *Text);
}

void UAsdaFishingSubsystem::Play2D(const TCHAR* Name) const
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2D(FName(Name)); }
}

void UAsdaFishingSubsystem::Play3DAtSelf(const TCHAR* Name) const
{
	UGameInstance* GI = GetGameInstance();
	AAsdaPlayerCharacter* Ch = SelfChar();
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (Snd && Ch) { Snd->PlaySound3D(FName(Name), Ch->GetActorLocation()); }
}

// `Effect_SpawnByName(g_pEffectMgr, имя, 1, x, y + 130, z, 0, char->posVec, 0, 0, 0, 1.0)` (g:604367-604392).
//   Игровая ось Y = UE Z ⇒ +130 по Z. BP `BP_Fx_fish_end_01/02` собраны волной В5а.
//   ★ Волна В5: режим привязки разобран — `Effect_SpawnByName@0xF56340` кладёт a10 (= 0) в узел +200 (режим привязки),
//   posVec (a8) — только владелец узла; режим 0 = эффект стоит в точке спавна (как и сделано). Чужим тот же эффект
//   ставит персонаж при входе в действие 65/66 (`AAsdaPlayerCharacter::SpawnFishEndFx`).
void UAsdaFishingSubsystem::SpawnEndFx(const TCHAR* JebBase) const
{
	AAsdaPlayerCharacter* Ch = SelfChar();
	UWorld* W = Ch ? Ch->GetWorld() : nullptr;
	if (!W) { return; }
	const FVector At = Ch->GetActorLocation() + FVector(0.f, 0.f, 130.f);
	// ★ Дедуп №2 В1-C (F12): спавн и срок — общей точкой (тот же вызов у чужого: `AAsdaPlayerCharacter::SpawnFishEndFx`).
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, JebBase, At, Ch);
	if (!Fx)
	{
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] эффект %s: BP_Fx_%s ещё не собран (волна В5) — не показан"), JebBase, JebBase);
		return;
	}
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] эффект %s показан"), JebBase);
}

// ═════════════════════════════════ ВХОД И ЗОНЫ ═══════════════════════════════════════════════════

// `sub_FC3590` (g:603165): `if (!Input_IsTextInputFocused(fishState)) g_pPickedMapObject = zone`.
bool UAsdaFishingSubsystem::PickSpot(int32 SpotIdx)
{
	if (FishState != 0) { return false; }
	if (!GetSpot(SpotIdx)) { return false; }
	PickedSpot = SpotIdx;
	return true;
}

// `inputHotkeyAction109@0xFC3540` (g:603153).
void UAsdaFishingSubsystem::RequestStart109()
{
	if (FishState != 0) { return; }
	// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущий не рыбачит — МОЛЧА (автомат остаётся 0). Ключ `asda.Swim.ActionGate`.
	if (AsdaSwimActionLocked(SelfChar()))
	{
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 109 не начат: персонаж плывёт"));
		return;
	}
	// ★ Ревизия В1 ВАЖН-2 [ПОРТ] (`asda.Fish.Legacy109`): зоны нет — эталонная ветка «зоны нет» соседнего входа
	//   `targetSelectNearestEntity` (g:604742-604746): чат 110700 и автомат 0, а не вечный 109.
	if (!GetSpot(PickedSpot) && CVarFishLegacy109.GetValueOnGameThread() == 0)
	{
		ChatGold(LocFmt(kLocTooFar), kLocTooFar);
		SetState(0, TEXT("109 без выбранной зоны (ревизия В1 ВАЖН-2)"));
		return;
	}
	SetState(109, TEXT("вход: inputHotkeyAction109"));
}

// `targetSelectNearestEntity@0xFC6490` (g:604677): перебор объектов-зон списка `CMap+0x140+0x724` (0x35202D4) — тех, что
//   поставил в мир вход на карту (`worldMapPlaceMarkersByType`), их же пикает мышь (тип 5). Расстояние — 2D от
//   (obj+0x204, 0, obj+0x20C) до персонажа (Y игрока обнулён), радиус — запись зоны `sub_91FB12(obj+0x1CC)` +12 ×100, условие
//   «внутри» строго `r·100 > d`, берётся ближайшая (строгое `<`: при равенстве — первая в списке).
//   ★ Ревизия В4 МЕЛК-5 (09.10.2026): перебираются АКТЁРЫ зон `UAsdaFishZoneSubsystem::GetZones()` (единый источник с пикингом
//   мышью), а не таблица спотов по номеру карты: в окне между `BeginMapLoad` и травелом (номер карты уже новый, мир и
//   персонаж — старые) прежний код сравнивал позицию старой карты со спотами новой; эталон в это время видит список старой.
bool UAsdaFishingSubsystem::StartAtNearestSpot()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaFishZoneSubsystem* ZS = GI ? GI->GetSubsystem<UAsdaFishZoneSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = SelfChar();
	int32 Best = -1;
	float BestDist = 999999.f;
	if (ZS && Ch)
	{
		const FVector2D P = Ch->GetGamePosXZ();
		for (const TWeakObjectPtr<AAsdaFishZoneActor>& Z : ZS->GetZones())
		{
			const AAsdaFishZoneActor* A = Z.Get();
			const FAsdaFishSpotRow* S = A ? GetSpot(A->SpotIdx) : nullptr;   // `sub_91FB12(g_pMap, obj[115])`
			if (!S) { continue; }
			const float D = (FVector2D(A->CenterGame.X, A->CenterGame.Z) - P).Size();   // (obj+0x204, obj+0x20C)
			if ((float)S->Radius * AsdaCoord::kTileUnits > D && D < BestDist) { Best = A->SpotIdx; BestDist = D; }   // ★ N11: радиус в тайлах → единицы
		}
	}
	if (Best >= 0)
	{
		PickSpot(Best);
		RequestStart109();
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] ближайшая зона %d (карта %d слот %d), %.0f ед."),
			Best, SpotRows[Best]->MapId, SpotRows[Best]->Slot, BestDist);
		return true;
	}
	ChatGold(LocFmt(kLocTooFar), kLocTooFar);
	SetState(0, TEXT("targetSelectNearestEntity: зоны в радиусе нет"));
	return false;
}

// Дистанция FSM 102/109: `|zone.xz − player.xz|` против `зона+12 · 100` (`fcomp` с маской 0x4100 = «≤»).
bool UAsdaFishingSubsystem::IsFarFromPicked(FVector2D& OutDelta) const
{
	OutDelta = FVector2D::ZeroVector;
	const FAsdaFishSpotRow* S = GetSpot(PickedSpot);
	AAsdaPlayerCharacter* Ch = SelfChar();
	if (!S || !Ch) { return true; }
	OutDelta = FVector2D((float)S->X * AsdaCoord::kTileUnits, (float)S->Y * AsdaCoord::kTileUnits) - Ch->GetGamePosXZ();   // ★ N11
	return !((float)S->Radius * AsdaCoord::kTileUnits > OutDelta.Size());
}

// FSM 102/109: `CharFx_AutoSabzil_State(ч, 0)` → `Net_SendActionPacket` (4008, только при `g_bActionPending`) →
//   нормализованный курс к зоне (`rendererSetField5Vec3`) → снова 4008. Общая точка стопа порта —
//   `AAsdaPlayerController::StopMoveAndNotify` (локальный стоп + 4008 под тем же гейтом).
//   Повтор каждый кадр в 109 (эталон зовёт это каждый кадр, пока висит диалог 81) — без хода и без
//   незакрытого 4006 стоп ничего не делает, поэтому он и не зовётся (журнал не засоряется).
void UAsdaFishingSubsystem::StopAndFaceZone(const FVector2D& Delta, const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	AAsdaPlayerController* PC = GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = SelfChar();
	if (PC && (PC->IsMovingNow() || (Net && Net->bActionPending))) { PC->StopMoveAndNotify(Why); }
	if (Ch && !Delta.IsNearlyZero()) { Ch->FaceGameDir(Delta.GetSafeNormal()); }
	if (Ch && Net && Net->bActionPending)
	{
		const FVector2D Cur = Ch->GetGamePosXZ();
		Net->SendStop4008(Cur.X, Cur.Y);   // второй `Net_SendActionPacket` эталона (тот же гейт)
	}
}

// ═════════════════════════════════ СНАРЯЖЕНИЕ ═════════════════════════════════════════════════════

bool UAsdaFishingSubsystem::HasRodEquipped() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* C = Inv ? Inv->GetCell(3, kEquipSlotRod) : nullptr;
	const FAsdaItemRecord* R = (C && !C->IsEmpty() && IT) ? IT->GetItem(C->ItemCode) : nullptr;
	return R && R->ItemSubType == kRodSubType && R->ItemVariant == kRodVariant;
}

bool UAsdaFishingSubsystem::HasBaitEquipped() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* C = Inv ? Inv->GetCell(3, kEquipSlotBait) : nullptr;
	const FAsdaItemRecord* R = (C && !C->IsEmpty() && IT) ? IT->GetItem(C->ItemCode) : nullptr;
	return R && R->ItemSubType == kBaitSubType;
}

// `invUseFirstEquipType10Item@0xFC7A90` (g:605377): первая удочка 10/1 с прочностью (+13) ≠ 0 из контейнера 1 —
//   надеть штатным «использовать» (`sub_934F99(inv, 1)` + `CInventory_OnSlotUse(10010 + i)`) и диалог 82;
//   нет — плашка 221 `111043` (`CControl_SetTooltipText`) и 128.
void UAsdaFishingSubsystem::UseFirstRod()
{
	if (HasRodEquipped()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Inv && IT)
	{
		// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон перебирает 30 ячеек сумки — у нас всю доступную сумку (60).
		for (int32 i = 0; i < Inv->GetContainerSize(1); ++i)
		{
			const FAsdaItemInstance* C = Inv->GetCell(1, i);
			const FAsdaItemRecord* R = (C && !C->IsEmpty()) ? IT->GetItem(C->ItemCode) : nullptr;
			if (R && R->ItemSubType == kRodSubType && R->ItemVariant == kRodVariant && C->Durability != 0)
			{
				bAutoRod = true;          // `v8[8] = 1`
				AutoRodBagCell = i;       // `*(DWORD*)v8 = &ячейка`
				const int32 Code = C->ItemCode;
				Inv->UseFromBagWindow(1, i);
				ShowFishDialog(82, LocFmt(kLocAutoRod, { IT->GetItemName(Code) }));
				UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] авто-удочка: сумка ячейка %d код %d"), i, Code);
				return;
			}
		}
	}
	PlankGold(LocFmt(kLocNoRod), kLocNoRod);
	SetState(128, TEXT("invUseFirstEquipType10Item: удочки нет"));
}

// `invUseFirstDyeItem@0xFC7C20` (g:605420): первая наживка (подтип 204, прочность не проверяется) из контейнера 2.
void UAsdaFishingSubsystem::UseFirstBait()
{
	if (HasBaitEquipped()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Inv && IT)
	{
		for (int32 i = 0; i < 60; ++i)
		{
			const FAsdaItemInstance* C = Inv->GetCell(2, i);
			const FAsdaItemRecord* R = (C && !C->IsEmpty()) ? IT->GetItem(C->ItemCode) : nullptr;
			if (R && R->ItemSubType == kBaitSubType)
			{
				bAutoBait = true;         // `v8[9] = 1`
				AutoBaitCell = i;         // `*((DWORD*)v8 + 1) = &ячейка`
				const int32 Code = C->ItemCode;
				Inv->UseFromBagWindow(2, i);
				ShowFishDialog(83, LocFmt(kLocAutoBait, { IT->GetItemName(Code) }));
				UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] авто-наживка: расходники ячейка %d код %d"), i, Code);
				return;
			}
		}
	}
	PlankGold(LocFmt(kLocNoBait), kLocNoBait);
	SetState(128, TEXT("invUseFirstDyeItem: наживки нет"));
}

// `invFlaggedSlotTriggerUse@0xFC7920` (g:605334): вернуть авто-надетое. Эталон берёт код, который СЕЙЧАС лежит
//   в исходной ячейке (после обмена туда легло снятое прежде), ищет первую ячейку с этим кодом и «использует»
//   её. Ячейка пуста (оружия не было) — запись предмета не находится, удочка остаётся надетой [ОРИГИНАЛ].
void UAsdaFishingSubsystem::ReturnAutoEquipped()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (bAutoRod)
	{
		bAutoRod = false;
		const int32 Want = Inv ? Inv->GetCellItemCode(1, AutoRodBagCell) : 0;
		for (int32 i = 0; Inv && IT && i < Inv->GetContainerSize(1); ++i)   // ★ 29.10.2026 (инвентарь ×2): было 30
		{
			const int32 Code = Inv->GetCellItemCode(1, i);
			if (IT->GetItem(Code) && Code == Want)
			{
				DebugLastReturnRodCell = i;   // ревизия В1 МЕЛК-6: самотест утверждает ячейку возврата
				Inv->UseFromBagWindow(1, i);
				UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] возврат удочки: надеваем обратно код %d (сумка %d)"), Code, i);
				break;
			}
		}
	}
	if (bAutoBait)
	{
		bAutoBait = false;
		const int32 Want = Inv ? Inv->GetCellItemCode(2, AutoBaitCell) : 0;
		for (int32 j = 0; Inv && IT && j < 60; ++j)
		{
			const int32 Code = Inv->GetCellItemCode(2, j);
			if (IT->GetItem(Code) && Code == Want)
			{
				Inv->UseFromBagWindow(2, j);
				UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] возврат наживки: код %d (расходники %d)"), Code, j);
				break;
			}
		}
	}
}

// ★ fishfix 09.10 (МЕЛК-5): «использовать» уходит на сервер — только когда сессия снова в мире после перехода (соединение
//   пересоздано `EnterGameWorld`), свой персонаж есть и экран загрузки снят.
bool UAsdaFishingSubsystem::IsWorldReadyForAutoReturn() const
{
	if (TestWorldReadyOverride >= 0) { return TestWorldReadyOverride != 0; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	return Net && Net->State == EAsdaNetState::InWorld && SelfChar() != nullptr && !(LM && LM->IsLoadingScreenVisible());
}

// ★ fishfix 09.10 (МЕЛК-5): в покое после смены карты — ветка 128 эталона (возврат авто-надетого, действие 0, флаг 0,
//   панель скрыта) → 0. Новая рыбалка до этого момента (автомат ≠ 0) сама дойдёт до 128 и вернёт снасть тем же путём.
void UAsdaFishingSubsystem::TickReturnAutoAfterMap()
{
	if (!bReturnAutoAfterMap) { return; }
	if (!bAutoRod && !bAutoBait) { bReturnAutoAfterMap = false; return; }
	if (FishState != 0 || !IsWorldReadyForAutoReturn()) { return; }
	bReturnAutoAfterMap = false;
	SetState(128, TEXT("МЕЛК-5: возврат авто-надетой снасти после смены карты (как ближайшее 128 оригинала)"));
}

// ═════════════════════════════════ ДИАЛОГИ 79…83 ═════════════════════════════════════════════════

// `CItem_BuildTooltipText(g_pItemTooltipUI, 1, Ctx, …, 1)` — TWOBUT, кнопки 108510/108511 (g:378887-378968).
//   Пара (1, Ctx) в очереди глушит повтор (`objList_ContainsPair`) — FSM 109 зовёт это каждый кадр.
//   `?` 82/83 эталон ещё показывает контрол msgbox [6] и `helper_setupFields(…, 1)` — назначение не разобрано.
void UAsdaFishingSubsystem::ShowFishDialog(int32 Ctx, const FString& Text)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	if (WM->IsMsgBoxQueued(1, Ctx)) { return; }
	TWeakObjectPtr<UAsdaFishingSubsystem> WeakThis(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [WeakThis, Ctx](bool bOk)
	{
		if (UAsdaFishingSubsystem* S = WeakThis.Get()) { S->OnFishDialogAnswer(Ctx, bOk); }
	}, 0.f, kLocDlgOk, kLocDlgCancel, /*очередь: эталон (1, Ctx)*/Ctx, 1);
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] диалог %d: %s"), Ctx, *Text);
}

// `OnSendItemUpgrade@0xD74180`: OK — g:380104-380126, Отмена — g:380704-380720.
void UAsdaFishingSubsystem::OnFishDialogAnswer(int32 Ctx, bool bOk)
{
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] ответ диалога %d: %s (автомат %d)"), Ctx, bOk ? TEXT("OK") : TEXT("Отмена"), FishState);
	if (bOk)
	{
		switch (Ctx)
		{
		case 80: Send6171(2); break;                                                        // 0x50
		case 81: SetState((!HasRodEquipped() || !HasBaitEquipped()) ? 103 : 102, TEXT("диалог 81 OK")); break;   // 0x51
		case 82: SetState(!HasBaitEquipped() ? 105 : 102, TEXT("диалог 82 OK")); break;    // 0x52
		case 83: SetState(102, TEXT("диалог 83 OK")); break;                               // 0x53 → LABEL_104
		default: break;
		}
		return;
	}
	if (Ctx == 81) { SetState(0, TEXT("диалог 81 Отмена")); }
	if (Ctx == 82) { SetState(128, TEXT("диалог 82 Отмена")); }
	if (Ctx == 83) { SetState(128, TEXT("диалог 83 Отмена")); }
}

// «Отмена» диалога 79 (зовёт книга В3): `else if (ctx == 79 && fishState) fishState = 100` (g:380708).
void UAsdaFishingSubsystem::OnBookDialogCancelled()
{
	// ★ R5-fix8-c (14.11.2026): два поля наблюдения для самотеста (M4 книги сверяется с самим переходом, а не с конечным состоянием).
	if (FishState != 0) { ++BookCancelCount; BookCancelFromState = FishState; SetState(100, TEXT("диалог 79 Отмена")); }
}

void UAsdaFishingSubsystem::RequestCancelDialog80()
{
	ShowFishDialog(80, LocFmt(kLocStopAsk));
}

// ═════════════════════════════════ ДЕЙСТВИЯ СВОЕГО ПЕРСОНАЖА ══════════════════════════════════════

// `CharFx_AutoSabzil_State(свой, Action)` — поставить состояние действия. Клип персонажа/удочки — В5 по делегату;
//   до В5 — мост: конец клипа наступает по таймеру длительности из .jbb ([ПОРТ], `asda.Fish.ClipBridge`).
void UAsdaFishingSubsystem::SetSelfActionState(int32 Action)
{
	SelfAction = Action;
	// Мост прошлого прохода снимается ДО показа: показ (`OnSelfFishAction` → персонаж) может сам взвести страховку
	//   `ArmClipBridgeFallback` для нового действия (В5fix, МЕЛК-5) — её не затираем.
	BridgeAction = -1; bBridgeForced = false;
	OnSelfFishAction.Broadcast(Action);
	if (Action >= 60 && Action <= 67 && CVarFishClipBridge.GetValueOnGameThread() != 0)
	{
		BridgeAction = Action;
		BridgeEndMs = NowMs() + (uint32)kClipMs[Action - 60];
		bBridgeForced = false;
	}
}

void UAsdaFishingSubsystem::ArmClipBridgeFallback(int32 Action, const TCHAR* Why)
{
	if (Action < 60 || Action > 67 || SelfAction != Action) { return; }
	BridgeAction = Action;
	BridgeEndMs = NowMs() + (uint32)kClipMs[Action - 60];
	bBridgeForced = true;
	UE_LOG(LogAsdaFish, Warning, TEXT("[рыбалка] клип действия %d не идёт (%s) — [ПОРТ] страховка: конец прохода по таймеру %d мс (длительность .jbb)"),
		Action, Why, kClipMs[Action - 60]);
}

// `charEmoteSabzilAction@0xFC5590` (g:604289): пропуск, если действие то же (кроме 61); клип удочки
//   (`sub_935007` → В5); состояние; 4024 — ТОЛЬКО для 60/62/65/66 (stateId 122/124/127/128), курс — направление
//   взгляда (`effectSlots[112].gap_298+12`), общая точка `GetFacingDirGame`. 61/63/64/0 соседям не шлются.
// ★ fishfix 09.10 (ревизия МЕЛК-2): текущее «действие персонажа» в смысле эталона `Net_GetCurrentMapType(свой)` (МИСНЕЙМ:
//   это состояние действия, память net-getcurrentmaptype-is-misname). Порт держит его у персонажа: действие рыбалки 60…67
//   (`GetFishAction`); клип не запустился, но проход ведёт страховка-мост этого действия — оно же (иначе повтор каждый кадр
//   перезапускал бы страховку); мёртв — 6; иначе 0. Персонажа нет — своё поле `SelfAction` (как до правки).
int32 UAsdaFishingSubsystem::GetSelfCharEtalonAction() const
{
	const AAsdaPlayerCharacter* Ch = SelfChar();
	if (!Ch) { return SelfAction; }
	if (Ch->IsFishActionActive()) { return Ch->GetFishAction(); }
	if (BridgeAction >= 60 && BridgeAction == SelfAction) { return SelfAction; }
	if (IsSelfDeadOrBusyFlag()) { return 6; }
	return 0;
}

void UAsdaFishingSubsystem::EmoteAction(int32 Action)
{
	// Пропуск повтора — по действию ПЕРСОНАЖА, как эталон (`curMapType != actionId || actionId == 61`, g:604314):
	//   если действие у персонажа снял кто-то другой (смерть, сброс анимации), повторный вход в 112/113/126/127 клип
	//   перезапускает. Прежде сравнивалось своё поле `SelfAction`, и такой вход молча пропускался.
	if (GetSelfCharEtalonAction() == Action && Action != 61) { return; }
	// ★ fishfix2 09.10 (СРЕДН-1) [ПОРТ] исправление дефекта оригинала: свой персонаж РЕАЛЬНО мёртв — клип 60…67 на труп не
	//   ставим и 4024 не шлём; действие помечается своим, проход доводит страховка по таймеру .jbb (конец «клипа» двигает
	//   автомат 112/113/126/127 → … → 120/121 → 128 → 0, как у живого). Повтор этого же действия гасит сравнение выше
	//   (`GetSelfCharEtalonAction` отдаёт действие моста). Ключ отката `asda.Fish.LegacyDeadClip 1`.
	if (Action >= 60 && Action <= 67 && !IsLegacyDeadClip() && IsSelfReallyDead())
	{
		SelfAction = Action;
		BridgeAction = -1; bBridgeForced = false;
		ArmClipBridgeFallback(Action, TEXT("свой персонаж мёртв — клип на труп не ставится, 4024 не шлём"));
		return;
	}
	SetSelfActionState(Action);
	const int32 StateId = Action + 62;
	if (Action == 60 || StateId == 124 || StateId == 127 || StateId == 128)
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		AAsdaPlayerCharacter* Ch = SelfChar();
		const FVector2D F = Ch ? Ch->GetFacingDirGame() : FVector2D::ZeroVector;
		if (Net) { Net->SendCharStatePos4024(StateId, (float)F.X, (float)F.Y); }
	}
}

// `Fishing_OnStateMessage@0xFC56E0` (g:604345): код = трек конца клипа = action − 33 (0x1B…0x22).
void UAsdaFishingSubsystem::OnFishingClipEnd(int32 Action)
{
	const int32 Code = Action - 33;
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] конец клипа действия %d (код 0x%X), автомат %d"), Action, Code, FishState);
	switch (Code)
	{
	case 0x1B:
		SetState(110, TEXT("конец клипа 60 (заброс)"));
		EmoteAction(61);
		break;
	case 0x1C:
	case 0x1E:
	case 0x1F:
		EmoteAction(61);
		break;
	case 0x1D:
		if (CaughtItemId <= 0)
		{
			SpawnEndFx(TEXT("fish_end_02"));
			Play2D(TEXT("s_e_fishing_fail_00"));
			SetState(127, TEXT("конец клипа 62: сорвалась"));
		}
		else
		{
			SpawnEndFx(TEXT("fish_end_01"));
			SetState(126, TEXT("конец клипа 62: поймал"));
			// Р13: файла `s_e_fishing_success_00.wav` нет ни в одном клиенте — оригинал молчит; зовём как эталон.
			//   ★ Ревизия В1 МЕЛК-11: звуковая подсистема предупреждает об отсутствующем файле — один раз за процесс,
			//   дальше вызов не делается (слышимый итог тот же: звука нет).
			if (!bSuccessSoundWarned)
			{
				bSuccessSoundWarned = true;
				Play2D(TEXT("s_e_fishing_success_00"));
				Play3DAtSelf(TEXT("s_e_fishing_success_00"));
			}
		}
		break;
	case 0x20:
		SetState(120, TEXT("конец клипа 65 (удача)"));
		EmoteAction(0);
		bSelfFishingFlag = false;
		Fs2Ms = NowMs();
		Fs3Ms = NowMs();
		break;
	case 0x21:
		SetState(121, TEXT("конец клипа 66 (неудача)"));
		EmoteAction(0);
		bSelfFishingFlag = false;
		Fs2Ms = NowMs();
		break;
	default:
		break;   // 0x22 (67) и прочие — ветки нет
	}
}

void UAsdaFishingSubsystem::TickClipBridge()
{
	if (BridgeAction < 0) { return; }
	if (CVarFishClipBridge.GetValueOnGameThread() == 0 && !bBridgeForced) { BridgeAction = -1; return; }
	if ((int32)(NowMs() - BridgeEndMs) < 0) { return; }
	const int32 A = BridgeAction;
	BridgeAction = -1;
	bBridgeForced = false;
	OnFishingClipEnd(A);
}

// ═════════════════════════════════ ШКАЛА ═════════════════════════════════════════════════════════

void UAsdaFishingSubsystem::SetGaugeValue(int32 V)
{
	if (V == 0) { GaugeAux = 0.f; }
	Gauge = (float)V;
	if (Gauge > GaugeMax) { Gauge = GaugeMax; }
	if (IAsdaFishMiniGame* M = MG()) { M->OnGaugeChanged(Gauge, GaugeMax); }
}

bool UAsdaFishingSubsystem::SendPhaseResult(int32 Total, int32 Correct)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->SendPhaseResult6169(Net->SelectedCharId, (int32)Gauge, Total, Correct);
}

void UAsdaFishingSubsystem::SetMiniGame(IAsdaFishMiniGame* InMiniGame, UObject* InOwner)
{
	MiniGame = InMiniGame;
	MiniGameOwner = InOwner;
	bMiniGameHasOwner = (InMiniGame != nullptr && InOwner != nullptr);
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] мини-игра: %s"), MiniGame ? TEXT("окно В2") : TEXT("встроенный эмулятор"));
}

bool UAsdaFishingSubsystem::EmuQueueRound(int32 Total, int32 Correct)
{
	if (GetRegisteredMiniGame() || !Emu) { return false; }
	return Emu->QueueRound(Total, Correct);
}

// ═════════════════════════════════ СЕТЬ ═════════════════════════════════════════════════════════

void UAsdaFishingSubsystem::OnNet6164(const TArray<uint8>& B) { PendingPkts.Push({ 6164, B }); }
void UAsdaFishingSubsystem::OnNet6168(const TArray<uint8>& B) { PendingPkts.Push({ 6168, B }); }
void UAsdaFishingSubsystem::OnNet6170(const TArray<uint8>& B) { PendingPkts.Push({ 6170, B }); }
void UAsdaFishingSubsystem::OnNet6172(const TArray<uint8>& B) { PendingPkts.Push({ 6172, B }); }
void UAsdaFishingSubsystem::OnNet6173(const TArray<uint8>& B) { PendingPkts.Push({ 6173, B }); }
void UAsdaFishingSubsystem::OnNet6183(const TArray<uint8>& B) { PendingPkts.Push({ 6183, B }); }

void UAsdaFishingSubsystem::DrainPackets()
{
	TArray<FPkt> Ready;
	if (!PendingPkts.Drain(Ready)) { return; }
	for (const FPkt& P : Ready)
	{
		switch (P.Op)
		{
		case 6164: Apply6164(P.Body); break;
		case 6168: Apply6168(P.Body); break;
		case 6170: Apply6170(P.Body); break;
		case 6172: Apply6172(P.Body); break;
		case 6173: Apply6173(P.Body); break;
		case 6183: Apply6183(P.Body); break;
		default: break;
		}
	}
}

// `uiUpdateStatValueLabel@0xFCAAF0` (g:606445): навык → [15072]; строка 997 — В6 (по делегату).
void UAsdaFishingSubsystem::Apply6164(const TArray<uint8>& B)
{
	if (B.Num() < 4) { return; }
	FishingSkill = AsdaBytes::RdI32(B.GetData());
	bSkillKnown = true;
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6164 навык = %d"), FishingSkill);
	OnFishSkillChanged.Broadcast(FishingSkill);
}

// `netHandleTooltipNoticePacket@0xFCBD80` (g:607015): ВСЕГДА сначала 128; код 1 → 108 + шкала; иначе — msgbox.
void UAsdaFishingSubsystem::Apply6168(const TArray<uint8>& B)
{
	if (B.Num() < 7) { return; }
	const uint8 Code = B[0];
	const int32 V1 = AsdaBytes::RdI32(B.GetData() + 1);
	const int32 Bonus = AsdaBytes::RdI16(B.GetData() + 5);
	// [ПОРТ] Р4: любой 6168 — ответ сервера (в т.ч. коды 4/5 из `Recv_EndFishing`) → защёлка 6171 снимается.
	b6171InFlight = false;
	SetState(128, TEXT("6168: всегда сначала 128"));
	switch (Code)
	{
	case 1:
		Last6168 = B;
		SelectedFishCode = V1;              // 0x3520342
		SetState(108, TEXT("6168 код 1"));
		SetGaugeValue(0);
		SetGaugeValue(Bonus);
		SelfFishCode = V1;                  // [15076]
		break;
	case 2: MsgBox1(LocFmt(kLocAlready), kLocAlready); break;
	case 3: MsgBox1(LocFmt(kLocBagFull), kLocBagFull); break;
	case 4: MsgBox1(LocFmt(kLocForbidden), kLocForbidden); break;
	case 5: MsgBox1(LocFmt(kLocNoRod), kLocNoRod); break;
	case 6: MsgBox1(LocFmt(kLocNoBait), kLocNoBait); break;
	case 7: MsgBox1(LocFmt(kLocLowSkill, { FString::FromInt(V1) }), kLocLowSkill); break;
	case 8: MsgBox1(LocFmt(kLocRodWorn), kLocRodWorn); break;
	default: MsgBox1(FString::Printf(TEXT("%d"), (int32)Code), 0); break;
	}
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6168 код %d, @1=%d, шкала %d → автомат %d"), (int32)Code, V1, Bonus, FishState);
}

// `recvSetInputGaugeValue@0xFCC7A0` (g:607237): код ≤ 1 → шкала; полна и автомат ≠ 118 → 116.
void UAsdaFishingSubsystem::Apply6170(const TArray<uint8>& B)
{
	if (B.Num() < 5) { return; }
	const uint8 Code = B[0];
	if (Code > 1) { UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6170 код %d — игнор (эталон)"), (int32)Code); return; }
	SetGaugeValue(AsdaBytes::RdI16(B.GetData() + 3));
	if (IsGaugeFull() && FishState != 118) { SetState(116, TEXT("6170: шкала полна")); }
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6170 код %d фаза %d шкала %.0f"), (int32)Code, AsdaBytes::RdI16(B.GetData() + 1), Gauge);
}

// `netOnItemUpgradeResult@0xFCCF50` (МИСНЕЙМ, g:607415) — РЕЗУЛЬТАТ РЫБАЛКИ. Копия 121 б — всегда; ветки — при код ≤ 2.
void UAsdaFishingSubsystem::Apply6172(const TArray<uint8>& B)
{
	if (B.Num() < 121) { return; }
	const uint8 Code = B[0];
	b6171InFlight = false;                  // Р4: ответ на 6171 пришёл
	Last6172 = B;                           // 0x3520348
	CaughtSizeCm = AsdaBytes::RdI16(B.GetData() + 5);   // word_352034D
	CaughtItemId = AsdaBytes::RdI32(B.GetData() + 64);  // dword_3520388
	if (Code > 2)
	{
		// ★ Р2: эталон код ≥ 3 игнорирует целиком → в 118 тупик. Порт: отказ → 128 + msgbox с кодом.
		if (FishState == 118 && CVarFishLegacy118.GetValueOnGameThread() == 0)
		{
			// ★ fishfix 09.10 (МЕЛК-1): нотис по смыслу серверной ветки (строки оригинала), «%d» — только прочим кодам.
			const int32 Loc = FishLegacy118LocId(6172, Code);
			MsgBox1(Loc ? LocFmt(Loc) : FString::Printf(TEXT("%d"), (int32)Code), Loc);
			SetState(128, TEXT("Р2: 6172 код ≥3 в ожидании результата"));
		}
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6172 код %d (≥3) — эталон игнорирует; автомат %d"), (int32)Code, FishState);
		return;
	}
	SelfFishSizePct = CaughtSizeCm;          // +226092 [15068]
	SelfFishCode = CaughtItemId;             // +226100 [15076]
	if (Code == 1)
	{
		bSelfHasFish = true;                 // +226090 [15066]
		OnFishCaught.Broadcast(CaughtItemId, CaughtSizeCm);   // `questTableUpdateKeyMinMax` книги (В3)
	}
	else { bSelfHasFish = false; }
	if (Code == 0 || Code == 1) { SetState(119, TEXT("6172 код 0/1")); }
	if (Code == 2) { SetState(128, TEXT("6172 код 2")); }
	// Вес (`HUD_UpdateMiniWeightBar(@3)`, g:607453) — ★ ревизия В1 МЕЛК-8: здесь, в Tick, после смены состояния.
	if (UAsdaNetSubsystem* NetW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		NetW->SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(B.GetData() + 3)), -1, TEXT("6172"));   // ★ 29.10.2026: вес u16
	}
	FishingSkill = AsdaBytes::RdI16(B.GetData() + 1);    // +226096 [15072]
	bSkillKnown = true;
	ChatGold(LocFmt(kLocSkillUp, { FString::FromInt(FishingSkill) }), kLocSkillUp);   // [ОРИГИНАЛ] на ЛЮБОЙ код 0..2
	OnFishSkillChanged.Broadcast(FishingSkill);
	// Записи: код 1 → 2 (наживка @7, рыба @64), иначе 1 (наживка). Общий путь инвентаря (как 6166).
	UGameInstance* GI = GetGameInstance();
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		const int32 N = (Code == 1) ? 2 : 1;
		for (int32 i = 0; i < N; ++i)
		{
			TArray<uint8> R;
			R.Append(B.GetData() + 7 + 57 * i, 57);
			Inv->ApplyExternalRecord(R);
		}
	}
	UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6172 код %d навык %d вес %d размер %d улов %d → автомат %d"),
		(int32)Code, FishingSkill, AsdaBytes::RdI16(B.GetData() + 3), CaughtSizeCm, CaughtItemId, FishState);
}

// `stat_RecvStatSlotUpdate@0xFCAA10` (g:606420): запись в очередь; разбор — каждый кадр.
void UAsdaFishingSubsystem::Apply6173(const TArray<uint8>& B)
{
	if (B.Num() < 11) { return; }
	FPeerRec R;
	R.Body = B;
	R.RecvMs = NowMs();
	PeerQueue.Add(MoveTemp(R));
}

// Только Р2: 6183 коды 4/6 сервер шлёт и из `Recv_EndFishing` (GameServer.c:7152-7184) — в 118 это ответ на 6171.
void UAsdaFishingSubsystem::Apply6183(const TArray<uint8>& B)
{
	if (B.Num() < 1) { return; }
	const uint8 Code = B[0];
	if (FishState != 118 || (Code != 4 && Code != 6)) { return; }   // прочее — книга (В3)
	b6171InFlight = false;
	if (CVarFishLegacy118.GetValueOnGameThread() != 0) { return; }
	// ★ fishfix 09.10 (МЕЛК-1): 4 → 111045, 6 (перевес) → 2252 — строки оригинала по смыслу ветки сервера.
	const int32 Loc = FishLegacy118LocId(6183, Code);
	MsgBox1(Loc ? LocFmt(Loc) : FString::Printf(TEXT("%d"), (int32)Code), Loc);
	SetState(128, TEXT("Р2: 6183 код 4/6 в ожидании результата"));
}

bool UAsdaFishingSubsystem::Send6167()
{
	const FAsdaFishSpotRow* S = GetSpot(PickedSpot);
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!S || !Net)
	{
		UE_LOG(LogAsdaFish, Warning, TEXT("[рыбалка] 6167 не отправлен: зона не выбрана (эталон молча не шлёт)"));
		return false;
	}
	return Net->SendStartFishing6167(Net->SelectedCharId, S->Slot);
}

// `netSendTsMaskedReq6171@0xFCC8D0` (g:607274).
void UAsdaFishingSubsystem::Send6171(int32 Mode)
{
	++DebugSend6171Calls;   // ревизия В1 МЕЛК-2: счётчик вызовов для самотеста
	if (Mode == 2 && !bSelfFishingFlag)
	{
		SetState(128, TEXT("6171(2) без флага «рыбачит» — не шлём (эталон)"));
		return;
	}
	if (b6171InFlight)
	{
		// ★ Р4 (без ключа — защита от дюпа): сервер на повтор до ответа DB даёт второй улов/двойной расход.
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] Р4: 6171(%d) — предыдущий ещё без ответа 6172, не шлём"), Mode);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->SendEndFishing6171(Net->SelectedCharId, Mode, (int32)Gauge)) { b6171InFlight = true; }
}

// ═════════════════════════════════ АВТОМАТ ═══════════════════════════════════════════════════════

void UAsdaFishingSubsystem::SetState(int32 NewState, const TCHAR* Why)
{
	if (NewState != FishState)
	{
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] автомат %d → %d (%s)"), FishState, NewState, Why);
	}
	FishState = NewState;
	if (FishState == 0)
	{
		// [ПОРТ] цикл кончился — защёлки Р3/Р4 следующего цикла начинаются чистыми.
		b6171InFlight = false;
		bDeathCancelCalled = false;
	}
}

bool UAsdaFishingSubsystem::IsSelfDeadOrBusyFlag() const
{
	if (TestDeadOverride >= 0) { return TestDeadOverride != 0; }
	// `sub_971D90`: `effectSlots[112].field_8 == 6` (мёртв) || `charTestFlags(1)` (`field_C & 1`).
	//   Бит занятости `field_C & 1` в порте не заведён (PORT_PENDING_ON_SYSTEMS.md, «Биты занятости»).
	UGameInstance* GI = GetGameInstance();
	AAsdaPlayerController* PC = GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
	return PC && PC->IsSelfDead();
}

bool UAsdaFishingSubsystem::IsSelfReallyDead() const
{
	if (const AAsdaPlayerCharacter* Ch = SelfChar())
	{
		if (Ch->IsIncapacitated()) { return true; }
	}
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->bSelfDead;
}

bool UAsdaFishingSubsystem::IsLegacyDeadClip()
{
	return CVarFishLegacyDeadClip.GetValueOnGameThread() != 0;
}

// `Fishing_UpdateStateMachine@0xFCEE90` (g:608234-608630).
void UAsdaFishingSubsystem::UpdateFsm()
{
	// Перед switch (g:608331-608336): fishState ≠ 0 и (мёртв || флаг 1) → 6171(2) — каждый кадр у эталона.
	// ★ fishfix 09.10 (ревизия `fishing_REVIEW.md`, СРЕДН-1): Р3 глушит ТОЛЬКО провод. Локальная ветка эталона
	//   `netSendTsMaskedReq6171` «режим 2 без флага [15036] → fs = 128» (g:607297-607298) выполняется КАЖДЫЙ кадр, как в
	//   оригинале: как только флаг «рыбачит» снят (0x20/0x21 после клипа 65/66 → 120/121), рыбалка мёртвого кончается и
	//   автоповтор (122 → 100 → 6167) у трупа не наступает. Прежде `bDeathCancelCalled` глушил весь вызов до fs = 0.
	if (FishState != 0 && IsSelfDeadOrBusyFlag())
	{
		if (!bSelfFishingFlag)
		{
			Send6171(2);                 // эталон: флаг 0 → только fs = 128 (без сети), каждый кадр
		}
		else if (CVarFishLegacyCancelSpam.GetValueOnGameThread() != 0 || !bDeathCancelCalled)
		{
			bDeathCancelCalled = true;   // ★ Р3: провод — один вызов на эпизод
			UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] смерть/флаг во время рыбалки (автомат %d) → 6171(2)"), FishState);
			Send6171(2);
		}
	}
	else if (FishState != 0)
	{
		bDeathCancelCalled = false;
	}
	TickReturnAutoAfterMap();   // ★ fishfix 09.10 (МЕЛК-5): в покое ставит 128 — тот же кадр её и выполнит

	switch (FishState)
	{
	case 100:   // 0x64: 6167 → 107
	{
		const bool bSent = Send6167();
		if (!bSent && CVarFishLegacy107.GetValueOnGameThread() == 0)
		{
			SetState(128, TEXT("Р1: 6167 не ушёл — не ждём 6168"));   // ★ Р1
			break;
		}
		SetState(107, TEXT("6167 отправлен — ждём 6168"));
		Fs1Ms = NowMs();
		break;
	}
	case 101:   // 0x65: книга (В3) или 100
	{
		const bool bStay = BookRegisterProbe.IsBound() && BookRegisterProbe.Execute();
		if (!bStay) { SetState(100, TEXT("101: регистрировать в книге нечего")); }
		break;
	}
	case 102:   // 0x66: дистанция + снаряжение
	{
		if (PickedSpot < 0) { break; }   // `if (g_pPickedMapObject)`
		FVector2D Delta;
		if (IsFarFromPicked(Delta))
		{
			ChatGold(LocFmt(kLocTooFar), kLocTooFar);
			SetState(0, TEXT("102: далеко от зоны"));
		}
		else if (HasRodEquipped() && HasBaitEquipped())
		{
			StopAndFaceZone(Delta, TEXT("рыбалка 102: стоп и поворот к зоне"));
			SetState(101, TEXT("102: снасть на месте"));
		}
		else
		{
			// Ветка эталона: поворот к зоне (без стопа), 110701, 0.
			if (AAsdaPlayerCharacter* Ch = SelfChar()) { if (!Delta.IsNearlyZero()) { Ch->FaceGameDir(Delta.GetSafeNormal()); } }
			ChatGold(LocFmt(kLocNoGear), kLocNoGear);
			SetState(0, TEXT("102: нет удочки или наживки"));
		}
		break;
	}
	case 103:   // 0x67: авто-снаряжение → 104
		if (HasRodEquipped())
		{
			if (!HasBaitEquipped()) { UseFirstBait(); }
		}
		else
		{
			UseFirstRod();
		}
		if (FishState != 128) { SetState(104, TEXT("103: ждём ответ 82/83")); }
		break;
	case 105:   // 0x69: авто-наживка → 106
		UseFirstBait();
		if (FishState != 128) { SetState(106, TEXT("105: ждём ответ 83")); }
		break;
	case 108:   // 0x6C: флаг, звук старта → 112
		bSelfFishingFlag = true;
		Play2D(TEXT("s_e_fishing_start_00"));
		SetState(112, TEXT("108: старт"));
		break;
	case 109:   // 0x6D: дистанция → стоп/поворот → диалог 81
	{
		if (PickedSpot < 0) { break; }
		FVector2D Delta;
		if (IsFarFromPicked(Delta))
		{
			ChatGold(LocFmt(kLocTooFar), kLocTooFar);
			SetState(0, TEXT("109: далеко от зоны"));
		}
		else
		{
			StopAndFaceZone(Delta, TEXT("рыбалка 109: стоп и поворот к зоне"));
			ShowFishDialog(81, LocFmt(kLocFishHere));
		}
		break;
	}
	case 110:   // 0x6E: окно → 114
		MG()->FishWinShow();
		SetState(114, TEXT("110: окно показано"));
		break;
	case 112:   // 0x70: заброс (60); выход — конец клипа
		EmoteAction(60);
		break;
	case 113:   // 0x71: вытаскивание (62); выход — конец клипа
		EmoteAction(62);
		break;
	case 114:   // 0x72: раунды → гайд → 115
	{
		const FAsdaFishDataRow* F = FindFishData(SelectedFishCode);
		int32 Rounds = F ? F->Rounds : kNoFishDataRounds;
		if (F && F->Rounds < 0) { Rounds = 1; }
		// ★ Р10: при отсутствии записи эталон читает `*(0 + 8)` и падает — у нас 1 раунд (ключа нет).
		MG()->GuideStart(Rounds);
		SetState(115, TEXT("114: гайд запущен"));
		break;
	}
	case 115:   // 0x73: тик гайда
	{
		const int32 R = MG()->GuideTick();
		if (R == 5)
		{
			const int32 Pick = FMath::Rand() % 2;
			Play2D(TEXT("s_e_fishing_fishing_00"));
			EmoteAction(Pick ? 64 : 63);
		}
		else if (R == 8)
		{
			SetState(116, TEXT("115: все раунды"));
		}
		break;
	}
	case 116:   // 0x74
		SetState(117, TEXT("116"));
		break;
	case 117:   // 0x75: 6171(1) → 118
		Send6171(1);
		SetState(118, TEXT("117: 6171(1) — ждём 6172"));
		break;
	case 119:   // 0x77
		SetState(113, TEXT("119"));
		break;
	case 120:   // 0x78: всплывашка 3 с → 121
		if (!MG()->IsCatchPopupShown()) { MG()->ShowCatchPopup(CaughtItemId); }
		if (NowMs() - Fs3Ms > 3000u)
		{
			Fs2Ms = NowMs();
			MG()->HideCatchPopup();
			SetState(121, TEXT("120: 3 с всплывашки"));
		}
		break;
	case 121:   // 0x79: 5 с отсчёт перезаброса
	{
		const uint32 Now = NowMs();
		if (Now - Fs2Ms > 5000u) { SetState(122, TEXT("121: отсчёт кончился")); }
		SetGaugeValue((int32)((double)(Now - Fs2Ms) / 10.0));
		if (Now - CountdownNoticeMs > 1000u)
		{
			CountdownNoticeMs = Now;
			// `abs(ftol((now − fs2)/1000.0 − 5000/1000.0))` — усечение к нулю, как `__ftol2`.
			const int32 D = FMath::Abs((int32)((double)(NowMs() - Fs2Ms) / 1000.0 - 5000.0 / 1000.0));
			PlankGold(LocFmt(kLocCountdown, { FString::FromInt(D) }), kLocCountdown);
		}
		if (!bAutoRepeat) { SetState(128, TEXT("121: автоповтор выключен")); }
		break;
	}
	case 122:   // 0x7A: перезаброс
		bSelfFishingFlag = false;
		SetState(100, TEXT("122: перезаброс"));
		break;
	case 126:   // 0x7E: удача (65)
		EmoteAction(65);
		break;
	case 127:   // 0x7F: неудача (66)
		EmoteAction(66);
		break;
	case 128:   // 0x80: конец
		ReturnAutoEquipped();
		bReturnAutoAfterMap = false;   // ★ fishfix 09.10 (МЕЛК-5): флаги возвращены этим же 128
		SetSelfActionState(0);
		bSelfFishingFlag = false;
		MG()->PanelHideAndClear();
		SetState(0, TEXT("128: конец рыбалки"));
		break;
	default:    // 0, 104, 106, 107, 111, 118 — ожидание
		break;
	}
}

// `charApplyStateFromList@0xFCAC60` (g:606480): поиск по charId, поворот к ближайшей зоне в радиусе, фаза.
void UAsdaFishingSubsystem::ApplyPeerQueue()
{
	if (PeerQueue.Num() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const bool bLegacyQueue = CVarFishLegacyOtherQueue.GetValueOnGameThread() != 0;
	const uint32 Ttl = (uint32)FMath::Max(0, CVarFishOtherQueueTtlMs.GetValueOnGameThread());
	// ★ Ревизия В4 МЕЛК-5 (09.10.2026): зоны — объекты списка 0x35202D4 (`sub_936E16(dword_35202D4)` в `charApplyStateFromList`),
	//   т.е. актёры `UAsdaFishZoneSubsystem`, а не таблица спотов по номеру карты (единый источник с cmd 8 и пикингом).
	const UAsdaFishZoneSubsystem* ZS = GI ? GI->GetSubsystem<UAsdaFishZoneSubsystem>() : nullptr;
	static const TArray<TWeakObjectPtr<AAsdaFishZoneActor>> kNoZones;   // подсистемы зон нет — список пуст
	const TArray<TWeakObjectPtr<AAsdaFishZoneActor>>& ZoneList = ZS ? ZS->GetZones() : kNoZones;   // список 0x35202D4
	for (int32 i = 0; i < PeerQueue.Num();)
	{
		const TArray<uint8>& B = PeerQueue[i].Body;
		const uint8 Phase = B[0];
		const int32 CharId = AsdaBytes::RdI32(B.GetData() + 1);
		AActor* A = WS ? WS->FindPlayerActorByCharId(CharId) : nullptr;
		if (!A)
		{
			if (!bLegacyQueue && NowMs() - PeerQueue[i].RecvMs > Ttl)
			{
				// ★ Р5: эталон оставляет такую запись навсегда (итератор просто сдвигается, g:606616).
				UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6173: charId %d так и не найден за %u мс — запись снята (Р5)"), CharId, Ttl);
				PeerQueue.RemoveAt(i);
				continue;
			}
			++i;
			continue;
		}
		// Поворот к ближайшей зоне (зоны текущей карты, радиус `entry+12 · 100`, строго «>»).
		AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(A);
		const FVector2D P = Ch ? Ch->GetGamePosXZ() : AsdaCoord::UeToGame2D(A->GetActorLocation());
		float BestD = 999999.f;
		FVector2D BestDelta = FVector2D::ZeroVector;
		bool bAny = false;
		for (const TWeakObjectPtr<AAsdaFishZoneActor>& Zw : ZoneList)
		{
			const AAsdaFishZoneActor* ZA = Zw.Get();
			const FAsdaFishSpotRow* S = ZA ? GetSpot(ZA->SpotIdx) : nullptr;   // `sub_91FB12(g_pMap, obj[115])`
			if (!S) { continue; }
			const FVector2D Delta = FVector2D(ZA->CenterGame.X, ZA->CenterGame.Z) - P;   // (obj[129], obj[131])
			const float D = Delta.Size();
			if ((float)S->Radius * AsdaCoord::kTileUnits > D && D < BestD) { BestD = D; BestDelta = Delta; bAny = true; }   // ★ N11
		}
		if (bAny && Ch && !BestDelta.IsNearlyZero()) { Ch->FaceGameDir(BestDelta.GetSafeNormal()); }

		FAsdaFishPeerState& St = PeerStates.FindOrAdd(CharId);
		St.CharId = CharId;
		St.Phase = Phase;
		if (Phase == 3)
		{
			St.Action = (FMath::Rand() % 2) ? 64 : 63;   // подсечка; поля флагов эталон в этой ветке не трогает
		}
		else
		{
			int32 Act = -1;
			if (Phase != 0) { Act = 61; }
			if (Phase == 2) { Act = 0; }
			St.Action = Act;
			St.bFishingFlag = (Phase != 0 && Phase != 2);                // [15036]
			St.FishCode = AsdaBytes::RdI32(B.GetData() + 5);             // [15076]
			St.SizePct = AsdaBytes::RdI16(B.GetData() + 9);              // [15068]
			St.bHasFish = (St.FishCode != 0);                            // [15066]
		}
		UE_LOG(LogAsdaFish, Log, TEXT("[рыбалка] 6173 чужой charId %d: фаза %d действие %d флаг %d рыба %d размер %d"),
			CharId, (int32)Phase, St.Action, St.bFishingFlag ? 1 : 0, St.FishCode, St.SizePct);
		OnPeerFishState.Broadcast(A, St);
		PeerQueue.RemoveAt(i);
	}
}

// ═════════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════

FString UAsdaFishingSubsystem::DumpState() const
{
	const FAsdaFishSpotRow* S = GetSpot(PickedSpot);
	FString Out = FString::Printf(
		TEXT("═══ РЫБАЛКА: автомат %d, зона %d (%s), шкала %.0f/%.0f, автоповтор %d, пауза %d ═══\n")
		TEXT("  свой: действие %d, флаг %d, рыба на крючке %d, размер %d%%, FishCode %d, навык %d (%s)\n")
		TEXT("  6168: FishCode %d; 6172: улов %d, размер %d см; 6171 в полёте %d; авто-удочка %d (ячейка %d), авто-наживка %d (ячейка %d)\n")
		TEXT("  мост клипов: действие %d; очередь 6173: %d; чужих в памяти: %d; мини-игра: %s; таблицы %d/%d/%d"),
		FishState, PickedSpot,
		S ? *FString::Printf(TEXT("карта %d слот %d X %d Y %d r %d умение %d"), S->MapId, S->Slot, S->X, S->Y, S->Radius, S->Skill) : TEXT("нет"),
		Gauge, GaugeMax, bAutoRepeat ? 1 : 0, bPaused ? 1 : 0,
		SelfAction, bSelfFishingFlag ? 1 : 0, bSelfHasFish ? 1 : 0, SelfFishSizePct, SelfFishCode, FishingSkill,
		bSkillKnown ? TEXT("пришёл") : TEXT("не пришёл"),
		SelectedFishCode, CaughtItemId, CaughtSizeCm, b6171InFlight ? 1 : 0,
		bAutoRod ? 1 : 0, AutoRodBagCell, bAutoBait ? 1 : 0, AutoBaitCell,
		BridgeAction, PeerQueue.Num(), PeerStates.Num(), GetRegisteredMiniGame() ? TEXT("окно В2") : TEXT("эмулятор"),
		FishRows.Num(), NumRows.Num(), SpotRows.Num());
	return Out;
}

// ═════════════════════════════════ КОНСОЛЬ ═══════════════════════════════════════════════════════

// Подсистема рыбалки мира W (nullptr — нет).
static UAsdaFishingSubsystem* FishOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
}

// Вывод консольной команды: журнал + строки «Подсказка» в ленту.
// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): своя копия эха → общая `AsdaNotice::ConsoleEcho`. Было: пустые строки
//   отбрасывались и в журнале, а строки из одних пробелов уходили в ленту; стало как у всех эхо: пустые строки — в журнал,
//   в ленту — только строки с видимым текстом.
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
static void FishEcho(UWorld* W, const FString& Text) { AsdaNotice::ConsoleEcho(W, Text, LogAsdaFish); }
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFishDump(
	TEXT("asda.Fish.Dump"),
	TEXT("Рыбалка: состояние автомата, выбранная зона, шкала, свои поля, очередь 6173, мост клипов"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishingSubsystem* F = FishOf(W)) { FishEcho(W, F->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFishStart(
	TEXT("asda.Fish.Start"),
	TEXT("Рыбалка: как доп-меню «рыбалка» (targetSelectNearestEntity) — ближайшая зона текущей карты в радиусе → 109 ")
	TEXT("(диалог 81 «Хочешь здесь порыбачить?»); нет зоны — чат 110700. Это путь игрока, 6167 уйдёт после OK."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishingSubsystem* F = FishOf(W)) { F->StartAtNearestSpot(); FishEcho(W, F->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFishCancel(
	TEXT("asda.Fish.Cancel"),
	TEXT("Рыбалка: как X 71328 / ESC окна рыбалки (до волны В2) — диалог 80 «Прекратить рыбалку?»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishingSubsystem* F = FishOf(W)) { F->RequestCancelDialog80(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFishEmuRound(
	TEXT("asda.Fish.EmuRound"),
	TEXT("asda.Fish.EmuRound <всего=4> <верно=4> — [ПОРТ] до волны В2: завершить раунд мини-игры встроенного эмулятора ")
	TEXT("(уйдёт 6169 с этими числами и текущей шкалой, как из ключ-гайда)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaFishingSubsystem* F = FishOf(W);
		if (!F) { return; }
		const int32 T = A.Num() >= 1 ? FCString::Atoi(*A[0]) : 4;
		const int32 C = A.Num() >= 2 ? FCString::Atoi(*A[1]) : T;
		FishEcho(W, FString::Printf(TEXT("рыбалка: раунд эмулятора (%d/%d) %s"), C, T,
			F->EmuQueueRound(T, C) ? TEXT("поставлен") : TEXT("НЕ поставлен (гайд не запущен или зарегистрировано окно В2)")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFishClipEnd(
	TEXT("asda.Fish.ClipEnd"),
	TEXT("asda.Fish.ClipEnd <действие 60…67> — вызвать конец клипа своего действия (Fishing_OnStateMessage) вручную"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaFishingSubsystem* F = FishOf(W);
		if (!F || A.Num() < 1) { return; }
		F->OnFishingClipEnd(FCString::Atoi(*A[0]));
		FishEcho(W, F->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE
