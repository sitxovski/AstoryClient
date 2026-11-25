// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSkillTreeResetSubsystem — ★★★ СВИТКИ СБРОСА НАВЫКОВ: окно выбора ветки `Skill_OneTree_Reset_Win` (JUI IDNum 16500)
//   и вопросы 12865/12866. Перенос функции оригинала (25.10.2026, жалоба Player52: свитки 197/470 тратились впустую).
//
// ═══════════════ ПОТОК ОРИГИНАЛА (все ветки) ══════════════════════════════════════════════════════════
//   Использование функционального предмета — `CItemManager_UseInventoryItem@0xA0FDF0`, `switch (subType − 35)`
//   (item_inventory.c:2934-2969):
//     • case 0/19/20 — подтипы 35/54/55 (полный сброс 196/31/278/412/469/540, 198…201): гейт `*(g_LocalPlayer+1093)`
//       (= `CCharacter+0x445`, профессия, в порту `SelfJob`) — 0 ⇒ плашка 221 нотис 12848 (−11514, 10 с) и выход;
//       иначе запрос копируется в `unk_2C0A71C` (= `g_pItemTooltipUI+13460`) и показывается вопрос (1, 12866).
//     • case 1 — подтип 36 (сброс ОДНОЙ ветки 197/309/470/543): тот же гейт; иначе `uiTradeDialogInitFromItemType@0xD7DAF0`
//       (ui_core.c:58923) — заполнение и показ окна 16500.
//   Окно (`uiTradeDialogInitFromItemType`):
//     • 16521 (флажок «4-я профессия», `Box_Check`) виден только у классов 4…7, 15…18, 26…29, 34…39; снят;
//     • 16501 = нотис 12861; 16502 = нотис 12862 со слотами [ss1] = своё имя, [ss2] = класс (нотис 12300 + класс);
//     • 16510 (радиогруппа `Smelting_V3Btn2`, 3 пункта) — выбран пункт 0; 16511…16513 = нотисы 9400 + 3·(полоса−1) + i;
//     • ветка (`+13715` = `word_2C0A81B`) = 3·(полоса−1) + 1; полоса = `itemGradeBandFromTypeIndex(класс)` (1…11 → 1, 12…22 → 2,
//       23…33 → 3, иначе 0); запрос копируется в `+13460`.
//   Клики (`uiItemUpgradeWnd_OnCommand@0xD79260`, ui_core.c:58411-58486):
//     • 66817 по 16510 (пункт a5): ветка = a5 + 3·(полоса−1) + 1, флажок 16521 снят;
//     • 67073 по 16521 (флажок поставлен): радиогруппа без выбора (индекс 3), ветка = 10 (классы 4…7) / 11 (15…18) / 12 (26…29);
//     • 67074 по 16521 (снят повторным щелчком): флажок СНОВА поставлен (`SetVisibleState(…, 1)`) — снять его можно только
//       выбором пункта радиогруппы; ветка не меняется;
//     • 66561 по 16503 «ОК» (12863): окно скрыто, вопрос (1, 12865); по 16504 «Отмена» (12864): окно скрыто, ничего не уходит.
//   Ответ «Да» на 12865/12866 (ui_core.c:57585) — `itemUseBuildAndDispatch@0xD7DDB0` → `itemUseDispatch_C20` → 5450; у
//   подтипа 36 `netSendItemUsePacket@0xA0D470` кладёт ветку в +255 тела. «Нет» — только закрытие: свиток не тратится.
//   Ответ сервера (`sub_480F20`, GameServer.c:51265): сброс навыков ветки → 5464 {очки, ветка} — сообщение 12854 с названием
//   ветки (`UAsdaSkillWindowSubsystem::OnNetSkillsRealigned`), затем 6057 + 5251 (новый список); отказ — 5451 (код 15 → 12848).
//   ESC окно не закрывает и НЕ поглощает: признака `CControl+0x228` у 16500 нет (в эталонной таблице `kEscCloseWindows` окна нет),
//   проход окон его не видит — нажатие идёт дальше по цепочке ESC (цель, ветки состояния, меню 69400), окно остаётся.
//   ★ 26.10.2026 (ревизия wave_1025d): прежний вызов `RegisterEscSwallower` отклонялся с предупреждением — снят.
//
// ═══════════════ ПОРТ ═════════════════════════════════════════════════════════════════════════════════
//   [ПРОТОКОЛ] Наш сервер читает ветку из тела 5450 +135 (а не +255) — отправка пишет в оба поля (AsdaNetSubsystem_Items.cpp).
//   Вопросы — `ShowGameConfirm` (TWOBUT 1520) с парой эталона (1, 12865/12866) и БЕЗ Enter (необратимо, AsdaEnterGuard).
//   Нумпада у потока нет (количества не вводится). Ключ отката всего потока — asda.SkillReset.Dialogs 0 (прежняя прямая 5450).
//   ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (ключ asda.SkillReset.KeepFirstRequest): пока окно 16500 открыто, второй свиток сброса запрос окна
//   НЕ подменяет — использование отклоняется (строка журнала). [ОРИГИНАЛ]: у эталона запрос окна и вопроса 12866 — ОДИН буфер
//   (`unk_2C0A71C` = `g_pItemTooltipUI+13460`, `word_2C0A81B` = +13715): второй свиток его перезаписывал, и «ОК» окна уходил
//   5450 другого свитка (у подтипа 35/54/55 — с веткой окна).
//   [ОРИГИНАЛ-дефект] классы 34…39: флажок 16521 виден (`AsdaSelf::IsJob4Class`), но ветки 10/11/12 у них нет (`Job4GradeOfClass` = −1),
//   полоса `BandOfClass` = 0 ⇒ ветка пунктов `3·(0−1)+1+i` = −2…0, в +135 уходит 0 — сервер сбрасывает ветку 0, свиток тратится.
//   ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (ключ asda.SkillReset.CraftClassGuard): свиток сброса ветки у класса без боевой полосы отклоняется
//   до отправки нотисом 9421, флажок 16521 ремесленному классу не показывается.
//   ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (ключ asda.Job4.StageFromSubJob): флажок 16521, ветка 10/11/12 и имя класса в 16502 — по ступени
//   4-й профессии из ПОДКЛАССА (`AsdaSelf::GetJob4StageClass`): сервер пишет 4-ю ступень в подкласс, основной класс остаётся 3/14/25.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSkillTreeResetSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UButton;
class UImage;
class UTextBlock;
class UCheckBox;
class UAsdaButtonRelay;

