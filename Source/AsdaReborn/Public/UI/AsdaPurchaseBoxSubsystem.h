// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPurchaseBoxSubsystem — ★★★ ОБЩАЯ МОДАЛКА ПОДТВЕРЖДЕНИЯ ПОКУПКИ
//   `SHELLITEM_PURCHASE_BOX` (JUI IDNum **1559**, 31 узел, скин `Popup_Win`, 290×276).
//   Полный реверс — `Reports/purchase_box_1559.md`; порт — `Reports/premium_mall_port.md`.
//
// ═══════════════ ЧТО ЭТО ЗА ОКНО ══════════════════════════════════════════════════════════════
//   В оригинале это ОДНА ИЗ ФОРМ менеджера модалок `CMsgBoxMgr` (глобал `g_pItemTooltipUI` —
//   МИСНЕЙМ, это не тултип предмета, а менеджер ВСЕХ модалок: 1500 ONEBUT, 1520 TWOBUT,
//   1530 SOWELL, 1545 THREEBUT, 1552 GIFT, **1559 PURCHASE**, 1580 SHOUTING, 16500, 25000).
//   Привязка — `CMsgBoxMgr_InitControls@0xD6AD20` (`ida.c:588727…588910`).
//
//   ★★★ СЕМЬ РЕЖИМОВ — ОДНО ОКНО. Его открывают ЧЕТЫРЕ функции-заполнителя НАПРЯМУЮ, минуя
//   штатный конвейер msgbox (`ItemInfoBuyPanel_Populate@0xD7A270`,
//   `CUpgradeUI_FillItemInfoPanel@0xD7B060`, `CUpgradeUI_FillItemInfoPanelEx@0xD7C340`,
//   `upgradeUiFillConfirmPanel@0xD7BD90`), и каждая — из своей системы:
//     A кэш-магазин 6000, снаряжение   кнопка 6319  → 5070 / 5074
//     B кэш-магазин 6000, функциональн. кнопка 6353  → 5070 / 5074
//     C витрина набора (вкладка 58300) кнопка 58878 → 5070 / 5074
//     D парикмахерская внутри 6000     кнопка 58818 → 5470
//     E окно `HAIR_SHOP` 15000         кнопка 15178 → 5470
//     F бустер-шоп 15300/15515         кнопка 15527 → 5074 (подарок) либо 5070
//     G премиум-гача                   кнопка 68511 → 6608 либо 6581
//
// ═══════════════ ★★★ ЧЕМ ПОРТ ОТЛИЧАЕТСЯ ОТ ОРИГИНАЛА (дефект D3 ИСПРАВЛЕН) ═══════════════════
//   ОРИГИНАЛ. Так как окно открывается «мимо» менеджера, поле `contentKind` (+0x32F8) остаётся
//   от предыдущей модалки, и обработчик `case 1561` (`OnSendItemUpgrade@0xD74180`, `ida.c:592594`)
//   НЕ ЗНАЕТ, кто его открыл: он опрашивает состояние КАЖДОГО магазина ПЯТЬЮ НЕЗАВИСИМЫМИ `if`
//   БЕЗ `else`. Если два магазина взведены одновременно (окно 6000 в режиме крафта + открытая
//   панель набора), одно нажатие «Подтвердить» шлёт ДВА пакета 5070 — двойное списание.
//   ПОРТ. Окно принимает ЯВНЫЙ РЕЖИМ и ЯВНЫЙ КОЛБЭК (`FAsdaPurchaseBoxRequest` +
//   `TFunction<void(bool)>`); нажатие вызывает ровно один колбэк ровно одного владельца.
//   Ключ отката `asda.PremiumMall.LegacyPurchaseFanout 1` возвращает эталонное поведение
//   «разослать всем взведённым владельцам» (см. .cpp) — для сверки 1:1.
//
// ═══════════════ ПОЛЯ ОКНА (JUI-ID, а НЕ имена полей IDA — они СДВИНУТЫ) ══════════════════════
//   1560 PURCHASE_TXT1        «Кому: [ss1]» (loc 20196) — только режим подарка, иначе ПУСТО
//   1570 PURCHASE_TXT0        вопрос: подарок → loc 12922, покупка → loc 12923
//   1563 PURCHASE_TITLE       имя предмета (ПОДПИСЬ контрола, `CWidget_SetTextAll4`)
//   1564/1573                 «Профессия» (2297) / loc (8438 + usageCategory), КРАСНЫМ при несоотв.
//   1565/1574                 «Уровень» (2330) / число, КРАСНЫМ если выше уровня игрока
//   1566/1575                 «Эффект ЖК» (2360) / «min ~ max»  (у `special_itemlist` — ПУСТО, D8)
//   1567/1576                 «Вариант 1» (12927) / ВСЕГДА «Нет» (2335)   ← D7
//   1568/1577                 «Вариант 2» (12928) / ВСЕГДА «Нет» (2335)   ← D7
//   1571/1578                 «Поместить на хранение» (12926) / «хранилище» (12924) | «Рюкзак» (12925)
//   1569/1579                 «Цена» (2283) / `String_FormatThousands`
//   1572 PURCHASE_LINE1       линия-разделитель, кодом не трогается никогда
//   1561/1562                 «Подтвердить» (loc 6857) / «Отмена» (loc 2289) — подписи из init
//
// ═══════════════ ЧЕГО В ОКНЕ НЕТ (проверено дампом 31 узла, не выдумываем) ════════════════════
//   • крестика закрытия — только 1561/1562;
//   • прокрутки, иконки предмета, счётчика количества (количество попадает лишь в текст 1563);
//   • ★ НУМПАДА 1410 — не участвует ВООБЩЕ (доказано двумя методами, `purchase_box_1559.md` §5;
//     количество приходит из КОМБОБОКСА магазина `sub_C42710@0xC42710`: индекс 0…6 → 1…7).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaPurchaseBoxSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;

