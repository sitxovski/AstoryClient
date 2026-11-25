// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaAfterDeathSubsystem — МЕНЮ СМЕРТИ (окно After_Death_Win) и воскрешение.
//
// ═══════════════════════════════ ЭТАЛОН (клиент + GameServer) ═══════════════════════════════════
// ПРИЧИНА СИСТЕМЫ. Сервер при смерти персонажа (`SendCharDead @0x4CB480`, GameServer.c:80766) ставит
//   CUser+79 = 1 («мёртв») и CUser+21732 = 200 (charState), рассылает всем в секторе (включая жертву)
//   опкод **4053** [i16 userIdx][i16 killerIdx]. После этого `sub_52BFF0` (IsCheck_CharFlag,
//   GameServer.c:113825) режет ЛЮБОЕ действие — движение 4006, подбор 5012, атаку 4026/4028 и др. —
//   и на каждую попытку шлёт клиенту **6001** [i16 опкод][u8 причина=1]. Для 5012 отказ выглядит как
//   5013 result=0 → эталонный текст loc 1012 «рюкзак будет переполнен», который к инвентарю
//   отношения не имеет (это generic-fail ветки LABEL_103 CharItemPicking).
//   Единственный выход — воскрешение: клиент шлёт **4054** (25 б, режим), сервер `RecvCharRegen
//   @0x52A570` обнуляет CUser+79 через `sub_4CB070(...,0)` и отвечает 4055 (перезагрузка карты)
//   либо 4056 (self + сектор).
//
// КЛИЕНТ. `Recv_CharDeath_Apply @0xF42E50` (network.c:42579) на СВОЙ userIdx:
//   • сброс меню (`deathMenuWnd_reset`), состояние персонажа 6 (смерть);
//   • закрытие открытых окон: магазин (uiInventoryResetAndLeave), парикмахерская
//     (uiLeaveAndResetAllWindows), booster-shop (tagBoosterShop_CloseWindow); показ сумки;
//   • НАБОР ОПЦИЙ через `DeathMenu_AddOption @0xD6D060` (ui_core.c:56139), максимум 6, дедуп по типу:
//       3 (loc 2381) — если на персонаже висит Resuscitate (effectSlots[112].field_48);
//       5 (loc 2376) — если в сумке есть предмет subType 48 (`inventoryCheckHasItemType48@0xD07350`);
//       mapId 19 → 7 (loc 2385) + 8 (loc 2386);   mapId 23 → только 8;
//         (getField3036 = mapId ТЕКУЩЕЙ карты; 19 «battlefield» и 23 «mine» — единственные две карты
//          с признаком campwar/PK = 1 в map_table.csv, что и подтверждает трактовку);
//       иначе     → 4 (loc 2370) и, при level ≤ 20, 6 (loc 2383).
//   • взводит флаг персонажа 0x4000 (режим меню) и клиентский отсчёт `field_32DE4`:
//       опция 4 → 300 000 мс (5 мин); ЛЮБОЙ ДРУГОЙ тип → 60 000 мс (карта 23 → 30 000; по рангу нации
//       30 000 / 20 000 / 10 000 — таблица рангов в порту не поднята, берём эталонный дефолт).
//       ★ Следствие: при level ≤ 20 после 4 добавляется 6 и отсчёт становится 60 с, хотя авто-опция
//         остаётся 4 (город) — «авто» помечаются только типы 4/7/8.
//     второй таймер 30 000 мс (`[1].buffFxSlots[108]`) взводится при ЛЮБОМ типе ≠ 4 и блокирует
//     ручной клик по опциям 7/8.
//   ★ ОПЦИИ 0/1/2 — ПРЕДЛОЖЕНИЕ ВОСКРЕШЕНИЯ ОТ ДРУГОГО ИГРОКА. Производитель — пакет 6093 S→C
//     (сервер sub_52B070@0x52B070, отправляют скилл-хендлеры GameServer.c:46866/57779 с Type 2,
//     63930 с Type 3 и обработчик предмета 86206 с Type 0). Клиент: netFriendListAddFromPacket
//     @0xF43C70 (имя авто-генерённое; фактически DeathMenu_AddOfferOption) → friendListAddEntry
//     @0xD6D740 (social.c:5237): дедуп по (тип, инициатор), тексты loc 2372 / 2380 / 2371,
//     отсчёт 300 000 мс. Ответ игрока — 6093 C→S (netSendPkt6093_Target@0xF47090) →
//     Recv_CharResurrectionConfirm@0x52B0E0 → CharRegenSelfPosition(Type 0 / 2 / 3).
//   • тик отсчёта — entity_char.c:22843-22855: подпись loc 20192 (> 60 с, минуты) / 20193 (≤ 60 с,
//     секунды) / 108107 (кампвар); по нулю автоматически выполняется помеченная опция (4/7/8).
//   • клик по опции (ui_core.c:58452-58480, ctrlId 25001..25006) → msgbox loc 2382 «Воскреснуть?» →
//     ОК → `DeathMenu_OnSelect @0xD6DE70`. Для 7/8 при активной 30-секундной блокировке клик игнор.
//
// ПАКЕТ ВОСКРЕШЕНИЯ (все сетевые опции — один опкод 4054, различается только режим @+24):
//   опция 3 → режим 1 (сервер: есть скилл 189 → CharRegenSelfPosition(6), иначе город);
//   опция 4 → режим 0 (CharRegenVillage — город);
//   опция 5 → пакета 4054 НЕТ: авто-использование первого предмета subType 48 (обычный «use item»);
//   опция 6 → режим 6 (сервер проверяет level ≤ 20 → CharRegenSelfPosition(7) — на месте);
//   опция 7 → режим 7 (CharRegenVillageMove_1 к ближайшему занятому кэмпу);
//   опция 8 → режим 8 (то же, требуется тип карты 4 = RoseWar).
//
// ОКНО (JUI gamemain, база 1024×768): контейнер After_Death_Win **25000** @ (310, 104.2, 404×406),
//   скин Box_Black_Ot50, JUI Visible=0; 6 слотов с шагом 66 px:
//     After_Death_Choice0..5 = 25001..25006 (Resize_BlackBtn) @ (322, 116.2 + 66·i, 380×52)
//     After_Death_Txt0..5    = 25010..25015 (NoImage)         @ (332, 121.2 + 66·i, 360×42)
//     After_Death_BtnBase0..5 = декоративные подложки (idnum −1, BtnBase_3)
//   Высота панели подгоняется под число опций: 66·n + 10 (DeathMenu_AddOption).
//
// ЗАКОНЫ: окно = ИМПОРТИРОВАННЫЙ WBP_After_Death_Win (no-invented-ui); тексты ТОЛЬКО из DT_GameText;
//   у каждой кнопки hover/pressed (ui-button-hover-click-rule); подписи SelfHitTestInvisible.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaAfterDeathSubsystem.generated.h"

