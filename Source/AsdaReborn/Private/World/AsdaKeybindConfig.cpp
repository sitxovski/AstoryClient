// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaKeybindConfig — см. AsdaKeybindConfig.h.
//   Все три таблицы ниже сняты ПРЯМЫМ ДИЗАССЕМБЛИРОВАНИЕМ .text клиента (сборка 13 344 768 б),
//   а не по декомпилу: `CKeyConfig_BuildDefaultBindings@0xE34730` разваливается в Hex-Rays
//   («bad sp value»), и целочисленные ключи map<VK,string> в .c — мусор.
//   Отчёт с методикой и полными таблицами: `Reports/keybinds_shortcutkey.md`.
#include "World/AsdaKeybindConfig.h"
#include "GameFramework/InputSettings.h"
#include "World/AsdaKeybindProfile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerInput.h"   // FInputActionKeyMapping — снимок ActionMappings песочницы самотестов
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR — ключи отката правок 25.10.2026

DEFINE_LOG_CATEGORY_STATIC(LogAsdaKeys, Log, All);

// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] Подбор (56) и прыжок (95) не делят клавишу: дефолт подбора у порта — F (Пробел — прыжок, решение
//   владельца), назначение игроком снимает конфликт и с прыжком, старый файл «оба на Пробеле» чинится после загрузки.
//   0 — прежнее: подбор по умолчанию Пробел (эталон), прыжок конфликтов не снимает.
static ASDA_DEV_CVAR(int32, CVarAsdaKeysJumpPickupSplit,
	TEXT("asda.Keys.JumpPickupSplit"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — «подобрать» по умолчанию F (Пробел — прыжок), назначение игроком снимает клавишу и с прыжка, ")
	TEXT("старый файл «подбор и прыжок на Пробеле» → подбор на F; 0 — прежнее (подбор на Пробеле вместе с прыжком)."),
	ECVF_Default);
// ★ 25.10.2026 [ОРИГИНАЛ] Захват клавиши в окне настроек файл НЕ пишет — пишет «ОК» (`optionsWnd_ApplyAndSave@0xED5800`).
//   0 — прежнее: каждый захват сразу писал `Shortcutkey.ini` (и «Отмена» не отменяла назначение после перезапуска).
static ASDA_DEV_CVAR(int32, CVarAsdaKeysSaveOnOkOnly,
	TEXT("asda.Keys.SaveOnOkOnly"), 1,
	TEXT("[ОРИГИНАЛ] 1 — захват клавиши в окне настроек файл раскладки не пишет (пишет «ОК», как эталон); 0 — писать сразу (прежнее)."),
	ECVF_Default);
// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] Слой ActionMappings движка не умеет PrintScreen (FKey нет) и сохранял «SCREEN SHOT» как Key=None —
//   после первого «ОК» и перезапуска клавиша скриншота пропадала. 1 — непредставимый в FKey бинд файла слой None не снимает.
static ASDA_DEV_CVAR(int32, CVarAsdaKeysKeepUnmappable,
	TEXT("asda.Keys.KeepUnmappableOnLoad"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — клавиша без FKey (PrtSc) из Shortcutkey.ini не затирается пустым ActionMapping при загрузке; 0 — прежнее."),
	ECVF_Default);
// ★ 26.10.2026 (ревизия wave_1025d п. 7) [ПОРТ] Предохранитель файла раскладки владельца: в самотест-запуске (`IsSelfTestRun`) без
//   песочницы (`-AsdaShortcutkeyIni=` / `SetTestIniOverride`) `ResolveShortcutkeyIniPath` уводит файл в Saved. 0 — прежнее.
static ASDA_DEV_CVAR(int32, CVarAsdaKeysSelfTestIniGuard,
	TEXT("asda.Keys.SelfTestIniGuard"), 1,
	TEXT("[ПОРТ] 1 — в самотест-запуске без песочницы раскладки файл Shortcutkey.ini уводится в Saved/AsdaSelfTest (копия файла ")
	TEXT("владельца), файл владельца не пишется; 0 — прежнее (самотест пишет system/Shortcutkey.ini)."),
	ECVF_Default);
// ★ 26.10.2026 (ревизия wave_1025d 2.2) [ИСПРАВЛЕНИЕ] Миграция «подбор и прыжок на Пробеле» при занятой F оставляет подбор без клавиши —
//   теперь не молча: один раз за процесс нотис в чат (показывает окно настроек, `UAsdaSystemOptionSubsystem::Tick`). 0 — прежнее (молча).
static ASDA_DEV_CVAR(int32, CVarAsdaKeysPickupUnboundNotice,
	TEXT("asda.Keys.PickupUnboundNotice"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — если перенос подбора с Пробела на F не удался (F занята), игрок получает нотис в чат (один раз за запуск); ")
	TEXT("0 — прежнее: подбор молча остаётся без клавиши."),
	ECVF_Default);

namespace
{
	// Песочница файла раскладки для самотеста (`SetTestIniOverride`); пусто — нет.
	FString GAsdaKeysTestIniOverride;
	// ★ 26.10.2026: самотест допущен офлайн-гейтом в этом процессе (`NoteSelfTestAdmitted`).
	bool GAsdaKeysSelfTestAdmitted = false;
	// ★ 26.10.2026: предохранитель уже сообщил о песочнице (строка журнала — один раз).
	bool GAsdaKeysGuardLogged = false;
	// ★ 26.10.2026: команда, держащая F, когда миграция оставила подбор без клавиши (0 — нет); снимает `ConsumePickupUnboundNotice`.
	int32 GAsdaKeysPickupUnboundOwner = 0;
	// Дефолт подбора у порта (VK 'F') при `asda.Keys.JumpPickupSplit 1`.
	constexpr int32 kPortPickupVk = 70;
	constexpr int32 kCmdGetItem = 56;   // «ACTION GET ITEM»
	constexpr int32 kCmdJump = 95;      // «EXT JUMP»
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ТАБЛИЦА 1 — хардкод-дефолты `keybindLoadDefaults@0xE3A9C0` (85 записей, cmd 1..85).
//  Дизасм-шаблон: `push <mod>; push <vk>; call CPair_Set(0xE39C50)` → `push <cmd>; call SetBind(0xE3B660)`.
// ════════════════════════════════════════════════════════════════════════════════════════════════
namespace
{
	struct FDef { int32 Cmd; int32 Vk; uint8 Mod; };
	static const FDef GDefaults[] = {
		{1,112,1},{2,113,1},{3,114,1},{4,115,1},{5,116,1},        // Shift+F1..F5
		{6,49,0},{7,50,0},{8,51,0},{9,52,0},{10,53,0},{11,54,0},  // 1..6
		{12,55,0},{13,56,0},{14,57,0},{15,48,0},                  // 7..9, 0
		{16,189,0},{17,187,0},                                     // -, =
		{18,112,0},{19,113,0},{20,114,0},{21,115,0},{22,116,0},   // F1..F5
		{23,117,0},{24,118,0},{25,119,0},{26,120,0},{27,121,0},   // F6..F10
		{28,122,0},{29,123,0},                                     // F11, F12
		{30,67,0},{31,69,0},{32,75,0},{33,73,0},{34,81,0},{35,85,0}, // C E K I Q U
		{36,74,0},{37,77,0},{38,84,0},{39,220,0},{40,86,0},{41,90,0},// J M T \ V Z
		{42,71,0},{43,80,0},{44,44,0},{45,45,0},{46,36,0},{47,33,0}, // G P PrtSc Ins Home PgUp
		{48,0,0},{49,0,0},{50,0,0},
		{51,87,0},{52,83,0},{53,65,0},{54,68,0},                  // W S A D (движение)
		{55,0,0},{56,32,0},{57,78,0},{58,66,0},{59,192,0},{60,9,0},// Space N B ` Tab
		{61,0,0},{62,9,1},{63,0,0},{64,0,0},                       // Shift+Tab
		{65,89,0},{66,13,0},                                       // Y Enter
		{67,78,2},{68,89,2},{69,82,0},{70,87,2},{71,80,2},{72,67,2},// Ctrl+N Ctrl+Y R Ctrl+W Ctrl+P Ctrl+C
		{73,83,2},{74,72,0},                                       // Ctrl+S H
		{75,0,0},{76,0,0},{77,0,0},{78,0,0},{79,0,0},{80,0,0},
		{81,0,0},{82,0,0},{83,0,0},{84,0,0},{85,0,0},
	};

	// ════════════════════════════════════════════════════════════════════════════════════════════
	//  ТАБЛИЦА 2 — лейблы действий `UI_BuildKeybindLabels@0xE38B30`, actionId 0..85 (86 шт).
	//  ★ ЛЕЙБЛ ВКЛЮЧАЕТ ДВОЕТОЧИЕ — по нему идёт `str_StartsWith(строка_ini, лейбл)`, а значение
	//    читается с позиции strlen(лейбл). Индекс массива == actionId.
	// ════════════════════════════════════════════════════════════════════════════════════════════
	static const TCHAR* const GActionLabels[] = {
		TEXT("NONE"),
		TEXT("PAGE 1:"), TEXT("PAGE 2:"), TEXT("PAGE 3:"), TEXT("PAGE 4:"), TEXT("PAGE 5:"),
		TEXT("BAR SLOT 1:"), TEXT("BAR SLOT 2:"), TEXT("BAR SLOT 3:"), TEXT("BAR SLOT 4:"),
		TEXT("BAR SLOT 5:"), TEXT("BAR SLOT 6:"), TEXT("BAR SLOT 7:"), TEXT("BAR SLOT 8:"),
		TEXT("BAR SLOT 9:"), TEXT("BAR SLOT 10:"), TEXT("BAR SLOT 11:"), TEXT("BAR SLOT 12:"),
		TEXT("NEW BAR SLOT 1:"), TEXT("NEW BAR SLOT 2:"), TEXT("NEW BAR SLOT 3:"), TEXT("NEW BAR SLOT 4:"),
		TEXT("NEW BAR SLOT 5:"), TEXT("NEW BAR SLOT 6:"), TEXT("NEW BAR SLOT 7:"), TEXT("NEW BAR SLOT 8:"),
		TEXT("NEW BAR SLOT 9:"), TEXT("NEW BAR SLOT 10:"), TEXT("NEW BAR SLOT 11:"), TEXT("NEW BAR SLOT 12:"),
		TEXT("INTERFACE CHARACTOER:"), TEXT("INTERFACE EMOTION:"), TEXT("INTERFACE SKILL:"),
		TEXT("INTERFACE INVENTORY:"), TEXT("INTERFACE QUEST:"), TEXT("INTERFACE COMMUNITY:"),
		TEXT("INTERFACE WHO:"), TEXT("INTERFACE MAP:"), TEXT("INTERFACE TITLE:"),
		TEXT("INTERFACE PREMIUM:"), TEXT("INTERFACE DISASSEMBLY:"), TEXT("INTERFACE REFINE:"),
		TEXT("INTERFACE UPGRADE:"), TEXT("INTERFACE PETBAG:"), TEXT("SCREEN SHOT:"),
		TEXT("SOUL GUARDIAN SKILL Lv1:"), TEXT("SOUL GUARDIAN SKILL Lv2:"), TEXT("SOUL GUARDIAN SKILL Lv3:"),
		TEXT("PARTY INVITE:"), TEXT("PARTY LEAVE:"), TEXT("PARTY EXILE:"),
		TEXT("MOVE_W:"), TEXT("MOVE_S:"), TEXT("MOVE_A:"), TEXT("MOVE_D:"),
		TEXT("ACTION SIT DOWN UP:"), TEXT("ACTION GET ITEM:"), TEXT("ACTION EXCHANGE NORMAL:"),
		TEXT("ACTION EXCHANGE PREMIUM:"), TEXT("ACTION ATTACK:"), TEXT("ACTION TARGET NEAR_ENEMY:"),
		TEXT("ACTION VIEW OTHER INFO:"), TEXT("ACTION SELETE MYSELF:"), TEXT("ACTION TARGET SHARE:"),
		TEXT("ACTION TARGET SHARE NEXT:"), TEXT("ACTION PRIVATESHOP:"),
		TEXT("CHAT OPEN CLOSE:"), TEXT("CHAT NORMAL:"), TEXT("CHAT SHOUT:"), TEXT("CHAT ANSWER:"),
		TEXT("CHAT WHISPER:"), TEXT("CHAT PARTY:"), TEXT("CHAT CLAN:"), TEXT("CHAT SOULMATE:"),
		TEXT("EMOTION BOW:"), TEXT("EMOTION THANK:"), TEXT("EMOTION ANGER:"), TEXT("EMOTION SAD:"),
		TEXT("EMOTION HAPPY:"), TEXT("EMOTION FOLLOW:"), TEXT("EMOTION DUEL:"), TEXT("EMOTION DANCE1:"),
		TEXT("EMOTION DANCE2:"), TEXT("EMOTION COURTSHIP:"), TEXT("EMOTION JUGGLING:"),
		TEXT("EMOTION UPPERARM:"),
	};
	static_assert(UE_ARRAY_COUNT(GActionLabels) == 86, "actionId 0..85 — ровно 86 лейблов (UI_BuildKeybindLabels)");

	// ════════════════════════════════════════════════════════════════════════════════════════════
	//  ТАБЛИЦА 2b — ★ НАШИ РАСШИРЕНИЯ (actionId 86+). В ОРИГИНАЛЕ ИХ НЕТ.
	//  Зачем: в порту появились свои действия, клавиши которых были ЗАШИТЫ В КОД, и владелец не мог
	//  их переназначить без пересборки (требование 2026-08-29 «данные из файла, а не из кода»).
	//  Диапазон 1..85 остаётся ПОБАЙТОВО эталонным (static_assert выше) — расширения идут СТРОГО
	//  ПОСЛЕ него и ОТДЕЛЬНЫМ массивом, чтобы эталонную нумерацию нельзя было сдвинуть.
	//  ★ Префикс «EXT » в лейбле — признак «этого в оригинале не было».
	//  ⚠ ЧЕГО ЗДЕСЬ НЕТ И ПОЧЕМУ: комбинация админ-режима CTRL+SHIFT+ALT+Q (ТРИ модификатора —
	//    формат оригинала выражает только ОДИН), клавиша ESC и КНОПКИ МЫШИ. Ни ESC, ни мыши нет в
	//    эталонной таблице 74 биндабельных клавиш `CKeyConfig_BuildDefaultBindings@0xE34730`,
	//    поэтому забиндить их нельзя было и в оригинале. Они остаются фиксированными; перечень —
	//    в `Reports/keybinds_all_actions.md` и в реестре отложенного.
	// ════════════════════════════════════════════════════════════════════════════════════════════
	static const TCHAR* const GExtActionLabels[] = {
		TEXT("EXT FREECAM TOGGLE:"),     // 86 — свободная камера админа (AsdaAdminSubsystem.cpp:244)
		TEXT("EXT FREECAM UP:"),         // 87 — подъём камеры            (:663)
		TEXT("EXT FREECAM UP ALT:"),     // 88 — подъём камеры, 2-я клавиша (:663)
		TEXT("EXT FREECAM DOWN:"),       // 89 — спуск камеры             (:664)
		TEXT("EXT FREECAM DOWN ALT:"),   // 90 — спуск камеры, 2-я клавиша  (:664)
		TEXT("EXT DEV MENU:"),           // 91 — dev-меню (AsdaDevMenuSubsystem.cpp:88)
		TEXT("EXT RIDE EXIT:"),          // 92 — выйти из транспорта-пасхалки (AsdaVehicleRideSubsystem.cpp:215)
		TEXT("EXT RIDE LIGHT:"),         // 93 — фары транспорта              (:220)
		TEXT("EXT RIDE HANDBRAKE:"),     // 94 — ручник транспорта            (:306)
		// ★ 23.10.2026: прыжок из донора AsdaFableClassic (Asda2.exe `0x40E280`: GetAsyncKeyState(VK_SPACE), без таблицы
		//   клавиш). У нас — переназначаемое расширение; опрашивает `AAsdaPlayerCharacter::TickSwimAndJump` по коду 95.
		//   Пробел у эталона занят «ACTION GET ITEM» (56); ★ 25.10.2026 у 56 есть обработчик, и порт переносит её дефолт на F,
		//   как донор (`LoadDefaults`, ключ `asda.Keys.JumpPickupSplit`); назначение игроком разводит 56 и 95 (`SetBind`).
		TEXT("EXT JUMP:"),               // 95 — прыжок (AsdaPlayerCharacter_Move.cpp, TickSwimAndJump)
	};
	static constexpr int32 kExtFirstCmd = 86;
	static constexpr int32 kExtLastCmd  = kExtFirstCmd + (int32)UE_ARRAY_COUNT(GExtActionLabels) - 1;

	// Дефолты расширений = ровно те клавиши, что были зашиты в код до этой правки.
	static const FDef GExtDefaults[] = {
		{86,123,0},{87,32,0},{88,69,0},{89,67,0},{90,81,0},
		{91,78,2},{92,123,0},{93,76,0},{94,32,0},
		{95,32,0},   // ★ 23.10.2026: прыжок — Пробел (как у донора)
	};
	static_assert(UE_ARRAY_COUNT(GExtDefaults) == UE_ARRAY_COUNT(GExtActionLabels),
		"у каждого расширения должен быть дефолт");

	// ════════════════════════════════════════════════════════════════════════════════════════════
	//  ТАБЛИЦА 3 — VK ↔ имя клавиши `CKeyConfig_BuildDefaultBindings@0xE34730` (74 записи).
	//  ★ ЭТО ПОЛНЫЙ СПИСОК БИНДАБЕЛЬНЫХ КЛАВИШ ОРИГИНАЛА. Чего в нём НЕТ — забиндить нельзя
	//    (нет DELETE/END/PGDN, стрелок кроме четырёх, ESC, BACKSPACE, отдельного нумпада, F13+).
	//    Порядок записей — ровно порядок вставки в бинаре.
	//  ★ Ключи 16/17/18 (SHIFT/CTRL/ALT) служат префиксами модификатора: `cfgParsePrefixedOption`
	//    сравнивает часть строки до «+» именно с ними → mod 1/2/3.
	// ════════════════════════════════════════════════════════════════════════════════════════════
	struct FKeyName { int32 Vk; const TCHAR* Name; };
	static const FKeyName GKeyNames[] = {
		{0,TEXT("NONE")},
		{112,TEXT("F1")},{113,TEXT("F2")},{114,TEXT("F3")},{115,TEXT("F4")},{116,TEXT("F5")},{117,TEXT("F6")},
		{118,TEXT("F7")},{119,TEXT("F8")},{120,TEXT("F9")},{121,TEXT("F10")},{122,TEXT("F11")},{123,TEXT("F12")},
		{65,TEXT("A")},{66,TEXT("B")},{67,TEXT("C")},{68,TEXT("D")},{69,TEXT("E")},{70,TEXT("F")},{71,TEXT("G")},
		{72,TEXT("H")},{73,TEXT("I")},{74,TEXT("J")},{75,TEXT("K")},{76,TEXT("L")},{77,TEXT("M")},{78,TEXT("N")},
		{79,TEXT("O")},{80,TEXT("P")},{81,TEXT("Q")},{82,TEXT("R")},{83,TEXT("S")},{84,TEXT("T")},{85,TEXT("U")},
		{86,TEXT("V")},{87,TEXT("W")},{88,TEXT("X")},{89,TEXT("Y")},{90,TEXT("Z")},
		{48,TEXT("0")},{49,TEXT("1")},{50,TEXT("2")},{51,TEXT("3")},{52,TEXT("4")},
		{53,TEXT("5")},{54,TEXT("6")},{55,TEXT("7")},{56,TEXT("8")},{57,TEXT("9")},
		{9,TEXT("TAB")},{13,TEXT("ENTER")},{32,TEXT("SPACE")},
		{16,TEXT("SHIFT")},{17,TEXT("CTRL")},{18,TEXT("ALT")},
		{44,TEXT("PrtSc")},
		{192,TEXT("`")},{189,TEXT("-")},{187,TEXT("=")},{220,TEXT("\\")},{219,TEXT("[")},{221,TEXT("]")},
		{186,TEXT(";")},{222,TEXT("'")},{188,TEXT(",")},{190,TEXT(".")},{191,TEXT("/")},
		{38,TEXT("UP")},{40,TEXT("DOWN")},{37,TEXT("LEFT")},{39,TEXT("RIGHT")},
		{45,TEXT("INSERT")},{36,TEXT("HOME")},{33,TEXT("PGUP")},
	};
	static_assert(UE_ARRAY_COUNT(GKeyNames) == 74, "CKeyConfig_BuildDefaultBindings — ровно 74 клавиши");

	// Имена модификаторов = имена VK 16/17/18 из таблицы 3 (эталон берёт их оттуда же).
	static const TCHAR* ModName(EAsdaKeyMod M)
	{
		switch (M)
		{
		case EAsdaKeyMod::Shift: return TEXT("SHIFT");
		case EAsdaKeyMod::Ctrl:  return TEXT("CTRL");
		case EAsdaKeyMod::Alt:   return TEXT("ALT");
		default: return nullptr;
		}
	}
}

// ── Справочники ─────────────────────────────────────────────────────────────────────────────────

const TCHAR* UAsdaKeybindConfig::GetActionLabel(int32 Cmd)
{
	if (Cmd >= 0 && Cmd < (int32)UE_ARRAY_COUNT(GActionLabels)) { return GActionLabels[Cmd]; }
	// ★ Наши расширения (86+) — отдельным массивом, чтобы эталонная нумерация 0..85 не сдвигалась.
	if (Cmd >= kExtFirstCmd && Cmd <= kExtLastCmd) { return GExtActionLabels[Cmd - kExtFirstCmd]; }
	return nullptr;
}

int32 UAsdaKeybindConfig::GetLastActionCmd() { return kExtLastCmd; }

bool UAsdaKeybindConfig::IsExtensionCmd(int32 Cmd) { return Cmd >= kExtFirstCmd && Cmd <= kExtLastCmd; }

FString UAsdaKeybindConfig::GetCmdName(int32 Cmd)
{
	if (const TCHAR* L = GetActionLabel(Cmd))
	{
		FString S(L);
		S.RemoveFromEnd(TEXT(":"));
		return S;
	}
	return FString::Printf(TEXT("CMD %d"), Cmd);
}

const TCHAR* UAsdaKeybindConfig::VkToKeyName(int32 Vk)
{
	for (const FKeyName& K : GKeyNames) { if (K.Vk == Vk) { return K.Name; } }
	return nullptr;
}

int32 UAsdaKeybindConfig::KeyNameToVk(const FString& Name)
{
	// Эталон `StringMap_FindValueByName` = std::string::compare → РЕГИСТРОЗАВИСИМО
	//   (в ini поэтому именно «PrtSc», а не «PRTSC»).
	for (const FKeyName& K : GKeyNames) { if (Name.Equals(K.Name, ESearchCase::CaseSensitive)) { return K.Vk; } }
	return 0;
}

FString UAsdaKeybindConfig::FormatBind(const FAsdaKeyBind& B)
{
	// Эталон `keybindSaveShortcutIni`: не назначено → лейбл действия 0, т.е. «NONE»;
	//   иначе [MOD+]имя_клавиши.
	if (B.Vk == 0) { return TEXT("NONE"); }
	const TCHAR* KeyName = VkToKeyName(B.Vk);
	if (!KeyName) { return TEXT("NONE"); }
	if (const TCHAR* M = ModName(B.Mod)) { return FString::Printf(TEXT("%s+%s"), M, KeyName); }
	return FString(KeyName);
}

bool UAsdaKeybindConfig::ParseBindToken(const FString& Token, FAsdaKeyBind& Out)
{
	// Порт `cfgParsePrefixedOption@0xE3C0B0`: ищем ПЕРВЫЙ '+'; префикс до него сравниваем с именами
	//   VK 16/17/18 → mod 1/2/3, остаток → VK. Если префикс не модификатор — весь токен как имя клавиши
	//   (так «+» сам по себе клавишей быть не может — его в таблице 74 и нет).
	Out = FAsdaKeyBind();
	if (Token.IsEmpty()) { return false; }

	int32 PlusIdx = INDEX_NONE;
	if (Token.FindChar(TEXT('+'), PlusIdx) && PlusIdx > 0)
	{
		const FString Prefix = Token.Left(PlusIdx);
		EAsdaKeyMod M = EAsdaKeyMod::None;
		if (Prefix.Equals(TEXT("SHIFT"), ESearchCase::CaseSensitive))     { M = EAsdaKeyMod::Shift; }
		else if (Prefix.Equals(TEXT("CTRL"), ESearchCase::CaseSensitive)) { M = EAsdaKeyMod::Ctrl; }
		else if (Prefix.Equals(TEXT("ALT"), ESearchCase::CaseSensitive))  { M = EAsdaKeyMod::Alt; }

		if (M != EAsdaKeyMod::None)
		{
			// ★ ДЕФЕКТ ОРИГИНАЛА (исправлен): при токене вида «SHIFT+» (модификатор без клавиши)
			//   эталон не заходит в ветку `++Count < v10` и возвращает НЕИНИЦИАЛИЗИРОВАННЫЙ VK со
			//   стека. Здесь такой токен честно считается нераспознанным → «не назначено».
			if (PlusIdx + 1 >= Token.Len()) { return false; }
			const int32 Vk = KeyNameToVk(Token.Mid(PlusIdx + 1));
			if (Vk == 0) { return false; }
			Out.Vk = Vk; Out.Mod = M;
			return true;
		}
	}

	const int32 Vk = KeyNameToVk(Token);
	if (Vk == 0) { return false; }        // включая «NONE» (VK 0) — не назначено
	Out.Vk = Vk; Out.Mod = EAsdaKeyMod::None;
	return true;
}

int32 UAsdaKeybindConfig::NormalizeVk(int32 Vk)
{
	// Эталон `Keybind_NormalizeVK@0xE3CB60`.
	switch (Vk)
	{
	case 13: case 16: case 17: case 18: return 0;    // Enter / Shift / Ctrl / Alt — не диспетчеризуются
	default: break;
	}
	if (Vk >= 96 && Vk <= 105) { return Vk - 48; }   // нумпад 0..9 → цифровой ряд 48..57
	return Vk;
}

EAsdaKeyMod UAsdaKeybindConfig::CurrentModifier(bool bShift, bool bCtrl, bool bAlt)
{
	// Эталонный приоритет Keybind_ResolveModifier: Shift → Ctrl → Alt → None.
	if (bShift) return EAsdaKeyMod::Shift;
	if (bCtrl)  return EAsdaKeyMod::Ctrl;
	if (bAlt)   return EAsdaKeyMod::Alt;
	return EAsdaKeyMod::None;
}

// ── Загрузка раскладки ──────────────────────────────────────────────────────────────────────────

void UAsdaKeybindConfig::LoadDefaults()
{
	Binds.Reset();
	for (const FDef& D : GDefaults)
	{
		FAsdaKeyBind B; B.Vk = D.Vk; B.Mod = (EAsdaKeyMod)D.Mod;
		Binds.Add(D.Cmd, B);
	}
	// ★ Дефолты НАШИХ расширений (86+) — те же клавиши, что были зашиты в код.
	for (const FDef& D : GExtDefaults)
	{
		FAsdaKeyBind B; B.Vk = D.Vk; B.Mod = (EAsdaKeyMod)D.Mod;
		Binds.Add(D.Cmd, B);
	}
	// ★ 25.10.2026 [ПОРТ] (ключ `asda.Keys.JumpPickupSplit`): эталонная таблица выше не тронута (Пробел у 56), но у порта Пробел —
	//   прыжок (95); подбор по умолчанию — F, как у донора AsdaFableClassic (его `system/Shortcutkey.ini`: «ACTION GET ITEM: F»).
	//   F в эталонной таблице не занята никем.
	if (CVarAsdaKeysJumpPickupSplit.GetValueOnGameThread() != 0)
	{
		Binds.Add(kCmdGetItem, FAsdaKeyBind{ kPortPickupVk, EAsdaKeyMod::None });
	}
	Source = EAsdaKeybindSource::Defaults;
	SourcePath.Reset();
}

// ══════════ ★★★ ШТАТНЫЙ КОНФИГ ДВИЖКА: UInputSettings::ActionMappings ═══════════════════════════
//   ПОЧЕМУ ИМЕННО ОН (установлено фактом, а не предположением):
//     · в `Content` НЕТ ни одного `InputAction`/`InputMappingContext`;
//     · ни `AsdaReborn.uproject`, ни `*.Build.cs` не подключают модуль `EnhancedInput`;
//     · ввод заведён напрямую: `InputComponent->BindKey(EKeys::…)` в
//       `AAsdaPlayerController::SetupInputComponent` + опрос клавиш в `PollKeys`.
//   ⇒ проект на LEGACY-вводе, и штатное место клавиш — `ActionMappings`
//     (`Config/DefaultInput.ini`, секция `[/Script/Engine.InputSettings]`).
//     Строки `DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput` в этом ini —
//     СТОКОВЫЕ УМОЛЧАНИЯ UE5, они не означают, что проект использует Enhanced Input.
//   Пользовательские переназначения движок сохраняет сам — `UInputSettings::SaveKeyMappings()`
//   пишет их в `Saved/Config/<Платформа>/Input.ini`. Своего формата мы не заводим.

FName UAsdaKeybindConfig::GetInputActionName(int32 Cmd)
{
	const TCHAR* Label = GetActionLabel(Cmd);
	if (!Label || Cmd <= 0) { return NAME_None; }
	FString S(Label);
	S.RemoveFromEnd(TEXT(":"));
	S.ReplaceInline(TEXT(" "), TEXT("_"));
	return FName(*FString::Printf(TEXT("ASDA_%s"), *S));
}

int32 UAsdaKeybindConfig::CmdFromInputActionName(FName ActionName)
{
	for (int32 Cmd = 1; Cmd <= GetLastActionCmd(); ++Cmd)
	{
		if (GetActionLabel(Cmd) && GetInputActionName(Cmd) == ActionName) { return Cmd; }
	}
	return 0;
}

// Модификатор бинда → флаги ActionMapping (у движка это отдельные bShift/bCtrl/bAlt).
static void ModToFlags(EAsdaKeyMod M, bool& bShift, bool& bCtrl, bool& bAlt)
{
	bShift = (M == EAsdaKeyMod::Shift);
	bCtrl  = (M == EAsdaKeyMod::Ctrl);
	bAlt   = (M == EAsdaKeyMod::Alt);
}

int32 UAsdaKeybindConfig::LoadFromInputSettings()
{
	UInputSettings* IS = UInputSettings::GetInputSettings();
	if (!IS) { return 0; }
	int32 nApplied = 0;
	for (const FInputActionKeyMapping& M : IS->GetActionMappings())
	{
		const int32 Cmd = CmdFromInputActionName(M.ActionName);
		if (Cmd <= 0) { continue; }                       // не наше действие — не трогаем
		const int32 Vk = FKeyToVk(M.Key);                 // Key=None → 0 = «не назначено»
		// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Keys.KeepUnmappableOnLoad`): пустой ActionMapping при биндe файла, который в FKey
		//   не выразить (PrtSc у «SCREEN SHOT»: `SaveToInputSettings` пишет его Key=None), — это не «игрок снял клавишу», а
		//   ограничение слоя; бинд файла остаётся.
		if (Vk == 0 && CVarAsdaKeysKeepUnmappable.GetValueOnGameThread() != 0)
		{
			const FAsdaKeyBind* Cur = Binds.Find(Cmd);
			if (Cur && Cur->Vk != 0 && !VkToFKey(Cur->Vk).IsValid()) { ++nApplied; continue; }
		}
		EAsdaKeyMod Mod = EAsdaKeyMod::None;
		if (M.bShift) { Mod = EAsdaKeyMod::Shift; }
		else if (M.bCtrl) { Mod = EAsdaKeyMod::Ctrl; }
		else if (M.bAlt)  { Mod = EAsdaKeyMod::Alt; }
		// ★ БЕЗ RemoveConflicts: в штатном конфиге одна клавиша штатно может нести несколько
		//   действий (эталонные «1» и «SHIFT+1» — как раз такой случай), и вычищать «прежнего
		//   владельца» здесь означало бы терять строки конфига при загрузке.
		Binds.Add(Cmd, FAsdaKeyBind{ Vk, Mod });
		++nApplied;
	}
	if (nApplied > 0)
	{
		Source = EAsdaKeybindSource::InputSettings;
		SourcePath = TEXT("Config/DefaultInput.ini → [/Script/Engine.InputSettings] ActionMappings");
	}
	UE_LOG(LogAsdaKeys, Log, TEXT("Штатные ActionMappings: применено действий — %d"), nApplied);
	return nApplied;
}

int32 UAsdaKeybindConfig::SaveToInputSettings(bool bSaveToDisk)
{
	UInputSettings* IS = UInputSettings::GetInputSettings();
	if (!IS) { return 0; }
	int32 n = 0;
	for (int32 Cmd = 1; Cmd <= GetLastActionCmd(); ++Cmd)
	{
		const FName ActionName = GetInputActionName(Cmd);
		if (ActionName.IsNone()) { continue; }
		// Снять прежние отображения этого действия, чтобы не плодить дубли.
		TArray<FInputActionKeyMapping> Old;
		IS->GetActionMappingByName(ActionName, Old);
		for (const FInputActionKeyMapping& O : Old) { IS->RemoveActionMapping(O, /*bForceRebuildKeymaps=*/false); }

		const FAsdaKeyBind* B = Binds.Find(Cmd);
		const FAsdaKeyBind V = B ? *B : FAsdaKeyBind();
		FInputActionKeyMapping M;
		M.ActionName = ActionName;
		// ★ Не назначено → Key=None. Действие ВСЁ РАВНО присутствует в конфиге — именно этого
		//   и просил владелец («включая неназначенные, чтобы не потерялись»).
		M.Key = V.IsBound() ? VkToFKey(V.Vk) : EKeys::Invalid;
		// ⚠ У `FInputActionKeyMapping` флаги — БИТОВЫЕ ПОЛЯ uint8, ссылку на bool с них не взять.
		bool bS = false, bC = false, bA = false;
		ModToFlags(V.Mod, bS, bC, bA);
		M.bShift = bS; M.bCtrl = bC; M.bAlt = bA;
		IS->AddActionMapping(M, /*bForceRebuildKeymaps=*/false);
		++n;
	}
	IS->ForceRebuildKeymaps();
	if (bSaveToDisk)
	{
		IS->SaveKeyMappings();      // штатное сохранение движка (Saved/Config/<Платформа>/Input.ini)
		IS->SaveConfig();
	}
	UE_LOG(LogAsdaKeys, Log, TEXT("Штатные ActionMappings: записано действий — %d%s"), n,
		bSaveToDisk ? TEXT(", сохранено на диск") : TEXT(""));
	return n;
}

FString UAsdaKeybindConfig::ResolveShortcutkeyIniPath(const UAsdaKeybindProfile* Prof, bool bForWrite)
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

	// 00) ★ 25.10.2026: песочница самотеста (`SetTestIniOverride`) — выше всех, и для чтения, и для записи.
	if (!GAsdaKeysTestIniOverride.IsEmpty()) { return GAsdaKeysTestIniOverride; }

	// 0) ★★★ ПЕСОЧНИЦА ДЛЯ ТЕСТОВ (19.09.2026): `-AsdaShortcutkeyIni=<путь>` — ВЫСШИЙ приоритет.
	//    Раскладка по умолчанию живёт в `<ProjectDir>/system/Shortcutkey.ini` — папке, ОБЩЕЙ для игры
	//    владельца и для тестовых прогонов. Тестовый стенд изолировал только файл настроек
	//    (`-GameUserSettingsINI`), а раскладку — нет: за один день тесты записали файл владельца семь раз,
	//    и нажатие «По умолчанию» + «ОК» в тесте стёрло его раскладку. Этот параметр уводит раскладку в
	//    песочницу так же, как `-GameUserSettingsINI` уводит настройки. ЛЮБОЙ тестовый запуск, который
	//    нажимает «ОК»/«Применить»/«По умолчанию» в окне настроек, ОБЯЗАН его передавать.
	//    Без параметра поведение прежнее.
	{
		FString Sandbox;
		if (FParse::Value(FCommandLine::Get(), TEXT("-AsdaShortcutkeyIni="), Sandbox) && !Sandbox.IsEmpty())
		{
			if (FPaths::IsRelative(Sandbox)) { Sandbox = FPaths::Combine(FPaths::LaunchDir(), Sandbox); }
			FPaths::NormalizeFilename(Sandbox);
			return Sandbox;   // и для чтения, и для записи — владельческий файл не трогается вовсе
		}
	}

	// 0а) ★★★ 26.10.2026 (ревизия wave_1025d п. 7) ПРЕДОХРАНИТЕЛЬ: самотест-запуск без песочницы (пакет, `-ExecCmds` с самотестом,
	//   `-AsdaNoNet` + допущенный самотест) — файл раскладки уводится в `Saved/AsdaSelfTest/Shortcutkey.ini`, файл владельца не
	//   пишется. Чтобы тест видел ту же раскладку, при первом обращении туда кладётся копия файла владельца.
	//   (Самотесты окна настроек 25.10 писали `system/Shortcutkey.ini` владельца: PrtSc → NONE, лишняя строка EXT JUMP.)
#if !UE_BUILD_SHIPPING
	if (CVarAsdaKeysSelfTestIniGuard.GetValueOnAnyThread() != 0 && IsSelfTestRun())
	{
		const FString Guard = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaSelfTest"), TEXT("Shortcutkey.ini")));
		if (!PF.FileExists(*Guard))
		{
			const FString Owner = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectDir(), TEXT("system"), TEXT("Shortcutkey.ini")));
			if (PF.FileExists(*Owner))
			{
				PF.CreateDirectoryTree(*FPaths::GetPath(Guard));
				PF.CopyFile(*Guard, *Owner);
			}
		}
		if (!GAsdaKeysGuardLogged)
		{
			GAsdaKeysGuardLogged = true;
			UE_LOG(LogAsdaKeys, Warning, TEXT("[самотест] раскладка уведена в песочницу «%s» — файл владельца system/Shortcutkey.ini не пишется ")
				TEXT("(самотест-запуск без -AsdaShortcutkeyIni)"), *Guard);
		}
		return Guard;
	}
#endif

	// 1) Явный путь из профиля (абсолютный либо относительно ProjectDir).
	if (Prof && !Prof->ShortcutkeyIniPath.IsEmpty())
	{
		FString P = Prof->ShortcutkeyIniPath;
		if (FPaths::IsRelative(P)) { P = FPaths::Combine(FPaths::ProjectDir(), P); }
		FPaths::NormalizeFilename(P);
		if (bForWrite || PF.FileExists(*P)) { return P; }
	}

	// 2) `<ProjectDir>/system/Shortcutkey.ini` — наш аналог `<GameDir>\system\…` оригинала.
	const FString ProjPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("system"), TEXT("Shortcutkey.ini")));
	if (PF.FileExists(*ProjPath)) { return ProjPath; }

	// 3) `<LaunchDir>/system/Shortcutkey.ini` — на случай запуска рядом с исполняемым файлом
	//    (эталон берёт именно `GetCurrentDir()`).
	const FString LaunchPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::LaunchDir(), TEXT("system"), TEXT("Shortcutkey.ini")));
	if (PF.FileExists(*LaunchPath)) { return LaunchPath; }

	return bForWrite ? ProjPath : FString();
}

