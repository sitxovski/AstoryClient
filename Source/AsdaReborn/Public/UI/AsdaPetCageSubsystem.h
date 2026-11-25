// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// UAsdaPetCageSubsystem — ГЛАВНОЕ ОКНО СИСТЕМЫ ПИТОМЦЕВ (волна 2 порта питомцев, 21.09.2026).
//
// Одно окно эталона `CPetData`/`CRefineWnd` разложено в порту на ТРИ виджета, потому что в
// оригинале это три `.JUI`-окна одного объекта:
//     • **43000** `Pet_Cage_Win`   — «Дом для питомца»: список, статы, имя, кнопка-корзина;
//     • **43058** `Pet_Button_Win` — всплывающая панель пяти действий (призыв/отзыв/рост/развитие/закрыть);
//     • **43200** `Pet_Status_Win` — HUD-плашка ПРИЗВАННОГО питомца (имя, стадия, опыт, сытость).
// Все три живут в ОДНОМ объекте эталона, поэтому и в порту у них одна подсистема: у них общее
// состояние (выбранный и призванный питомец) и общие перерисовки.
//
// ЧТО ЧИТАЕМ. Данные — только `UAsdaPetSubsystem` (волна 1). Своей копии списка здесь нет.
// Справочники — клиентские `DT_pettable` / `DT_petexptable` / `DT_petoptiontable` через
// `UAsdaItemTableSubsystem` (эталон грузит те же `PetTable.BIN` / `PetExpTable.BIN` /
// `PetOptionTable.BIN` в `CPetData_LoadPetTable` и соседях, `AsdaStory_game.c:411626`).
//
// ══════════════════════ ★★★ МИСНЕЙМЫ И ЛОЖНЫЕ ИМЕНА, КОТОРЫЕ ЗДЕСЬ СНЯТЫ ══════════════════════
//   М1. `CRefineWnd_Open@0xDC3390` — это ОТКРЫТИЕ ОКНА ПИТОМЦА 43000, не рефайна
//       (`item_enhance.c:3801`). Весь префикс `refine*`/`upgrade*` в диапазоне idnum 43000-43205 —
//       методы `CPetData`.
//   М2. **`nPageSlots` (CPetData+0x1E0) — это НЕ «слотов на странице», а ЧИСЛО ДОСТУПНЫХ ЯЧЕЕК**
//       (серверное `m_iPetPossessionCount`, 6..30): в него пишут приёмники 6122
//       (`item_enhance.c:5036`) и 6123 (`item_enhance.c:5090`).
//   М3. **43020…43023 — это НЕ «кнопки страниц», а КОНТЕЙНЕРЫ РЯДОВ** `PetCage_ListBase1…4`.
//       `cRefineUI_ResetPanel` (`AsdaStory_game.c:411358-411364`) делает ровно:
//           `for (j = 0; j < 4; ++j) SetVisible(pPageBtns[j], j >= PossessionCount/6 - 1 ? 0 : 1)`
//       то есть ряд j показывается, когда ячеек хватает на (j+2)-й ряд. Никакого «листания»
//       в оригинале НЕТ: все доступные 6..30 слотов видны одновременно, пятью рядами по шесть.
//       ⇒ Формулировка плана «4 кнопки страниц» опровергнута кодом; воспроизводим код.
//   М4. `CPetData+0x268` = ВЫБРАННЫЙ в UI слот (`wnd[154]`), `+0x26C` = ПРИЗВАННЫЙ (`wnd[155]`).
//       Прежнее имя `selectedPetCode` у второго — ложь (`item_enhance.c:4519-4526`).
//   М5. `refineWnd_RefreshSelectedPanel@0xDC70B0` — это перерисовка **статус-плашки 43200**,
//       а не «выбранной панели»: она пишет в `pCtrl43201/43202/43205/43203/43204`.
//   М6. `sub_92FC88@0xDC7070` — «спрятать 43200» (`byte+1 = 0; SetVisible(pCtrl43200, 0)`).
//       `sub_93DB58@0xDCC010` — «спрятать панель 43058». `upgradeWindowShow@0xDCBF50` — показать её
//       у курсора. `sub_91E898@0xDC4570` — форс-переинициализация окна.
//   М7. Поле `pettable+0x50` в порту названо `DurabilityFactor`, но эталон делит его на 10 и
//       умножает на порог опыта (`AsdaStory_game.c:411568-411570`, `item_enhance.c:4303-4305`) —
//       это **ExpRateX10**, ровно как серверное `PetInfo+21`. Имя оставлено как есть (таблица
//       генерируемая, закон `itemdb-header-generated-not-manual`), смысл — здесь.
//   М8. `pettable+0x44` (`Grade`) — ключ строки названия ранга: loc `101200 + Grade`, цвет по
//       таблице из восьми значений. `pettable+0x84/0x88/0x8C` (`OptRarityCode0..2`) — ключ
//       «типа питания»: loc `101300 + код`, три цвета.
//
// ══════════════════════════ ЧЕТЫРЕ ГЕЙТА ОТКРЫТИЯ (item_enhance.c:3801-3835) ═══════════════════
//   Раннее: окно уже видно ⇒ ПРОСТО СПРЯТАТЬ (тумблер), ни один гейт не проверяется.
//   Иначе по порядку, любой сработавший — окно НЕ открывается и нотиса НЕТ:
//     Г1 `tileInteract_isControlVisible(g_pItemMainWnd)` — открыто окно создания предмета;
//     Г2 `isBoundControlVisible(g_pItemQtyMakeDlg)`      — открыто окно количества;
//     Г3 `uiPetDialogEnsureInit(g_PetUIDialog)`          — открыт диалог развития 43500;
//     Г4 `sub_937DCA(g_pUpgradeDialog)` (= `get_o5C`)    — открыт диалог роста 44200.
//   Окна Г1/Г2 (создание предмета, количество) и Г3/Г4 (волны 4-5) в порту ещё не сделаны;
//   гейты написаны через реестр окон и заработают сами, как только окна появятся.
//
// ═════════════════════════════ ПАНЕЛЬ 43058 И ЕЁ ПЯТЬ КНОПОК ══════════════════════════════════
//   Обработчик — `upgradeWndProc@0xDC3690`, ветка `a3 == 66561` (`item_enhance.c:3864-3975`).
//   Каждая кнопка ЗАКРЫВАЕТ панель (`sub_93DB58`) — кроме 43002 и 43083, они не её.
//     43065 «Призыв» (loc 109950): есть выбранный?
//              сытость != 0 → если НЕ призван, шлём 6100; если уже призван — НИЧЕГО;
//              сытость == 0 → в сумке есть предмет subType 105?
//                              да  → msgbox 56 (loc 101002) → по «ОК» 6108 (лечение);
//                              нет → нотис 101104.
//     43066 «Отменить призыв» (109951): призван → 6100; иначе нотис 101113.
//     43055 «Рост питомца» (109952)      → окно 44200 (волна 4). Гейты 109975 / 109946 — у окна.
//     43056 «Развитие питомца» (109953)  → окно 43500 (волна 5). Гейт 109974 — у окна.
//     43057 «Закрыть» (109954)           → только спрятать панель.
//   Кнопки 43053/43054 (`Pet_Cage_Win_BTN00/BTN01`) — НЕ панель: это пара внутри 43000, их
//   родители `Btn_CBase` 43061/43062 скрыты `CControl_SetVisible(…, 0)` в `CUpgradeWindow_Init`
//   и в разобранном срезе НИКОГДА не показываются. Подпись 43053 при этом РЕЛЕЙНАЯ: init ставит
//   loc 100602, а каждая перерисовка переписывает её на 100602 (сытость != 0) либо 101108
//   (сытость == 0) — `AsdaStory_game.c:411501-411505`. Подпись у них ОТДЕЛЬНЫЙ виджет
//   (`WkBtn_W100_148_txt` / `RdBtn_W100_153_txt`), правило «<кнопка>_Text» её не находит.
//
// ═════════════════════════════════ СТАТУС-ПЛАШКА 43200 ═══════════════════════════════════════
//   Показывается, только когда питомец ПРИЗВАН (`refineWindow_Open@0xDC6FE0`: нет призванного ⇒
//   `sub_92FC88` прячет). Плюс автопоказ: `inventoryAutoUpdateThrottled2s@0xDCC170`
//   (`item_inventory.c:54624`) раз в 2000 мс — если призванный есть, а плашка спрятана, показать.
//   Содержимое (`refineWnd_RefreshSelectedPanel`): 43201 имя · 43202 «стадия/(MaxLevel)» ·
//   43205 иконка породы · 43203 полоса опыта · 43204 полоса сытости.
//   ОДИНАРНОЕ нажатие ЛКМ по иконке 43205 открывает окно 43000 (`item_enhance.c:4005-4008`,
//   ветка `case 0xBu` — см. доказательство про событие 11 ниже).
//
// ══════════════════════════════════ ВЫБРОС ПИТОМЦА (6110) ═════════════════════════════════════
//   ДВА пути, оба через msgbox 55 (текст loc 100711, кнопки 100601/100406):
//     • клик по корзине 43051 — команда 66561 (`item_enhance.c:3975`);
//     • ПЕРЕТАСКИВАНИЕ питомца из слота на корзину: событие 5 (LMB down) взводит захват
//       (`refineWndSetSelectedItem` возвращает 2 ⇒ движок рождает призрак), событие 7 (LMB up)
//       несёт КОНТРОЛ ПОД КУРСОРОМ шестым аргументом, и если это 43051 — тот же msgbox
//       (`refineWndOnCloseRefresh`, `item_enhance.c:3844-3851`).
//
// ═════════════════════════ КОДЫ СОБЫТИЙ JUI — ЧТО ЗДЕСЬ ПРИНЯТО ═══════════════════════════════
//   Доказаны раскладкой `CControl_On*` (`AsdaStory_game.c:986787-987110`):
//     5 = LMB down (возврат 2 ⇒ призрак захвата), 7 = LMB up (a6 = контрол под курсором), 21 = Esc.
//
//   ★★★ 11 — ЭТО ОДИНАРНОЕ НАЖАТИЕ ЛКМ, А НЕ ДВОЙНОЙ КЛИК. Имя `CControl_OnLButtonDblClick`
//   (`ui_core.c:107474-107489`) — МИСНЕЙМ, и доказывается это НЕ именем, а БИТАМИ СОСТОЯНИЯ:
//     • эта функция — ЕДИНСТВЕННЫЙ во всём бинаре писатель флага `0x400` в состояние ввода
//       (`*(a3+1972) |= 0x400u`, `AsdaStory_game.c:986983`; других писателей ЭТОГО поля нет);
//     • `CControl_OnLButtonDown` (`ui_core.c:107277-107304`) ПЕРВОЙ же строкой отказывается
//       работать без `0x400` (`if ((*(a3+1972) & 0x400) == 0) return 0;`) и только потом
//       поднимает 5 и 6.
//   ⇒ 11 обязано срабатывать ПЕРЕД КАЖДЫМ обычным нажатием ЛКМ, иначе нажатий не было бы вовсе.
//   Симметрично 14 (`OnRButtonDblClick`, `ui_core.c:107522-107536`) = обычное нажатие ПКМ.
//
//   ★★★ 8/9/10 — ЭТО НАВЕДЕНИЕ, и тоже доказано механикой, а не именами. Насос
//   `uiContainerHandleMouse` (`AsdaStory_game.c:987273-987302`) зовёт `OnRButtonDown`/`OnRButtonUp`
//   в ветке «курсор НАД планом» (хит-тест `vtable+39`), а `OnRButtonClick` — в ветке «НЕ над»;
//   сами функции (`ui_core.c:107350-107412`) состояние кнопок мыши не проверяют вообще, только
//   защёлки `0x80`/`0x100`. То есть 8 = «наведение вошло», 9 = «держится», 10 = «ушло».
//   Настоящие `OnMouseEnter`/`OnMouseLeave` поднимают 1 и 2 с `a2 == −1` (`ui_core.c:107229, :107259`),
//   то есть БЕЗ номера контрола, и для пооконного тултипа непригодны.
//   ★★★ Событие 14 (`uiRefineWndSelectSlot` — выбрать слот И показать панель 43058) = **ПРАВЫЙ
//   КЛИК ПО СЛОТУ**. Доказательство — не догадка, а САМ ТЕКСТ, который окно печатает игроку в
//   подсказке 43063: loc **109955** = «View pet options by right clicking a Pet in storage»
//   (`Donors\AsdaChroniclesInterlude_MINE\system\error list.txt`; сверено вторым методом с RU-строкой
//   из `error list.bin` — «Чтобы открыть настройки питомца…»). «Pet options» — это ровно пять
//   кнопок 43058: Summon / Cancel summon / Pet limit break / Pet evolve / Close.
//   Следствие: событие 11 (одинарная ЛКМ) — «только выбрать», событие 14 (одинарная ПКМ) —
//   «выбрать и открыть панель».
//
// ═══════════════════════════════════ ЧЕГО ЗДЕСЬ НЕТ ══════════════════════════════════════════
//   • (ПРЕВЬЮ 43052 — СДЕЛАНО ВОЛНОЙ 9, см. раздел ниже; прежняя запись «контрол не трогаем
//     вовсе» СНЯТА: она опиралась на ложное «RT общий с гачей».)
//   • ОКНА 42000/43500/44200/43300/43400/43450 — волны 3-6. Точки входа отсюда готовы.
//   • НУМПАД. Доказано двумя методами, что пет-окна его не зовут (`Docs/Port/systems/NUMPAD_FLOW.md`,
//     отчёт §5): в `item_enhance.c` и `skill_buff.c` вхождений `g_pNumPadDialog` НЕТ.
//     Здесь его нет и не должно быть.
//
// ══════════ ★★★ ПРЕВЬЮ ПИТОМЦА 43052 И ЕГО ВРАЩЕНИЕ — ВОЛНА 9 (21.09.2026) ════════════════════
//   ПОЧЕМУ ВЛАДЕЛЕЦ ИМЕННО ЗДЕСЬ. В эталоне render-target питомца принадлежит ОБЪЕКТУ ОКНА 43000:
//   рисующая функция `CPetWindow_RenderToTexture@0xDC6910` — его метод, и три COM-объекта буфера
//   лежат в полях этого же объекта (`+0x274/+0x278/+0x27C`). Поэтому и в порту хозяин превью —
//   подсистема окна 43000, а не отдельная сущность.
//
//   ⚠ ПРЕЖНЯЯ ЗАПИСЬ БЫЛА НЕВЕРНА: «эталон рисует питомца методом `CGachaScene`
//   (`PetWindow_RenderToTarget@0xB6D5B0`)». Это МИСНЕЙМ — та функция принадлежит ГАЧЕ и рисует
//   её текстуру "PetWindow2". У питомца свой буфер "PetWindow" 240×170. Полный разбор и
//   доказательство — в шапке `UI/AsdaPreviewSceneActor.h`.
//
//   ★ ЧТО В ОРИГИНАЛЕ ДЕЙСТВИТЕЛЬНО ОБЩЕЕ: одна текстура "PetWindow" подставляется в ДВА плана —
//   43052 этого окна (`npc_mob_pet.c:5602`) и 43576 окна 43500 (`:5605`), с одним handle и одним
//   UV-регионом. Поэтому наружу отдан `GetPreviewBrush()`: окно 43500 берёт ТУ ЖЕ кисть, и
//   вращение, сделанное здесь, видно и там. Своей камеры у 43576 в оригинале нет.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaSessionReset.h"      // IAsdaSessionResettable
#include "AsdaPetCageSubsystem.generated.h"

