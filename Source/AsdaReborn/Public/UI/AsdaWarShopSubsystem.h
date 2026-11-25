// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaWarShopSubsystem — ВОЕННЫЙ МАГАЗИН: окно `HONOR_itemmall_Win` (JUI IDNum **67200**).
//   Открывается кнопкой диалога NPC **30070** («Военный магазин»; NPC 133/179/180).
//   Полный реверс потока с адресами — `Reports/repair_and_warshop_port.md` §2.
//   Полный структурный разбор контролов — `Docs/UI/Premium/HONOR_itemmall_Win.md`.
//
// ═══════════════ ★★★ ЧТО ЭТО ЗА ОКНО И ЗА ЧТО ТАМ ПЛАТЯТ ═════════════════════════════════════
//   Это НЕ «Honor Shop за очки чести» (так его звали прежние документы) и НЕ магазин NPC 1300.
//   Серверный обработчик называется `CharRoseWarStoreBuy@0x514FE0` — «магазин войны Роз».
//   Витрина берётся из `./Data/Honor_price_table.bin` (140 строк по 48 б), и у ВСЕХ 140 строк
//   цена — ОДИН И ТОТ ЖЕ ПРЕДМЕТ **20628 «Монеты славы»** (подпись поля 111928) плюс требование
//   ВОИНСКОГО ЗВАНИЯ фракции. Золото здесь не тратится.
//
// ═══════════════ УСТРОЙСТВО ОКНА ═════════════════════════════════════════════════════════════
//   Двенадцать карточек 3×4 (`67210+10i` … `67218+10i`, i = 0…11), переключатель категорий
//   `67202` (110520 «Воин», 110521 «Лучник», 110522 «Маг», 110523 «Обычный»), страницы
//   `67331`/`67332` + кнопки `67333`/`67334`, счётчик валюты `67335`/`67336`, крестик `67349`.
//   Значок «только для победившей армии» — `67401+i` (виден при поле записи +40 == 1).
//   ПОДТВЕРЖДЕНИЕ ПОКУПКИ — ОТДЕЛЬНОЕ окно `ICON_mssageBOX` (**57500**), а не msgbox 1500/1520:
//   иконка 57501, текст 57502 (нотис 110600), OK 57503 (12519), Отмена 57504 (12864).
//
// ═══════════════ ЗАПИСЬ ВИТРИНЫ (48 б, `HonorPriceTable_Load@0xB76BB0`) ═══════════════════════
//     +0  StorePos (индекс строки — уходит в пакет)   +24 код цены колонки 1 (в данных всегда −1)
//     +4  код товара                                  +28 количество колонки 0
//     +8  флаг (0/1/10; читателей в срезе нет)        +32 значение колонки 1 = ТРЕБУЕМОЕ ЗВАНИЕ
//     +12 тип колонки 0 (1 = предмет)                 +36 категория (0…3, фильтр 67202)
//     +16 тип колонки 1 (2 = звание)                  +40 «только победившей армии»
//     +20 код цены колонки 0 (20628)                  +44 фильтр города (−1 любой, 0/1/2)
//   Имена воинских званий — `./Data/CampWar_HonorTable.BIN` (21 запись; на диске 56 б,
//   в памяти 40 б: MD5 16 б отбрасывается, два числа −43, имя побайтово −1) — ровно то, что
//   читает `sub_935854` → `sub_CA1530@0xCA1530` (`имя = запись+8`).
//
// ★★★ ГДЕ ЭТИ ДАННЫЕ ЛЕЖАТ В ПОРТУ (2026-08-26). Оба файла оригинала РАЗОБРАНЫ импортёром
//   `Tools/asda_import_war_shop.py` и переведены в игровые таблицы:
//     `/Game/AsdaAssets/Data/Character/DT_WarShopList` — 140 строк `FAsdaWarShopRow`, ключ `StorePos`;
//     `/Game/AsdaAssets/Data/Character/DT_WarRank`     —  21 строка `FAsdaWarRankRow`, ключ `RankIndex`.
//   Рантайм читает ТОЛЬКО их (`AsdaDt::Cached`); прямого чтения `.bin` с диска БОЛЬШЕ НЕТ.
//   Исходные `.bin` перенесены из `Content/` в `RawAssets/Data/` (отчёт `Reports/raw_data_to_datatables.md`).
//
// ═══════════════ ПОТОК ПОКУПКИ ════════════════════════════════════════════════════════════════
//   Клик по ИКОНКЕ товара (`67214+10i`, эталонное сообщение 0xB) — ПРИМЕРКА на своём персонаже.
//   Клик по КНОПКЕ покупки (`67212+10i`, `charEquipFromUniconSlot@0xB76EB0`) — сперва гейт
//   «только победившая армия» (отказ нотисом 111925), затем та же примерка и показ окна 57500.
//   «OK» окна 57500 шлёт **6162**, ответ **6163** разбирает `uiHandleItemActionResult@0xB75580`.
//   Пока ответ не пришёл, стоит эталонный флаг `filler[932]`, и повторный запрос не уходит.
//
// ═══════════════ ПРИМЕРКА ═════════════════════════════════════════════════════════════════════
//   Эталон меняет модель СВОЕГО персонажа: `charToggleEquipSlotByItem@0xB77380` пишет код в
//   `preview[itemType]` (снимая — возвращает прежний из «отката»), а `charRefreshEquipSlots
//   @0xB77530` раскатывает весь набор через `tagCHARACTER_SetPartsModelByType_NoUpdate`.
//   Закрытие окна (`uiHonorShopDlgOpen@0xB76AA0`, имя врёт — это ЗАКРЫТИЕ) восстанавливает
//   снимок реального надетого (`honorShopBackupSlots@0xB76870`).
//   В порту это делает ОБЩАЯ точка `UAsdaWorldSubsystem::ApplySelfEquipPart` (закон
//   `port-shared-points`); откат — `asda.WarShop.Preview 0`.
//
// ═══════════════ ОТСТУПЛЕНИЯ ══════════════════════════════════════════════════════════════════
//   1. В `.JUI` у ВСЕХ контролов карточек поле `ID` ПУСТОЕ (реестр отложенного, п. 70), поэтому
//      импортёр назвал их по СКИНУ с idx-суффиксом. Имена сняты дампом реального
//      `WBP_HONOR_itemmall_Win` и строго регулярны: карточка i начинается с индекса `14 + 11*i`.
//      Тот же приём уже применён к сетке призов премиум-гачи (`UAsdaIconCellGrid::SetupNamed`).
//   2. Текстовых контролов карточки (имя товара 67211, цены 67215/67217) в WBP НЕТ вовсе
//      (у `Text`-скина без `ID` импортёр создаёт только хит-зону). Подписи строятся в рантайме
//      по прямоугольникам этих зон — тем же приёмом, каким магазин достраивает полосу 1365.
//   3. Подписи четырёх категорий эталон кладёт в сам контрол-переключатель
//      (`CListBox_SetItemString`); у нас переключатель импортирован как четыре кнопки
//      `HONOR_item_Radio_OPT0…3`, поэтому подписи — четыре рантайм-`TextBlock` над ними.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "Engine/DataTable.h"   // FTableRowBase — обе таблицы магазина живут в DataTable
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaWarShopSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;