bool UAsdaKeybindConfig::LoadShortcutkeyIni(const FString& Path)
{
	TArray<FString> Lines;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		return false;
	}

	// ── Порт `keybind_LoadShortcutkeyIni@0xE3BBE0` ──────────────────────────────────────────────
	//   Для КАЖДОЙ строки перебираются лейблы по ВОЗРАСТАНИЮ actionId (в оригинале это
	//   std::map<int,string>, итератор идёт по ключу) и берётся ПЕРВЫЙ, который является префиксом
	//   строки. Значение — первый токен (sscanf "%s") после лейбла.
	int32 nApplied = 0, nUnknownLine = 0, nBadToken = 0;
	bool bArrowUsed = false;

	for (const FString& Raw : Lines)
	{
		if (Raw.TrimStartAndEnd().IsEmpty()) { continue; }

		// ★★★ ДЕФЕКТ ПОРТА — ИСПРАВЛЕН (аудит 2026-09-02, находка К-2, КРИТ).
		//   БЫЛО: строка опознавалась по ДВУМ таблицам (эталонной и таблице расширений), а длина
		//   лейбла для отрезания значения бралась ВСЕГДА из эталонной — `GActionLabels[Cmd]`.
		//   При `Cmd` = 86…94 (наши расширения) это чтение ЗА ГРАНИЦЕЙ массива из 86 элементов
		//   (`static_assert` выше) с последующим `Strlen` по мусорному указателю, т.е. порча памяти
		//   на штатном пути «переназначил клавишу → `SaveShortcutkeyIni` дописал 9 EXT-строк →
		//   следующий старт читает тот же файл».
		//   СТАЛО: запоминаем ТОТ САМЫЙ лейбл, по которому строка опознана, и длину берём у него.
		//   Так же устроен и эталон: `keybind_LoadShortcutkeyIni@0xE3BBE0` (`input.c:3610-3620`)
		//   берёт `strlen` у строки ТЕКУЩЕГО элемента `std::map<int,string>`, а не по индексу.
		int32 Cmd = INDEX_NONE;
		const TCHAR* MatchedLabel = nullptr;   // лейбл, по которому строка опознана (у него и берём длину)
		for (int32 i = 1; i < (int32)UE_ARRAY_COUNT(GActionLabels); ++i)   // 0 = «NONE», действием не является
		{
			if (Raw.StartsWith(GActionLabels[i], ESearchCase::CaseSensitive)) { Cmd = i; MatchedLabel = GActionLabels[i]; break; }
		}
		// ★ Наши расширения (86+). Двоеточие в лейбле снимает неоднозначность префиксов — так же,
		//   как у эталонных («BAR SLOT 1:» не является префиксом строки «BAR SLOT 11: …»).
		if (Cmd == INDEX_NONE)
		{
			for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(GExtActionLabels); ++i)
			{
				if (Raw.StartsWith(GExtActionLabels[i], ESearchCase::CaseSensitive))
				{
					Cmd = kExtFirstCmd + i; MatchedLabel = GExtActionLabels[i]; break;
				}
			}
		}
		if (Cmd == INDEX_NONE || MatchedLabel == nullptr)
		{
			++nUnknownLine;
			UE_LOG(LogAsdaKeys, Warning, TEXT("Shortcutkey.ini: строка без известного лейбла — «%s»"), *Raw);
			continue;
		}

		// sscanf("%s") = первый непробельный токен.
		FString Rest = Raw.Mid(FCString::Strlen(MatchedLabel));
		Rest.TrimStartInline();
		FString Token = Rest;
		for (int32 i = 0; i < Rest.Len(); ++i)
		{
			if (FChar::IsWhitespace(Rest[i])) { Token = Rest.Left(i); break; }
		}

		FAsdaKeyBind B;
		if (!ParseBindToken(Token, B))
		{
			// «NONE» — штатное «не назначено», всё остальное нераспознанное логируем.
			if (!Token.Equals(TEXT("NONE"), ESearchCase::CaseSensitive) && !Token.IsEmpty())
			{
				++nBadToken;
				UE_LOG(LogAsdaKeys, Warning, TEXT("Shortcutkey.ini: «%s» — клавиша «%s» не из таблицы 74, снято"),
					*GetCmdName(Cmd), *Token);
			}
			B = FAsdaKeyBind();
		}

		SetBind(Cmd, B.Vk, B.Mod);
		++nApplied;

		// ★★★ ДЕФЕКТ ОРИГИНАЛА — ИСПРАВЛЕН (закон [[fix-original-defects]]).
		//   Эталон (подтверждено ДИЗАСМОМ 0xE3BDA0..0xE3BE1F, не только декомпилом) считает
		//   «стрелки уже заняты» так:
		//       if (vk==UP    && mod==0) f = true;      // без else
		//       f = (vk==DOWN  && mod==0);              // ← БЕЗУСЛОВНАЯ запись, затирает
		//       f = (vk==LEFT  && mod==0);              // ← затирает
		//       f = (vk==RIGHT && mod==0);              // ← затирает
		//   т.е. флаг отражает только ПОСЛЕДНЮЮ разобранную строку файла. Из-за этого при
		//   `MOVE_W: NONE` движение молча забирает UP, а `Keybind_RemoveConflicts` внутри SetBind
		//   ОЧИЩАЕТ прежнего владельца — «BAR SLOT 11» теряет клавишу без единого сообщения.
		//   Здесь флаг накапливается по всем строкам, как и задумывалось.
		if (B.Mod == EAsdaKeyMod::None && (B.Vk == 38 || B.Vk == 40 || B.Vk == 37 || B.Vk == 39))
		{
			bArrowUsed = true;
		}
	}

	// Дострой движения стрелками — только если стрелки НИКЕМ не заняты (эталон, ветка `else`):
	//   MOVE_W→UP(38), MOVE_S→DOWN(40), MOVE_A→LEFT(37), MOVE_D→RIGHT(39).
	if (!bArrowUsed)
	{
		static const int32 kMoveCmd[4] = { 51, 52, 53, 54 };
		static const int32 kArrowVk[4] = { 38, 40, 37, 39 };
		for (int32 i = 0; i < 4; ++i)
		{
			const FAsdaKeyBind* Cur = Binds.Find(kMoveCmd[i]);
			if (!Cur || Cur->Vk == 0)
			{
				SetBind(kMoveCmd[i], kArrowVk[i], EAsdaKeyMod::None);
				UE_LOG(LogAsdaKeys, Log, TEXT("Shortcutkey.ini: «%s» не назначено → стрелка %s (эталонный достраиватель)"),
					*GetCmdName(kMoveCmd[i]), VkToKeyName(kArrowVk[i]));
			}
		}
	}

	Source = EAsdaKeybindSource::ShortcutkeyIni;
	SourcePath = Path;
	UE_LOG(LogAsdaKeys, Log, TEXT("Раскладка прочитана из «%s»: строк применено %d, неизвестных лейблов %d, плохих клавиш %d"),
		*Path, nApplied, nUnknownLine, nBadToken);
	return true;
}

