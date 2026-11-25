// Copyright (c) 2026 @sitxovski (Discord)

#include "UI/AsdaSkillWindowSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока

#include "UI/AsdaSystemMenuSubsystem.h"   // кнопка меню 73403
#include "World/AsdaPlayerController.h"      // клавиша «K» (команда 32)

#if PLATFORM_WINDOWS
// ★ Системный интервал двойного клика: оригинал получает событие 0xD от самой Windows
//   (`WM_LBUTTONDBLCLK`), то есть по значению «Панель управления → Мышь». В Slate такого запроса нет
//   (`FSlateApplication` умеет только маршрутизировать уже готовое событие), поэтому берём его у ОС
//   тем же вызовом, каким его берёт оконная процедура оригинала.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "AsdaSkillWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillWindowSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaSkillWin);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (жалоба «All Skill Reset Scroll … hotbar skill icons populated»): ревизия модели выученного `Learned`.
//   Растёт при каждом её изменении (`DrainPending`: 6057/5462/5251/6056/5252/5254; сброс сессии). Панель быстрого доступа сверяет
//   её в своём тике и перекрашивает ТОЛЬКО слоты навыков (`UAsdaHotbarSubsystem::Tick`, ключ `asda.Quick.GreyUnlearnedSkill`):
//   эталон после сброса хотбар не трогает вовсе. Счётчик, а не делегат — без правки заголовков (оба класса UCLASS).
uint32 GAsdaSkillLearnedRev = 0;
// Текущая ревизия модели выученных навыков (читает панель быстрого доступа; объявление — `extern` в AsdaHotbarSubsystem.cpp).
uint32 AsdaSkillLearnedRevision() { return GAsdaSkillLearnedRev; }

// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 — 5274 «откат навыка истёк» как точка синхронизации с сервером. Сервер (`sub_47B9C0@0x47B9C0`)
//   снижает откат навыка источниками, которых клиент оригинала не считает: камни/снаряжение типа 28 (`sub_442810`, ×1,3 при
//   +0x1C1 == 10), проценты у 756/764 (CUser +42518/+42519) и 767–769 (+42542/+42543), процент у 930 (+88374/+88386) — и по
//   истечении шлёт 5274 (`sub_47BD60@0x47BD60`). Эталон 5274 к реестру не применяет (`pktHandlerCharFxStop@0xF4EDF0` →
//   `sub_93B5B5` = пустая `sub_E57C90`), поэтому при такой скидке навык у него «в откате» дольше сервера. Ключ 0 — как эталон.
static ASDA_DEV_CVAR(int32, CVarCooldownSkill5274Sync,
	TEXT("asda.Cooldown.Skill5274Sync"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ 25.10.2026] 5274 (сервер: откат навыка истёк) снимает откат навыка в реестре клиента: 1 = да (сервер "
	     "учитывает скидки, которых клиент не знает); 0 = как оригинал (5274 к откату не применяется)."),
	ECVF_Default);

// ★ [ОРИГИНАЛ] 25.10.2026 (ревизия `Reports/wave_1025b_review/REVIEW.md`, 1.1) — ОТКАТЫ НАВЫКОВ В ПОРЯДКЕ ПРИХОДА ПАКЕТОВ.
//   Эталон разбирает 5465 (`Recv_SkillCoolTime@0xE4B180`: очистка реестра + записи пакета), 5271 (`recvOtherCharSkillUse@0xF4D7D0`
//   → `skillStartGroupCooldown@0xE57860`) и 5274 синхронно, ровно в порядке прихода. У порта 5465 ждал тика книги, 5271 — тика
//   диспетчера навыков (порядок двух тиков не задан), а 5274 применялся сразу. Итог в одном кадре (вход, портал): снимок мог стереть
//   откат, взведённый 5271 ПОСЛЕ него, и вернуть откат, который 5274 уже снял. Теперь 5465 применяется в момент приёма (только
//   реестры, без UI и сети — закон no-reentrant-net-callbacks; сетка перерисовывается в `DrainPending`), а отложенный взвод 5271
//   несёт номер прихода и пропускается, если после него пришли снимок 5465 или 5274 этого навыка (`IsCooldownArmSuperseded`).
//   0 — прежнее: 5465 в очереди книги, номера прихода не сверяются.
ASDA_DEV_CVAR(int32, CVarCooldownArrivalOrder,
	TEXT("asda.Cooldown.ArrivalOrder"),
	1,
	TEXT("[ОРИГИНАЛ 25.10.2026] Откаты навыков 5465/5271/5274 применяются в порядке прихода пакетов: 1 = да (5465 сразу при приёме, "
	     "взвод 5271 отменяется, если после него пришли 5465 или 5274); 0 = прежнее (5465 в тике книги, порядок не сверяется)."),
	ECVF_Default);