// ═══ РЕЖИМ = КТО ОТКРЫЛ. Буквы соответствуют таблице режимов отчёта `purchase_box_1559.md` §2.2. ═══
UENUM(BlueprintType)
enum class EAsdaPurchaseMode : uint8
{
	None       = 0 UMETA(DisplayName = "нет"),
	CashEquip  = 1 UMETA(DisplayName = "A — кэш-магазин 6000, снаряжение (кнопка 6319)"),
	CashFunc   = 2 UMETA(DisplayName = "B — кэш-магазин 6000, функциональные (кнопка 6353)"),
	Package    = 3 UMETA(DisplayName = "C — витрина набора, вкладка 58300 (кнопка 58878)"),
	HairInShop = 4 UMETA(DisplayName = "D — парикмахерская внутри 6000 (кнопка 58818)"),
	HairShop   = 5 UMETA(DisplayName = "E — окно HAIR_SHOP 15000 (кнопка 15178)"),
	Booster    = 6 UMETA(DisplayName = "F — бустер-шоп 15300/15515 (кнопка 15527)"),
	Gacha      = 7 UMETA(DisplayName = "G — премиум-гача (кнопка 68511)")
};

// ═══ ЗАПРОС НА ПОКАЗ. Ровно те аргументы, что эталон передаёт своим четырём заполнителям. ═══
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPurchaseBoxRequest
{
	GENERATED_BODY()

	// Кто открыл (эталонного аналога НЕТ — это и есть исправление D3).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") EAsdaPurchaseMode Mode = EAsdaPurchaseMode::None;

	// Код предмета. Ищется сперва в `item_list`, затем в `special_itemlist` — как эталон.
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") int32 ItemCode = 0;

	// Эталонный `panelMode`: 0 = «Хочешь купить эту вещь?» (12923), 1 = подарок (12922 + строка 1560).
	//   ★ Значение 2 у эталона ОБРАБОТАНО, но ни один вызывающий его не передаёт — ветка мёртвая (D9).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") int32 PanelMode = 0;

	// Количество. > 1 ⇒ имя предмета печатается как «%s (%d Кол-во)» (loc 6794), а у записи
	//   `special_itemlist` цена умножается на количество — 1:1 эталон (`ida.c:593627-593630`).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") int32 Quantity = 0;

	// Эталонный `tradeMode`: 0 → «хранилище» (12924), 1 → «Рюкзак» (12925), иное → строка
	//   «Поместить на хранение» вообще не заполняется (остаётся пустой, но видимой — так в эталоне).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") int32 TradeMode = -1;

	// Имя получателя подарка (эталонный глобал `byte_2BE811E` → слот 0 строки 20196).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") FString RecipientName;

	// Цена, если её задаёт вызывающий (режимы D/E: `upgradeUiFillConfirmPanel` получает `amount`
	//   параметром). −1 = взять из записи предмета, как делают режимы A/B/C/F/G.
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") int32 PriceOverride = -1;

	// Скрыть строку цены (1569 + 1579). Эталон делает это в гаче (`sub_922D5D(g_pGachaScene)`)
	//   и при `panelMode == 2` (`ida.c:593651-593662`).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") bool bHidePrice = false;

	// Явное имя (режимы D/E — у причёски своя запись, `itemName` приходит параметром).
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") FString NameOverride;

	// ★★★ УРОВЕНЬ, ЗАДАННЫЙ ВЛАДЕЛЬЦЕМ. −1 = взять из записи предмета (режимы A/B/C/G).
	//   Нужен режиму **F** (бустер-шоп): его заполнитель `CUpgradeUI_FillItemInfoPanelEx@0xD7C340`
	//   берёт уровень НЕ из `ItemRecord->requiredLevel`, а из САМОГО МАГАЗИНА —
	//   `itemLevel = sub_91FDF6(g_bModalNoticeActive)` (`ida.c:594213`; `g_bModalNoticeActive` —
	//   МИСНЕЙМ, это указатель на `CBoosterShop`), то есть значение комбобокса 15542 «Уровень».
	//   Это ОСОБЕННОСТЬ РЕЖИМА, а не дефект: у бустера уровень выбирает игрок.
	UPROPERTY(BlueprintReadWrite, Category="Asda|Покупка") int32 LevelOverride = -1;
};