bool UAsdaKeybindConfig::SaveShortcutkeyIni(const FString& Path) const
{
	// Порт `keybindSaveShortcutIni@0xE3C300`: строки `<ЛЕЙБЛ>\t<КЛАВИША>` по возрастанию actionId,
	//   не назначено → «NONE». Оригинал открывает файл в текстовом режиме ("wt"), т.е. «\n» → CRLF —
	//   пишем CRLF явно (поставочный файл именно такой: 0D 0A, разделитель TAB).
	if (Path.IsEmpty()) { return false; }
	FString Out;
	Out.Reserve(4096);
	for (int32 Cmd = 1; Cmd < (int32)UE_ARRAY_COUNT(GActionLabels); ++Cmd)
	{
		const FAsdaKeyBind* B = Binds.Find(Cmd);
		Out += FString::Printf(TEXT("%s\t%s\r\n"), GActionLabels[Cmd], *FormatBind(B ? *B : FAsdaKeyBind()));
	}
	// ★ Блок НАШИХ расширений идёт СТРОГО ПОСЛЕ эталонных 85 строк: первые 85 строк файла обязаны
	//   остаться теми же, что в поставке оригинала (там номер строки == код действия).
	for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(GExtActionLabels); ++i)
	{
		const int32 Cmd = kExtFirstCmd + i;
		const FAsdaKeyBind* B = Binds.Find(Cmd);
		Out += FString::Printf(TEXT("%s\t%s\r\n"), GExtActionLabels[i], *FormatBind(B ? *B : FAsdaKeyBind()));
	}
	const bool bOk = FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceAnsi);
	UE_LOG(LogAsdaKeys, Log, TEXT("Запись раскладки в «%s» — %s"), *Path, bOk ? TEXT("успешно") : TEXT("ОШИБКА"));
	return bOk;
}

