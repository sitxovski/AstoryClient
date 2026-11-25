// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaBoosterPanelSubsystem — ОКНО «БАРАБАН ГАЧИ» (JUI IDNum **10050** `BOOSTING_WIN`).
//   Полный реверс с адресами и дампом каждого контрола — `Reports/item_use_panels.md`.
//   Ни одно число здесь не выдумано.
//
// ═══════════════════ КЛАССИФИКАЦИЯ (закон `classify-before-building`) ═══════════════════════════
//   **НОВАЯ СИСТЕМА**: собственное верхнеуровневое окно .JUI (10050 — корень), собственный
//   КОНЕЧНЫЙ АВТОМАТ (открыто → процесс → вращение → стоп → показ приза → подтверждение),
//   ДВА таймаута, покадровая анимация и своя половина протокола (5468/5469 + 5474).
//   Отдельная подсистема по образцу `UAsdaShowOffSubsystem`.
//
// ═══════════════════ ★★★ ГЛАВНОЕ: «БАРАБАНА» ИЗ ЯЧЕЕК НЕТ ═════════════════════════════════════
//   В окне **25 контролов** и НИ ОДНОЙ ячейки барабана. «Барабан» — это ОДИН контрол-иконка
//   **`BOOSTER_RESULT` (10068)**, который перелистывает коды из массива на 36 позиций.
//   Прошлый проход писал «барабан из 36 иконок» — 36 это размер МАССИВА КОДОВ, а не число контролов.
//   Заполняет массив `craftWnd_populateBoosterList@0xD1FBB0` (`item_enhance.c:1771-1845`) — имя
//   ВРЁТ, к крафту отношения не имеет: берёт запись `Booster_list.BIN` (284 б) по
//   `ItemRecord.DurationOrEnchant` и раскладывает её 35 кодов-призов, зациклив их на 36 позиций.
//
// ═══════════════════ ★★★ АНИМАЦИЯ — `boosterShopUpdate@0xD202A0` ══════════════════════════════
//   (`shop_trade.c:12151-12338`). **Не по таймеру**: прямой вызов каждый кадр из главного цикла
//   `game_main.c:2113`, поэтому скорость framerate-зависима — это свойство оригинала.
//
//   Кадр:
//     if (!bProcess) return;                            // процесс не запущен
//     alpha = альфа иконки 10068
//     if (CloseDeadline && прошло >= 30000) → закрыть+применить, чат-нотис 13114, выход
//     if (ResetDeadline && прошло >= 5000)  → ResetSelection(), выход      // ★ ОСТАНОВКА БАРАБАНА
//     if (!bSpinning) return;
//     Decel -= DecelStep;
//     if (Decel > 500)  { alpha<=0 ? следующая позиция + alpha=255 : alpha -= 50 }
//     else              { alpha>0  ? alpha -= 95 : следующая позиция + alpha=255 }
//
//   ⇒ один «кадр барабана» = затухание иконки до нуля + подмена картинки.
//     При 60 FPS: 255/50 ⇒ 6 кадров затухания + 1 подмена ≈ 117 мс на позицию, оборот ≈ 4,2 с.
//
//   ★★★ ЗАМЕДЛЕНИЯ НЕТ — и это НЕ наша недоделка, а состояние оригинала. Доказано двумя способами:
//     (1) вычитаемое `shop[635]` (байт 2540) **не записывается нигде в бинаре** — единственное
//         вхождение это строка чтения (`AsdaStory_ida.c:561508`);
//     (2) вторая ветка не тормозит, а УСКОРЯЕТ (шаг 95 против 50).
//     ⇒ `Decel` вечно 1000 > 500, ветка с шагом 95 недостижима, барабан крутится РАВНОМЕРНО.
//   ★★★ КОНЕЧНАЯ ПОЗИЦИЯ НЕ ВЫБИРАЕТСЯ ПО ПРИЗУ. Ни одного сравнения кода позиции с кодом приза в
//     бинаре нет: барабан просто ЗАМОРАЖИВАЕТСЯ таймаутом 5 000 мс на произвольной позиции,
//     а приз рисуется отдельно в тот же контрол 10068 после заливки прогресс-бара.
//
// ═══════════════════ ТАЙМЛАЙН ОДНОГО ПРОКРУТА ═════════════════════════════════════════════════
//   t=0        клик START (10069): звук, 5468, процесс=1, CloseDeadline=+30000,
//              СКРЫТЬ 10071 (крестик) и 10074 (подложка «Просмотр»)
//   приход 5469 (код 0): CloseDeadline=0, ResetDeadline=+5000, вращение=1,
//              звук `s_item_boosting02.wav`, остановка зациклённого `s_item_boosting01.wav`,
//              СКРЫТЬ 10053/10054 (CHIMNEY_BG/CLOCK_BG), прогресс-бар 10066 залить за **9200 мс**
//   +5000 мс   `boosterShopResetSelection@0xD21A90` — БАРАБАН СТОП, процесс=0
//   +9200 мс   прогресс дошёл до 100 % → событие 24 →
//              `tagBoosterShop_RefreshSelectedItemDisplay@0xD21C00`: спрятать 10065 (BANG),
//              иконка приза в 10068, имя приза в 10067 цветом грейда, спрятать 10066,
//              ПОКАЗАТЬ 10075 (подложка кнопки «Подтвердить»)
//   клик OK (10070): применить результат, эффект удачи на кости, **5474**, нотис 20066/20067, закрыть
//   ★ 30 000 мс от START без результата → закрыть + чат-нотис **13114**
//
// ═══════════════════ КНОПКИ (`boosterWnd_OnMessage@0xD16ED0`, `shop_trade.c:11919-12023`) ══════
//   **10069 START**   → звук 4; `boosterShopUseOrClose@0xD22250`: есть запись предмета → 5468,
//                       иначе закрыть. ★ Других проверок НЕТ — ни денег, ни веса, ни «уже крутится».
//                       ★★★ И повторный клик НЕ БЛОКИРУЕТСЯ (в case нет ни одного `if`) —
//                       это дефект оригинала, воспроизводим (см. CVar ниже).
//   **10070 OK**      → `OnRecv_ItemEnchantResult@0xD20B30` (имя ВРЁТ — это обработчик OK).
//   **10071 CLOSE**   → `boosterApplyResult@0xD1F830` (имя `tagBoosterShop_CloseWindow` ВРЁТ:
//                       это не «закрыть», а «ПРИМЕНИТЬ РЕЗУЛЬТАТ и закрыть»).
//   **10072 PREVIEW** → показать список возможных призов. Не блокируется.
//
// ═══════════════════ ★ АНИМАЦИЯ КОНТРОЛОВ ЗАДАНА В ДАННЫХ, НЕ В КОДЕ ══════════════════════════
//   Сирена, труба, часы, глаза, «пятна», рамка, «?» и «взрыв» — планы с `Animate="1"` в
//   `RE/docs/JuiSkin.ini`; код их только показывает/прячет. Импортёр поэтому и сделал из них
//   `UAsdaAnimatedPlane`. Кадры из данных (StartFrame..EndFrame/TotalFrame, FrameInterval мс):
//     Booster_Siren 0..11/12 @700 · Booster_Chimney 0..15/16 @1200 · Booster_Clock 0..15/16 @800
//     Booster_Eye_White/Yellow/Blue 0..2/3 @300 · Booster_Border 0..15/16 @900
//     Booster_Question 0..24/25 @1500 · Booster_Center (BANG) 0..14/15 @800
//   Своей анимации мы НЕ пишем (закон `no-invented-ui`) — только видимость, как эталон.
//
// ═══════════════════ РАСКЛАДКА 68-БАЙТНОЙ НАГРУЗКИ 5469 ═══════════════════════════════════════
//   Сверено ТРЕМЯ источниками (клиентский распаковщик, клиентские потребители полей, серверный
//   сборщик `sendGachaItemUse@0x4F5680`, `GameServer.c:94521-94578`):
//     +0   u8   код результата: **0 = УСПЕХ**, 1…8 — отказы (нотисы 13107…13113, 12165)
//     +1   u8   флаг «редкий» (1 → `eff_lucky_a_01.jeb` + `s_item_boostrare.wav`)
//     +2   i32  код ПОТРАЧЕННОГО гача-предмета  ⚠ НЕ приза
//     +6   u8   контейнер исходного слота
//     +7   i16  индекс исходного слота
//     +9   57 б запись НОВОГО предмета; первый i32 (**+9…+12**) = **КОД ПРИЗА**
//     +66  i16  вес персонажа
//   Итого 1+1+4+1+2+57+2 = 68 ✓
//   ★ Флаг «редкий» — свойство ПОЗИЦИИ в таблице шансов (индекс ≤ 4), а не самого предмета
//     (`GameServer.c:94270-94296`).
//
// ═══════════════════ ДЕФЕКТЫ ОРИГИНАЛА, НАЙДЕННЫЕ ПОПУТНО ═════════════════════════════════════
//   1. `shop[635]` не инициализируется ⇒ «замедление» барабана недостижимо (см. выше).
//   2. Массив призов чистится циклом на **35** записей, а заполняется и перебирается на **36**:
//      36-я позиция лежит ровно на буфере пакета 5469 — за оборот получается один «пустой» кадр.
//      Мы за границу НЕ читаем (это UB), а воспроизводим НАБЛЮДАЕМОЕ: 36-я позиция пуста.
//   3. Кнопка START не блокируется во время процесса — повторный клик шлёт 5468 заново и
//      перезапускает 30-секундный таймаут.
//   4. Сброс на 5 000 мс гасит флаг процесса, из-за чего проверка 30-секундного таймаута
//      перестаёт выполняться, а прогресс-бар успевает залиться лишь до ~54 %.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaBoosterPanelSubsystem.generated.h"

