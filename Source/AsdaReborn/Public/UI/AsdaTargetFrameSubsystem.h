// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaTargetFrameSubsystem — ПАНЕЛЬ ЦЕЛИ (target frame), окно JUI `PVP_TARGET` (idnum 40).
//
// ═════════════════════════ ЭТАЛОН (сборка IDB, `RE/etalon/ui_core.c`) ══════════════════════════
// Полный поконтрольный разбор окна: `Docs/UI/HUD/PVP_TARGET.md` (112 контролов, каждый со
//   статусом CONFIRMED); инвентарь: `Docs/UI/_inv/HUD/PVP_TARGET.json`. Здесь — модель.
//
// ФУНКЦИИ ЭТАЛОНА И ЧТО ОНИ ДЕЛАЮТ:
//   • `HUD_Main_Init`            (ui_core.c:74658…75014) — резолв контролов по idnum в поля `CHUD`;
//       здесь же снимаются `halfScreenWidth = ширина экрана/2` и `halfTargetWidth = ширина
//       PVP_TARGET00/2` (структура `structs_ru.h:2291-2292` прямо называет их «центрирование
//       target frame»). ★ ЧИТАТЕЛЕЙ У ОБОИХ ПОЛЕЙ НЕТ — проверено вторым методом по ПОЛНОМУ
//       экспорту IDA (47390 функций): единственные вхождения — эти две записи. То есть в
//       оригинале рамка стоит там, куда её ставит анкер JUI (`AlignXPos "16 40 15"` = доля 0.4
//       ширины экрана), а арифметика центрирования осталась неиспользованной.
//   • `hud_setTarget @0xE7B470`  (:75885) — ВЫБОР цели: шлёт 6538, гасит first-attack/being-attack
//       и 6 стихийных меток, показывает PVP_TARGET00 и кольцо Normal, рисует эмблему клана,
//       кладёт имя (`target+1056`) и уровень (`target+1082`), красит имя/уровень в БЕЛЫЙ.
//   • `hudShowMobTargetInfo @0xE7AB50` (:75701) — цель-МОБ: сегментированный HP, MP-бар закрыт
//       слоем `PVP_MPBAR_HIDE`, метка «бьёт первым», стихийная метка ПО ИНДЕКСУ `ent+6978`,
//       портрет из `g_pMobInfoTable`, кольцо ранга и цвет имени по `switch (ent+6979)`.
//   • `HUD_UpdateTargetFrame`    (:77497) — общий апдейт (цель-ИГРОК): стихия по МАКСИМУМУ из
//       шести значений `target+1168..+1178`, эмблема класса/PvP-ранга из атласа `char_emblems`,
//       имя перерисовывается только при изменении, HP-бар односегментный.
//   • `hudUpdateSegmentedHpBar @0xE7A860` (:75641) — 4-полосный HP для целей с «жизнями».
//   • `HUD_TargetLevelDigits @0xE80C50`   (:77630) — уровень СПРАЙТАМИ (сотни/десятки/единицы).
//   • `HUD_TargetBuffIcons_Init/Update/Refresh/OnClick` (:77113/:77144/:76928/:77361) — до 28
//       иконок состояний цели; у игрока список `target+2556`, у моба `target+18580`.
//   • `hudResetCursorItemSlot @0xE7BE00`  (:76046) — СНЯТИЕ цели: спрятать окно и обнулить кэш.
//   • `hudRefreshControlColorFromCount @0xE7F9E0` (:77350) — МИГАНИЕ `PVP_BEING_ATTACK`:
//       альфа читается с самого контрола (`sub_92E374` → `sub_12E3D40`: альфа вершины 0 плана 0)
//       и уменьшается на 13 за вызов, дойдя до ≤0 — сбрасывается в 255. Пила 255→0, шаг 13.
//
// ═══════════════ ЧЕТЫРЕ ДЕФЕКТА ОРИГИНАЛА, НАЙДЕННЫХ ПРИ ПОРТЕ (закон fix-original-defects) ═════
// Каждый исправлен, каждый имеет КЛЮЧ ОТКАТА к эталонному поведению (см. cvar-ы ниже).
//   Д1. `PVP_ELE_Base` (подложка стихийной метки) в .JUI имеет `Visible = "0"`, а метки
//       `PVP_MARK00..05` — его ДЕТИ. Эталон показывает метку, но подложку НЕ показывает никогда
//       (`SetVisible(pPvpMark0[i], 1)` есть, `SetVisible(ELE_Base)` нет — контрол вообще не
//       резолвится, idnum = −1). Скрытый родитель прячет детей (это же правило запёк импортёр
//       JUI: все дети `Visible=0`-родителя пришли `Collapsed`) ⇒ СТИХИЙНАЯ МЕТКА В ОРИГИНАЛЕ НЕ
//       ВИДНА НИ РАЗУ. Порт показывает подложку вместе с меткой. Откат: `asda.Target.LegacyEleBase 1`.
//   Д2. `PVP_BEING_ATTACK` у цели-МОБА показывается по `if (*(WORD*)(ent + 474))`. Писателя у
//       этого слова НЕТ — проверено по ПОЛНОМУ экспорту IDA (7 вхождений «+ 474», все три
//       осмысленных — ЧТЕНИЯ: ui_core 75751/76984 и network 5969 «по этой сущности нельзя
//       кастовать»). Поле всегда 0 ⇒ рамка «цель в бою» у мобов не загоралась никогда.
//       Порт зажигает её, когда моб в боевой стойке / ведёт погоню. Откат: `asda.Target.LegacyBeingAttack 1`.
//   Д3. Мигание рамки (`hudRefreshControlColorFromCount`) гейтится флагом `hud[690]`, который
//       ставится в 1 ТОЛЬКО при «моб бьёт первым» — то есть мигал контрол, который по Д2 был
//       невидим. Порт мигает ровно тогда, когда рамка показана. Тот же ключ отката, что у Д2.
//   Д4. Цвет по разнице уровней эталон ставит на `PVP_TARGETNAME` и на `PVP_LVTXT`, но `PVP_LVTXT`
//       скрыт при инициализации (`ui_core.c:74858`) — уровень рисуется СПРАЙТАМИ, а спрайты не
//       красятся. Порт красит и спрайты тоже. Откат: `asda.Target.LegacyLevelColor 1`.
// ★ Пятое отступление — не дефект, а ЯВНАЯ ПРОСЬБА ВЛАДЕЛЬЦА: центрирование панели по ширине
//   `PVP_TARGET00` (та самая эталонная арифметика `halfScreenWidth − halfTargetWidth`, у которой
//   в оригинале не нашлось читателя). Откат к анкеру JUI: `asda.Target.CenterByPanel 0`.
//
// ═══════════════════════════ ЛЕСЕНКА ЦВЕТА ИМЕНИ (эталон, sRGB ARGB) ════════════════════════════
//   `levelDiff = уровень ИГРОКА − уровень ЦЕЛИ`; ветка выбирается по агрессии моба (`ent+6979`):
//     агрессия 1 / 4 / 5 → лесенка:  ≥7 → FFAAAAAA · 2..6 → FF00FF00 · −1..1 → FFFFFFFF ·
//                                    −5..−2 → FFFFA200 · ≤−6 → FFFF0000
//     агрессия 2         → FFE24ACB (фиксированный, разница уровней не участвует)
//     агрессия 3         → FFC3A868 (фиксированный)
//     прочее             → цвет не трогается (остаётся белый после сброса)
//   ⚠ Верхняя ступень ИМЕННО FFAAAAAA (−5592406). У над-головной плашки на том же месте
//     FF808080 (−8355712) — это РАЗНЫЕ таблицы, путать нельзя (см. `DT_MobNameColors`).
//   ⚠ Цель-ИГРОК лесенки НЕ получает: `hud_setTarget`/`HUD_UpdateTargetFrame` ставят обоим
//     контролам −1 (белый) и на этом останавливаются.
//
// ═════════════════════ ДВА МИСНЕЙМА В УЖЕ СУЩЕСТВУЮЩИХ ДАННЫХ ПОРТА (важно!) ════════════════════
//   `FAsdaMobStatsRow::Rank`       = Mob list.BIN +0xF8 → сущность **+6978** — это НЕ ранг, а
//        ИНДЕКС СТИХИИ (0..5) для `PVP_MARK00..05`. Доказательство: `hudShowMobTargetInfo`
//        `if (ent[6978] < 6) SetVisible(pPvpMark0[ent[6978]], 1)`; распределение по 854 записям
//        {0:194, 1:99, 2:390, 3:55, 5:116} — все < 6, значения 4 (Свет) в данных просто нет.
//   `FAsdaMobStatsRow::Aggression` = Mob list.BIN +0xFC → сущность **+6979** — это и есть
//        РАНГ/АГРЕССИЯ, по нему `switch`: 1/5 → кольцо Normal, 2 → Named, 3 → Boss, 4 → Elite.
//        Распределение {1:629, 2:8, 3:87, 4:130} — ровно четыре класса колец.
//   Колонки НЕ переименовываю (это сломало бы уже сгенерированный `DT_MobStats` и его читателей);
//   истинный смысл зафиксирован здесь, в `AsdaMobStatsRow.h` и в отчёте.
//
// ЗАКОНЫ: окно = ИМПОРТИРОВАННЫЙ WBP (no-invented-ui, редактируемо в UE); заливка баров = клип
//   ширины плана (ui-progressbar-animate-existing); цвета sRGB→linear (ui-srgb-gamma-rule);
//   имена узлов ассета — из живого дампа `Tools/_probe_pvp_target_widget.py` (228 узлов).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "UI/AsdaBarSmoother.h"   // ★ общий сглаживатель полос здоровья/щитов
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaMiniStateSubsystem.h"   // FAsdaMiniBarFill + ClipFill/GetSlotRect — общее ядро баров
#include "UI/AsdaHelpTopics.h"   // FAsdaHelpTipHover — подсказка иконок состояний цели
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaTargetFrameSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UCanvasPanel;
class UAsdaAnimatedPlane;
class AAsdaMobActor;