void UAsdaKeybindConfig::LoadLayout(UAsdaKeybindProfile* InProfile)
{
	Profile = InProfile;

	// Слой 1 — хардкод-дефолты оригинала.
	LoadDefaults();

	// Слой 2 — редакторный профиль (в оригинале его нет; применяется только если заполнен).
	if (Profile && Profile->Binds.Num() > 0)
	{
		for (const TPair<int32, FAsdaKeyBind>& P : Profile->Binds)
		{
			// ★ ДЕФЕКТ ПОРТА — ИСПРАВЛЕН (аудит 2026-09-02, находка №11): отсечка по 85 не пускала
			//   в редакторный слой НАШИ расширения 86..94. Пропускаем всё, у чего есть лейбл.
			if (P.Key >= 1 && GetActionLabel(P.Key) != nullptr) { SetBind(P.Key, P.Value.Vk, P.Value.Mod); }
		}
		Source = EAsdaKeybindSource::Profile;
		SourcePath = Profile->GetPathName();
		UE_LOG(LogAsdaKeys, Log, TEXT("Применён профиль-DataAsset «%s» (%d записей)"), *Profile->GetName(), Profile->Binds.Num());
	}

	// Слой 3 — legacy-импорт `system/Shortcutkey.ini` (файл ОРИГИНАЛЬНОЙ игры).
	//   ★ 2026-08-29 он БОЛЬШЕ НЕ ПОБЕЖДАЕТ: по решению владельца раскладка живёт в ШТАТНОМ
	//     конфиге движка, а этот файл остаётся только удобным импортом для тех, кто переносит
	//     свои клавиши из оригинала. Поверх него ложится слой 4 (ActionMappings).
	const bool bWantIni = !Profile || Profile->bUseShortcutkeyIni;
	bool bIniLoaded = false;
	if (bWantIni)
	{
		const FString Path = ResolveShortcutkeyIniPath(Profile, /*bForWrite=*/false);
		if (!Path.IsEmpty() && LoadShortcutkeyIni(Path))
		{
			bIniLoaded = true;
		}
		else
		{
			// Файла нет — оригинал пишет его из дефолтов (`keybind_LoadShortcutkeyIni`, ветка !pFile).
			const bool bWrite = !Profile || Profile->bWriteIniIfMissing;
			UE_LOG(LogAsdaKeys, Log,
				TEXT("system/Shortcutkey.ini не найден — legacy-импорт пропущен (это не ошибка: ")
				TEXT("основная раскладка берётся из штатных ActionMappings)."));
			if (bWrite)
			{
				SaveShortcutkeyIni(ResolveShortcutkeyIniPath(Profile, /*bForWrite=*/true));
			}
		}
	}

	// ★★★ Слой 4 — ШТАТНЫЙ КОНФИГ ДВИЖКА. ПОБЕЖДАЕТ ВСЕХ (решение владельца 2026-08-29:
	//   «сохраняться будет в штатном конфиге UE, а не в кастомном формате»).
	//   Источник: `Config/DefaultInput.ini` → `[/Script/Engine.InputSettings]` → `+ActionMappings`,
	//   поверх него движок сам накладывает пользовательский `Saved/Config/<Платформа>/Input.ini`.
	const int32 nFromSettings = LoadFromInputSettings();
	if (nFromSettings == 0)
	{
		// Действий в конфиге нет вовсе (первый запуск после обновления) — заполняем его текущей
		//   раскладкой, чтобы владелец увидел ВСЕ действия, включая неназначенные.
		UE_LOG(LogAsdaKeys, Warning,
			TEXT("В штатных ActionMappings нет ни одного действия ASDA_* — записываю текущую раскладку ")
			TEXT("(источник: %s)"), bIniLoaded ? TEXT("Shortcutkey.ini") : TEXT("дефолты/профиль"));
		SaveToInputSettings(/*bSaveToDisk=*/false);
	}

	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ]: старый файл/ActionMappings, где подбор и прыжок оба на Пробеле, — развести (разбор у объявления).
	ApplyJumpPickupSplit(TEXT("загрузка раскладки"));
}

