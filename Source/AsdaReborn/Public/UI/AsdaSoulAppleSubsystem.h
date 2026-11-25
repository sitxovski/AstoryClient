// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// UAsdaSoulAppleSubsystem — «ЯБЛОКО ДРУЖБЫ» на панели соулмейта `SOULMATE_POP_WIN` (6500), контролы
//   6689…6699, пакеты 6049/6051. ★ Волна 1-Б (06.10.2026): найдено при реверсе пути открытия рулетки
//   67000 — «Золотое яблоко» 6693, единственный ручной отправитель 6575, живёт ЗДЕСЬ, а не во вкладке
//   соулмейта окна 6400 (прежняя привязка `UAsdaSoulRouletteSubsystem::BindTabControls` искала его
//   в окне 6400 и не находила никогда). Отчёт: `Reports/owner_1006/wave1B_party_roulette.md` §3.
//
// ═══════════════════════ СОСТАВ (gamemain_jui.ini:20335-20600, дамп `_wave1B_wbp_dump.txt`) ═══════
//   6691 Soulmate_AppleBase  — группа панели (vis 1), в ней:
//     Soulmate_Bar (декор), 6689 Soulmate_Gear (vis 0), 6690 Soulmate_AniGear (vis 0, анимация),
//     6699 Soulmate_Glass (группа-якорь, vis 1), в ней:
//       6692 Soulmate_Apple (37×36 @6,5, vis 1)          — яблоко, масштабируется
//       6693 Soulmate_GoldenApple (45×44 @2,3, vis 0)    — ЗОЛОТОЕ яблоко: клик = 6575
//       6694 Soulmate_AppleEffect (vis 0)                — гасится при раскрытии, больше не трогается
//       6695 Soulmate_ShakeApple (37×36 @6,5, vis 0)     — «трясущееся» яблоко, анимация 3 прогона
//       6696/6697/6698 Soulmate_AppEff1/2/3 (vis 0, NoSelect 1) — цепочка эффектов «созрело»
//   Привязка — `social.c:6570-6640` (CSoulmateWnd +0x294…+0x2B8), поля состояния +0x2CC…+0x2F0.
//
// ═══════════════════════ ПОТОК (эталон, все ветки) ═════════════════════════════════════════════
//   S→C 6049 `net_OnDuelStageNotice@0xE6CF50` (★ миснейм — к дуэли отношения нет), тело 7 б (сервер
//     `sub_499220@GameServer.c:61307`): @0 u16 userNum · @2 i32 опыт яблока · @6 u8 процент (сервер:
//     счётчик 0…200 → ×100/200, ненулевой минимум 1). Только если userNum == мой:
//       опыт → +185 (подсказка 12183); процент 0 → «сброс» (186 = 1, вид по умолчанию, 181 = 0);
//       иначе, если панель не раскрыта (181 == 0) → раскрыть (181 = 1, `uiDlgShowExpandState`);
//       1…33  → «трясти» (`uiToggleShowAlt`);
//       34…66 → впервые: рост 50 % → 80 % (+2 % каждые 100 мс, 15 шагов) + шестерня; потом — 70 % и «трясти»;
//       67…99 → впервые: рост 70 % → 110 % (+2 % / 100 мс, 20 шагов) + шестерня; потом — 100 % и «трясти»;
//       ≥ 100 → 182…184 = 0, «созрело»: яблоко прячется, цепочка 6696 → 6697 → 6698 (+6693 золотое), 495 = 1.
//   Конец анимации роста яблока (событие 24, a7 = 3):
//       первый раз (182 == 0) → 182 = 1, шестерня крутится 3 прогона, «трясти»;
//       183 && !184 → 70 %;  183 && 184 → 100 %.
//   Конец анимации шестерни 6690 → шестерня 6689. Конец «трясущегося» 6695 → снова 6692.
//   Тик главного цикла (`MainLoop_Tick_SoundUpdate`, game.c:9960) при «созрело»: эффект 6696+k
//     доиграл прогон → показать следующий; на третьем показать ЗОЛОТОЕ 6693.
//   Наведение на 6692/6693/6695 → подсказка loc 12183 «Очки опыта: [ss1]» (опыт с разделителями).
//   Клик по 6693 → 6575 (`netSendAntiCheatRequest` — миснейм), ответ 6576 код 1 открывает окно 67000.
//   S→C 6051 `Net_OnExpGiftNotice@0xE6D940` (тело 14 б): код 2 → плашка 221 loc 8167; код 3 → 8170;
//     иначе, если userNum == мой: съел я → 12184, иначе → 12185 (чат, жёлтый −11514); звук
//     `S_c_SmExpGiftApple.wav` (3D) и сброс вида; эффект `eff_1_char_99_expmovg-a_01` + `S_c_SmExpGift.wav`
//     на съевшем, если его userNum совпал.
//   Кнопка 67002 окна рулетки при 495 == 1 → 495 = 0 и ЛОКАЛЬНОЕ вращение (пакета нет) — в рулетке.
//
// ═══════════════════════ ТАБЛИЦА НОТИСОВ «событие → loc → канал → цвет» ═══════════════════════
//   6051 код 2                 → 8167 «Яблоко можно использовать только на Друге»       → плашка 221 → #FFD306
//   6051 код 3                 → 8170 «…только когда оно станет золотым»                → плашка 221 → #FFD306
//   6051, съел я               → 12184 «Съев Яблоко Дружбы, ты получаешь очки опыта.»   → сис. чат   → #FFD306
//   6051, съел друг            → 12185 «Твой Друг съел Яблоко Дружбы.»                   → сис. чат   → #FFD306
//   наведение 6692/6693/6695   → 12183 «Очки опыта: [ss1]»                               → тултип g_ItemTooltip
//   6049                       → без текста.  НУМПАД: нет (количеств нет).
//
// ═══════════════════════ ОТСТУПЛЕНИЯ И ОГОВОРКИ ════════════════════════════════════════════════
//   • [ПОРТ] Событие 24 «конец анимации» у плоскостей с `EFrameStop` эталон поднимает из тика плана;
//     у порта его заменяет таймер, длительность — ИЗ ДАННЫХ СКИНА (`FrameInterval` × прогоны, как в рулетке).
//   • [ОРИГИНАЛ] Ветка «6693/6698 доиграли и 186 == 1 → сброс» (game.c:473574) НЕДОСТИЖИМА: для этих
//     контролов `EFrameStop` не ставит никто (`CControl_Show` его не взводит), событие 24 не придёт.
//     Не воспроизводим — мёртвого кода не пишем; флаг 186 храним (читает только эта ветка).
//   • [ОРИГИНАЛ] Подсказка у 6698 в обработчике есть, но у контрола `NoSelect = 1` — мышь он не получает.
//   • Сброс панели при потере пары (`inventoryHidePanelsAndRefresh`, ветка `!gap_8B4[6]`) — геттера
//     «есть пара» у `UAsdaSoulmateSubsystem` нет (файл волны 1-А) → в реестр отложенного.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"          // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSoulAppleSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;

