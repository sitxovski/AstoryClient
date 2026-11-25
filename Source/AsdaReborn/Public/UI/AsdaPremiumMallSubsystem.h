// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPremiumMallSubsystem — ★★★ ДИСПЕТЧЕР ПРЕМИУМ-МОЛЛА, окно `Premium_Menu` (JUI IDNum **30000**).
//   Полный реверс — `Reports/premium_mall_core.md` §1; порт — `Reports/premium_mall_port.md`.
//
// ═══════════════ КЛАССИФИКАЦИЯ (закон `classify-before-building`) ══════════════════════════════
//   НОВАЯ СИСТЕМА: собственное корневое окно `.JUI` (30000, 10 узлов), собственный жизненный цикл
//   (тумблер по хоткею), собственные гейты и собственная маршрутизация в четыре разные подсистемы.
//   Это НЕ доделка магазина 6000 — 6000 всего лишь один из четырёх его адресатов.
//
// ═══════════════ ТРИ ВХОДА ════════════════════════════════════════════════════════════════════
//   1. Хоткей `\` — `INTERFACE PREMIUM:` = строка 39 `Shortcutkey.ini` → `hotkeyDispatchAction
//      @0xE3CD90` `case 39` (`ida.c:667666`) → `uiInventoryConfirmAction@0xC4AD60` (`ida.c:489938`).
//      ТУМБЛЕР: закрыт → открыть рамку молла и сегмент 0; открыт → закрыть ТЕКУЩИЙ сегмент.
//      В порту команда 39 уже лежит в дефолтах (`AsdaKeybindConfig.cpp:30` `{39, 220, 0}`),
//      подписка — `AAsdaPlayerController::OnGameCommand`.
//   2. Кнопка главного меню `NEW_NEW_Primumshop_M_BUT` **73407** (вкладка 3, нотис 2281 «Магазин»).
//      В порту она уже объявлена — `AsdaSystemMenuSubsystem.cpp:117`, `TargetWindowId = 0`.
//   3. ★★★ КОД ДИАЛОГА NPC **30020 — НЕ ВХОД В МОЛЛ.** Задание называет его входом со ссылкой на
//      `premium_mall_core.md` §1.1, но это ОШИБКА ОТЧЁТА, проверенная по коду:
//      `CGameUI_HandleControlCommand@0xD87590 case 30020` зовёт `shopWin_Open@0xC37110`, а тот
//      (`ida.c:483090-483118`) вызывает `shopWindowReset@0xC37540` — функцию, которая трогает
//      ИСКЛЮЧИТЕЛЬНО контролы STORE2 (`m_pStore2Check`, `m_pStore2Menu`, `m_pStore2Txt09/11`),
//      и пересчитывает высоту контейнера **6983** `Shellitem_Store2_Win_Part2`. То есть 30020 —
//      это МАГАЗИН СНАРЯЖЕНИЯ **6800**, и в порту он уже так и подключён
//      (`AsdaNpcDialogSubsystem.cpp:1692` → `UAsdaEquipShopSubsystem::OpenWindow`).
//      Окно **6000** открывает ДРУГАЯ функция — `uiInventoryOpenWindow@0xC3DCB0` (`ida.c:485563`:
//      показывает `gap_0[4]`, шлёт 5466), и её зовёт как раз сегмент 0 этого меню.
//      ⇒ ПРАВКА В `AsdaNpcDialogSubsystem.cpp` НЕ ТРЕБУЕТСЯ. Файл не тронут.
//
// ═══════════════ ПЕРЕКЛЮЧАТЕЛЬ `PremiumMall_Menu` 30002 — 4 СЕГМЕНТА ══════════════════════════
//   Скин `PremiumMall_Menu` (`JuiSkin.ini:48244`, `Type = "RadioButton"`): четыре плана
//   Rect (0,0,68,14) / (70,0,138,14) / (140,0,208,14) / (210,0,278,14), у ВСЕХ ЧЕТЫРЁХ
//   `PlaneAlwaysInvisible = "1"` — то есть вкладка не рисует НИЧЕГО, видна только ПОДПИСЬ.
//   Цвета подписи покадрово: кадр 0 покой `FFC8C8C8`, кадр 1 наведение `FFFFFFFF`,
//   кадр 2/3 ВЫБРАНО `FF94FF28` (зелёный).
//   ⚠ Именно из-за `PlaneAlwaysInvisible` импортёр НЕ создал кнопок `_OPT<i>` (его правило —
//     «по кнопке на ВИДИМЫЙ план»), и переключателя в `WBP_Premium_Menu` не было вовсе.
//     Недостающие узлы кладёт идемпотентный шаг `Tools/asda_ui_premium_menu_tabs.py`
//     (зарегистрирован в `asda_ui_post_import.STEPS`, сторожа — в `HANDMADE`).
//
//   Подписи ставит КОД (`CBoosterShop::Init`, `ida.c:420658-420671`):
//     сегм. 0 loc **99401** «Элитный»        → магазин 6000  (`UAsdaPremiumShopSubsystem`)
//     сегм. 1 loc **99400** «Бустер»         → бустер-шоп 15300 (ЧУЖАЯ ОБЛАСТЬ, отдельный этап)
//     сегм. 2 loc **99402** «Салон причесок» → парикмахерская 15000 (ещё не портирована)
//     сегм. 3 loc **99403** «Набор»          → витрина набора = вкладка **58300 внутри окна 6000**
//   Тумблер хоткея закрывает по ТОМУ ЖЕ индексу (`ida.c:489972-489984`).
//
//   ★★★ РАЗРЕШЕНО РАСХОЖДЕНИЕ ОТЧЁТОВ «26000 против 58300» — ЖИВА ВКЛАДКА 58300.
//   Второй метод (независимый от скана immediate'ов в exe, которым пользовался первый отчёт) —
//   ТРАССИРОВКА ЕДИНСТВЕННОГО ЖИВОГО ВХОДА:
//     • сегмент 3 зовёт `uiPackageStampWndOpenPage@0xDAC7E0` (`ida.c:423342`), а тот
//       (`ida.c:615245-615252`) делает ровно четыре вещи: Init, `uiBuildPackageItemList`,
//       `uiItemListRefreshPage`, `uiPackageStampWndRefreshGold` — и НЕ ПОКАЗЫВАЕТ `pMainPanel`;
//     • во ВСЁМ дампе IDA `pMainPanel` встречается 5 раз, и `SetVisible(pMainPanel, 1)` нет ни
//       разу: `ida.c:615936` (`FindControlByID(26000)`), `615937`, `615258` и `617242` (ЧТЕНИЕ
//       `isVisible`), `615263` (`SetVisible(…, 0)`);
//     • зато вкладку показывает КОД ОКНА 6000: `SetVisible(thisPtr[951], 1)` (`ida.c:473636`),
//       а `this[951]` привязано в `CShellItemStore_InitControls` как
//       `FindControlByID(g_pWindowMgr, 58300)` (`ida.c:480073`), с проверкой и логом
//       «58300 UI Error» (`ida.c:480075`).
//   ⇒ Витрина набора живёт как вкладка 58300 окна 6000; окно 26000 остаётся СКЛАДОМ ШАБЛОНОВ
//     (плашки «купи N — получи M» 26350/26360/26370 клонируются в карточки 58310+20i).
//     Порт делает ИМЕННО вкладку — см. `UAsdaPremiumShopSubsystem::ShowPackageTab`.
//
// ═══════════════ ГЕЙТЫ ОТКРЫТИЯ (`uiInventoryConfirmAction@0xC4AD60`, `ida.c:489950-489996`) ═══
//   1. молча ничего, если открыты `g_pItemMainWnd` / `g_pItemQtyMakeDlg` / поле254 инвентаря /
//      поле5581 склада;
//   2. молча ничего, если занят слот `getSlotByIndex(localPlayer, field3036)` (байт +68);
//   3. личная лавка → нотис **107022** «Ты в своей лавке» (чат, жёлтый);
//   4. `netSendSimpleCmd_5455` + снятие баффа 15 (принудительно слезть с маунта);
//   5. бой → нотис **6946** «В бою хранилище недоступно» (чат) + скрыть вкладку 3 меню;
//   6. предупреждение **97100** «Осторожнее! …на тебя могут напасть монстры!» (общая плашка) —
//      ★ это ПРЕДУПРЕЖДЕНИЕ, а не блокировка: окно всё равно открывается.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaPremiumMallSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UButton;
class UImage;
class UTextBlock;
class UAsdaButtonRelay;