// ══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaSkillWindowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	// ★ Аудит №3 (A12): база навыков — САМОСТОЯТЕЛЬНАЯ подсистема (как `g_pSkillInfoTable` оригинала),
	//   а не объект, создаваемый окном. Окно её только берёт; данные живут и когда книга не открывалась.
	Collection.InitializeDependency(UAsdaSkillDatabase::StaticClass());
	Db = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>() : nullptr;

	Slots.SetNum(45);
}

void UAsdaSkillWindowSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSkillListFull.RemoveAll(this);
			Net->OnSkillListUpdate.RemoveAll(this);
			Net->OnSkillListCompact.RemoveAll(this);
			Net->OnSkillUpgradeResult.RemoveAll(this);
			Net->OnSkillTreeReset.RemoveAll(this);
			Net->OnSkillSlotsReset.RemoveAll(this);
			Net->OnSkillsRealigned.RemoveAll(this);
			Net->OnSkillCooldowns.RemoveAll(this);
			Net->OnSkillCooldownReady.RemoveAll(this);   // ★ 25.10.2026: синхронизация по 5274
			Net->OnSelfSkillPoints.RemoveAll(this);
			Net->OnSelfJobChanged.RemoveAll(this);   // ★ 18.10.2026
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterUiZone(kSkillWinId);
			WM->UnregisterWheelHandler(kSkillWinId);
		}
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.RemoveDynamic(this, &UAsdaSkillWindowSubsystem::OnMenuCommand);
			Menu->UnregisterWindow(kMenuButtonIdNum);
		}
	}
	HideOwnTooltip();
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 — СВОБОДНЫЕ ОЧКИ НАВЫКОВ ПРОШЛОЙ УЧЁТКИ (ревизия `Reports/inventory_relog_leak/REVIEW.md`, Н3).
//   Было: `SkillPoints` (копия окна) не сбрасывался вовсе, а очередь `PendingPoints` сброс 1021 (`EnteringWorld`) чистил. Очки новой
//   учётки приходят блоком 1017 РАНЬШЕ 1021 ([[session-reset-drops-early-data]]): если 1017 и 1021 разобраны в одном кадре, тик не
//   успевал применить очки, сброс выбрасывал их из очереди, и окно навыков Б показывало свободные очки А и доступность «Изучить».
//   У оригинала копии нет: окно печатает `CCharacter+0x443` (`skillUiUpdatePointText@0xE56720`), который перезаписывает сам 1017.
//   Решение: `LeftWorld` — копия «не приходило» (−1); `EnteringWorld` — копия = зеркало сети `SelfSkillPoints` (его пишет 1017 и сброс
//   1021 его НЕ трогает, `AsdaNetSubsystem_Session.cpp`), то есть очки входящего персонажа, даже если очередь выброшена.
//   `MapChanged` и `Disconnected` копию не трогают (персонаж тот же).
//   Ключ: 1 — так (по умолчанию); 0 — прежнее поведение (копия переживала перезаход).
static ASDA_DEV_CVAR(int32, CVarSkillRelogResetPoints,
	TEXT("asda.SkillWin.RelogResetPoints"),
	1,
	TEXT("Свободные очки навыков при выходе на логин / входе в мир: 1 = выход — сброс, вход — из блока 1017 (исправление "
	     "перезахода, 25.10.2026); 0 = как было — копия окна переживала перезаход."),
	ECVF_Default);

