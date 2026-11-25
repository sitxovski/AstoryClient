// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaTownWarSubsystem — ОКНО ГОРОДА «Установить налог» `Town_as_WIN` (JUI **79300**) войны фракций за города.
//   Порт 06.10.2026, волна 2-Г (решение владельца «внимательно, надёжно»). Полный реверс, таблица нотисов, структурный
//   разбор и итог — `Reports/owner_1006/wave2D_gift_townwar.md` §2; дамп ассета — `Reports/owner_1006/w2d/wbp_tree.txt`.
//
// ═════════════════ ★★★ ЧТО ЭТО ЗА ОКНО В ОРИГИНАЛЕ ═════════════════════════════════════════════
//   Поля окна живут в объекте сообщества `g_pCommunityWnd` (слоты dword 549…561, +2248 «открыто», +2252 позиция полосы,
//   +2256 момент последнего «Нажми»). Контролы привязывает `uiCampWarPanelBindControls@0xBCDDA0` — его зовёт
//   `CmtyClan_Intro_BuildUI@0xBBCDF0` при сборке окна клана (★ это ТОЛЬКО привязка; ОТКРЫВАЕТ окно не клан, а NPC).
//   ★ Вход у игрока ОДИН: кнопка NPC с кодом **30076** (`CGameUI_HandleControlCommand`, game_main.c:10300-10302) —
//   `CUniconWar_UpdateFactionInfoPanel@0xBCE060`, затем `UI_resetAllWindowsAndLeave_5045`. Кнопка есть у трёх NPC —
//   133npc (карта 7, Пламения), 179npc (карта 0, Силарис), 180npc (карта 3, Альпия) (`NpcNsrButton.json`, `NpcQuestNpc.csv`).
//   Второй метод (скан байтов образа 13 344 768 б): вызов 0xBCE060 — ровно один (0xD8839E), закрытие 0xBCE470 — ровно
//   один (из обработчика кнопок 0xBC23E3), «Нажми» 0xBCE4B0 — ровно один (0xBC23F4). В «закрыть все окна» и ESC окна НЕТ.
//
// ═════════════════ ПОТОК (все ветки) ════════════════════════════════════════════════════════════
//   ОТКРЫТИЕ (`CUniconWar_UpdateFactionInfoPanel`): слот города по карте (3 → 0 Альпия/603, 0 → 1 Силарис/600,
//     7 → 2 Пламения/707; иначе слот 0 и заголовок loc 0 — недостижимо: все три NPC стоят в своих городах).
//     • город не занят (фракция 3) → ONEBUT 111913 «В нейтральном режиме.», окно НЕ открывается;
//     • иначе: +2248 = 1, окно видно; 79311 «+ N%», 79313 «- N%», 79315 «N%» (N — налог из 6743), полоса 79314 = N
//       (`uiScrollbarSetPos` → событие 69638 → тот же «N%»); тело 79303 = 111909 + 111910 + 111911; знак лагеря-хозяина
//       79302 — ячейка 64×64 `CampWar_MarkImage` (`uiMakeAxisVec2`: 0 → (0,0), 1 → (64,0), 2 → (0,64)).
//   ПОЛОСА 79314 (`Nomal_Scr_hori`, 218×11, диапазон 0…25 — второй `uiScrollbarSetRange` перекрывает 0…50; шаги по
//     умолчанию `CScrollBar_ctor`: стрелка 6, «страница» 50): ← / → (события 69633/69634) и перетаскивание бегунка
//     (69637) меняют позицию и «N%» в 79315 (`settings_DispatchFlagById`, a2 == 79314). Клик по жёлобу у ГОРИЗОНТАЛЬНОЙ
//     полосы оригинала не работает (дефект Д-ТВ-2, ключ ниже).
//   «Нажми» 79321 (`uiCampWarPanelTick@0xBCE4B0`): не чаще раза в 2000 мс (+2256); я — глава клана-хозяина (запись
//     6743 +25 charId == мой charId && +29 == мой индекс персонажа) → TWOBUT (1, 86) «111921 %d» (кнопки 108510/108511);
//     иначе → ONEBUT 111900 (фракция, имя главы). «Да» TWOBUT 86 → 6744 (charId, позиция полосы НА МОМЕНТ «Да»).
//   ЗАКРЫТИЕ: X 79329 и «Быстрее/Отмена» 79322 → `sub_BCE470`: +2248 = 0, окно скрыто.
//   ОТВЕТ 6745 (2 б: код, ставка): 1 → ONEBUT 111912 «…%d%%…»; 3 → ONEBUT 111919; иное → ONEBUT «%d» КОДА (дефект Д-ТВ-3).
//     Коды сервера (`User_Game_TaxChange@0x46B230`): 0 — ставка ≥ 26 или карта не город, 2 — не глава, 4 — фракция
//     игрока не хозяин; 1/3 — от чат-сервера (`sub_46B570`). Ставка вступает в силу в 00:00 — 6743 не приходит сразу.
//   ★ Нумпад окно НЕ зовёт (количества нет — полоса). Гейтов «души»/одержимости у 6744 на сервере НЕТ (диспетчер
//     `case 0x1A58` без `sub_52EEA0`, в обработчике нет `sub_52EE40`).
//
// ═════════════════ ДЕФЕКТЫ ОРИГИНАЛА (исправлены с ключами, [ПОРТ]) ════════════════════════════════
//   Д-ТВ-1 (КРИТ): RU-строка 111909 «Ставка налога: %d%%. Клан-лидер города %s - %s.» переставила порядок полей
//     корейского оригинала, а код подаёт (город, клан, процент): `%d` печатает адрес строки города, второй `%s` читает
//     ПРОЦЕНТ как указатель — при ставке ≠ 0 клиент RU падает при открытии окна (при 0 CRT MSVC печатает «(null)»). [ПОРТ] подстановка по смыслу: %d ← процент, %s ← город, %s ← клан. `asda.TownWar.FixTextArgs`.
//   Д-ТВ-2: клик по жёлобу горизонтальной полосы сравнивает Y курсора с Y бегунка (`uiScrollbarOnButtonEvent` case 0) —
//     у горизонтальной полосы они всегда совпадают, «страница» не листается никогда. [ПОРТ] сравнение по X.
//     `asda.TownWar.FixTrackPage`.
//   Д-ТВ-3: на коды 0/2/4 ответа 6745 оригинал показывает голую цифру кода. [ПОРТ] коды 2 и 4 (не глава / не хозяин) →
//     та же строка 111900, что клиент печатает при своём отказе «Нажми». Код 0 — цифра, как оригинал (строки нет).
//     `asda.TownWar.FixRawCode`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "AsdaTownWarSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;

