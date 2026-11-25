// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMiniStateSubsystem — окно MINISTATE_WIN (JUI IDNum 1, gamemain_jui.ini:7951), ЭТАП 1:
// структура окна + БАЗОВЫЕ ПОКАЗАТЕЛИ (HP, MP, вес, уровень, класс, имя).
//
// ════════════════════════ ЭТАЛОН (сборка IDB, RE/etalon; полный реверс — ═══════════════════════
//   Reports/ministate_win_re_A.md, машинный дамп всех 258 контролов _ministate_full_dump.txt)
//
// ОКНО. Корень `MINISTATE_WIN` (id 1, скин NoImage, ES0 PlaneAlwaysInvisible) — невидимый якорь
//   20×20 в АБСОЛЮТЕ (0, 6) базы 1024×768, то есть ЛЕВЫЙ ВЕРХ экрана (старая дока `Docs/UI/
//   HUD/MINISTATE_WIN.md` писала «левый-нижний» — ошибка). У ВСЕХ 258 контролов анкор fx=fy=0.00
//   и ни одного Xspan/Yspan ⇒ окно приколочено к левому-верхнему углу и НЕ тянется.
//   `AlwaysTop = 0` у всех ⇒ обычная полоса z. Блока `EVS` у мини-статуса НЕТ ⇒ окно НЕ таскается
//   (закон ui-window-drag-hittest: перетаскивание — это ДАННЫЕ JUI; ручки есть только у Soul Boost
//   56345/56346, и они двигают под-окно 56000, а не мини-статус).
//   Открытие — `uiWindowOpen@0xE7A770` (ui_core.c:75624): SetVisible(1) + MapInsert в менеджер,
//   плюс SetVisible(pPvpMode,1), если у локального игрока взведён PvP-флаг. `SetVisible(win, 0)`
//   в срезе НЕТ ВООБЩЕ — окно не прячется ни при смерти, ни в кат-сценах.
//
// HP  — `MINISTATE_HPBAR` (id 3, скин HP_RedBar, ProgressType 5, 125×18 @ (64,34)).
//   `CProgressBar_SetRange(0, 100)` (ui_core.c:74896) — значение это ПРОЦЕНТ.
//   `UI_UpdateHPBar@0xE78CF0` (ui_core.c:75036):
//       if (max <= 0) return;  pct = cur/max*100;  if (pct < 0 || cur > max) return;  ← пакет ИГНОРИТСЯ
//       SetValue(pct);  sprintf("%d/%d", cur, max) → MINISTATE_HPTEXT (32).
//   Скин: ES0 `PlaneAlwaysInvisible=1` (подложки НЕТ — фон даёт Main_MyInfoBase), ES1 = ЗАЛИВКА
//   diffuse FFFF0A00 (sRGB). ★ Импортёр JUI выбрасывает невидимые планы, поэтому в WBP у бара
//   ровно ОДИН узел `MINISTATE_HPBAR_84_0` — и это ES1. «Белой подложки» в ассете нет физически.
// MP  — `MINISTATE_MPBAR` (id 4, MP_BlueBar, ES1 FF0B63F4), всё симметрично; текст в MPTEXT (33).
//   Цвета текста берутся из EXS TextColor (закон ui-text-color-exs): HP FFF3D7CF, MP FFCEE3EC.
// СЛОИ «БУФФА» — `MINISTATE_HPBuff` (37, Box_Orange C8FFB300) и `MINISTATE_MPBuff` (89,
//   Box_Skyblue B466FBFF). Это НЕ рамки, а второй, «отстающий» индикатор: каждый кадр
//   `HUD_RefreshHPMPGauges_Frame@0xE79CD0` (ui_core.c:75446-75482) ставит им ШИРИНУ = pct × 1.25
//   (Box_Orange 100 px против 125 px полосы: 125/100 = 1.25) и ВСЕГДА делает их видимыми,
//   несмотря на `Visible="0"` в JUI. Источник процента — «второй» HP/MP (4058 dword@13 /
//   4059 i16@9), клампится к [0..max]; равен текущему → эталон обнуляет (слой нулевой ширины).
//   ★ Особый режим: `Net_GetCurrentMapType == 6` — все гауги принудительно обнуляются.
//
// ВЕС — три полосы 96×5 @ (92,74), одна поверх другой, + счётчик процентов атлас-цифрами.
//   `HUD_UpdateMiniWeightBar@0xE78FA0` (ui_core.c:75139):
//       if (cur <= 0) cur = 0;  char->[0x44A] = cur;  if (char->[0x448] <= 0) return;  ← НИЧЕГО не рисуем
//       pctF = cur/max*100;  SetValue(WEIGHTBAR, pctF);
//       обе доп. полосы: SetValue(pctF) + SetVisible(0);
//       pct = (int)pctF; if (pct < 0) pct = 0;  hudDrawThreeDigitNumber(pct);
//       pct <  70 → сброс флагов уведомлений;
//       pct >= 70 и флаг70 не поднят → чат loc 2358, цвет −21248 (0xFFFFAD00), 30 000 мс;
//       pct >= 90 и флаг90 не поднят → чат loc 2359, тот же цвет/время;
//       pct <  90 → SetVisible(WEIGHTBAR,1), KillTimer(25); при pct>=70 ещё SetVisible(WEIGHTBAR2,1);
//       pct >= 90 → SetVisible(WEIGHTBAR3,1), SetVisible(WEIGHTBAR,0), SetTimer(25, 250 мс)
//                   → WM_TIMER 0x19 → uiControlToggleVisible@0xE817A0 = МИГАНИЕ 4 Гц полосы 96.
//   Скины: WEIGHTBAR LightGray_Bar (ES1 FFBFBFBF), WEIGHTBAR2 Orange_Bar (FFFF6C00),
//   WEIGHTBAR3 DarkRed_Bar (FFF20000) — у всех трёх ОБА плана видимы (ES0 подложка + ES1 заливка).
//   ЦИФРЫ (`hudDrawThreeDigitNumber@0xE811F0`, ui_core.c:77737): базовые `WEICount0..9`
//   (id 130..139, скины SmNum_White_0..9, 4×5) + по 2 клона на цифру (родитель — MINISTATE_WEIGHTTEXT
//   id 36). Массив `pWeightDigit0[3*d + 0|1|2]`: [3d] = сотни, [3d+1] = десятки, [3d+2] = единицы.
//   X-оффсеты локальные и жёсткие: сотни 0, десятки 5, единицы 10; Y = Y контрола WEICount1 (= 2).
//   Сотни рисуются при pct/100 != 0, десятки при (десятки||сотни), единицы всегда.
//   Знак «%» — WEIPercent (idnum −1, SmNum_Bold_White%, @ (15,2)), всегда видим.
//   ★ ВТОРОЙ набор — `MINISTATE_WEIGHTTEXT_2` (166) с цифрами 170..179 и своим «%», Visible=0:
//   в `HUD_InitControls` эти id НЕ резолвятся, ссылок в срезе НЕТ ⇒ воспроизводим скрытым.
//   `MINISTATE_BONUSBAR` (165, Green_Bar) — то же самое, ссылок нет.
//
// УРОВЕНЬ — `MINISTATE_LVWIN` (27, NoImage ES0-inv, 40×39 @ (192,33)) = якорь-контейнер;
//   `MINISTATE_COUNT0..9` (id 10..19, скины LgNum_0..9, 19×32, локаль (0,3), все Visible=0).
//   `HUD_InitControls` (ui_core.c:74688-74707) на каждую цифру создаёт 2 клона с родителем
//   pMiniLvWin + один «прототип» pPlayerBuffProto. Ряды: единицы = базовые, десятки = клон A,
//   сотни = клон B. `HUD_UpdateLevelDigits@0xE7A160` (ui_core.c:75500):
//       все 30 гасятся; hundreds = lvl/100, tens = lvl%100/10, ones = lvl%10;
//       ★ БАЗА X/Y = позиция ПРОТОТИПА (клон COUNT0 → локаль (0,3)), ШИРИНА = LVWIN (40):
//       3 разряда: Xh = px − 6;            Xt = px + lvW/3 − 6;      Xo = px + 2*lvW/3 − 6
//       2 разряда: Xo = px + lvW/2 − 2;    Xt = px + lvW/2 − dW + 2  (dW = ширина цифры, 19)
//       1 разряд : Xo = px + lvW/2 − dW/2 − 2
//       Y всех = py. Деление ЦЕЛОЧИСЛЕННОЕ (40/3 = 13, 2*40/3 = 26).
//   ★ Полоса `MINISTATE_LEVELBAR` (id 2) физически лежит в ДРУГОМ окне — `EXPBar_WIn` (310);
//   класс CHUD общий на два окна. Опыт мини-статус НЕ рисует (уже сделан в UAsdaHudSubsystem).
//
// ИМЯ / КЛАСС / СТИХИИ — `hudRefreshClassAndElementIcons@0xE79850` (ui_core.c:75340):
//   6 значений стихий = i16 @ char->hpClamped[28 + 2k] (Fire,Water,Earth,Wind,Light,Dark),
//   берётся МАКСИМУМ (строгое `>`, при равенстве побеждает Fire); все 6 иконок гасятся,
//   включается одна (id 20..25). Класс — по байту `char->gap_438[0]` (+0x438, НЕ SelfJob/+0x445):
//     0 → ClsTenderfoot(28) · 1..7 → ClsWarrior(29) · 12..18 → ClsArcher(30) ·
//     23..29 → ClsWizard(31) · 8..11, 19..22, >29 → НИ ОДНОЙ (провалы так в оригинале).
//   Имя пишется в `MINISTATE_CharName` (5).
//   ★ ИКОНКИ СТИХИЙ В ОРИГИНАЛЕ ФАКТИЧЕСКИ НЕ ВИДНЫ: их родитель `MINISTATE_ELE_Base` имеет
//   Visible="0" и IDNum=−1 (коду недоступен), а `CControl_RenderRecursive@0x12DF320` заходит в
//   детей ТОЛЬКО при видимом родителе, `CControl_SetVisible@0x12DFB00` ставит флаг лишь себе.
//   Воспроизводим КАК ЕСТЬ: выбор стихии считаем и логируем, родитель остаётся скрытым.
//
// СОБЫТИЯ (`HUD_PlayerBuffIcon_OnClick@0xE7DB40`, ui_core.c:76727; коды CControl_*):
//   8 = RButtonDown · 9 = RButtonUp · 10 = RButtonClick · 11 = LButtonDblClick · 24 = конец тайм-действия.
//   • `case 11`, a2 == 97 (`MiniState_Click`, прозрачная зона 227×58 @ (10,14)) →
//     hudHideAndResetSoundFlags, g_nInputControlState = 0, g_nTargetUserIdx = Target_GetSelectedUserIdx(self),
//     TargetNameplate_Update, HUD_UpdateTargetFrame, hud_setTarget = **ВЫБРАТЬ СЕБЯ ЦЕЛЬЮ**
//     (ui_core.c:76884-76895). Старая дока «обработчик не найден» — ОШИБКА.
//   • `case 8`, a2 == 5110 (`pluscpack`) → большой тултип ITEM_TIPWIN: заголовок = loc(rec+4),
//     тело = sprintf(loc(rec+8), дни) из записи `g_WingDataTable` (= PlusPackageTable, 140 б)
//     по id крыла игрока; дни = g_BuffRemainSeconds / 86400 (ui_core.c:76801-76819).
//     `case 9` — переложить тултип к курсору, `case 10` — скрыть.
//   • `case 8`, a2 == 6 / 95 / 96 (три полосы веса) → `uiShowHelpTooltipForCode(a2)` — справка.
//   • `case 24`, a2 == 8 → показать `MINISTATE_Gear1` (7) по концу анимации `MINISTATE_AniGear1` (8).
//
// ═══════════════ ДОРАБОТКИ 06.10.2026 (`Reports/owner_1006/self_portrait_impl.md`) ═══════════════
// ЭМБЛЕМА КЛАНА — `MINISTATE_ClanMarkBase` (270, рамка Box_WhiteRound 36×36 @ (239,14), Visible=0) и
//   `MINISTATE_ClanMark` (271, 32×32 внутри). Видимость базы — `CHUD_SetVisible@0xE81F50` (SetVisible(CHUD+0x9B0)),
//   рисунок — `uiDrawPlayerPortrait@0xE81FF0` (ui_core.c:77886-77912):
//       кампвар (байт +69 записи текущей карты == 1) → ячейка 64×64 атласа `CampWar_MarkImage.tex`
//         по лагерю (`uiMakeAxisVec2`: 0 → (0,0), 1 → (64,0), 2 → (0,64)) + показать базу;
//       иначе → испечённый флаг клана, вырез (23,12)+64×42; клана нет (−1) → базу погасить.
//   Кто зовёт: вход на карту (game_main.c:3876, 3935-3949), снимок клана своего (4135/6153 →
//   charUpdateVisibleCharsFromPacket, entity_char.c:44687-44697), 6542 своего клана (social.c:3640-3650 +
//   флаг вкл → [148]=[152]=1, social.c:3656-3657 → перепечь и показать `CharFx_UpdateKingCrown`,
//   effects_particles.c:3620-3634; шаг загрузки карты game_main.c:3185-3203 при g_LoadProgress == 11 делает то же).
//   Гасят: 4107 код 1, 4117 код 1, 4132 код 2, 4133 код 2 (свой).
// ЗНАЧОК ГЛАВЫ ОТРЯДА — `MINISTATE_Partyer` (9): `Party_SetMasterLocal@0x9CCC30` (social.c:419-455) из
//   5102 → `sub_9CCD30` (SetVisible(CHUD+0x21C)): новый глава найден в составе — «я» → показать, иначе скрыть.
//   [ПОРТ] Оригинал НЕ гасит значок при выходе из отряда/роспуске — исправлено (`asda.Mini.PartyerHideOnLeave`).
// СМЕРТЬ — `HUD_RefreshHPMPGauges_Frame` (ui_core.c:75482-75497): при состоянии 6 (мёртв) каждый кадр
//   слои 37/89 = 0 и «второе» HP/MP персонажа = 0.
// НОТИСЫ 4206 (`netTradeRequestHandler@0xF4A190`): 108114/108131/108132 (команда 1), 108119/108120 (2/0).
//
// ════════════════════════════════ ЗАКОНЫ, КОТОРЫМ СЛЕДУЕМ ═══════════════════════════════════════
//   no-invented-ui / ui-editable-in-ue — окно = ИМПОРТИРОВАННЫЙ WBP_MINISTATE_WIN (258 узлов),
//     ни один виджет здесь не рисуется руками; все параметры вынесены в UPROPERTY.
//   ui-progressbar-animate-existing — анимируем САМ виджет: клип ширины слота плана ES1.
//   ui-srgb-gamma-rule — цвета скинов sRGB → FLinearColor::FromSRGBColor (импортёр уже применил;
//     здесь важно НЕ перекрашивать заливки в коде).
//   ui-text-color-exs — цвет текста из EXS, не из CaptionInfo (импортёр уже применил).
//   ui-autowrap-canvas-gotcha — bWrap = false у HPTEXT/MPTEXT/CharName (однострочные).
//   jui-align-anchors — анкор (0,0), фиксированные оффсеты, DPI-база 768 (уже в ассете).
//   jui-prefer-new-windows — неприменим: пары MINISTATE_NEW_WIN не существует.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "UI/AsdaBarSmoother.h"   // ★ общий сглаживатель полос здоровья/щитов
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Net/AsdaNetWorldTypes.h"   // ★ R1: FAsdaVitals — тип без класса сети (класс включает .cpp)
#include "UI/AsdaTipPanel.h"    // EAsdaTipOwner — владелец ОБЩЕЙ плашки-тултипа (одна на игру)
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick — двойной щелчок по порогу Windows
#include "AsdaMiniStateSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UCanvasPanel;

