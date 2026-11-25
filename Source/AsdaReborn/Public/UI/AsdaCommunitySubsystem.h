// Copyright (c) 2026 @sitxovski (Discord)
// Контейнер социальной панели COMMUNITY_WIN (JUI 6400) — окно с четырьмя вкладками:
//   0 «Друг» (соулмейт, COMMUNITY_SOUL_WIN 6402), 1 «Приятель» (COMMUNITY_FRND_WIN 14500),
//   2 «Клан» (COMMUNITY_CLAN_WIN 14033), 3 «Кто?» (COMMUNITY_Who_WIN 14600).
// Порт `communityUI_Open@0xBD3D10` / `communityUI_SwitchTab@0xBD4040` / `Community_BuildUI@0xBD38E0`.
// Полный разбор — `Reports/soulmate_6400.md` §2. Содержимое вкладки 0 ведёт UAsdaSoulmateSubsystem.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaCommunitySubsystem.generated.h"

class UUserWidget;
class UButton;
class UTextBlock;
class AAsdaPlayerController;

UCLASS()
class ASDAREBORN_API UAsdaCommunitySubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Кнопка главного меню (73405 NEW_SYSTEMMAIN_COMUNITYBUT, индекс таба 5) и доп-меню (74612).
	static constexpr int32 kMenuButtonIdNum = 73405;
	// Хоткеи `input.c:4293`: case 35 → открыть на вкладке 0 (соулмейт), case 36 → вкладка 3 («Кто?»).
	static constexpr int32 kCmdCommunitySoul = 35;
	static constexpr int32 kCmdCommunityWho  = 36;
	// Крестик окна 6403 `COMMUNITY_CLOSE` (`CloseBtn_18` в разметке, `gamemain_jui.ini:184224`).
	//   Эталонная команда ведёт в `communityUI_Close@0xBD3F20` = наш `CloseWindow()`.
	static constexpr int32 kCloseButtonIdNum = 6403;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р5 отчёта 14_soulmate: подсистема жила на GameInstance и переживала
	// разрыв, смену персонажа и травел вместе со списками и защёлками сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// FTickableGameObject: только для поздней привязки меню/контроллера (окно появляется позже подсистемы).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCommunitySubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Публичный API (точки входа меню/хоткеев + консольные проверки asda.Community.*) ──

	// Открыть со всеми гейтами эталона (порт `communityUI_Open`). Вкладка по умолчанию — сохранённая/0.
	UFUNCTION(BlueprintCallable, Category="Asda|Сообщество")
	void OpenWindow();

	// Закрыть (порт `communityUI_Close`).
	UFUNCTION(BlueprintCallable, Category="Asda|Сообщество")
	void CloseWindow();

	// ★ 10.10.2026 (barber_minors): под-окно 6640 `OldSoulmate_Win` (узел `OldSoulmate_Win_grp` окна 6400).
	bool HideOldSoulmateWin();             // `uiHideControlIfSet(g_pSoulmateWnd)` — скрыть; true — было показано
	bool DebugShowOldSoulmateWin();        // показать — только самотест `asda.UI.SelfTestEscOnTop`
	bool IsOldSoulmateWinShown() const;    // под-окно видно

	// ★★★ D1: «окно видно» считаем по САМОМУ окну 6400, а не по вкладке 6402 (дефект оригинала).
	UFUNCTION(BlueprintCallable, Category="Asda|Сообщество")
	bool IsWindowOpen() const;

	// Переключить вкладку (0..3). Порт `communityUI_SwitchTab`. Гейт вкладки 2: нет клана → отмена.
	UFUNCTION(BlueprintCallable, Category="Asda|Сообщество")
	void SwitchTab(int32 Tab);

	// Открыть сразу на нужной вкладке (хоткеи 35/36, доп-меню).
	UFUNCTION(BlueprintCallable, Category="Asda|Сообщество")
	void OpenOnTab(int32 Tab);

	int32 GetActiveTab() const { return ActiveTab; }
	UUserWidget* GetWindowWidget() const { return Window; }

	// Многострочный дамп для живой проверки (`asda.Community.Dump`).
	FString DumpState() const;