class UUserWidget;
class UButton;
class UWidget;

// ── ★ ЗАПИСЬ ОПЦИИ МЕНЮ СМЕРТИ (эталон aDeathRecord[6*i]: i16 userNum + u8 тип + i16 процент) ──
//   Тип 0/1/2 — ПРЕДЛОЖЕНИЕ от другого игрока (6093): userNum инициатора и процент HP/MP нужны, чтобы
//   отправить подтверждение обратно. Типы 3..8 — собственные варианты (пакет 4054), поля не нужны.
USTRUCT(BlueprintType)
struct FAsdaDeathOption
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Asda|Death") int32 Type = -1;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Death") int32 CasterUserIdx = -1;   // aDeathRecord[6*i]
	UPROPERTY(BlueprintReadOnly, Category="Asda|Death") int32 Percent = -1;         // aDeathRecord[6*i+4]
};

UCLASS()
class ASDAREBORN_API UAsdaAfterDeathSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaAfterDeathSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Открыто ли меню смерти (эталонный флаг персонажа 0x4000).
	UFUNCTION(BlueprintPure, Category="Asda|Death")
	bool IsDeathMenuOpen() const { return bMenuOpen; }

	// Выполнить опцию меню (эталон DeathMenu_OnSelect@0xD6DE70). Публично — для консольной команды
	//   asda.Revive и для авто-срабатывания по таймеру.
	UFUNCTION(BlueprintCallable, Category="Asda|Death")
	void SelectOption(int32 OptionType);
	// Выполнить опцию по её ИНДЕКСУ в наборе (нужно типам 0/1/2: у них есть инициатор и процент).
	void SelectOptionAt(int32 Index);

	FString DebugState() const;
	// Полный структурный дамп окна в лог (закон full-structural-analysis) — asda.Death.DumpWidgets.
	void DumpWidgetTree() const;

	// ★ ПУБЛИЧНАЯ пересборка компоновки ОТКРЫТОГО меню (слоты + ужимание фона + рецентровка по фактической
	//   высоте). Дёргать после ЛЮБОГО изменения набора опций «на лету» (напр. 6093 — предложение воскрешения
	//   от игрока добавило кнопку). Путь 6093 через OnResurrectOffer зовёт её сам; метод — для логики,
	//   меняющей Options другим способом.
	UFUNCTION(BlueprintCallable, Category="Asda|Death")
	void RebuildMenuLayout();

