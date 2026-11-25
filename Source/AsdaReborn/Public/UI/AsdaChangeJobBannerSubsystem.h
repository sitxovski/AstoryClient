// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ БАННЕР СМЕНЫ ПРОФЕССИИ `ChangeJob_Banner` (JUI 18500) + ВЕТКА СМЕНЫ ПРОФЕССИИ ПАКЕТА 5042.
//   Волна 2-В (06.10.2026), отчёт `Reports/owner_1006/wave2C_duel_mark_banner.md` §3.
//
//   ЭТАЛОН (класс `CChangeJobBanner`, глобал `g_pChangeJobBanner` = 0x2BE55D0, одна сущность на ДВА баннера:
//   18500 «смена профессии» и 18550 `Con_Banner2` «уровень/звание»; второй в порту — `UAsdaHudSubsystem::ShowBanner`):
//     • `CChangeJobBanner_InitUI@0xBD47A0` (ui_core.c:17724-17895) — контролы 18500/18599/18598/18560…18567/18501/18502+i;
//       якорь `bannerAnchorY` = posY корня (JUI Origin 0) и ширины `bannerRectX` (320) / `becomeRectX` (130).
//     • `changeJobBannerStart@0xBD4F40` (ui_core.c:17907-17982), вызов `network.c:21882`:
//         `changeJobBannerStart(g_pChangeJobBanner, jobBannerId, 500, 2000, 1000)` из ветки смены профессии
//         `net_ApplyPlayerFullUpdate@0xD828A0` (5042, `case 0`, `if (jobChangeFlag > 0)`).
//       Гейты: `jobBannerId != −1`, `startActive != 1`, `showActive != 1` (идёт баннер 18550).
//       Старт: корень ВИДИМ, альфа 255 у 22 контролов, корень на `anchorY − H/4`, шаг `moveXStep = (H/4)/10 + 8`,
//       `centerFlag = 1` при `jobBannerId % 3 == 0` (ПЕРВАЯ профессия класса), «Become» и надпись профессии
//       центрируются: `freeW = 320 − (jobW + 130)`, Become.x = freeW/2, Job.x = 130 + freeW/2; прочие 8 надписей скрыты.
//     • `changeJobBannerAnimTick@0xBD5FB0` (game_main.c:2118, КАЖДЫЙ КАДР пока `started`): `if (step > 1) --step;
//       y += step; if (y >= 0) { y = anchor; started = 0; SetTimer(11, 2000) }`.
//     • WM_TIMER 11 (game_main.c:6343-6348) → `uiPanel_fadeInControls(18500)` (ui_core.c:18171-18200): альфа 255→0
//       за 1000 мс у всех контролов, `SetTimer(12, 1000)`, `startActive = 0`.
//     • WM_TIMER 12 (game_main.c:6349-6357): `if (centerFlag) { нотис 7006 окном-сообщением; centerFlag = 0 }`.
//   ★ Ветка 5042 (network.c:21841-21889) кроме баннера: профессия/класс/подкласс/очки умений (body[10..14]),
//     звук 15, «группа» профессии `gap_43B[32]` (1..3), цвет своего имени `uiSetItemSlotColorByGrade`, обучение
//     `sub_92EBB2`, окно персонажа `UI_FillEquipDetailPanel`, очки `skillUiUpdatePointText`, таймеры 5/6 подсказки.
//     Что из этого перенесено и что отложено — отчёт §3.4.
//   ★ 18.10.2026: поля профессии пишет СЕТЬ до рассылки 5042 и шлёт `OnSelfJobChanged` (ключ `asda.Quest.ApplyJobChange`);
//     здесь — баннер, звук 15, группа и нотис 20200/6177 (`chatBuildTransactionNotice` вид 0). Окно характеристик, окно
//     навыков, магазин 6800 и квестовые метки подписаны на событие сами. Отложено по-прежнему: `sub_92EBB2` (обучения в
//     порту нет), цвет своего имени `uiSetItemSlotColorByGrade` (нужен для всех игроков, §2.3), таймеры 5/6 (контрол не опознан).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "AsdaChangeJobBannerSubsystem.generated.h"

