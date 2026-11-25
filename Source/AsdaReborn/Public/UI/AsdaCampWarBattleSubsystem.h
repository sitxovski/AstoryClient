// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCampWarBattleSubsystem — «БИТВА АРМИЙ» (CampWar / RoseWar / BattleField), БОЕВАЯ ФАЗА на карте 19 (этап «в» решения
//   владельца 30.10.2026 «вся система CampWar целиком»). Тот же объект оригинала `CCampWar` (`g_pCampWarClan = 0x2BFE5C0`), что и у
//   регистрации: общая модель и `CCampWar_ResetState` — в `UAsdaCampWarSubsystem`, здесь — боевые поля и окна HUD боя.
//   Разбор: `Reports/faction_select/BATTLE_SPEC.md` (смещения — по дизасму; имена полей декомпила врут), поток — `FLOW.md` §8,
//   узлы ассетов — `campwar_wbp_tree.txt`.
//
// ═════════════════ ★★★ ПОТОК БОЯ (BATTLE_SPEC §1) ═══════════════════════════════════════════════
//   5055 код 1 → `campWarWnd_ToggleVisible(1, вход на кампварную карту)` (НЕ 6739 — тот только у оператора) → HUD:
//     49600 очки армий (6 цифр × 3), 49900 карта боя 200×200 (+ слой рисунка миникарты, 10 таймеров 49950…50050, свёртка
//     49902/49903), 49940 «Осталось времени MM:SS» + 49500 «Рейд», 17520 «Выйти из боя» на миникарте, чат в режиме 9,
//     системное меню спрятано, сервис-кнопки чата спрятаны.
//   Приём (из сети — в очередь, разбор в Tick): 6716/6720/6721 список сквада, 6724 захват (полоса 49510), 6725/6726 точки,
//     6728 состояния (чат 10171…10177 / крупные надписи 49910), 6729 очки, 6730 «+N POINT», 6732/6733 итог (окно 49800),
//     6735 капитан, 6736 таймер боя, 6737 сброс, 6739 оператор, 6741 контрол HUD, 6742 численность; 6715 (сквад) — тоже
//     здесь (одна очередь с 6716/6721 — порядок пакетов). 6738 — у регистрации.
//   Отправка (`UAsdaNetSubsystem::SendCampWar`): 6717 список сквада, 6731 выход, 6734 капитан, 6186 исключить; 6723 — `SendCampWarJoin6723`.
//
// ═════════════════ ДЕФЕКТЫ ОРИГИНАЛА (BATTLE_SPEC, [ДЕФЕКТ ОРИГИНАЛА]) ════════════════════════════
//   Эффекты `camp_*` (8 BP_Fx_camp_*) собраны каналом `Tools/asda_build_char_fx.py`; тон объекта точки — векторный параметр
//     `ObjTint` ОТДЕЛЬНОГО мастера `M_AsdaObjectLit_CampTint` (`Tools/asda_campwar_point_tint_mat.py`; 31.10: прежняя ветка на CPD
//     в общем `M_AsdaObjectLit` обнулила текстуры всех объектов — непрописанные CPD движок читает как 0).
//   Д-Б1 6725/6726/6723 без проверки слота пишут по индексу −1 (или ≥ 10) — у порта запись мимо массива невозможна: пакет
//     пропускается с журналом ([ПОРТ], ключа нет — повторять порчу памяти нечем).
//   Д-Б2 тик: конец захвата (≥ 10000 мс) делает `return` — остальные точки и очередь 6722 в этом кадре пропускаются —
//     [ИСПРАВЛЕНИЕ] `asda.CampWar.TickNoBreak` (1 — продолжить цикл).
//   Д-Б3 таймер точки, 3 цифры: третья цифра пишется снова в глиф [2] — [ИСПРАВЛЕНИЕ] `asda.CampWar.TimerThirdDigit` (1 — в [3]).
//   Д-Б4 49410 «Пригласить»: (а) цель уже в отряде — в чат пустая строка; (б) имя берётся по индексу СТРОКИ, а не члена —
//     [ИСПРАВЛЕНИЕ] `asda.CampWar.InviteFix` (1 — (а) нотис 1047 «уже в отряде», (б) имя найденного члена).
//   Д-Б5 6726 повтор на тот же номер раздувает число слотов +0x23D0 — [ИСПРАВЛЕНИЕ] `asda.CampWar.SlotCountFix` (1 — счётчик =
//     max(номер+1)).
//   Д-Б6 6742: строка BaseTable `a2 == count` пропускает проверку — у порта строки читаются только существующие (DataTable).
//   Д-Б7 тик, шаг 2 (захват погашен): 49510 прячется БЕЗУСЛОВНО — конец чужого захвата гасит и мою полосу —
//     [ИСПРАВЛЕНИЕ] `asda.CampWar.HoldBarKeep` (1 — прятать только свою).
//   Д-Б8 6721/6735: имя кладётся в слот 0 (`[ss1]`), а строки 10071/10072 ждут `[ss2]`/`[ss3]` — имени в нотисе нет —
//     [ИСПРАВЛЕНИЕ] `asda.CampWar.NoticeNames` (1 — имя во всех слотах строки).
//   Оставлены 1:1 (без ключа, наблюдаемой разницы нет или разница — смысл оригинала): 6738 `$d` в 10111, двойной шаг 48099 при
//     идущем таймере боя (у регистрации), тултипы 49811/49814 (см. FLOW §8).
//   Общий ключ этапа `asda.CampWar.Battle` (1 — боевая фаза по оригиналу; 0 — прежний порт: HUD не включается, пакеты боя
//     глотаются, 17520 по флагу карты и клик только пишет лог).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaNetQueue.h"              // TAsdaNetQueue — очередь тел 67xx (закон no-reentrant-net-callbacks)
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Styling/SlateBrush.h"        // FSlateBrush — исходные кисти узлов-картинок
#include "AsdaCampWarBattleSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UButton;
class UAudioComponent;
class UTexture2D;
class UMaterialInterface;
class AActor;
class AAsdaNpcActor;
class UAsdaButtonRelay;
class SAsdaCampWarMapLayer;