// ── Предобъявления. Окно 43000 собрано из импортированных JUI-узлов, поэтому типы движка нужны
//   только указателями: полные заголовки тянет `.cpp`, а шапка остаётся дешёвой. ──
class UUserWidget;         // сами три виджета окна: клетка 43000, панель 43058, плашка 43200
class UWidget;             // обобщённый узел — им ходит поиск контролов по имени
class UImage;              // планы JUI: иконки слотов, полосы сытости/опыта, скины кнопок
class UButton;             // кнопки, которым нужен настоящий Slate-клик (а не покадровый опрос)
class UTextBlock;          // подписи: кличка, уровень, числа полос
class UEditableText;       // поле ввода клички (лимит 14 символов, см. `kNameMaxChars`)
class UCanvasPanel;        // канвас-хозяин: в него кладутся клоны и «призрак» перетаскивания
class UAsdaButtonRelay;    // мостик hover/press: состояния подписи кнопки живут через него
class UAsdaPetSubsystem;   // модель данных — единственный источник списка питомцев
struct FAsdaPetSlot;       // одна запись питомца из модели (порода, кличка, уровень, сытость)

// Виджеты ОДНОГО слота списка 43140+i: план-иконка `Pet_Cage_PetNN_<juiIdx>_0` и одноимённый
//   `TextBlock` (в оригинале подпись слота пустая — эталон в неё ничего не пишет).
USTRUCT()
struct FAsdaPetCageCell
{
	GENERATED_BODY()
	// Иконка ячейки (атлас JUI, вырезка 32×32 по UV строки таблицы).
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;
	// Подпись ячейки под иконкой.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;
};