class UUserWidget;
class UWidget;

UCLASS()
class ASDAREBORN_API UAsdaChangeJobBannerSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;   // подписка на 5042
	virtual void Deinitialize() override;                                      // отписка, снять окно
	// Полный сброс (разрыв/выход в выбор) — снять баннер; смена карты баннер не трогает (эталон не сбрасывает).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;   // ход баннера (въезд → показ → угасание → таймер 12)
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaChangeJobBannerSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** `changeJobBannerStart`: показать баннер профессии `JobBannerId` (0..8). Возвращает true, если показ начат. */
	bool StartBanner(int32 JobBannerId, int32 EndMs = 500, int32 HoldMs = 2000, int32 FadeMs = 1000);
	/** Признак `startActive` (+0x108): баннер 18500 въезжает/держится; его читает баннер 18550 (HUD). */
	bool IsStartActive() const { return bStartActive; }
	/** Разбор эталона network.c:21858-21877: класс (`body[11]`) → номер надписи 0..8 и «группа» 1..3; −1 — баннера нет. */
	static int32 JobBannerIdFromClass(int32 ClassByte, int32& OutJobGroup);
	/** «Группа» профессии своего персонажа (`gap_43B[32]`, network.c:21878-21879): 0 = не менялась в этой сессии. */
	int32 GetSelfJobGroup() const { return SelfJobGroup; }

	// ── Отладка/самотест (тот же код, что у живого пути) ──
	/** Ручные часы: настоящий Tick перестаёт двигать баннер, время подаёт самотест через `DebugAdvance`. */
	void DebugSetManualClock(bool bOn) { bManualClock = bOn; }
	/** Продвинуть баннер на `Seconds` (тот же `Advance`, что у Tick). */
	void DebugAdvance(float Seconds) { Advance(Seconds); }
	/** Фаза для самотеста: 0 нет, 1 въезд, 2 показ, 3 угасание. */
	int32 DebugPhase() const;
	/** Текущий сдвиг корня, пиксели экрана (0 = на якоре). */
	float DebugOffsetPx() const { return PosYPx; }
	/** Прозрачность корня 0..1. */
	float DebugOpacity() const;
	/** Номер показанной надписи профессии (−1 нет) и X «Become»/надписи в координатах JUI-окна (как `SetField74_75`). */
	int32 DebugShownJob() const;
	bool  DebugLabelX(float& OutBecomeX, float& OutJobX) const;
	/** Сколько раз за сессию ушёл нотис 7006 (след для самотеста). */
	int32 DebugNotice7006Count() const { return Notice7006Count; }
	/** Сколько раз показан нотис смены профессии 20200 (`chatBuildTransactionNotice` вид 0) — след для самотеста. */
	int32 DebugJobNoticeCount() const { return JobNoticeCount; }
	/** Текст последнего нотиса 20200 и был ли он ещё и окном-сообщением (ветка `!useShortNameTable`). */
	const FString& DebugLastJobNoticeText() const { return LastJobNoticeText; }
	bool DebugLastJobNoticeMsgBox() const { return bLastJobNoticeMsgBox; }
	/** Окно баннера (для дампа дерева). */
	UUserWidget* DebugWidget() const { return Banner; }
	/** Сбросить состояние баннера (самотест между сценариями). */
	void DebugReset() { StopAll(TEXT("самотест")); Notice7006Count = 0; JobNoticeCount = 0; LastJobNoticeText.Reset(); bLastJobNoticeMsgBox = false; }