// Ключ `asda.SkillReset.Dialogs`: 1 — поток оригинала (вопрос 12866 / окно ветки 16500), 0 — прежняя прямая отправка 5450.
ASDAREBORN_API bool AsdaSkillResetDialogsOn();

UCLASS()
class ASDAREBORN_API UAsdaSkillTreeResetSubsystem : public UGameInstanceSubsystem, public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Сброс сессии: окно скрыто, запрос забыт, вопрос (если висит) ничего не отправит.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Подтипы `special_itemlist`, которые ведёт эта подсистема (35/36/54/55 — ветки `switch (subType − 35)` 0/1/19/20).
	static bool HandlesSubType(int32 SubType) { return SubType == 35 || SubType == 36 || SubType == 54 || SubType == 55; }

	// Использование свитка из ячейки сумки (порт ветки `CItemManager_UseInventoryItem`). true — показан вопрос или окно.
	bool BeginUse(int32 ContainerId, int32 CellIndex, int32 ItemCode, int32 SubType);

	// Клики окна (тот же путь, что щелчок игрока): пункт радиогруппы 16510, флажок 16521, «ОК» 16503, «Отмена» 16504.
	void OnTreeClicked(int32 Index);
	void OnFourthClicked();
	void OnOkClicked();
	void OnCancelClicked();

	// Окно 16500 показано.
	bool IsWindowShown() const;
	// Текущая ветка (`word_2C0A81B`), выбранный пункт (−1 — нет), флажок 4-й профессии, виден ли он.
	int32 GetGrade() const { return Grade; }
	int32 GetSelectedTree() const { return SelectedTree; }
	bool IsFourthChecked() const { return bFourthChecked; }
	bool IsFourthShown() const { return bFourthShown; }
	// Виджет окна (самотест: тексты контролов, кнопки).
	UUserWidget* GetWidget() const { return Widget; }
	// Строка состояния для журнала/самотеста.
	FString DumpState() const;

	// Идентификатор окна в реестре `UAsdaWindowSubsystem` (имя окна .JUI) и его IDNum.
	static const FName kWindowId;
	static constexpr int32 kWinIdNum = 16500;

