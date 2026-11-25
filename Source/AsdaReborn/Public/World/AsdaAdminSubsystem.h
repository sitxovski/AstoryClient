// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaAdminSubsystem — АДМИН-РЕЖИМ (GM) клиента.
//
//   ★ ЭТО НЕ ПОРТ ОРИГИНАЛА. В AsdaStory такого экрана нет — это наша служебная надстройка по ТЗ
//     пользователя. Поэтому здесь нет ссылок на эталон: логика наша, UI — из ассетов пользователя
//     (/Game/AsdaAssets/UI/Admin/Admin_login, Admin_menu), собранных из тех же JUI-скинов, что и
//     остальной интерфейс (кнопки уже несут кадры Normal/Hover/Pressed скина — button_states_re.md).
//
//   ПОТОК (★ античит А2, мера 7, 12.10.2026 — права даёт СЕРВЕР, клиентского пароля больше нет):
//     1. CTRL+SHIFT+ALT+Q → `RequestAdminMode`. Клиентский расчёт «я оператор» (`UAsdaNetSubsystem::IsSelfOperator`:
//        UserIndex 0…5000 из 1004 или операторский префикс имени) — лишь ФИЛЬТР, чтобы обычный игрок не слал
//        операторский пакет. Оператору уходит проба прав — ★ ac0r (12.10.2026): опкод 6036 GetValue
//        (`UAsdaNetSubsystem::SendGmRightsProbe6036`), а не 6005: «принятый» 6005 переписывал на сервере состояние
//        действия оператора (CUser+21700 = 300/301) и рассылал 4025 — у сидящего оператора сбрасывалась поза.
//        6036 на сервере только читает и отвечает 6037 одному отправителю.
//     2. Ответ 6037 = команда прошла гейт оператора (`sub_52ECF0`, под P47 ещё GM-флаг): флаг сессии bAuthorized
//        (в память, НЕ на диск; гаснет при разрыве/выходе в выбор персонажа) и сразу меню. Отказа у 6036 нет —
//        без прав сервер молчит; по таймауту заявка снимается, и ★ до конца сессии проб больше нет (неотвеченная
//        проба одна на сессию; повтор — только после явного ответа). Если сервер уже подтверждал права в этой
//        сессии (6006 «принято» от пункта «Скрыться» или 6037), проба не нужна — меню открывается сразу.
//     3. F12 (только авторизованным) → окно GM-меню со списком функций.
//     4. Функция применяется кнопкой «Выбрать» (Apply_button/AD_TodayBtn) или ДВОЙНЫМ кликом по строке.
//        Закрытие: «Закрыть» (Close_button/AD_ClsBtn2), крестик Admin_Close, клавиша ESC.
//     5. Пока одна функция — СВОБОДНАЯ КАМЕРА. Реестр функций расширяемый: RegisterFunction().
//        ★ 12.10.2026 (античит П6): пункт `GM_BLOCK` — блок учётки ВЫБРАННОЙ цели на N часов (опкод 6094 →
//        сервер `UPHackBlock`); поток — `RE/docs/client/logic/gm_block_6094.md`.
//
//   ★ Прежний клиентский пароль (SHA-256 с хэшем в бинаре, подбирался офлайн) убран 12.10.2026: свободная камера
//     давала разведку в пределах AoI любому, кто подобрал пароль. Окно `Admin_login` (ассет) больше не открывается.
//     Подменённый клиент обойдёт и этот гейт — защита GM-команд остаётся на сервере; здесь закрыт путь «обычный клиент + пароль».
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "InputCoreTypes.h"
#include "Containers/Ticker.h"   // FTSTicker::FDelegateHandle — очередь отложенных действий меню
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaAdminSubsystem.generated.h"

class APlayerController;
class ACameraActor;
class UUserWidget;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;

