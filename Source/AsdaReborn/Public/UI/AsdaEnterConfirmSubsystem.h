// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaEnterConfirmSubsystem — Enter (и Enter цифрового блока) нажимает БЕЗВРЕДНУЮ кнопку согласия верхнего окна; ESC под
//   модалкой реальной клавишей = «Отмена». [ИСПРАВЛЕНИЕ] 25.10.2026, воля владельца; редакция 2 (3.0.1) по ревизии
//   `Reports/wave_1025c_review/REVIEW.md` (ВАЖНО-1…4, СРЕДН-1). Аудит окон — `Reports/enter_confirm/AUDIT.md`.
//
// КАК У ОРИГИНАЛА (доказано по эталону, подробно — AUDIT.md §1):
//   • механизма «кнопка по умолчанию» в JUI НЕТ. Клавиатура окон — слоты 15/16 vtable контрола (KeyDown/KeyUp, их зовут
//     `uiControlDispatchKeyDown@0x12DCAB0` / `uiControlDispatchKeyToChildren@0x12DCB70`). У базы `0x1502FF8` и 16 типов это
//     `retC_0_3170@0x12E3170` / `retC_0_3190@0x12E3190` («не обработал»), кроме CEditBox (`0x12F9700/0x12F9730`, поле с фокусом).
//   • Enter в поле ввода = событие 67587 (0x10803) окну, на ОТПУСКАНИИ (`ctrlEditImeWndProc@0x12F82C0`, WM_KEYUP → LABEL_39);
//     правила такого Enter — у каждого окна свои (нумпад 1412 → ОК, окно 1552 поле 1558 → 1554, лавка 23403 → 6313…).
//   • Enter вне поля: менеджер окон БЕЗУСЛОВНО зовёт колбэк `+0x21C` (`CControl_DispatchMessage@0x12B2440`) =
//     `GameInput_DispatchEvent@0xC6DCA0`, ветка 2 (отпускание), клавиша 13 → `uiChatInputToggle` (гейт `uiCanOpenWindowGate`).
//     У оригинала Enter при открытом окне ОТКРЫВАЕТ ЧАТ, а не жмёт кнопку.
//   • ESC = событие 21 верхнему видимому окну с признаком +0x228 (`uiContainerHandleKey@0x12AF390`): 1520 → 1524 «Отмена»,
//     1530 → 1533, 1500 — съедает. Порт: `UAsdaWindowSubsystem::CloseTopmostOnEsc`.
//   • Enter цифрового блока у Win32 — тот же VK_RETURN (13) с битом «расширенная»; действие по ОТПУСКАНИЮ ⇒ автоповтор не множит.
//
// ЧТО ДЕЛАЕТ ПОРТ [ИСПРАВЛЕНИЕ] (ключ `asda.UI.EnterConfirm`, 0 = как было):
//   ПРАВИЛО (3.0.1): Enter жмёт только безвредное и обратимое согласие — информационное «ОК», закрытие, подтверждение ввода.
//   Окна, где кнопка сразу делает необратимое или тратит деньги/предметы (разбор, роспуск клана, знак зодиака, магазины, кузнец,
//   ремесло, питомцы, премиум…), в таблице НЕТ; входящие от сервера вопросы и вопросы о необратимом (TWOBUT/MESSAGE2) помечены
//   вызывающим «без Enter» (`AsdaEnterGuard::NoEnter`, `FAsdaMsgBoxEntry::bNoEnter`) — их подтверждает только щелчок.
//   Препроцессор ввода Slate видит Enter раньше виджетов и вьюпорта. НАЖАТИЕ (не автоповтор, без Alt/Ctrl/Shift):
//     1) фокус в поле ввода — Enter по правилу поля (не трогаем); dev-меню / захват клавиши 69500 — их ввод;
//     2) строка чата открыта (`IsInputActive`, даже без фокуса) — Enter чата: и модальное окно Enter НЕ жмёт; модалку, всплывшую
//        во время набора (показ модалки строку чата закрывает), Enter не жмёт до конца её показа — только щелчок;
//     3) модальное окно (`GetTopModalWindow`) — только оно: кнопка видна и доступна → наше нажатие; недоступна, вопрос «без Enter»
//        или окно показано меньше 400 мс → Enter съеден, ничего; модалки нет в таблице → не трогаем;
//     4) иначе — верхнее по z видимое окно среди окон таблицы и эталонной ESC-таблицы (кроме сумки 400 и подсказок):
//        окно таблицы с видимой доступной кнопкой, показанное ≥ 400 мс → наше нажатие; иначе — прежний путь (чат).
//   Наше нажатие съедается целиком (нажатие, автоповтор, отпускание). Кнопка жмётся на ОТПУСКАНИИ [ОРИГИНАЛ] и только если окно,
//   кнопка и СОДЕРЖИМОЕ (номер записи очереди окон-сообщений `Serial` / номер показа MESSAGE2 / видимая кнопка) — те же, что на
//   нажатии: подмена вопроса в том же окне во время удержания не подтверждается. Нажатие = звук кнопки + `UButton::OnClicked`.
//   «400 мс» отсчитываются от показа окна ИЛИ смены его содержимого (тот же виджет с новой записью очереди).
//   ESC (ключ `asda.UI.EscModalKey`, 0 = как было): под модалкой, которая забрала ввод мира (`IsWorldInputCapturedByModal`,
//   вьюпорт клавиш не получает), реальная клавиша идёт в тот же эталонный проход `CloseTopmostOnEsc` (1520 → «Отмена» 1524,
//   1500 — съедает) [ОРИГИНАЛ]; прежде под `FInputModeUIOnly` ESC до опроса контроллера не доходил.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaEnterConfirmSubsystem.generated.h"

class UButton;
class UUserWidget;
class UWidget;
class FAsdaEnterConfirmKeyProc;
struct FKeyEvent;

UCLASS()
class ASDAREBORN_API UAsdaEnterConfirmSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── FTickableGameObject: отметка времени показа / смены содержимого окон таблицы (правило «400 мс») ──
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaEnterConfirmSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual bool IsTickableWhenPaused() const override { return true; }

	// Итог разбора «чья клавиша Enter» (общий для нажатия, дампа и самотеста).
	enum class EVerdict : uint8
	{
		Off,          // ключ asda.UI.EnterConfirm 0 — не трогаем
		TextField,    // фокус в поле ввода — правило окна
		Chat,         // идёт набор в чате
		DevMenu,      // открыто dev-меню Ctrl+N / захват клавиши в настройках
		NoTarget,     // окна согласия сверху нет — прежний путь (чат)
		Disabled,     // кнопка согласия верхнего окна видна, но недоступна
		Forbidden,    // вопрос помечен «без Enter» (необратимое / входящее от сервера) — только щелчок
		TooEarly,     // окно (или его содержимое) показано меньше 400 мс назад
		Press,        // жмём кнопку согласия
	};

	// Разобранная цель: окно, кнопка, модальность, содержимое и пояснение (для журнала).
	struct FTarget
	{
		EVerdict Verdict = EVerdict::NoTarget;   // итог
		FName WindowId;                          // окно (реестр окон или внешняя роспись)
		FName ButtonName;                        // имя кнопки (idc .JUI)
		TWeakObjectPtr<UButton> Button;          // сама кнопка
		bool bModal = false;                     // окно — модальное (неподходящее нажатие съедается)
		uint64 ContentKey = 0;                   // ключ содержимого: номер записи очереди / показа MESSAGE2 (0 — у окна его нет)
		FString Why;                             // почему так решено (строка журнала/дампа)
	};

	// Разобрать текущее состояние: чьим будет нажатие Enter прямо сейчас (без побочных эффектов).
	FTarget ResolveTarget() const;

	// ★ Окно, которое показано мимо реестра окон (`AddToViewport` напрямую) — выбор сервера/канала на сцене входа.
	//   Z — порядок вьюпорта. Роспись слабая (виджет ушёл — запись мертва). Повторный вызов с тем же Id заменяет запись.
	static void RegisterExternalWindow(const UObject* Ctx, FName WindowId, UUserWidget* Widget, int32 ZOrder);
	// Снять роспись внешнего окна.
	static void UnregisterExternalWindow(const UObject* Ctx, FName WindowId);
	// Есть ли окно в таблице «окно → кнопка согласия» (самотест: опасные окна в таблицу не попали).
	static bool IsWindowInTable(FName WindowId);
	// Порог «окно только что показано», с (правило 400 мс; ключ `asda.UI.EnterConfirmDelayMs`).
	static double GetShowDelaySec();

	// Сколько раз Enter нажал кнопку согласия с начала сессии (самотест).
	int32 GetPressCount() const { return PressCount; }
	// Последнее нажатие: окно и кнопка (самотест/дамп).
	FName GetLastPressedWindow() const { return LastWindow; }
	FName GetLastPressedButton() const { return LastButton; }
	// Сколько раз реальная клавиша ESC под модалкой ушла в проход `CloseTopmostOnEsc` (самотест).
	int32 GetEscModalCount() const { return EscModalCount; }
	// Идёт ли сейчас «наше» удержание Enter (нажатие съедено, ждём отпускания).
	bool IsHoldingOurPress() const { return Session == ESession::Ours || Session == ESession::Blocked; }

	// Полный дамп: таблица окон, видимость, найденные кнопки и текущее решение (консоль asda.UI.EnterConfirm.Dump).
	FString DumpState() const;
	// ★ Сверка таблицы с ассетами: каждое окно таблицы грузится (класс WBP), в нём есть каждая кнопка-кандидат и она `UButton`.
	//   OutLines — строки отчёта, возврат — число расхождений (0 = таблица верна). Только вне Shipping (создаёт виджеты).
	int32 VerifyTableAgainstAssets(TArray<FString>& OutLines) const;

	// Строка решения для журнала.
	static const TCHAR* VerdictName(EVerdict V);

	// ── вход препроцессора (только для FAsdaEnterConfirmKeyProc) ──
	bool HandleEnterDown(const FKeyEvent& Ev);   // true — нажатие съедено
	bool HandleEnterUp(const FKeyEvent& Ev);     // true — отпускание съедено (и, может быть, кнопка нажата)
	bool HandleEscDown(const FKeyEvent& Ev);     // true — ESC ушёл в проход под модалкой и съеден