private:
	// Запомненный запрос использования (эталон `+13460`): ячейка, код и подтип свитка.
	struct FPendingUse
	{
		int32 ContainerId = -1;   // контейнер (всегда сумка 1 — канал 5450 работает только с ней)
		int32 CellIndex = -1;     // ячейка свитка
		int32 ItemCode = 0;       // код свитка в момент нажатия
		int32 SubType = 0;        // подтип special_itemlist
		bool IsSet() const { return ItemCode > 0 && CellIndex >= 0; }
	};

	// Окно: создать (раз за жизнь виджета) и привязать контролы; false — WBP не загрузился.
	bool EnsureWidget();
	void SetupControls();
	// Заполнить и показать окно (порт `uiTradeDialogInitFromItemType`).
	bool OpenTreeWindow();
	// Скрыть окно (без отправки).
	void HideWindow();
	// Щелчок игрока по флажку 16521 (делегат UCheckBox: новое состояние уже выставлено Slate) — события 67073/67074 эталона.
	UFUNCTION() void OnFourthCheckChanged(bool bIsChecked);
	// Перерисовать выбор радиогруппы (UV состояний `SS 0` / `SS 2` из JuiSkin.ini) и состояние флажка.
	void RefreshSelectionVisual();
	// Вопрос (1, NoticeId) без Enter; «Да» → отправка 5450 с веткой (−1 — без ветки).
	void AskAndSend(int32 NoticeId, int32 SendGrade);
	// Отправка 5450 общей точкой сумки (`UsePremiumItem`). Ячейка обязана держать тот же свиток.
	bool SendUse(int32 SendGrade);
	// Класс персонажа (`gap_438[0]`) и полоса (`itemGradeBandFromTypeIndex`).
	int32 SelfClassId() const;
	static int32 BandOfClass(int32 ClassId);
	// Записать дерево виджетов окна в журнал (полный структурный разбор — закон full-structural-analysis-always).
	void LogWidgetTree() const;

	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle = nullptr;       // 16501
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBody = nullptr;        // 16502
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TxtTrees;         // 16511…16513
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> BtnTrees;            // пункты радиогруппы 16510 (`_OPT0…2`)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ImgTrees;             // планы пунктов радиогруппы (UV состояния)
	UPROPERTY(Transient) TObjectPtr<UWidget> FourthGroup = nullptr;       // 16521 `Box_Check` с подписью 16522
	UPROPERTY(Transient) TObjectPtr<UCheckBox> ChkFourth = nullptr;       // флажок 16521 (`CheckButton Box_Check`, импортёр — UCheckBox)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFourth = nullptr;      // 16522 = нотис 111800
	UPROPERTY(Transient) TObjectPtr<UButton> BtnOk = nullptr;             // 16503
	UPROPERTY(Transient) TObjectPtr<UButton> BtnCancel = nullptr;         // 16504
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapOk = nullptr;          // подпись 16503 = 12863
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapCancel = nullptr;      // подпись 16504 = 12864
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;     // GC-якорь реле кнопок

	FPendingUse Pending;             // запрос (эталон `+13460`)
	int32 Grade = -1;                // ветка для 5450 (эталон `+13715` = `word_2C0A81B`)
	int32 SelectedTree = -1;         // выбранный пункт радиогруппы (−1 — нет, эталон индекс 3)
	bool bFourthChecked = false;     // флажок 16521 поставлен (эталон `gap_32D8[20]`)
	bool bFourthShown = false;       // флажок 16521 виден (класс 4-й профессии)
	bool bReady = false;             // контролы привязаны
	uint32 AskSerial = 0;            // номер текущего вопроса — ответ протухшего вопроса ничего не отправит
	bool bLoggedTree = false;        // дерево виджетов уже записано в журнал

	friend struct FAsdaSkillTreeResetSelfTest;
};
