// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPremiumPostSubsystem — ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК (`Primium_Item_post_WIN`, JUI IDNum 69000,
//   кор. «프리미엄 아이템 우편함» — премиум-почта).
//
// ═══════════════════ КЛАССИФИКАЦИЯ (закон `classify-before-building`) ═══════════════════
//   НОВАЯ СИСТЕМА: своё окно, свой список, свои четыре опкода. Расширять `UAsdaInventorySubsystem`
//   нельзя — это другое окно с другой моделью данных. Но НИ ОДНОГО собственного механизма здесь
//   нет: окна берутся у `UAsdaWindowSubsystem`, пакеты — у `UAsdaNetSubsystem::SendSecured`,
//   модель контейнеров — у `UAsdaInventorySubsystem::ApplyExternalRecord`, сетка иконок — у
//   `UAsdaIconCellGrid`, подсказка — у `UAsdaItemTooltipSubsystem`, нотисы — у
//   `UAsdaMessageSubsystem`, иконка уведомления — у `UAsdaHotbarSubsystem` (закон `port-shared-points`).
//
// ═══════════════════ ЧТО ЭТО ЗА ФИЧА ═══════════════════
//   Клиентское представление ПРЕМИУМ-СПИСКА БД (хранимая процедура `UPCashitemList`), куда
//   попадают и покупка премиум-шопа (`BuyType == 1`), и приз премиум-гачи (`BuyType == 3`).
//   Игрок открывает ящик, выбирает СТРОКУ (= один заказ) и жмёт «Положить в рюкзак».
//
//   Полный реверс — `Reports/elite_mailbox_re.md`; реализация — `Reports/elite_mailbox_impl.md`.
//
//   Эталонные функции клиента, портированные здесь один к одному:
//     `uiInitMixWindowGrid@0xEEFE60`      — привязка контролов и статические подписи;
//     `uiBagWindow_OpenAndSync@0xEF1B30`  — открытие окна + запрос 11004;
//     `uiInventoryHandleCommand@0xEF1CC0` — команды окна (кнопки, выбор строки, события 8/9/0xA);
//     `uiItemGridFindTooltip@0xEF2040`    — подсказка по иконке строки;
//     `gridWindowPopulate7x7@0xEF1DE0`    — перерисовка 7×7 и авто-выбор строки;
//     `netParseLocalSlotSync@0xEF0990`    — разбор 11005;
//     `net_sendSlotSyncOpcode11006@0xEF0CD0` — отправка 11006;
//     `inventorySyncApplyPacket@0xEF1320` — применение 11007.
//
// ═══════════════════ ★★★ ТРИ ИСПРАВЛЕННЫХ ДЕФЕКТА ОРИГИНАЛА (закон `fix-original-defects`) ═══════
//   **A. Клик по иконке НЕ выбирал строку.** В эталоне выбор строки меняет ТОЛЬКО команда 66817 от
//      радиогруппы 69081 — вертикальной полосы 18×19 px слева (3.3 % площади строки 268×38).
//      Иконки 69011…69077 шлют лишь события 8/9/0xA (подсказка). Игрок кликал по нужной иконке,
//      выбор не менялся, и забиралась строка, которую поставил `gridWindowPopulate7x7`
//      (перебор `k = 6…0` С ВЫХОДОМ НА ПЕРВОЙ найденной, то есть НИЖНЯЯ/ПОСЛЕДНЯЯ непустая
//      строка — последний пришедший заказ) — ровно жалоба владельца «выбираешь один
//      предмет, а выдаются все». ЗДЕСЬ: клик по ЛЮБОЙ иконке строки выбирает эту строку
//      (`OnIconClicked`), радиокнопка при этом работает как прежде.
//   **B. `Count` доверялся клиенту.** Ни `Recv_PremiumItemToInven_Req@0x4F8340`, ни
//      `RecvDB_PremiumItemToInven_Res@0x4F89D0` не сверяют `Count` с БД и не ограничивают его
//      семью: при `Count > 7` сервер пишет за границу 87-байтовой структуры на стеке. ЗДЕСЬ:
//      в 11006 уходит ТОЛЬКО тот `Count`, что пришёл в 11005 (UI его не редактирует), значение
//      клампится семью на приёме и ещё раз проверяется в `SendPremiumPostToInven11006`.
//   **C. Число строк 11005 не ограничено.** Сервер шлёт столько записей, сколько вернула БД (до 64
//      в буфер 649 б), а клиент рассчитан ровно на 7 (`_WORD slotArray[36]`, 72 б) — переполнение
//      стека. ЗДЕСЬ: приём читает не больше 7 записей, лишние логируются и отбрасываются.
//   ⚠ НЕ ДЕФЕКТ: «строка = один заказ с `Count` единиц одного предмета, забрать поштучно нельзя».
//      Это устройство протокола (11006 несёт всю 10-байтовую запись, сервер размножает ОДНУ
//      запись предмета `Count` раз) — поштучный забор не реализуем и не реализуется.
//
// ═══════════════════ ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА ═══════════════════
//   Клик по иконке-уведомлению (слот 15 квик-бара). В ОРИГИНАЛЕ `uiNoticeButtonDispatch@0xDF4F10`
//   `case 15` зовёт `uiBagWindow_OpenAndSync`, а тот внутри делает
//   `sowelWindowClose(g_pInventoryWnd, 1)` — то есть ЗАКРЫВАЕТ окно инвентаря и открывает ящик.
//   ПО ТРЕБОВАНИЮ ВЛАДЕЛЬЦА открываются ОБА окна: и инвентарь, и ящик. Реализовано в
//   `OpenAndSync(bAlsoOpenInventory = true)`; эталонное поведение при этом не потеряно — оно
//   доступно вызовом с `false` и включается ключом консоли `asda.Post.LegacyCloseInven 1`.
//
// ═══════ ★★★ НАШЕ НОВОВВЕДЕНИЕ (НЕ ЭТАЛОН): «ПРОСМОТРЕННЫЕ ЗАКАЗЫ» ═══════
//   Требование владельца 2026-08-20: просмотренный ящик больше не моргает иконкой — ни при входе
//   в игру, ни после смены карты; иконка возвращается ТОЛЬКО когда пришёл заказ, которого игрок
//   ещё не видел. В оригинале такого механизма нет вовсе (иконка вешается на каждый непустой
//   11005 и снимается лишь кликом по ней). Реализация — набор `SeenOrders` в ЭТОЙ подсистеме
//   (она владелец данных ящика; второго источника истины в хотбаре не заводим — закон
//   `port-shared-points`), выключатель отката `asda.Post.SeenGate 0`, дамп `asda.Post.SeenDump`.
//   Подробности и обоснование ключа заказа — у `FAsdaPremiumPostSeenKey` ниже.
//   Отчёт — `Reports/elite_mailbox_seen.md`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaPremiumPostSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;
class UAsdaIconCellGrid;