// ★ 31.10.2026: КТО ОТКРЫЛ ОКНО «ВИД» 15600 — от этого зависит, двигать ли окно при (пере)заполнении.
//   Эталон двигает его только в двух входах: барабан (`boosterWnd_OnMessage@0xD16ED0`, `case 0x2758`) и окно состава
//   набора (`positionHoverTooltipWnd@0xDB3900`); премиум-магазин окно НЕ двигает (`uiHandleCommand@0xC23AC0` case 0xE619,
//   `itemOnUseDispatch@0xC267E0`). Полный поток — `Reports/booster_view/FLOW.md`.
enum class EAsdaPrevSource : uint8
{
	Drum,          // барабан 10050, кнопка 10072
	BoosterShop,   // магазин бустеров 15300 (кнопка 15547, клик по иконке карточки)
	PremiumShop,   // премиум-магазин 6000 (клик по карточке бустера; кнопка 58905 панели 58900)
	PackagePanel   // окно состава набора 26500 (клик по ячейке бустера)
};

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAudioComponent;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

UCLASS()
class ASDAREBORN_API UAsdaBoosterPanelSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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

	// ★★ ПОСТАВИТЬ ОКНО «ВИД» 15600 В ЗАДАННУЮ ТОЧКУ ЭКРАНА (абсолютные координаты Slate).
	//   Барабан 10050 ставит его по эталонной формуле «справа от себя» (см. `PlacePrevWindow`),
	//   а магазин бустеров — СЛЕВА от своей рамки и на уровне выбранной строки (просьба владельца
	//   2026-08-28). Заявка живёт до успешной установки: пока окно ни разу не отрисовано, живой
	//   геометрии нет, и `Tick` повторит попытку.
	void RequestPrevPlacementAbs(const FVector2D& TopLeftAbs);

	// Ширина окна «Вид» ПО ДАННЫМ АССЕТА (объединение девяти планов рамки), а не константой.
	//   Нужна вызывающему, чтобы посчитать «слева от чего-то».
	float GetPrevWindowWidth() const;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaBoosterPanelSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═══════════════════ КОНСТАНТЫ ИЗ ЭТАЛОНА ═══════════════════
	static constexpr int32 kWinIdNum   = 10050;
	static constexpr int32 kDrumSlots  = 36;     // позиций перебора (`shop[260] < 35 ? ++ : 0`)
	static constexpr int32 kDrumFilled = 35;     // реально заполняемых (сброс чистит `for i < 35`)
	static constexpr int32 kCloseTimeoutMs = 30000;  // 0x7530 — от клика START
	static constexpr int32 kResetTimeoutMs = 5000;   // 0x1388 — от приёма 5469, ОСТАНОВКА барабана
	static constexpr int32 kProgressMs     = 9200;   // заливка прогресс-бара 10066
	// ★ Длительность анимации «взрыва» 10065 `BOOSTER_BANG`. Скин `Booster_Center`
	//   (`Donors/TRANSLATE_unpacked/jui_orig/JUISKIN.ini`: TotalFrame 15, FrameInterval **800**), а `FrameInterval` —
	//   это длительность ПОЛНОГО прогона, а не время кадра (`AsdaAnimatedPlane.h:34`, формула 1:1 из
	//   `ui_core.c:108794-108888`). По концу прогона план шлёт событие 24 → показ приза.
	static constexpr int32 kBangMs         = 800;
	static constexpr int32 kDecelStart     = 1000;   // `shop[261]`
	static constexpr int32 kDecelThreshold = 500;    // порог переключения фазы
	static constexpr int32 kAlphaStepSlow  = 50;     // шаг затухания в достижимой фазе
	static constexpr int32 kAlphaStepFast  = 95;     // шаг в НЕДОСТИЖИМОЙ фазе (оставлен 1:1)
	// Z: окно обязано лежать выше инвентаря (720), из которого его открывают.
	static constexpr int32 kWinZ = 762;

	// ═══════════════════ ★★★ ОТКРЫТИЕ — порт `itemBoosterWnd_Open@0xD1F4A0` ══════════════════════
	//   Гейты 1:1: есть запись `item_list` И есть строка `booster_list` по `DurationOrEnchant`.
	//   Нет — return false, и вызывающий уходит в обычное применение предмета.
	UFUNCTION(BlueprintCallable, Category="Asda|Гача")
	bool OpenWindow(int32 ItemCode, int32 ContainerId, int32 CellIndex);

	// ═══════════════════ КНОПКИ ═══════════════════
	UFUNCTION(BlueprintCallable, Category="Asda|Гача") void OnStartButton();     // 10069
	UFUNCTION(BlueprintCallable, Category="Asda|Гача") void OnOkButton();        // 10070
	UFUNCTION(BlueprintCallable, Category="Asda|Гача") void OnCloseButton();     // 10071
	UFUNCTION(BlueprintCallable, Category="Asda|Гача") void OnPreviewButton();   // 10072

	UFUNCTION(BlueprintPure, Category="Asda|Гача") bool IsWindowOpen() const { return bWindowOpen; }
	// `boosterApplyResult@0xD1F830` (имя IDA `tagBoosterShop_CloseWindow` врёт): применить полученный приз и закрыть окно.
	//   Публичен с волны В6 ремесла (W5-7): его зовёт смерть своего персонажа (`Recv_CharDeath_Apply`, network.c:42603).
	void ApplyResultAndClose();
	UFUNCTION(BlueprintPure, Category="Asda|Гача") bool IsSpinning()  const { return bSpinning; }
	UFUNCTION(BlueprintPure, Category="Asda|Гача") bool IsProcessRunning() const { return bProcess; }

	// ★★★ Приход 5469. Возвращает true, если окно ЗАБРАЛО пакет себе (ветки А/Б эталона: результат
	//   покажет окно). false — окно закрыто, и вызывающий обязан применить результат немедленно
	//   (ветка В, `network.c:17060+`: там же сразу уходит 5474).
	bool NotifyGachaResult(const TArray<uint8>& Body68);

	// ═══════════════════ ★ ОКНО «ВИД» 15600 — ОБЩАЯ ТОЧКА ДВУХ ВЛАДЕЛЬЦЕВ ═══════════════════
	//   Порт `boosterWindowPopulate@0xB82D90`. В эталоне эту ОДНУ функцию зовут ДВА окна:
	//     • барабан `BOOSTING_WIN` 10050 — кнопка 10072 (`boosterWnd_OnMessage@0xD16ED0`, `case 0x2758`);
	//     • магазин бустеров `BOOSTER_SHOP_WIN` 15300 — кнопка 15547 «Вид» и закрытие/смена
	//       вкладки (`uiShopDialogCommandHandler@0xB86600`, `case 15547/15648`;
	//       `uiCloseItemComposeDialog@0xB83DB0` зовёт `boosterWindowPopulate(this, 0, −1)`).
	//   Поэтому обе точки ПУБЛИЧНЫ, а второй копии окна 15600 не заводится (закон
	//   `port-shared-points`). `PopulatePrevWindow(0)`/`ClosePrevWindow()` = эталонный «0, −1».
	UFUNCTION(BlueprintCallable, Category="Asda|Гача")
	void PopulatePrevWindow(int32 ItemCode);   // порт `boosterWindowPopulate`, ветка a2 != 0
	UFUNCTION(BlueprintCallable, Category="Asda|Гача")
	void ClosePrevWindow();                    // порт `boosterWindowPopulate(..., 0, −1)`

	// ═══════════ ★★★ 31.10.2026: ОТКРЫТИЕ «ВИДА» ИЗ ЛЮБОГО ОКНА (порт эталонной тройки вызовов) ═══════════
	//   Каждый вход эталона: `uiStorageWindow_ResetState(g_bModalNoticeActive)` (для окна 15600 это страница `+662` = 0,
	//   «чей состав» `+698` = −1, флаг `+697` = 0) → `boosterWindowPopulate(…, 1, код)` → у премиум-магазина ещё
	//   `set_o2B9(…, 1)` (= `sub_923DB6`, флаг `+697` = 1). Флаг `+697` оживляет щелчок по ячейкам окна
	//   (`uiInventorySlotUseItem@0xB86EE0`: набор → окно 26500, иное → примерка). `bCellsActive` = этот флаг.
	//   Возвращает true, если окно показано с составом `ItemCode`.
	bool OpenPrevWindowFrom(int32 ItemCode, EAsdaPrevSource Source, bool bCellsActive);
	// Поставить окно «Вид» левым-верхним углом рамки (`BOOSTER_PREV_WIN_0_0`) в точку КАНВЫ — в тех же единицах, что
	//   формула барабана (`PlacePrevWindow`) и центрирование окна 26500. Порт `CControl_SetField74_75(g_pHoverTooltipWnd, x, y)`.
	void PlacePrevAtCanvas(const FVector2D& TopLeft);
	bool IsPrevVisible() const { return bPrevVisible; }                    // эталонный `v62[615]`
	int32 GetPrevItemCode() const { return PrevItemCode; }                 // эталонный `v62[698]`
	EAsdaPrevSource GetPrevSource() const { return PrevSource; }           // кто открыл окно (см. `EAsdaPrevSource`)
	bool ArePrevCellsActive() const { return bPrevCellsActive; }           // эталонный флаг `v62[697]`
	int32 GetPrevPage() const { return PrevPage; }                         // эталонный `v62[662]`
	// ── 05.11.2026: доступ самотеста `asda.PremiumMall.BoosterLayoutSelfTest` (состав «Вида» созданного бустера) ──
	// Код «особой» ячейки 0…4 / «обычной» ячейки 0…14 текущей страницы (эталонные `v62[623+i]` / `v62[645+i]`), −1 — пусто.
	int32 DebugPrevSpecialCode(int32 i) const { return PrevSpecialCode.IsValidIndex(i) ? PrevSpecialCode[i] : -1; }
	int32 DebugPrevNormalCode(int32 i) const { return PrevNormalCode.IsValidIndex(i) ? PrevNormalCode[i] : -1; }
	// Страница «обычных» — тот же вход, что кнопки 15644 `_OPT0`/`_OPT1`.
	void DebugSetPrevPage(int32 NewPage) { SetPrevPage(NewPage); }
	// Ключ `asda.Booster.PrevPaths` (1 = входы из премиум-магазина и окна 26500 перенесены). Аксессор — чтобы имя
	//   ключа не попадало в чужие .cpp (Shipping-правило `AGENT_RULES_PORT.md`).
	static bool PrevPathsOn();
	// Высота окна «Вид» по `.JUI` (`BOOSTER_PREV_WIN` Size = "360 410") — нужна стыковке с окном 26500.
	static constexpr float kPrevWinH = 410.f;
	// Вычитаемое стыковки `positionHoverTooltipWnd@0xDB3900`: y(15600) = y(26500) + h(26500) − 343 (число из кода эталона).
	static constexpr float kPrevDockBelowTop = 343.f;

	// ═══════════════════ ДИАГНОСТИКА ═══════════════════
	FString DumpControls() const;
	FString DumpModel() const;