private:
	void OnCharDeath(int32 UserIdx, int32 KillerIdx);   // 4053
	void OnCharRevived();                               // 4055/4056 (state != 200)
	// ★ 6093 S→C: другой игрок применил воскрешение — добавить опцию 0/1/2 (эталон friendListAddEntry
	//   @0xD6D740, на деле DeathMenu_AddOfferOption): дедуп по (тип, инициатор), текст с именем инициатора,
	//   отсчёт 300 000 мс. Приходит и когда меню ещё не открыто — эталон в этом случае его показывает.
	void OnResurrectOffer(int32 CasterUserIdx, uint8 Type, int32 Percent);

	bool EnsureWidget();
	void ResetWidget();
	void CloseMenu();                                   // эталон deathMenuWnd_reset@0xD6DD30
	void CloseMenuWidget();                             // закрыть ТОЛЬКО окно выбора (состояние не трогать)

	// ★ Однокнопочная модалка «вошёл в мир мёртвым» (эталон game_main.c:3464 + ui_core.c case 0x942).
	void ShowEntryDeadModal();
public:
	// Самотест очереди окон-сообщений (`asda.MsgBox.SelfTest`, ревизия С-2): показ модалки «вошёл мёртвым» и её снятие
	//   боевыми путями (тот же код, что 4053 состояние 29 и воскрешение 4055/4056).
	void DebugShowEntryDeadModal() { ShowEntryDeadModal(); }
	void DebugCloseMenuOnRevive() { CloseMenu(); }
	bool DebugHasModal() const { return Modal != nullptr; }
	// ★ 31.10.2026 (самотест боя, окно смерти на карте 19): отсчёт, блокировка 7/8, авто-опция, набор опций, подпись и кнопка слота.
	float DebugRespawnSec() const { return RespawnTimerSec; }
	float DebugClickLockSec() const { return ClickLockSec; }
	int32 DebugAutoOption() const { return AutoOption; }
	int32 DebugOptionCount() const { return Options.Num(); }
	int32 DebugOptionType(int32 I) const { return Options.IsValidIndex(I) ? Options[I].Type : -1; }
	FString DebugSlotLabel(int32 I) const;
	class UWidget* DebugSlotButton(int32 I) const;
