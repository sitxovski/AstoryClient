// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEnterGuard — пометка «Enter это согласие НЕ подтверждает» для окон-вопросов. [ИСПРАВЛЕНИЕ] 25.10.2026
//   (ревизия `Reports/wave_1025c_review/REVIEW.md`, ВАЖНО-3/4; механизм — `UAsdaEnterConfirmSubsystem`).
//   Правило: Enter жмёт только безвредное и обратимое согласие (информационное «ОК», закрытие, подтверждение ввода). Вопросы о
//   необратимом или о трате денег/предметов и ВСЕ вопросы, пришедшие от сервера (обмен, клан, друг, отряд, соулмейт, призыв),
//   подтверждаются только щелчком.
//   Применение у вызывающего: `AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(...)` — следующий показ окна-вопроса
//   (`ShowGameConfirm`/`ShowGameMessage`/`ShowGiftMsgBox` → `FAsdaMsgBoxEntry::bNoEnter`; `ShowConfirm` MESSAGE2) получает пометку.
//   Пометка одноразовая: её снимает первый же показ. Заголовок лёгкий — без зависимостей от оконной подсистемы.
#pragma once

#include "CoreMinimal.h"

namespace AsdaEnterGuard
{
	// Взвести пометку «без Enter» для СЛЕДУЮЩЕГО показа окна-вопроса.
	ASDAREBORN_API void ArmNextNoEnter();
	// Снять пометку и вернуть её (зовут точки показа окон-вопросов в начале показа).
	ASDAREBORN_API bool ConsumeNextNoEnter();
	// MESSAGE2 (`ShowConfirm`) показан: запомнить пометку и сменить номер показа (окно одно на все подтверждения).
	ASDAREBORN_API void NoteConfirmShown(bool bNoEnter);
	// Пометка текущего показа MESSAGE2.
	ASDAREBORN_API bool IsConfirmNoEnter();
	// Номер текущего показа MESSAGE2 (растёт с каждым показом) — Enter сверяет его на отпускании.
	ASDAREBORN_API uint32 GetConfirmSerial();

	// Пометить следующий показ и вернуть тот же указатель — для записи в одно выражение `NoEnter(WM)->ShowGameConfirm(...)`.
	template<typename T>
	T* NoEnter(T* WindowSubsystem)
	{
		ArmNextNoEnter();
		return WindowSubsystem;
	}
}