// Ранг цели-моба = кольцо вокруг портрета. Значение = байт `Mob list.BIN +0xFC` (сущность +6979).
UENUM()
enum class EAsdaTargetRing : uint8
{
	None   = 0,   // агрессия 0 или неизвестная — эталон не показывает НИ ОДНОГО кольца
	Normal = 1,   // агрессия 1 и 5 → Gear_Target_Normal (idnum 94)
	Named  = 2,   // агрессия 2     → Gear_Target_Named  (idnum 92)
	Boss   = 3,   // агрессия 3     → Gear_Target_Boss   (idnum 91)
	Elite  = 4,   // агрессия 4     → Gear_Target_Elite  (idnum 93)
};

// Один слот иконки состояния врага: группа `ENERMY_STATE_ICON<NN>_grp` + план-картинка 24×24 +
//   подпись стека (текст) + декоративный оверлей кулдауна (`ENERMY_STATE_Cool<NN>`; логики у него
//   в эталоне НЕТ — idnum = −1, ни одного вызова, поэтому только гасим).
USTRUCT()
struct FAsdaEnemyStateSlot
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget>    Group = nullptr;   // ENERMY_STATE_ICON<NN>_grp
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;   // ENERMY_STATE_ICON<NN>_<i>_0
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Stack = nullptr;   // ENERMY_STATE_ICON<NN> (подпись стека)
	UPROPERTY(Transient) TObjectPtr<UWidget>    Cool  = nullptr;   // ENERMY_STATE_Cool<NN>_<i>_0 (декор)
	// Кэш эталона `targetBuffId0` / `targetBuffType0`: что сейчас в слоте (−1 = пусто).
	UPROPERTY(Transient) int32 BuffId   = -1;
	UPROPERTY(Transient) int32 BuffType = 0;
	UPROPERTY(Transient) int32 StackNum = 0;
};