UCLASS()
class ASDAREBORN_API UAsdaPurchaseBoxSubsystem : public UGameInstanceSubsystem,
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

	// Числа окна — все из `.JUI`, ни одно не выдумано.
	static constexpr int32 kWinIdNum = 1559;
	static constexpr float kWinW     = 290.f;   // ★ СВОЯ ширина; эталон центрирует по 360 (D5)
	static constexpr float kWinH     = 276.f;   // ★ СВОЯ высота; эталон центрирует по 140 (D5)
	// Модалка обязана лежать выше окна-владельца (6000 → 733, бустер/гача — полоса 8800).
	static constexpr int32 kBoxZ = 8850;

	// ═══ ЕДИНСТВЕННЫЙ ВХОД. `OnResult(true)` — «Подтвердить» (1561), `false` — «Отмена» (1562). ═══
	//   ★ Колбэк «Отмена» — ИСПРАВЛЕНИЕ D6: эталон при отказе не уведомляет никого, и состояние
	//   магазина остаётся взведённым до следующего действия игрока.
	bool Open(const FAsdaPurchaseBoxRequest& Req, TFunction<void(bool)> OnResult);

	// Закрыть без ответа (окно-владелец закрылось раньше). Колбэк НЕ зовётся.
	UFUNCTION(BlueprintCallable, Category="Asda|Покупка")
	void Close();

	UFUNCTION(BlueprintPure, Category="Asda|Покупка")
	bool IsOpen() const { return bOpen; }
	// ★ 02.11.2026 P101: запрос показанной модалки (самотест `asda.SelfTest.CashGift` сверяет режим подарка Д-К1).
	const FAsdaPurchaseBoxRequest& DebugRequest() const { return Req; }
	UWidget* DebugOkWidget() const;   // кнопка 1561 (настоящий щелчок самотеста)
	FString DebugShownName() const;   // подпись 1563 (имя товара), как на экране (самотест `asda.SelfTest.PremiumCardQty`)

	UFUNCTION(BlueprintPure, Category="Asda|Покупка")
	EAsdaPurchaseMode GetMode() const { return Req.Mode; }

	// Кнопки (публичны — их дёргают и мышь, и самотест).
	UFUNCTION(BlueprintCallable, Category="Asda|Покупка") void OnConfirmClicked();   // 1561
	UFUNCTION(BlueprintCallable, Category="Asda|Покупка") void OnCancelClicked();    // 1562

	// Диагностика (закон `full-structural-analysis-always`).
	FString DumpControls() const;
	FString DumpState() const;