// Полоса JUI: план «жёлоб» (`*_0`) и план «заливка» (`*_1`). Базовый отступ и полная ширина
//   снимаются с жёлоба ОДИН раз при разборе, дальше заливка режется долей.
USTRUCT()
struct FAsdaPetCageGauge
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage> Track = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage> Fill  = nullptr;
	float BaseLeft = 0.f;   // левый отступ жёлоба, снятый при разборе: от него отсчитывается заливка
	float FullW    = 0.f;   // полная ширина жёлоба = 100 %; ширина заливки = `FullW * доля`
};

UCLASS()
class ASDAREBORN_API UAsdaPetCageSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подъём подсистемы: зависимость от менеджера окон, заготовка массивов ячеек и рядов,
	//   закрытие по Esc и подписка на события модели. Виджеты здесь НЕ создаются — они рождаются
	//   лениво при первом открытии окна (`EnsureCage`), потому что `.JUI` разбирать дорого.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Окно, панель, плашка и выбор принадлежат СЕССИИ: сокет пересоздан — данных о питомцах нет.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Кадр окна: слив событий модели, покадровый опрос мыши (`TickSlots`/`TickTooltips`/
	//   `TickStatusIcon`) и отсчёт засова «запрос в полёте».
	virtual void Tick(float DeltaTime) override;
	// Обязательная метка профилировщика для `FTickableGameObject` — своей работы не несёт.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPetCageSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна. Все из `.JUI` и `CUpgradeWindow_Init`, ни одно не выдумано ──
	/** Слотов ровно 30: `for (slotIdx = 0; slotIdx < 30; ++slotIdx)` (item_enhance.c:4082). */
	static constexpr int32 kSlots = 30;
	/** Рядов пять по шесть: ListBase0 всегда видим + четыре контейнера 43020…43023. */
	static constexpr int32 kSlotsPerRow = 6;
	static constexpr int32 kRowContainers = 4;   // сами контейнеры 43020…43023 (ряд 0 показан всегда)
	/** Сторона иконки слота: `CControl_DrawTextureRect(…, 32, 32)` (AsdaStory_game.c:411461). */
	static constexpr int32 kCellPx = 32;
	/** Лимит поля имени: `ctrlEditSetTextLength(pNameEditBox, 14)` (item_enhance.c:4094-4095). */
	static constexpr int32 kNameMaxChars = 14;
	/** Период автопоказа плашки 43200: `timeGetTime() - last > 0x7D0` (item_inventory.c:54627). */
	static constexpr float kStatusPollSec = 2.0f;
	/** z-порядок: клетка над инвентарём (720), панель действий над клеткой, плашка — HUD. */
	static constexpr int32 kCageZ   = 732;
	static constexpr int32 kPanelZ  = 742;   // панель 43058 — НАД клеткой, иначе её съест сама клетка
	static constexpr int32 kStatusZ = 705;   // плашка 43200 — слой HUD, НИЖЕ окон: она не модальна

	// ═══════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ОКНА 43000 ═══════════════════════════════
	/** ПОЛНЫЙ порт `CRefineWnd_Open@0xDC3390`: тумблер + четыре гейта.
	 *  Возвращает false, если окно не открылось (тумблер закрыл его либо сработал гейт). */
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	bool OpenCage();

	/** Порт `uiCloseWindowReset@0xDC3500`: гасит окно, снимает фокус с поля имени, прячет панель. */
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	void CloseCage();

	// Открыто ли 43000. Читают гейты соседних окон: 43500 не откроется поверх (item_enhance.c:3817).
	UFUNCTION(BlueprintPure, Category="Asda|Питомцы")
	bool IsCageOpen() const { return bCageOpen; }

	// ═══ ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №4 (25.09.2026): ДОЧЕРНИЕ ОКНА НЕ ЗАКРЫВАЮТ 43000 ═══════════════
	//
	// ТРЕБОВАНИЕ ВЛАДЕЛЬЦА ДОСЛОВНО: «ОКНА РАЗВИТИЯ (44200) И ЭВОЛЮЦИИ (43500) НЕ ДОЛЖНЫ
	//   ЗАКРЫВАТЬ ОКНО ПИТОМЦА 43000».
	//
	// ★★★ ЭТО ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА, И ОНО ДОКАЗАНО ПО ОБОИМ ОКНАМ (закон `fix-original-defects`).
	//   Оба открывателя заканчиваются ОДНИМ И ТЕМ ЖЕ вызовом:
	//     • 43500: `uiPetDialog_Open@0xEDD450` → `uiCloseWindowReset((CControl **)g_bDeviceReloadPending)`
	//              (`ui_core.c:89976`);
	//     • 44200: `uiUpgradeDialog_Open@0xEE18E0` → тот же вызов (`ui_core.c:90627`).
	//   ЧТО ИМЕННО ЗАКРЫВАЕТ ЭТОТ ВЫЗОВ — доказано ТРЕМЯ независимыми способами, потому что оба
	//   имени в декомпиле МИСНЕЙМЫ (законы `false-negatives-method`, `net-getcurrentmaptype-is-misname`):
	//     1) АДРЕС. `g_bDeviceReloadPending` = глобаль `byte_2C2EFD0`
	//        (`RE/ida/export/globals_ranked.tsv:43`), а её поле `+0x2F4` = `dword_2C2F2C4`
	//        (0x2C2EFD0 + 756 = 0x2C2F2C4) — то самое, что читает гейт подсказки 43300.
	//     2) ПРИВЯЗКА. Объект по этому адресу биндит контрол **43000**:
	//        `this->pCloseBtn = CWindowMgr_FindControlByID(g_pWindowMgr, 43000)` (`item_enhance.c:4064`),
	//        поле `+0x08` (`structs_ru.h`, `struct CPetData`), а `uiCloseWindowReset@0xDC3500`
	//        гасит ровно `*(this + 2)` = `+0x08` (`ui_core.c:64926-64933`).
	//     3) ВТОРОЙ ВЫЗЫВАЮЩИЙ. Та же функция стоит обработчиком кнопки закрытия 43002
	//        (`item_enhance.c:3945`) и события окна 21 «закрыть» (`item_enhance.c:3982`).
	//   ⇒ В ОРИГИНАЛЕ 43000 ГАСНЕТ ПРИ ОТКРЫТИИ И 43500, И 44200. Требование владельца идёт
	//   ПРОТИВ оригинала осознанно, поэтому помечено и снабжено ключом отката.
	//
	// КЛЮЧ ОТКАТА: `asda.Pet.KeepCageOpen 0` → дословное поведение оригинала (закрывать 43000).
	//   Пара по духу к уже принятым отступлениям того же окна: `asda.Pet.CageCenterOnScreen`
	//   (позиция) и `asda.Pet.PreviewFaceCamera` (разворот превью).
	//
	// ТОЧКА ОДНА: оба дочерних окна зовут ЭТУ функцию, а не `CloseCage()` напрямую — иначе
	//   отступление пришлось бы поддерживать в двух местах и одно из них рано или поздно разошлось бы.
	//   `Who` — имя окна для лога («43500» / «44200»), ничего кроме лога не решает.
	void CloseCageForChildWindow(const TCHAR* Who);

	// ★ Поставить дочернее окно (43500 / 44200) РЯДОМ с открытой клеткой и ВЫШЕ неё по z.
	//   Нужно только из-за отступления выше: у оригинала эти окна никогда не видны вместе с 43000,
	//   поэтому и раскладка у них перекрывающаяся (43000 `334,223 512×385`, 44200 `329,185 366×398`,
	//   43500 `205,126 390×558` — базовые 1024×768 из дампов `Reports/pet/ui_dump/`).
	//   Возвращает true, если окно было подвинуто. Разбор и ключ отката — у определения.
	//   `MinReserveWidth` — ширина, которую окно займёт В САМОМ ШИРОКОМ своём виде. Нужна окну
	//   43500: оно открывается УЗКИМ (рама 390), а по первому же клику по стадии разворачивается
	//   до 776 (`kWinNarrowW`/`kWinWideW`) и без запаса влезло бы правым краем на клетку.
	bool PlaceChildBesideCage(FName ChildWindowId, class UUserWidget* ChildWidget,
		double MinReserveWidth = 0.0);

	// ★ Волна В4 ремесла (ревизия В3 С-2) — слот 10 снимка открытых окон (`uiCaptureOpenWindowsState`):
	//   предикат `sub_93DBDA(g_bDeviceReloadPending)` = `*(*(this+105)+292)` — видна ли плашка 43200 (`byte+1`).
	bool IsStatusPanelShown() const { return bStatusShown; }
	// Восстановление слота 10 — `refineWindow_Open@0xDC6FE0` (`uiDispatchWindowToggle` case 10): показать плашку,
	//   если есть призванный. Та же точка, что автопоказ (`OpenStatusPanel`), — второй копии нет.
	void ReopenStatusPanelFromSnapshot() { OpenStatusPanel(); }

