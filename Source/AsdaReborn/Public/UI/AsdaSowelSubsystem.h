// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSowelSubsystem — СИСТЕМА САМОЦВЕТОВ («sowel», «Живой камень») окна инвентаря `INVEN_WIN` 400.
//   Полный реверс и таблица КАЖДОГО контрола ветки — `Reports/sowel_system_port.md`.
//
// ═════════════════════ ЧТО ЭТО ЗА СИСТЕМА ══════════════════════════════════════════════════════
//   Правая панель инвентаря содержит ДВЕ группы, которые прежний реверс считал квестовыми:
//     • `Inven_SowelInven`   — СПИСОК ДОСТУПНЫХ КАМНЕЙ, подходящих выбранному предмету
//       (6 ячеек `INVEN_SOWTRAY0..5` = idnum **430…435**, курсор **436**, колесо **495**,
//        заголовок **437** = нотис 12198 «Живой камень»);
//     • `Inven_StonSowelBase` — ЧЕТЫРЕ ГНЕЗДА выбранного предмета (`INVEN_SOWELL0..3` = **441…444**),
//       курсор гнезда **445**, «замки» **446 / 10040 / 10041** и подложка «гнёзд нет» **499**.
//   Плюс две анимации над сеткой: **498** `INVEN_HOLYEFFECT` и **10009** `INVEN_SOULSTONEEFFECT`.
//   ★ Прежний вывод «430…435 — квестовые слоты» ОШИБОЧЕН: переменную назвал так декомпилятор
//     (`questSlotIdx`), а `uiSowelGridSetPage@0xD011F0` пишет в `pInvenWnd[42+i]` (= 430+i) иконки
//     `SowelRecord->iconPath` из вектора `this->gap_330` (шаг элемента 44 б). Проверено повторно.
//
// ═════════════════════ ГДЕ ЛЕЖАТ ДАННЫЕ ════════════════════════════════════════════════════════
//   • Камень = запись таблицы `sowell_list` (коды **7001…10925**, ТРЕТЬЕ пространство кодов, см.
//     [[inventory-port]]). Иконка — `IconPath` целиком, ячейка 32×32.
//   • Гнёзда предмета — ЧЕТЫРЕ i16 в 57-байтовой записи по смещениям **+16 / +18 / +20 / +22**
//     (эталон читает `word_2901960 + 57*slot + 2*i`, где `word_2901960 = g_InventoryBag + 16`).
//     0 = гнездо пустое. Сколько гнёзд ДОСТУПНО — из `ItemRecord->EnchantOrSocket + 1`.
//   • Сам камень как ПРЕДМЕТ лежит в КОНТЕЙНЕРЕ 2 (расходники) — список строится сканом всех
//     60 ячеек контейнера 2 (`inventorySowelWin_BuildSowelList@0xD18120`).
//   • Совместимость: у записи камня ТРИ байта-цели `+45 / +46 / +47` (в DataTable есть только
//     первые два — `SocketTargetType`, `SocketTargetType2`; третий читаем из `DataHex[47]`).
//     Камень подходит предмету, если ЛЮБОЙ из трёх байтов:
//        == `ItemRecord->ItemType` предмета  (а при значении 9 ещё и `SpecialFlag != 0`,
//           иначе требуется совпадение по маске `Item_IsSowelCompatible@0xD1E810`:
//           бит `ItemSubType` в 19-битной маске `OptionMask` камня);
//        == 12  — «подходит всему»;
//        == 13  — всему, кроме типов 8 и 9 (щит/оружие);
//        == 14  — всему, кроме типа 9.
//     ★ Удочка (`ItemSubType == 10 && ItemVariant == 1`) камней НЕ принимает вовсе — эталон
//       обнуляет запись камня для неё ещё до фильтра.
//
// ═════════════════════ ТРИ ПАРЫ ОПКОДОВ (авторитет — сервер) ═══════════════════════════════════
//   1) **5024 → 5025** ВСТАВИТЬ / ЗАМЕНИТЬ камень. `itemSowelSocketRequest@0xD090B0`
//      (при занятом гнезде 0 — через `itemSowelApplySend@0xD1D590`, тело то же);
//      сервер `CharCellobItemMove@0x4DC390`.
//      ★ ГНЕЗДО ВЫБИРАЕТ НЕ ИГРОК: обычный камень (`SpecialFlag == 0`) идёт ВСЕГДА в гнездо **0**
//        (эталон жёстко пишет `socket = 0`), а камень со `SpecialFlag != 0` — в ПЕРВОЕ СВОБОДНОЕ
//        из 1..`EnchantOrSocket` (`slotFindFirstEmpty@0xD10300`). Если свободных нет — эталон
//        открывает окошко `SOWEL_Tooltip` со стоящими камнями и НИЧЕГО не шлёт.
//   2) **5022 → 5023** ВЫНУТЬ камень (`invItemDropSendPacket@0xD06BA0`, сервер
//      `CharCellobItemDetach@0x4DBC30`). ★★★ ИСПРАВЛЕНО 21.10.2026: ГНЕЗДО **0** ВЫНИМАЕТСЯ — прежнее
//      правило «гнездо 0 вынуть нельзя» было ложным чтением декомпила. `if (invCtx[200])` лишь выбирает
//      между ВСТРОЕННОЙ копией отправки (гнездо ≠ 0) и вызовом `netSendItemActionRequest@0xD1CE30`
//      (гнездо 0), а тот шлёт ТОТ ЖЕ 5022 (`push 0x139E`) с байтом гнезда `invCtx[200]` = 0. Доказано
//      вторым методом — дизасм эталонной сборки: `0xD06C74 cmp [eax+0x320],0 / jne 0xD06CAF`, ветка нуля
//      `0xD06CA5 call 0x92245C` → `jmp 0xD1CE30`. Сервер номер гнезда не ограничивает (читает
//      `*((i16*)&Src[4] + гнездо)` без проверки на 0). Разбор — `Reports/sowel_bugfix_2026-10-21/REPORT.md`.
//      Двойной клик по гнезду с камнем (событие 0xD → `CInventory_OnSlotUse`, `m_nSelKind == 3`) — тоже 5022.
//      Ответ `result == 2` = сервер требует ПОДТВЕРЖДЕНИЯ (двухкнопочный msgbox, нотис 2002);
//      «OK» повторяет тот же 5022 (`netSendItemActionRequest@0xD1CE30`, msgbox-код 0x19).
//   3) **5031 → 5032** «СВЯТОСТЬ»: применить предмет `ItemType == 11` к КАМНЮ, лежащему в
//      контейнере 2 (`net_sendSecuredRequest_5031@0xD167A0`, сервер `CharSpendHolyItemUse@0x4DE590`).
//      Взводится ДВОЙНЫМ КЛИКОМ по такому предмету (курсор «bottle»), гасится повторным двойным
//      кликом или кликом по НЕ-камню (нотис 2016). Успех (result 1/7) включает анимацию **498**
//      над ячейкой камня, ставит курсор «hammer» и играет звук 44.
//
// ═════════════════════ ВИДИМОСТЬ «ЗАМКОВ» (эталон 1:1) ═════════════════════════════════════════
//   `socketCount = ItemRecord->EnchantOrSocket + 1`; до switch: 499 показан, 446/10040/10041 скрыты.
//     socketCount 1 → 446+10040+10041 показаны, 499 скрыт   (свободно ТОЛЬКО гнездо 0)
//     socketCount 2 → 10040+10041                            (гнёзда 0 и 1)
//     socketCount 3 → 10040                                  (гнёзда 0,1,2 — заперто 3)
//     socketCount 4 → ничего                                 (все четыре)
//     иначе (0 или ≥5, а также УДОЧКА) → показан ТОЛЬКО 499 — а он в JUI РОДИТЕЛЬ ещё трёх
//        безымянных «шторок», поэтому его показ гасит все четыре гнезда сразу.
//   ★ Соответствие «шторка → гнездо» по геометрии .JUI: 446 → гнездо 1 (23,20), 10040 → гнездо 3
//     (65,62), 10041 → гнездо 2 (107,20), 499 → гнездо 0 (65,11) + три его дочерних плана.
//
// ═════════════════════ ЧЕГО В СРЕЗЕ НЕТ (не выдумываем) ════════════════════════════════════════
//   • Окна `WBP_SOWEL_Tooltip` (`sowelSlot_DrawStones@0xD6CC50`) эта подсистема НЕ рисует: оно
//     принадлежит `CItemTooltipUI` — соседняя зона. Точка вызова оставлена делегатом
//     `OnNeedSowelStonesWindow` (см. ниже), сама подсистема при «нет свободных гнёзд» только
//     сообщает наружу и молчит, ровно как эталон (никакого пакета).
//   • Кто и когда включает **10009** `INVEN_SOULSTONEEFFECT`: эталон КЭШИРУЕТ контрол и считает
//     смещение центрирования (`pInvenWnd[220]`), но ни одного `SetVisible(…, 1)`/`Show` для 10009
//     ни в `AsdaStory_ida.c`, ни в `RE/etalon/*.c` НЕТ. Центрирование мы считаем (как эталон),
//     показ оставлен публичным методом `ShowSoulStoneEffect` без автоматического вызова.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaScrollBarNomal.h"   // ★ 21.10.2026: полоса колёсного окна 495 (`VertScrollPosType = 1` в .JUI)
#include "UI/AsdaShopDblClick.h"     // ★ 21.10.2026: FAsdaDblClick — общий распознаватель двойного щелчка (порог Windows)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSowelSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UAsdaAnimatedPlane;
class UAsdaInventorySubsystem;