// ── Тексты окна. Все id проверены по `RawAssets/Text/GameText.csv`; каждый — из СВОЕГО обработчика. ──
namespace AsdaTownWarLoc
{
	constexpr int32 kTitle       = 111907;   // 79301 «Установить ставку налога» (`uiCampWarPanelBindControls`)
	constexpr int32 kTaxCaption  = 111908;   // 79310 «Ставка налога»
	constexpr int32 kDiscCaption = 111903;   // 79312 «Скидка»
	constexpr int32 kBtnOk       = 111904;   // 79321 «Установить ставку» (`CWidget_SetTextAll4`)
	constexpr int32 kBtnClose    = 5035;     // 79322 «Закрыть»
	constexpr int32 kNeutral     = 111913;   // ONEBUT: город не занят (открытие)
	constexpr int32 kBody1       = 111909;   // 79303 часть 1: ставка, город, клан (Д-ТВ-1)
	constexpr int32 kBody2       = 111910;   // 79303 часть 2: фракция, глава
	constexpr int32 kBody3       = 111911;   // 79303 часть 3: «Новая ставка начнет действовать в 00:00.»
	constexpr int32 kNotMaster   = 111900;   // ONEBUT: «Нажми» не главой (фракция, глава)
	constexpr int32 kConfirm     = 111921;   // TWOBUT (1, 86): «Установить ставку %d%%?…»
	constexpr int32 kConfirmOk   = 108510;   // TWOBUT 86 «OK» (`CItem_BuildTooltipText` case 'V')
	constexpr int32 kConfirmNo   = 108511;   // TWOBUT 86 «Отмена»
	constexpr int32 kResultOk    = 111912;   // 6745 код 1
	constexpr int32 kResultLater = 111919;   // 6745 код 3
	constexpr int32 kFactionBase = 10191;    // имя фракции = код + 10191 (0 Свет / 1 Тьма / 2 Хаос)
}

