// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCharRenameSubsystem — ПРЕДМЕТ «СМЕНА ИМЕНИ ПЕРСОНАЖА» (`special_itemlist` 357, подтип 111): вопрос 65, окно ввода
//   `CharNameChange_Win` (JUI 7900, `WBP_CharNameChange_Win`), проверка 6138 → 6139, вопросы 66/67, смена 6140 → 6141.
//   Порт 09.11.2026, пачка 20 баг 8. Полный разбор — `Reports/batch20/rename/REPORT.md`.
//
// ═════════════════ ★★★ ПОТОК ОРИГИНАЛА (AsdaStory_game.c) ═══════════════════════════════════════
//   1. `CItemManager_UseInventoryItem@0xA0FDF0`, `switch (subType − 35)` case 76 (:75223) → TWOBUT (1, 65) текст 109401
//      («список приятелей будет уничтожен… Продолжить?»), кнопки 2288/2289 (`CItem_BuildTooltipText`, :379007-379020).
//   2. «ОК» 65 (`case 0x41`, :380149) → показать окно 7900. Инициализация окна (`:338401-338436`): 7901 заголовок = 109400,
//      7903 текст = 109402, 7905 кнопка = 2516 «Доступность», 7904 поле ввода (LimitText 20), 7902 закрыть.
//   3. 7902 (:348362) — скрыть окно. 7905 (:348364-348384): текст поля → буфер нового имени;
//        имя == своё → ONEBUT 101118; бранное (`Chat_MatchBannedWord`) → (0, 7009); иначе 6138.
//   4. 6138 (`AntiTamper_SendStringChecksumPacket@0xD33AB0` — МИСНЕЙМ): 24 б = +0 i32 charId, +4 char[20] имя; пакет 48 б.
//      Сервер `RecvClient_CharNameCheck@0x55D7C0` (0x18, конверт с секретом) → DB 2450 `EXEC UPNickCheck '%s'` → 2451 → 6139.
//   5. 6139 (`itemTooltipBuildOrNotice@0xF34EC0`, 5 б: +0 charId, +4 код): 1 → TWOBUT (1, 66) 109403 «'%S' доступно…»;
//      иначе (2 занято, 3 запрещённое слово) → ONEBUT 109404.
//   6. «ОК» 66 (`case 0x42`, :380151) → TWOBUT (1, 67) 109405 «[старое] -> [новое]… завершить игру» (цвета 0xff5096ff/0xfffa3200).
//   7. «ОК» 67 (`case 0x43`, :380160) → поле очищено, окно скрыто, 6140 (`netSendTsMaskedReq6140@0xD33430`): 27 б =
//      +0 i32 charId, +4 char[20] имя, +24 i16 ячейка, +26 u8 контейнер; пакет 51 б.
//      Сервер `RecvClient_CharNameChange@0x55D910` (0x1B): нет предмета → 5, подтип ≠ 111 → 6, поле клана → 3, отряд → 7;
//      иначе DB 2452 `EXEC UPCharRename` → 2453 → `sub_55DD50` → 6141.
//   8. 6141 (`recvItemActionResult_ShowNotice@0xF34CE0`, 86 б: +0 charId, +4 имя, +24 код, +25 запись предмета 57 б):
//      0 → 109411; 1 → предмет снят (`OnRecv_RemoveSlot`), `g_GameState = 0` — КЛИЕНТ ЗАВЕРШАЕТСЯ (вход заново покажет имя);
//      2/3 → 109406 (клан), 4 → 109407 (аукцион), 5 → 109408, 6 → 109409, 7 → 109410 (отряд). Все отказы — ONEBUT.
//   Логин-окно 3000 `CharName_Change_Win` (`WBP_CharName_Change_Win`) оригиналом НЕ используется (ссылок на 3000…3030 нет).
//
// ═════════════════ ОТЛИЧИЯ ПОРТА (ключи) ═════════════════════════════════════════════════════════
//   asda.Rename.Flow 1        — поток оригинала; 0 — прежнее поведение порта (предмет уходил в 5450 и тратился впустую).
//   asda.Rename.NameRule 1    — [ИСПРАВЛЕНИЕ] то же правило имени, что при создании персонажа (≤ 18 символов, ≤ 19 байт провода,
//                               без апострофа, символы по `asda.Text.NameLocale`) → ONEBUT 9583; оригинал проверял только
//                               совпадение и брань (пустое имя и апостроф уходили на сервер в `EXEC … '%s'`).
//   asda.Rename.AfterSuccess 1 — [ПОРТ] после успеха — возврат к выбору персонажа (1028 + карта входа: список уже с новым
//                               именем); 2 — закрыть клиент, как оригинал; 0 — остаться в мире (только для проверок).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "AsdaNetQueue.h"              // TAsdaNetQueue — очередь пакетов из сетевого колбэка
#include "AsdaCharRenameSubsystem.generated.h"