// ═══ Элемент списка доступных камней. 1:1 элемент вектора `gap_330` эталона (шаг 44 б):
//     {i32 код камня, i32 индекс ячейки контейнера 2, i32 порядковый номер, char[32] iconPath}. ═══
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaSowelListEntry
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Самоцветы") int32 Code = 0;      // +0
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Самоцветы") int32 SpendCell = -1;// +4
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Самоцветы") int32 Ordinal = 0;   // +8
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Самоцветы") FString IconPath;    // +12
};

// ═══ Что сейчас «взято в руку» (эталонное `m_nSelKind`, только интересные нам значения) ═══
UENUM(BlueprintType)
enum class EAsdaSowelPick : uint8
{
	None   = 0 UMETA(DisplayName = "ничего"),
	Stone  = 1 UMETA(DisplayName = "камень из списка (m_nSelKind = 2)"),
	Socket = 2 UMETA(DisplayName = "камень В ГНЕЗДЕ (m_nSelKind = 3)"),
};

UCLASS()
class ASDAREBORN_API UAsdaSowelSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSowelSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ══════════════════ ЧИСЛА ЭТАЛОНА (менять нельзя) ══════════════════
	static constexpr int32 kTrayFirstId   = 430;    // INVEN_SOWTRAY0..5   (pInvenWnd[42..47])
	static constexpr int32 kTrayCount     = 6;      // 6 ячеек на страницу (`6 * page`)
	static constexpr int32 kTraySelId     = 436;    // INVEN_SOWELLSELECT  (pInvenWnd[75])
	static constexpr int32 kAvailTxtId    = 437;    // Inven_AvailableSowelTxt (pInvenWnd[107]), нотис 12198
	static constexpr int32 kSocketFirstId = 441;    // INVEN_SOWELL0..3    (pInvenWnd[48..51])
	static constexpr int32 kSocketCount   = 4;
	static constexpr int32 kSocketSelId   = 445;    // INVEN_SOWELLSEL     (pInvenWnd[76])
	static constexpr int32 kShade1Id      = 446;    // INVEN_SOWELLSHADE   (pInvenWnd[78]) — гнездо 1
	static constexpr int32 kShade3Id      = 10040;  // INVEN_SOWELLSHADE1  (pInvenWnd[79]) — гнездо 3
	static constexpr int32 kShade2Id      = 10041;  // INVEN_SOWELLSHADE2  (pInvenWnd[80]) — гнездо 2
	static constexpr int32 kEmptyBackId   = 499;    // INVEN_SOWELLEMTYBACK(pInvenWnd[77]) — «гнёзд нет»
	static constexpr int32 kWheelId       = 495;    // INVEN_SOWELLWHEELWIN(pInvenWnd[82])
	// ★ 21.10.2026: rect колёсного окна 495 в .JUI — «98 65», Origin (-2,0) у того же родителя, что и ячейки
	//   списка (`INVEN_SOWTRAY_Base`), т. е. его левый-верх = левый-верх ячейки 430. Полоса (`VertScrollPosType = 1`)
	//   стоит у правого края rect'а и идёт на всю его высоту — ровно как у 494 сетки инвентаря.
	static constexpr float kWheelJuiW     = 98.f;
	static constexpr float kWheelJuiH     = 65.f;
	static constexpr float kTrayJuiStep   = 33.f;   // авторский шаг ячеек списка — база коэффициента масштаба
	static constexpr int32 kHolyEffectId  = 498;    // INVEN_HOLYEFFECT    (pInvenWnd[84])
	static constexpr int32 kSoulStoneId   = 10009;  // INVEN_SOULSTONEEFFECT (pInvenWnd[105])

	// Смещение первого гнезда в 57-байтовой записи и шаг (i16). Подтверждено с двух сторон:
	//   клиент `*(i16*)(record + 2*i + 16)`, сервер `*((i16*)&Src[4] + socketIdx)` (Src[4] = запись+16).
	static constexpr int32 kSocketOff0    = 16;
	static constexpr int32 kSocketStride  = 2;

	// Первый код таблицы `sowell_list`. Сервер адресует запись как `43 * (code - 7001)`.
	static constexpr int32 kSowelCodeFirst = 7001;

	// Значения байтов-целей камня (`+45/+46/+47`), означающие «подходит группе типов».
	static constexpr int32 kTargetAny        = 12;  // всему
	static constexpr int32 kTargetExcept89   = 13;  // всему, кроме типов 8 и 9
	static constexpr int32 kTargetExcept9    = 14;  // всему, кроме типа 9
	static constexpr int32 kTargetNeedsMask  = 9;   // == 9 требует SpecialFlag либо маску OptionMask

	// Удочка камней не принимает (`ItemSubType == 10 && ItemVariant == 1`).
	static constexpr int32 kRodSubType = 10;
	static constexpr int32 kRodVariant = 1;

	// Нотисы (все — из `DT_GameText`, закон «UI-текст только из таблиц»).
	static constexpr int32 kLocAvailTitle    = 12198;  // «Живой камень» — заголовок 437
	static constexpr int32 kLocNotSowel      = 2016;   // клик по не-камню при взведённой «святости»
	static constexpr int32 kLocDetachConfirm = 2002;   // подтверждение извлечения (двухкнопочный)
	// ★ 21.10.2026 (ревизия): имена по ТЕКСТУ строк (значения прежние, эталон network.c:17490-17497).
	static constexpr int32 kLocBagFull       = 2003;   // «В рюкзаке нет места» — 5023 result 4 (сервер: свободной ячейки контейнера 2 нет)
	static constexpr int32 kLocSoulmateOnly  = 8013;   // «Эта возможность доступна Друзьям.» — 5023 result 5 (как и 5025 result 9)
	static constexpr int32 kLocDetachDone    = 20120;  // 5023 result 1 (с числом-разницей веса)
	static constexpr int32 kLocSocketDone    = 20121;  // 5025 result 1 (имя камня + имя предмета)
	static constexpr int32 kLocHolyNothing   = 20140;  // 5032 result 0 (имя камня)
	static constexpr int32 kLocHolySuccess   = 20141;  // 5032 result 1 (имя камня)
	static constexpr int32 kLocHolyItemName  = 20142;  // 5032 result 2 (имя ПРЕДМЕТА)
	static constexpr int32 kLocHolyNoSowel   = 2013;   // 5032 result 0, когда в слоте не камень
	static constexpr int32 kLocIncompatible  = 20068;  // локальная проверка совместимости не прошла
	static constexpr int32 kLocIncompatible2 = 20150;  // то же в ветке `itemSowelApplySend`

	// ══════════════════ ТОЧКИ ВХОДА ДЛЯ СИСТЕМЫ ИНВЕНТАРЯ ══════════════════
	// ★ Игрок выбрал ячейку. Container 1 (сумка) / 3 (надетое) — строим список и рисуем гнёзда;
	//   любой другой контейнер — ветка гаснет (эталон зовёт `itemGrid_RedrawDefaultSlots`).
	void OnItemSelected(int32 Container, int32 Cell);

	// ★ Выбор снят / окно закрыто / вкладка переключена (эталон `uiWindowCloseAndReset@0xD02B80`).
	void OnSelectionCleared();

	// ★ Двойной клик по ячейке КОНТЕЙНЕРА 2. Возвращает true, если событие СЪЕДЕНО системой камней:
	//   (а) предмет — «святой» (`ItemType == 11`, подтип 8/9/12/44…53) ⇒ взвести/снять режим;
	//   (б) режим взведён и предмет — КАМЕНЬ ⇒ отправить 5031;
	//   (в) режим взведён и предмет НЕ камень ⇒ снять режим + нотис 2016 (событие тоже съедено —
	//       в эталоне такой клик возвращает 0 и дальше не идёт).
	bool OnSpendCellUse(int32 Cell);

	// ══════════════════ СПИСОК ДОСТУПНЫХ КАМНЕЙ ══════════════════
	// Эталон `inventorySowelWin_BuildSowelList@0xD18120`.
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	void RebuildList();

	// Эталон `uiSowelGridSetPage@0xD011F0` (страница = 6 ячеек).
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	void SetListPage(int32 Page);
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	void ScrollListBy(int32 Delta);
	UFUNCTION(BlueprintPure, Category="Asda|Самоцветы")
	int32 GetListPage() const { return ListPage; }
	UFUNCTION(BlueprintPure, Category="Asda|Самоцветы")
	int32 GetListPageCount() const;
	UFUNCTION(BlueprintPure, Category="Asda|Самоцветы")
	const TArray<FAsdaSowelListEntry>& GetList() const { return SowelList; }
	// ★ 21.10.2026: колесо над списком/полосой 495 (зовёт обработчик колеса окна инвентаря — z самого окна).
	//   true — колесо съедено списком (листаем, если страниц больше одной).
	bool HandleListWheel(int32 Notches);

	// Выбрать камень по КОЛОНКЕ страницы 0..5 (эталон `CInventoryUI_OnSlotClick`, cmdId 430…435).
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	bool SelectStoneByColumn(int32 Column);

	// ══════════════════ ГНЁЗДА ══════════════════
	// Значение гнезда 0..3 выбранного предмета (код камня; 0 — пусто, −1 — нет предмета).
	UFUNCTION(BlueprintPure, Category="Asda|Самоцветы")
	int32 GetSocketCode(int32 SocketIndex) const;
	// Сколько гнёзд у предмета ДОСТУПНО (`EnchantOrSocket + 1`, 0 если предмета нет).
	UFUNCTION(BlueprintPure, Category="Asda|Самоцветы")
	int32 GetSocketCount() const;

	// Выбрать ГНЕЗДО (эталон: только если в нём есть камень; иначе выбор не меняется).
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	bool SelectSocket(int32 SocketIndex);

	// ══════════════════ ДЕЙСТВИЯ ══════════════════
	// Вставить выбранный камень (5024). Номер гнезда выбирает ЭТАЛОННОЕ правило, а не игрок.
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	bool RequestSocketInsert();

	// ★ То же, но гнездо ЗАДАНО извне — единственный случай в эталоне: свободных гнёзд не было,
	//   открылось окно выбора `SOWELL_SELBOX` (1530), и игрок ткнул, какой камень заменить
	//   (`ui_core.c:57886` — «OK» шлёт тот же 5024 с выбранным гнездом). Окно принадлежит
	//   подсистеме тултипа и состояния камней не знает, поэтому шлём отсюда.
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	bool RequestSocketInsertAt(int32 SocketIndex);

	// Вынуть камень из ВЫБРАННОГО гнезда (5022). Любое гнездо 0..3 с камнем, в том числе ГЛАВНОЕ 0
	//   (эталон: гнездо 0 — через `netSendItemActionRequest@0xD1CE30`, тот же 5022; см. шапку).
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	bool RequestSocketDetach();

	// Отправить «святость» на камень в ячейке контейнера 2 (5031).
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	bool RequestHolyOnSowel(int32 SowelCell);

	// Снять взведённый режим «святости» (эталон: курсор «nomal», сброс `m_bSowelAttachMode`).
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	void CancelHolyMode(bool bNotice = false);

	UFUNCTION(BlueprintPure, Category="Asda|Самоцветы")
	bool IsHolyArmed() const { return bHolyArmed; }

	// ══════════════════ АНИМАЦИИ НАД СЕТКОЙ ══════════════════
	// 498 — «святость сработала» (эталон `sowelWndOnSocketResultPacket`, result 1 или 7).
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	void ShowHolyEffectOverCell(int32 SpendCell);
	// 10009 — «камень души». Автовызова в оригинале НЕТ (см. шапку) — только руками/командой.
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	void ShowSoulStoneEffect(int32 SpendCell);
	UFUNCTION(BlueprintCallable, Category="Asda|Самоцветы")
	void HideEffects();

	// ══════════════════ СОБЫТИЯ ДЛЯ СОСЕДНИХ СИСТЕМ ══════════════════
	// ★ «Свободных гнёзд нет» — эталон здесь открывает окно `WBP_SOWEL_Tooltip`
	//   (`sowelSlot_DrawStones(g_pItemTooltipUI, запись, EnchantOrSocket)`), которое принадлежит
	//   подсистеме тултипа. Мы сообщаем наружу СЫРУЮ запись и число гнёзд, ничего не рисуя сами.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSowelStonesWindow, const TArray<uint8>& /*rec57*/, int32 /*slotMode*/);
	FOnAsdaSowelStonesWindow OnNeedSowelStonesWindow;

	// ★ Сервер прислал новую запись предмета/камня — свечение оружия надо пересобрать
	//   (эталон `itemSetupSowelGlow(pLocalPlayer->gap_0)` в обеих ветках ответа).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaSowelGlowDirty);
	FOnAsdaSowelGlowDirty OnSowelGlowDirty;

	// ══════════════════ ДИАГНОСТИКА ══════════════════
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis-always)
	FString DumpState() const;      // список, выбор, гнёзда, режим святости
	void SimulateSocketResult(const TArray<uint8>& Body117);   // 5025 без сервера
	void SimulateDetachResult(const TArray<uint8>& Body115);   // 5023 без сервера
	void SimulateHolyResult(const TArray<uint8>& Body63);      // 5032 без сервера

	// ── Для самотеста `asda.SelfTest.Sowel` (только чтение живого состояния; поведение не меняют) ──
	UImage* DebugTrayIcon(int32 Col) const { return TrayIcon.IsValidIndex(Col) ? TrayIcon[Col].Get() : nullptr; }      // план ячейки списка 430+Col
	UImage* DebugSocketIcon(int32 S) const { return SocketIcon.IsValidIndex(S) ? SocketIcon[S].Get() : nullptr; }      // план гнезда 441+S
	bool DebugIsDragging() const { return DragGhost.IsActive(); }                    // летит ли призрак камня сейчас
	UImage* DebugDragGhost() const { return DragGhost.Ghost.Get(); }                 // сам призрак (геометрия/прозрачность)
	UImage* DebugDragSource() const { return DragSrcIcon.Get(); }                    // приглушённый источник переноса
	const FAsdaScrollBarNomal& DebugListBar() const { return ListBar; }              // полоса колёсного окна 495
	EAsdaSowelPick DebugPick() const { return Pick; }                                // что «в руке» (m_nSelKind)
	int32 DebugSelSocket() const { return SelSocket; }                               // выбранное гнездо (m_nSelStatCol)
	void DebugClearAwaiting() { bAwaitingResult = false; }                           // снять «ждём ответ» между шагами самотеста
	const FString& DebugLastNotice() const { return LastNoticeText; }                // последняя строка в чат от подсистемы (самотест)
	FColor DebugLastNoticeColor() const { return LastNoticeColor; }                  // её цвет
	void DebugClearLastNotice() { LastNoticeText.Reset(); }                          // забыть её перед шагом самотеста
	bool DebugIsCompatible(int32 SowelCode, int32 ItemCode, int32 ItemType) const { return IsSowelCompatible(SowelCode, ItemCode, ItemType); }