UCLASS()
class ASDAREBORN_API UAsdaPremiumMallSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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

	// Тик нужен ровно для одного: контроллер игрока появляется ПОЗЖЕ подсистемы, а хоткей `\`
	//   приходит именно от него (`AAsdaPlayerController::OnGameCommand`).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPremiumMallSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа из данных ──
	static constexpr int32 kWinIdNum  = 30000;   // Premium_Menu
	static constexpr int32 kBarIdNum  = 30002;   // PremiumMall_Menu (RadioButton, 4 сегмента)
	static constexpr int32 kSegments  = 4;
	static constexpr int32 kCmdHotkey = 39;      // `INTERFACE PREMIUM:` — строка 39 Shortcutkey.ini
	static constexpr int32 kMenuBtn   = 73407;   // NEW_NEW_Primumshop_M_BUT
	// Рамка молла обязана лежать НАД магазином (733) и под тултипом/модалкой.
	static constexpr int32 kMenuZ = 8810;

	// ═══ ТУМБЛЕР (хоткей `\` и кнопка меню 73407) ═══
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМолл")
	void Toggle();

	// Открыть молл на сегменте (по умолчанию 0 — «Элитный»). Проходит все гейты.
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМолл")
	void OpenMall(int32 Segment = 0);

	// Закрыть рамку и ТЕКУЩИЙ сегмент (эталонный тумблер, `ida.c:489972-489984`).
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМолл")
	void CloseMall();

	// Переключить сегмент 0…3 — порт `case 30002` (`ida.c:423296-423345`).
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМолл")
	void SelectSegment(int32 Segment);

	// ★ 25.10.2026 (ui_toggles) [ОРИГИНАЛ]: «молл открыт» = латч `bOpen` (эталон `this+4`) И адресат текущего сегмента на
	//   экране. У эталона каждый путь закрытия магазина 6000 (крестик, ESC, загрузка карты) зовёт и `uiStorageWindow_Close`,
	//   то есть латч сам не переживает закрытое окно; в порту магазин закрывается мимо молла, поэтому латч сверяем с фактом.
	//   Ключ `asda.PremiumMall.ToggleFromFact 0` — прежнее (голый латч). Реализация — в .cpp.
	UFUNCTION(BlueprintPure, Category="Asda|ПремиумМолл") bool IsOpen() const;
	UFUNCTION(BlueprintPure, Category="Asda|ПремиумМолл") int32 GetSegment() const { return Segment; }

	// ★ Гейт «в бою» скрывает вкладку 3 системного меню (эталон
	//   `uiPanel_SetTabVisible(g_pSystemMainWnd, 3, 0)`); наружу отдаём сам результат проверки.
	UFUNCTION(BlueprintPure, Category="Asda|ПремиумМолл") bool CanOpen() const;
	// ★ Волна Л5 торговли (30.09.2026): гейт 3 «личная лавка» — витрина 23000 открыта ⇒ 107022 в чат, true (открывать/закрывать нельзя).
	bool RefuseOwnShop() const;
	// ★ Ревизия Л4/Л5 (МЕЛК-3): молчаливый гейт 1 эталона (ремесло 76000/77500, окно бустера, свиток возврата склада) — true: молчать.
	bool MallGate1Silent() const;

	// Диагностика.
	FString DumpControls() const;
	FString DumpState() const;

