// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// UAsdaPetGrowthSubsystem — ОКНО 44200 `Pet_LimitBreak_Win` «Рост питомца» (волна 4, 21.09.2026).
//
// Эталонный класс — `CUpgradeDialogTradePanel` (глобальный объект `g_pUpgradeDialog`). Его
// методы: `uiInitTradeWindow@0xEE12F0` (ui_core.c:90495) — привязка контролов и подписи;
// `uiUpgradeDialog_Open@0xEE18E0` (:90605) — открытие с двумя гейтами;
// `uiUpgradeDialog_RefreshItemInfo@0xEE2A70` (:90700) — ПЕРЕРИСОВКА окна;
// `uiUpgradeDialogRefreshItemInfo@0xEE17B0` (:90565) — ★ ДРУГАЯ функция: ПРОВЕРКА по «ОК»;
// `uiUpgradeDialogWndProc@0xEE2FB0` (:90788) — события; `net_sendTradeConfirmRequest@0xEE1C80`
// (network.c:32791) — отправка 6148; `pet_OnRecvItemOpResult@0xEE2370` (npc_mob_pet.c:7691) —
// приём 6149.
//
// ══════════════════ ★★★ ЧТО ЗДЕСЬ ИСПРАВЛЕНО ПРОТИВ ПРЕЖНИХ МАТЕРИАЛОВ ═════════════════════════
//   И1. **44205/44206 показывают УРОВЕНЬ (`Level`), а НЕ предел (`MaxLevel`).** Перерисовка пишет
//       `sprintf("%d", v12[22][20])` в 44205 и `+1` в 44206 (ui_core.c:90739-90743), а
//       `v12[22]` — указатель на `CPetSlot`, dword-индекс 20 = БАЙТ +80. Что именно лежит в +80,
//       доказано ВТОРЫМ методом — приёмником 6149: `itemRecord[20] = HIBYTE(itemOpPayload[5])`,
//       то есть байт 23 тела = `PetRecord+19` = **Level** (npc_mob_pet.c:7735). Предел же
//       (`PetRecord+20`) едет в `itemRecord[27]` = байт +108 = 0x6C (:7737). Ровно те же два
//       поля читает гейт открытия: `*(int*)(a2+80) < 10` — это «уровень < 10», а
//       `*(int*)(a2+80) >= *(int*)(a2+108)` — это «Level >= MaxLevel» (:90621, :90631-90632).
//       ⇒ Подписи 110650 «Текущий уровень» / 110651 «Изменение уровня» относятся к УРОВНЮ.
//   И2. **Чекбокс 44241 при наличии талисмана не «показывается», а АВТОМАТИЧЕСКИ ВЗВОДИТСЯ.**
//       `CControl_SetVisibleState` (ui_core.c:117907) пишет `m_bToggleState`, то есть СОСТОЯНИЕ
//       ГАЛКИ, а не видимость. Открытие делает `if (count209 > 0) SetVisibleState(44241, 1)`
//       (:90635-90639), перерисовка снимает галку при `count209 <= 0` (:90754-90755), а события
//       67073/67074 ставят/снимают её вручную (:90806-90815). Видимость 44241 в окне постоянная.
//   И3. **Веток `switch` у приёмника 6149 — СЕМЬ (+`default`), а кодов в них СЕМНАДЦАТЬ (0…0x10).**
//       Прежняя формулировка «13 веток» не подтвердилась: группы ровно такие —
//       `0` · `1,2,3,4` · `5,6,7,8,0xC,0xD,0xE,0xF` · `9` · `0xA` · `0xB,0x10` · `default`.
//       Пересчитано по телу `npc_mob_pet.c:7718-7886` дважды.
//   И4. **Признак «использован талисман» — это КОД ВОЗВРАЩЁННОГО ПРЕДМЕТА в байтах +53**, а не
//       отдельное поле: `if (*(int*)((char*)&itemOpPayload[13] + 1))` (:7834, :7850), где
//       `13*4+1 = 53` — начало `Item(57)`. ⇒ «ноль» = сервер предмета не вернул.
//   И5. **Открытие 44200 ЗАКРЫВАЕТ окно 43000**: `uiCloseWindowReset(g_bDeviceReloadPending)`
//       стоит прямо в теле открытия (:90627). Обратно 43000 поднимает сам приём 6149 на исходах
//       1..4 (`CRefineWnd_Open`, npc_mob_pet.c:7815).
//
// ═════════════════════════ ★★★ КОЛЛИЗИЯ ИМЁН 44210 ↔ 44242 И КАК ОНА СНЯТА ════════════════════
//   В `.JUI` ДВА разных узла носят имя `Pet_LimitBreak_Text_8`: idx 30 = **44210** (подпись
//   «Изменение уровня», скин `Box_Brown_Skill2`) и idx 44 = **44242** (подпись талисмана, скин
//   `Black`). Поиск по имени вернул бы НЕ ТОТ. Импортёр развёл их так (живой дамп
//   `Reports/pet/ui_dump/wbp_raw/WBP_Pet_LimitBreak_Win.txt`, строки 78-79 и 99-103):
//       44210 → подпись `Pet_LimitBreak_Text_8_Text`, план `Pet_LimitBreak_Text_8_30_0`,
//               родитель `Pet_LimitBreak_Box_grp`, авторский текст «Новый уровень»;
//       44242 → подпись `Pet_LimitBreak_Text_8`,      план `Pet_LimitBreak_Text_8_44_0`,
//               родитель `Pet_LimitBreak_Text_8_grp`, авторский текст «Свитки защиты при прорыв».
//   ⚠ Это ОБРАТНО привычному правилу «<имя>_Text — подпись этого узла»: здесь `_Text` достался
//     ПЕРВОМУ вхождению, а голое имя — ВТОРОМУ. Поэтому адресуемся по РОДИТЕЛЮ, а не по имени:
//     оба узла разобраны с проверкой `GetParent()->GetName()`, и несовпадение пишется в лог.
//   Тот же приём волна 2 применила к паре 43072/43112 и 43074/43110 (суффикс `_1` у второго
//   вхождения) — здесь суффикс достался группе, а не подписи, поэтому проверка именно по родителю.
//   Живая геометрия дампа подтверждает развод: 44210 стоит в (681,298)-(811,317), 44242 — в
//   (681,300)-(811,320) СВОЕЙ группы, которая в ассете `Collapsed`.
//
// ════════════════════════ ГРУППА `Pet_LimitBreak_Text_8_grp` — ЕЁ НАДО ПОКАЗАТЬ ════════════════
//   В `.JUI` узел 44242 имеет `Visible="0"`, поэтому импортёр свернул всю его группу вместе с
//   детьми 44243 (счётчик) и 44241 (чекбокс). Оригинал в `uiInitTradeWindow` ЯВНО поднимает
//   и 44242 (`CControl_SetVisible(filler[64], 1)`, ui_core.c:90542), и 44243 (:90545) — а в
//   `.JUI` дети уже `Visible="1"`, то есть в оригинале вся тройка видна с первого открытия.
//   ⇒ Группу показываем как `SelfHitTestInvisible` (чекбокс внутри останется кликабельным).
//   Это НЕ правка ассета (закон `no-runtime-widget-fixes`): оригинал делает ровно этот вызов.
//
// ═══════════════════════════ ЧТО ОКНО ПОКАЗЫВАЕТ ПРО ШАНСЫ — НИЧЕГО ════════════════════════════
//   ★ ОРИГИНАЛ ПОРОГОВ БРОСКА НЕ ПОКАЗЫВАЕТ. Доказано двумя методами:
//     (1) сплошной разбор перерисовки `uiUpgradeDialog_RefreshItemInfo` (ui_core.c:90713-90781).
//         ★ ПОЛНЫЙ перечень, без сокращений (первая редакция этой шапки его ЗАНИЗИЛА — там стояло
//         «шесть текстов и три иконки», и именно это подавалось как доказательство):
//           ВОСЕМЬ текстовых контролов — 44202 имя (:90733), 44203 «можно/нельзя» (:90739),
//             44205 уровень (:90741), 44206 уровень+1 либо «Предел» (:90743/:90747),
//             44208 счётчик зелий (:90752), 44243 счётчик талисманов (:90762),
//             44253 счётчик талисманов повторно (:90769), 44252 счётчик зелий повторно (:90776);
//           ЧЕТЫРЕ иконочных — 44209 порода (:90730), 44253 предмет 404 (:90763),
//             44252 предмет 23237 (:90764), 44251 предмет 404 по галке (:90771).
//         Ни одного числа из `Petlimitbreaktable` среди них нет — ни в одном из двенадцати;
//     (2) grep по всему клиентскому декомпилу: единственный читатель таблицы порогов —
//         СЕРВЕР (`GameServer.c:36724-36775`), у клиента её вообще нет (отчёт §7.4:
//         «у клиента отдельной таблицы нет — клиент только показывает исход»).
//   ⇒ В порту окно тоже НЕ показывает шансов: дорисовать их было бы выдумкой
//     (закон `never-invent-only-original`, `full-1to1-reproduction`).
//   ⇒ Но ★ ДЕФЕКТ ПД4 (разъехавшиеся колонки загрузчика сервера) обязан быть виден там, где
//     пороги вообще называются, — в ДИАГНОСТИКЕ. Команда `asda.Pet.GrowthOdds` печатает
//     ФАКТИЧЕСКИЕ пороги из памяти сервера (`GetLimitBreakThresholds`, дефолт ключа
//     `asda.Pet.LimitBreakFixTable 0`), а не колонки файла, и прямо помечает случаи, где
//     исход предопределён (`MaxLevel 6` — исход 1 всегда; `MaxLevel 5` — исход 1 почти никогда).
//
// ═════════════════════════════════ ДВА ГЕЙТА ОТКРЫТИЯ ══════════════════════════════════════════
//   Г1 `g_pActiveBuffEntry != 0` (питомец ПРИЗВАН) → нотис **109975**, окно НЕ открывается
//      (ui_core.c:90612-90616). Проверяется ПЕРВЫМ и не смотрит на аргумент вовсе.
//   Г2 `a2 == 0` (питомца не передали) → молча ничего (:90617, ветки `else` нет).
//   Г3 `Level >= 10` → нотис **109946**, окно НЕ открывается (:90621, :90645).
//   Гейта «окно уже открыто» у 44200 НЕТ (в отличие от 43000): повторный вызов просто
//   переоткрывает окно на том же питомце.
//
// ══════════════════════════ ПРИЗНАК «ЗАПРОС В ПОЛЁТЕ» (байт +104) ══════════════════════════════
//   `net_sendTradeConfirmRequest` первой строкой проверяет `*((BYTE*)this + 104) != 1` и после
//   успешной отправки ставит 1 (network.c:32817, :32924). Снимается он ТОЛЬКО приходом 6149 —
//   причём В САМОМ НАЧАЛЕ приёмника, ДО switch, то есть на ЛЮБОМ коде, включая `default`
//   (npc_mob_pet.c:7718). Значит, второе подтверждение до ответа сервера не отправится.
//   В порту это `bSendInFlight`; он же снимается сбросом сессии (сокет пересоздан — ответа не будет).
//
// ═══════════════════════════════════ ЧЕГО ЗДЕСЬ НЕТ ═══════════════════════════════════════════
//   • АНИМАЦИЙ 44220/44221/44222/44223 (`Gfont_Limitbreak`, `Gfont_Bomb`, две `Ani_Holy`).
//     `uiInitTradeWindow` их НЕ привязывает вовсе (ui_core.c:90495-90592 — в списке
//     `CWindowMgr_FindControlByID` этих номеров нет), значит оригинал их никогда не показывает;
//     формулировка по закону `dead-code-markings-suspect`: ПРИВЯЗКИ В РАЗОБРАННОМ СРЕЗЕ НЕТ.
//     Мы их тоже не трогаем — в ассете они `Collapsed`.
//   • ОКНА 43300 — оно в `UAsdaPetEvoTipSubsystem` (тот же заход волны 4), потому что в оригинале
//     это метод ДРУГОГО класса (`CPetMgr`, `uiUpgradeDialogBindControls@0xEE0E30`), а `CPetMgr`
//     целиком достаётся волне 5 (окна 43500/43400/43450).
//   • НУМПАДА. Проверено для СВОЕГО диапазона: сплошной поиск `g_pNumPadDialog` по
//     `ui_core.c` даёт 9 вхождений, ни одного в диапазоне строк 90495…90840 (окно 44200);
//     по `network.c` в теле `net_sendTradeConfirmRequest` (32791-32930) — ноль; по
//     `npc_mob_pet.c` в теле приёмника 6149 (7691-7888) — ноль. Сверено с
//     `Docs/Port/systems/NUMPAD_FLOW.md`: окон питомцев там нет. Причина та же, что у остальных пет-окон:
//     предмет берётся РОВНО ОДИН из конкретной ячейки, количество не выбирается.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaReplyLatch.h"        // FAsdaReplyLatch — засов «запрос в полёте»
#include "AsdaSessionReset.h"      // IAsdaSessionResettable
#include "AsdaPetGrowthSubsystem.generated.h"