private:
	// Снять СВОЮ модалку (запись (0, 2370) очереди окон-сообщений) — порт `uiCloseOnExchangeItemType`, молча;
	//   bSilent — сброс сессии (без гейта видимости окон).
	void CloseModal(bool bSilent = false);
	UFUNCTION() void OnModalOk();

	// ★ Динамическая компоновка окна выбора (эталон DeathMenu_AddOption: SetSize(w, 66·n + 10)):
	//   скрыть ВСЕ виджеты незанятых слотов (по ПРЕФИКСУ имени — планы импортёр называет
	//   «{idc}_{idx}_{plane}») и ужать фон окна по вертикали как 9-slice.
	void ApplyDynamicLayout(int32 UsedSlots);
	void SetSlotWidgetsVisible(int32 SlotIdx, bool bVisible);
	void ResizeWindowBackground(float NewHeightJui);
	// ★ Рецентровка окна по ФАКТИЧЕСКОЙ высоте — эталон: анкор X «16 50 40» = центр 50 % ширины,
	//   Y «32 50 40» = центр 40 % высоты экрана, пересчёт от ЖИВОГО размера окна каждый кадр
	//   (uiCalcAnchoredScreenPos@0x12DFBB0, jui_align_layout.md). После ужимания 66·n+10 группа окна
	//   ставится так, чтобы центр ВИДИМОЙ части = (0.5·W, 0.4·H).
	void RecenterWindow(float NewHeightJui);
	// ★ Подпись слота 1:1 по JUI (EXS Txt: TextAlign 5 = центр/центр, TextColor FFFFFFFF, TextPos (0,−1),
	//   rect 360×42; шрифт — единый кегль оригинала, как у ВСЕХ импортированных подписей FONT_PX 8 =
	//   cap-height 8 px из FONT0.BIN). Rich-замена больше не используется (она давала left/top и чужой кегль).
	void SetSlotLabel(int32 SlotIdx, const FString& Raw);

	// Эталон DeathMenu_AddOption: дедуп по типу, лимит 6, текст из loc, подгон высоты панели.
	void AddOption(int32 OptionType, int32 CasterUserIdx = -1, int32 Percent = -1);
	void BuildOptionSet();                              // набор опций по правилам Recv_CharDeath_Apply
	void RefreshSlots();                                // разложить опции по слотам 25001..25006
	void RefreshTimerLabel();                           // подпись отсчёта: loc 20192 / 20193
	void RefreshLockLabel();                            // ★ 08.11.2026: остаток блокировки на опции 7 карты 19 (loc 2385 + 2418)

	UFUNCTION() void OnSlot0(); UFUNCTION() void OnSlot1(); UFUNCTION() void OnSlot2();
	UFUNCTION() void OnSlot3(); UFUNCTION() void OnSlot4(); UFUNCTION() void OnSlot5();
	void OnSlotClicked(int32 SlotIdx);

	// loc-id подписи для типа опции (эталонная таблица 3/4/5/6/7/8).
	static int32 LocForOption(int32 OptionType);

	// Уровень персонажа: рантайм-поле Net->SelfLevel, фолбэк — запись char-select (как в HUD).
	int32 ResolveSelfLevel() const;
	int32 CharListLevel() const;


	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Modal;   // ONEBUT_MSGBOX (сценарий «вошёл мёртвым»)
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SlotBtn;    // 6 кнопок-реле над Choice0..5
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SlotTxt;    // 6 подписей Txt0..5

	TArray<FAsdaDeathOption> Options;   // опции в порядке добавления (эталон aDeathRecord[6*i])

	// Исходная (импортированная) геометрия планов фона окна — база для ужимания под число опций.
	TMap<class UCanvasPanelSlot*, TPair<float, float>> BgBase;   // слот → (исходный Y, исходная высота)
	// Базовые (импортные) offset.Top ТОП-виджетов окна — база рецентровки по фактической высоте.
	TMap<class UCanvasPanelSlot*, float> TopBase;
	float BgBaseTop = FLT_MAX;
	float BgBaseHeight = 0.f;
	bool  bMenuOpen = false;
	int32 AutoOption = -1;          // опция, срабатывающая по нулю отсчёта (эталон 4/7/8)
	float RespawnTimerSec = 0.f;    // эталон field_32DE4 (мс → секунды)
	float ClickLockSec = 0.f;       // эталон [1].buffFxSlots[108] — блокировка кликов 7/8 (30 с)
	int32 LastKillerIdx = -1;

	FDelegateHandle HDeath, HRevive, HOffer;

	static constexpr int32 kMaxOptions = 6;      // DeathMenu_AddOption: v16[73] <= 5
	static constexpr float kSlotStep   = 66.f;   // шаг слотов в JUI
	static constexpr int32 kDeathZ     = 800;    // поверх HUD и нотисов
};