private:
	// ── Жизненный цикл виджетов ──
	bool EnsureWindow();
	void SetupControls();
	void RegisterZones();
	void UnregisterZones();
	bool GetWinRect(FVector2D& OutMin, FVector2D& OutMax) const;

	// ── Порты эталонных функций ──
	void PopulateDrum(int32 ItemCode);        // `craftWnd_populateBoosterList@0xD1FBB0`
	void ResetFields();                       // `uiQuestSlotPanelResetFields@0xD1FFC0`
	void HidePanelControls();                 // `boosterShopHidePanelControls@0xD200E0`
	void ShowPanelControls();                 // `boosterShopShowPanelControls@0xD1FEB0`
	void ResetSelection();                    // `boosterShopResetSelection@0xD21A90` — СТОП барабана
	void RefreshSelectedItemDisplay();        // `tagBoosterShop_RefreshSelectedItemDisplay@0xD21C00`
	void TickDrum();                          // тело `boosterShopUpdate@0xD202A0`
	void TickProgress();                      // `CProgressBar_UpdateAnim@0x12E9400`, только заливка
	void TickBang();                          // конец анимплана 10065 → событие 24 → показ приза
	// Применение результата к инвентарю + эффект + нотис + 5474 (общая часть OK и «применить при закрытии»).
	void ApplyPrize(bool bSend5474);

	void SetVis(UWidget* W, bool bVisible);
	void PlayLoopStart();
	void StopLoopStart();

	// ═══════════ ★★★ ОКНО «ВИД» / «ПРОСМОТР» — JUI `BOOSTER_PREV_WIN`, IDNum 15600 ═══════════
	//   Кнопка 10072 в оригинале открывает НЕ панель пакета 26500 и НЕ текстовую подсказку, а
	//   отдельное корневое окно на 5 «особых» + 15 «обычных» ячеек с иконками:
	//     `boosterWnd_OnMessage@0xD16ED0`, `case 0x2758u` →
	//       `uiStorageWindow_ResetState(g_bModalNoticeActive)`
	//       `boosterWindowPopulate(g_bModalNoticeActive, 1, itemCode)`   ← @0xB82D90, `shop_trade.c:563`
	//       `CControl_SetField74_75(g_pHoverTooltipWnd, x223, y223 − 263)`
	//   ⚠ Имена глобалов ВРУТ и это доказано адресами: `g_bModalNoticeActive` = 0x02BDEC18 —
	//   синглтон `CBoosterShop`; `g_pHoverTooltipWnd` = 0x02BDF5B8 — его же поле `+616*4`, то есть
	//   КОРЕНЬ окна 15600. Разность 0x9A0 = 616×4.
	//   Привязка контролов — `BoosterShop_InitWindow@0xB7E4A0`:
	//     616 → 15600 корень, 618…622 → 15603/15605/15607/15609/15611 (`BOOSTER_PREV_SITEM00…04`),
	//     630…644 → 15614…15642 шаг 2 (`BOOSTER_PREV_ITEM05…19`), 661 → 15644 (страницы),
	//     664 → 15646 (`BOOSTER_PREV_NAME`), 665 → 15648 (`BOOSTER_PREV_WIN_CLOSE`).
	//   Источник данных — та же строка `booster_list` (284 б = id + 35 пар): позиции 0…4 идут в
	//   «особые» (`boosterRecord->itemName[8*i]`), 5…34 — в «обычные» (`[8*i + 40]`), по 15 на страницу.
	bool EnsurePrevWindow();
	void SetupPrevControls();
	// ★ `PopulatePrevWindow` / `ClosePrevWindow` ПЕРЕЕХАЛИ В public: их зовёт и магазин
	//   бустеров 15300 (кнопка «Вид» 15547) — см. блок «ОКНО «ВИД» 15600» выше.
	void PlacePrevWindow();                    // порт `CControl_SetField74_75(корень, x223+w223, y223+h223−263)`
	// ★ Подсказка по наведению на ячейку окна «Вид» — порт `uiInventoryShowSlotTooltip@0xB87200`
	//   (`AsdaStory_game.c:211457-211530`): индекс ячейки берётся таблицами `cmdToHairIndex5@0xB86860`
	//   (5 «особых») и `invLookupSpecialItemIndex@0xB868F0` (15 «обычных»), код — из `v15[idx+623]` и
	//   `v15[idx+645]`, дальше СТАНДАРТНЫЙ тултип предмета.
	void TickPrevTooltip();
	// ★ Порт `case 15644` сообщения 0x10501 (`shop_trade.c:1511-1519`): смена страницы «обычных».
	void SetPrevPage(int32 NewPage);
	// ★ 31.10.2026: ячейка окна «Вид» под курсором (0…4 «особые», 5…19 «обычные», −1 = нет) и её код. Общая часть
	//   подсказки (`TickPrevTooltip`) и щелчка (`TickPrevCellClick`) — эталон опознаёт ячейку одними и теми же таблицами
	//   `cmdToHairIndex5@0xB86860` / `invLookupSpecialItemIndex@0xB868F0`.
	int32 PrevCellUnderCursor(int32& OutCode) const;
	// ★ 31.10.2026: щелчок ЛКМ по ячейке окна «Вид» — событие 0xB `boosterShopOnMessage@0xB876F0` →
	//   `uiInventorySlotUseItem@0xB86EE0` (гейт — флаг `+697`, см. `OpenPrevWindowFrom`).
	void TickPrevCellClick();
	void OnPrevCellClicked(int32 ItemCode);    // ветки «особые»/«обычные» `uiInventorySlotUseItem`

	// ── Виджеты (idnum → узел WBP) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> WinWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget> BgDrag        = nullptr;  // 10051 BOOSTER_BG (ручка EVS 6→0x20003)
	UPROPERTY(Transient) TObjectPtr<UWidget> ChimneyBg     = nullptr;  // 10053
	UPROPERTY(Transient) TObjectPtr<UWidget> ClockBg       = nullptr;  // 10054
	UPROPERTY(Transient) TObjectPtr<UWidget> Chimney       = nullptr;  // 10055
	UPROPERTY(Transient) TObjectPtr<UWidget> Clock         = nullptr;  // 10056
	UPROPERTY(Transient) TObjectPtr<UWidget> Eye1          = nullptr;  // 10057
	UPROPERTY(Transient) TObjectPtr<UWidget> Eye2          = nullptr;  // 10058
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Spots;            // 10059…10062
	UPROPERTY(Transient) TObjectPtr<UWidget> Border        = nullptr;  // 10063
	UPROPERTY(Transient) TObjectPtr<UWidget> Question      = nullptr;  // 10064
	UPROPERTY(Transient) TObjectPtr<UWidget> Bang          = nullptr;  // 10065 BOOSTER_BANG
	UPROPERTY(Transient) TObjectPtr<UImage>  ProgressFill  = nullptr;  // 10066 заливка (ES1)
	UPROPERTY(Transient) TObjectPtr<UImage>  ProgressBase  = nullptr;  // 10066 подложка (ES0)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PrizeName  = nullptr;  // 10067 BOOSTER_NAME
	UPROPERTY(Transient) TObjectPtr<UImage>  DrumIcon      = nullptr;  // ★ 10068 — ЕДИНСТВЕННАЯ ячейка барабана
	UPROPERTY(Transient) TObjectPtr<UButton> BtnStart      = nullptr;  // 10069
	UPROPERTY(Transient) TObjectPtr<UButton> BtnOk         = nullptr;  // 10070
	UPROPERTY(Transient) TObjectPtr<UButton> BtnClose      = nullptr;  // 10071
	UPROPERTY(Transient) TObjectPtr<UButton> BtnPreview    = nullptr;  // 10072
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapStart   = nullptr;  // нотис 12162
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapOk      = nullptr;  // нотис 2288
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapPreview = nullptr;  // нотис 12870
	UPROPERTY(Transient) TObjectPtr<UWidget> StartBtnBase  = nullptr;  // 10073
	UPROPERTY(Transient) TObjectPtr<UWidget> ViewBtnBase   = nullptr;  // 10074
	UPROPERTY(Transient) TObjectPtr<UWidget> OkBtnBase     = nullptr;  // 10075
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> LoopSound = nullptr;   // зациклённый s_item_boosting01

	// ── ★★★ Окно «Вид» 15600 `BOOSTER_PREV_WIN` ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> PrevWidget = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> PrevSpecial;      // 15603/15605/15607/15609/15611 (5)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> PrevNormal;       // 15614…15642 шаг 2 (15)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PrevName  = nullptr;  // 15646
	UPROPERTY(Transient) TObjectPtr<UButton> PrevClose    = nullptr;  // 15648
	// ★★★ 15644 `BOOSTER_PREV_PAGE` — в .JUI это ОДИН контрол-`RadioButton` шириной 340 со скином
	//   `BoosterPrev_Index_2Btn`: план ES0 = стрелка «<» (лок. 0,0,18,18), план ES1 = стрелка «>»
	//   (лок. 322,0,340,18), плюс собственный `CaptionText="<"` по центру всего контрола.
	//   Импортёр развернул его в ПЯТЬ узлов (`_22_0`, `_22_1`, `_OPT0`, `_OPT1`, `_Text`), поэтому
	//   эталонное `CControl_SetVisible(v62[661], …)` в порту = гашение ВСЕХ пяти.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PrevPageParts;   // 15644 — видны только при >15 призах
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> PrevPageBtns;    // `_OPT0` (страница 0) / `_OPT1` (страница 1)
	bool  bPrevReady   = false;
	bool  bPrevVisible = false;
	// Заявка на постановку окна «Вид» в абсолютную точку (см. `RequestPrevPlacementAbs`).
	bool      bPrevPlaceAbsPending = false;
	FVector2D PrevPlaceAbsWant = FVector2D::ZeroVector;
	int32 PrevPage     = 0;                                           // эталонный `v62[662]`
	// Коды 35 позиций строки `booster_list` текущего бустера (0…4 «особые», 5…34 «обычные»).
	TArray<int32> PrevSlots35;
	// ★ Коды, РЕАЛЬНО положенные в ячейки окна «Вид» — эталонные массивы `v62[623+i]` (5 «особых») и
	//   `v62[645+i]` (15 «обычных», зависят от страницы). Именно их читает подсказка по наведению.
	TArray<int32> PrevSpecialCode;                                    // 5 шт., −1 = ячейка пуста
	TArray<int32> PrevNormalCode;                                     // 15 шт., −1 = ячейка пуста
	// Ячейка, для которой сейчас показана плашка: 0…4 «особые», 5…19 «обычные», −1 = нет.
	int32 PrevCellHovered = -1;
	int32 PrevItemCode    = 0;                                        // эталонный `v62[698]` — чей состав показан
	// ★ 31.10.2026: кто открыл окно (решает, двигать ли его при перезаполнении — см. `EAsdaPrevSource`).
	EAsdaPrevSource PrevSource = EAsdaPrevSource::Drum;
	// ★ 31.10.2026: эталонный флаг `v62[697]` — щелчок по ячейкам окна «Вид» оживлён (`uiInventorySlotUseItem`).
	bool bPrevCellsActive = false;
	// ★ 31.10.2026: заявка «поставить окно в точку канвы» (`PlacePrevAtCanvas`): живой геометрии не требует.
	bool      bPrevPlaceCanvasPending = false;
	FVector2D PrevPlaceCanvasWant = FVector2D::ZeroVector;
	// ★ 31.10.2026: опрос мыши для щелчка по ячейкам окна «Вид» (фронт нажатия ЛКМ = эталонное событие 0xB).
	FAsdaMouseFrame PrevMouse;
	// ★ 31.10.2026: кадр последнего открытия окна (`GFrameCounter`) — нажатие, которое само открыло «Вид» (клик по карточке
	//   премиум-магазина), в том же кадре щелчком по ячейке под курсором не считается (порядок тиков подсистем не задан).
	uint64 PrevOpenFrame = 0;
	static constexpr int32 kPrevZ = 764;   // выше окна барабана (762) — эталон кладёт его поверх

	// ── Состояние (эталонные поля окна) ──
	bool  bWindowOpen = false;   // dword 254 `set_oFE`
	bool  bProcess    = false;   // dword 255 `set_oFF` — гейт всего update
	bool  bSpinning   = false;   // dword 256 `set_o100`
	bool  bReady      = false;
	int32 DrumIdx     = 0;       // dword 260, 0…35
	int32 Decel       = kDecelStart;   // dword 261
	// ★ Вычитаемое `shop[635]`: в бинаре НЕ ЗАПИСЫВАЕТСЯ НИГДЕ ⇒ фактически 0. Оставлено полем
	//   ровно затем, чтобы дефект оригинала был виден в коде, а не «исправлен» молча.
	int32 DecelStep   = 0;
	int32 DrumAlpha   = 0;       // альфа иконки 10068 (эталон читает её через `sub_92E374`)
	double CloseDeadlineS = 0.0; // dword 262 (0 = таймаут не взведён)
	double ResetDeadlineS = 0.0; // dword 263
	double ProgressStartS = 0.0; // старт заливки 10066; 0 = не идёт
	bool   bProgressDone  = false;
	// ★ Старт анимации «взрыва» 10065; 0 = не идёт. Именно её окончание, а НЕ заполнение
	//   прогресс-бара, показывает приз в оригинале (`ui_core.c:48103-48104`, `cmdId == 10065`).
	double BangStartS     = 0.0;

	// Дескриптор выбранного предмета (эталонные 7 байт `gap_3F0[48]`: код + контейнер + слот).
	int32 SelItemCode  = 0;
	int32 SelContainer = -1;
	int32 SelSlot      = -1;

	// Копия принятого пакета 5469 (эталонный буфер `gap_3F0[1460]`, 68 байт).
	TArray<uint8> Packet68;
	bool bHavePacket = false;

	// Массив кодов барабана: 36 позиций, реально заполняются 35 (36-я — дефект оригинала, пуста).
	int32 DrumCodes[kDrumSlots] = {};

	int32 CntOpen = 0, CntStart = 0, CntPrize = 0;
};