// Одно окно JUI боевой фазы: виджет + разобранные контролы по IDNum (ключ — IDNum эталона; для узлов без своего IDNum —
//   производный ключ, см. .cpp) + модельная видимость корня.
USTRUCT()
struct FAsdaCwWindow
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;          // корень WBP
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UWidget>> Ctl;              // IDNum → узел
	TMap<int32, FVector2D> Base;                                            // IDNum → исходная позиция слота (до сдвигов кодом)
	bool bReady = false;                                                    // контролы разобраны
	bool bShown = false;                                                    // корень окна видим (модель `CControl_SetVisible`)
};

// Слот точки (BATTLE_SPEC §2, база `0x242C + 64·s`).
struct FAsdaCwPointSlot
{
	int32 Id = -1;               // +0x00 номер точки (= ключ сущности `entity+0x1CC`)
	int32 State = 0;             // +0x04 владение: 0 нейтр., 1 «временно», 2 «захвачена»
	int32 Anim = 0;              // +0x08 шаг анимации захвата: 3 старт / 1 идёт / 2 гасить / 0 покой
	int32 Owner = -1;            // +0x0C армия-владелец
	int32 Capturer = -1;         // +0x10 армия захватчика
	TWeakObjectPtr<AActor> Light;   // +0x14 эффект «свет точки» (хэндл)
	int32 LightKind = -1;        // +0x18 какой свет создан (0 / 1 / 2; −1 при сбросе)
	bool bSent6723 = false;      // +0x1C «6723 отправлен» (ставится; проверок в срезе нет)
	uint32 CaptureMs = 0;        // +0x20 прошло мс захвата
	int32 Raw24 = 0;             // +0x24 бонус удержания [9..10] (читателей в срезе нет)
	int32 TimerMs = 0;           // +0x28 мс таймера над точкой (тик += dt)
	FVector Color = FVector::ZeroVector;   // +0x2C текущий цвет объекта
	int32 CellX = 0, CellY = 0;  // +0x38/+0x3C клетка из 6726 (читателей в срезе нет)
};