/** Контролы панели яблока (индексы массивов зеркала видимости). Порядок — как в CSoulmateWnd. */
UENUM()
enum class EAsdaSoulAppleNode : uint8
{
	AppleBase = 0  UMETA(DisplayName = "6691 Soulmate_AppleBase (группа)"),
	Gear           UMETA(DisplayName = "6689 Soulmate_Gear"),
	AniGear        UMETA(DisplayName = "6690 Soulmate_AniGear (анимация)"),
	Apple          UMETA(DisplayName = "6692 Soulmate_Apple"),
	Golden         UMETA(DisplayName = "6693 Soulmate_GoldenApple"),
	AppleEffect    UMETA(DisplayName = "6694 Soulmate_AppleEffect"),
	Shake          UMETA(DisplayName = "6695 Soulmate_ShakeApple (анимация)"),
	Eff1           UMETA(DisplayName = "6696 Soulmate_AppEff1"),
	Eff2           UMETA(DisplayName = "6697 Soulmate_AppEff2"),
	Eff3           UMETA(DisplayName = "6698 Soulmate_AppEff3"),
	Count          UMETA(Hidden),
};

UCLASS()
class ASDAREBORN_API UAsdaSoulAppleSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	/** Окно-хозяин панели (его создаёт и показывает `UAsdaSoulmateSubsystem`; мы только находим). */
	static const FName kPopWindowId;   // "SOULMATE_POP_WIN"

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ──
	// Состояние яблока принадлежит сессии персонажа: полный сброс возвращает вид по умолчанию
	//   (как конструктор CSoulmateWnd); смена карты состояние СОХРАНЯЕТ (виджет 6500 пересоздаётся —
	//   зеркало видимости применяется к новому при привязке).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── FTickableGameObject ──
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSoulAppleSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** ★ Флаг «яблоко созрело» (CSoulmateWnd +495 = 1 при проценте ≥ 100). Читает кнопка 67002 рулетки. */
	bool IsGoldenFlag() const { return bGoldenFlag; }
	/** Порт ветки 67002 (`SoulmateUI_HandleButtonCommand`): если флаг взведён — снять и вернуть true. */
	bool ConsumeGoldenFlag();

	/** ★ Ревизия С-1: яблочная часть `inventoryHidePanelsAndRefresh@0xE65510`. Пары нет — сброс панели и вида;
	 *  группа 6691 прячется ВСЕГДА. При живой паре и `asda.Soul.Apple.FixCloseAllHide 1` она вернётся с окном 6500. */
	void OnHidePanelsAndRefresh(bool bHasPair, const TCHAR* Why);
	/** Ждёт ли группа 6691 возврата вместе с окном 6500 (исправление дефекта оригинала). */
	bool IsBaseRestorePending() const { return bBaseRestorePending; }
	/** ★ Ревизия М-3: якорь окна рулетки — группа 6699 `Soulmate_Glass_grp` окна 6500 (nullptr — окна нет). */
	UWidget* GetGlassAnchor() const { return bBound ? GlassAnchor.Get() : nullptr; }

	/** Клик по «Золотому яблоку» 6693 (эталон msg 0xB, `a3 == 6693` → 6575). Зовёт опрос мыши и самотест. */
	bool ClickGoldenApple();

	// ── Диагностика (самотест asda.UI.SelfTestWave1B, консоль asda.Soul.Apple.Dump) ──
	/** Видим ли контрол панели по ЗЕРКАЛУ состояния (то, что применяется к виджету). */
	bool IsNodeShown(EAsdaSoulAppleNode N) const { return Vis[(int32)N]; }
	/** Виджет контрола (nullptr — окна 6500 нет или узел не найден). */
	UWidget* GetNodeWidget(EAsdaSoulAppleNode N) const;
	/** Текущий масштаб яблока, % (эталон `uiControl_ScaleCenterChildren` / анимация роста). */
	float GetApplePct() const { return ApplePct; }
	/** Опыт яблока из 6049 @2 (+185). */
	int32 GetAppleExp() const { return AppleExp; }
	/** Шаг цепочки эффектов «созрело» (+187) и признак цепочки (byte +752). */
	int32 GetEffStep() const { return EffStep; }
	bool  IsEffChain() const { return bEffChain; }
	/** Идёт ли анимация роста (событие 24 ещё не пришло). */
	bool  IsScaleAnimActive() const { return bScaleActive; }
	/** Окно 6500 найдено и контролы привязаны. */
	bool  IsBound() const { return bBound; }
	/** Полный структурный дамп: узлы, зеркало, флаги 181…187/495, анимации, счётчики. */
	FString DumpState() const;

