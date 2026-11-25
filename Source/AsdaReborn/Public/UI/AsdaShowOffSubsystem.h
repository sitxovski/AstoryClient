// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaShowOffSubsystem — «ХВАСТОВСТВО ПРЕДМЕТОМ» (Item Boast, кор. 아이템 자랑하기).
//   ПОЛНЫЙ реверс с адресами, дампом каждого контрола и разбором обеих половин протокола —
//   `Reports/showoff_system.md`. Здесь ничего не додумано: каждое число ниже имеет ссылку
//   на `RE/etalon/*.c`, `Data/Decoded/jui/gamemain_jui.ini` или `RE/ida/server_work/GameServer.c`.
//
// ═══════════════════ ЧТО ЭТО ЗА СИСТЕМА ЦЕЛИКОМ ════════════════════════════════════════════════
//   ДВЕ половины, связанные ОДНОЙ парой опкодов 6129 → 6130:
//
//   1) **МЕНЮ** `Open_Item_Win` (IDNum **28000**) — всплывашка ПО ПРАВОЙ КНОПКЕ на ячейке
//      инвентаря. Эталон `CGameWindow_OnCommand@0xD1B140`, событие **14 (0xE) = ПКМ**
//      (`ui_core.c:48036-48048`):
//          self[202] = CInventoryUI_OnSlotClick(self, cmdId);       // тот же выбор, что и по ЛКМ
//          if (self[202] && self[1065]) {                            // 1065 = контрол 28000
//              CControl_KillFocus(&g_ItemTooltip); self[222] = 0;    // тултип предмета гаснет
//              copyPair79(g_pWindowMgr, v44);                        // позиция курсора
//              CClass_MapInsert(g_pWindowMgr, self[1065]);           // поднять окно наверх
//              CControl_SetField74_75(self[1065], v44[0] + 32, v44[1] + 32);   // ★ курсор +32/+32
//              CControl_SetVisible(self[1065], 1);
//          }
//      ⇒ меню открывается ТОЛЬКО если ПКМ попала в НЕПУСТУЮ ячейку (OnSlotClick вернул 1).
//
//   2) **ОКНО-ПРИЁМНИК** `Opend_Item_Win` (IDNum **28100**) — список «кто чем похвастался»,
//      ТРИ строки «имя игрока | имя предмета цветом грейда» + колесо прокрутки + крестик.
//      Появляется у ТЕХ, КОМУ хвастовство адресовано, при приходе 6130
//      (`itemListWnd_OnFillPacket@0x974270` → `itemListWnd_FillFromPacket@0xD32EE0`).
//
// ═══════════════════ ★ СВЯЗЬ С ЧАТОМ: ЕЁ НЕТ ═══════════════════════════════════════════════════
//   Проверено сплошным просмотром обеих веток: НИ `CGameWindow_OnCommand` (28003/28007), НИ
//   `netSendTsMaskedReq6129`, НИ `itemListWnd_OnFillPacket/FillFromPacket/PageRows` не зовут
//   ни `Chat_AddChatLine`, ни `uiShowTimedNoticeText`. Ссылки на предмет в строке чата (тега
//   вида `<ITEM=…>`) в разметке клиента тоже нет — единственные inline-теги разметки это
//   `<FC=…> <FS..> <FB> <FI> <FU> </F>` (`RE/docs/TEXT_MARKUP_TAGS_RE.md`). ⇒ хвастовство
//   идёт СВОИМ окном, а не ссылкой в чате. Гипотезу задания честно закрываем как НЕ подтверждённую.
//   ★ Единственное место, где хвастовство соседствует с чатом — АВТО-АНОНС редкой добычи
//     (`CItemManager_NotifyItemInfo@0xD0C8F0`, item_inventory.c:39560-39575): при грейде ≥ 3 и
//     анонсируемом типе клиент шлёт СРАЗУ ТРИ вещи — строку чата (нотис 20250, канал 10),
//     `netSendTsMaskedReq6129(…, −1, …)` (это МЫ) и `netSendSignedPacket6616` (анонс сервера).
//     Порт уже делает 1-е и 3-е в `UAsdaHudSubsystem::NotifyItemInfo`; 2-е — метод
//     `SendShowOffForRecord` ниже (точка подключения описана в отчёте).
//
// ═══════════════════ ПРОТОКОЛ (авторитет — СЕРВЕР) ═════════════════════════════════════════════
//   **6129 C→S** `OP_RECV_ITEM_INFO_DISPLAY`, 33 б = 8 токен + 16 MD5 + 9 payload:
//       @0 i32 userIdx отправителя · @4 i16 userIdx адресата (−1 = «всем вокруг») ·
//       @6 u8 контейнер (запись+4) · @7 i16 слот (запись+5).
//   **6130 S→C** `OP_ITEM_LIST_WND_ON_FILL_PACKET`, тело 81 б:
//       @0 i32 (клиент НЕ читает) · @4 char[20] имя отправителя cp1251 · @24 запись предмета 57 б.
//   **СЕРВЕР** `Recv_ItemInfoDisplay@0x55BF80` (GameServer.c:133853) — обработчик ЕСТЬ, патч НЕ нужен:
//       • `sub_523F00(pkt, 9, idx, 1)` — длина payload ровно 9;
//       • `sub_524390(...)` — анти-реплей/MD5;
//       • `v6[0] == sub_5324F0(this, arg0)` — заявленный userIdx обязан совпасть с реальным
//         (подделать «от чужого имени» нельзя);
//       • `sub_4D61E0(itemMgr, arg0, контейнер, слот, out57)` == 1 — предмет обязан РЕАЛЬНО лежать
//         в этом контейнере/слоте (иначе тишина, отказа клиенту НЕ шлётся);
//       • target < 0 → `SendPacket_ClippingSectorInAllUser(mapMgr, сектор, 6130, 81, …, −1, 1)` —
//         ВСЕ игроки в клиппинг-секторе отправителя, ВКЛЮЧАЯ САМОГО ОТПРАВИТЕЛЯ;
//       • target ≥ 0 → `sub_4C28F0(…, 6130, 81, target, …)` — ТОЛЬКО адресату.
//     ★ Проверок уровня, кулдауна, партии, клана, соулмейта на сервере НЕТ.
//
// ═══════════════════ КОМУ АДРЕСОВАНО: ДВА РЕЖИМА, А НЕ ПЯТЬ ════════════════════════════════════
//   В раскладке меню ПЯТЬ кнопок-адресатов, но живых только ДВЕ:
//     28003 «Всем рядом»        Visible=1  → 6129 с target = −1   (CONFIRMED, ui_core.c:47687)
//     28007 «Только выбранному» Visible=1  → 6129 с target = userIdx ВЫБРАННОЙ ЦЕЛИ (47698)
//     28004 «Только группе»     Visible=0  → обработчика cmdId 28004 в эталоне НЕТ
//     28005 «Только клану»      Visible=0  → обработчика НЕТ
//     28006 «Только соулмейту»  Visible=0  → обработчика НЕТ
//   ★★★ И на СЕРВЕРЕ этих режимов тоже нет: в payload 6129 адресат — ОДИН i16, третьего значения
//     («партия»/«клан») протокол не выражает. ⇒ три кнопки мертвы в ДАННЫХ, а не только в коде.
//     Мы их НЕ оживляем (это была бы выдумка) и НЕ удаляем (они есть в .JUI) — оставляем скрытыми.
//   У 28007 нет цели ⇒ эталон показывает нотис **102003** «Нет выбранной цели» в общей плашке
//     (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, 24, текст, 1)`), пакет НЕ шлётся.
//
// ═══════════════════ ОКНО-ПРИЁМНИК: ЖИЗНЕННЫЙ ЦИКЛ 1:1 ════════════════════════════════════════
//   • Приход 6130 → запись добавляется В КОНЕЦ списка окна. Список — контейнер эталона
//     `this+1088`; при `size > 20` вырезается ПЕРВЫЙ элемент и потолок держится 20
//     (`itemListWnd_FillFromPacket`, item_inventory.c:45425-45431).
//   • Показываются ПОСЛЕДНИЕ ТРИ записи: `PageRows(this, max(count − 3, 0))`.
//   • Окно показывается, только если включена ОПЦИЯ `g_bOptShowOtherChars` — чекбокс **875**
//     окна настроек (`ui_core.c:80610-80620`), подпись = нотис **102004** «Show the Item Boast
//     window». Выключение опции НЕМЕДЛЕННО прячет окно. Окна настроек в порту ещё нет, поэтому
//     опция живёт как CVar `asda.ShowOff.Enabled` + пара `SetWindowEnabled/IsWindowEnabled`
//     (их вызовет будущее окно настроек — своего UI мы не выдумываем).
//   • Уже видимое окно повторный приход НЕ дёргает; невидимое — показывается и поднимается наверх
//     (`if (opt == 1 && !ctrl->bVisible) { SetVisible(1); BringToFront(); }`).
//   • ТАЙМЕРА ЖИЗНИ НЕТ — окно висит, пока его не закроют крестиком **28101** или не выключат опцию.
//   • Прокрутка — колесо (эталонное «колёсное окно» 28102 `WheelWin_Nomal`, Visible=0,
//     `SetScrollRange(3, 20)`, `SetScrollPage(1, 1)`; событие 23/0x17 → `start = значение − 2`).
//   • Наведение на строку 28110/28120/28130 → ТУЛТИП ПРЕДМЕТА (`ui_core.c:47894-47912`):
//     код вне диапазона самоцветов → полный `ItemTooltip_Build`, иначе — тултип самоцвета.
//
// ═══════════════════ ЧЕГО В СРЕЗЕ НЕТ (не выдумываем) ══════════════════════════════════════════
//   • Звука у кнопок меню 28003/28007/28002 эталон НЕ играет (в отличие от меню чата 80002…80004,
//     где стоит `csoundPlayName2DStore(g_pSoundMgr, 5, 0)`) — молчим и мы.
//   • Меню НЕ закрывается кликом мимо себя: `CControl_SetVisible(self[1065], 0)` встречается ровно
//     в трёх местах — три его собственные кнопки. Воспроизводим 1:1.
//   • Никакого «истёк срок показа», «повтор запрещён N секунд», «нельзя хвастаться в городе» —
//     ни в клиенте, ни в сервере таких веток нет.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaShowOffSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;