private:
	bool EnsureWidget();
	void SetupControls();
	void ClearAllFieldTexts();      // порт `uiClearAllFieldTexts@0xD7D150`
	void PlayButtonSfx(const TCHAR* Why);   // звук 4 кнопок 1561/1562 — общая точка окон-сообщений (волна В6)
	void Populate();                // порт `ItemInfoBuyPanel_Populate@0xD7A270` и родственных
	FString Loc(int32 Id) const;

	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Txt1560 = nullptr;   // «Кому: [ss1]»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Txt1570 = nullptr;   // вопрос
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Txt1563 = nullptr;   // имя предмета (подпись рамки)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl1564 = nullptr;   // «Профессия»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Val1573 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl1565 = nullptr;   // «Уровень»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Val1574 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl1566 = nullptr;   // «Эффект ЖК»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Val1575 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl1567 = nullptr;   // «Вариант 1»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Val1576 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl1568 = nullptr;   // «Вариант 2»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Val1577 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl1571 = nullptr;   // «Поместить на хранение»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Val1578 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Lbl1569 = nullptr;   // «Цена»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Val1579 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk   = nullptr;   // 1561
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel = nullptr; // 1562
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapOk   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapCancel = nullptr;
	// Группы строки цены (1569 + 1579) — гасятся целиком в гаче и при `panelMode == 2`.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PriceGroup;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ═══ ★★ СХЛОПЫВАНИЕ ПУСТОЙ ПОЛОСЫ В РЕЖИМАХ **D/E** (парикмахерская) ══════════════════════
	//   ЗАЧЕМ. Эталонный заполнитель этих режимов (`upgradeUiFillConfirmPanel@0xD7BD90`) трогает
	//   ровно ЧЕТЫРЕ поля, а строки «Профессия»/«Уровень»/«Эффект ЖК»/«Вариант 1/2»/«Поместить на
	//   хранение» ОЧИЩАЕТ — они остаются пустыми, но ВИДИМЫМИ. В `.JUI` они занимают y = 30…145
	//   внутри группы `PURCHASE_TITLE`, и на экране это чёрная дыра между именем и ценой
	//   (жалоба владельца 2026-08-28).
	//   ⚠ Окно 1559 ОБЩЕЕ (магазин 6000, снаряжение, бустеры, гача, набор). Схлопывание включается
	//   ТОЛЬКО в режимах D/E; во всех прочих `ApplyHairModeLayout(false)` возвращает раскладку
	//   к ИСХОДНОЙ — поэтому переключение режимов в одной сессии ничего не ломает.
	//   ⚠ Сдвиг считается ОТ ИСХОДНОГО смещения (`AsdaUi::ShiftSlotYFromBase`), а не от текущего:
	//   иначе после первого схлопывания разница станет нулевой и окно перестанет перестраиваться
	//   (ровно тот дефект, что ловили на складе, D19).
	void ApplyHairModeLayout(bool bCollapse);
	// Склад ИСХОДНЫХ смещений узлов, которые двигает схлопывание (по одному на окно).
	TMap<TWeakObjectPtr<UWidget>, FMargin> BaseSlots;
	// Шесть строк, пустых в режимах D/E, — гасим их целиком (группа, а не только текст).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> HairEmptyRows;
	// Что едет вверх: линия, группа цены и обе кнопки.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> HairShiftDown;
	// Девять планов рамки окна 1559 — её высота уменьшается на ту же дельту.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> FramePlanes;
	UPROPERTY(Transient) TObjectPtr<UWidget> TitleGroup = nullptr;   // `PURCHASE_TITLE_grp`
	UPROPERTY(Transient) TObjectPtr<UWidget> DragPlane  = nullptr;   // `..._0_DRAG` (ручка на всё окно)
	bool bHairLayoutOn = false;

	FAsdaPurchaseBoxRequest Req;
	TFunction<void(bool)>   Result;
	bool bOpen = false;
	bool bReady = false;
	int32 CntOpen = 0;      // счётчики для дампа — видно, что точка ЖИВАЯ
	int32 CntOk   = 0;
	int32 CntCancel = 0;
};