// ═══ ОДНА СТРОКА ЯЩИКА = ОДИН ЗАКАЗ. Раскладка записи 11005 (10 б) — из формат-строки лога
//   сервера `0x594568` («… i = %d Order %d ItemIndex %d Count %d Type %d»), а не угадана. ═══
// ═══ ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 ОДНА ЗАПИСЬ 11005 — ОДНА ПОЗИЦИЯ ЗАКАЗА (слово владельца: «в премиум ящике каждый ряд
//   имеет несколько ячеек, а клался всегда 1 предмет»). Корзина премиум-магазина = один заказ = несколько записей с одним
//   одного `Order` в ОДНУ строку окна (до 7 ячеек), забирает строку целиком — по 11006 на запись. Ключ отката
//   `asda.EliteMail.MultiItemRows 0` — как оригинал: запись = строка, не больше 7 записей. ═══
USTRUCT()
struct FAsdaPremiumPostRec
{
	GENERATED_BODY()
	/*+4 i32*/ UPROPERTY() int32 ItemIndex = -1;   // код предмета записи
	/*+8 u8 */ UPROPERTY() int32 Count = 0;        // единиц записи (у строк корзины — 1)
	/*+9 u8 */ UPROPERTY() int32 BuyType = 0;      // 1 = премиум-шоп, 3 = гача
};

USTRUCT()
struct FAsdaPremiumPostRow
{
	GENERATED_BODY()
	// ★ 25.10.2026: все записи 11005 этого заказа по порядку прихода (поля ниже — первой записи; `Count` — ячеек строки).
	UPROPERTY() TArray<FAsdaPremiumPostRec> Recs;
	// ★★★ МИСНЕЙМ ИСПРАВЛЕН (2026-08-28). Имя `Order` — из формат-строки лога сервера, и оно ВРЁТ:
	//   это НЕ номер заказа и НЕ идентификатор строки БД. По живым логам
	//   (`Log_MGamePremium\Buy_*.txt`: «Insert Item Info … OrderIndex 150 … Cash 150») в него
	//   попадает то, что платёжный шлюз положил в байт 174 своего ответа: для покупки в шопе —
	//   ЦЕНА предмета, для приза гачи — ОСТАТОК БАЛАНСА. Поэтому `Order` НЕ УНИКАЛЕН (в живом
	//   ящике одновременно висели две строки `Order 150` и три `Order 10000`) и НЕ НЕСЁТ НОВИЗНЫ.
	//   Разбор и что править на сервере — `Reports/elite_mailbox_order.md`.
	/*+0 i32*/ UPROPERTY() int32 Order = 0;        // «номер заказа» по имени лога; фактически цена/остаток
	/*+4 i32*/ UPROPERTY() int32 ItemIndex = -1;   // код предмета; −1 = строка пуста (эталонный маркер)
	/*+8 u8 */ UPROPERTY() int32 Count = 0;        // единиц в заказе (1…7)
	/*+9 u8 */ UPROPERTY() int32 BuyType = 0;      // 1 = премиум-шоп, 3 = гача

	bool IsEmpty() const { return ItemIndex == -1; }
};