// Клипуемая заливка бара: сам виджет + ПОЛНАЯ ширина, снятая на этапе Setup.
//   Импортёр кладёт все вложенные узлы POINT-анкером (ApplyResponsiveSlot: Offsets = L,T,W,H),
//   поэтому клип = Offsets.Right := FullW · доля. Полную ширину нельзя читать «на лету»: после
//   первого же клипа в 0 она была бы потеряна (грабля EXP-бара, круг 3).
USTRUCT()
struct FAsdaMiniBarFill
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage> Fill = nullptr;   // план ES1 (заливка)
	UPROPERTY(Transient) TObjectPtr<UImage> Back = nullptr;   // план ES0 (подложка; у HP/MP её нет)
	UPROPERTY() float FullW = 0.f;                            // ширина 100 % из ассета
	UPROPERTY() float BaseLeft = 0.f;                         // левый оффсет (не меняется)
	bool IsValid() const { return Fill != nullptr && FullW > 0.f; }
};

// ═════════════ ЭТАП 2: один слот активного эффекта (STATE_ICON00..27 = 28 штук) ═════════════════
// Слот = группа `STATE_ICON<NN>_grp` + картинка-план `STATE_ICON<NN>_<idx>_0` (24×24, скин
//   NoImage_icon = пустой пиксель N_01) + подпись-стек `STATE_ICON<NN>` (тип контрола Text)
//   + 5 клонов таймера (эталон CControl_InstantiateChild от прототипов COOL_black/COOL_SFONT1..4,
//   ui_core.c:74736-74748) + COOLTIME<NN> (в срезе только резолвится и гасится).
USTRUCT()
struct FAsdaBuffSlotUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget>    Group = nullptr;      // STATE_ICON<NN>_grp
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon = nullptr;       // план 24×24
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StackText = nullptr;  // подпись стека
	UPROPERTY(Transient) TObjectPtr<UImage>     CoolTime = nullptr;   // COOLTIME<NN> (60-кадровая анимация)
	// [0] = подложка COOL_black (bufftimeback.tex 24×10), [1..4] = глифы COOL_SFONT1..4 (9×9)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Timer;
	// Что сейчас в слоте (эталонные поля CHUD): id (для type 2 — itemCode), typeFlag, стек.
	UPROPERTY(Transient) int32 BuffId = -1;      // gap_23C[16*slot+20]
	UPROPERTY(Transient) int32 TypeFlag = 0;     // statusIconTypeFlag
	UPROPERTY(Transient) int32 StackCount = 0;   // statusIconStackCount
};