bool UAsdaKeybindConfig::IsSaveOnOkOnly()
{
	return CVarAsdaKeysSaveOnOkOnly.GetValueOnGameThread() != 0;
}

void UAsdaKeybindConfig::SetTestIniOverride(const FString& Path)
{
	GAsdaKeysTestIniOverride = Path;
	UE_LOG(LogAsdaKeys, Log, TEXT("Песочница раскладки самотеста: %s"), Path.IsEmpty() ? TEXT("снята") : *Path);
}

void UAsdaKeybindConfig::NoteSelfTestAdmitted()
{
	GAsdaKeysSelfTestAdmitted = true;
}

bool UAsdaKeybindConfig::IsSelfTestRun()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const TCHAR* Cmd = FCommandLine::Get();
	if (FCString::Stristr(Cmd, TEXT("-saveddirsuffix="))) { return true; }   // пакет asda_selftest_batch.py / стенды с песочницей Saved
	if (FCString::Stristr(Cmd, TEXT("SelfTest"))) { return true; }           // `-ExecCmds=…SelfTest…` (стенды PIE/-game)
	return GAsdaKeysSelfTestAdmitted && FParse::Param(Cmd, TEXT("AsdaNoNet"));
#endif
}

int32 UAsdaKeybindConfig::ConsumePickupUnboundNotice()
{
	const int32 Owner = GAsdaKeysPickupUnboundOwner;
	GAsdaKeysPickupUnboundOwner = 0;
	return Owner;
}

