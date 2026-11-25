// Copyright (c) 2026 @sitxovski (Discord)
// AsdaButtonRelay — мостик для UButton::OnClicked (у него нет отправителя): несёт индекс + колбэк.
// Позволяет привязать много одинаковых кнопок UMG (SERVER_BTN00.., CHANNEL_BTN00..) к одному обработчику
// с передачей индекса. Создаётся на владельце-виджете, живёт вместе с ним.
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AsdaButtonRelay.generated.h"

UCLASS()
class ASDAREBORN_API UAsdaButtonRelay : public UObject
{
	GENERATED_BODY()
public:
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.RelayAnchorPrune`): виджет, чьи делегаты держат это реле (слабо). Ставит общая точка
	//   `AsdaUi::AnchorRelay`/`MakeClickRelay`. Виджет уничтожен (окно пересоздано и старое дерево собрано) ⇒ реле мертво, и якорь
	//   владельца выбрасывает его при следующей привязке — иначе мёртвые реле копились в якоре при каждом пересоздании окна.
	TWeakObjectPtr<UObject> BoundWidget;
	bool bHasBoundWidget = false;   // виджет был задан (реле без него якорь не трогает)
	// Роль реле у владельца (пусто — без роли): по ней владелец снимает прежние подписки той же роли с той же кнопки.
	FName Role;
	// Реле мертво: его виджет был задан и уже уничтожен.
	bool HasDeadBinding() const { return bHasBoundWidget && !BoundWidget.IsValid(); }
	// Запомнить виджет, чьи делегаты держат реле.
	void SetBoundWidget(UObject* W) { BoundWidget = W; bHasBoundWidget = (W != nullptr); }

	int32 Index = 0;
	TFunction<void(int32)> Callback;
	// Для кнопок с УДЕРЖАНИЕМ (стрелки вращения превью): (index, bPressed) — true на нажатии, false на отпускании.
	TFunction<void(int32, bool)> PressCallback;
	// Наведение/уход мыши (подсказка помощника «Минопио» по hover кнопки, эталон loginWinOnEvent GOT/LOST_FOCUS).
	TFunction<void()> HoverCallback;
	TFunction<void()> UnhoverCallback;
	// ★ Тот же мостик для JUI-контролов типа CheckButton (импортёр делает из них `UCheckBox`):
	//   `OnCheckStateChanged` — динамический делегат с bool, отправителя не передаёт. Нужен там, где
	//   ОДИН обработчик обслуживает НЕСКОЛЬКО чекбоксов и должен различать их по индексу — панель
	//   главного меню `SYSTEMMAIN_NEW_WIN` (10 кнопок-тогглов, эталонные события 67073/67074).
	TFunction<void(int32, bool)> CheckCallback;

	UFUNCTION()
	void OnClicked() { if (Callback) { Callback(Index); } }

	UFUNCTION()
	void OnPressed() { if (PressCallback) { PressCallback(Index, true); } }

	UFUNCTION()
	void OnReleased() { if (PressCallback) { PressCallback(Index, false); } }

	UFUNCTION()
	void OnHovered() { if (HoverCallback) { HoverCallback(); } }

	UFUNCTION()
	void OnUnhovered() { if (UnhoverCallback) { UnhoverCallback(); } }

	// Тогл чекбокса: bChecked=true = эталонное событие 67073 (0x10601), false = 67074 (0x10602).
	UFUNCTION()
	void OnCheckChanged(bool bChecked) { if (CheckCallback) { CheckCallback(Index, bChecked); } }

	// ★ Тот же мостик для `USlider::OnValueChanged` (динамический делегат с float, отправителя не передаёт) и
	//   `USlider::OnMouseCaptureBegin/End`, `OnControllerCaptureBegin/End` (без аргумента): ползунки dev-меню Ctrl+N
	//   («Дальность тумана», «Плотность тумана»). OnSliderValue — живой накат значения при движении (второй аргумент —
	//   идёт ли захват мышью/Enter: если нет, значение пришло с клавиатуры без захвата и «отпускания» не будет —
	//   получатель персистит отложенно); OnSliderCaptureEnd — персист по отпусканию ручки (мышь) или Enter/потере фокуса.
	TFunction<void(float, bool)> SliderCallback;
	TFunction<void()> SliderReleaseCallback;
	bool bSliderCaptured = false;   // true между CaptureBegin и CaptureEnd (мышь зажата / Enter-захват клавиатуры)

	UFUNCTION()
	void OnSliderValue(float Value) { if (SliderCallback) { SliderCallback(Value, bSliderCaptured); } }

	UFUNCTION()
	void OnSliderCaptureBegin() { bSliderCaptured = true; }

	UFUNCTION()
	void OnSliderCaptureEnd() { bSliderCaptured = false; if (SliderReleaseCallback) { SliderReleaseCallback(); } }
};

// ★ Тот же мостик для текстовых полей: `UEditableText::OnTextChanged` — динамический делегат,
//   ему нужен UObject-приёмник. Используется жёстким стопом набора в диалоге ввода 1580
//   (эталон ставит настоящему Win32-полю EM_LIMITTEXT, `ctrlEditSetTextLength`, ui_core.c:116681).
UCLASS()
class ASDAREBORN_API UAsdaTextLimiter : public UObject
{
	GENERATED_BODY()
public:
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.RelayAnchorPrune`): поле ввода, чьи делегаты держат ограничитель (слабо) — как у
	//   `UAsdaButtonRelay`: ограничители лежат в тех же якорях окон, и якорь выбрасывает ограничитель уничтоженного поля.
	TWeakObjectPtr<UObject> BoundWidget;
	bool bHasBoundWidget = false;   // поле было задано (без него якорь ограничитель не трогает)
	// Ограничитель мёртв: его поле было задано и уже уничтожено.
	bool HasDeadBinding() const { return bHasBoundWidget && !BoundWidget.IsValid(); }
	// Запомнить поле, чьи делегаты держат ограничитель.
	void SetBoundWidget(UObject* W) { BoundWidget = W; bHasBoundWidget = (W != nullptr); }

	TFunction<void(const FText&)> Callback;
	// ★★★ СПОСОБ ПОДТВЕРЖДЕНИЯ ОБЯЗАН ДОХОДИТЬ ДО ПОДПИСЧИКА.
	//   `OnTextCommitted` в Slate стреляет НЕ ТОЛЬКО по Enter: `OnUserMovedFocus` (потеря фокуса),
	//   `OnCleared` (Esc), `Default`. Пока способ отбрасывался, диалог ввода 1580 закрывался «как
	//   по ОК» с ПУСТЫМ полем в тот же кадр, в котором открывался: `RefreshModalInput` уводит
	//   фокус на КОРЕНЬ модального окна ⇒ поле теряет фокус ⇒ commit(OnUserMovedFocus) ⇒
	//   `CloseInputDialog(true)` ⇒ пустое имя ⇒ красное «Заголовок вкладки не может…».
	//   Именно это юзер видел как «никаких окон не вылазит, только надпись».
	TFunction<void(const FText&, ETextCommit::Type)> CommitCallback;

	UFUNCTION()
	void OnTextChanged(const FText& Text) { if (Callback) { Callback(Text); } }

	UFUNCTION()
	void OnTextCommitted(const FText& Text, ETextCommit::Type Method)
	{
		if (CommitCallback) { CommitCallback(Text, Method); return; }
		if (Callback) { Callback(Text); }
	}
};