UCLASS()
class ASDAREBORN_API UAsdaMiniStateSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaMiniStateSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Публичный API (он же точка входа консольных проверок asda.Mini.*) ──

	// Полный пересчёт всех показателей из живых данных (аналог hudRefreshFromCharacterStats@0xE796A0).
	UFUNCTION(BlueprintCallable, Category="Asda|MiniState")
	void RefreshAll();

	// HP: эталон UI_UpdateHPBar (пакет с cur > max или max <= 0 ИГНОРИРУЕТСЯ целиком).
	void ApplyHp(int32 Cur, int32 Max);
	// MP: эталон HUD_UpdateMPBar (та же защита).
	void ApplyMp(int32 Cur, int32 Max);
	// Вес: эталон HUD_UpdateMiniWeightBar (3 полосы, пороги 70/90, мигание 250 мс, чат 2358/2359).
	void ApplyWeight(int32 Cur, int32 Max);
	// Уровень: эталон HUD_UpdateLevelDigits (раскладка 1/2/3 разрядов).
	void ApplyLevel(int32 Level);
	// Имя + иконка класса + выбор стихии: эталон hudRefreshClassAndElementIcons.
	void ApplyNameClassElements(const FString& CharName, int32 ClassId, const TArray<int32>& Elements);
	// «Отстающие» слои HP/MP (Box_Orange / Box_Skyblue): ширина = pct × 1.25, слои всегда видимы.
	void ApplyVitalGhosts(int32 HpSecond, int32 HpMax, int32 MpSecond, int32 MpMax);
	// Иконка режима PvP (5300) — эталон sub_E82C00 (SetVisible(CHUD+0x13C, vis)).
	void SetPvpModeIcon(bool bVisible);
	// Иконка премиум-пакета (5110). ⚠ В ОРИГИНАЛЕ кода показа НЕТ (иконка всегда скрыта) — метод
	//   существует только для консольной проверки ПКМ-тултипа; штатно никто его не зовёт.
	void SetPlusPackIcon(bool bVisible);
	// ★ 08.11.2026: Plus Package (зелье уровня) действует — флаг последнего 6585 (эталон `charSitStatePoll`, rec+10).
	bool IsPlusPackActive() const { return bWingActive; }
	// Значок главы отряда (9) — эталон `sub_9CCD30` (SetVisible(CHUD+0x21C)). Состояние хранится в подсистеме
	//   (bPartyerOn) и переживает пересоздание окна при смене карты, как живой CHUD оригинала.
	void SetPartyerIcon(bool bVisible);

	// ═══════════════ ЭМБЛЕМА КЛАНА НА ПОРТРЕТЕ (270/271) ═══════════════
	// База 270 — эталон `CHUD_SetVisible@0xE81F50`.
	void SetClanMarkBaseVisible(bool bVisible);
	// Рисунок 271 — эталон `uiDrawPlayerPortrait@0xE81FF0` (ветки кампвара и флага, см. шапку).
	void DrawPlayerPortraitMark();
	// Вход на карту — эталон game_main.c:3876 + 3935-3949 (зовётся из EnsureWidget; открыт и самотесту).
	void ApplyClanMarkOnMapEntry();

	// ═══════════════ САМОТЕСТ asda.UI.SelfTestMiniState ═══════════════
	// Синхронная часть: пакеты штатным HandlePacket + Tick, контрольные пары. Возвращает число провалов.
	int32 RunSelfTestSync();
	// Отложенная часть: экранная геометрия значка главы и эмблемы после отрисовки (GetCachedGeometry).
	int32 RunSelfTestGeometry();
	// Возврат состояния после самотеста (клан, отряд, карта, ключи, зеркала сети).
	void FinishSelfTest();

	// ═══════════════════════ ЭТАП 2: ИКОНКИ АКТИВНЫХ ЭФФЕКТОВ ═══════════════════════════════════
	// Перерисовать 28 слотов STATE_ICON00..27 из списка UAsdaEffectsSubsystem.
	//   bWithTimers = false → эталон `HUD_PlayerBuffIcons_Update@0xE7CEB0` (иконки + стек, БЕЗ таймеров);
	//   bWithTimers = true  → эталон `HUD_PlayerBuffTray_Update@0xE7BFB0` (иконки + стек + таймеры
	//                          `Skill_DrawBuffTimerIcon@0xE830B0` + авто-снятие зелья/свитка).
	void RefreshBuffIcons(bool bWithTimers);
	// Перерисовать ивент-иконки EXP_Buff/Drop_Buff (эталон gameUi_updateScheduledClockDisplay@0xE819D0).
	void RefreshEventIcons();
	// Дамп слотов иконок/таймеров/ивент-иконок (asda.Mini.BuffDump).
	FString DumpBuffSlots() const;