// ═══ ★ 26.10.2026 ПЕСОЧНИЦА РАСКЛАДКИ ДЛЯ САМОТЕСТОВ (объявление и назначение — AsdaKeybindConfig.h) ═══════════════════════════
namespace
{
	// ActionMappings движка с именем ASDA_* (снимок/возврат песочницы).
	TArray<FInputActionKeyMapping> AsdaActionMappingsSnapshot()
	{
		TArray<FInputActionKeyMapping> Out;
		if (const UInputSettings* IS = UInputSettings::GetInputSettings())
		{
			for (const FInputActionKeyMapping& M : IS->GetActionMappings())
			{
				if (UAsdaKeybindConfig::CmdFromInputActionName(M.ActionName) > 0) { Out.Add(M); }
			}
		}
		return Out;
	}
}

bool FAsdaKeybindTestSandbox::Begin(UAsdaKeybindConfig* Keys, const TCHAR* Tag, FString& OutWhy)
{
	if (bArmed) { OutWhy = TEXT("песочница уже взведена"); return false; }
	// ActionMappings «ОК»/«Применить» пишет в Saved/Config — без отдельной папки Saved прогон испортил бы Input.ini владельца.
	if (!FString(FCommandLine::Get()).Contains(TEXT("-saveddirsuffix=")))
	{
		OutWhy = TEXT("нужен -saveddirsuffix (иначе «ОК» окна запишет Input.ini владельца)");
		return false;
	}
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	OwnerPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("system"), TEXT("Shortcutkey.ini")));
	OwnerBytes.Reset();
	bOwnerExisted = PF.FileExists(*OwnerPath) && FFileHelper::LoadFileToArray(OwnerBytes, *OwnerPath);
	// Действующий файл раскладки (с учётом -AsdaShortcutkeyIni и предохранителя) — его копия станет песочницей теста.
	const FString Cur = UAsdaKeybindConfig::ResolveShortcutkeyIniPath(Keys ? Keys->Profile.Get() : nullptr, /*bForWrite*/ false);
	Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaSelfTest"),
		FString::Printf(TEXT("Shortcutkey_%s.ini"), (Tag && *Tag) ? Tag : TEXT("test"))));
	PF.CreateDirectoryTree(*FPaths::GetPath(Path));
	IFileManager::Get().Delete(*Path, false, true, true);
	if (!Cur.IsEmpty() && PF.FileExists(*Cur)) { PF.CopyFile(*Path, *Cur); }
	WKeys = Keys;
	if (Keys) { SavedBinds = Keys->Binds; SavedSource = Keys->Source; SavedSourcePath = Keys->SourcePath; }
	SavedMappings = MakeShared<TArray<FInputActionKeyMapping>>(AsdaActionMappingsSnapshot());
	UAsdaKeybindConfig::SetTestIniOverride(Path);
	bArmed = true;
	bOwnerUntouched = true;
	UE_LOG(LogAsdaKeys, Log, TEXT("[самотест] песочница раскладки «%s» (копия «%s»), файл владельца %s"), *Path,
		Cur.IsEmpty() ? TEXT("—") : *Cur, bOwnerExisted ? TEXT("есть") : TEXT("отсутствует"));
	return true;
}

bool FAsdaKeybindTestSandbox::End()
{
	if (!bArmed) { return bOwnerUntouched; }
	bArmed = false;
	// Песочницу снимаем, только если она наша (другой самотест мог взвести свою).
	if (GAsdaKeysTestIniOverride == Path) { UAsdaKeybindConfig::SetTestIniOverride(FString()); }
	if (SavedMappings.IsValid())
	{
		if (UInputSettings* IS = UInputSettings::GetInputSettings())
		{
			for (const FInputActionKeyMapping& M : AsdaActionMappingsSnapshot()) { IS->RemoveActionMapping(M, false); }
			for (const FInputActionKeyMapping& M : *SavedMappings) { IS->AddActionMapping(M, false); }
			IS->ForceRebuildKeymaps();
		}
		SavedMappings.Reset();
	}
	if (UAsdaKeybindConfig* K = WKeys.Get()) { K->Binds = SavedBinds; K->Source = SavedSource; K->SourcePath = SavedSourcePath; }
	WKeys.Reset();
	IFileManager::Get().Delete(*Path, false, true, true);
	// Сверка файла владельца байт в байт.
	TArray<uint8> Now;
	const bool bExistsNow = FPlatformFileManager::Get().GetPlatformFile().FileExists(*OwnerPath) && FFileHelper::LoadFileToArray(Now, *OwnerPath);
	bOwnerUntouched = (bExistsNow == bOwnerExisted) && (!bExistsNow || Now == OwnerBytes);
	if (!bOwnerUntouched)
	{
		UE_LOG(LogAsdaKeys, Error, TEXT("[самотест] ФАЙЛ РАСКЛАДКИ ВЛАДЕЛЬЦА ИЗМЕНЁН за прогон: «%s» (было %d б, стало %d б)"), *OwnerPath,
			bOwnerExisted ? OwnerBytes.Num() : -1, bExistsNow ? Now.Num() : -1);
	}
	return bOwnerUntouched;
}