// ── Предобъявления окна 44200 «лимит-брейк». Полные заголовки тянет `.cpp`. ──
class UUserWidget;              // сам виджет окна 44200
class UWidget;                  // обобщённый узел — им ходит поиск контролов по имени
class UImage;                   // планы JUI: иконки талисмана и защиты, полосы, скины кнопок
class UButton;                  // кнопки 44232 «Развитие» и 44239 «крестик»
class UTextBlock;               // подписи: кличка, потолок уровня, название талисмана
class UCheckBox;                // галка «использовать защиту от провала» (премиум-слот)
class UAsdaButtonRelay;         // мостик hover/press: состояния подписи кнопки живут через него
class UAsdaPetSubsystem;        // модель данных: питомец, пороги броска, ответ 6149
class UAsdaInventorySubsystem;  // поиск талисмана и защиты в сумке + возврат записи 57 б
class UAsdaItemTableSubsystem;  // имя и иконка предмета по его коду
struct FAsdaPetSlot;            // одна запись питомца (порода, уровень, потолок уровня)

/** Найденный в инвентаре расходник окна: указатель эталона + счётчик подходящих ячеек.
 *  Эталонные искатели возвращают ПОСЛЕДНЮЮ подходящую ячейку и ОБЩЕЕ число ячеек
 *  (`inventoryFindItemByType207`, item_inventory.c:65219; `bagFindItemByType209`, ui_core.c:90653) —
 *  порт повторяет это дословно, включая «последнюю, а не первую». */