#if !UE_BUILD_SHIPPING
	// 04.11.2026 (selftest asda.Buff.SelfTestExpiry): own status icon of slot i and its id (-1 = empty) for the hover regression.
	UImage* DebugBuffSlotIcon(int32 i) const { return BuffSlots.IsValidIndex(i) ? BuffSlots[i].Icon.Get() : nullptr; }
	int32   DebugBuffSlotId(int32 i) const { return BuffSlots.IsValidIndex(i) ? BuffSlots[i].BuffId : -1; }
#endif

	// Многострочный дамп КАЖДОГО контрола этапа 1 (видимость/геометрия/значения) — доказательная
	//   строка для asda.Mini.Dump (закон full-structural-analysis-always).
	FString DumpControls() const;

	// Воспроизведение C-`sprintf` с ОДНИМ int-аргументом (эталон гонит тело тултипа через sprintf,
	//   ui_core.c:76812). Нужно 1:1, потому что escape-последовательности процентов в данных
	//   съедаются sprintf-ом — так
	//   выглядит эталонный клиент. Подробности — в .cpp.
	static FString LegacySprintfOneInt(const FString& Fmt, int32 Arg);

	// ★ ПОВЕДЕНИЕ ПО УМОЛЧАНИЮ (решение юзера 2026-08-07): дефект оригинала НЕ воспроизводим —
	//   подставляется только число дней в первое `%[флаги]d|i|u`, все прочие «%» остаются
	//   обычными символами. Разметку `<FC=…>` и переносы не затрагивает.
	static FString SubstituteDaysKeepPercents(const FString& Fmt, int32 Days);

	// Все виды Plus Package из DT_PlusPackage с их loc-строками (asda.Mini.PlusPackList):
	//   доказательство «для каждого вида окно описания формируется» + честный список заглушек.
	FString DumpPlusPackages() const;

	// Эмуляция пакета 6585 без сервера (asda.Mini.PlusPack): вид пакета, остаток, флаг активности.
	void SimulateWingState(int32 InWingId, int32 RemainSec, bool bOn);

	// ═══ ЭТАП 3: общий тултип и хит-тест для UAsdaSoulGuardSubsystem ════════════════════════════
	// Показать/убрать тултип в ITEM_TIPWIN теми же формулами, что и у иконок эффектов
	//   (`uiHelpEntryLayoutAndShow@0xEA6EC0`: w = clamp(ширина текста, 90, 218) + 27). Окно тултипа
	//   одно на весь мини-статус — второй такой заводить нельзя (эталон тоже держит один g_ItemTooltip).
	//   ★★★ ВЛАДЕЛЕЦ ОБЯЗАТЕЛЕН (см. шапку `EAsdaTipOwner`). Плашка одна на всю игру, и покадровые
	//   опросчики курсора (хранитель души, системное меню, хотбар) раньше гасили её БЕЗУСЛОВНО —
	//   тултип предмета жил один кадр. Теперь скрыть может только тот, кто показал.
	void ShowSharedTip(const FString& Title, const FString& BodyRaw,
	                   EAsdaTipOwner Owner = EAsdaTipOwner::Legacy);
	// Возвращает true, если плашка реально была скрыта (владелец совпал).
	bool HideSharedTip(EAsdaTipOwner Owner = EAsdaTipOwner::Legacy);
	// Курсор над узлом (тот же хит-тест, что у иконок эффектов).
	bool IsCursorOverWidget(const UWidget* W) const { return IsCursorOver(W); }

	// ═══ ЭТАП 3: доступ к живому виджету окна для UAsdaSoulGuardSubsystem ═══════════════════════
	// Под-окна `Soul_Boost_Win` (56000) и `Soul_Boost_Texts` (56200) физически лежат ВНУТРИ
	//   WBP_MINISTATE_WIN, поэтому система хранителя берёт узлы из ЭТОГО же виджета, а не открывает
	//   своё окно. nullptr, пока мини-статус не поднят и не разобран.
	UUserWidget* GetMiniWidget() const { return bControlsReady ? MiniWidget.Get() : nullptr; }
	// Счётчик пересборок дерева (SetupControls). Меняется → потребителю надо перерезолвить свои узлы.
	int32 GetWidgetGeneration() const { return WidgetGeneration; }

	// ★★★ ОБЩЕЕ ЯДРО ПРОГРЕСС-БАРОВ ПОРТА (закон ui-progressbar-animate-existing + дедуп
	//   `code_dedup_audit_2`). Раньше эти два хелпера были приватными, и каждой новой полосе
	//   пришлось бы заводить СВОЮ копию клипа. Теперь они публичные: их зовут и мини-статус,
	//   и полоса применения навыка (`UAsdaCastBarSubsystem`), и любой будущий бар.
	//   Логика не менялась — только область видимости.
	//   ClipFill: анимируем САМ виджет, обрезая ширину слота плана-заливки ES1 (не подменяя браш).
	static void ClipFill(const FAsdaMiniBarFill& Bar, float Frac);
	//   GetSlotRect: локальная позиция узла в его канвасе (Offsets.L/T) и размер (Offsets.R/B) —
	//   раскладка импортёра JUI (POINT-анкер).
	static bool GetSlotRect(const UWidget* W, FVector2D& OutPos, FVector2D& OutSize);