// ═══ ОДНА СТРОКА ВИТРИНЫ — 12 × i32 записи `Honor_price_table.bin` (раскладка в шапке файла) ═══
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaWarShopRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 StorePos     = 0;    // +0
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 ItemCode     = 0;    // +4
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 Flag8        = 0;    // +8
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 PriceType0   = 0;    // +12
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 PriceType1   = 0;    // +16
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 PriceCode0   = 0;    // +20
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 PriceCode1   = 0;    // +24
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 PriceQty0    = 0;    // +28
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 PriceQty1    = 0;    // +32
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 Category     = 0;    // +36
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 VictorOnly   = 0;    // +40
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 RegionFilter = -1;   // +44
};

// ═══ ОДНО ВОИНСКОЕ ЗВАНИЕ — запись `CampWar_HonorTable.BIN` ПОСЛЕ раскодировки эталона ═══
//   На диске запись 56 б: 16 б MD5 + 40 б тела. `CMap_LoadCampWarHonorTable@0xCA0CC0` MD5 ОТБРАСЫВАЕТ
//   (он служит только контролем целостности файла), из двух чисел тела вычитает 43, а каждый байт имени
//   уменьшает на 1. В DataTable ложится УЖЕ РАСКОДИРОВАННОЕ тело — то, что эталон держит в памяти.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaWarRankRow : public FTableRowBase
{
	GENERATED_BODY()

	// Номер звания (тело +0 − 43). Совпадает с порядковым номером записи во всех 21 строке.
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 RankIndex = 0;
	// Сколько чести нужно накопить для этого звания (тело +4 − 43).
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") int32 HonorRequired = 0;
	// Название звания (тело +8, char[32], каждый байт − 1). У записи 0 ПУСТО — так в данных.
	UPROPERTY(BlueprintReadOnly, Category="Asda|WarShop") FString Title;
};