private:
	// Заявка «поставить рядом на следующем кадре»: в кадре открытия живой геометрии корпуса ещё
	//   нет (Slate считает её в конце кадра), и спрашивать её сразу — та самая ловушка, от которой
	//   предостерегает закон `no-state-latch-before-shown`.
	static constexpr int32 kPendingBesideMaxFrames = 8;
	FName PendingBesideId = NAME_None;
	TWeakObjectPtr<UUserWidget> PendingBesideWidget;   // слабый — GC-владения не нужно
	int32 PendingBesideFrames = 0;
	double PendingBesideReserveW = 0.0;                // запас ширины заявки (см. MinReserveWidth)
	void TickPendingBeside();
public:

	// ★★★ ПРЕВЬЮ 43052: кисть кадра. ОКНО 43500 БЕРЁТ ЕЁ ЖЕ для своего контрола 43576 —
	//   в оригинале это буквально одна текстура в двух планах (`npc_mob_pet.c:5602` и `:5605`).
	//   nullptr, пока превью не поднято. В кисть контрола кладётся ИМЕННО материал, не сам RT.
	UFUNCTION(BlueprintPure, Category="Asda|Питомцы")
	class UMaterialInstanceDynamic* GetPreviewBrush() const;

	/** ★ САМ буфер превью 43052 — только для диагностики (`asda.Pet.PreviewDumpRt`): он разрезает
	 *  цепочку «капчер → RT → кисть → контрол» пополам и показывает, на каком её конце пусто. */
	class UTextureRenderTarget2D* GetPreviewRenderTarget() const;

	/** Размер кадра превью в пикселях — нужен окну 43500, которое берёт ТУ ЖЕ кисть для своего
	 *  контрола 43576 и собирает её заново (см. `AsdaUi::ApplySceneMaterialBrush`). */
	FVector2D GetPreviewRenderSize() const;

	/** Мастер-материал кисти превью — окно 43500 собирает свой контрол 43576 ТОЙ ЖЕ схемой, что и
	 *  43052 (мастер в кисть + параметр на экземпляре виджета). Разбор — `AsdaUi::ApplySceneMaterialBrush`. */
	class UMaterialInterface* GetPreviewBrushMaster() const;

	/** Подбор базового разворота модели в превью (команда `asda.Pet.PreviewFaceYaw`): число порта
	 *  снимается замером, потому что из эталона оно не выводится. Разбор - у `FacingYawDeg`. */
	void SetPreviewFacingYaw(float Deg);

	// Показать в превью питомца данной породы. Зовут ОБА окна: 43000 — по выбранному в списке,
	//   43500 — по цели/результату развития (эталон выбирает модель так же, `entity_char.c:7790-7800`).
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	void SetPreviewPet(int32 PetTypeId, const FString& PetName);

	/** Состояние превью 43052 для прогонов: сцена, порода, буфер, кисть, угол (`asda.Pet.PreviewState`). */
	FString DumpPreview() const;

	// ★ Окно 43500 заявляет, что кадр превью нужен ЕМУ. Нужно потому, что буфер один на два окна:
	//   без заявки закрытие 43000 погасило бы капчер, и 43576 остался бы с застывшей картинкой.
	//   Эталонный гейт рендера ровно такой же — «открыто 43000 ИЛИ открыт диалог развития»
	//   (`npc_mob_pet.c:5591`).
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	void HoldPreviewForEvolution(bool bHold);

	// Видна ли ПАНЕЛЬ ДЕЙСТВИЙ 43058 `Pet_Button_Win`. ★ Это гейт показа подсказки 43300 у
	//   эталона: `*(BYTE*)(dword_2C2F2C4 + 292) != 1` (`ui_core.c:90433`), где `dword_2C2F2C4` —
	//   поле `+0x2F4` окна 43000 с указателем на 43058, а `+292` — `CControl::isVisible`.
	//   Панель и подсказка ставятся в ОДНУ точку «курсор + 2», поэтому под открытой панелью
	//   подсказка не всплывает. Читатель — `UAsdaPetEvoTipSubsystem::ShowForPet`.
	UFUNCTION(BlueprintPure, Category="Asda|Питомцы")
	bool IsActionPanelShown() const { return bPanelShown; }

	/** ★ ЖИВОЙ ВИДЖЕТ 43000 — донор эталонных контролов для соседних окон набора.
	 *  Сегодня читатель один: `UAsdaPetEvoTipSubsystem::EnsureCloseButton` берёт отсюда кнопку
	 *  43002 `Pet_Cage_ClsBtn` и клонирует её крестиком закреплённого окна «О питомце» — так у
	 *  добавки порта ровно тот же скин, что у окон оригинала, и ассеты остаются нетронутыми.
	 *  ⚠ Только ЧТЕНИЕ: владение виджетом остаётся за этой подсистемой (`ResetCageWidget`). */
	UUserWidget* GetCageWidget() const { return Cage; }

	// ═══════════════════════════════ ДЕЙСТВИЯ (обработчики) ═══════════════════════════════════
	void OnSlotRightClicked(int32 SlotIndex);   // событие 0xE → выбрать + показать панель 43058
	void OnSlotLeftClicked(int32 SlotIndex);    // событие 0xB → только выбрать
	void OnTrashClicked();                      // 43051 → msgbox 55 → 6110
	void OnNameChangeClicked();                 // 43083 → фильтр брани → msgbox 57 → 6118
	void OnPanelCall();                         // 43065
	void OnPanelRecall();                       // 43066
	void OnPanelGrowth();                       // 43055 → окно 44200 (волна 4)
	void OnPanelEvolution();                    // 43056 → окно 43500 (волна 5)
	void OnPanelCancel();                       // 43057
	/** ★★★ ШЕСТАЯ КНОПКА ПАНЕЛИ — ДОБАВКА ПОРТА ПО ТРЕБОВАНИЮ ВЛАДЕЛЬЦА 25.09.2026.
	 *  В ОРИГИНАЛЕ ЕЁ НЕТ: `upgradeWindow_BindResultLabels@0xDCBDA0` (item_enhance.c:5117-5131)
	 *  биндит РОВНО ПЯТЬ кнопок (43065/43066/43055/43056/43057) и пять подписей (109950…109954);
	 *  живой дамп панели даёт те же пять (`Reports/pet/ui_dump/43058_Pet_Button_Win.md`,
	 *  6 узлов .JUI = корпус + 5 кнопок). ⇒ Номер 43067 НАШ, в данных .JUI его нет.
	 *  Кнопка открывает окно «О питомце» (43300 в закреплённом виде).
	 *  Откат: `asda.Pet.InfoWindow 0` — кнопки нет, панель прежней высоты, подсказка по наведению. */
	void OnPanelInfo();                         // ★ 43067 (наш номер) → окно «О питомце»
	/** ★ ОДНА ТОЧКА «окно О питомце, следуй за выбором» (требование владельца 25.09.2026).
	 *  Зовётся из трёх мест, где выбор может смениться: ЛКМ по слоту, ПКМ по слоту
	 *  и пачка серверных событий (там питомца могли выбросить). Разбор — у определения. */
	void FollowSelectionInInfoWindow();
	void OnStatusIconClicked();                 // 43205 → открыть 43000 (одинарная ЛКМ)

	// ═══════════ ★ ВОЛНА 6: СТЕНД ОТПРАВКИ 6108 / 6121 (образец — стенд 6148 волны 4) ══════════
	// ЗАЧЕМ. Сборщик пакета проверять его же кодом бессмысленно; стенд взводит перехват исходящих
	//   волны 1 (`UAsdaPetSubsystem::BeginSendCapture`), зовёт БОЕВОЙ путь игрока и разбирает
	//   снятое тело ПО СМЕЩЕНИЯМ СЕРВЕРА, мимо нашего сборщика. Пока перехват взведён, в сеть не
	//   уходит ничего, поэтому он снимается сам — по снятому телу либо по таймауту.
	/** Запустить стенд 6108. `bFromWindowSlot` — второй путь (кнопка окна, `useEnhanceSlot == 1`);
	 *  иначе первый путь (предмет subType 106 из сумки, `useEnhanceSlot == 0`). */
	void StartFeedStand(bool bFromWindowSlot, int32 TestCharId);
	/** Запустить стенд 6121 (расширитель ячеек, `special_itemlist.subType == 104` из сумки). */
	void StartSlotUpStand(int32 TestCharId);
	/** Слот списка под точкой ЭКРАНА (абсолютные координаты Slate). −1 — ни одного.
	 *  Общая точка покадрового опроса и диагностической команды `asda.Pet.WindowHitTest`. */
	int32 SlotUnderPoint(const FVector2D& AbsPos) const;

	/** ★ ВОЛНА 5: перерисовать список 43000 ИЗВНЕ. Порт `CRefineUI_RefreshItemPanel(g_PetData)`,
	 *  который приёмник 6113 зовёт сразу после юниона (npc_mob_pet.c:7682). Это обязательно:
	 *  после юниона сервер список 6106 НЕ шлёт (`sub_4590E0` отправляет только 6113,
	 *  GameServer.c:36115), три съеденных материала удаляет модель волны 1 — и без этого вызова
	 *  в клетке остались бы три фантомных питомца до следующего входа в мир.
	 *  Раньше метод был приватным; вынесен в public без изменения тела. */
	void RefreshItemPanel();

	// ═══════════════════════════════════ ДИАГНОСТИКА ══════════════════════════════════════════
	/** Каждый разобранный контрол трёх окон (закон `full-structural-analysis-always`). */
	FString DumpControls() const;
	/** Состояние: список, выбранный, призванный, ячейки, ряды, гейты. */
	FString DumpState() const;