// Пункт GM-меню: строка списка + обработчик. Реестр наполняется в Initialize() и через RegisterFunction().
struct FAsdaAdminFunction
{
	FName   Id;             // машинный идентификатор ("FREECAM")
	FText   Title;          // подпись в списке (для пунктов с постоянной подписью)
	FText   Hint;           // короткая подсказка (вторая колонка строки)
	TFunction<void(APlayerController*)> Activate;   // что делать по «Выбрать»/двойному клику
	// ★ ПЕРЕКЛЮЧАТЕЛИ: подпись зависит от текущего состояния («Скрыться: ВКЛ / ВЫКЛ»).
	//   Если задана — используется ВМЕСТО `Title` и пересчитывается при каждой отрисовке строк
	//   (`BuildFunctionRows`) и по `RefreshRowTexts()`. Пусто = обычный пункт с постоянной подписью.
	TFunction<FText()> DynamicTitle;
	// ★ 10.10.2026 (язык на лету): номера строк DT_GameText подписи и подсказки (0 — текст задан строкой). По ним
	//   `Title`/`Hint` пересчитываются на `FAsdaLang::OnLanguageChanged`.
	int32 TitleLoc = 0;
	int32 HintLoc = 0;
	// ★ 19.10.2026 (вкладки меню, просьба владельца): вкладка пункта — `EAsdaAdminTab` (0 «Функции порта», 1 «Команды оригинала»).
	uint8 Tab = 0;
};

// ★ 19.10.2026: вкладки GM-меню. Строка вкладок рисуется кодом над списком (как и сами строки) — ассет окна не меняется.
enum class EAsdaAdminTab : uint8
{
	Port = 0,       // «Функции порта» — служебные пункты порта (свободная камера, «Скрыться», рейтинги, блок учётки …)
	Original = 1,   // «Команды оригинала» — штатные операторские команды AsdaStory (`UAsdaGmOriginalSubsystem`)
	Count
};

UCLASS()
class ASDAREBORN_API UAsdaAdminSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Права оператора выдаются НА СЕССИЮ; ResetSession уже есть, ей нужен вызов.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── Реестр функций ────────────────────────────────────────────────────────────────────────
	// Добавить пункт меню. Дальше расширяем ТОЛЬКО этим (новый пункт = одна регистрация).
	void RegisterFunction(FName Id, const FText& Title, const FText& Hint,
		TFunction<void(APlayerController*)> Activate);
	// Пункт-ПЕРЕКЛЮЧАТЕЛЬ: подпись строит `TitleFn` при каждой отрисовке («Скрыться: ВКЛ/ВЫКЛ»).
	//   Отдельная функция, а не параметр по умолчанию, — чтобы у существующих 5 регистраций
	//   не менялась сигнатура (закон check-existing-before-creating).
	void RegisterToggleFunction(FName Id, TFunction<FText()> TitleFn, const FText& Hint,
		TFunction<void(APlayerController*)> Activate);
	// ★ 10.10.2026: подписи пункта по номерам loc (DT_GameText) — текст на языке сессии и пересчёт при смене языка на
	//   лету. TitleLoc/HintLoc ≤ 0 — не трогать. Пункт должен быть уже зарегистрирован.
	void SetFunctionLoc(FName Id, int32 TitleLoc, int32 HintLoc);
	// ★ 19.10.2026: перенести пункт на вкладку (по умолчанию все пункты — на вкладке «Функции порта»).
	void SetFunctionTab(FName Id, EAsdaAdminTab Tab);
	// ★ 19.10.2026: активная вкладка меню; переключение перестраивает список (если окно открыто).
	EAsdaAdminTab GetActiveTab() const { return ActiveTab; }
	void SetActiveTab(EAsdaAdminTab Tab);
	// ★ 19.10.2026 (самотест): id пунктов вкладки в порядке показа.
	TArray<FName> GetTabFunctionIds(EAsdaAdminTab Tab) const;
	// ★ 19.10.2026 (самотест): текст строки вкладки / страницы и число строк-пунктов текущей страницы (окно должно быть открыто).
	int32 DebugVisibleRowCount() const { return RowFuncIndex.Num(); }
	FString DebugRowText(int32 Row) const { return RowLineText(Row); }
	// ★ 19.10.2026 (самотест): перейти на страницу списка (0…); вне диапазона — зажим.
	void SetPage(int32 Page);
	int32 GetPage() const { return Page; }
	int32 GetPageCount() const;
	// ★ 19.10.2026 (самотест, ревизия С-3): кнопка вкладки/листания по порядку постройки (вкладки 0…, затем «Назад», «Вперёд»).
	class UButton* DebugTabButton(int32 Index) const { return TabButtons.IsValidIndex(Index) ? TabButtons[Index].Get() : nullptr; }
	// ★ 21.10.2026 (самотест): выполнить отложенные действия клика сейчас — вместо прокачки тикера ядра изнутри тикера.
	void DebugRunDeferredMenuActions() { RunDeferredMenuActions(); }

	// ── Сессия ────────────────────────────────────────────────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category="Asda|Admin")
	bool IsAuthorized() const { return bAuthorized; }

	// Сброс прав + закрытие окон + выход из свободной камеры. Зовётся при выходе в меню логина.
	UFUNCTION(BlueprintCallable, Category="Asda|Admin")
	void ResetSession();

	// ── Хоткеи и тик (зовёт AAsdaPlayerController::PlayerTick — там уже есть опрос клавиш) ─────
	// CTRL+SHIFT+ALT+Q / F12 / ESC. Фронты отслеживаются внутри (PrevDown).
	// ★★★ РЕВИЗИЯ 2026-09-17 (С6): ВОЗВРАЩАЕТ «ESC ПОГЛОЩЁН».
	//   true = нажатие ESC в этом кадре ушло админ-надстройке (выход из свободной камеры либо
	//   закрытие GM-меню) и НАВЕРХ ПРОХОДИТЬ НЕ ДОЛЖНО. Раньше метод был `void`, и
	//   `AAsdaPlayerController::PlayerTick` сразу за ним безусловно звал
	//   `UAsdaSystemOptionSubsystem::PollHotkeys` — тем же нажатием закрывалось GM-меню И
	//   открывалось ESC-меню 69400. Приоритетная раскладка ESC у эталона одноуровневая
	//   (`GameInput_DispatchEvent` case 2, `inputHandleInventoryAndEscHotkeys@0xC6E710`):
	//   сработавший обработчик забирает клавишу себе.
	bool PollHotkeys(APlayerController* PC);
	bool IsFreeCamActive() const { return FreeCam != nullptr; }
	// Движение свободной камеры (WASD + мышь). Зовётся вместо обычного управления персонажем.
	void TickFreeCam(APlayerController* PC, float DeltaSeconds);

	// ── ★ Античит А2 (мера 7): вход в админ-режим по СЕРВЕРНОМУ признаку оператора ───────────────
	// Запрос админ-режима (CTRL+SHIFT+ALT+Q): меню, если права уже есть; иначе проба 6036 оператору (см. шапку).
	void RequestAdminMode(APlayerController* PC);
	// Ожидание ответа 6037 на пробу: ответ → права и меню; таймаут → заявка снята, повторов в сессии нет. Зовёт `PollHotkeys`.
	void TickAuthProbe(APlayerController* PC);
	// Проба 6036 отправлена, ответа 6037 ещё нет.
	bool IsAuthPending() const { return bAuthPending; }