private:
	// ── Жизненный цикл ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();

	// ── Делегаты сети ──
	void OnVitalsChanged(int32 UserIdx, const FAsdaVitals& V);
	void OnSelfWeight(int32 Cur, int32 Max);
	void OnSelfExpChanged(int32 Level, int64 Exp, int64 Gained, bool bFullExp, int32 SrcRenderId);
	void OnPvpMode(int32 UserIdx, bool bOn, int32 SubType);
	void OnWingState(int32 CharId, int32 WingId, int32 RemainSec, bool bOn);
	// ★ 06.10.2026: 4206 команды 0/2 (нотисы 108120/108119 своему персонажу).
	void OnPvpModeRefused(int32 UserIdx, int32 Cmd);
	// ★ 06.10.2026: события эмблемы клана (колбэки только кладут событие в очередь — разбор в Tick,
	//   когда мир-подсистема уже обновила данные своей плашки; порядок подписчиков делегата не важен).
	void OnClanFlagNet(int32 ClanId, bool bFlagOn, const TArray<uint8>& Flag40);       // 6542
	void OnClanSnapshotNet(int32 CharId, int32 ClanId, const FString& ClanName, const FString& ClanRank,
		bool bFlagOn, const TArray<uint8>& Flag40);                                    // 4135/6153
	void OnClanResetNet(int32 CharId, const FString& CharName);                         // 4107/4117/4133
	void OnClanBaseInfoPushNet(int32 Opcode, const TArray<uint8>& Body);                // 4132 код 2
	// ★ 06.10.2026: 5102 (смена главы отряда) → значок 9; сигнал отряда — после разбора очереди пакетов.
	void OnPartyMasterChanged(int32 CharId, bool bFound, bool bMe);

	// ── Ввод (эталонные коды событий 8/9/10/11 приходят к нам опросом: UImage клика не поглощает) ──
	// ★ Плавный ход полос жизни и маны игрока (см. FAsdaBarSmoother). Текстовые значения рядом с
	//   полосой НЕ сглаживаются — цифры должны быть точные, плавной делается только заливка.
	FAsdaBarSmoother HpSmooth;
	FAsdaBarSmoother MpSmooth;
	void TickVitalBars(float DeltaSeconds);

	void TickPointer();
	bool IsCursorOver(const UWidget* W) const;
	// Двойной ЛКМ по MiniState_Click (id 97) → взять СЕБЯ целью.
	void OnSelfTargetDoubleClick();

	// ── Тултип премиум-пакета (ITEM_TIPWIN 5000: ITEM_TIPTITLE 5001 + ITEM_TIPCONTENS 5002) ──
	void ShowPlusPackTooltip();
	// Ленивое создание окна тултипа (общее для Plus Package и иконок эффектов).
	bool EnsureTipWidget();
	// Раскладка и показ тултипа. bBuffFormula:
	//   false — формулы Plus Package (`tutorialPopupLayoutLarge`: w = clamp(текст,90,218) + 38);
	//   true  — формулы тултипа ЭФФЕКТА (`uiHelpEntryLayoutAndShow@0xEA6EC0`, ui_core.c:83020:
	//           w = clamp(textFontMeasureMarkupString, 90, 218) + 27; h = 16*(строк+1) + 12).
	void LayoutAndShowTip(const FString& Title, const FString& BodyRaw, bool bBuffFormula,
	                      EAsdaTipOwner Owner = EAsdaTipOwner::MiniState);
	// Число строк С УЧЁТОМ ПЕРЕНОСА по ширине WrapW — эталон `textFontCountWrappedLines_Active`.
	//   ОДНА функция на оба тултипа (Plus Package и иконки эффектов): по ней считается высота панели.
	static int32 CountWrappedLines(const FString& Plain, const struct FSlateFontInfo& Fnt, float WrapW);
	void HideTooltip();
	void LayoutTooltipAtCursor();
	// Пересобрать 9-slice панель тултипа под новый размер (эталон uiPanel9SliceLayout):
	//   углы сохраняют размер, края тянутся, центр заполняет остаток.
	void Resize9Slice(const TArray<UImage*>& Nine, const FVector2D& NewSize);

	// ★★★ ВЫРАВНИВАНИЕ ТЕКСТА ПО EXS TextAlign (маска 0x1 hCenter, 0x2 hRight, 0x4 vCenter, 0x8 vBottom).
	//   ЗАЧЕМ: генератор JUI берёт маску из `CaptionInfo[3]`, а НЕ из `EXS TextAlign` — ровно тот же
	//   промах, что был с цветом (закон ui-text-color-exs). У MINISTATE_HPTEXT/MPTEXT/CharName
	//   `CaptionInfo` = "0 0 … 0" (маска 0 = лево/верх), а `EXS TextAlign = 5` = центр+центр —
	//   отсюда «числа HP/MP не отцентрованы». Здесь чиним в рантайме (перегенерация WBP запрещена),
	//   а сам генератор поправлен на будущее.
	//   JuiH — ИСТИННАЯ высота контрола из .JUI (генератор поднимал её до 14 ради шрифта).
	void ApplyJuiTextAlign(class UTextBlock* Tb, int32 AlignMask, float JuiH, float TextPosY = 0.f);

	// ── ЭТАП 2: внутреннее ──
	void OnEffectsChanged(int32 UserIdx, bool bWithTimers);
	void OnEventBonusChanged();
	// Поставить в слот иконку-ячейку атласа (эталон CControl_DrawTextureRect(ctrl, tex, u, v, 24, 24)
	//   + IconAtlas_IndexToUV = gridIndexToXY_10col@0xEBFF70: X = 24*(i%10), Y = 24*(i/10)).
	bool SetSlotIcon(int32 Slot, const FString& AtlasName, int32 Cell);
	// Спрайтовый таймер под иконкой — эталон Skill_DrawBuffTimerIcon@0xE830B0 (skill_buff.c:34917).
	void DrawBuffTimer(int32 Slot, const struct FAsdaEffectEntry& E, int32 Buff0SubType);
	// Спрятать все 5 контролов таймера слота.
	void HideBuffTimer(int32 Slot);
	// Наведение/клик по иконкам эффектов (эталон HUD_PlayerBuffIcon_OnClick case 8/9/10/11).
	void TickBuffPointer();
	void ShowBuffTooltip(int32 Slot);

	// ── Внутреннее ──
	int32 ResolveSelfLevel() const;   // SelfLevel, иначе запись char-select (сессия без 4005)
	int32 ResolveSelfClass() const;   // SelfClassId (+0x438), иначе FAsdaCharRecord::Job
	// (ClipFill и GetSlotRect подняты в публичную секцию — общее ядро баров, см. выше.)
	static void SetVis(UWidget* W, bool bVisible);                   // Visible ↔ Collapsed, безопасно для nullptr
	// Клон атлас-цифры (эталон CControl_InstantiateChild): копия браша и слота исходного глифа.
	UImage* CloneGlyph(UImage* Src, const TCHAR* NameHint);
	static void SetSlotPos(UWidget* W, const FVector2D& Pos);
	// Чат-уведомление о перегрузе (эталон: CControl_SetTooltipText(g_pChatWnd, loc, −21248, 30000)).
	void PostWeightNotice(int32 LocId);
	// ★ 06.10.2026: нотис режима боя 4206 (эталон: журнал + 221, белый −1, 10 000 мс) — см. .cpp.
	//   ★ 18.10.2026 (ревизия Ф4, мелочь 7): bChatOnly — только системный чат (локальный отбой режима, ui_core.c:32302).
	void PostPvpNotice(int32 LocId, bool bChatOnly = false);

	// ── ★ 06.10.2026: эмблема клана — внутреннее ──
	// Разобрать очередь событий эмблемы (из Tick, после EnsureWidget).
	void ProcessPendingClanMark();
	// Своя плашка (данные клана своего персонажа: id, флаг, дизайн) — nullptr вне мира.
	const class UAsdaNameplateComponent* SelfPlate() const;
	// Байт +69 записи ТЕКУЩЕЙ карты == 1 (эталон `*(g_pLocalEntity + 69) == 1`).
	bool IsCampWarMap() const;
	// Флаг клана своего персонажа включён (эталон `char+2505`, `gap_96A[81]`).
	bool IsSelfClanFlagOn() const;
	// ── ★ 06.10.2026: смерть — обнулить слои 37/89 и «второе» HP/MP (эталон ui_core.c:75482-75497) ──
	void ApplyDeathGaugeReset();
	// ★ ревизия МЕЛК-3: свой персонаж МЁРТВ (состояние 6) — без нокдаунов 7/8/9.
	bool IsSelfDeadExact() const;

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> MiniWidget;
	// ★ ДЕДУП 2026-08-08: окно тултипа ОДНО на игру и принадлежит `UAsdaItemTooltipSubsystem`
	//   (порт глобального `g_ItemTooltip`). Здесь — только ссылки на общую плашку и её виджет.
	UPROPERTY(Transient) TObjectPtr<class UAsdaTipPanel> SharedTip;   // общая плашка (ITEM_TIPWIN 5000)
	UPROPERTY(Transient) TObjectPtr<UUserWidget> TipWidget;      // её виджет (кэш, только для чтения)

	UPROPERTY(Transient) FAsdaMiniBarFill HpBar;                 // MINISTATE_HPBAR (3)
	UPROPERTY(Transient) FAsdaMiniBarFill MpBar;                 // MINISTATE_MPBAR (4)
	UPROPERTY(Transient) FAsdaMiniBarFill WeightBar;             // MINISTATE_WEIGHTBAR (6)  серая
	UPROPERTY(Transient) FAsdaMiniBarFill WeightBar2;            // MINISTATE_WEIGHTBAR2 (95) оранжевая
	UPROPERTY(Transient) FAsdaMiniBarFill WeightBar3;            // MINISTATE_WEIGHTBAR3 (96) тёмно-красная
	UPROPERTY(Transient) FAsdaMiniBarFill BonusBar;              // MINISTATE_BONUSBAR (165) — ссылок в срезе нет

	UPROPERTY(Transient) TObjectPtr<UImage>     HpGhost;         // MINISTATE_HPBuff (37)
	UPROPERTY(Transient) TObjectPtr<UImage>     MpGhost;         // MINISTATE_MPBuff (89)
	UPROPERTY(Transient) float                  HpGhostFullW = 0.f;   // 100 px по JUI
	UPROPERTY(Transient) float                  MpGhostFullW = 0.f;   // 90 px по JUI

	UPROPERTY(Transient) TObjectPtr<UTextBlock> HpText;          // MINISTATE_HPTEXT (32)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MpText;          // MINISTATE_MPTEXT (33)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CharNameText;    // MINISTATE_CharName (5)

	// Цифры веса: [0..9] базовые (единицы), [10..19] клоны десятков, [20..29] клоны сотен.
	//   Порядок эталона — pWeightDigit0[3d+2] единицы, [3d+1] десятки, [3d] сотни.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> WeiDigitOnes;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> WeiDigitTens;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> WeiDigitHundreds;
	UPROPERTY(Transient) TObjectPtr<UImage>         WeiPercentGlyph;   // WEIPercent (idnum −1)
	UPROPERTY(Transient) float                      WeiDigitY = 2.f;   // Y = Y контрола WEICount1

	// Цифры уровня: базовые = единицы, два ряда клонов = десятки/сотни (эталон 1:1).
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> LvDigitOnes;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> LvDigitTens;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> LvDigitHundreds;
	UPROPERTY(Transient) FVector2D LvProtoPos = FVector2D::ZeroVector;   // локаль прототипа-цифры (0,3)
	UPROPERTY(Transient) float     LvWinW = 40.f;                        // ширина MINISTATE_LVWIN
	UPROPERTY(Transient) float     LvDigitW = 19.f;                      // ширина глифа LgNum

	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ClassIcons;   // [0] Tenderfoot .. [3] Wizard (id 28..31)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ElementIcons; // id 20..25 (Fire..Dark)
	UPROPERTY(Transient) TObjectPtr<UWidget>        ElementBase;  // MINISTATE_ELE_Base (скрыт в оригинале)

	// ═════════════ ЭТАП 2: слоты активных эффектов (STATE_ICON00..27 = 28 штук) ══════════════════
	UPROPERTY(Transient) TArray<FAsdaBuffSlotUi> BuffSlots;
	// Прототипы таймера из ассета (лежат внутри STATE_ICON00_grp): их клонируем на все 28 слотов
	//   и прячем сами прототипы (в оригинале они остаются, но их скин NoImage2_icon пуст).
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TimerProtos;

	// Ивент-бонусы: контейнер EXP_Buff (5100) + 7 иконок EXP + 3 иконки Drop.
	UPROPERTY(Transient) TObjectPtr<UWidget> EventBuffGroup;                  // EXP_Buff_grp (5100)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ExpBuffIcons;             // 5101..5107 (×0.5 … ×4)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> DropBuffIcons;            // 5203/5205/5207 (×2/×3/×4)

	int32 HoverBuffSlot = -1;      // слот под курсором (эталон: событие 8/9/10 диспетчера)
	// ★★★ Аудит №3 (B3): ВТОРОЙ экземпляр опроса мыши, и он обязан остаться отдельным (прежний латч
	//   `bBuffLmbWasDown`). Иконки эффектов обслуживает `TickBuffPointer`, у которого СВОИ условия
	//   входа (`bControlsReady` + ровно 28 слотов), не совпадающие с гейтами `TickPointer` (модалка,
	//   чужой слой поверх). Один общий латч на две функции проглатывал бы фронт: та, что отработала
	//   первой, «съедала» бы нажатие у второй.
	FAsdaMouseFrame BuffMouse;
	// Двойной клик по иконке баффа (ключ — слот). ★ U1 (В2-A): общий `FAsdaDblClick` (было — пара полей и игровое время).
	FAsdaDblClick BuffDbl{ -1, -1, -1.0, 0.5 };   // `LegacySec` = прежний `DoubleClickSec` (для самотеста U1; окно передаётся явно)

	UPROPERTY(Transient) TObjectPtr<UImage>  PvpModeIcon;    // MiniState_PvPMode (5300)
	UPROPERTY(Transient) TObjectPtr<UImage>  PlusPackIcon;   // pluscpack (5110)
	UPROPERTY(Transient) TObjectPtr<UImage>  PartyerIcon;    // MINISTATE_Partyer (9) — значок главы отряда (5102 → Party_SetMasterLocal)
	UPROPERTY(Transient) TObjectPtr<UWidget> ClanMarkBase;   // MINISTATE_ClanMarkBase_grp (270) — рамка эмблемы
	UPROPERTY(Transient) TObjectPtr<UImage>  ClanMarkImg;    // MINISTATE_ClanMark_146_0 (271) — сама эмблема 32×32
	UPROPERTY(Transient) TObjectPtr<UWidget> ClickZone;      // MiniState_Click_148_HIT (id 97)
	UPROPERTY(Transient) TObjectPtr<UWidget> Gear1Group;     // MINISTATE_Gear1_grp (7)
	UPROPERTY(Transient) TObjectPtr<UWidget> AniGear1Group;  // MINISTATE_AniGear1_grp (8) — стартер в срезе не найден

	// ★ Узлы окна тултипа (заголовок / тело / 9 планов рамки) больше здесь НЕ кэшируются:
	//   ими владеет `UAsdaTipPanel` (см. SharedTip выше) — одна плашка на всю игру, как g_ItemTooltip.

	// ── Состояние ──
	bool  bControlsReady = false;
	// ★ ЭТАП 3: растёт при каждой успешной пересборке дерева (SetupControls). Система хранителя
	//   держит свой снимок узлов и перерезолвливает его, когда номер поколения изменился.
	int32 WidgetGeneration = 0;
	bool  bWeightWarn70Shown = false;      // эталон CHUD::weightWarn70Shown
	bool  bWeightWarn90Shown = false;      // эталон CHUD::weightWarn90Shown
	bool  bWeightBlinkOn = true;           // фаза мигания полосы 96
	double WeightBlinkNextS = -1.0;        // < 0 = таймер 25 не запущен (KillTimer)
	int32 LastLevelApplied = -1;
	int32 LastWeightCur = -2, LastWeightMax = -2;   // для повторного применения, когда данные доехали позже окна
	int32 LastClassApplied = -2;
	int32 WingId = -1;                     // 6585: id записи PlusPackageTable (g_WingDataTable)
	int32 WingRemainSec = 0;               // 6585: остаток (эталонный g_BuffRemainSeconds)
	// ★ Флаг «плюс-пакет активен» = байт `LocalPlayer[1].gap_A42[15140]` эталона (6585 rec+10).
	//   Именно по нему `charSitStatePoll@0xEE3250` (entity_char.c:49969-49979) КАЖДЫЕ 2000 мс
	//   показывает/прячет `g_pAutoStateControl` — а это, по живому дампу (VA 0x2C3BE14 → CControl
	//   c IDNum 5110 @+0x16C, скин «pluspac», idc «pluscpack»), ИМЕННО ИКОНКА PLUS PACKAGE.
	//   Прежний вывод «кода показа в срезе нет» был ОШИБКОЙ: контрол адресуется через глобаль,
	//   а не через поле CHUD (закон dead-code-markings-suspect).
	bool  bWingActive = false;
	// Двойной клик: эталон получает готовое событие 11 от менеджера окон; у нас мышь ловится опросом.
	//   ★ Аудит №3 (B3): прежние латчи `bLmbWasDown` + `bRmbWasDown` — один экземпляр общей точки
	//   (обе кнопки опрашивал `TickPointer`, там же и латч).
	FAsdaMouseFrame Mouse;
	// Двойной ЛКМ по зоне MiniState_Click (событие 11). ★ U1 (В2-A): общий `FAsdaDblClick` (было — `LastLmbDownS` игрового времени).
	FAsdaDblClick SelfDbl{ -1, -1, -1.0, 0.5 };   // `LegacySec` = прежний `DoubleClickSec` (для самотеста U1; окно передаётся явно)
	// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (решение владельца, ключ `asda.UI.SelfTargetSingleClick`): выбор себя ОДНИМ кликом по зоне 97.
	//   Срабатывает на ОТПУСКАНИИ ЛКМ, нажатой в зоне и не сдвинутой дальше порога (перетаскивание окна — не клик); второе
	//   нажатие пары двойного клика выбор не повторяет; нажатие на иконку эффекта / плюс-пак внутри зоны выбор не делает.
	bool bSelfPressArmed = false;     // ЛКМ нажата в зоне 97 (ждём отпускания)
	bool bSelfPressIsPair = false;    // это нажатие — второе в паре двойного клика (выбор уже сделан первым)
	FVector2D SelfPressPos = FVector2D::ZeroVector;   // точка нажатия (порог сдвига)
	// Курсор над своим контролом внутри зоны 97 (иконка эффекта, плюс-пак) — одиночный клик по нему не выбирает себя.
	bool IsCursorOverOwnControlInClickZone() const;