class UUserWidget;
class UButton;
class UTextBlock;
class UEditableText;
class UEditableTextBox;
class UWidget;
class UAsdaButtonRelay;

// Тексты потока. Все id проверены по `RawAssets/Text/GameText.csv` и `DT_Loc_GameText_en.csv`.
namespace AsdaRenameLoc
{
	constexpr int32 kTitle      = 109400;   // 7901 «Смена имени персонажа»
	constexpr int32 kAskUse     = 109401;   // TWOBUT 65: «…список приятелей будет уничтожен… Продолжить?»
	constexpr int32 kPrompt     = 109402;   // 7903 «Введи желаемое имя персонажа и нажми кнопку 'Доступность'.»
	constexpr int32 kAvailable  = 109403;   // TWOBUT 66: «Имя '%S' доступно. Использовать его?»
	constexpr int32 kTaken      = 109404;   // 6139 ≠ 1: «Имя уже кем-то занято. Введи другое.»
	constexpr int32 kConfirm    = 109405;   // TWOBUT 67: «Имя персонажа изменится: [%s] -> [%s]…»
	constexpr int32 kInClan     = 109406;   // 6141 2/3
	constexpr int32 kOnAuction  = 109407;   // 6141 4
	constexpr int32 kNoItem     = 109408;   // 6141 5
	constexpr int32 kWrongItem  = 109409;   // 6141 6
	constexpr int32 kInParty    = 109410;   // 6141 7
	constexpr int32 kError      = 109411;   // 6141 0
	constexpr int32 kSameName   = 101118;   // 7905: имя совпадает со своим
	constexpr int32 kBadWords   = 7009;     // 7905: бранное слово
	constexpr int32 kBadName    = 9583;     // [ИСПРАВЛЕНИЕ] правило имени создания персонажа
	constexpr int32 kBtnCheck   = 2516;     // 7905 «Доступность»
}