#if !UE_BUILD_SHIPPING
	// ★ ТОЛЬКО САМОТЕСТ: «состарить» ждущую пробу на Seconds, чтобы проверить таймаут без ожидания. В Shipping нет.
	void AgeAuthProbeForSelfTest(double Seconds) { AuthProbeSentAt -= Seconds; }
#endif
	// GM-меню сейчас открыто (для самотеста).
	bool IsMenuOpen() const { return MenuWidget != nullptr; }
	// ★ 12.10.2026 (античит П6, самотест пункта «Заблокировать учётку»): пункт с таким id зарегистрирован.
	bool HasFunction(FName Id) const;
	// ★ 12.10.2026 (античит П6): применить пункт по id МИМО окна меню — тот же гейт прав, что у `ActivateSelected`
	//   (без прав от сервера — false, обработчик не зовётся). Нужен самотесту: окно меню офлайн кликать нечем.
	bool ActivateFunctionById(FName Id, APlayerController* PC);
	// ★ 12.10.2026 (античит П6): обработчик пункта «Заблокировать учётку цели»: цель → NUMPAD_WIN (часы) → TWOBUT «ОК/Отмена»
	//   → `UAsdaNetSubsystem::SendGmBlock6094`. Публичен — его же зовёт самотест `asda.Admin.SelfTestGmBlock6094`.
	void ActivateGmBlock(APlayerController* PC);

	// ── Окна ──────────────────────────────────────────────────────────────────────────────────
	// ★ Сбросить указатели на окна, снесённые `UAsdaWindowSubsystem::CloseAll()` (см. .cpp).
	void DropClosedWindows();
	void OpenMenuWindow(APlayerController* PC);
	void CloseMenuWindow();

	// Свободная камера (функция реестра; публична — может пригодиться консоли/тестам).
	void EnterFreeCam(APlayerController* PC);
	void ExitFreeCam();