// Виджеты одной карточки. Все — из WBP, кроме трёх подписей (см. отступление 2 в шапке).
USTRUCT()
struct FAsdaWarShopCardUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> CardParts;   // всё, что гасится вместе с карточкой
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SelParts;    // рамка выбора 67213+10i (8 планов)
	UPROPERTY(Transient) TObjectPtr<UWidget>    BuyButton = nullptr;   // 67212+10i (план кнопки)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyCaption= nullptr;   // подпись кнопки (нотис 6857)
	UPROPERTY(Transient) TObjectPtr<UImage>     ItemIcon  = nullptr;   // 67214+10i, 64×64
	UPROPERTY(Transient) TObjectPtr<UImage>     RankIcon  = nullptr;   // 67216+10i, 16×16 (Faction_Grade)
	UPROPERTY(Transient) TObjectPtr<UWidget>    Badge     = nullptr;   // 67401+i
	UPROPERTY(Transient) TObjectPtr<UWidget>    NameZone  = nullptr;   // хит-зона 67211+10i
	UPROPERTY(Transient) TObjectPtr<UWidget>    Price0Zone= nullptr;   // хит-зона 67217+10i
	UPROPERTY(Transient) TObjectPtr<UWidget>    Price1Zone= nullptr;   // хит-зона 67215+10i
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText  = nullptr;   // рантайм-подпись
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Price0Text= nullptr;   // подпись 67217+10i: у карточки 0 — строка ассета `HONOR_itemmall_Win_TXT01`, у прочих — рантайм в её оформлении (10.10.2026)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Price1Text= nullptr;   // подпись 67215+10i: у карточки 0 — строка ассета `HONOR_itemmall_Win_TXT00`, у прочих — рантайм в её оформлении (10.10.2026)
	// Что сейчас показано в карточке (−1 = карточка пуста).
	UPROPERTY(Transient) int32 RowIndex = -1;
};

UCLASS()
class ASDAREBORN_API UAsdaWarShopSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaWarShopSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (из `.JUI` и `UI_HonorShopDlg_Init`, ни одно не выдумано) ──
	static constexpr int32 kCards       = 12;      // 67210…67320 шагом 10
	static constexpr int32 kEquipSlots  = 11;      // снимок надетого для примерки
	static constexpr int32 kCoinItem    = 20628;   // «Монеты славы» — валюта витрины
	static constexpr int32 kWarShopZ    = 730;
	static constexpr int32 kConfirmZ    = 900;     // окно 57500 поверх витрины
	// Индекс первого виджета карточки 0 в WBP и шаг между карточками (см. отступление 1).
	static constexpr int32 kCardIdxBase = 14;
	static constexpr int32 kCardIdxStep = 11;

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	UFUNCTION(BlueprintCallable, Category="Asda|WarShop")
	void OpenWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|WarShop")
	void CloseWindow();
	UFUNCTION(BlueprintPure, Category="Asda|WarShop")
	bool IsOpen() const { return bOpen; }

	// ═══ ДЕЙСТВИЯ ═══
	void OnCardIconClicked(int32 Card);   // 67214+10i — примерка
	void OnCardBuyClicked(int32 Card);    // 67212+10i — примерка + подтверждение
	void SetCategory(int32 Cat);          // 67202
	void SetPage(int32 NewPage);          // 67333 / 67334

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;
	FString DumpState() const;

	// Имя воинского звания по номеру (пусто, если номера нет в таблице — как `sub_CA1530` при 0).
	//   ★ 08.11.2026 (пачка 15, баг 9): публичный — второй читатель эталонного `sub_935854` = окно персонажа 900 (926).
	FString RankName(int32 Grade) const;