private:
	UFUNCTION() void OnGameCommand(int32 Cmd);                                   // хоткей 39
	UFUNCTION() void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpenReq); // 73407

	bool EnsureWidget();
	void SetupControls();
	void BindPlayerController();
	void RefreshTabState();
	void CloseSegment(int32 Seg);      // закрытие адресата сегмента
	// ★ 25.10.2026 (ui_toggles): адресат текущего сегмента сейчас на экране (рамка 30000 при `LegacyMenu`, магазин 6000,
	//   парикмахерская) — факт, по которому проверяется латч.
	bool IsAddresseeShown() const;
	// ★ 25.10.2026 (ui_toggles): латч «открыт» при закрытом адресате снять (адресат закрыли ESC/крестиком/другим окном мимо
	//   молла). `Why` — откуда позвали (для журнала).
	void ReconcileLatch(const TCHAR* Why);
	void OpenSegment(int32 Seg);       // открытие адресата сегмента
	FString Loc(int32 Id) const;
	void NoticeChat(int32 LocId) const;
	void NoticeTip(int32 LocId) const;

	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	// 4 кнопки-сегмента `PremiumMall_Menu_OPT0…3` и их подписи `_Text`/`_Text2`/`_Text3`/`_Text4`
	//   (кладёт шаг `Tools/asda_ui_premium_menu_tabs.py`).
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    SegButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SegLabels;
	// ★ Мёртвая пара «альтернативный селектор раздела» (дефект К10 оригинала): текст 30010
	//   «Select Shop» и комбобокс 30011 — оба `Visible="0"` в `.JUI` и кодом не показываются.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    DeadSelector;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Базовые цвета подписей — снимает общая точка `AsdaUi::ApplyTabCaptionState`.
	TArray<FLinearColor> TabIdleColors;

	bool  bOpen = false;
	bool  bReady = false;
	int32 Segment = 0;
	bool  bPcBound = false;
	// ★ 25.10.2026 (ui_toggles) [ИСПРАВЛЕНИЕ]: контроллер, на чей `OnGameCommand` подписан хоткей `\`. Травел UE создаёт НОВЫЙ
	//   контроллер, а флаг `bPcBound` оставался взведённым от старого — после любой смены карты `\` молл больше не открывал
	//   (команда 39 уходила в никуда). Как у соседей (`BoundController` окон персонажа/сумки/навыков/сообщества).
	TWeakObjectPtr<class AAsdaPlayerController> BoundPc;
	int32 CntOpen = 0;
	int32 CntSeg[kSegments] = { 0, 0, 0, 0 };
};