private:
	// 5042 → ветка смены профессии (`net_ApplyPlayerFullUpdate`, case 0, `jobChangeFlag > 0`) — ПРЕЖНИЙ путь
	//   (ключ `asda.Quest.ApplyJobChange 0`): поля пишет этот обработчик. При ключе 1 он молчит — работает `OnSelfJobChanged`.
	void OnQuestTurnInResult(int32 StepCode, int32 QuestNo, int32 Stage, int32 FinishType, int32 TalkId,
	                         const TArray<uint8>& Body);
	// `UAsdaNetSubsystem::OnSelfJobChanged` (ключ 1): поля уже записаны сетью — звук 15, группа, баннер, нотис 20200.
	void OnSelfJobChanged(int32 OldClassId, int32 Job, int32 ClassId, int32 SubJob);
	// Общая часть ветки после записи полей: звук 15, группа `gap_43B[32]`, `changeJobBannerStart`; `bNotice` — ещё нотис вида 0.
	void RunJobChangeUi(int32 ClassId, int32 SubJob, bool bNotice);
	// `chatBuildTransactionNotice@0xD9FC70` вид 0 (chat.c:6094-6125): «Поздравляем! Теперь ты [ss1]!» (20200) и 6177.
	void PostJobChangeNotice(int32 ClassId, int32 SubJob);
	bool EnsureWidget();            // поднять WBP_ChangeJob_Banner и разобрать контролы
	void Advance(float Seconds);    // общий шаг времени (Tick и самотест)
	void SlideFrame();              // один кадр `changeJobBannerAnimTick` (60 Гц)
	void ApplyOffset();             // сдвиг корня по Y (пиксели → единицы UI)
	void StopAll(const TCHAR* Why); // снять баннер и таймеры

	FDelegateHandle QuestTurnInHandle;   // подписка на `OnQuestTurnInResult`
	FDelegateHandle JobChangedHandle;    // подписка на `OnSelfJobChanged`
	int32   JobNoticeCount = 0;          // след нотиса 20200 для самотеста
	FString LastJobNoticeText;           // текст последнего нотиса 20200
	bool    bLastJobNoticeMsgBox = false; // последний нотис 20200 ушёл и окном-сообщением (0, 24)
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Banner = nullptr;   // WBP_ChangeJob_Banner (корень 18500)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> JobImg;   // надписи 18502+i: картинка (i = 0..8)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> JobTxt;   // надписи 18502+i: подпись-текст (может быть пустой)
	UPROPERTY(Transient) TObjectPtr<UWidget> BecomeImg = nullptr;   // 18501 «Con_Banner_Become» картинка
	UPROPERTY(Transient) TObjectPtr<UWidget> BecomeTxt = nullptr;   // 18501 подпись-текст
	TMap<TWeakObjectPtr<UWidget>, FMargin> BaseOffsets;   // исходные смещения слотов (сдвиг считается от них)
	TArray<float> JobWidth;     // ширины надписей (`vtable+6 GetRect`), из слота; фолбэк — JUI
	float BecomeWidth = 130.f;  // `becomeRectX`
	float BannerWidth = 320.f;  // `bannerRectX`

	bool  bStartActive = false;  // `startActive` (+0x108)
	bool  bSliding     = false;  // `started` (+0x110 = dword_2BE56E0) — идёт `changeJobBannerAnimTick`
	bool  bCenterFlag  = false;  // `centerFlag` (+0x10C = dword_2BE56DC) — по таймеру 12 нотис 7006
	int32 MoveStep     = 0;      // `moveXStep` (+0xF8)
	float PosYPx       = 0.f;    // posY корня относительно якоря, пиксели экрана
	float SlideAccumS  = 0.f;    // накопитель кадров въезда (шаг 1/60 с)
	float HoldLeftS    = -1.f;   // WM_TIMER 11: сколько осталось до угасания (−1 = не взведён)
	float PendingHoldS = 2.f;    // `moveTimerInterval` (+0xEC): интервал таймера 11, взводится по прибытии
	float FadeDurS     = 1.f;    // `timerIntervalB` (+0xF0)
	float FadeElapsedS = -1.f;   // угасание: прошло (−1 = не идёт)
	float Timer12LeftS = -1.f;   // WM_TIMER 12: сколько осталось (−1 = не взведён)
	int32 ShownJob     = -1;     // какая надпись профессии показана
	int32 SelfJobGroup = 0;      // `gap_43B[32]` своего персонажа после смены профессии
	int32 Notice7006Count = 0;   // след нотиса 7006 для самотеста
	bool  bManualClock = false;  // самотест ведёт часы сам
};