private:
	static const FName  kWindowId;         // "COMMUNITY_WIN" (id окна в UAsdaWindowSubsystem)
	static const TCHAR* kWindowWbp;        // путь к WBP_COMMUNITY_WIN
	static constexpr int32 kWinZ = 400;    // слой окна (как у прочих окон меню)

	// Имена вкладок-подпанелей (JUI id) внутри 6400 — их видимостью и рулим при переключении.
	static const TCHAR* kTabPanelNames[4]; // COMMUNITY_SOUL_WIN / _FRND_WIN / _CLAN_WIN / _Who_WIN
	// Кнопки таб-бара COMMUNITY_MENU: импортёр создаёт `COMMUNITY_MENU_OPT0..3` (asda_ui_tabbar_captions).
	static const TCHAR* kTabBarIdc;        // "COMMUNITY_MENU"

	// ★★★ ПОДПИСИ ВКЛАДОК ПОЛОСЫ 6401 — ИМЕНА УЗЛОВ, А НЕ `<idc>_OPT<i>_Text`.
	//   Шаг пост-импорта `Tools/asda_ui_tabbar_captions.py` (см. `apply_control`) кладёт подписи
	//   переключателя под именами `<idc>_Text` (вкладка 0), `<idc>_Text2`, `<idc>_Text3`, … —
	//   нумерация со ВТОРОЙ вкладки. Проверено по самому ассету `WBP_COMMUNITY_WIN.uasset`:
	//   в нём лежат ровно `COMMUNITY_MENU_OPT0..3`, планы `COMMUNITY_MENU_753_0..3` и подписи
	//   `COMMUNITY_MENU_Text`, `_Text2`, `_Text3`, `_Text4`; узлов `COMMUNITY_MENU_OPT<i>_Text`
	//   в ассете НЕТ. Именно поэтому общий примитив `AsdaCommunityUi::SetOptSelected`, который
	//   ищет подписи как `FindLabel(<idc>_OPT<i>)`, не находил НИ ОДНОЙ и цвет вкладок не менял.
	static const TCHAR* kTabCaptionNames[4];

	// Строки подписей вкладок. 1:1 `Community_BuildUI@0xBD38E0` (`RE/etalon/social.c:3714-3721`):
	//   `CListBox_SetItemString(COMMUNITY_MENU, 0..3, loc 8014/8015/8016/8060)`.
	//   0 «Друг» (соулмейт), 1 «Приятель», 2 «Клан», 3 «Кто?» — см. `Reports/community_tabs_frnd_clan_who.md`.
	static constexpr int32 kTabCaptionLoc[4] = { 8014, 8015, 8016, 8060 };

	// ★★★ ВТОРАЯ, СКРЫТАЯ РАЗМЕТКОЙ ПОЛОСА ВКЛАДОК — источник «задвоения текста» (02.09.2026).
	//   В окне 6400 объявлены ДВЕ полосы одного скина `TabBar_Cmt_4Btn`:
	//     `COMMUNITY_MENU`   idnum 6401,  `Visible="1"`, 4 пункта — рабочая;
	//     `COMMUNITY_MENU_2` idnum 13631, `Visible="0"`, 5 пунктов — НЕ используется ни эталоном,
	//        ни портом (`gamemain_jui.ini:203331`; ни одной ссылки на 13631 в `RE/etalon/*.c`).
	//   Обе лежат В ТОЧНОСТИ друг на друге: экранный прямоугольник (302.5,122)-(644.5,144) против
	//   (302.5,122)-(721.5,144), а подписи пункта 0 совпадают до пикселя
	//   (`off L-209.5 T-259.0 R108.0 B22.0` у обеих — headless-дамп собранного ассета
	//   `Reports/headless_comm6400_dump.log`).
	//   ★ ПОЧЕМУ ВТОРАЯ ВИДНА. Импортёр честно погасил её планы и кнопки
	//   (`COMMUNITY_MENU_2_833_0..4`, `COMMUNITY_MENU_2_OPT0..4` — `Collapsed`), но ПОСЛЕ него
	//   по окну прошёл шаг подписей `Tools/asda_ui_tabbar_captions.py`, который создаёт/правит
	//   узлы `<idc>_Text<N>` и ЖЁСТКО ставит им `HIT_TEST_INVISIBLE`, не глядя на `Visible`
	//   контрола. В итоге `COMMUNITY_MENU_2_Text` осталась ВИДИМОЙ с заглушкой дизайнера
	//   «Соулмейт» ровно поверх подписи вкладки 0.
	//   Пока подписи вкладок были пустыми/такими же, это читалось как один текст; как только
	//   `ApplyTabBarState` начал писать в 6401 loc 8014 «Друг», поверх него полезла «Соулмейт».
	//   ★ ВЫЛЕЧЕНО В АССЕТЕ 2026-09-02 (закон `no-runtime-widget-fixes`: дефект разметки чинится
	//   в ассете, а не подпоркой в коде). Причина была в шаге `Tools/asda_ui_tabbar_captions.py`:
	//   он ставил подписям `<idc>_Text<N>` видимость ЖЁСТКО, не глядя на `Visible` контрола,
	//   и зажигал обратно то, что импортёр честно погасил. Шаг починен (видимость берётся из
	//   разметки JUI) и прогнан по ассетам: 69 подписей в 7 `.uasset`, бэкап
	//   `AstoryClient\_backup\tabbar_captions_2026-09-02\`.
	//   Прежняя рантайм-подпорка `HideDuplicateTabBar()` СНЯТА — она нарушала этот закон.
	//   Константа оставлена для ДИАГНОСТИКИ (`DumpState` считает живые узлы двойника): если
	//   после переимпорта UI призрак вернётся, это будет видно в дампе.
	static const TCHAR* kHiddenTabBarIdc;  // "COMMUNITY_MENU_2" (13631, `Visible="0"`)

	bool EnsureWindow();                    // ленивое построение (`Community_BuildUI`, флаг +48)
	void ResetWidget();
	void SetupControls();                   // привязать кнопки вкладок и закрытие
	void ApplyTabVisibility(int32 Tab);     // показать выбранную под-панель, спрятать прочие

	// ★★★ ВИД САМОЙ ПОЛОСЫ 6401: выбранный пункт (плашка кадра 2) + ТЕКСТ и ЦВЕТ подписей.
	//   В эталоне это делает сам контрол-список: `CControl_SetSelectedIndex` переносит выбор,
	//   а рендер подписи берёт `CaptionColor` АКТИВНОГО кадра плана (закон `jui-caption-states`).
	//   В UMG кнопка состояния не держит, поэтому оба шага зовём явно и ОДНИМ местом.
	//   Зовётся ТОЛЬКО после реального показа окна (закон `no-state-latch-before-shown`).
	void ApplyTabBarState(int32 Tab);

	// Цвет подписи вкладки: активная/неактивная (разбор и цифры — у констант в .cpp).
	void ApplyTabCaptionColors(int32 Tab);

	bool CanOpen(FString& OutBlockReason) const;   // гейты `communityUI_Open`
	int32 SelfClanId() const;               // для гейта вкладки 2

	// Обработчики
	UFUNCTION() void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpen);
	UFUNCTION() void OnGameCommand(int32 Cmd);
	void OnTabButtonClicked(int32 Tab);

	FString Notice(int32 LocId) const;
	void PostChat(const FString& Text) const;

	UPROPERTY(Transient) UUserWidget* Window = nullptr;
	UPROPERTY(Transient) TArray<UButton*> TabButtons;   // 4 кнопки COMMUNITY_MENU_OPT0..3
	// 4 подписи COMMUNITY_MENU_Text / _Text2 / _Text3 / _Text4 (имена — см. `kTabCaptionNames`).
	UPROPERTY(Transient) TArray<UTextBlock*> TabCaptions;

	/** ★★★ GC-ЯКОРЯ ОБРАБОТЧИКОВ КНОПОК ОКНА (регрессия 02.09.2026: «вкладки перестают
	 *  переключаться, окно не закрывается»).
	 *  `UButton::OnClicked` — ДИНАМИЧЕСКИЙ мультикаст: его запись (`FScriptDelegate`) держит
	 *  приёмник СЛАБО (`TWeakObjectPtr` + имя функции), а `NewObject<UAsdaButtonRelay>(this)`
	 *  внутренний объект НЕ ЯКОРИТ. Без массива реле четырёх кнопок вкладок и крестика 6403
	 *  умирают на первом же проходе сборщика мусора — окно остаётся на экране, но перестаёт
	 *  реагировать. Тот же приём уже применён в 30+ подсистемах порта
	 *  (`TArray<TObjectPtr<UAsdaButtonRelay>> Relays`) и в `UAsdaWindowSubsystem::CaptionRelays`.
	 *  Чистится в `SetupControls` — реле мёртвого дерева держать нельзя. */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Relays;

	int32 ActiveTab = 0;      // текущая вкладка (по умолчанию 0 — соулмейт)
	int32 PrevTab   = 0;      // прошлая вкладка (для отката гейта вкладки 2)
	bool  bReady    = false;  // контролы привязаны

	bool bMenuBound = false;                          // подписка на кнопку меню 73405 выполнена
	TWeakObjectPtr<AAsdaPlayerController> BoundController;   // для хоткеев 35/36
};