bool UAsdaKeybindConfig::IsWorldConflictCmd(int32 Cmd)
{
	return (Cmd >= 1 && !IsExtensionCmd(Cmd) && GetActionLabel(Cmd) != nullptr) || Cmd == kCmdJump;
}

int32 UAsdaKeybindConfig::LookupWorldOwner(int32 Vk, EAsdaKeyMod Mod, int32 ExcludeCmd) const
{
	if (Vk == 0) { return 0; }
	int32 Best = 0;
	for (const TPair<int32, FAsdaKeyBind>& P : Binds)
	{
		if (P.Key == ExcludeCmd || !IsWorldConflictCmd(P.Key)) { continue; }
		if (P.Value.Vk == Vk && P.Value.Mod == Mod && (Best == 0 || P.Key < Best)) { Best = P.Key; }
	}
	return Best;
}

bool UAsdaKeybindConfig::ApplyJumpPickupSplit(const TCHAR* Why)
{
	if (CVarAsdaKeysJumpPickupSplit.GetValueOnGameThread() == 0) { return false; }
	const FAsdaKeyBind* Pick = Binds.Find(kCmdGetItem);
	const FAsdaKeyBind* Jump = Binds.Find(kCmdJump);
	if (!Pick || !Jump || !Pick->IsBound() || *Pick != *Jump) { return false; }
	const FString Was = FormatBind(*Pick);
	// F свободна среди команд игрового контекста — подбор на F; иначе «не назначено» (игрок назначит сам).
	const int32 FOwner = LookupWorldOwner(kPortPickupVk, EAsdaKeyMod::None, kCmdGetItem);
	FAsdaKeyBind& Slot = Binds.FindOrAdd(kCmdGetItem);
	Slot = (FOwner == 0) ? FAsdaKeyBind{ kPortPickupVk, EAsdaKeyMod::None } : FAsdaKeyBind();
	// ★ 26.10.2026 (ревизия wave_1025d 2.2): подбор остался без клавиши — не молча: окно настроек покажет нотис (один раз за процесс).
	if (FOwner != 0 && CVarAsdaKeysPickupUnboundNotice.GetValueOnAnyThread() != 0)
	{
		GAsdaKeysPickupUnboundOwner = FOwner;
	}
	UE_LOG(LogAsdaKeys, Log, TEXT("[ИСПРАВЛЕНИЕ] %s: «ACTION GET ITEM» и «EXT JUMP» оба на %s (файл до 25.10.2026) — подбор → %s%s"),
		Why ? Why : TEXT("?"), *Was, *FormatBind(Slot), FOwner ? *FString::Printf(TEXT(" (F занята «%s»)"), *GetCmdName(FOwner)) : TEXT(""));
	return true;
}

bool UAsdaKeybindConfig::ReloadLayout()
{
	UAsdaKeybindProfile* P = Profile;
	LoadLayout(P);
	return Source == EAsdaKeybindSource::ShortcutkeyIni;
}

// ── Поиск / изменение ───────────────────────────────────────────────────────────────────────────

int32 UAsdaKeybindConfig::LookupCmd(int32 Vk, EAsdaKeyMod Mod) const
{
	if (Vk == 0) { return 0; }
	// Эталон `Keybind_Lookup` идёт по списку и возвращает ПЕРВОЕ совпадение; список упорядочен по cmd,
	//   поэтому при дубле побеждает МЕНЬШИЙ cmd. Воспроизводим порядок явно — TMap порядка не гарантирует.
	// ★ У нас дубли не теоретические, а штатные: расширения 86..94 намеренно делят клавиши с
	//   эталонными действиями (см. `SetBind`). Правило «меньший cmd» и даёт нужный итог — эталонное
	//   действие всегда старше нашей надстройки, а расширения диспетчеризуют их собственные
	//   подсистемы по коду, а не через этот поиск.
	int32 Best = 0;
	for (const TPair<int32, FAsdaKeyBind>& P : Binds)
	{
		if (P.Value.Vk == Vk && P.Value.Mod == Mod && (Best == 0 || P.Key < Best)) { Best = P.Key; }
	}
	return Best;
}

bool UAsdaKeybindConfig::GetBind(int32 Cmd, FAsdaKeyBind& OutBind) const
{
	if (const FAsdaKeyBind* B = Binds.Find(Cmd)) { OutBind = *B; return true; }
	OutBind = FAsdaKeyBind();
	return false;
}