private:
	// ── Виджеты ──
	bool EnsureWidgets();
	void ResetWidgets();
	void ScanWidgets();
	void RedrawList();          // отрисовать текущую страницу списка
	void RedrawSockets();       // иконки гнёзд + «замки» (часть `ItemTooltip_PopulateInfoPanel`)
	void ClearBranch();         // `itemGrid_RedrawDefaultSlots@0xD18D50`
	void PlaceTrayCursor();     // `uiSelSlot_PositionCursorControl@0xD02AB0`  (436 на x−2, y)
	void PlaceSocketCursor();   // `uiTargetSlotShow@0xD029E0`                 (445 на гнездо)
	void ApplyShades(int32 SocketCountAvailable, bool bIsRod);
	void SetEmptyBackVisible(bool bVisible);

	// ── Ввод ──
	void TickPointer();
	// ★ 21.10.2026: перенос камня мышью — тот же вид, что у ячеек инвентаря (`UAsdaInventorySubsystem::BeginDrag`):
	//   летящая иконка `FAsdaDragGhost` + приглушённый источник. Kind 1 — ячейка списка (Idx = колонка 0..5),
	//   2 — гнездо (Idx = 0..3).
	void BeginStoneDrag(int32 Kind, int32 Idx);
	void EndStoneDragVisual();   // погасить призрак и вернуть источнику полную яркость (перенос кончился/отменён)
	// ★ Гейты эталона на отпускании (событие 7) и двойном клике (0xD) процедуры инвентаря: открыт барабан гачи
	//   (`CObject_GetField254(g_pInventoryWnd)`) → нотис 13106 в системный чат; активен удалённый склад
	//   (`CObject_GetField5581(g_pWarehouseWnd)`) → 10309. true — действие разрешено.
	bool PassInventoryActionGates() const;
	// ★ Первый гейт `itemSowelSocketRequest` / `invItemDropSendPacket`: персонаж «занят» (`objStateAllowsRender`,
	//   порт `UAsdaSystemOptionSubsystem::IsLocalPlayerBusy`) → нотис 2314 в системный чат. true — отказ.
	bool RefuseIfLocalPlayerBusy() const;
	// ★ 21.10.2026: полоса прокрутки колёсного окна 495 (эталон `uiControlSetScrollRange(495, 1, страниц)`).
	void LayoutListBar();
	bool TickListBar();          // ввод полосы; true — нажатие ЛКМ пришлось на полосу (дальше его не разбираем)
	bool IsCursorOver(const UWidget* W) const;
	int32 HitTestTray() const;     // колонка 0..5 под курсором (−1 нет)
	int32 HitTestSocket() const;   // гнездо 0..3 под курсором (−1 нет)

	// ── Модель ──
	const struct FAsdaItemInstance* TargetItem() const;
	// Совместимость камня с предметом (`inventorySowelWin_BuildSowelList` + `Item_IsSowelCompatible`).
	bool IsSowelCompatible(int32 SowelCode, int32 TargetItemCode, int32 TargetItemType) const;
	// Три байта-цели камня (+45/+46/+47). Третий — из `DataHex`, колонки под него в DT нет.
	bool GetSowelTargets(int32 SowelCode, int32 Out[3], int32& OutSpecialFlag, int32& OutOptionMask) const;
	// Куда положить камень (эталон): обычный → 0; со `SpecialFlag` → первое пустое 1..N.
	//   false ⇒ свободного гнезда нет (эталон открывает окно камней и молчит).
	bool ResolveTargetSocket(int32 SowelCode, int32& OutSocket) const;

	// ── Сеть (колбэки ТОЛЬКО копят, работа в Tick — закон `no-reentrant-net-callbacks`) ──
	void OnSocketResult(const TArray<uint8>& Body117);
	void OnDetachResult(const TArray<uint8>& Body115);
	void OnHolyResult(const TArray<uint8>& Body63);
	void ApplySocketResult(const TArray<uint8>& B);
	void ApplyDetachResult(const TArray<uint8>& B);
	void ApplyHolyResult(const TArray<uint8>& B);
	// ★★★ ПОРЯДОК ЭТАЛОНА: при result 1/7 `sowelWndOnSocketResultPacket` НЕ разбирает ответ сразу —
	//   он только включает анимацию 498, а разбор (`sowelWndHandleSocketResult`) выполняется ПОЗЖЕ,
	//   по событию 0x18 «анимация завершена» этого же контрола (`ui_core.c:48101-48103`).
	//   Поэтому тело ответа откладывается, а модель и нотис применяются в `FinishHolyResult`.
	void FinishHolyResult(const TArray<uint8>& B);
	UFUNCTION() void OnHolyEffectAnimFinished();

	// ── Помощники ──
	UAsdaInventorySubsystem* Inv() const;
	// ★ 21.10.2026: строка `Id` с аргументами в строковых слотах [ss1]…[ss5] (порт `CObject_SetSlotString` + `UI_FormatLocalizedNotice`).
	FString LocSlots(int32 Id, std::initializer_list<FString> Strs) const;
	FString LocRaw(int32 Id) const;                       // ★ GetTextByIdRaw — иначе теги срезаются
	static FString FormatPrintf(const FString& Fmt, const TArray<FString>& Args);
	void ChatNotice(const FString& Text, FColor Color) const;   // `CChat_AddTokenizedLines`
	void MsgBox1(const FString& Text) const;                    // ONEBUT_MSGBOX эталона
	void PlayUiSound(int32 SoundId) const;
	void SetCursorMode(int32 Mode) const;                       // 0 nomal · 1 bottle · 2 hammer
	FString SowelName(int32 Code) const;
	bool ApplyIcon(UImage* Img, int32 ItemCode) const;
	void ClearIcon(UImage* Img) const;
	static void SetVis(UWidget* W, bool bVisible);

	// ══════════════════ ДАННЫЕ: ВИДЖЕТЫ ══════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> HostWidget = nullptr;             // WBP_INVEN_WIN
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     TrayIcon;                  // 430…435 (планы)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TrayText;                  // их Text-контролы
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> TraySel = nullptr;         // 436
	UPROPERTY(Transient) TObjectPtr<UTextBlock>         AvailTxt = nullptr;        // 437
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     SocketIcon;                // 441…444 (планы)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SocketText;
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> SocketSel = nullptr;       // 445
	UPROPERTY(Transient) TObjectPtr<UImage>             Shade1 = nullptr;          // 446   (гнездо 1)
	UPROPERTY(Transient) TObjectPtr<UImage>             Shade3 = nullptr;          // 10040 (гнездо 3)
	UPROPERTY(Transient) TObjectPtr<UImage>             Shade2 = nullptr;          // 10041 (гнездо 2)
	UPROPERTY(Transient) TObjectPtr<UWidget>            EmptyBackGrp = nullptr;    // 499 (группа из 4 планов)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     EmptyBackPlanes;           // сами 4 плана
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> HolyEffect = nullptr;      // 498
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> SoulStoneEffect = nullptr; // 10009
	// Смещения центрирования эффектов над ячейкой — эталон `pInvenWnd[219]` / `[220]`
	//   ( (ширина эффекта − ширина INVEN_SET0) / 2 ).
	FVector2D HolyCenterOff = FVector2D::ZeroVector;
	FVector2D SoulCenterOff = FVector2D::ZeroVector;
	// Позиции планов в системе координат КОРНЯ окна — курсоры лежат в других канвасах.
	TArray<FVector2D> TrayIconRoot;
	TArray<FVector2D> SocketIconRoot;
	FVector2D TraySelParentRoot = FVector2D::ZeroVector;
	FVector2D SocketSelParentRoot = FVector2D::ZeroVector;

	bool bControlsReady = false;
	bool bNetBound = false;
	bool bTipBound = false;   // подписка на «OK» окна выбора гнезда (1530) — ставится лениво, ОДИН раз

	// Общее тело отправки 5024 для обоих входов (авто-выбор гнезда и выбор игроком в окне 1530).
	bool SendSocketInsert(int32 Socket);
	// Обработчик «OK» окна `SOWELL_SELBOX`: подсистема тултипа отдаёт номер гнезда.
	void HandleSelBoxAccepted(int32 SocketIndex);

	// ══════════════════ ДАННЫЕ: СОСТОЯНИЕ (зеркало полей CInventoryWindow) ══════════════════
	UPROPERTY(Transient) TArray<FAsdaSowelListEntry> SowelList;   // gap_330 (вектор, шаг 44)
	int32 ListPage   = 0;          // this[137]  (m_nQuestPage)
	int32 CachedPage = -1;         // this[138]  (gap_228 — кэш «уже нарисованной» страницы)
	int32 SelStoneIndex = -1;      // this[199]  (m_nSelQuestSlot) — АБСОЛЮТНЫЙ индекс в списке
	int32 SelStoneCol   = -1;      // this[210]  (m_nSelQuestCol)  — колонка 0..5 на странице
	int32 SelSocket     = -1;      // this[200]  (m_nSelStatCol)   — выбранное гнездо 0..3
	EAsdaSowelPick Pick = EAsdaSowelPick::None;   // m_nSelKind (2 → Stone, 3 → Socket)

	int32 TargetContainer = 0;     // this[213]  (1 сумка / 3 надетое)
	int32 TargetCell = -1;         // this[198] / this[214]

	bool  bHolyArmed = false;      // m_bSowelAttachMode
	int32 HolyCell = -1;           // gap_300 (ячейка контейнера 2 со «святым» предметом)
	int32 HolySowelCell = -1;      // this[218] (ячейка камня, по которой ударили — для эффекта 498)

	// Драг
	// ★ Аудит №3 (B3): прежний латч `bLmbWasDown` — общая точка. Экземпляр один: опрос у списка
	//   камней ровно один (`TickPointer`), его ранний выход «панель подробностей свёрнута» латч
	//   не трогает — как и раньше.
	FAsdaMouseFrame Mouse;
	// ★ 21.10.2026: прежние защёлки `PressTray`/`PressSocket` заменены общим взводом переноса (порог — общая точка
	//   порта `FAsdaDragArm::kDragStartPixels`). KeyA = вид (1 список / 2 гнездо), KeyB = колонка/гнездо.
	FAsdaDragArm DragArm;
	// Летящая за курсором иконка камня (общая точка порта, тот же механизм, что у инвентаря/хотбара).
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;
	// Исходная иконка переноса — притушена, пока камень «в руке».
	UPROPERTY(Transient) TObjectPtr<UImage> DragSrcIcon = nullptr;
	// Двойной щелчок по ячейке списка / гнезду (эталонное событие 0xD = WM_LBUTTONDBLCLK).
	FAsdaDblClick Dbl;
	// Полоса прокрутки колёсного окна 495 (4 плана скина Nomal_Scr, собираются кодом — как у сетки инвентаря 494).
	UPROPERTY(Transient) FAsdaScrollBarNomal ListBar;
	// Окно, в дереве которого собраны полоса и призрак: тот же WBP при переоткрытии — переиспользуем, новый — собираем заново.
	TWeakObjectPtr<UUserWidget> ChromeHost;
	// Ключ последней раскладки полосы (начало, страниц, страница) — перекладываем только при изменении.
	FVector4f ListBarKey = FVector4f(-1.f, -1.f, -1.f, -1.f);
	bool bListBarShown = false;
	// Последняя строка, отданная в чат `ChatNotice` (только для самотеста — что и каким цветом увидел игрок).
	mutable FString LastNoticeText;
	mutable FColor LastNoticeColor = FColor::Transparent;

	// Ожидание ответа: второй запрос до ответа не шлём (эталонного флага нет — предохранитель).
	bool   bAwaitingResult = false;
	double SentAtS = 0.0;

	// Очереди ответов (закон `no-reentrant-net-callbacks`).
	//   ★ Аудит №3 (B10): три массива под общим `FCriticalSection` заменены тремя `TAsdaNetQueue`.
	//   Порядок применения между очередями и раньше был ФИКСИРОВАННЫМ (сначала все 5024, потом все
	//   5022, потом все 5031, независимо от порядка прихода), поэтому раздельные замки его не меняют.
	TAsdaNetQueue<TArray<uint8>> PendingSocket;
	TAsdaNetQueue<TArray<uint8>> PendingDetach;
	TAsdaNetQueue<TArray<uint8>> PendingHoly;

	// 5023 result 2 — сервер просит подтверждения; запомнили, что переспросить.
	bool  bDetachConfirmPending = false;

	// Отложенный ответ 5032 (ждём конца анимации 498 — см. FinishHolyResult).
	TArray<uint8> DeferredHolyBody;
	bool   bHolyAnimBound = false;
	double HolyAnimStartS = 0.0;