private:
	// ── сеть (закон no-reentrant-net-callbacks: из колбэка только КЛАДЁМ) ──
	TAsdaNetQueue<TPair<int32, TArray<uint8>>> PendingIn;
	FDelegateHandle HApple;
	void OnGrowth6049(const TArray<uint8>& Body);
	void OnExpGift6051(const TArray<uint8>& Body);

	// ── привязка к окну 6500 ──
	/** Найти окно 6500; если виджет СМЕНИЛСЯ — разобрать узлы и применить зеркало. */
	void EnsureBound();
	void BindNodes(UUserWidget* W);
	void ApplyMirror();
	TWeakObjectPtr<UUserWidget> Wnd;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Nodes;   // по EAsdaSoulAppleNode
	bool bBound = false;
	/** Исходный прямоугольник 6692 в его канве (эталон m_AppleBasePos/m_AppleBaseSize, +175…+178). */
	FVector2D AppleBasePos = FVector2D::ZeroVector;
	FVector2D AppleBaseSize = FVector2D::ZeroVector;

	// ── визуальные операции эталона (все — через зеркало) ──
	void SetVis(EAsdaSoulAppleNode N, bool bShow);
	/** `uiControl_ScaleCenterChildren(pct)`: 6692 и 6695 — размер base×pct/100, центр на месте (целочисленно). */
	void ScaleCenter(int32 Pct);
	/** Применить масштаб в % (дробный — для анимации роста) к 6692 и 6695. */
	void ApplyScale(float Pct, bool bIntegerPct);
	/** `uiContainerConfigItemAndShow` для 6692: старт `StartPct`, шаг `StepPct` каждые `IntervalMs`, шагов `MaxSteps`. */
	void StartGrow(float StartPct, int32 IntervalMs, float StepPct, int32 MaxSteps);
	/** `ctrlShowWithItemFlags(ctrl, Loops)`: показать, анимация Loops прогонов, по концу — событие 24. */
	double ShowAnimLoops(EAsdaSoulAppleNode N, int32 Loops);
	/** `CControl_Show`: показать и пустить анимацию с начала (бесконечно). Возвращает длительность прогона, с. */
	double ShowAnimRestart(EAsdaSoulAppleNode N);
	/** `CControl_CloseHide`: остановить анимацию и спрятать. */
	void CloseHide(EAsdaSoulAppleNode N);
	void ShowExpandState();          // uiDlgShowExpandState@0xE6DD20
	void ToggleShowAlt();            // uiToggleShowAlt@0xE6E0E0
	void ToggleChildVisible();       // uiToggleChildVisible168_171@0xE6E140
	void WindowShowAltView();        // uiWindowShowAltView@0xE6DF20
	void WindowResetView();          // uiWindow_ResetView@0xE6DF80
	void PanelResetAndKillFocus();   // uiPanelResetAndKillFocus@0xE6DE50
	void OnGrowAnimEnd();            // SoulmateUI_HandleEvent: a4 == 24, a3 == 6692, a7 == 3
	void TickAnims();                // таймеры событий 24 + шаги роста + цепочка эффектов
	void TickPointer();              // наведение (подсказка 12183) и клик по 6693

	// ── состояние (поля CSoulmateWnd +0x2CC…+0x2F0, +752, +495) ──
	bool  bShown = false;            // +181 панель раскрыта
	bool  bStageGear = false;        // +182 первый рост доигран
	bool  bStage34 = false;          // +183 рост 34…66 был
	bool  bStage67 = false;          // +184 рост 67…99 был
	int32 AppleExp = 0;              // +185 опыт яблока (подсказка)
	bool  bResetPending = false;     // +186 (читает только недостижимая ветка, см. шапку)
	int32 EffStep = 0;               // +187 шаг цепочки 6696…6698
	bool  bEffChain = false;         // byte +752 цепочка идёт
	bool  bGoldenFlag = false;       // +495 яблоко созрело (для 67002)
	bool  bBaseRestorePending = false;  // ★ ревизия С-1: 6691 спрятана при живой паре — вернуть с окном 6500
	bool  bBaseVisBeforeHide = true;    // видимость 6691 до скрытия (её и возвращаем)
	UPROPERTY(Transient) TWeakObjectPtr<UWidget> GlassAnchor;   // ★ ревизия М-3: 6699 `Soulmate_Glass_grp`

	// ── зеркало видимости и масштаба (переживает пересоздание виджета 6500) ──
	bool  Vis[(int32)EAsdaSoulAppleNode::Count] = {};
	float ApplePct = 50.f;

	// ── анимация роста (поля плана: sub_927CA9 вкл, sub_9290AE текущий %, sub_938ADB шаг, sub_93A868 мс, sub_924545 шагов) ──
	bool   bScaleActive = false;
	float  ScaleCur = 0.f, ScaleStep = 0.f;
	int32  ScaleIntervalMs = 0, ScaleMaxSteps = 0, ScaleSteps = 0;
	double ScaleNextDue = 0.0;

	// ── таймеры «событие 24» (из данных скина) и цепочки ──
	double AniGearEndS = 0.0;        // 6690 доиграла 3 прогона
	double ShakeEndS = 0.0;          // 6695 доиграла 3 прогона
	double EffPassEndS = 0.0;        // текущий 6696+EffStep доиграл ПЕРВЫЙ прогон

	// ── мышь и подсказка ──
	FAsdaMouseFrame Mouse;
	bool bTipShown = false;
	/** Нажатие ЛКМ пришлось на видимое 6693; прямоугольник 6693 на момент нажатия (геометрия на отпускании может быть сброшена перевесом окна). */
	bool bGoldenPressed = false;
	FVector2D GoldenPressMin = FVector2D::ZeroVector, GoldenPressMax = FVector2D::ZeroVector;

	// ── счётчики ──
	int32 Rx6049 = 0, Rx6051 = 0, Tx6575 = 0;
};