private:
	// ── Жизненный цикл трёх виджетов ──
	//   Тройка на каждый виджет: `Ensure*` — создать и разобрать, если ещё нет (ленивое рождение,
	//   false = не удалось); `Reset*Widget` — забыть виджет и все указатели на его контролы, не
	//   трогая данные модели; `Setup*Controls` — разовый разбор `.JUI` в поля подсистемы.
	//   ⚠ Указатели на контролы ОБЯЗАНЫ обнуляться в `Reset*Widget`: виджет уничтожает движок,
	//   и переживший его указатель — висячий.
	bool EnsureCage();            // клетка 43000 — главное окно списка питомцев
	bool EnsurePanel();           // панель действий 43058 (призвать/отпустить/переименовать)
	bool EnsureStatus();          // плашка 43200 — состояние призванного, живёт на слое HUD
	void ResetCageWidget();       // забыть виджет 43000 и указатели на его контролы
	void ResetPanelWidget();      // то же для панели 43058
	void ResetStatusWidget();     // то же для плашки 43200
	void SetupCageControls();     // разобрать `.JUI` клетки: 30 ячеек, 4 контейнера рядов, поле имени
	void SetupPanelControls();    // разобрать панель: четыре кнопки действий и «Нет»
	/** ★★★ ВСТАВИТЬ ШЕСТУЮ КНОПКУ «О питомце» В ПАНЕЛЬ 43058 — ДОБАВКА ПОРТА (см. `OnPanelInfo`).
	 *  СПОСОБ: КЛОН уже импортированной кнопки `Pet_Button_3` вместе с её подписью
	 *  (`AsdaUi::CloneWidgetSubtree`) — тот же приём, которым оригинал размножает шаблоны
	 *  (`CControl_InstantiateChild`) и которым порт уже добавлял записи в трекер заданий 1800.
	 *  Ассет `WBP_Pet_Button_Win` НЕ ТРОГАЕТСЯ (`no-ui-regeneration-without-permission`,
	 *  `tools-no-import-side-effects`: переимпорт .JUI стёр бы дописанный контрол).
	 *  ПОБОЧНОЕ: корпус панели растёт по высоте ровно на один шаг кнопки, а серая «Закрыть»
	 *  съезжает вниз — иначе новая кнопка легла бы на неё.
	 *  Возврат: true — кнопка есть (или уже была). */
	bool BuildPanelInfoButton();
	void SetupStatusControls();   // разобрать плашку: иконка, кличка, полосы сытости и опыта

	// ── Перерисовка ──
	/** Порт `cRefineUI_ResetPanel@0xDC45A0` — гасит иконки, ряды и все значения. */
	void ResetPanel();
	/** Порт `CItemRefineUI_RefreshItemPanel@0xDC48C0` — весь правый блок и список.
	 *  ★ Волна 5 вынесла объявление в public (нужен приёмнику 6113); здесь его больше нет. */
	/** Порт `refineWnd_RefreshSelectedPanel@0xDC70B0` — плашка 43200. */
	void RefreshStatusPanel();
	/** Порт `sub_92FC88@0xDC7070` — спрятать плашку 43200. */
	void HideStatusPanel();
	/** Порт `refineWindow_Open@0xDC6FE0` — показать плашку, если есть призванный. */
	void OpenStatusPanel();
	/** Порт `upgradeWindowShow@0xDCBF50` — показать панель 43058 у курсора. */
	void ShowActionPanel();
	/** Порт `sub_93DB58@0xDCC010` — спрятать панель 43058. */
	void HideActionPanel();