public:
	// ══════════════════ ПАРАМЕТРЫ (правятся в редакторе UE, закон ue-editor-friendly) ═══════════
	// Звук успешной вставки/замены камня. ЭТАЛОН: `csoundPlayName2DStore(g_pSoundMgr, 10, 0)`
	//   в `invApplySlotSwapResult`.
	UPROPERTY(EditAnywhere, Category="Asda|Самоцветы|Звук")
	int32 InsertSoundId = 10;
	// Звук успешного извлечения. ЭТАЛОН: 11 (`netRecvSowelActionResult`, case 1).
	UPROPERTY(EditAnywhere, Category="Asda|Самоцветы|Звук")
	int32 DetachSoundId = 11;
	// Звук успешной «святости». ЭТАЛОН: 8 (`sowelWndHandleSocketResult`, case 1 и 7).
	UPROPERTY(EditAnywhere, Category="Asda|Самоцветы|Звук")
	int32 HolySoundId = 8;
	// Звук запуска анимации 498. ЭТАЛОН: 44 (`sowelWndOnSocketResultPacket`).
	UPROPERTY(EditAnywhere, Category="Asda|Самоцветы|Звук")
	int32 HolyEffectSoundId = 44;

	// Сколько секунд ждать ответа, прежде чем разрешить новый запрос. ★ Эталон флага ожидания под
	//   5024/5022/5031 не держит вовсе — это наш предохранитель от «залипания» при потерянном ответе.
	UPROPERTY(EditAnywhere, Category="Asda|Самоцветы")
	float ResultTimeoutS = 5.f;

	// Размер иконки камня. ЭТАЛОН: `DrawTextureRect(…, 0, 0, 32, 32)` во всех четырёх местах.
	UPROPERTY(EditAnywhere, Category="Asda|Самоцветы")
	float IconDrawSize = 32.f;

	// Предохранитель: если анимация 498 не пришлёт «завершено» (кадры/интервал в скине не заданы),
	//   отложенный ответ 5032 применяется по этому таймауту. В эталоне такого запаса НЕТ.
	UPROPERTY(EditAnywhere, Category="Asda|Самоцветы")
	float HolyAnimMaxWaitS = 3.f;
};