UCLASS()
class ASDAREBORN_API UAsdaCharRenameSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на 6139/6141 (`OnWave2DPacket`).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка, сброс виджета.
	virtual void Deinitialize() override;
	// Сброс сессии: окно скрыть, запрос (ячейка, имя) снять.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Разбор пришедших тел (закон no-reentrant-net-callbacks) и отложенный уход после успеха.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCharRenameSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Ключ `asda.Rename.Flow`: 1 — поток оригинала (подтип 111 уходит сюда, а не в 5450).
	static bool IsFlowOn();
	// Вход из использования предмета (`UAsdaInventorySubsystem::UseSpecialItemDispatch`, подтип 111): вопрос 65.
	//   true — вопрос поставлен (или отказ показан), 5450 не шлётся.
	bool BeginUse(int32 ContainerId, int32 CellIndex, int32 ItemCode);

	// ── Самотест / диагностика ──
	bool IsWindowShown() const;                                       // окно 7900 видно
	UUserWidget* GetWindowWidget() const { return Widget; }           // корень окна 7900
	UEditableText* GetInput() const { return EdtInput; }              // поле 7904 (UEditableText)
	UEditableTextBox* GetInputBox() const { return EdtInputBox; }     // поле 7904 (UEditableTextBox — если импортёр сделал его так)
	void SetInputText(const FString& S);                              // ввести текст в поле 7904 (как игрок)
	FString GetInputText() const;                                     // текст поля 7904
	void ClickCheck() { OnCheckClicked(); }                           // нажатие 7905 (тот же обработчик, что у реле)
	void ClickClose() { OnCloseClicked(); }                           // нажатие 7902
	UButton* GetCheckButton() const { return BtnCheck; }              // кнопка 7905
	UButton* GetCloseButton() const { return BtnClose; }              // кнопка 7902
	int32 GetPendingCell() const { return PendingCell; }              // ячейка предмета запроса
	const FString& GetPendingName() const { return PendingName; }     // имя, ушедшее в 6138
	const TArray<int32>& GetNoticeTrace() const { return NoticeTrace; }   // loc-id показанных окон (по порядку)
	FString GetLastNoticeText() const { return LastNoticeText; }      // текст последнего окна
	void ClearTrace() { NoticeTrace.Reset(); LastNoticeText.Reset(); }
	int32 GetLeaveCount() const { return LeaveCount; }                // сколько раз сработал уход после успеха
	void SetSuppressLeave(bool b) { bSuppressLeave = b; }             // самотест: уход считать, но не выполнять

private:
	bool EnsureWidget();                          // `FAsdaJuiWindowHandle::Ensure` окна 7900
	void SetupControls();                         // разбор узлов окна + подписи инициализации (:338401-338436)
	void ShowWindow();                            // `case 0x41`: окно видно, поле пусто
	void HideWindow();                            // `CControl_SetVisible(7900, 0)`
	void OnCloseClicked();                        // 7902
	void OnCheckClicked();                        // 7905
	void OnCheckResult(const TArray<uint8>& B5);  // 6139
	void OnChangeResult(const TArray<uint8>& B86);// 6141
	void AskAvailable();                          // TWOBUT 66
	void AskConfirm();                            // TWOBUT 67
	void SendChange();                            // `case 0x43` → 6140
	void LeaveAfterSuccess();                     // `g_GameState = 0` у оригинала
	void NoticeMsgBox(int32 LocId, int32 EtalonId = -1);   // ONEBUT + след
	FString Loc(int32 LocId) const;               // loc-строка
	FString SelfName() const;                     // своё имя (`CClass_GetField142C(&g_InventoryBag)->name`)

	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;            // окно 7900
	UPROPERTY(Transient) TObjectPtr<UButton> BtnCheck = nullptr;              // 7905 «Доступность»
	UPROPERTY(Transient) TObjectPtr<UButton> BtnClose = nullptr;              // 7902
	UPROPERTY(Transient) TObjectPtr<UEditableText> EdtInput = nullptr;        // 7904
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> EdtInputBox = nullptr;  // 7904 (вариант импорта)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;          // GC-якорь реле кнопок
	bool bReady = false;                     // окно разобрано (кнопки и поле найдены)
	bool bLoggedTree = false;                // дерево окна записано в журнал один раз

	int32 PendingContainer = -1;             // контейнер предмета (`sub_93D5AE`)
	int32 PendingCell = -1;                  // ячейка предмета (`sub_92BA1B`)
	int32 PendingCode = 0;                   // код предмета (для журнала)
	FString PendingName;                     // буфер нового имени (`g_nItemUpgradeLevel` — мисней буфера +4373)
	bool bLeavePending = false;              // успех 6141 пришёл — уход в следующем Tick (не из колбэка окна)
	bool bSuppressLeave = false;             // самотест
	int32 LeaveCount = 0;                    // самотест

	TAsdaNetQueue<TPair<int32, TArray<uint8>>> Pending;   // (опкод, тело) из сети — разбор в Tick
	FDelegateHandle HWave;                   // подписка `OnWave2DPacket`
	TArray<int32> NoticeTrace;               // след окон (самотест)
	FString LastNoticeText;                  // последний текст (самотест)
};