// ═══════ ★★★ НАШЕ НОВОВВЕДЕНИЕ (В ОРИГИНАЛЕ ЭТОГО НЕТ ВООБЩЕ) — КЛЮЧ «ЗАКАЗ УЖЕ ПОКАЗАН» ═══════
//   ТРЕБОВАНИЕ ВЛАДЕЛЬЦА (2026-08-20, дословно): «Когда человек просмотрел элитный почтовый ящик
//   (в текущей игровой сессии) — не нужно больше ему оповещение отправлять иконкой моргающей!
//   Выводить заново только если новый предмет там появляется.»
//
//   ⚠ ЭТО НЕ ЭТАЛОННОЕ ПОВЕДЕНИЕ. Эталон (`netParseLocalSlotSync@0xEF0990`, п. 6) вешает иконку
//   типа 15 на КАЖДЫЙ непустой 11005 при закрытом окне и не помнит о просмотре ничего — ни флага,
//   ни списка, ни таймера (обработчик `WM_TIMER case 0x1E` снимает только тип 11, реверс §1.4).
//   Здесь заведён СВОЙ, отсутствующий в оригинале, признак «просмотрено» — и он на уровне
//   КОНКРЕТНЫХ ЗАКАЗОВ, а не общим флагом на весь ящик: иначе третий пришедший заказ не зажёг бы
//   иконку, раз «ящик уже смотрели».
//
//   ★ ПОЧЕМУ КЛЮЧ ИМЕННО ТАКОЙ — ВСЯ ЗАПИСЬ 11005 ЦЕЛИКОМ (`Order` + `ItemIndex` + `Count` +
//     `BuyType`), а не один `Order`:
//     • `Order` сервер кладёт в DB-запрос (`Recv_PremiumItemToInven_Req@0x4F8340`, поле
//       `+9 dword rec10.Order`), чтобы хранимая процедура закрыла ИМЕННО ЭТУ покупку — это
//       доказано протоколом, а не предположено;
//     • ★★★ НО ИДЕНТИФИКАТОРОМ ЗАКАЗА ОН НЕ ЯВЛЯЕТСЯ (проверено на живых логах 2026-08-28,
//       см. комментарий у поля `Order` ниже): туда попадает цена покупки либо остаток баланса,
//       и в одном списке спокойно висят две строки `Order 150` и три `Order 10000`. Тем более
//       ключом взята ВСЯ запись — так совпадение требует совпадения всех четырёх полей;
//     • переиспользование `Order` всё равно закрыто НЕ ключом, а сжатием памяти: набор
//       «просмотренных» на каждом 11005 пересекается с текущим списком, и заказ, ушедший из
//       ящика, из набора ВЫБЫВАЕТ. Значит, если сервер позже выдаст тот же `Order` новой
//       покупке, в наборе его уже нет ⇒ заказ считается новым ⇒ иконка загорится. Это и есть
//       ответ на «если `Order` переиспользуется сервером, учти это».
//   ★ Сравнение — МУЛЬТИМНОЖЕСТВОМ (совпадение «съедает» запись набора), поэтому два полностью
//     одинаковых заказа в одном списке остаются двумя, а не схлопываются в один.
USTRUCT()
struct FAsdaPremiumPostSeenKey
{
	GENERATED_BODY()
	/** Копия полей записи 11005 — идентичность заказа. Значения по умолчанию = «пустая строка». */
	UPROPERTY() int32 Order = 0;
	UPROPERTY() int32 ItemIndex = -1;
	UPROPERTY() int32 Count = 0;
	UPROPERTY() int32 BuyType = 0;

	FAsdaPremiumPostSeenKey() {}
	FAsdaPremiumPostSeenKey(int32 InOrder, int32 InItem, int32 InCount, int32 InBuyType)
		: Order(InOrder), ItemIndex(InItem), Count(InCount), BuyType(InBuyType) {}
	explicit FAsdaPremiumPostSeenKey(const FAsdaPremiumPostRow& R)
		: Order(R.Order), ItemIndex(R.ItemIndex), Count(R.Count), BuyType(R.BuyType) {}

	bool operator==(const FAsdaPremiumPostSeenKey& O) const
	{
		return Order == O.Order && ItemIndex == O.ItemIndex && Count == O.Count && BuyType == O.BuyType;
	}

	/** Для диагностических дампов (`asda.Post.SeenDump`). */
	FString ToText() const
	{
		return FString::Printf(TEXT("Order=%d ItemIndex=%d Count=%d BuyType=%d"), Order, ItemIndex, Count, BuyType);
	}
};