UCLASS()
class ASDAREBORN_API UAsdaTownWarSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на 6745 (делегат `OnWave2DPacket`).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка, снятие виджета.
	virtual void Deinitialize() override;
	// Окно и состояние принадлежат сессии: любое снятие — окно скрыто, полоса отпущена, очередь ответов пуста.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Очередь 6745 (закон no-reentrant-net-callbacks) и опрос мыши полосы.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaTownWarSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (`.JUI` + `uiCampWarPanelBindControls` + `CScrollBar_ctor` + скин `Nomal_Scr_hori`) ──
	static constexpr int32 kRangeMax    = 25;    // `uiScrollbarSetRange(…, 0, 25)` — второй вызов перекрывает 0…50
	static constexpr int32 kLineStep    = 6;     // `CScrollBar_ctor`: +155 = 6 (стрелки)
	static constexpr int32 kPageStep    = 50;    // `CScrollBar_ctor`: +156 = 50 (жёлоб)
	static constexpr int32 kTrackLeft   = 11;    // ES0 Rect «11 0 41 11» → после раскладки 11…207
	static constexpr int32 kTrackWidth  = 196;   // 218 − 2×11
	static constexpr int32 kThumbWidth  = 11;    // ES3 Rect «20 0 31 11» — длина бегунка горизонтальной полосы
	static constexpr double kOkThrottleMs = 2000.0;   // `timeGetTime() − +2256 > 0x7D0`
	static constexpr int32 kWindowZ     = 700;   // окно сообщества-уровня (не модальное)

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// Порт `CUniconWar_UpdateFactionInfoPanel@0xBCE060` — вход кнопки NPC 30076.
	UFUNCTION(BlueprintCallable, Category="Asda|TownWar")
	void OpenFromNpc();
	// Порт `sub_BCE470` — X 79329 / «Отмена» 79322.
	UFUNCTION(BlueprintCallable, Category="Asda|TownWar")
	void CloseWindow();
	// Окно видно (+2248).
	UFUNCTION(BlueprintPure, Category="Asda|TownWar")
	bool IsOpen() const { return bOpen; }

	// ═══ ДЕЙСТВИЯ (боевые обработчики; опрос мыши, консоль и самотест зовут их же) ═══
	void OnOkClicked();                 // 66561 по 79321 → `uiCampWarPanelTick`
	void OnScrollButton(int32 ButtonId, float CursorAbsX);   // `uiScrollbarOnButtonEvent`: 1 ←, 2 →, 3 бегунок, 0 жёлоб
	void OnScrollDrag(float CursorAbsX);                     // `uiScrollbarOnMouseDrag` (пока бегунок схвачен)
	void OnScrollRelease();                                  // отпускание ЛКМ — `sub_12FF560`: +608 = 0
	void SetScrollPos(int32 Pos);                            // `uiScrollbarSetPos` (событие 69638)

	// ═══ ДИАГНОСТИКА / САМОТЕСТ ═══
	int32 GetScrollPos() const { return ScrollPos; }        // +154 полосы 79314
	int32 GetStoredRate() const { return StoredRate; }      // +2252 (последнее значение события полосы)
	int32 GetCitySlot() const { return OpenSlot; }          // слот города открытого окна
	FString GetText(int32 IdNum) const;                     // текст контрола 79301/79303/79310…79315/79321/79322
	bool IsMarkShown() const;                               // знак лагеря 79302 нарисован
	int32 GetThumbLocalX() const;                           // X бегунка в канвасе полосы (по формуле `uiScrollbarLayoutThumb`)
	UWidget* GetHitWidget(int32 Which) const;               // 0 жёлоб, 1 ←, 2 →, 3 бегунок, 4 «Нажми», 5 X, 6 «Отмена», 7 корень
	const TArray<int32>& GetNoticeTrace() const { return NoticeTrace; }   // loc-id показанных нотисов (−1 — «голый» код)
	FString GetLastNoticeText() const { return LastNoticeText; }          // текст последнего нотиса
	void ClearTrace() { NoticeTrace.Reset(); LastNoticeText.Reset(); }
	void DebugResetThrottle() { LastOkMs = -1.0; }          // снять 2-секундный замок «Нажми»
	FString DumpControls() const;                           // каждый разобранный контрол
	int32 GetFoundControls() const { return FoundControls; }
	int32 GetTotalControls() const { return TotalControls; }