// ★ 29.10.2026 [ИСПРАВЛЕНИЕ]: очереди пакетов навыков при смене карты (разбор — в `ResetSessionState`).
static ASDA_DEV_CVAR(int32, CVarSkillKeepQueuesOnMapChange,
	TEXT("asda.SkillWin.KeepQueuesOnMapChange"),
	1,
	TEXT("1 (по умолчанию) — при смене карты (портал) очереди пакетов навыков (ответ изучения 5254, открывшиеся навыки 5252, очки)\n")
	TEXT("сохраняются и разбираются на новой карте; 0 — прежнее поведение: смена карты их выбрасывала."),
	ECVF_Default);

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaSkillWindowSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Очереди пакетов навыков (5460/5461/5462/…) — тела ЭТОЙ сессии.
	// ★ 29.10.2026 [ИСПРАВЛЕНИЕ] (ключ asda.SkillWin.KeepQueuesOnMapChange, баг игроков «очки навыков не открывают навыки,
	//   помогает перезапуск»): при смене карты персонаж тот же, а очереди — его же пакеты (5254 ответ изучения, 5252 открывшиеся
	//   навыки, очки), принятые по прежнему соединению и ещё не разобранные тиком. Выбрасывать их нельзя: сервер на портале
	//   список заново не шлёт (`sub_476E10`, ветка v9==1), и вложенное очко «терялось» до перезахода. Оригинал очередей не имеет —
	//   пакет применяется сразу. Полный сброс (новый персонаж / разрыв) чистит, как прежде.
	if (AsdaIsFullSessionReset(Reason) || CVarSkillKeepQueuesOnMapChange.GetValueOnGameThread() == 0)
	{
		PendingFull.Clear(); PendingUpdate.Clear(); PendingCompact.Clear();
		PendingResult.Clear(); PendingCool.Clear(); PendingPoints.Clear();
		FScopeLock Lock(&PendingFlagsLock);
		bPendingFullReset = false;
	}
	PendingLearnCode = -1;
	// ⚠ `Cooldowns` при смене карты НЕ чистим — это счёт СЕРВЕРА по персонажу, а не по карте.
	CedTimers.Reset();
	bDragActive = false; DragSrcSlot = -1; DragSkillCode = -1;
	DragArm.Disarm();
	HoverSlot = -1;
	DragMouse.Reset(); ScrollMouse.Reset();
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] сброс сессии (%s): изучено %d — список снят"),
		AsdaSessionResetReasonName(Reason), Learned.Num());
	HideOwnTooltip();
	CloseWindow();
	Learned.Reset();
	++GAsdaSkillLearnedRev;   // ★ 26.10.2026: список снят — панель быстрого доступа больше не гасит навыки до прихода нового
	Cooldowns.Reset();
	CooldownReleasedSeq.Reset();   // ★ ревизия 1.1: номера 5274 прошлой сессии (счётчик монотонный — его не сбрасываем)
	SubJobOverride = -1;
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 (Н3): шапка ключа `asda.SkillWin.RelogResetPoints` выше.
	if (!AsdaSessionResetKeepsWorldAlive(Reason) && CVarSkillRelogResetPoints.GetValueOnGameThread() != 0)
	{
		const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const int32 Was = SkillPoints;
		SkillPoints = (Reason == EAsdaSessionResetReason::EnteringWorld && Net) ? Net->SelfSkillPoints : -1;
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] сброс сессии (%s): свободные очки %d → %d"),
			AsdaSessionResetReasonName(Reason), Was, SkillPoints);
	}
}

void UAsdaSkillWindowSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.SkillWindow");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// Ленивое связывание с сетью: подсистема окна поднимается раньше, чем появляется соединение.
	if (!bNetBound)
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSkillListFull.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetListFull);
			Net->OnSkillListUpdate.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetListUpdate);
			Net->OnSkillListCompact.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetListCompact);
			Net->OnSkillUpgradeResult.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetUpgradeResult);
			Net->OnSkillTreeReset.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetTreeReset);
			Net->OnSkillSlotsReset.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetSlotsReset);
			Net->OnSkillsRealigned.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetSkillsRealigned);
			Net->OnSkillCooldowns.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetCooldowns);
			// ★ 25.10.2026: 5274 — сервер отпустил откат навыка (ключ `asda.Cooldown.Skill5274Sync`, шапка выше). Колбэк трогает
			//   только свой реестр (без UI и сети — закон no-reentrant-net-callbacks).
			Net->OnSkillCooldownReady.AddWeakLambda(this, [this](int32 SkillId)
			{
				if (CVarCooldownSkill5274Sync.GetValueOnGameThread() == 0) { return; }
				// ★ Ревизия 1.1: номер прихода — отложенный взвод 5271, пришедший РАНЬШЕ этого 5274, откат уже не вернёт.
				if (CVarCooldownArrivalOrder.GetValueOnGameThread() != 0) { CooldownReleasedSeq.Add(SkillId, NextCooldownArrivalSeq()); }
				if (const FSkillCooldown* C = Cooldowns.Find(SkillId))
				{
					const float Left = CooldownRemainLive(*C);
					Cooldowns.Remove(SkillId);
					UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 5274: сервер отпустил откат навыка %d (у клиента оставалось %.2f с)"), SkillId, Left);
				}
			});
			Net->OnSelfSkillPoints.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetSelfSkillPoints);
			Net->OnSelfJobChanged.AddUObject(this, &UAsdaSkillWindowSubsystem::OnNetSelfJobChanged);   // ★ 18.10.2026
			// Блок 1017 мог прийти РАНЬШЕ, чем поднялась эта подсистема, — забираем уже полученное число.
			if (Net->SelfSkillPoints >= 0)
			{
				PendingPoints.Push(int32(Net->SelfSkillPoints));   // ★ Аудит №3 (B10): замок — в очереди
			}
			bNetBound = true;
		}
	}

	// ★ Кнопка главного меню 73403 и клавиша «K» (`INTERFACE SKILL`, команда 32 из Shortcutkey.ini).
	//   Схема ровно как у инвентаря: подписываемся ЛЕНИВО — меню и контроллер появляются позже
	//   подсистемы. DYNAMIC-делегаты ⇒ `AddDynamic`.
	if (!bMenuBound)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.AddDynamic(this, &UAsdaSkillWindowSubsystem::OnMenuCommand);
			// ★★★ ЕДИНЫЙ МЕХАНИЗМ (эталон: подсветку кнопки ставит САМО окно — `uiPanel_SetTabVisible
			//   (g_pSystemMainWnd, 7, 1)` в конце `skillwin_OpenSkillWindow@0xE52DE0` и `…, 7, 0`
			//   в конце `uiPanel_Hide@0xE53240`). Регистрируемся один раз — дальше подсветка кнопки
			//   73403 выводится из фактической видимости `SKILL_WIN` панелью меню, для любого пути
			//   открытия/закрытия: клавиша «K», кнопка 73403, крестик SKILL_WIN_CLOSEBTN, консоль.
			Menu->RegisterWindow(kMenuButtonIdNum);
			bMenuBound = true;
			UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] подписка на кнопку меню %d выполнена"), kMenuButtonIdNum);
		}
	}
	if (UWorld* W = GI->GetWorld())
	{
		AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
		if (Pc && Pc != BoundController.Get())
		{
			if (AAsdaPlayerController* Old = BoundController.Get())
			{
				Old->OnGameCommand.RemoveDynamic(this, &UAsdaSkillWindowSubsystem::OnGameCommand);
			}
			Pc->OnGameCommand.AddDynamic(this, &UAsdaSkillWindowSubsystem::OnGameCommand);
			BoundController = Pc;
			UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] подписка на клавишу «Навыки» (команда %d) выполнена"),
				kCmdInterfaceSkill);
		}
	}

	DrainPending();

	// Кулдауны узлов (клиентский тик; авторитет — сервер, 5465).
	if (Cooldowns.Num() > 0)
	{
		TArray<int32> Done;
		for (TPair<int32, FSkillCooldown>& It : Cooldowns)
		{
			// ★ [ИСПРАВЛЕНИЕ] 25.10.2026: остаток — от момента готовности по реальным часам (`asda.Cooldown.RealClock`).
			//   `DeltaTime` мира зажат 0,4 с/кадр (`MaxUndilatedFrameTime`): на рывке (загрузка, портал) откат «стоял».
			if (AsdaCooldownRealClockOn() && It.Value.EndS > 0.0) { It.Value.RemainSec = CooldownRemainLive(It.Value); }
			else { It.Value.RemainSec -= DeltaTime; }
			if (It.Value.RemainSec <= 0.f) { Done.Add(It.Key); }
		}
		for (int32 C : Done)
		{
			Cooldowns.Remove(C);
			// ★ Эталон `uiSkillTreeShowUpgradeCost@0xE474C0` по коду навыка считает слот и при слоте
			//   <= 44 показывает `SKILLTREE_CED{NN}` — «вспышку окончания кулдауна». Держим её
			//   ровно один прогон плана (длительность берём из самих данных плана, не подбираем).
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				if (Slots[i].SkillCode != C || !Slots[i].Ced) { continue; }
				float Secs = CedFlashSeconds;
				if (const UAsdaAnimatedPlane* Pl = Cast<UAsdaAnimatedPlane>(Slots[i].Ced))
				{
					if (Pl->FrameIntervalMs > 0.f) { Secs = Pl->FrameIntervalMs / 1000.f; }
				}
				if (UAsdaAnimatedPlane* Pl = Cast<UAsdaAnimatedPlane>(Slots[i].Ced)) { Pl->RestartAnim(); }
				Slots[i].Ced->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				CedTimers.Add(i, Secs);
			}
		}
		if (IsWindowOpen()) { RebuildGrid(); }
	}

	// Гашение вспышек CED по истечении прогона плана.
	if (CedTimers.Num() > 0)
	{
		TArray<int32> Off;
		for (TPair<int32, float>& It : CedTimers)
		{
			It.Value -= DeltaTime;
			if (It.Value <= 0.f) { Off.Add(It.Key); }
		}
		for (int32 I : Off)
		{
			CedTimers.Remove(I);
			if (Slots.IsValidIndex(I) && Slots[I].Ced) { Slots[I].Ced->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}

	// ★ Шторки отката узлов — покадрово, пока окно открыто (отступление 2026-08-14, см. метод).
	//   Стоит ПОСЛЕ `RebuildGrid` выше: тот меняет `SkillCode` узлов, а шторка адресуется по коду.
	if (IsWindowOpen()) { TickCooldownCurtains(); }

	if (IsWindowOpen()) { TickPointer(); }
	else if (bTipShown)  { HideOwnTooltip(); }
}

// ══════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════
// ★★★ ИСПРАВЛЕНО: здесь стояло `WM->IsOpen(kSkillWinId)` — «окно ЗАРЕГИСТРИРОВАНО в менеджере».
//   `CloseWindow` окно не снимает с регистрации, а лишь прячет (`SetWindowVisible(false)`), поэтому
//   после первого открытия проверка навсегда возвращала true: `ToggleWindow` по клавише «K» вместо
//   открытия каждый раз ЗАКРЫВАЛ уже закрытое окно, а кнопка меню 73403 расходилась с окном.
//   Правильный вопрос — «окно сейчас ВИДНО»: `IsWindowVisible` (одно определение на весь порт).
bool UAsdaSkillWindowSubsystem::IsWindowOpen() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return Win != nullptr && WM != nullptr && WM->IsWindowVisible(kSkillWinId);
}