public:
	// ★ U1 (В2-A дедупа №2): КОПИЯ распознавателя двойного щелчка с прежним порогом окна (`LegacySec`) — для таблицы
	//   порогов самотеста `asda.Shop.SelfTestDblClick` (раздел U1). Своё состояние пары окна не трогает. `Which`: 0 — зона «двойной по себе», 1 — иконка баффа.
	FAsdaDblClick DebugDblClickProbe(int32 Which = 0) const;
private:
	// ★ U1 (В2-A): момент и окно для распознавателя: реальное время и порог Windows; при ключе отката `asda.UI.DblClickLegacy 1` —
	//   прежнее: игровое время мира и `DoubleClickSec`.
	void DblClickNowAndWindow(double& OutNow, double& OutWindow) const;
	bool   bTipShown = false;

	// ── ★ 06.10.2026: состояние доработок портрета ──
	bool    bPartyerOn = false;            // значок главы показан (модель; переживает пересоздание окна)
	bool    bPartyHooked = false;          // подписка на UAsdaPartySubsystem::OnPartyMasterChanged уже сделана
	bool    bClanMarkBaseShown = false;    // база 270 сейчас видима (зеркало CHUD+0x9B0)
	bool    bLastSelfFlagOn = false;       // флаг клана своего на момент прошлой обработки (для 6542: «был выкл»)
	FString LastNameApplied;               // имя, последним записанное в MINISTATE_CharName (5)
	bool    bDeathResetLogged = false;     // строка лога «смерть — слои обнулены» уже была в эту смерть
	// Очередь событий эмблемы (разбор в Tick): 1 = снимок клана своего, 2 = 6542 своего клана, 3 = погасить.
	TArray<uint8>   PendingClanMark;
	TArray<FString> PendingClanMarkWhy;    // причина каждого события (для лога)
	// След нотисов портрета (loc-id в порядке отправки) — читает самотест: «что отправил обработчик».
	TArray<int32> NoticeTrace;

	// ── ★ 06.10.2026: самотест — что вернуть после прогона ──
	int32 StSavedCharId = 0;               // Net->SelectedCharId до прогона
	int32 StSavedMapId = -1;               // LM->CurrentMapId до прогона
	int32 StSavedHideOnLeave = 1;          // asda.Mini.PartyerHideOnLeave до прогона
	int32 StSavedHideOnFlagOff = 1;        // asda.Mini.ClanMarkHideOnFlagOff до прогона
	int32 StSavedWeightCur = 0;            // Net->SelfWeightCur до прогона
	int32 StSavedWeightMax = 0;            // Net->SelfWeightMax до прогона
	int32 StOk = 0;                        // пройдено проверок (обе части)
	int32 StFail = 0;                      // провалено проверок (обе части)