UCLASS()
class ASDAREBORN_API UAsdaTargetFrameSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// FTickableGameObject — опрос выбранной цели и покадровое мигание рамки «в бою».
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaTargetFrameSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Многострочный дамп состава и состояния окна — консоль `asda.Target.Dump` (доказательная
	//   строка живого прогона по закону full-structural-analysis-always).
	FString DumpState() const;

	// Принудительно перечитать текущую цель (консоль `asda.Target.Refresh`, смена данных цели).
	void ForceRefresh();

	// ★★★ САМОТЕСТ ПУТЁМ ИГРОКА (`asda.Target.SelfTest`): снимает значение С САМОЙ ШКАЛЫ ПОСЛЕ
	//   ОТРИСОВКИ — фактическую экранную геометрию плана-заливки `PVP_HPBAR` из `GetCachedGeometry`
	//   и долю «ширина заливки / полная ширина». Печатать то, что положил код, здесь запрещено:
	//   ровно так пропускается «значение есть, а шкала не двигается».
	FString SelfTest() const;

	// Создать окно и связать контролы, НЕ показывая панель. Нужно консольному дампу: без цели тик
	//   окно не поднимает, и `asda.Target.Dump` печатал бы «окна нет» — ложное «нет»
	//   (закон false-negatives-method). Возвращает true, если состав окна готов к печати.
	bool EnsureForDump() { return EnsureWidgets(); }

#if !UE_BUILD_SHIPPING
	// ★ 06.10.2026 (самотест портрета `asda.UI.SelfTestMiniState`): узлы эмблемы цели — только чтение.
	UWidget* DebugClanGroup() const { return ClanGroup; }   // PVP_ClanMarkBase_grp (280)
	UImage*  DebugClanMark()  const { return ClanMark; }    // PVP_ClanMark (281)
	// Нарисовать эмблему клана заданного актора (тот же путь, что при пересборке панели) — для самотеста.
	void DebugSetClanMark(AActor* Target) { if (EnsureWidgets()) { SetClanMark(Target); } }
	// ★ 26.10.2026 (самотест `asda.Skill.SelfTestBowFx`): id состояния в слоте иконки под полосой HP цели (−1 — пусто) и видна ли иконка.
	int32 DebugEnemyStateId(int32 Slot) const { return EnemySlots.IsValidIndex(Slot) ? EnemySlots[Slot].BuffId : -1; }
	bool  DebugEnemyStateIconShown(int32 Slot) const;
	// 04.11.2026 (selftest asda.Buff.SelfTestExpiry): icon widget of the target state slot (hover tooltip check).
	UImage* DebugEnemyStateIcon(int32 Slot) const { return EnemySlots.IsValidIndex(Slot) ? EnemySlots[Slot].Icon.Get() : nullptr; }
	// 04.11.2026 (selftest asda.Buff.SelfTestExpiry): state type of the slot (0 skill state, 1 soulmate state; -1 empty).
	int32 DebugEnemyStateType(int32 Slot) const { return (EnemySlots.IsValidIndex(Slot) && EnemySlots[Slot].BuffId >= 0) ? EnemySlots[Slot].BuffType : -1; }
#endif

private:
	// ★ 06.10.2026 (ревизия, вне задачи): гашение эмблемы цели `sub_92132C(g_pHUD, 0)` = SetVisible(CHUD+0x9A8, 0)
	//   эталон зовёт БЕЗУСЛОВНО (кто бы ни был целью) в 4107 код 1 (ui_core.c:17121), 4117 код 1 (:17166),
	//   4132 код 2 (:17214) и 4133 код 2 своего (network.c:12461). Колбэки только считают события, гасим в Tick.
	void OnClanResetNet(int32 CharId, const FString& CharName);          // 4107/4117/4133 (свой)
	void OnClanBaseInfoPushNet(int32 Opcode, const TArray<uint8>& Body);  // 4132 код 2
	int32 PendingClanMarkHide = 0;   // число событий гашения, ждущих Tick
	// ── Жизненный цикл окна ─────────────────────────────────────────────────────────────────────
	bool EnsureWidgets();     // создать/найти окно и связать 228 узлов (идемпотентно, переживает травел)
	void ResetWidgets();      // сброс ссылок (окно умерло вместе с миром)
	void BindControls();      // разложить узлы ассета по типизированным полям

	// ── Показ/скрытие ───────────────────────────────────────────────────────────────────────────
	void ShowFrame(bool bShow);                     // эталон: SetVisible(pPvpTargetWin, …)
	void ApplyCentering();                          // halfScreenWidth − halfTargetWidth (см. шапку)

	// ── Наполнение ──────────────────────────────────────────────────────────────────────────────
	void RebuildForTarget(AActor* Target);          // цель сменилась: полный пересбор панели
	void FillForMob(AAsdaMobActor* Mob);            // эталон hudShowMobTargetInfo
	void FillForPlayer(AActor* Player, int32 UserIdx);   // эталон hud_setTarget + HUD_UpdateTargetFrame
	void RefreshVitals();                           // покадрово: HP (и MP, если включён) текущей цели
	// ★ ЕДИНЫЙ вход для полосы жизни цели: задаёт ЦЕЛЬ, а рисует сглаженное значение.
	//   Сами `UpdateSegmentedHp`/`SetSimpleHp` остаются дословным портом эталона и рисуют ровно то
	//   число, которое им дали, — сглаживание живёт слоем выше, чтобы порт не размывать.
	void ApplyTargetHp(int32 CurHp, int32 MaxHp, int32 Segments);
	void DrawTargetHpShown();          // перерисовать по текущему состоянию сглаживателя
	void TickTargetBars(float DeltaSeconds);
	FAsdaBarSmoother HpSmooth;         // жизнь цели
	FAsdaBarSmoother MpSmooth;         // «щит»/мана цели (только при asda.Target.ShowMp)
	int32 SmoothHpMax = 0;             // максимум, по которому разворачивается сглаженная доля
	int32 SmoothSegments = 0;          // «жизни» цели на момент задания цели

	void UpdateSegmentedHp(int32 CurHp, int32 MaxHp, int32 Segments);   // эталон hudUpdateSegmentedHpBar
	void SetSimpleHp(int32 CurHp, int32 MaxHp);     // одиночный бар (цель без «жизней»)
	void SetLevelDigits(int32 Level);               // эталон HUD_TargetLevelDigits
	void SetRing(EAsdaTargetRing Ring);             // 4 кольца + их анимированные оверлеи
	void SetElementMark(int32 Idx);                 // −1 = все метки погасить
	void SetNameAndColor(const FString& Name, const FColor& Srgb);
	void SetPortraitFromAtlas(const FString& AtlasName, int32 Cell, int32 CellPx, int32 Cols);
	void ClearPortrait();                           // эталон: пустая текстура 32×32
	void SetClanMark(AActor* Target);               // эмблема клана цели (RT 128×64, под-прямоугольник 23,12,64,42)
	void RefreshEnemyStateIcons(AActor* Target);    // 28 иконок состояний цели
	void ClearEnemyStateIcons();
	void TickBeingAttackBlink();
	// ★ 04.11.2026 Подсказка иконки состояния цели (эталон `HUD_TargetBuffIcon_OnClick@0xE7FA70`): наведение на слот 6703+i с
	//   id ≥ 0 → `uiHelpEntryLayoutAndShow` (type 0, stateinfotip) / `…Alt` (type 1, soulstateinfotip); уход — гашение.
	void TickStateIconTips();
	FAsdaHelpTipHover StateTip;   // контрол под курсором и показанная подсказка (владелец `EAsdaTipOwner::TargetState`)                    // пила альфы 255 → 0 шагом 13 (эталон)

	// ── Мелкие помощники ────────────────────────────────────────────────────────────────────────
	// Узел-план ассета по имени контрола JUI: ищет `<Idc>_<номер>_<план>` (импортёр даёт планам
	//   именно такое имя — закон jui-importer-widget-naming). Точный разбор имени, без «contains»:
	//   иначе `PVP_HPBAR` подхватил бы `PVP_HPBAR_Y` (у них общий префикс).
	UWidget* FindPlane(const TCHAR* Idc, int32 PlaneIdx) const;
	// Первый `UImage` среди прямых детей панели (иконки состояний: номер плана у них «плавает»).
	static UImage* FirstImageIn(UWidget* Panel);
	// Ширина/левый край плана-заливки для `FAsdaMiniBarFill` (снимается ОДИН раз при связывании).
	static void SetupBar(FAsdaMiniBarFill& Bar, UImage* Fill, UImage* Back, float FallbackW);

	// Эталонная лесенка цвета имени/уровня в ОКНЕ ЦЕЛИ (верхняя ступень FFAAAAAA, не FF808080).
	static FColor TargetNameColor(int32 LevelDiff, int32 Aggression);
	// Кольцо по байту агрессии (`ent+6979`): 1/5 Normal, 2 Named, 3 Boss, 4 Elite, иначе None.
	static EAsdaTargetRing RingFromAggression(int32 Aggression);
	// Категория эмблемы цели-игрока по `target+1080` (ClassId): 1..7→0, 12..18→1, 23..29→2, 0→3,
	//   иначе −1 (эмблемы нет). Дословно `HUD_UpdateTargetFrame` (ui_core.c:77548-77570).
	static int32 EmblemTypeFromClassId(int32 ClassId);

	// ── Окно и узлы ─────────────────────────────────────────────────────────────────────────────
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win;              // WBP_PVP_TARGET

	UPROPERTY(Transient) TObjectPtr<UWidget>     BaseGroup;        // PVP_TARGET00_grp (261×69) — база центрирования
	UPROPERTY(Transient) TObjectPtr<UImage>      BaseImage;        // PVP_TARGET00_10_0 (фон панели)

	UPROPERTY(Transient) TObjectPtr<UWidget>     RingNormal;       // Target_Gear_Normal (94)
	UPROPERTY(Transient) TObjectPtr<UWidget>     RingNamed;        // PVP_TARGETNAMED    (92)
	UPROPERTY(Transient) TObjectPtr<UWidget>     RingElite;        // PVP_TARGETElite    (93)
	UPROPERTY(Transient) TObjectPtr<UWidget>     RingBoss;         // PVP_TARGETBOSS     (91)
	UPROPERTY(Transient) TObjectPtr<UWidget>     RingNormalAni;    // Target_Gear_NormalAni (декор-оверлей)
	UPROPERTY(Transient) TObjectPtr<UWidget>     RingNamedAni;     // PVP_TARGETNamedAni
	UPROPERTY(Transient) TObjectPtr<UWidget>     RingEliteAni;     // PVP_TARGETEliteAni
	UPROPERTY(Transient) TObjectPtr<UWidget>     RingBossAni;      // PVP_TARGETBossAni

	UPROPERTY(Transient) TObjectPtr<UWidget>     LvBaseGroup;      // PVP_LVTxtBase_grp (якорь блока цифр, 40×40)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  LvText;           // PVP_LVTXT (53) — скрыт, как в эталоне
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> DigitProto;    // PVP_Count0..9 — ПРОТОТИПЫ спрайтов цифр
	// Три позиции цифр (сотни/десятки/единицы). Эталон делает по 2 клона на каждую из 10 цифр
	//   (`CControl_InstantiateChild`, ui_core.c:74869/74873) — 30 контролов; здесь достаточно трёх
	//   картинок с подменой браша: результат тот же, узлов в 10 раз меньше.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> DigitSlot;     // [0]=сотни, [1]=десятки, [2]=единицы
	FSlateBrush DigitBrush[10];                                    // брашы глифов 0..9
	bool  bDigitBrushValid[10] = {};
	float DigitProtoX = 0.f, DigitProtoY = 0.f, DigitProtoW = 19.f;  // геометрия прототипа (PVP_Count0)
	float LvBaseW = 40.f;                                          // ширина PVP_LVTxtBase (делитель /2 и /3)

	UPROPERTY(Transient) TObjectPtr<UTextBlock>  NameText;         // PVP_TARGETNAME (44)

	// ★ Четыре полосы РОВНО в эталонном порядке массива `CHUD::gap_818[4]`:
	//   0 = PVP_HPBAR (красная), 1 = PVP_HPBAR_Y (жёлтая), 2 = PVP_HPBAR_G (зелёная),
	//   3 = PVP_HPBAR_P (фиолетовая). Индекс = номер «жизни» цели, поэтому босс по мере
	//   расходования сегментов идёт сверху вниз: фиолетовая → зелёная → жёлтая → красная.
	UPROPERTY(Transient) TArray<FAsdaMiniBarFill> HpBar;           // всегда 4 элемента (SetNum в ResetWidgets)
	// ★★ ЦИФРЫ ЗДОРОВЬЯ ПОВЕРХ ПОЛОСЫ (просьба владельца 2026-08-25) — «текущее/максимум» по центру.
	//   ⚠ В ОРИГИНАЛЕ ИХ НЕТ: у окна `PVP_TARGET` нет ни такого контрола, ни кода, который бы его
	//   заполнял (в отличие от полос игрока, где числа есть). Поэтому подпись создаётся НАШИМ
	//   виджетом в рантайме и кладётся поверх полосы `PVP_HPBAR` (в .JUI её место — 56,20, 157×18).
	//   Ключ отката: `asda.Target.HpNumbers 0`.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HpNumbers = nullptr;
	void EnsureHpNumbers();                       // создать подпись (один раз на виджет)
	void UpdateHpNumbers(int32 CurHp, int32 MaxHp);
	UPROPERTY(Transient) FAsdaMiniBarFill        MpBar;            // PVP_MPBAR (55)
	UPROPERTY(Transient) TObjectPtr<UWidget>     MpBarHide;        // PVP_MPBAR_HIDE (56) — чёрный перекрыватель
	UPROPERTY(Transient) TObjectPtr<UWidget>     HpBuff;           // PVP_HPBuff (46) — ЧАСТИЧНО: логики нет
	UPROPERTY(Transient) TObjectPtr<UWidget>     MpBuff;           // PVP_MPBuff (58) — декоративный

	UPROPERTY(Transient) TObjectPtr<UImage>      Portrait;         // PVP_IMAGE (57)
	UPROPERTY(Transient) TObjectPtr<UImage>      BeingAttack;      // PVP_BEING_ATTACK (6701)
	UPROPERTY(Transient) TObjectPtr<UWidget>     BatFirstMark;     // PVP_BAT_FIRST_MARK (6700)

	UPROPERTY(Transient) TObjectPtr<UWidget>     EleBaseGroup;     // PVP_ELE_Base_grp (подложка + 6 меток)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> EleMark;      // PVP_MARK00..05 (47..52)

	UPROPERTY(Transient) TObjectPtr<UWidget>     ClanGroup;        // PVP_ClanMarkBase_grp (280)
	UPROPERTY(Transient) TObjectPtr<UImage>      ClanMark;         // PVP_ClanMark (281)

	UPROPERTY(Transient) TObjectPtr<UWidget>     EnemyStateGroup;  // ENERMY_STATE_CONTROLWIN_grp (6702)
	UPROPERTY(Transient) TArray<FAsdaEnemyStateSlot> EnemySlots;   // 28 слотов (6703..6730)

	// ── Состояние ───────────────────────────────────────────────────────────────────────────────
	UPROPERTY(Transient) TWeakObjectPtr<AActor> CurTarget;         // эталон `hud->targetEntityPtr`
	int32  CurUserIdx     = -1;    // userNum цели-игрока (−1 = цель не игрок)
	int32  CurRenderId    = -1;    // renderId цели-моба  (−1 = цель не моб ИЛИ запечённое превью)
	// ★ Признак «цель — моб» отдельным флагом: у запечённого превью-актора `NetRenderId == -1`,
	//   и вывод «renderId нет ⇒ это игрок» отправил бы запрос состояний в список игроков.
	bool   bTargetIsMob   = false;
	int32  CurSegments    = 0;     // «жизни» цели (ent+18700) — 0 = односегментный бар
	int32  LastHpCur      = -1;    // кэш, чтобы не переписывать бары каждый кадр
	int32  LastHpMax      = -1;
	int32  LastLevel      = -1;
	FString LastName;              // эталон перерисовывает имя ТОЛЬКО при изменении (:77585-77593)
	// ★★★ КЭШ ЗАПРОСА HP/MP ЦЕЛИ (6538) — дословно поля эталонного `CHUD`:
	//   `targetStateCacheId` = charId цели, по которой запрос уже ушёл (эталон `ui_core.c:75922`),
	//   `targetPrevUserIdx`  = её userNum. Оба нужны в теле следующего 6538: сервер по ним
	//   вычёркивает нас из списка наблюдателей ПРЕЖНЕЙ цели. Снятие цели сбрасывает оба в −1
	//   (эталон `hudResetCursorItemSlot`, `ui_core.c:76065-76066`).
	int32  TargetReqCharId  = -1;
	int32  TargetReqUserNum = -1;
	bool   bFrameShown    = false;
	bool   bControlsReady = false;
	bool   bBeingAttackOn = false; // рамка «цель в бою» показана (гейт мигания)
	float  BlinkAlpha     = 255.f; // текущая альфа пилы (эталон: −13 за вызов, ≤0 → 255)
	double LastTargetPollS = -1.0; // время последнего опроса цели (лог)
	// ★ Центрирование. Сдвиг считается ОДИН раз — от ИСХОДНОГО левого края базы из .JUI. Читать
	//   его «на лету» нельзя: после первого применения слот уже сдвинут, и повторный расчёт даёт
	//   ноль (ровно это и случилось в живом прогоне — откат ключа не возвращал панель на место).
	float JuiBaseLeft = 0.f;       // Offsets.Left базовой группы, как её положил импортёр JUI
	float CenterDelta = 0.f;       // сдвиг «в центр» = −halfTargetWidth − JuiBaseLeft
	bool  bCentered   = false;     // сдвиг сейчас применён

	// z-order: панель цели — постоянный слой HUD, ниже всплывающих окон и подсказок.
	static constexpr int32 kTargetFrameZ = 95;
	// Шаг пилы альфы мигания (эталон `hudRefreshControlColorFromCount`: `alpha − 13`).
	static constexpr float kBlinkStep = 13.f;
	// Иконки состояний: ячейка атласа 24 px, 10 колонок (`gridIndexToXY_10col@0xEBFF70`).
	static constexpr int32 kStateIconPx = 24;
	static constexpr int32 kStateIconCols = 10;
	// Эмблема/портрет: ячейка 46 px, 11 колонок (`gridIndexToXY_11col@0xEBFFE0`, он же `sub_935651`).
	static constexpr int32 kEmblemPx = 46;
	static constexpr int32 kEmblemCols = 11;
};