// Член сквада (BATTLE_SPEC §4 «6715», запись 118 б `+0xBC4 + 118·m`). Сырая запись хранится целиком (6720 пишет в её середину).
struct FAsdaCwMember
{
	uint8 Raw[118] = {};         // запись 1:1
	bool Used() const { return Raw[0] == 1; }                         // +0 занят
	int32 UserNum() const;       // +1 i16 (эталон `g_PartyInviteTargetClanId` — номер персонажа для приглашения)
	int32 CharId() const;        // +3 i32
	int32 Captain() const { return Raw[8]; }                          // +8 0 = капитан / 3
	int32 Grade() const { return (int8)Raw[9]; }                      // +9 звание
	int32 Level() const;         // +10 i16
	int32 Job1() const { return Raw[12]; }                            // +12 профессия
	int32 Job2() const { return Raw[13]; }                            // +13 вторая профессия
	bool HasFlag() const { return (int8)Raw[14] > 0; }                // +14 есть флаг клана
	int32 Squad() const;         // +55 i16 отряд (< 0 — нет)
	FString Name() const;        // +57 char[20]
	FString Leader() const;      // +77 char[20]
	FString Clan() const;        // +97 char[20]
};

UCLASS()
class ASDAREBORN_API UAsdaCampWarBattleSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на `OnCampWarSquadPacket`, ESC окон 49399/49800.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка, снятие виджетов и слоя карты.
	virtual void Deinitialize() override;
	// Сброс сессии: `campWarWndClose` (Toggle(0) + ResetState + позиции); смена карты не сбрасывает (объект статический).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Очередь пакетов, кадровый тик `LightFx_UpdateAll`, окна, опрос мыши.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCampWarBattleSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	static constexpr int32 kSlots    = 10;    // точек (слотов) у оригинала ровно десять
	static constexpr int32 kArmies   = 3;     // армий три
	static constexpr int32 kMembers  = 50;    // записей сквада
	static constexpr int32 kRows     = 15;    // видимых строк списка «Рейд»
	static constexpr float kCaptureMs = 10000.f;   // `0x461C4000` — полоса захвата 0…10000 мс

	// Ключ этапа `asda.CampWar.Battle` (аксессор для других .cpp — имя ключа в рабочий код не вписывать).
	static bool IsBattleOn();

	// ═══ ВХОДЫ ОТ ДРУГИХ СИСТЕМ ═══
	// Порт `campWarWnd_ToggleVisible@0xCA1690` (BATTLE_SPEC §3.1). bLeaveParty — a3 (выйти из отряда пакетом 5098).
	void ToggleVisible(bool bShow, bool bLeaveParty);
	// Боевая часть `CCampWar_ResetState@0xC96F20` (зовёт `UAsdaCampWarSubsystem::ResetState`): очки, слоты, число точек,
	//   таймер боя, флаг «ждём список сквада».
	void OnResetState();
	// `uiDlg_ResetChildPositions@0xCA2110` — позиции 49399 и 49800 к умолчанию (47000/48099/46000 — их окна).
	void ResetChildPositions();
	// `charEffectSlots_StopAll` из `Clan_Send_6714` (47601): эффекты 10 точек погашены, слоты очищены, число точек 0.
	void StopAllPointEffects();
	// 6715 `Recv_ClanMemberListBulk@0xCA4B50` (зовёт регистрация из своего разбора — одна точка). true — своя запись найдена.
	bool ApplyMemberList6715(const TArray<uint8>& B);
	// Блок таймера боя `ClanTimer_Tick(1)` (зовёт регистрация из своего `ClanTimerTick(1)` перед блоком 48099).
	void TickBattleTimer();
	// Клик по объекту-точке на карте 19 в радиусе разговора — `netSendCampWarjoin@0xCA5750` (6723, флаг слота +0x1C).
	void RequestCapture(AAsdaNpcActor* Target);
	// ★ 31.10.2026: чат армии (режим ввода 9) — `Clan_Send_SetNotice@0xCA3010` → 6718 в СЕРВЕРНОЙ раскладке (108 б). false — не ушло.
	bool SendArmyChat6718(const FString& Text);
	// 17520 «Выйти из боя» (`MiniMap_Window_OnCommand` case 0x4470): живой → TWOBUT 10047 → «Да» 6731.
	void OnOutButton();
	// 4055 (воскрешение со сменой карты) / 6013 (призыв): если HUD включён — Toggle(0,1) + ResetState + ResetPositions.
	//   Зовёт сетевой слой из колбэка — разбор отложен в Tick.
	void OnLeaveByServerMove(const TCHAR* Why);

	// ═══ МОДЕЛЬ (читают миникарта, самотест) ═══
	bool IsHudOn() const { return bHudOn; }                                                     // +0x2388
	int32 GetScore(int32 F) const { return (F >= 0 && F < kArmies) ? Score[F] : 0; }           // +0xB9C+4f
	int32 GetHeadcount(int32 F) const { return (F >= 0 && F < kArmies) ? Headcount[F] : 0; }   // +0xBA8+2f
	int32 GetSlotCount() const { return SlotCount; }                                             // +0x23D0
	const FAsdaCwPointSlot& GetSlot(int32 S) const { return Slots[FMath::Clamp(S, 0, kSlots - 1)]; }   // +0x242C+64s
	int32 GetMySlot() const { return MySlot; }                                                   // +0x23D4
	int32 GetTimerMin() const { return TimerMin; }                                               // +0x232C
	int32 GetTimerSec() const { return TimerSec; }                                               // +0x2330
	int32 GetMyIndex() const { return MyIndex; }                                                 // +0xB7E
	int32 GetSelfArmy() const;                                                                    // +0xB7C (армия)
	const FAsdaCwMember& GetMember(int32 M) const { return Members[FMath::Clamp(M, 0, kMembers - 1)]; }   // +0xBC4+118m
	int32 GetListTop() const { return ListTop; }                                                 // +0x2314
	int32 GetListSel() const { return ListSel; }                                                 // +0x2318
	bool IsWaitingList() const { return bWaitList; }                                             // +0x235A
	int32 GetHudFlag6741() const { return Hud6741; }                                             // `g_pHUD+0x9B4` (последний 6741)
	FVector GetObjTint(int32 S) const { return ObjTint[FMath::Clamp(S, 0, kSlots - 1)]; }        // тон объекта точки
	AActor* GetHandFx(int32 CharId) const { return HandFx.FindRef(CharId).Get(); }                // `Camp_hand_01` захватчика (char+0x33080)
	AActor* GetStartFx() const { return StartFx.Get(); }                                          // `Camp_start` (+0x23E0)
	AAsdaNpcActor* DebugFindPointActor(int32 Id) const { return FindPointActor(Id); }             // объект точки по номеру (самотест)
	// Тон на меше объекта точки: `ObjTint` динамических инстансов мастера тона на всех слотах (самотест). false — слотов с ним нет.
	bool DebugGetMeshTint(int32 Id, FVector& Out, int32* OutSlots = nullptr) const;
	// Точка для слоя карты: позиция значка (BATTLE_SPEC §3.3 — `g_pMiniMap+0x360`).
	static FVector2D PointMapPos(int32 S);

	// ═══ ДИАГНОСТИКА / САМОТЕСТ ═══
	bool IsWinShown(int32 IdNum) const;          // корень окна по IDNum корня (49600/49900/49910/49940/49510/49800/49399/49400)
	bool IsShown(int32 IdNum) const;             // узел видим (по IDNum или производному ключу)
	FString GetText(int32 IdNum) const;          // текст узла
	UWidget* GetNode(int32 IdNum) const;         // узел (для кликов самотеста)
	float GetBarValue(int32 IdNum) const;        // значение полосы (49521 — мс захвата; 49811/49814+10i — проценты)
	FVector2D GetNodePos(int32 IdNum) const;     // позиция слота узла (в канвасе родителя)
	float GetNodeAlpha(int32 IdNum) const;       // альфа рендера узла (затухание 49910)
	int32 GetVisibleDigit(int32 F, int32 J) const;   // какая цифра 0…9 показана в разряде J армии F окна 49600 (−1 — ни одна)
	bool IsMapLayerDrawn() const;                // слой рисунка карты 49900 рисуется (гейт HUD + 49904)
	int32 GetMapIconCell(int32 S, FVector2D& OutUV) const;   // ячейка значка точки S в атласе (−1 — не рисуется)
	const TArray<int32>& GetNoticeTrace() const { return NoticeTrace; }   // loc-id показанных нотисов/строк
	FString GetLastNoticeText() const { return LastNoticeText; }          // текст последнего нотиса
	void ClearTrace() { NoticeTrace.Reset(); LastNoticeText.Reset(); }    // сбросить след
	int32 GetFoundControls() const { return FoundControls; }   // разобрано контролов
	int32 GetTotalControls() const { return TotalControls; }   // ожидалось
	FString DumpControls() const;                // сводка окон
	void DebugAdvanceTick(float Ms);             // самотест: прогнать кадровый тик на Ms мс (без ожидания реального времени)