public:
	// Интервал двойного клика. В оригинале его задаёт МЕНЕДЖЕР ОКОН (событие 11 приходит готовым),
	//   числа в срезе нет — берём системный порог Windows (GetDoubleClickTime по умолчанию 500 мс)
	//   и оставляем редактируемым. ЭТО НАШ ПАРАМЕТР, а не значение эталона.
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	float DoubleClickSec = 0.5f;

	// Период мигания полосы перегруза ≥ 90 % — эталон SetTimer(hWnd, 25, 250 мс) (ui_core.c:75216).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	float OverloadBlinkSec = 0.25f;

	// ★ Кегль текста тултипа Plus Package. В .JUI размер шрифта per-control НЕ хранится — он
	//   глобальный: импортёр ставит всему UI `FONT_PX = 8` (Tools/asda_jui_generic_umg.py:37).
	//   Базовая строка `DT_AsdaRichStyle` = 14 px, поэтому кегль задаётся тегом `<FS08>` — ровно
	//   как это делает лента чата (`UAsdaChatSubsystem::PushLine`).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	int32 TipFontPx = 8;

	// ★★★ КОНСТАНТЫ ВЫСОТЫ ПАНЕЛИ ТУЛТИПА. Эталон `tutorialPopupLayoutLarge@0xEB6EE0`
	//   (item_inventory.c:62085-62086):
	//       filler[176] = filler[268]*(строк+1) + filler[264]*(строк+2);
	//       SetSize(filler[172] /*ширина*/, filler[176] + 52);
	//   Значений `filler[264]`/`filler[268]` в декомпиле нет — сняты с ЖИВОГО ДАМПА
	//   (`live.dmp`, объект `g_ItemTooltip` @0x2C433B0): **filler[264] = 12**, **filler[268] = 3**
	//   (там же filler[172] = 250, filler[176] = 50 — остатки прошлого вызова).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	int32 TipLineHeight = 12;     // filler[264]
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	int32 TipLineGap = 3;         // filler[268]
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	int32 TipChromeH = 52;        // эталонная добавка «+52» к высоте содержимого

	// ★ ОТСТУПЛЕНИЕ (решение юзера 2026-08-07): по умолчанию высота панели считается ПО ФАКТУ
	//   содержимого (верх тела + строки × измеренная высота строки + низ), а не эталонной формулой:
	//   константы 3/12 сняты с дампа верно, но рассчитаны на метрики шрифта ОРИГИНАЛА (D3D), а не UE,
	//   и при L=7 давали 184 px против ~112 фактических — снизу оставалась пустая полоса.
	//   `asda.Mini.TipLegacyHeight 1` возвращает эталонную формулу.
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	bool bTipLegacyHeight = false;

	// Отступы тела внутри панели (из .JUI: ITEM_TIPTITLE y=2 h=18 ⇒ верх тела 20; низ 8; бока 8).
	//   Боковой отступ задаёт и ШИРИНУ ПЕРЕНОСА: BodyW = PanelW − 2*kTipBodyInsetX.
	static constexpr float kTipBodyTop = 20.f;
	static constexpr float kTipBodyBottom = 8.f;
	static constexpr float kTipBodyInsetX = 8.f;

	// ═══════════════ ЭТАП 2: параметры иконок эффектов (все правятся в редакторе) ════════════════
	// Сдвиг ВТОРОГО РЯДА иконок вниз — эталон HUD_InitControls (ui_core.c:74724-74728):
	//   при `iconIdx >= 14` контролу ставится Y + 9.
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	float BuffRow2OffsetY = 9.f;
	// Локальная Y-координата строки таймера внутри слота — эталон CControl_SetField74_75(ctrl, x, 24).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	float BuffTimerY = 24.f;
	// Атласы цифр таймера 9×9 (эталон: sprintf(texName, ...) в Skill_DrawBuffTimerIcon).
	//   Порядок глифов: 0..9 = цифры, 10='d', 11='h', 12='m', 13='s', 14=':' (u = 9*index, v = 0).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	FString BuffTimeTexNormal = TEXT("bufftime");        // бафф (skillRecord->buff0SubType < 2)
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	FString BuffTimeTexDebuff = TEXT("RedBufftime");     // дебафф (buff0SubType >= 2)
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	FString BuffTimeTexItem   = TEXT("GreenBufftime");   // предметный эффект (type == 2)
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	FString BuffTimeBackTex   = TEXT("bufftimeback");    // подложка 24×10 под цифрами
	// Цвет числа стека (эталон: sprintf("<FC=%x00FFFF>",255) и "<FC=%xFFFF00>",255 → sRGB).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	FColor BuffStackColorBuff = FColor(0x00, 0xFF, 0xFF, 0xFF);    // buff0SubType == 1 — голубой
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	FColor BuffStackColorDebuff = FColor(0xFF, 0xFF, 0x00, 0xFF);  // buff0SubType >= 2 — жёлтый
	// Размер ячейки атласа иконок и число колонок — эталон gridIndexToXY_10col (24 px, 10 колонок).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	int32 BuffIconCellPx = 24;
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	int32 BuffIconCols = 10;
	// ★★★ ВКЛЮЧЕНО 2026-08-24 по решению владельца («по истечении действия зелья/навыка полоса
	//   должна исчезать»). Эталонный хвост `HUD_PlayerBuffIcons_Update` (`ui_core.c:76441-76443`)
	//   обнуляет второе значение HP/MP, если состояния «зелье» (52/265) или «свиток» (266) в
	//   списке нет вовсе — именно это и гасит слой MINISTATE_HPBuff/MPBuff, когда действие кончилось.
	//   Раньше стояло false из опасения, что обнуление гасит полосу; опасение было следствием
	//   неверной формулы слоя (он показывал текущее здоровье вместо второго значения).
	//   Ключ возврата прежнего поведения — снять этот флаг.
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	bool bResetVitalSecondWithoutPotion = true;
	// ★ Отмена баффа кликом. Событие эталона (case 11) — то же, что у `MiniState_Click` (id 97),
	//   которое ЭТАП 1 трактует как ДВОЙНОЙ ЛКМ; держим трактовку единой. false = одиночный клик.
	UPROPERTY(EditAnywhere, Category="Asda|MiniState|Эффекты")
	bool bBuffCancelOnDoubleClick = true;

	// Множитель ширины «отстающего» слоя HP/MP — эталон 1.25 (ui_core.c:75455/75474).
	UPROPERTY(EditAnywhere, Category="Asda|MiniState")
	float GhostWidthScale = 1.25f;

	// z-order окна. AlwaysTop = 0 у всех контролов ⇒ обычная полоса; ставим рядом с EXP-баром (90),
	//   чуть выше, чтобы панель мини-статуса не перекрывалась постоянным слоем полосы опыта.
	static constexpr int32 kMiniZ = 95;
	static constexpr int32 kTipZ  = 9100;   // тултип выше всплывашек (kTopmostZ 9000), ниже модалок
};