USTRUCT()
struct FAsdaPetGrowthItemRef
{
	GENERATED_BODY()
	/** Контейнер порта: 2 — расходники (60 ячеек, `g_InventoryArray2`), 1 — премиум-сумка (30, `g_InventoryBag`). */
	UPROPERTY(Transient) int32 Container = -1;
	/** Номер ячейки внутри контейнера либо −1. */
	UPROPERTY(Transient) int32 Cell = -1;
	/** Код предмета в найденной ячейке либо 0. */
	UPROPERTY(Transient) int32 ItemCode = 0;
	/** Сколько ЯЧЕЕК подошло (эталон считает ячейки, а не штуки). */
	UPROPERTY(Transient) int32 Count = 0;

	/** Предмет найден. */
	bool IsSet() const { return Cell >= 0; }
};

UCLASS()
class ASDAREBORN_API UAsdaPetGrowthSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подъём подсистемы: подписки и заготовки. Виджет окна рождается лениво при первом открытии.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снятие подписок и указателей на контролы: виджет переживать подсистему не должен.
	virtual void Deinitialize() override;

	/** Окно, цель и «запрос в полёте» принадлежат СЕССИИ: сокет пересоздан — ответа 6149 не будет. */
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Кадр окна: слив событий модели, подсказка по наведению на 44242 и отсчёт засова 6148.
	virtual void Tick(float DeltaTime) override;
	// Обязательная метка профилировщика для `FTickableGameObject` — своей работы не несёт.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPetGrowthSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна. Все из `.JUI`, дампа и кода эталона, ни одно не выдумано ──
	/** Потолок уровня роста: гейт `*(int*)(a2 + 80) < 10` (ui_core.c:90621). */
	static constexpr int32 kLevelCap = 10;
	/** Уровень, на котором 44206 показывает «Предел» красным: `v12[22][20] == 9` (:90747). */
	static constexpr int32 kLastLevelBeforeCap = 9;
	/** Цвет «Предела» — эталонное `CControl_SetField923(..., -65536)` = ARGB 0xFFFF0000 (:90748). */
	static constexpr uint32 kCapColorArgb = 0xFFFF0000u;
	/** Цвет обычного значения 44206 — `CControl_SetField923(..., -1)` = ARGB 0xFFFFFFFF (:90744). */
	static constexpr uint32 kNormalColorArgb = 0xFFFFFFFFu;
	/** Сторона иконки породы 44209: `CControl_DrawTextureRect(..., 64, 64)` (:90728). */
	static constexpr int32 kPetIconPx = 64;
	/** Сторона «пустой» иконки при гашении: `DrawTextureRect(..., 0, 0, 32, 32)` (:90720). */
	static constexpr int32 kBlankIconPx = 32;
	/** ★ ЖЁСТКО ЗАШИТЫЕ В ЭТАЛОН КОДЫ ПРЕДМЕТОВ для иконок 44253/44251 и 44252
	 *  (`itemTooltipDrawRecordTextures(g_pItemTooltipWindow, v12[18], 404)` и `(v12[20], 23237)`,
	 *  ui_core.c:90763-90764). Это именно КОДЫ ПРЕДМЕТОВ, а не loc-id: функция ищет их в
	 *  `item_list`/`special_itemlist`/`sowell_list` (item_inventory.c:5853-5855). */
	static constexpr int32 kTalismanIconItem = 404;
	// Код предмета, чья иконка рисуется в слоте зелья окна.
	static constexpr int32 kPotionIconItem   = 23237;
	/** Подтип талисмана во ВТОРОЙ таблице (`Table2Record->subType == 209`, ui_core.c:90666). */
	static constexpr int32 kTalismanSubType = 209;
	/** ★ Зелье ищется НЕ по подтипу, а по полю связи групп: `ItemRecord->linkCode == 207`
	 *  (item_inventory.c:65234). Именно `linkCode`, а не `itemSubType` — в отличие от
	 *  `invFindItemByType105` (:54613), который ищет по подтипу. Расхождение эталонное. */
	static constexpr int32 kPotionLinkCode = 207;
	/** Контейнер расходников порта (эталонный `g_InventoryArray2`, 60 ячеек). */
	static constexpr int32 kSpendContainer = 2;
	/** Контейнер премиум-сумки порта (эталонный `g_InventoryBag`, 30 ячеек). */
	static constexpr int32 kShellContainer = 1;
	/** z-порядок: диалог роста поверх окна 43000 (у клетки 732, у её панели действий 742). */
	static constexpr int32 kGrowthZ = 748;

	// ═══════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ══════════════════════════════════════
	/** ПОЛНЫЙ порт `uiUpgradeDialog_Open@0xEE18E0`: гейт 109975 (призван), гейт 109946 (уровень ≥ 10),
	 *  закрытие окна 43000, подпись 109936, расчёт «можно ли расти», первая перерисовка,
	 *  автовзвод галки при наличии талисмана. `false` — окно не открылось (сработал гейт). */
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	bool OpenGrowth(int32 PetUniqueId);

	/** Порт `sub_91FDCE` — «убрать окно 44200 из менеджера и спрятать». Тела в срезе нет
	 *  (сплошной поиск по `AsdaStory_game.c` даёт только 4 ВЫЗОВА, определения нет), поэтому
	 *  состав взят у его близнеца `uiCloseWindowReset@0xDC3500` (ui_core.c:64925): снять признак
	 *  «открыто» + `SetVisible(корень, 0)`. Ничего сверх этого не добавлено. */
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	void CloseGrowth();

	// Открыто ли 44200. Читает диагностика прогонов и гейты соседних окон.
	UFUNCTION(BlueprintPure, Category="Asda|Питомцы")
	bool IsGrowthOpen() const { return bOpen; }

	// ═══════════════════════════════ ДЕЙСТВИЯ (обработчики) ═══════════════════════════════════
	/** 44232 «Развитие» — событие 66561. Порт `uiUpgradeDialogRefreshItemInfo@0xEE17B0`:
	 *  ★ несмотря на имя, это НЕ перерисовка, а ПРОВЕРКА расходников с ветками
	 *  msgbox 71 / нотис 109938 / нотис 109937 (ui_core.c:90565-90598). */
	void OnOkClicked();
	/** 44233 «Закрыть» и 44239 «крестик» — обе ветки события 66561 зовут одно и то же (:90800-90803). */
	void OnCloseClicked();
	/** 44241 — события 67073 (галка поставлена) и 67074 (снята), :90806-90816. */
	void OnProtectToggled(bool bChecked);
	/** Отправка 6148 по «ОК» модального подтверждения 71 (`ui_core.c:57104` → network.c:32791). */
	void SendLimitBreak();

	// ═══════════════════════════════════ ДИАГНОСТИКА ══════════════════════════════════════════
	/** Каждый разобранный контрол окна (закон `full-structural-analysis-always`). */
	FString DumpControls() const;
	/** Состояние: цель, уровни, расходники, галка, гейты, последний 6149. */
	FString DumpState() const;
	/** ★ ФАКТИЧЕСКИЕ пороги броска — то, что РЕАЛЬНО сделает сервер (дефект ПД4). В UI не выводятся
	 *  (оригинал их не показывает), это только печать в лог. */
	FString DumpOdds() const;
	/** Таблица «событие → loc → канал» с фактическими строками порта. */
	FString DumpNotices() const;

	// ═════════════════ ★★★ СТЕНД ОТПРАВКИ 6148 ПУТЁМ ИГРОКА (замечание ревизии волны 3) ═══════
	//   ЗАЧЕМ. На живом прогоне без логина `SelectedCharId == 0` и сокета нет, поэтому ни один
	//   исходящий пет-пакет из окна НЕ УХОДИТ — проверка «путём игрока» молча теряет весь путь
	//   «кнопка → подтверждение → очередь → сборщик тела». Волна 1 для этого уже держит
	//   `UAsdaPetSubsystem::BeginSendCapture` (подмена charId + перехват `SendCaptureForStand`,
	//   живущий между кадрами) — ЕГО И ЗОВЁМ, второго механизма не заводим
	//   (закон `check-existing-before-creating`).
	//   ЧТО ПРОВЕРЯЕТСЯ: опкод, длина тела (9 б), вид конверта («с секретом») и ЗНАЧЕНИЯ ПОЛЕЙ
	//   по СЕРВЕРНЫМ смещениям (`+0 petUid`, `+4 u16 слот`, `+6 u8 сумка`, `+7 u16 премиум-слот`,
	//   GameServer.c:36624-36640) — то есть мимо кода сборщика.
	//   Путь при этом БОЕВОЙ: зовётся `SendLimitBreak()` окна, а тело снимается уже из очереди
	//   волны 1 (закон `diagnostic-must-use-real-path`).
	void StartSendStand(int32 TestCharId);