int32 UAsdaKeybindConfig::SetBind(int32 Cmd, int32 Vk, EAsdaKeyMod Mod, bool bPlayerRebind)
{
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Keys.JumpPickupSplit`): назначение ИГРОКОМ — прыжок (95) в общем правиле «одна клавиша —
	//   одно действие» с эталонными 1..85: Пробел на подбор снимает его с прыжка, и наоборот. Чтение файлов сюда не заходит.
	if (bPlayerRebind && Vk != 0 && CVarAsdaKeysJumpPickupSplit.GetValueOnGameThread() != 0 && IsWorldConflictCmd(Cmd)
		&& (Cmd == kCmdJump || LookupWorldOwner(Vk, Mod, Cmd) == kCmdJump))
	{
		const int32 Owner = LookupWorldOwner(Vk, Mod, Cmd);   // прежний владелец в игровом контексте (наименьший код)
		if (Owner != 0)
		{
			if (FAsdaKeyBind* B = Binds.Find(Owner)) { B->Vk = 0; B->Mod = EAsdaKeyMod::None; }
		}
		FAsdaKeyBind& S = Binds.FindOrAdd(Cmd);
		S.Vk = Vk; S.Mod = Mod;
		return Owner;
	}
	const int32 Prev = (Vk != 0) ? LookupCmd(Vk, Mod) : 0;   // эталон: конфликты снимаются только при VK != 0

	// ★★★ ДЕФЕКТ ПОРТА — ИСПРАВЛЕН (аудит 2026-09-02, находка №12).
	//   Снятие конфликта воспроизводит `Keybind_RemoveConflicts@0xE3B8A0`, но у ОРИГИНАЛА все команды
	//   лежат в одном диапазоне 1..85 и в одном контексте — там правило «клавиша принадлежит ровно
	//   одному действию» верно. НАШИ расширения (86..94) живут в ИСКЛЮЧИТЕЛЬНЫХ режимах (свободная
	//   камера админа, dev-меню, транспорт-пасхалка), и их дефолты НАМЕРЕННО совпадают с эталонными
	//   биндами (`GExtDefaults`: SPACE, E, C, Q, CTRL+N, F12). Совмещение безопасно по построению:
	//   обычный диспетчер расширения не исполняет (`AsdaPlayerController.cpp:1845` — `IsExtensionCmd`
	//   → `continue`), а подсистемы-владельцы спрашивают свой бинд по коду напрямую (`GetBind`).
	//   БЫЛО: `SetBind` снимал конфликт БЕЗ учёта диапазона, а `SaveShortcutkeyIni` пишет 85 эталонных
	//   строк и ПОСЛЕ них 9 EXT-строк ⇒ первое же чтение сохранённого файла молча отбирало клавишу
	//   у «ACTION GET ITEM» (SPACE), «CHAT NORMAL» (CTRL+N), «INTERFACE EMOTION/CHARACTOER/QUEST»
	//   (E/C/Q), а расширения обкрадывали сами себя (F12: 92 у 86; SPACE: 94 у 87) — без единого
	//   сообщения. Маскировалось только тем, что слой 4 (`LoadFromInputSettings`) грузится позже и
	//   конфликты не снимает; при урезанном `DefaultInput.ini` действия пропадали бы совсем.
	//   СТАЛО: конфликт снимается ТОЛЬКО когда обе стороны — эталонные команды 1..85, то есть
	//   поведение оригинала в его собственном диапазоне сохранено ПОБАЙТОВО, а наша надстройка
	//   эталонные бинды больше не трогает.
	const bool bRemoveConflict = (Prev != 0) && (Prev != Cmd) && !IsExtensionCmd(Cmd) && !IsExtensionCmd(Prev);
	if (bRemoveConflict)
	{
		// `Keybind_RemoveConflicts@0xE3B8A0` — прежний владелец комбинации теряет клавишу.
		if (FAsdaKeyBind* B = Binds.Find(Prev)) { B->Vk = 0; B->Mod = EAsdaKeyMod::None; }
	}
	FAsdaKeyBind& Slot = Binds.FindOrAdd(Cmd);
	Slot.Vk = Vk; Slot.Mod = Mod;
	// Возвращаем того, у кого клавиша ДЕЙСТВИТЕЛЬНО снята, — иначе журнал и консоль писали бы
	//   «снято с …» там, где ничего не снималось.
	return bRemoveConflict ? Prev : 0;
}

int32 UAsdaKeybindConfig::RebindFromKey(int32 Cmd, const FKey& Key, EAsdaKeyMod Mod, bool bSaveIni)
{
	// ★ ДЕФЕКТ ПОРТА — ИСПРАВЛЕН (аудит 2026-09-02, находка №11).
	//   БЫЛО: `Cmd > 85` → отказ, то есть НАШИ расширения 86..94 нельзя было переназначить ни одним
	//   предусмотренным способом, хотя заводились они именно ради требования владельца 2026-08-29
	//   «данные из файла, а не из кода» (см. шапку ТАБЛИЦЫ 2b), а `SaveShortcutkeyIni`,
	//   `SaveToInputSettings` и `DumpLayout` их уже пишут и показывают.
	//   СТАЛО: допускаем любой код, у которого ЕСТЬ лейбл, — это ровно 1..85 (эталон) + 86..94 (наши).
	//   Эталонная нумерация от этого не сдвигается: расширения лежат отдельным массивом.
	if (Cmd < 1 || GetActionLabel(Cmd) == nullptr) { return 0; }
	const int32 Vk = NormalizeVk(FKeyToVk(Key));
	// Эталон `Input_OnKeyDown_Dispatch` (ветка captureMode): клавиши, которой нет в таблице имён,
	//   назначить НЕЛЬЗЯ — нотис 11603 «клавиша недопустима».
	if (Vk == 0 || VkToKeyName(Vk) == nullptr)
	{
		UE_LOG(LogAsdaKeys, Warning, TEXT("Переназначение «%s»: клавиша «%s» недопустима (нет в таблице 74)"),
			*GetCmdName(Cmd), *Key.ToString());
		return 0;
	}
	const int32 Prev = SetBind(Cmd, Vk, Mod, /*bPlayerRebind*/ true);
	if (bSaveIni && (!Profile || Profile->bWriteIniOnRebind))
	{
		SaveShortcutkeyIni(ResolveShortcutkeyIniPath(Profile, /*bForWrite=*/true));
	}
	UE_LOG(LogAsdaKeys, Log, TEXT("Переназначено «%s» → %s%s"), *GetCmdName(Cmd),
		*FormatBind(Binds[Cmd]), Prev ? *FString::Printf(TEXT(" (снято с «%s»)"), *GetCmdName(Prev)) : TEXT(""));
	return Prev;
}

// ── Дамп ────────────────────────────────────────────────────────────────────────────────────────

FString UAsdaKeybindConfig::DumpLayout(bool bOnlyBound) const
{
	const TCHAR* Src =
		Source == EAsdaKeybindSource::InputSettings  ? TEXT("ШТАТНЫЕ ActionMappings движка") :
		Source == EAsdaKeybindSource::ShortcutkeyIni ? TEXT("system/Shortcutkey.ini (legacy-импорт)") :
		Source == EAsdaKeybindSource::Profile        ? TEXT("DataAsset-профиль") :
		                                               TEXT("хардкод-дефолты keybindLoadDefaults@0xE3A9C0");
	FString S = FString::Printf(TEXT("── РАСКЛАДКА КЛАВИШ ── источник: %s%s%s\n"), Src,
		SourcePath.IsEmpty() ? TEXT("") : TEXT("\n   файл: "), *SourcePath);
	int32 nBound = 0, nTotal = 0, nExt = 0;
	// ★ Обходим ДО GetLastActionCmd(), а не до 85: иначе НАШИ расширения (86+) «потерялись» бы
	//   в дампе — ровно та беда, ради которой владелец и просил свести всё в один конфиг.
	for (int32 Cmd = 1; Cmd <= GetLastActionCmd(); ++Cmd)
	{
		if (!GetActionLabel(Cmd)) { continue; }
		++nTotal;
		const bool bExt = IsExtensionCmd(Cmd);
		if (bExt) { ++nExt; }
		const FAsdaKeyBind* B = Binds.Find(Cmd);
		const FAsdaKeyBind V = B ? *B : FAsdaKeyBind();
		if (V.IsBound()) { ++nBound; }
		else if (bOnlyBound) { continue; }
		S += FString::Printf(TEXT("  cmd %2d %s %-26s %s\n"), Cmd,
			bExt ? TEXT("[наше]") : TEXT("      "), *GetCmdName(Cmd), *FormatBind(V));
	}
	S += FString::Printf(TEXT("  назначено %d из %d (эталонных %d, наших расширений %d)\n"),
		nBound, nTotal, nTotal - nExt, nExt);
	return S;
}

// ── UE FKey ↔ Windows VK. Покрывает ВСЕ 74 биндабельные клавиши оригинала. ──────────────────────

int32 UAsdaKeybindConfig::FKeyToVk(const FKey& K)
{
	// Буквы A..Z → 65..90, цифры 0..9 → 48..57 (имена FKey односимвольные).
	const FString N = K.GetFName().ToString();
	if (N.Len() == 1)
	{
		const TCHAR C = N[0];
		if (C >= 'A' && C <= 'Z') return 65 + (C - 'A');
		if (C >= '0' && C <= '9') return 48 + (C - '0');
	}
	if (K == EKeys::One) return 49;   if (K == EKeys::Two) return 50;   if (K == EKeys::Three) return 51;
	if (K == EKeys::Four) return 52;  if (K == EKeys::Five) return 53;  if (K == EKeys::Six) return 54;
	if (K == EKeys::Seven) return 55; if (K == EKeys::Eight) return 56; if (K == EKeys::Nine) return 57;
	if (K == EKeys::Zero) return 48;
	// Нумпад — эталон `Keybind_NormalizeVK` сводит его к цифровому ряду.
	if (K == EKeys::NumPadZero) return 48;  if (K == EKeys::NumPadOne) return 49;
	if (K == EKeys::NumPadTwo) return 50;   if (K == EKeys::NumPadThree) return 51;
	if (K == EKeys::NumPadFour) return 52;  if (K == EKeys::NumPadFive) return 53;
	if (K == EKeys::NumPadSix) return 54;   if (K == EKeys::NumPadSeven) return 55;
	if (K == EKeys::NumPadEight) return 56; if (K == EKeys::NumPadNine) return 57;
	if (K == EKeys::F1) return 112;  if (K == EKeys::F2) return 113;  if (K == EKeys::F3) return 114;
	if (K == EKeys::F4) return 115;  if (K == EKeys::F5) return 116;  if (K == EKeys::F6) return 117;
	if (K == EKeys::F7) return 118;  if (K == EKeys::F8) return 119;  if (K == EKeys::F9) return 120;
	if (K == EKeys::F10) return 121; if (K == EKeys::F11) return 122; if (K == EKeys::F12) return 123;
	if (K == EKeys::SpaceBar) return 32;      if (K == EKeys::Tab) return 9;         if (K == EKeys::Enter) return 13;
	if (K == EKeys::Escape) return 27;        if (K == EKeys::Insert) return 45;     if (K == EKeys::Home) return 36;
	if (K == EKeys::PageUp) return 33;        if (K == EKeys::Hyphen) return 189;    if (K == EKeys::Equals) return 187;
	if (K == EKeys::Backslash) return 220;    if (K == EKeys::Tilde) return 192;
	// ★ Раньше этих семи не было — из-за чего «NEW BAR SLOT 11/12» ([ и ]) из ini были непредставимы.
	if (K == EKeys::LeftBracket) return 219;  if (K == EKeys::RightBracket) return 221;
	if (K == EKeys::Semicolon) return 186;    if (K == EKeys::Apostrophe) return 222;
	if (K == EKeys::Comma) return 188;        if (K == EKeys::Period) return 190;
	if (K == EKeys::Slash) return 191;
	if (K == EKeys::Up) return 38;  if (K == EKeys::Down) return 40;  if (K == EKeys::Left) return 37;  if (K == EKeys::Right) return 39;
	return 0;
}

FKey UAsdaKeybindConfig::VkToFKey(int32 Vk)
{
	if (Vk >= 65 && Vk <= 90) { const TCHAR C[2] = { (TCHAR)Vk, 0 }; return FKey(FName(C)); }
	switch (Vk)
	{
	case 48: return EKeys::Zero;  case 49: return EKeys::One;  case 50: return EKeys::Two;   case 51: return EKeys::Three;
	case 52: return EKeys::Four;  case 53: return EKeys::Five; case 54: return EKeys::Six;   case 55: return EKeys::Seven;
	case 56: return EKeys::Eight; case 57: return EKeys::Nine;
	case 112: return EKeys::F1; case 113: return EKeys::F2; case 114: return EKeys::F3; case 115: return EKeys::F4;
	case 116: return EKeys::F5; case 117: return EKeys::F6; case 118: return EKeys::F7; case 119: return EKeys::F8;
	case 120: return EKeys::F9; case 121: return EKeys::F10; case 122: return EKeys::F11; case 123: return EKeys::F12;
	case 32: return EKeys::SpaceBar; case 9: return EKeys::Tab; case 13: return EKeys::Enter; case 27: return EKeys::Escape;
	case 45: return EKeys::Insert; case 36: return EKeys::Home; case 33: return EKeys::PageUp;
	case 189: return EKeys::Hyphen; case 187: return EKeys::Equals; case 220: return EKeys::Backslash; case 192: return EKeys::Tilde;
	case 219: return EKeys::LeftBracket; case 221: return EKeys::RightBracket;
	case 186: return EKeys::Semicolon;   case 222: return EKeys::Apostrophe;
	case 188: return EKeys::Comma;       case 190: return EKeys::Period;      case 191: return EKeys::Slash;
	case 38: return EKeys::Up; case 40: return EKeys::Down; case 37: return EKeys::Left; case 39: return EKeys::Right;
	case 44: return EKeys::Invalid;   // PrintScreen — FKey в UE нет; опрашивается платформенно (см. PollKeys)
	default: return EKeys::Invalid;
	}
}

FKey UAsdaKeybindConfig::VkToNumpadFKey(int32 Vk)
{
	switch (Vk)
	{
	case 48: return EKeys::NumPadZero;  case 49: return EKeys::NumPadOne;   case 50: return EKeys::NumPadTwo;
	case 51: return EKeys::NumPadThree; case 52: return EKeys::NumPadFour;  case 53: return EKeys::NumPadFive;
	case 54: return EKeys::NumPadSix;   case 55: return EKeys::NumPadSeven; case 56: return EKeys::NumPadEight;
	case 57: return EKeys::NumPadNine;
	default: return EKeys::Invalid;
	}
}
