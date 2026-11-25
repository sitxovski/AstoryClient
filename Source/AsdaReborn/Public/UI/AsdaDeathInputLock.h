// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 05.11.2026 [ОРИГИНАЛ] ЗАМОК ВВОДА МЁРТВОГО — общая точка флага эталона `CUIManager+0x44`.
//   Баг владельца 05.11: «пока персонаж мёртв, горячие клавиши действий и кнопки меню работают».
//   Порт знал про флаг (комментарии `AsdaSystemMenuSubsystem.cpp`, ESC-гейт `AsdaSystemOptionSubsystem.cpp`),
//   но кроме ESC нигде его не применял: в реестре отложенного он числился пунктом «ESC и модалки».
//
//   ЭТАЛОН. `DeathMenu_AddOption@0xD6D060` (ui_core.c, на КАЖДОЙ добавленной опции меню смерти) зовёт
//   `setField_p17(g_pUIManager, 1)` (сеттер 0x966700: `mov [ecx+0x44], arg`); снимает `DeathMenu_OnSelect@0xD6DE70`
//   и ветка ОК окна 2370 (`case 0x942`). Читатель — `sub_9323E3` (0x9865A0: `mov eax, [ecx+0x44]`). Места чтения:
//     • `MainWndProc@0x9841A0` game_main.c:6225 (диспетч раскладки на отпускании): `+0x44 == 1 && !dword_2C42C28` →
//       `Input_OnKeyDown_Dispatch(…, a4 = 44)` = исполняется ТОЛЬКО команда 44 (SCREEN SHOT); `dword_2C42C28` в срезе
//       никто не пишет (ссылок в срезе нет) ⇒ ветка всегда активна;
//     • там же :6140 (нажатие: флаги хода) и :6300/:6316 (Alt-сочетания) — при флаге диспетча нет вовсе;
//     • `inputHandleInventoryAndEscHotkeys@0xC6E710` input.c:1731 — ESC (в порту уже есть, `AsdaSystemOptionSubsystem`);
//     • `Hotbar_SlotActivate@0xDEDDD0` ui_core.c:67351 — слот панели быстрого доступа (мышь И клавиша), молча;
//     • `uiHotkey_DispatchCommand@0xE8F670` ui_core.c:79464 — ОТКРЫТИЕ окон кнопками системного меню: проходит только
//       73406 (вкладка системы). Закрытие (`uiMainMenu_OnCommand@0xE8F880`) флаг НЕ проверяет.
//   Чат (Enter → `GameInput_DispatchEvent`) флагом не гейтится — мёртвый пишет в чат, как в оригинале.
//
//   ПОРТ. Флаг = «меню смерти открыто» (`UAsdaAfterDeathSubsystem::IsDeathMenuOpen`, тот же бит 0x4000/+0x44 эталона).
//   Отличие порта: меню в порту закрывается по ответу сервера 4055/4056, а не в момент выбора опции (у эталона
//   `DeathMenu_OnSelect` снимает флаг сразу). Поэтому замок держится до воскрешения — мёртвый ничего не делает и
//   в промежутке «выбрал → сервер ответил» (сервер сам отбивает действия мёртвого кодом 6001).
//   Ключ: `asda.Death.InputLock` (1 = как оригинал; 0 = прежний порт, гейт снят везде, кроме ESC).
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaDeathLock
{
	// Команда раскладки, которую эталон пропускает мёртвому (`a4 = 44`, game_main.c:6227): SCREEN SHOT.
	constexpr int32 kAllowedCmd = 44;
	// Команда раскладки порта «строка чата» (CHAT OPEN CLOSE). У эталона Enter → чат идёт мимо раскладки
	//   (`GameInput_DispatchEvent@0xC6DCA0`, game_main.c:9258, гейт только модалки) ⇒ мёртвому чат доступен.
	constexpr int32 kAllowedChatCmd = 66;
	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] (баг владельца «мёртвый не может печатать в чат»): остальные клавиши открытия строки — 67 NORMAL,
	//   68 SHOUT, 69 ANSWER (R), 70 WHISPER, 71 PARTY, 72 CLAN, 73 SOULMATE. У эталона они в `hotkeyDispatchAction` и под `a4 = 44`
	//   молчат (input.c:4400+), но решение владельца — мёртвому чат работает ПОЛНОСТЬЮ. Ключ `asda.Death.ChatKeys`.
	constexpr int32 kChatCmdFirst = 67;
	constexpr int32 kChatCmdLast = 73;
	// Кнопка системного меню, которая открывается и у мёртвого (`uiHotkey_DispatchCommand`, ui_core.c:79466): вкладка системы.
	constexpr int32 kAllowedSysMenuButton = 73406;

	// true = меню смерти открыто И ключ `asda.Death.InputLock` != 0: ввод заперт по эталону `CUIManager+0x44`.
	ASDAREBORN_API bool IsInputLocked(const UGameInstance* GI);

	// Значение ключа `asda.Death.InputLock` (аксессор: имя ключа не должно попадать в рабочий код других .cpp).
	ASDAREBORN_API bool LockEnabled();

	// Команда раскладки, которую замок пропускает мёртвому: снимок экрана, строка чата и (по ключу `asda.Death.ChatKeys`) клавиши каналов.
	ASDAREBORN_API bool IsAllowedCmd(int32 Cmd);
}