UCLASS()
class ASDAREBORN_API UAsdaPremiumPostSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPremiumPostSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** ★ Число строк ящика. Ровно 7 — и в разметке (`Primium_Item_post_L1…L7`), и в модели
	 *  эталона (`this + 636 + 10*i`, i < 7), и в буфере 11007 (7 записей по 57 б). */
	static constexpr int32 kRows = 7;
	/** ★ Иконок в строке. Тоже 7 (69011…69017 и далее шагом 10), и это же — потолок `Count`. */
	static constexpr int32 kCols = 7;
	/** ★ U6 (В1 дедупа №2): запись 10 б пакета 11006 «в инвентарь» — +0 i32 Order, +4 i32 ItemIndex,
	 *  +8 u8 количество (кламп 0…kCols), +9 u8 BuyType. Отдельно от отправки — ради побайтного самотеста. */
	static TArray<uint8> BuildToInvenRec11006(int32 Order, int32 ItemIndex, int32 Count, int32 BuyType);

	// ═══════════════════ ОКНО ═══════════════════
	/** Порт `uiBagWindow_OpenAndSync@0xEF1B30`: показать окно 69000 и запросить содержимое (11004).
	 *  @param bAlsoOpenInventory — ★ ОТСТУПЛЕНИЕ ПО ТРЕБОВАНИЮ ВЛАДЕЛЬЦА (разбор в шапке файла):
	 *         true  — открыть ВМЕСТЕ с инвентарём (наше поведение по умолчанию);
	 *         false — эталон: инвентарь ЗАКРЫВАЕТСЯ (`sowelWindowClose(g_pInventoryWnd, 1)`). */
	UFUNCTION(BlueprintCallable, Category="Asda|ЭлитныйЯщик")
	bool OpenAndSync(bool bAlsoOpenInventory = true);

	/** Порт `sub_92548B`: крестик 69089, кнопка «Закрыть» 69083 и Esc (`a3 == 0x15`). */
	UFUNCTION(BlueprintCallable, Category="Asda|ЭлитныйЯщик")
	void CloseWindow();

	UFUNCTION(BlueprintCallable, Category="Asda|ЭлитныйЯщик")
	void ToggleWindow();

	UFUNCTION(BlueprintPure, Category="Asda|ЭлитныйЯщик")
	bool IsWindowVisible() const;

	/** ★ КЛЮЧ ОТКАТА К ЭТАЛОНУ (`asda.Post.LegacyCloseInven`). true ⇒ открытие ящика ЗАКРЫВАЕТ
	 *  инвентарь, как в оригинале (`sowelWindowClose(g_pInventoryWnd, 1)` внутри
	 *  `uiBagWindow_OpenAndSync`). По умолчанию false — требование владельца «открывать оба». */
	void SetLegacyCloseInventory(bool bOn) { bLegacyCloseInventory = bOn; }
	bool IsLegacyCloseInventory() const { return bLegacyCloseInventory; }

	// ═══════════════════ МОДЕЛЬ ═══════════════════
	/** Выбранная строка (порт `this[155]`), 0…6. */
	UFUNCTION(BlueprintPure, Category="Asda|ЭлитныйЯщик")
	int32 GetSelectedRow() const { return SelectedRow; }

	/** Выбрать строку. Порт команды 66817 от радиогруппы 69081 — И ЖЕ точка входа исправления
	 *  дефекта A (клик по иконке строки). */
	UFUNCTION(BlueprintCallable, Category="Asda|ЭлитныйЯщик")
	void SelectRow(int32 Row);

	/** ★ 07.11.2026 `asda.EliteMail.MultiSelect`: щелчок по галочке или иконке строки. Ставит или снимает отметку, другие строки не трогает. */
	UFUNCTION(BlueprintCallable, Category="Asda|ЭлитныйЯщик")
	void ToggleRow(int32 Row);

	/** Отмечена ли строка для забора. При выключенном ключе отмечена только выбранная строка. */
	UFUNCTION(BlueprintPure, Category="Asda|ЭлитныйЯщик")
	bool IsRowChecked(int32 Row) const;

	/** Кнопка 69082 «Положить в рюкзак» — отправка 11006 (порт `net_sendSlotSyncOpcode11006`). */
	UFUNCTION(BlueprintCallable, Category="Asda|ЭлитныйЯщик")
	void TakeSelected();

	// ═══ ★★★ ОТЛОЖЕННЫЙ ПОКАЗ ПРИЗА С ВЫБОРОМ «ПРОФЕССИЯ × УРОВЕНЬ» (замечание владельца №2) ═══
	//   ОРИГИНАЛ показывает предмет в сумке СРАЗУ (разбор — `Reports/elite_mailbox_fix3.md` §2):
	//   `inventorySyncApplyPacket` кладёт запись в `g_InventoryBag` и перерисовывает иконку ещё до
	//   показа окна 68500, признака «ждёт выбора» у записи нет вовсе. Незаметно это было потому,
	//   что оригинал на этом маршруте ЗАКРЫВАЕТ инвентарь (`sowelWindowClose(g_pInventoryWnd, 1)`
	//   внутри `uiBagWindow_OpenAndSync`), а мы по требованию владельца держим его открытым.
	//   ⇒ Это ОСОЗНАННОЕ ОТСТУПЛЕНИЕ, и оно ВЫКЛЮЧЕНО по умолчанию. Решение — за владельцем.
	/** true ⇒ запись приза не кладётся в модель инвентаря, пока игрок не подтвердит вариант.
	 *  Ключ консоли `asda.Post.HideUntilChoice`. По умолчанию false — как в оригинале. */
	void SetDeferItemUntilChoice(bool bOn) { bDeferItemUntilChoice = bOn; }
	bool IsDeferItemUntilChoice() const { return bDeferItemUntilChoice; }
	/** Зовёт подсистема гачи, когда история с выбором варианта закончилась.
	 *  @param bSwapApplied true — сервер провёл обмен (6582) и уже положил ВЫБРАННЫЙ вариант, наша
	 *         отложенная запись больше не нужна; false — выбор отменён/отказан, кладём отложенное. */
	void OnVariantChoiceFinished(bool bSwapApplied);
	/** [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: итог окна 68500, открытого ДО 11006 (зовёт подсистема гачи).
	 *  @param bChosen true — ОК, вариант `Variant` для предмета `BaseCode`: ящик шлёт 11006, мост потом шлёт 6581;
	 *         false — крестик: 11006 не уходит, заказ остаётся в ящике, отметка с него снимается. */
	void OnPreTakeChoiceDone(bool bChosen, int32 BaseCode, int32 Variant);
	/** Ждёт ли ящик выбора варианта ДО отправки 11006 (самотест). */
	bool IsWaitingPreTakeChoice() const { return bPreChoiceWait; }

	/** Сколько непустых строк сейчас в ящике. */
	UFUNCTION(BlueprintPure, Category="Asda|ЭлитныйЯщик")
	int32 GetFilledRowCount() const;

	// ══════ ★★★ НАШЕ НОВОВВЕДЕНИЕ: «ПРОСМОТРЕННЫЕ ЗАКАЗЫ» (требование владельца 2026-08-20) ══════
	//   Разбор ключа и обоснование выбора — у `FAsdaPremiumPostSeenKey` выше. Эталон такого
	//   механизма не имеет ВООБЩЕ, поэтому у него есть выключатель отката к эталонному поведению.
	/** Сколько заказов в текущем списке игрок ещё НЕ видел. Ровно это число решает, гореть иконке. */
	UFUNCTION(BlueprintPure, Category="Asda|ЭлитныйЯщик")
	int32 GetUnseenOrderCount() const { return UnseenCount; }
	/** Сколько заказов помечено просмотренными (набор всегда ⊆ текущего содержимого ящика). */
	UFUNCTION(BlueprintPure, Category="Asda|ЭлитныйЯщик")
	int32 GetSeenOrderCount() const { return SeenOrders.Num(); }
	/** ★ КЛЮЧ ОТКАТА К ЭТАЛОНУ (`asda.Post.SeenGate 0`): выключить признак «просмотрено» — иконка
	 *  снова будет загораться на каждый непустой 11005, как в оригинале. По умолчанию включено —
	 *  это прямое требование владельца. */
	void SetSeenGateEnabled(bool bOn);
	bool IsSeenGateEnabled() const { return bSeenGateEnabled; }
	/** Забыть всё просмотренное (отладка + смена персонажа). */
	void ResetSeenOrders(const TCHAR* Reason);

	const FAsdaPremiumPostRow& GetRow(int32 Row) const;

	// ═══════════════════ ДИАГНОСТИКА ═══════════════════
	/** КАЖДЫЙ разобранный контрол окна (закон `full-structural-analysis-always`). */
	FString DumpControls() const;
	/** Модель: 7 строк + 7×7 ячеек + выбор + гейты. */
	FString DumpModel() const;
	/** Эмуляция прихода 11005 без сервера: N записей по 10 б. */
	void SimulateList(const TArray<uint8>& Body);
	/** ★ Эмуляция прихода 11007 (результат забора) без сервера: одна запись предмета в контейнере 1.
	 *  Нужна ЖИВОЙ проверке гейта окна 68500 (замечание владельца №1): именно этот пакет решает,
	 *  открывать ли выбор варианта. Кадр собирается ровно как серверный — 409 б. */
	void SimulateApply(int32 CharId, int32 ItemCode, int32 BuyType);
	/** ★ Прогнать тот же путь, что делегат `OnWorldReady` (4055/4056) — живая проверка пункта №6. */
	void SimulateWorldReady();
	/** Состояние механизма «мигающая иконка при входе в мир и смене карты». */
	FString DumpNoticeState() const;
	/** ★ НАШЕ НОВОВВЕДЕНИЕ: набор просмотренных заказов + что из текущего списка ещё не показано. */
	FString DumpSeenState() const;
	/** ★ Прогнать клик по иконке ячейки ТЕМ ЖЕ путём, которым его шлёт прозрачная кнопка-оверлей
	 *  общей сетки (`UAsdaIconCellGrid::SimulateClick` зовёт тот самый колбэк). Нужен живой
	 *  проверке исправления дефекта A: мышью в headless-PIE не кликнуть, а второй путь обработки
	 *  клика ради теста заводить нельзя. Возвращает выбранную после клика строку. */
	int32 SimulateIconClick(int32 Cell);
	/** ★ 07.11.2026: нажать кнопку галочки `Primium_Item_post_BOX_SEL00_OPT<Row>`. Срабатывает её настоящий `OnClicked`, как от мыши. */
	void SimulateCheckboxClick(int32 Row);
	/** ★★★ ЖИВОЙ ХИТ-ТЕСТ ОКНА (жалоба «приходится нажимать по два раза»). Для каждого кликабельного
	 *  контрола окна печатает РЕАЛЬНЫЙ экранный прямоугольник и — главное — кого Slate реально
	 *  возвращает под центром этого прямоугольника (`LocateWindowUnderMouse`). Сразу видно и
	 *  перекрытие соседним виджетом, и промах разметки, и свёрнутого родителя. Плюс z-порядок ВСЕХ
	 *  открытых окон: перевешивание окна между нажатием и отпусканием съедает клик (разбор —
	 *  `UAsdaWindowSubsystem::SetWindowZOrder`). */
	FString DumpHitTest() const;
	/** ★ Проба: поднять окно наверх и тут же сравнить геометрию «галочки» и кнопки ДО и ПОСЛЕ.
	 *  Нужна, чтобы фактом ответить, ломает ли перевешивание окна покадровый хит-тест радиогруппы
	 *  (у виджетов-кнопок оно отбирает захват мыши — см. `UAsdaWindowSubsystem::SetWindowZOrder`). */
	FString ProbeRaise();
	// ── 25.10.2026: доступ самотеста строк корзины `asda.EliteMail.RowsSelfTest` ([ИСПРАВЛЕНИЕ] строка = заказ) ──
	void DebugFeedApply(const TArray<uint8>& Body409) { ApplyResultBody(Body409); }   // 11007 тем же обработчиком (без Tick-очереди)
	// Перехват 11006: запись 10 б кладётся в `DebugTakeSentRecs` вместо сокета, защёлка ставится как после отправки.
	void DebugSetTakeCapture(bool bOn) { bDebugTakeCapture = bOn; DebugTakeSentRecs.Reset(); }
	const TArray<TArray<uint8>>& DebugTakeSent() const { return DebugTakeSentRecs; }   // перехваченные записи 11006
	// Свободных ячеек сумок 1 и 2 для проверки места (−1 = из модели инвентаря; самотесту без входа в мир).
	void DebugSetFreeCells(int32 Bag1, int32 Bag2) { DebugFree1 = Bag1; DebugFree2 = Bag2; }
	int32 DebugRowCellCount(int32 Row) const;              // занятых ячеек строки окна
	int32 DebugRowRecCount(int32 Row) const { return Rows.IsValidIndex(Row) ? Rows[Row].Recs.Num() : 0; }   // записей 11005 строки
	bool  DebugTakeRunning() const { return bTakeRun; }    // идёт забор строки (по 11006 на запись)
	int32 DebugCellCode(int32 Cell) const { return CellCodes.IsValidIndex(Cell) ? CellCodes[Cell] : -1; }   // код ячейки (Row*7+Col)
	bool  DebugInFlight() const { return bRequestInFlight; }   // защёлка `this[616]`