// ═══ Одна запись списка окна 28100 = элемент контейнера `this+1088` эталона (шаг 0x51 = 81 б) ═══
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaShowOffEntry
{
	GENERATED_BODY()

	// @0 тела 6130. Клиент-эталон это поле НЕ читает (см. шапку) — храним только для дампа.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Хвастовство") int32 RawUserIdx = -1;
	// @4 тела — имя персонажа-отправителя (cp1251 → FString).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Хвастовство") FString CharName;
	// @24 тела — 57-байтовая запись предмета (та же, что в контейнерах инвентаря).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Хвастовство") TArray<uint8> Rec57;
	// Код предмета (Rec57 @+0) — вынесен, чтобы не разбирать запись на каждый кадр.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Хвастовство") int32 ItemCode = 0;
};

UCLASS()
class ASDAREBORN_API UAsdaShowOffSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaShowOffSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═══════════════════ КОНСТАНТЫ ИЗ ДАННЫХ .JUI ═══════════════════
	// Окно-меню 28000 `Popup_SmWin`, Origin 75 132, Size 158×100.
	static constexpr int32 kMenuIdNum   = 28000;
	static constexpr float kMenuBaseX   = 75.f;
	static constexpr float kMenuBaseY   = 132.f;
	static constexpr float kMenuW       = 158.f;
	static constexpr float kMenuH       = 100.f;
	// Смещение всплывашки от курсора — эталон `CControl_SetField74_75(…, x + 32, y + 32)`.
	static constexpr float kMenuDX      = 32.f;
	static constexpr float kMenuDY      = 32.f;
	// Окно-список 28100 `Box_BlackOt40_2`, Size 394×75, три строки по 15 px.
	static constexpr int32 kListIdNum   = 28100;
	static constexpr int32 kVisibleRows = 3;    // строк на экране (28110/28120/28130)
	static constexpr int32 kMaxEntries  = 20;   // потолок списка (эталон: size > 20 → pop_front)

	// Z окна: меню обязано лежать ВЫШЕ инвентаря (у него 720), список — обычное окно HUD.
	static constexpr int32 kListZ = 730;
	static constexpr int32 kMenuZ = 780;

	// ═══════════════════ ★★★ ТОЧКА ПОДКЛЮЧЕНИЯ ПКМ ИЗ ИНВЕНТАРЯ ═══════════════════
	// Зовётся из обработчика ПРАВОЙ КНОПКИ по ячейке инвентаря — ровно там, где эталон делает
	//   `if (self[202] && self[1065]) { … SetVisible(28000, 1) }` (ui_core.c:48038-48048).
	//   ContainerType/SlotIndex — поля +4 и +5 ВЫБРАННОЙ 57-байтовой записи (их же уносит 6129).
	//   Возвращает false, если ячейка пуста (эталон: `OnSlotClick` вернул 0 ⇒ меню не открывается)
	//   либо окно не загрузилось.
	// ⚠ Тултип предмета гасить НЕ надо — метод сам зовёт `HideTooltipFor(Item)`
	//   (эталонный `CControl_KillFocus(&g_ItemTooltip)` в той же ветке).
	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	bool OpenMenuForCell(int32 ContainerType, int32 SlotIndex, int32 ItemCode);

	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	void CloseMenu();
	UFUNCTION(BlueprintPure, Category="Asda|Хвастовство")
	bool IsMenuOpen() const { return bMenuOpen; }

	// ═══════════════════ ОТПРАВКА (6129) ═══════════════════
	// Кнопка 28003 «Всем рядом» — target = −1 (сервер разошлёт по клиппинг-сектору).
	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	bool SendShowOffToAll();
	// Кнопка 28007 «Только выбранному» — target = userIdx выбранной цели; нет цели ⇒ нотис 102003.
	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	bool SendShowOffToTarget();
	// Низкоуровневая отправка (её же зовут обе кнопки и авто-анонс редкой добычи).
	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	bool SendShowOff(int32 ContainerType, int32 SlotIndex, int32 TargetUserIdx);
	// ★ Удобная обёртка для АВТО-АНОНСА (`CItemManager_NotifyItemInfo`, item_inventory.c:39572-39574):
	//   там контейнер и слот берутся ровно из полей +4/+5 записи, а адресат всегда −1.
	bool SendShowOffForRecord(const TArray<uint8>& Rec57);

	// Текущий выбор меню (эталонный снимок `m_aSelItemSnapshot`: контейнер = запись+4, слот = запись+5).
	//   Нужен диагностике: `asda.ShowOff.Send <userNum>` шлёт 6129 по ЭТОМУ выбору.
	UFUNCTION(BlueprintPure, Category="Asda|Хвастовство")
	int32 GetSelContainer() const { return SelContainer; }
	UFUNCTION(BlueprintPure, Category="Asda|Хвастовство")
	int32 GetSelSlot() const { return SelSlot; }

	// ═══════════════════ ОКНО-ПРИЁМНИК (28100) ═══════════════════
	// Опция 875 «Показывать окно хвастовства» (нотис 102004). Выключение немедленно прячет окно.
	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	void SetWindowEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category="Asda|Хвастовство")
	bool IsWindowEnabled() const;

	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	void CloseListWindow();            // крестик 28101
	UFUNCTION(BlueprintPure, Category="Asda|Хвастовство")
	bool IsListWindowOpen() const { return bListVisible; }
	UFUNCTION(BlueprintPure, Category="Asda|Хвастовство")
	int32 GetEntryCount() const { return Entries.Num(); }
	// Верхняя показанная строка (эталонный `this[1086]`; 0 = самое начало списка).
	UFUNCTION(BlueprintPure, Category="Asda|Хвастовство")
	int32 GetStartRow() const { return StartRow; }
	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	void SetStartRow(int32 Row);

	// ═══════════════════ ДИАГНОСТИКА / ПРОВЕРКА БЕЗ ВТОРОГО ИГРОКА ═══════════════════
	// Полный дамп КАЖДОГО разобранного контрола обоих окон (закон full-structural-analysis-always).
	FString DumpControls() const;
	FString DumpModel() const;
	// Сымитировать приход 6130 (сервер не нужен): собирает тело 81 б и гонит его штатным путём.
	UFUNCTION(BlueprintCallable, Category="Asda|Хвастовство")
	void SimulateIncoming(const FString& CharName, int32 ItemCode);
	// То же, но с готовой 57-байтовой записью (например снятой из своего инвентаря).
	void SimulateIncomingRecord(const FString& CharName, const TArray<uint8>& Rec57);
	// Зона наведения строки j окна 28100 (подложка строки; nullptr — окно не разобрано / индекс вне 0..2).
	//   Нужна самотесту `asda.SelfTest.GoldBoastTip`: курсор ставится в центр ЖИВОЙ геометрии строки (путь игрока).
	UWidget* DebugGetRowHit(int32 Row) const { return (Row >= 0 && Row < RowHit.Num()) ? RowHit[Row].Get() : nullptr; }
	// Индекс записи, показанной в строке j (−1 — строка пуста); самотест сверяет, что строка занята пришедшим 6130.
	int32 DebugGetRowEntry(int32 Row) const { return (Row >= 0 && Row < kVisibleRows) ? RowEntryIdx[Row] : -1; }