private:
	// ── Окно ──
	bool EnsureWidget();               // каркас `FAsdaJuiWindowHandle::Ensure`
	void ResetWidget();                // снять ссылки на виджет
	void SetupControls();              // порт `uiCampWarPanelBindControls` (разбор + статичные подписи)
	void RegisterZones();              // зона окна по рамке 3×3
	// ── Полоса ──
	void LayoutThumb();                // `uiScrollbarLayoutThumb` (горизонталь): левый край = 11 + (196 − 11)·pos / max
	void RaiseScrollEvent(int32 EventId, int32 Value);   // `settings_DispatchFlagById` (a2 == 79314) и `CControl_RaiseEvent`
	void SetArrowPressed(int32 Plane, bool bPressed);   // состояние SS2 стрелки → (сдвиг UV −1 тексель)
	void TickMouse();                  // опрос ЛКМ по живой геометрии планов полосы
	// ── Сеть ──
	void OnTaxResult(const TArray<uint8>& B);   // `CChat_ShowItemSubnotice@0xBCE6B0` (имя врёт)
	// ── Прочее ──
	void Notice(int32 LocId, const FString& Text);   // ONEBUT (`CItem_BuildTooltipText(…, 0, −1, …)`) + след
	FString Loc(int32 LocId) const;    // loc-строка (теги срезаны)
	void SetLabel(int32 IdNum, const FString& S);

	// ═══ ВИДЖЕТЫ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle = nullptr;    // 79301 `Town_as_movetext`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnX     = nullptr;    // 79329 `Town_as_x`
	UPROPERTY(Transient) TObjectPtr<UImage>     MarkImg  = nullptr;    // 79302 план `Town_as_mark_18_0` (64×64)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBody  = nullptr;    // 79303 `Town_as_text_1`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTax   = nullptr;    // 79310 `Town_as_text_2`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTaxV  = nullptr;    // 79311 `Town_as_text_3` («+ N%», красный)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtDisc  = nullptr;    // 79312 `Town_as_text_4`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtDiscV = nullptr;    // 79313 `Town_as_text_5` («- N%», синий)
	UPROPERTY(Transient) TObjectPtr<UImage>     Track    = nullptr;    // 79314 ES0 `Town_as_slide_26_0` — жёлоб
	UPROPERTY(Transient) TObjectPtr<UImage>     ArrowL   = nullptr;    // 79314 ES1 `Town_as_slide_26_1` — ←
	UPROPERTY(Transient) TObjectPtr<UImage>     ArrowR   = nullptr;    // 79314 ES2 `Town_as_slide_26_2` — →
	UPROPERTY(Transient) TObjectPtr<UImage>     Thumb    = nullptr;    // 79314 ES3 `Town_as_slide_26_3` — бегунок
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPer   = nullptr;    // 79315 `Town_as_text_per` («N%»)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk    = nullptr;    // 79321 `Town_as_okbtn`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOk    = nullptr;    // подпись 79321
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNo    = nullptr;    // 79322 `Town_as_nobtn`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNo    = nullptr;    // подпись 79322
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;  // реле кнопок 79321/79322/79329
	FBox2f ArrowUvNormal[3];           // UV стрелок в обычном состоянии (из кисти ассета) — для SS2
	int32 FoundControls = 0;           // разобрано контролов
	int32 TotalControls = 0;           // ожидалось контролов

	// ═══ МОДЕЛЬ (поля объекта эталона) ═══
	bool bOpen = false;                // +2248
	bool bControlsReady = false;       // контролы разобраны
	int32 OpenSlot = 0;                // слот города, с которым окно открыто (factionIdx)
	int32 ScrollPos = 0;               // +154 полосы
	int32 StoredRate = 0;              // +2252 (`settings_DispatchFlagById`)
	bool bThumbGrab = false;           // +608 полосы — бегунок схвачен
	int32 PressedArrow = 0;            // нажатая стрелка (1 ←, 2 →, 0 нет) — картинка SS2
	double LastOkMs = -1.0;            // +2256 — момент последнего прохода «Нажми» (мс), <0 — не было
	FAsdaMouseFrame Mouse;             // опрос мыши кадра
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // тела 6745 из сети (разбор — в Tick)
	FDelegateHandle HPacket;           // подписка на `OnWave2DPacket`
	TArray<int32> NoticeTrace;         // след нотисов (самотест)
	FString LastNoticeText;            // текст последнего нотиса
};