private:
	// ══ ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 СТРОКА = ЗАКАЗ (`asda.EliteMail.MultiItemRows`) ══
	/** Потолок записей 11005: 7 строк × 7 ячеек (DBPatch P85 пускает до 490 байт = 49 записей). */
	static constexpr int32 kMaxRecs = kRows * kCols;
	/** Ячейки строки `Row` из её записей (каждая запись — `Count` одинаковых ячеек), не больше 7. */
	void FillRowCells(int32 Row);
	/** Контейнер, в который сервер положит предмет (`sub_5340B0@0x5340B0`): 1 — снаряжение/спец-таблица, 2 — прочее. */
	int32 ContainerForCode(int32 ItemCode) const;
	/** Свободных ячеек контейнера 1/2 (модель инвентаря либо подстановка самотеста). */
	int32 FreeCells(int32 Container) const;
	/** Хватит ли места под ВСЮ строку (иначе нотис «Нет места в рюкзаке» и забора нет). */
	bool CheckRoomForRows(const TArray<int32>& RowIdx) const;
	/** Строки, которые заберёт кнопка 69082: отмеченные сверху вниз, при выключенном ключе одна выбранная. */
	TArray<int32> PickedRows() const;
	/** Первая строка очереди `TakeQueue` становится текущей, её первая запись уходит в 11006. false — не ушла. */
	bool StartNextTakeRow();
	/** Забыть весь забор: текущую строку и очередь. */
	void ResetTake();
	/** Отправить запись заказа: если у предмета выбор «профессия × уровень» — сначала окно 68500 (выбор до получения).
	 *  true — 11006 ушёл или ждём выбора; false — отправить не удалось. */
	bool SendTakeRec(int32 Order, const FAsdaPremiumPostRec& Rec);
	/** Отправить 11006 одной записи заказа `Order` без окна выбора. true — ушёл (защёлка взведена). */
	bool SendTakeRecNow(int32 Order, const FAsdaPremiumPostRec& Rec);
	/** Откроет ли 11007 по этой записи окно выбора варианта (те же условия, что в `ApplyResultBody`, контейнер — по таблице). */
	bool RecNeedsChoice(const FAsdaPremiumPostRec& Rec) const;
	/** Отмена выбора до получения: заказ остаётся в ящике, очередь идёт дальше либо кончается. */
	void SkipPreChoiceOrder();
	bool bPreChoiceWait = false;          // окно 68500 открыто до 11006, очередь стоит
	int32 PreChoiceOrder = -1;            // заказ, для которого выбирают вариант
	FAsdaPremiumPostRec PreChoiceRec;     // его запись (уйдёт в 11006 после ОК)
	bool bAwaitSwap = false;              // 11007 взят, 6581 по выбранному варианту в полёте — ждём 6582
	/** Очередь забора строки: записи, которые ещё не отправлены; заказ; счёт. */
	TArray<FAsdaPremiumPostRec> TakeLeft;
	/** ★ 07.11.2026: снимок отмеченных строк, ещё не начатых. Снимок нужен потому, что список в окне может обновиться во время забора. */
	TArray<FAsdaPremiumPostRow> TakeQueue;
	/** Сколько строк забора закончено целиком. */
	int32 TakeRowsOk = 0;
	/** Отмеченные заказы. Ключ — `Order`: при `MultiItemRows` он уникален среди строк окна, а номера строк меняются после 11005. */
	TSet<int32> CheckedOrders;
	int32 TakeOrder = 0;
	int32 TakeTotal = 0;
	int32 TakeOk = 0;
	bool  bTakeRun = false;        // забор строки идёт
	bool  bTakeSendNext = false;   // 11007 код 1 принят — следующую запись шлёт Tick
	bool  bDebugTakeCapture = false;            // самотест: 11006 не уходит в сокет
	TArray<TArray<uint8>> DebugTakeSentRecs;    // самотест: перехваченные записи 11006
	int32 DebugFree1 = -1;                      // самотест: свободных ячеек сумки 1 (−1 = из инвентаря)
	int32 DebugFree2 = -1;                      // самотест: свободных ячеек сумки 2 (−1 = из инвентаря)
	// ── сеть ──
	/** 11005 из сетевого колбэка: тело откладывается и разбирается в `Tick`
	 *  (закон `no-reentrant-net-callbacks` — отсюда трогать окна и хотбар нельзя). */
	void OnListPacket(const TArray<uint8>& Body);
	/** 11007 из сетевого колбэка — так же откладывается (он пишет в модель инвентаря). */
	void OnApplyPacket(const TArray<uint8>& Body409);
	/** Порт `netParseLocalSlotSync@0xEF0990` (разбор + заливка + иконка уведомления). */
	void ApplyListBody(const TArray<uint8>& Body);
	/** Порт `inventorySyncApplyPacket@0xEF1320`. */
	void ApplyResultBody(const TArray<uint8>& Body409);

	// ── подписки смежных систем ──
	/** Кнопка 10082 в окне инвентаря (`FOnAsdaInvSideButton`). Своей кнопки НЕ заводим. */
	void OnInventorySideButton(int32 IdNum);
	/** Клик по иконке уведомления квик-бара (`FOnAsdaHotbarNotice`), нас интересует тип 15. */
	void OnNoticeIconClicked(int32 NoticeType);

	// ── окна и контролы ──
	/** Поднять окно 69000. Гвард — `IsWindowLive` (а НЕ проверка на null: после `CloseAll` наш
	 *  указатель остаётся ненулевым, но виджет уже не в реестре и не во вьюпорте). При
	 *  пересоздании дерева ВСЕ кэши подконтролов сбрасываются — этот дефект уже дважды ломал
	 *  систему (сетка иконок и полоса прокрутки держали указатели на снесённые виджеты). */
	bool EnsureWindows();
	/** Порт `uiInitMixWindowGrid@0xEEFE60`: привязка контролов + статические подписи из таблицы. */
	void SetupControls();
	/** Собрать имена 7 иконок строки `Row` ОБХОДОМ группы `Primium_Item_post_MARK_R{Row+1}_grp`.
	 *  ★ Почему обходом, а не по списку имён: у 69021…69077 в `gamemain_jui.ini` поле `ID` ПУСТОЕ,
	 *  поэтому импортёр назвал их по скину с idx-суффиксом (`NoImage_icon_49_0` …) — закон
	 *  `jui-importer-widget-naming`. Такие имена меняются при перегенерации WBP, а порядок детей
	 *  группы — нет. Отбрасываем подпись-`UTextBlock` и хит-план `…_HIT`. */
	void CollectRowIconNames(int32 Row, TArray<FName>& Out) const;

	/** Порт `gridWindowPopulate7x7@0xEF1DE0`: перерисовать 7×7 и (в конце) авто-выбрать НИЖНЮЮ
	 *  непустую строку — обход `k = 6…0` с выходом на первой найденной. */
	void RefreshGrid();
	/** Подсветка выбранной строки: `Box_BlueOt20` строки + состояние `SS 2` радиокнопки +
	 *  анимированная рамка 69079 над первой иконкой строки. */
	void RefreshSelectionVisual();
	/** Исправление дефекта A: клик по иконке (Cell = Row*7 + Col) выбирает СТРОКУ. */
	void OnIconClicked(int32 Cell);
	/** Порт событий 8/9/0xA: подсказка по иконке под курсором. */
	void TickTooltips();
	// ★ `TickRadioClick` СНЯТ 2026-08-27: у 69081 теперь есть настоящие кнопки
	//   `Primium_Item_post_BOX_SEL00_OPT{0..6}` В АССЕТЕ (шаг `asda_ui_missing_interactive`),
	//   клик приходит обработчиком, а не покадровым хит-тестом. Разбор — в `.cpp`, `SetupControls`.

	/** Подпись из `DT_GameText` на узел окна (порт пары `UI_FormatLocalizedNotice` + `SetText`). */
	bool SetLocText(const TCHAR* WidgetName, int32 LocId);

	// ── состояние модели (порт полей объекта окна 69000) ──
	/** 7 строк = `this + 636 + 10*i`. */
	UPROPERTY(Transient) TArray<FAsdaPremiumPostRow> Rows;
	/** Коды 7×7 ячеек = `this[21*i + 5 + 3*j]`; −1 = пусто. Индекс = Row*7 + Col. */
	UPROPERTY(Transient) TArray<int32> CellCodes;
	/** Выбранная строка = `this[155]`. */
	int32 SelectedRow = 0;
	/** Гейт «запрос уже в полёте» = `this[616]`: не даёт слать 11004/11006 повторно. */
	bool bRequestInFlight = false;
	/** ★★★ Отступление «не показывать предмет до выбора» (см. `SetDeferItemUntilChoice`).
	 *  ВКЛЮЧЕНО ПО УМОЛЧАНИЮ по прямому решению владельца (2026-08-19, «включить скрытие»).
	 *  Эталон показывает предмет сразу (`inventorySyncApplyPacket@0xEF1320` пишет запись в
	 *  `g_InventoryBag` ДО показа окна 68500), но в оригинале это не видно: там
	 *  `uiBagWindow_OpenAndSync` закрывает инвентарь, а мы по требованию владельца держим оба
	 *  окна открытыми — и порядок обнажился. Вернуть эталон: `asda.Post.HideUntilChoice 0`. */
	bool bDeferItemUntilChoice = true;
	/** Отложенная 57-байтовая запись приза и признак, что она ждёт исхода выбора. */
	TArray<uint8> DeferredRec;
	bool bDeferActive = false;
	/** Отдать отложенную запись в модель инвентаря (bApply) либо просто забыть её. */
	void FlushDeferredRecord(bool bApply, const TCHAR* Reason);

	/** ★ КЛЮЧ ОТКАТА К ЭТАЛОНУ: 1 ⇒ открытие ящика ЗАКРЫВАЕТ инвентарь, как в оригинале. */
	bool bLegacyCloseInventory = false;

	// Отложенные тела пакетов (закон `no-reentrant-net-callbacks`).
	TArray<uint8> PendingList;   bool bListPending = false;
	TArray<uint8> PendingApply;  bool bApplyPending = false;

	// ══ ★★★ СОСТОЯНИЕ ЯЩИКА ПРИ ВХОДЕ В МИР И СМЕНЕ КАРТЫ (замечание владельца №6) ══
	/** «Мир готов (4055/4056) — надо запросить 11004». Отложено в `Tick`: броадкаст приходит
	 *  ИЗ РАЗБОРА ПАКЕТА, а слать из колбэка сети запрещено (закон `no-reentrant-net-callbacks`). */
	bool bWorldEntryQueryPending = false;
	/** Секунд ждать перед попыткой (и между попытками) запроса после входа в мир. */
	float WorldEntryQueryDelay = 0.f;
	/** ★ «Иконка типа 15 ДОЛЖНА висеть» — истина ящика, а не хотбара. Нужна потому, что панель
	 *  `QUICK_NEW_WIN` поднимается лениво и ПЕРЕСОЗДАЁТСЯ при каждой смене карты
	 *  (`UAsdaHotbarSubsystem::ResetWidget` чистит массив слотов вместе с типами) — без повтора
	 *  иконка молча терялась бы и при раннем ответе сервера, и после каждого перехода. */
	bool bNoticeWanted = false;

	/** ★ ТРЕБОВАНИЕ ВЛАДЕЛЬЦА (2026-08-25): «уведомление при входе висит бесконечно — пусть
	 *  показывается секунд 10 и исчезает до следующего вызова (новая сессия или новый предмет)».
	 *  Момент включения иконки; 0 = иконка не горит. Ноль по истечении срока не ждём — по нему
	 *  зовём `MarkCurrentListSeen`, и «до следующего вызова» получается само собой: набор
	 *  просмотренного уже умеет зажигать иконку заново ровно на НОВЫЙ заказ.
	 *  ⚠ Это НАША добавка поверх оригинала: эталон снимает иконку типа 15 только кликом
	 *  (его таймер 0x1E гасит лишь тип 11). Откат — `asda.Post.NoticeSec 0`. */
	double NoticeShownAtS = 0.0;

	// ══ ★★★ НАШЕ НОВОВВЕДЕНИЕ: НАБОР ПРОСМОТРЕННЫХ ЗАКАЗОВ (требование владельца 2026-08-20) ══
	//   ⚠ ЭТОГО НЕТ В ЭТАЛОНЕ. Обоснование ключа — у `FAsdaPremiumPostSeenKey`.
	/** Заказы, которые игрок уже видел своими глазами. ИНВАРИАНТ: набор — всегда подмножество
	 *  текущего содержимого ящика. Он пересчитывается на КАЖДОМ 11005 (`ReconcileSeenOrders`),
	 *  поэтому забранный/исчезнувший заказ из набора выбывает и память не растёт: потолок — 7
	 *  записей, ровно столько строк в ящике. */
	TArray<FAsdaPremiumPostSeenKey> SeenOrders;
	/** Сколько заказов текущего списка ещё НЕ просмотрено. Единственный источник «иконке гореть». */
	int32 UnseenCount = 0;
	/** Чей это набор (`CharIndex` из 11005). Смена персонажа без перезапуска клиента (выход в
	 *  выбор персонажа и вход другим) обязана обнулять набор — ящик у каждого персонажа свой.
	 *  0 = набор ещё ничей. */
	int32 SeenOwnerCharId = 0;
	/** Выключатель нововведения: false ⇒ поведение эталона (иконка на каждый непустой 11005). */
	bool bSeenGateEnabled = true;

	/** Пересечь набор просмотренного с текущим списком (мультимножеством) и посчитать непросмотренные.
	 *  Возвращает число непросмотренных заказов; попутно сжимает `SeenOrders` до подмножества списка. */
	int32 ReconcileSeenOrders();
	/** «Игрок это увидел»: пометить ВЕСЬ текущий непустой список просмотренным и погасить иконку.
	 *  Зовётся из трёх точек просмотра — открытие окна, приход списка при открытом окне, закрытие окна. */
	void MarkCurrentListSeen(const TCHAR* Reason);
	/** Привести иконку квик-бара к `bNoticeWanted` (повесить/снять) — одна точка на всю подсистему. */
	void ApplyNoticeIcon(const TCHAR* Reason);

	// ── виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;           // 69000
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TitleText = nullptr;     // 69001 (loc 110666)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  NoteText = nullptr;      // 69002 (loc 110667)
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> SelFrame = nullptr; // 69079 `Slot_AniSelect`
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowGroups;           // `Primium_Item_post_L{n}_grp`
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowHighlights;       // `Box_BlueOt20` каждой строки
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RadioImages;         // 69081: `Smelting_V7Btn_2_…_{0..6}`
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  IconImages;          // 49 иконок (для хит-теста подсказки)
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> IconGrid = nullptr;// общая сетка иконок (49 ячеек)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;     // GC-якоря обработчиков кнопок

	bool bReady = false;              // контролы разобраны
	int32 TipHoverCell = -1;          // ячейка, по которой сейчас показана общая плашка (−1 нет)
	/** Кадровый опрос мыши — ОБЩАЯ точка порта (фронты ЛКМ + курсор в координатах Slate).
	 *  Нужен клику по радиокнопке 69081: у неё в UMG нет ни кнопки, ни одного контрола. */
	FAsdaMouseFrame Mouse;

	/** Мир готов (4055/4056): и первый вход в игру, и КАЖДАЯ смена карты. Ставит флаг запроса. */
	void OnWorldReady();
	/** Отложенный запрос 11004 после входа в мир + удержание иконки уведомления (см. поля выше). */
	void TickWorldEntry(float DeltaTime);

	// `HSelfSpawn` — подписка на 4003 `SpawnSelf`: в этой стойке именно он, а НЕ 4055/4056,
	//   означает «игрок в мире» (см. комментарий у подписки в `Initialize`).
	FDelegateHandle HList, HApply, HSideBtn, HNotice, HWorldReady, HSelfSpawn;
};