private:
	friend class SAsdaCampWarMapLayer;
	// ── Окна ──
	FAsdaCwWindow* Win(int32 RootId);                     // окно по IDNum корня
	const FAsdaCwWindow* Win(int32 RootId) const;
	bool EnsureWin(int32 RootId);                          // каркас окна (`FAsdaJuiWindowHandle::Ensure`) + разбор контролов
	void SetupWin(int32 RootId, FAsdaCwWindow& W);         // разбор контролов окна и подписи из init эталона
	void ShowWin(int32 RootId, bool bShow, bool bTop = false);   // корень окна видим/скрыт (модель + вьюпорт)
	void SyncWindows();                                    // окна, которые модель держит видимыми, но виджет умер (травел), — пересоздать
	void RedrawWin(int32 RootId);                          // перерисовать окно целиком из модели (после пересоздания)
	UWidget* Node(int32 RootId, int32 Key) const;          // узел окна
	bool VisModel(int32 Key, bool bDefault) const;         // модельная видимость узла (нет записи — bDefault)
	void SetVisNode(int32 RootId, int32 Key, bool bVis);   // `CControl_SetVisible` узла
	void SetTextNode(int32 RootId, int32 Key, const FString& S);   // `CControl_SetText`
	void PlaceNode(int32 RootId, int32 Key, int32 X, int32 Y);     // `CControl_SetField74_75` (−1 — координата не меняется)
	void DrawTexRect(UImage* Img, const TCHAR* Tex, int32 X, int32 Y, int32 W, int32 H);   // `CControl_DrawTextureRect`
	void ResetWidgets();                                   // снять виджеты всех окон
	void RegisterZones();                                  // зоны окон (клики не уходят в мир)
	TArray<FName> ZoneNames;                               // ★ 31.10: имена зон узлов табло/карты (снимаются в ResetWidgets)
	// ── Окна: содержимое ──
	void ShowPointPanel();                                 // `uiWidgetShowPanel@0xC9DE00` (49600 видно + цифры)
	void DrawDigits();                                     // `UI_SetNumericDigitDisplay@0xC9DE90`
	void DrawHeadcount();                                  // 6742: «%d/%d» в 49722/49724/49726
	void LayoutPointPanel();                               // сдвиг 49600 миникартой (развёрнута → X−128)
	void UpdateObjectTimers();                             // `CCampWar_UpdateObjectTimers@0xCA7AA0`
	void ShowModeControls(int32 Code, int32 N);            // `uiPanel_ShowControlsByMode@0xCA1D10` (49910)
	void TickFades(float DtMs);                            // `objStartTimedAction(…, 255, 0, 3000)` + событие 0x18
	void ShowEndWindow(const TArray<uint8>& B);            // `CItemSynthResultWin_Refresh@0xC9CF00` (окно итога 49800)
	void ResetEndWindow();                                 // `CItemSynthResultWin_Reset@0xC9DB40` (ничья)
	void OnEndExit();                                      // 49880 / 49881
	void PlaceHoldBar(int32 S);                            // позиция 49510 над объектом точки
	void ClearAttackRows();                                // `uiRefreshControls` строк списка
	void DrawMemberList();                                 // `CClan_DrawMemberList@0xC9C2E0`
	void CloseAttack();                                    // `uiSynthResultWinClose` (имя врёт): спрятать 49400 и 49399
	void SelectRow(int32 Row, bool bRight);                // `questTrackerSelectSlot@0xC9CC70` (имя врёт)
	void ClearRowSel();                                    // +0x2318 = −1, 49330 скрыт
	void ShowRowPopup();                                   // `uiSynthResultWinShow@0xC9CE30` (имя врёт): 49400 у курсора
	int32 MemberIndexOfSel() const;                        // член под выбранной строкой (счёт занятых от +0x2314)
	void OnPopupInvite();                                  // 49410 «Пригласить в отряд»
	void OnPopupCaptain();                                 // 49420 «Назначить капитана» → 6734
	void OnPopupKick();                                    // 49430 «Исключить» → окно 87 → 6186
	void OnRaidButton();                                   // 49500 «Рейд» → +0x235A = 1 + 6717
	void TickMouse();                                      // строки 49030+20r, колесо 49320, полосы 49811/49814
	// ── Кадровый тик (`LightFx_UpdateAll@0xCA5F90`) ──
	void TickPoints(float DtMs);
	AAsdaNpcActor* FindPointActor(int32 Id) const;         // `List_FindNodeByField460(g_pEntityList, id)`
	int32 FindSlotIndexById(int32 Id) const;               // `findSlotIndexById@0xCA76F0`
	FVector HalfColor(int32 F) const;                      // +0x23E4+24f
	FVector FullColor(int32 F) const;                      // +0x23F0+24f
	void ApplyObjectColor(int32 S, const FVector& C);      // `sub_9354E4` (тон объекта точки S)
	void StopLight(FAsdaCwPointSlot& Slot);                // `CEffectMgr_StopByOwner` по +0x14
	void ResetSlot(FAsdaCwPointSlot& Slot);                // `sub_C96060`
	// ── Сеть ──
	void HandlePacket(int32 Op, const TArray<uint8>& B);
	void On6716(const TArray<uint8>& B);   // `Recv_ClanDrawMemberList`
	void On6720(const TArray<uint8>& B);   // `clanMemberSetCellByPlayerId`
	void On6721(const TArray<uint8>& B);   // `Recv_ClanMemberUpdate`
	void On6724(const TArray<uint8>& B);   // `CCampWar_OnObjectEffectPacket@0xCA5910`
	void On6725(const TArray<uint8>& B);   // `QuickSlot_OnUpdatePacket@0xCA51B0` (имя врёт)
	void On6726(const TArray<uint8>& B);   // `campWarSetObjectiveSlot@0xCA6AE0`
	void On6728(const TArray<uint8>& B);   // `campWarHandleStateNotice@0xCA5490`
	void On6729(const TArray<uint8>& B);   // `uiPanelShowWith3@0xCA56F0` (имя врёт)
	void On6730(const TArray<uint8>& B);   // `floatingText_SpawnValueOverChar@0xCA7100`
	void On6732(const TArray<uint8>& B);   // `recvCampWarSynthResultRefresh`
	void On6733(const TArray<uint8>& B);   // `onRecvHonorAndInventoryUpdate@0xCA7040`
	void On6735(const TArray<uint8>& B);   // `clanMember_HandleResultCode@0xCA6E60`
	void On6736(const TArray<uint8>& B);   // `campWarUpdateCountdown@0xCA72D0`
	void On6739(const TArray<uint8>& B);   // `campWarRankingApply@0xCA7480` (имя врёт)
	void On6741(const TArray<uint8>& B);   // `uiToggleByFlagByte@0xCA7690`
	void On6742(const TArray<uint8>& B);   // `campWar_UpdateScoreLabels@0xCA6D00` (имя врёт)
	bool SendBody(uint16 Op, const TArray<uint8>& Body, const TCHAR* What);   // `Net::SendCampWar`
	void SendMemberList6717();             // `Clan_Send_ReqMemberList@0xCA33B0`
	void SendLeave6731();                  // `Clan_Send_Leave@0xCA37B0`
	// ── Прочее ──
	void ChatLine(int32 LocId, const FString& Text, uint32 Argb, float LifeMs, bool bTrackB);   // `CChat_AddTokenizedLines`
	void PlankLine(int32 LocId, const FString& Text, uint32 Argb, float LifeMs);                // `CControl_SetTooltipText(g_pChatWnd…)`
	void MsgBox(int32 LocId, const FString& Text, int32 EtalonId);                              // ONEBUT `CItem_BuildTooltipText(…, 0, id, …)`
	void Confirm(int32 LocId, const FString& Text, int32 EtalonId, TFunction<void()> OnYes);     // TWOBUT `CItem_BuildTooltipText(…, 1, id, …)`
	FString Loc(int32 LocId) const;        // loc-строка (теги срезаны)
	FString LocSd1(int32 LocId, int32 Sd1) const;   // [sd1]
	FString LocSs1(int32 LocId, const FString& Ss1) const;   // [ss1]
	FString LocSsN(int32 LocId, int32 N, const FString& S) const;   // [ssN]
	void PlaySound3D(const TCHAR* Name, const FVector& At, bool bLoop, TObjectPtr<UAudioComponent>* OutLoop);   // `Audio_Play3DSound`
	AActor* SpawnFx(const TCHAR* Jeb, const FVector& At, AActor* Carrier);   // `Effect_SpawnByName` / `CEffectMgr_CreateEffect`
	void EnsureMapLayer();                 // слой рисунка карты 49900 (порт ветки кампвара `uiMiniMapRender@0xB4EFE0`)
	void DropMapLayer();                   // снять слой

	// ═══ ОКНА ═══
	UPROPERTY(Transient) TMap<int32, FAsdaCwWindow> Windows;                 // корень IDNum → окно
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;       // реле кнопок (GC-якорь)
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> CaptureLoop = nullptr; // петля `S_c_get_motion.wav` захвата (+0x23DD)
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> MapTex;             // CampWar_Map1 / CampWar_Object / CampWar_Number1
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> TintMaster = nullptr; // `M_AsdaObjectLit_CampTint` (тон объекта точки), лениво
	TSharedPtr<SAsdaCampWarMapLayer> MapLayer;     // слой рисунка карты (Slate, во вьюпорте под окном 49900)
	int32 FoundControls = 0;                       // разобрано контролов
	int32 TotalControls = 0;                       // ожидалось
	TMap<int32, float> FadeMs;                     // узел 49910 → прошло мс затухания (`objStartTimedAction`)

	// ═══ МОДЕЛЬ — боевые поля `CCampWar` (BATTLE_SPEC §2, смещения — дизасм) ═══
	bool bHudOn = false;                 // +0x2388 HUD боя включён
	int32 MyIndex = -1;                  // +0xB7E мой индекс в скваде / слот оператора
	bool bOperator = false;              // 6739: запись +0xB7C сброшена (армия −1) — оператор/наблюдатель
	int32 Score[3] = { 0, 0, 0 };        // +0xB9C+4f очки армий (6729)
	int32 Headcount[3] = { 0, 0, 0 };    // +0xBA8+2f численность армий (6742)
	FAsdaCwMember Members[kMembers];     // +0xBC4+118m сквад (6715)
	int32 ListTop = 0;                   // +0x2314 первая видимая запись списка
	int32 ListSel = -1;                  // +0x2318 выбранная строка
	int32 RowMember[kRows];              // +0x2B4+52r [11] — индекс члена в строке r (−1 — пусто)
	int32 RowCharId[kRows];              // +0x2B4+52r [12] — charId строки r
	int32 TimerMin = 0, TimerSec = 0;    // +0x232C/+0x2330 таймер боя (флаг +0x2334 — у регистрации)
	bool bWaitList = false;              // +0x235A ждём список сквада для окна «Рейд»
	int32 SlotCount = 0;                 // +0x23D0 число точек
	int32 MySlot = -1;                   // +0x23D4 слот, который захватываю я
	float HoldBarMs = 0.f;               // значение полосы 49521 (мс захвата моей точки, диапазон 0…10000)
	TWeakObjectPtr<AActor> StartFx;      // +0x23E0 эффект `Camp_start.jeb`
	FAsdaCwPointSlot Slots[kSlots];      // +0x242C+64s
	FVector ObjTint[kSlots];             // последний тон, отданный объекту точки (`sub_9354E4`; на меш — `ObjTint` мастера тона)
	TMap<int32, TWeakObjectPtr<AActor>> HandFx;   // charId → эффект `Camp_hand_01.jeb` (`char+0x33080`)
	int32 Hud6741 = -1;                  // `g_pHUD+0x9B4` видимость (6741)
	int32 ChatSavedState = -1;           // `CChat+0x1A8` — режим чата до включения HUD
	TMap<int32, bool> NodeVis;           // модельная видимость узлов (`CControl_SetVisible`), переживает пересоздание окон
	TMap<int32, FString> NodeText;       // модельный текст узлов (`CControl_SetText`), переживает пересоздание окон
	TMap<int32, FSlateBrush> OrigBrush;  // исходная кисть узлов-картинок из WBP (`uiRefreshControls` — «пустая» иконка)
	float PointShift = 0.f;              // применённый сдвиг 49600 по X (миникарта развёрнута → −128)
	TMap<int32, FString> TimerSig;       // подпись последнего нарисованного таймера точки (перерисовка только при смене)
	TArray<uint8> LastEnd;               // последнее тело 6732 (перерисовка окна итога после пересоздания)
	bool bEndDraw = false;               // окно итога: false — `Reset` (ничья), true — `Refresh(LastEnd)`
	int32 HoverRow = -1;                 // строка списка под курсором (событие 8)
	int32 HoverBar = -1;                 // полоса итога под курсором (−1 — нет), ключ IDNum
	bool bPendingServerMove = false;     // 4055/6013 пришёл (разбор в Tick)
	FAsdaMouseFrame Mouse;               // опрос мыши кадра
	double LastTickS = 0.0;              // время прошлого кадрового тика (мс → `dt`)
	TAsdaNetQueue<TPair<int32, TArray<uint8>>> Pending;   // тела боевых пакетов (разбор — в Tick)
	FDelegateHandle HPacket;             // подписка на `OnCampWarSquadPacket`
	TArray<int32> NoticeTrace;           // след нотисов
	FString LastNoticeText;              // текст последнего нотиса
};