private:
	/** Разбор снятого тела 6148 и печать вердикта. Возвращает «всё сошлось». */
	bool CheckStandBody(int32 Opcode, const TArray<uint8>& Body, TCHAR Envelope) const;

public:

private:
	// ── Жизненный цикл ──
	bool EnsureWindow();
	// Снести кэш виджетов окна (смена мира либо пересборка): зона снята, указатели обнулены.
	void ResetWidget();
	// Разобрать контролы окна по именам живого дампа и выставить признак готовности.
	void SetupControls();

	// ── Перерисовка ──
	/** Порт `uiUpgradeDialog_RefreshItemInfo@0xEE2A70` (ui_core.c:90700) — весь видимый состав окна. */
	void RefreshItemInfo();

	// ── Поиск расходников ──
	/** Порт `inventoryFindItemByType207@0xEE1B00` (item_inventory.c:65219): 60 ячеек контейнера 2,
	 *  условие `ItemRecord->linkCode == 207`, запоминается ПОСЛЕДНЯЯ, считаются все. */
	FAsdaPetGrowthItemRef FindPotion207() const;
	/** Порт `bagFindItemByType209@0xEE1BC0` (ui_core.c:90653): 30 ячеек контейнера 1,
	 *  условие `Table2Record->subType == 209`, запоминается ПОСЛЕДНЯЯ, считаются все. */
	FAsdaPetGrowthItemRef FindTalisman209() const;

	// ── Данные ──
	UAsdaPetSubsystem* Pets() const;
	// Модель контейнеров сумки — окно только читает её и пишет через её общие точки.
	UAsdaInventorySubsystem* Inv() const;
	// Таблицы предметов (`pettable`, `item_list`, иконки) — источник строк для окна.
	UAsdaItemTableSubsystem* Items() const;
	/** Питомец, на котором открыто окно (эталонный `filler[88]`). nullptr — цели нет. */
	const FAsdaPetSlot* Target() const;
	/** Строка клиентской `pettable` по породе. false — породы нет.
	 *  ★ Возврат ПО ЗНАЧЕНИЮ (волна 2 уже чинила это со `static`): окно держит строку одновременно
	 *  с другими окнами питомцев. */
	bool PetRow(int32 PetIndex, struct FAsdaPetRow& Out) const;

	// ── Нотисы. ВСЕ loc-id сняты по СВОЕМУ обработчику (закон `loc-per-handler-not-by-family`) ──
	FString Loc(int32 LocId) const;
	// Нотис окна — переходник на общую точку `AsdaPetUi::NoticeMsgBox` (ONEBUT_MSGBOX 1500).
	void NoticeMsgBox(int32 LocId) const;

	// ── Мышь и подсказка ──
	/** Наведение на 44242: события 8/9/10 (`uiUpgradeDialogWndProc`, ui_core.c:90825-90835) —
	 *  двухстрочная подсказка 110113 «Предел» / 110643 «Этот свиток…». */
	void TickTooltip();

	// ── Реакция на пакеты ──
	void OnPetEvent(int32 Opcode, int32 Result, int32 PetUniqueId);
	// Слить очередь событий модели из `Tick` (закон `no-reentrant-net-callbacks`).
	void DrainPetEvents();
	/** Разбор исхода 6149 — ПОЛНЫЙ порт `pet_OnRecvItemOpResult` (npc_mob_pet.c:7691-7888). */
	void OnLimitBreakResult(int32 Result, int32 PetUniqueId);

	// ═══════════════════════════════════ СОСТОЯНИЕ ════════════════════════════════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;

	// Контролы. Номер JUI → имя виджета ассета (живой дамп `44200_Pet_LimitBreak_Win.md`).
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle    = nullptr;  // 44201 Pet_LimitBreak_Top_Textmove      loc 109930
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnX        = nullptr;  // 44239 Pet_LimitBreak_X
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgPetIcon  = nullptr;  // 44209 Pet_LimitBreak_Mark_14_0
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPetName  = nullptr;  // 44202 Pet_LimitBreak_Text_1
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCanBreak = nullptr;  // 44203 Pet_LimitBreak_Text_2   109945/109944
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLvlLabel = nullptr;  // 44204 Pet_LimitBreak_Text_3_Text loc 110650
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLvlCur   = nullptr;  // 44205 Pet_LimitBreak_Text_4    Level
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLvlNew   = nullptr;  // 44206 Pet_LimitBreak_Text_5    Level+1 / «Предел»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNewLabel = nullptr;  // 44210 Pet_LimitBreak_Text_8_Text ★ loc 110651
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPotLabel = nullptr;  // 44107 Pet_LimitBreak_Text_6    loc 109932
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPotCount = nullptr;  // 44208 Pet_LimitBreak_Text_7    счётчик 207
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHint     = nullptr;  // 44231 Pet_LimitBreak_Tooltip   loc 109936
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk       = nullptr;  // 44232 Pet_LimitBreak_OK        loc 109933
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOk       = nullptr;  // 44232 подпись
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNo       = nullptr;  // 44233 Pet_LimitBreak_NO        loc 109934
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNo       = nullptr;  // 44233 подпись
	UPROPERTY(Transient) TObjectPtr<UCheckBox>  ChkProtect  = nullptr;  // 44241 Pet_LimitBreak_Check
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTalisman = nullptr;  // 44242 Pet_LimitBreak_Text_8    ★ loc 110409
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTalCount = nullptr;  // 44243 Pet_LimitBreak_Text_9    счётчик 209
	UPROPERTY(Transient) TObjectPtr<UWidget>    TalismanGrp = nullptr;  // Pet_LimitBreak_Text_8_grp (родитель 44242/44243/44241)
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgMark3    = nullptr;  // 44253 Pet_LimitBreak_SETMARK3_32_0 иконка 404
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMark3    = nullptr;  // 44253 подпись — счётчик 209
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgMark1    = nullptr;  // 44251 Pet_LimitBreak_SETMARK_36_0  иконка 404 по галке
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMark1    = nullptr;  // 44251 подпись
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgMark2    = nullptr;  // 44252 Pet_LimitBreak_SETMARK2_31_0 иконка 23237
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMark2    = nullptr;  // 44252 подпись — счётчик 207
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgGfont3   = nullptr;  // 44250 Pet_LimitBreak_Gfont_3_33_0  видим по галке

	// Посредники `OnClicked` настоящих `UButton` окна: живут столько же, сколько и окно.
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Модель окна (эталонные поля `CUpgradeDialogTradePanel`) ──
	/** `filler[88]` — питомец, на котором открыто окно. −1 — цели нет. */
	int32 TargetUid = -1;
	/** `filler[92]` (его читает `get_o5C@0xEE18C0`, гейт Г4 окна 43000) — «диалог роста открыт». */
	bool bOpen = false;
	/** Контролы разобраны. */
	bool bReady = false;
	/** `filler[105]` — «уровень дорос до предела, можно расширять» (`Level >= MaxLevel`). */
	bool bCanBreak = false;
	/** `filler[104]` — запрос 6148 в полёте (см. шапку). */
	/** ★★★ ЗАСОВ «ЗАПРОС В ПОЛЁТЕ» — общая точка `FAsdaReplyLatch` (до 11.10.2026 `FAsdaInFlightLatch`; волна 8, пункт 1
	 *  сквозной ревизии). Было голым `bool bSendInFlight`, который снимался ТОЛЬКО
	 *  приходом ответа и сбросом сессии: промолчавший сервер делал кнопку мёртвой
	 *  навсегда (закон `no-state-latch-before-shown`). Теперь снимается также закрытием
	 *  и открытием окна и по таймауту `asda.Pet.SendTimeoutSec`. */
	FAsdaReplyLatch SendLatch;
	/** Состояние галки 44241 (`m_bToggleState`, читается `getFlagByte_p608`). */
	bool bProtectChecked = false;
	/** Модальное подтверждение 71 уже открыто — второго не открываем. */
	bool bConfirmOpen = false;
	/** `filler[96]/[100]` — найденное зелье 207 и число ячеек. */
	FAsdaPetGrowthItemRef Potion;
	/** `filler[108]/[112]` — найденный талисман 209 и число ячеек. */
	FAsdaPetGrowthItemRef Talisman;
	/** Показана ли сейчас подсказка по 44242 (чтобы не дёргать плашку каждый кадр). */
	bool bTipShown = false;

	// ── Стенд отправки 6148 (см. `StartSendStand`) ──
	/** Перехват взведён ЭТИМ окном — снимать его обязано оно же. */
	bool bStandArmed = false;
	/** Тело снято и разобрано — на следующем тике перехват снимается. */
	bool bStandDone = false;
	/** Сколько секунд ещё ждём тело. 0 ⇒ снять перехват с отметкой «пакет не ушёл».
	 *  ★ Обязателен: пока перехват взведён, в сеть НЕ УХОДИТ НИЧЕГО — оставить его висеть нельзя. */
	float StandLeft = 0.f;
	/** Что ждём в теле: uid питомца, сумка/ячейка зелья и ячейка премиума. */
	int32 StandExpectUid = -1;
	// Стенд: какую СУМКУ ждём в собранном теле запроса.
	int32 StandExpectBag = -1;
	// Стенд: какую ЯЧЕЙКУ ждём в собранном теле запроса.
	int32 StandExpectSlot = -1;
	// Стенд: номер ячейки талисмана (−1 = галка снята либо талисмана нет).
	int32 StandExpectPremium = -1;

	// Покадровый опрос мыши и фронты кнопок — общая точка `AsdaUiHelpers`.
	FAsdaMouseFrame Mouse;

	/** Очередь событий модели (закон `no-reentrant-net-callbacks`: из колбэка только кладём). */
	TArray<TTuple<int32, int32, int32>> PetEvents;
	// Подписка на события модели питомцев; снимается в `Deinitialize`.
	FDelegateHandle HPetEvent;
};