private:
	// ── Жизненный цикл виджетов ──
	bool EnsureMenu();
	bool EnsureList();
	void SetupMenuControls();
	void SetupListControls();
	void ApplyListVisibility();
	void RegisterZones();
	void UnregisterZones();
	bool GetMenuRect(FVector2D& OutMin, FVector2D& OutMax) const;
	bool GetListRect(FVector2D& OutMin, FVector2D& OutMax) const;
	bool OnListWheel(int32 Notches);

	// ── Сеть (колбэк ТОЛЬКО копит — закон no-reentrant-net-callbacks) ──
	void OnShowOffPacket(const TArray<uint8>& Body81);
	void DrainPending();

	// ── Модель/рендер ──
	void PushEntry(const FAsdaShowOffEntry& E);
	void RefreshRows();                       // порт `itemListWnd_PageRows@0xD33080`
	int32 MaxStartRow() const { return FMath::Max(0, Entries.Num() - kVisibleRows); }
	void TickRowTooltip();
	// Имя и ARGB-цвет предмета по коду — три таблицы эталона в том же порядке
	//   (`item_list` → `special_itemlist` → `sowell_list`, PageRows).
	bool ResolveItemLine(int32 ItemCode, FString& OutName, uint32& OutArgb) const;

	// ── Позиционирование меню у курсора (эталон SetField74_75) ──
	void PlaceMenuAt(const FVector2D& TopLeftUnits);
	bool GetMenuTopLeft(FVector2D& Out) const;

	// ── Кэш виджетов МЕНЮ 28000 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> MenuWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  MenuTitle  = nullptr;   // 28001 Open_Item_Move
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnAll     = nullptr;   // 28003
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnTarget  = nullptr;   // 28007
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnClose   = nullptr;   // 28002
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnParty   = nullptr;   // 28004 (мертва в данных)
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnClan    = nullptr;   // 28005 (мертва)
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnSoul    = nullptr;   // 28006 (мертва)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  CapAll     = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  CapTarget  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  CapClose   = nullptr;

	// ── Кэш виджетов СПИСКА 28100 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ListWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ListTitle  = nullptr;                 // заголовок окна
	UPROPERTY(Transient) TObjectPtr<UButton>     ListClose  = nullptr;                 // 28101
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowItem;                       // 28110/28120/28130
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowUser;                       // 28111/28121/28131
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    RowHit;                        // подложка строки (зона hover)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Состояние ──
	UPROPERTY(Transient) TArray<FAsdaShowOffEntry> Entries;
	// Индекс записи, показанной в строке j (эталонный `this[1082 + j]`; −1 = строка пуста).
	int32 RowEntryIdx[kVisibleRows] = { -1, -1, -1 };
	int32 StartRow    = 0;
	bool  bListVisible = false;
	bool  bMenuOpen    = false;
	bool  bMenuReady   = false;
	bool  bListReady   = false;
	int32 RowHovered   = -1;      // строка под курсором (владение подсказкой)

	// Что выбрано в инвентаре на момент открытия меню (эталонные `self+588` / `self+589`).
	int32 SelContainer = -1;
	int32 SelSlot      = -1;
	int32 SelItemCode  = 0;

	// Очередь тел 6130 (колбэк → Tick).
	//   ★★★ Аудит №3 (B10) — НАХОДКА, А НЕ КОСМЕТИКА: это была ЕДИНСТВЕННАЯ из шести очередей БЕЗ
	//   замка вовсе (у пяти остальных стоял свой `FCriticalSection PendingLock`). Пакеты сейчас
	//   приходят на игровом потоке, поэтому дефекта в живой игре не было — но защита у одной
	//   подсистемы отсутствовала, а у соседних была. Теперь тип один на всех, и замок есть у всех.
	TAsdaNetQueue<TArray<uint8>> PendingBodies;
	FDelegateHandle HShowOff;
	bool bNetBound = false;   // подписка на 6130 ленивая: сеть поднимается позже подсистем UI

	// Счётчики для консольного дампа.
	int32 CntRecv = 0;
	int32 CntSent = 0;
};