private:
	// Фаза удержания Enter: от физического нажатия до отпускания.
	enum class ESession : uint8
	{
		None,      // Enter не зажат (или зажат до нас — решение не принималось)
		Pass,      // нажатие отдано прежнему пути (поле, чат, окна согласия нет)
		Ours,      // нажатие наше: на отпускании жмём кнопку
		Blocked,   // нажатие съедено модалкой (недоступно / без Enter / рано): на отпускании ничего не делаем
	};

	// Внешнее окно (мимо реестра): виджет и порядок вьюпорта.
	struct FExternal
	{
		TWeakObjectPtr<UUserWidget> Widget;   // окно
		int32 ZOrder = 0;                     // порядок вьюпорта
	};

	// Что видно у окна таблицы сейчас и с какого момента (правило «400 мс»).
	struct FShown
	{
		TWeakObjectPtr<UUserWidget> Widget;   // виджет окна
		uint64 ContentKey = 0;                // ключ содержимого
		FName Button;                         // видимая кнопка-кандидат
		double Since = 0.0;                   // FPlatformTime::Seconds() показа / смены содержимого
		bool bDuringChat = false;             // строка чата была открыта при показе (или пока окно видно) — модалку Enter не жмёт
	};

	// Найти в окне Root первую видимую кнопку-кандидата строки таблицы (порядок строки = приоритет).
	//   OutEnabled — доступна ли она. nullptr — ни одна не видна.
	UButton* FindVisibleCandidate(UUserWidget* Root, int32 TableRow, bool& OutEnabled, FName& OutName) const;
	// Виджет окна по имени: реестр окон, затем внешние росписи.
	UUserWidget* WindowWidget(FName WindowId) const;
	// Ключ содержимого окна: номер показанной записи очереди (ONEBUT/TWOBUT), номер показа MESSAGE2; иначе 0.
	uint64 ContentKeyOf(FName WindowId) const;
	// Помечено ли содержимое окна «без Enter».
	bool IsContentNoEnter(FName WindowId) const;
	// Сколько секунд окно показано в нынешнем виде (виджет, содержимое, кнопка); отрицательно — ещё не замечено тиком.
	double ShownForSec(FName WindowId, const UUserWidget* Root, uint64 Key, FName Button) const;
	// Окно в нынешнем виде показано, пока была открыта строка чата (игрок набирал текст — его Enter предназначался чату).
	bool ShownDuringChat(FName WindowId) const;
	// Нажать кнопку так, как это делает щелчок мыши: звук нажатия + рассылка OnClicked.
	void PressButton(UButton* B, FName WindowId, FName ButtonName);

	TSharedPtr<FAsdaEnterConfirmKeyProc> KeyProc;   // препроцессор ввода Slate
	TMap<FName, FExternal> Externals;               // окна мимо реестра (выбор сервера/канала)
	TMap<FName, FShown> Shown;                      // окна таблицы: что видно и с какого момента
	double LastChatActiveAt = -1.0;                 // когда тик последний раз видел открытую строку чата (FPlatformTime::Seconds)
	ESession Session = ESession::None;              // фаза текущего удержания Enter
	FName ArmedWindow;                              // окно, чьё нажатие ждёт отпускания
	FName ArmedButtonName;                          // кнопка этого окна
	TWeakObjectPtr<UButton> ArmedButton;            // она же
	uint64 ArmedContentKey = 0;                     // содержимое окна на нажатии
	int32 PressCount = 0;                           // нажатий кнопок согласия за сессию
	int32 EscModalCount = 0;                        // ESC под модалкой, отданных проходу
	FName LastWindow;                               // окно последнего нажатия
	FName LastButton;                               // кнопка последнего нажатия
};