private:
	// Обработчики UI.
	UFUNCTION() void OnApplyClicked();
	UFUNCTION() void OnCloseClicked();

	void BuildFunctionRows();          // строки списка поверх Admin_menu_content
	void RebuildRows();                // ★ 19.10.2026: снять строки/вкладки/листание с канвы и собрать заново
	// ★ 19.10.2026 (ревизия С-3): выполнить действие меню на следующем тике ядра — клик вкладки/листания пересобирает
	//   строки, и снимать кнопку внутри рассылки её же OnClicked нельзя.
	void DeferMenuAction(TFunction<void()> Action);
	// Выполнить накопленные отложенные действия меню (тикер и самотест).
	void RunDeferredMenuActions();
	TArray<TFunction<void()>> PendingMenuActions;    // отложенные действия клика (вкладка/листание)
	FTSTicker::FDelegateHandle DeferTickHandle;      // тикер их разбора (один на очередь)
	void RefreshRowSelection();        // подсветка выбранной строки
	// ★ Пересобрать ТЕКСТ строк, не трогая сами виджеты. Нужен пунктам-переключателям: состояние
	//   может смениться, пока меню открыто (ответ сервера приходит асинхронно).
	void RefreshRowTexts();
	// Одна подпись строки: динамическая, если пункт — переключатель, иначе постоянная.
	FString RowLineText(int32 Index) const;
	void SelectRow(int32 Index);       // одиночный клик (+ детект двойного)
	void ActivateSelected();

	// Реестр пунктов.
	TArray<FAsdaAdminFunction> Functions;

	// ── ★ 19.10.2026: вкладки и страницы списка ──
	EAsdaAdminTab ActiveTab = EAsdaAdminTab::Port;   // показываемая вкладка
	int32 Page = 0;                                  // страница списка активной вкладки
	int32 RowsPerPage = 0;                           // сколько строк-пунктов помещается (считает BuildFunctionRows)
	TArray<int32> RowFuncIndex;                      // строка списка → индекс в Functions
	// Индексы пунктов вкладки в порядке показа.
	TArray<int32> TabFunctionIndices(EAsdaAdminTab Tab) const;
	// Кнопки вкладок и листания (строятся вместе со строками; держатся для GC).
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabTexts;
	// Подпись вкладки на языке сессии.
	static FString TabTitle(EAsdaAdminTab Tab);

	// Права на текущую сессию (в память; НЕ сохраняются). ★ Античит А2/ac0r: ставятся ТОЛЬКО по ответу сервера
	//   (6037 на пробу 6036 или 6006 «принято»).
	bool bAuthorized = false;
	// ★ Античит А2 (мера 7), ac0r: проба 6036 ушла, ждём 6037; номер ответа 6037 на момент пробы; момент пробы (с).
	bool   bAuthPending = false;
	uint32 AuthProbeSeq = 0;
	double AuthProbeSentAt = 0.0;

	UPROPERTY(Transient) TObjectPtr<UUserWidget> MenuWidget = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> RowButtons;      // строки списка функций
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays; // реле кликов (GC-рут)
	UPROPERTY(Transient) TObjectPtr<ACameraActor> FreeCam = nullptr;  // актор свободной камеры

	int32  SelectedRow = 0;
	int32  LastClickedRow = -1;
	double LastRowClickTime = 0.0;     // для детекта двойного клика (UButton двойных не даёт)

	// Фронты хоткеев (клавиша → была ли нажата в прошлом опросе).
	TMap<FKey, bool> PrevDown;

	// ── ★★★ GM-«скрыться» (опкод 6005) ────────────────────────────────────────────────────────
	//   Само СОСТОЯНИЕ здесь НЕ хранится: правда живёт в `UAsdaNetSubsystem::IsGmHidden()` и
	//   меняется только эхом сервера 6006. Здесь — лишь то, что нужно самому меню.
	bool bLastShownGmHidden = false;   // что показала подпись в прошлом кадре (чтобы не перерисовывать зря)
	bool bLastShownGmPending = false;
	// Сторож «ждём ответа»: сервер на отказ по правам отвечает ВСЕГДА, но связь может пропасть.
	//   По истечении заявка снимается, и кнопка снова живая (закон no-state-latch-before-shown).
	void TickGmHideWatchdog();

	// ── Состояние свободной камеры ────────────────────────────────────────────────────────────
	float FreeCamYaw = 0.f, FreeCamPitch = 0.f;
	float FreeCamSpeed = 1200.f;       // базовая скорость, юн/с (колесо меняет ×1.25 / ×0.8)
	TWeakObjectPtr<AActor> PrevViewTarget;   // куда вернуть вид по ESC
};