private:
	// ── Данные ──
	// Витрина и звания — из DT_WarShopList / DT_WarRank (было: прямое чтение двух .bin из Content).
	bool LoadTables();                    // `HonorPriceTable_Load` + `CMap_LoadCampWarHonorTable`

	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();
	UTextBlock* MakeLabelLike(UWidget* Ref, const TCHAR* NewName, const UTextBlock* FontDonor, bool bRightAlign);

	// ── Рендер ──
	void BuildPage();          // `GuildRankUI_BuildPage@0xB75DD0`
	void RefreshCoinText();    // 67335/67336 — «Монеты славы» и их количество
	void ShowCard(int32 Card, bool bVisible);
	void ShowCardSelection(int32 Card, bool bVisible);

	// ── Примерка ──
	void SnapshotEquip();      // `honorShopFillSlotsFromBag@0xB767C0` + `uiHonorShopDlgShow`
	void TogglePreview(int32 ItemCode);   // `charToggleEquipSlotByItem@0xB77380`
	void ApplyPreview();       // `charRefreshEquipSlots@0xB77530`
	void RestorePreview();     // `honorShopBackupSlots@0xB76870` + повторный `charRefreshEquipSlots`

	// ── Подтверждение (окно 57500) ──
	bool EnsureConfirmWidget();
	void ShowConfirm(int32 RowIndex);      // `itemTooltipBuildText@0xB76D00`
	void HideConfirm();                    // `sub_93211D` → 0xB76E80
	void OnConfirmOk();
	void OnConfirmCancel();

	// ── Сеть ──
	void SendBuy();
	void OnBuyResult(const TArray<uint8>& Body121);
	void DrainPendingPackets();

	// ── Покадровые опросы ──
	void TickCards();

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	void NoticeMsgBox(int32 LocId) const;
	void NoticeMsgBoxText(const FString& Text) const;

	// Сколько «Монет славы» у игрока (эталон считает ТОЛЬКО контейнер 2, 60 ячеек).
	int32 CoinCount() const;
	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaWarShopCardUi> Cards;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose   = nullptr;   // 67349
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnPrev    = nullptr;   // 67333
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNext    = nullptr;   // 67334
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    CatButtons;     // 67202 → OPT0…OPT3
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CatLabels;      // рантайм-подписи категорий
	// Базовые цвета подписей вкладок — снимает общая точка `AsdaUi::ApplyTabCaptionState`.
	TArray<FLinearColor> CatIdleColors;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage    = nullptr;   // 67331
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPages   = nullptr;   // 67332
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCoinLbl = nullptr;   // 67335 (111928)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCoin    = nullptr;   // 67336
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> IconGrid = nullptr;   // 12 иконок товара

	// Окно подтверждения 57500.
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ConfirmWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage>      ConfirmIcon   = nullptr;   // 57501
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ConfirmText   = nullptr;   // 57502
	UPROPERTY(Transient) TObjectPtr<UButton>     ConfirmOk     = nullptr;   // 57503
	UPROPERTY(Transient) TObjectPtr<UButton>     ConfirmNo     = nullptr;   // 57504
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ConfirmOkCap  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ConfirmNoCap  = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> ConfirmRelays;

	// ── Модель ──
	UPROPERTY(Transient) TArray<FAsdaWarShopRow> Rows;      // витрина (140 строк)
	UPROPERTY(Transient) TArray<FAsdaWarRankRow> Ranks;     // 21 звание, индекс = номер
	UPROPERTY(Transient) TArray<int32>           PageRows;  // индексы строк текущей страницы (≤ 12)
	bool  bOpen = false;
	bool  bControlsReady = false;
	bool  bTablesLoaded = false;
	bool  bPending = false;      // `filler[932]` — ждём 6163
	int32 Category = 0;          // `filler[920]`
	int32 Page = 0;              // `filler[924]`
	int32 LastPage = 0;          // `filler[928]`
	int32 SelCard = -1;          // `filler[940]`
	int32 SelRow  = -1;          // `filler[936]` — строка витрины, за которую идёт покупка
	int32 TipCard = -1;

	// Примерка: реальный снимок, текущее превью и «откат» — по индексу `itemType` (0…10).
	TArray<int32> PreviewReal;
	TArray<int32> PreviewNow;
	TArray<int32> PreviewUndo;
	// Что УЖЕ раскатано на модель — чтобы не пересобирать меш на неизменившихся частях.
	TArray<int32> PreviewApplied;
	// Счётчик «Монет славы» просит перерисовки (метит колбэк инвентаря, применяет Tick).
	bool bCoinDirty = false;

	FAsdaMouseFrame Mouse;
	TAsdaNetQueue<TArray<uint8>> PendingResults;
	FDelegateHandle HBuyResult;
	FDelegateHandle HInvCell;
};