public:
	// ★ 07.11.2026 тест-обёртки самотеста щелчка мимо меню.
	void DebugShowActionPanel() { ShowActionPanel(); }   // показать панель 43058 у курсора
	void DebugHideActionPanel() { HideActionPanel(); }   // спрятать панель 43058
private:

	// ★ ВОЛНА 4: наведение на слот списка — порт `refineGetSlotItemState@0xDC7550`
	//   (item_enhance.c:3996 зовёт его на событии 8). `SlotIndex < 0` — курсор ушёл со списка.
	void UpdateEvoTipForHover(int32 SlotIndex);

	// ── Мышь (в порту событий JUI нет, курсор опрашивается покадрово) ──
	// Поднять актор превью, если его ещё нет. nullptr — мира нет (редактор/ранний кадр).
	class AAsdaPetPreviewActor* EnsurePreview();
	// Кадр превью: ЛКМ-вращение по эталону (П8-1) и подстановка кисти в 43052.
	void TickPreview();

	void TickSlots();        // сетка 30 ячеек: наведение, выбор, ЛКМ/ПКМ, перетаскивание иконки
	void TickTooltips();     // подсказки по наведению (в т.ч. прогретая 43300)
	void TickStatusIcon();   // иконка плашки 43200: автопоказ раз в `kStatusPollSec`

	// ── Данные ──
	UAsdaPetSubsystem* Pets() const;
	/** Строка клиентской `pettable` по породе. false — породы нет.
	 *  ★ Возврат ПО ЗНАЧЕНИЮ: указатель на статик пережил бы только до следующего вызова, а окно
	 *  развития (волна 5) держит сразу четыре строки. */
	bool PetRow(int32 PetIndex, struct FAsdaPetRow& Out) const;
	/** Строка клиентской `petexptable` ПО ПОЗИЦИИ (эталон адресует её `база + 44 * позиция`).
	 *  ⚠ В данных `Level = позиция + 1`, поэтому ищем строку с `Level == Pos + 1`. false — нет. */
	bool ExpRow(int32 Pos, struct FAsdaPetexptableRow& Out) const;
	/** Шаблон подписи бонуса из `petoptiontable`. Пусто — записи нет. */
	FString OptionFormat(int32 OptionId) const;
	/** Доля заполнения полосы опыта (0..1) и её процент — формула эталона 1:1. */
	bool ExpFraction(const FAsdaPetSlot& S, float& OutFrac) const;

	/** Питомец, который сейчас ВЫБРАН в окне (эталонный `wnd[154]`). */
	const FAsdaPetSlot* Selected() const;
	/** Питомец, который сейчас ПРИЗВАН (эталонный `wnd[155]`). */
	const FAsdaPetSlot* Active() const;
	/** Сколько записей списка показывается: эталон обрывает обход на `PossessionCount`. */
	int32 VisibleCount() const;

	// ── Нотисы. ВСЕ loc-id сняты по ОБРАБОТЧИКУ (закон `loc-per-handler-not-by-family`) ──
	FString Loc(int32 LocId) const;
	// Нотис окна — переходник на общую точку `AsdaPetUi::NoticeMsgBox` (ONEBUT_MSGBOX 1500).
	void NoticeMsgBox(int32 LocId, int32 EtalonNoticeId = -1) const;   // id — для очереди окон-сообщений (−1; 7009 у мат-фильтра)

	// ── Реакция на пакеты ──
	void OnPetEvent(int32 Opcode, int32 Result, int32 PetUniqueId);

	/** ★ ВОЛНА 6 — приём 6109 (кормление). Порт `netOnRefineResultPacket@0xDC9710`
	 *  (network.c:25868-25986) в части ОКНА: нотисы по коду и, на коде 1, применение веса и
	 *  записи корма в сумку. Сытость и три бонуса записи питомца уже применила модель волны 1.
	 *  Имя функции эталона врёт («refine»): `this` у неё — `CPetData`, то есть это окно 43000. */
	void OnFeedResult6109(int32 Result, int32 PetUniqueId);

	/** ★ ВОЛНА 6 — приём 6122 (расширение ячеек). Порт
	 *  `refineWndOnRefineResultPacket@0xDCB9E0` (item_enhance.c:5015-5057): нотисы 101116/101114/
	 *  101115 и, на коде 1, вес + очистка ячейки расширителя. Счётчик ячеек пишет модель волны 1. */
	void OnSlotUpResult6122(int32 Result);

	/** ★ ВОЛНА 6 — общая точка инвентаря для обоих приёмников: сумка хранится инвентарём, а
	 *  применять её из двух мест подряд нельзя (пересборка кэша хотбара идёт на каждую ячейку).
	 *  Возвращает подсистему инвентаря или `nullptr`. */
	class UAsdaInventorySubsystem* Inv() const;

	/** Разбор снятого тела стенда по серверным смещениям. Возвращает true, если всё сошлось. */
	bool CheckStandBody(int32 Opcode, const TArray<uint8>& Body, TCHAR Envelope) const;

	bool  bStandArmed = false;      // перехват взведён
	bool  bStandDone  = false;      // тело снято — перехват пора снимать
	float StandLeft   = 0.f;        // сколько секунд ждём тело
	int32 StandOpcode = 0;          // какой опкод ждём: 6108 или 6121
	int32 StandExpectUid  = -1;     // 6108: ожидаемый petUniqueId в теле
	int32 StandExpectBag  = -1;     // ожидаемая сумка предмета
	int32 StandExpectSlot = -1;     // ожидаемая ячейка предмета
	/** Отложенная обработка события: сам пакет приходит из сети, окно перерисуется в `Tick`. */
	void DrainPetEvents();

	// ═══════════════════════════════════ СОСТОЯНИЕ ════════════════════════════════════════════
	// ── Окно 43000 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Cage = nullptr;
	UPROPERTY(Transient) TArray<FAsdaPetCageCell> Cells;                  // 30 × 43140+i
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowGroups;           // 4 × 43020…43023
	UPROPERTY(Transient) TObjectPtr<UWidget>    SelMarker    = nullptr;   // 43050
	UPROPERTY(Transient) TObjectPtr<UWidget>    SelMarkerHost= nullptr;   // родитель маркера
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose     = nullptr;   // 43002
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnTrash     = nullptr;   // 43051
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnName      = nullptr;   // 43083
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle     = nullptr;   // 43001  loc 100601
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtListTitle = nullptr;   // 43005  loc 100600
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNameLabel = nullptr;   // 43070  loc 100604
	UPROPERTY(Transient) TObjectPtr<UEditableText> EdtName   = nullptr;   // 43071  лимит 14
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStageLbl  = nullptr;   // 43072  loc 100605
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStage     = nullptr;   // 43073  «стадия/(N)»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtExpLbl    = nullptr;   // 43074  loc 100606
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtExp       = nullptr;   // 43076  «NN.NN%»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGrowLbl   = nullptr;   // 43077  loc 100607
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGrow      = nullptr;   // 43079  (эталон не пишет)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNutLbl    = nullptr;   // 43080  loc 100608
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNut       = nullptr;   // 43082  «NN.NN%»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStat1Lbl  = nullptr;   // 43100  loc 100609
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStat1     = nullptr;   // 43101
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStat2Lbl  = nullptr;   // 43102  loc 100610
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStat2     = nullptr;   // 43103
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStat3Lbl  = nullptr;   // 43104  loc 100611
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStat3     = nullptr;   // 43105
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFuncLbl   = nullptr;   // 43106  loc 100612
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOpt0      = nullptr;   // 43107
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOpt1      = nullptr;   // 43108
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOpt2      = nullptr;   // 43109
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGradeLbl  = nullptr;   // 43110  loc 101123
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGrade     = nullptr;   // 43111
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFeedLbl   = nullptr;   // 43112  loc 101122
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTaste0    = nullptr;   // 43113
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTaste1    = nullptr;   // 43114
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTaste2    = nullptr;   // 43115
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHint      = nullptr;   // 43063  loc 109955
	UPROPERTY(Transient) TObjectPtr<UWidget>    Grp43061     = nullptr;   // родитель 43053
	UPROPERTY(Transient) TObjectPtr<UWidget>    Grp43062     = nullptr;   // родитель 43054
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Txt43053     = nullptr;   // подпись 43053 (реле)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Txt43054     = nullptr;   // подпись 43054
	UPROPERTY(Transient) FAsdaPetCageGauge GaugeExp;                      // 43075
	UPROPERTY(Transient) FAsdaPetCageGauge GaugeNut;                      // 43081

	// ── Панель 43058 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Panel = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCall     = nullptr;    // 43065 loc 109950
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnRecall   = nullptr;    // 43066 loc 109951
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnGrowth   = nullptr;    // 43055 loc 109952
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnEvolution= nullptr;    // 43056 loc 109953
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnPanelNo  = nullptr;    // 43057 loc 109954
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCall     = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtRecall   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGrowth   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtEvolution= nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPanelNo  = nullptr;
	// ★ Шестая кнопка «О питомце» — КЛОН `Pet_Button_3`, рождается в `SetupPanelControls`
	//   (в ассете её нет; разбор и откат — у `OnPanelInfo`).
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnInfo     = nullptr;    // ★ 43067, loc 990347
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfo     = nullptr;    // ★ подпись клона

	// ── Плашка 43200 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Status = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStatusName  = nullptr; // 43201
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStatusStage = nullptr; // 43202
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgStatusIcon  = nullptr; // 43205
	UPROPERTY(Transient) TObjectPtr<UWidget>    StatusIconHit  = nullptr; // зона клика по 43205
	UPROPERTY(Transient) FAsdaPetCageGauge GaugeStatusExp;                // 43203
	UPROPERTY(Transient) FAsdaPetCageGauge GaugeStatusNut;                // 43204

	// Посредники `OnClicked` настоящих `UButton` окна: живут столько же, сколько и окно.
	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (МЕЛК-6): МОСТИКИ РАЗДЕЛЕНЫ ПО ВЛАДЕЛЬЦАМ.
	//   БОЛЕЗНЬ. Массив был ОДИН на два разных виджета — окно 43000 и панель 43058, — а
	//   `Reset()` не звался НИГДЕ. Каждый пересбор любого из двух окон добавлял мостики поверх
	//   мёртвых, и массив рос всю сессию. Сбросить общий массив в `ResetPanelWidget` было
	//   нельзя: он унёс бы и живые мостики клетки.
	//   ЛЕЧЕНИЕ. Два массива по владельцам, каждый чистится СВОИМ сбросом. Дефект существовал
	//   до этой волны, но волна добавляла шестой мостик на каждый пересбор панели — чиним здесь.
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;        // мостики окна 43000
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> PanelRelays;   // мостики панели 43058

	// ── Модель окна (эталонные поля) ──
	bool bCageOpen      = false;   // `gap_0[0]` — окно 43000 открыто
	bool bStatusShown   = false;   // `byte + 1`  — плашка 43200 видна
	bool bPanelShown    = false;   // видимость 43058
	bool bCageReady     = false;   // контролы 43000 разобраны
	bool bPanelReady    = false;   // контролы 43058 разобраны
	bool bStatusReady   = false;   // контролы 43200 разобраны

	// ── Превью 43052 (волна 9) ──
	/** Контрол-план, в который кладётся кадр превью (`PetCage_Preview_Win`). */
	UPROPERTY(Transient) TObjectPtr<class UImage> ImgPreview3d = nullptr;
	/** Актор сцены превью: свой render-target 240×170, модель питомца и угол вращения. */
	UPROPERTY(Transient) TObjectPtr<class AAsdaPetPreviewActor> Preview = nullptr;
	/** Кисть уже подставлена в 43052 — второй раз не ставим (материал переживает кадры). */
	bool bPreviewBrushSet = false;
	/** Кадр превью держит окно 43500 (см. `HoldPreviewForEvolution`): гасить капчер нельзя. */
	bool bEvoUsesPreview = false;
	bool bConfirmOpen   = false;   // модальное подтверждение уже открыто — второго не открываем
	/** ★ ЗАДЕЛ ВОЛНЫ 4: «пришёл пакет — перерисовать в `Tick`». Сейчас перерисовка идёт сразу в
	 *  `DrainPetEvents` (пачкой), поле держится для окон, которые будут метить себя из колбэков
	 *  инвентаря (как это делает окно ремонта). Читателя пока нет. */
	bool bDirty         = false;
	/** `g_LastShownUserSerial` эталона: чей питомец сейчас показан в поле имени 43071. */
	int32 NameShownForUid = -1;
	/** Слот под курсором (−1 — нет). ★ ВОЛНА 4 (21.09.2026): задел использован — потребитель
	 *  появился, это окно 43300 (`UAsdaPetEvoTipSubsystem`). СМЕНА значения = эталонные события
	 *  8 (наведение вошло) и 0xA (ушло), см. `UpdateEvoTipForHover`. */
	int32 HoverSlot = -1;
	/** По какой полосе показана двухстрочная подсказка: 43203 / 43204 / −1. */
	int32 TipBar = -1;
	/** Секунды до следующего опроса автопоказа плашки (`inventoryAutoUpdateThrottled2s`). */
	float StatusPollLeft = 0.f;
	/** Авторская позиция маркера 43050 из `.JUI` (ряд 0, столбец 0). −1 — ещё не снята.
	 *  Якорь таблицы смещений эталона: ряд 0 обязан встать ровно туда, где маркер нарисован. */
	FVector2D MarkerAnchor = FVector2D(-1.0, -1.0);

	// Покадровый опрос мыши и фронты кнопок — общая точка `AsdaUiHelpers`.
	FAsdaMouseFrame Mouse;
	// ★ `FAsdaDragArm` = защёлка нажатия `FAsdaPressLatch` (ключ слота в `KeyA`) ПЛЮС порог
	//   перетаскивания. То есть ЛЕВАЯ пара «нажал/отпустил» у этого окна живёт в ТОЙ ЖЕ общей
	//   точке, что и у 43500/42000 — третьей реализации, найденной ревизией волны 8, больше нет.
	FAsdaDragArm    Drag;          // захват иконки питомца ради сброса на корзину 43051
	// ★★★ ПОЛЕ `RmbPress` УБРАНО 24.09.2026 ВМЕСТЕ С «ПАРОЙ ПРАВОЙ КНОПКИ».
	//   Волна 8 завела его, прочитав флаги `0x80`/`0x100` (`ui_core.c:107388-107396`) как парность
	//   ПКМ. Это миснейм: флаги ставит `CControl_OnRButtonDown`, которая НЕ читает состояние
	//   кнопок мыши и вызывается из ветки «курсор над планом», а окно 43000 вешает на эти события
	//   подсказку (`item_enhance.c:3993-4005`) — то есть это защёлка НАВЕДЕНИЯ. Само событие ПКМ
	//   (0xE, `ui_core.c:107522-107535`) их не проверяет вовсе, пары у правой кнопки в оригинале
	//   НЕТ. Защёлка не просто была лишней — она гасила клик (полный разбор у ветки `Mouse.bRmbDown`
	//   в `TickSlots`). Ссылки на неё в `OpenCage`/`CloseCage`/`Deinitialize` сняты там же.
	/** ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-1): ПАРА НАЖАТИЯ ДЛЯ ИКОНКИ 43205 ПЛАШКИ 43200.
	 *  До неё окно 43000 открывалось по одному лишь фронту отпускания над иконкой, без принятого
	 *  нажатия, и вдобавок сверка шла `IsCursorOverWidget` в кадре, где плашку могли перевесить
	 *  во вьюпорте (у 43200 есть ручка `Pet_Status_Win_0_DRAG`). Разбор — у `TickStatusIcon`. */
	FAsdaPressLatch StatusIconPress;
	/** Ключ защёлки выше — номер JUI иконки породы в плашке 43200 (узел один, второго ключа нет). */
	static constexpr int32 kStatusIconKey = 43205;

	/** Очередь событий модели (закон `no-reentrant-net-callbacks`: из колбэка только кладём). */
	TArray<TTuple<int32, int32, int32>> PetEvents;
	// Подписка на события модели питомцев; снимается в `Deinitialize`.
	FDelegateHandle HPetEvent;
};
