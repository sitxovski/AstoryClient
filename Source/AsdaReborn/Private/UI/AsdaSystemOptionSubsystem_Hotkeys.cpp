// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSystemOptionSubsystem_Hotkeys.cpp — ★ R2 10.11.2026 (разрез AsdaSystemOptionSubsystem.cpp, дословный перенос): горячие клавиши: захват, страница, опрос, гейт Esc, тексты.
//   Общее для частей — в AsdaSystemOptionSubsystemInternal.h; шапка и реверс — в AsdaSystemOptionSubsystem.cpp.
#include "UI/AsdaSystemOptionSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEscChain.h"   // ★ 25.10.2026: шаг «ESC снимает выбранную цель» эталонной цепочки (между проходом окон и меню 69400)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaShowOffSubsystem.h"
#include "UI/AsdaChatBalloonSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"   // ★ гейт ESC, ветка 1: принудительный экран смерти
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Resources/AsdaBenchSubsystem.h"   // ★ 12.10.2026: «Автоподбор» = замер настоящего кадра на месте
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.09.2026: профиль игрока — штатное хранилище настроек окна
#include "Resources/AsdaSoundSubsystem.h"      // ★ 19.09.2026: громкость — через профиль, мимо приоритетов ключей консоли
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Text/AsdaLang.h"   // ★ 09.10.2026: строка «Язык» (мультиязычность)
#include "Scalability.h"   // ★ фаза 2: уровни групп качества при открытии окна
#include "Test/AsdaSelfTestKit.h"   // ★ 12.10.2026: самотест asda.SysOpt.SelfTest (аргументы, отказ онлайн, итог)
// ★ 18.10.2026: самотест строки «Цифры урона» — GetDamageTextMode/ShouldShowDamageText   ★ R3: комментарий снятого включения "World/AsdaWorldSubsystem.h" (файл им не пользовался) — сохранён
#include "UnrealClient.h"                // ★ 18.10.2026: FScreenshotRequest — кадры окна в самотесте «Цифры урона»
#include "Misc/FileHelper.h"             // ★ 18.10.2026: чтение GameUserSettings.ini в самотесте «Цифры урона»
#include "Containers/Ticker.h"           // ★ 18.10.2026: пошаговый самотест «Цифры урона»
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 69400/69500
#include "World/AsdaCursorClip.h"           // ★ 01.11.2026: курсор не покидает окно игры
#include "World/AsdaOrbitCursor.h"          // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaCursorSpeed.h"          // ★ 19.10.2026: «Чувствительность мыши» (строка I2) — скорость курсора
#include "AsdaSystemOptionSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaSystemOptionSubsystem*.cpp

namespace SysOptLoc {
	// Захват клавиши (`SkillTab_Select`, `Input_OnKeyDown_Dispatch`).
	static constexpr int32 kPressKey     = 11602;   // «Выбери клавишу.»
	static constexpr int32 kKeyNotAllowed= 11603;   // «Эту клавишу назначить нельзя. Выбери другую.»
	static constexpr int32 kKeyAssigned  = 11604;   // «Назначена клавиша быстрого доступа.»
	static constexpr int32 kKeyReassigned= 11605;   // «Назначена новая клавиша быстрого доступа.»
}

static ASDA_DEV_CVAR(int32, CVarLegacyNoRebind66,
	TEXT("asda.SysOpt.LegacyNoRebind66"), 1,
	TEXT("Дефект Д2 оригинала: действие 66 нельзя переназначить (`SkillTab_Select`: if (a2 != 66)).\n")
	TEXT("1 = как оригинал (умолчание), 0 = разрешить переназначение 66."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarAsdaKeysCaptureSwallow,
	TEXT("asda.Keys.CaptureSwallow"), 1,
	TEXT("[ОРИГИНАЛ] 1 — клавиша, нажатая при захвате в окне настроек, до своего отпускания не исполняет игровую команду (эталон: ")
	TEXT("в захвате ни хода, ни диспетча); 0 — прежнее (отпускание исполняло только что назначенную команду)."),
	ECVF_Default);

// ★★★ ПЕРСИСТ НАСТРОЕК (19.09.2026). Аналог эталонного `system\config.txt` (`Config_WriteSettings`) —
//   ШТАТНЫЙ профиль игрока UE: подкласс `UAsdaGameUserSettings` (`Public/Resources/AsdaGameUserSettings.h`,
//   регистрация `GameUserSettingsClassName` в `Config/DefaultEngine.ini`). Прежняя секция `[AsdaOptions]`,
//   куда окно писало строки через `GConfig` напрямую, больше не читается и не пишется: её значения один
//   раз переносит в профиль `UAsdaGameUserSettings::LoadSettings`.
// Шаг строки страницы клавиш в пикселях базы 1024×768 — эталонный множитель 19 в
//   `SkillTab_Select` (`19 * (a2 - …) + …`) и в `uiTabHighlightSelected` (`-4 - 19*a2`).
static constexpr float kHotkeyRowStep = 19.f;

// ═══════════════════════════ КЛАВИШИ ═══════════════════════════════════════════════════════════
UAsdaKeybindConfig* UAsdaSystemOptionSubsystem::GetKeybinds() const
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	return PC ? PC->Keys : nullptr;
}

const FAsdaOptHotkeyRow* UAsdaSystemOptionSubsystem::FindRow(int32 ActionId) const
{
	for (const FAsdaOptHotkeyRow& R : HotkeyRows) { if (R.ActionId == ActionId) { return &R; } }
	return nullptr;
}

// Порт `keybindPageRefreshLabels@0xED6330`: для каждой строки страницы вывести текущую привязку
//   в виде `CKeybind_FormatHotkeyString` («SHIFT+F1», «NONE»).
void UAsdaSystemOptionSubsystem::RefreshHotkeyPage(int32 Page)
{
	UAsdaKeybindConfig* Keys = GetKeybinds();
	for (const FAsdaOptHotkeyRow& Row : HotkeyRows)
	{
		if (Row.Page != Page || !Row.KeyText) { continue; }
		FAsdaKeyBind B;
		// ★ Фаза 2 (решение владельца №5): имена — как на клавиатуре, «не назначено» — «—».
		FString Text = FormatBindForWindow(FAsdaKeyBind());
		if (Keys && Keys->GetBind(Row.ActionId, B)) { Text = FormatBindForWindow(B); }
		Row.KeyText->SetText(FText::FromString(Text));
	}
}

void UAsdaSystemOptionSubsystem::RefreshAllHotkeyLabels()
{
	for (int32 p = 0; p < 4; ++p) { RefreshHotkeyPage(p); }
}

// Порт `SkillTab_Select@0xED5E20` (мисномер: это «начать захват клавиши»).
void UAsdaSystemOptionSubsystem::BeginKeyCapture(int32 ActionId)
{
	// ★ Дефект Д2: действие 66 эталон отсекает первой же строкой.
	if (ActionId == 66 && CVarLegacyNoRebind66.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[клавиши] действие 66 в оригинале не переназначается (ключ asda.SysOpt.LegacyNoRebind66)"));
		return;
	}
	if (bCapturing && CaptureAction == ActionId)
	{
		// Повторный клик по уже выбранной строке = отмена (эталон `uiWindowResetSelection`).
		CancelKeyCapture();
		return;
	}
	CaptureAction = ActionId;
	bCapturing = true;
	MoveClickOverlayTo(FindRow(ActionId));
	ChatNotice(SysOptLoc::kPressKey);   // 11602 «Выбери клавишу.»
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[клавиши] захват для действия %d (%s)"),
		ActionId, *UAsdaKeybindConfig::GetCmdName(ActionId));
}

bool UAsdaSystemOptionSubsystem::IsVkHeldFromCapture(int32 Vk) const
{
	return CVarAsdaKeysCaptureSwallow.GetValueOnGameThread() != 0 && CaptureHeldVk.Contains(Vk);
}

bool UAsdaSystemOptionSubsystem::IsGameInputCaptured() const
{
	return CVarAsdaKeysCaptureSwallow.GetValueOnGameThread() != 0 && bCapturing && IsOptionsShown();
}

bool UAsdaSystemOptionSubsystem::ConsumeCaptureHeldKey(int32 Vk, bool bDownNow)
{
	if (CVarAsdaKeysCaptureSwallow.GetValueOnGameThread() == 0) { CaptureHeldVk.Reset(); return false; }
	if (!CaptureHeldVk.Contains(Vk)) { return false; }
	if (!bDownNow) { CaptureHeldVk.Remove(Vk); }   // отпущена — дальше клавиша снова игровая
	return true;
}

// Порт `uiWindowResetSelection@0xED60E0`: сбросить выбор (+275 = −1), спрятать оверлей, снять режим.
void UAsdaSystemOptionSubsystem::CancelKeyCapture()
{
	bCapturing = false;
	CaptureAction = -1;
	for (const TObjectPtr<UImage>& Img : ClickOverlay)
	{
		if (Img) { Img->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

// Переставить рамку 69910 на строку. Эталон двигает ОДИН контрол
//   (`CControl_SetField74_75(this[89], X, Y - 19*scroll)`); у нас он расплющен импортёром в
//   восемь планов (закон `jui-flattened-hierarchy-geometry`), поэтому двигаем все восемь ОДНИМ
//   и тем же смещением — форма рамки не меняется, разметка не правится.
void UAsdaSystemOptionSubsystem::MoveClickOverlayTo(const FAsdaOptHotkeyRow* Row)
{
	if (ClickOverlay.Num() == 0 || ClickOverlayBase.Num() != ClickOverlay.Num()) { return; }
	if (!Row || !Row->KeyText)
	{
		for (const TObjectPtr<UImage>& Img : ClickOverlay) { if (Img) { Img->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	// Позиция поля клавиши в координатах корня окна (живая геометрия ещё может быть нулевой,
	//   поэтому берём ЗАДАННУЮ раскладку слотов — `AsdaUi::CanvasRootPos`).
	const FVector2D KeyPos = AsdaUi::CanvasRootPos(Row->KeyText);
	// Оверлей в .JUI стоит ровно на поле ПЕРВОЙ строки (дизайн-позиция = действие 1), поэтому
	//   смещение считаем как разницу «текущая строка − первая строка».
	FVector2D FirstRowPos = ClickOverlayBase[0];
	if (const FAsdaOptHotkeyRow* First = FindRow(1))
	{
		if (First->KeyText) { FirstRowPos = AsdaUi::CanvasRootPos(First->KeyText); }
	}
	const FVector2D Delta = KeyPos - FirstRowPos;
	for (int32 i = 0; i < ClickOverlay.Num(); ++i)
	{
		if (!ClickOverlay[i]) { continue; }
		AsdaUi::SetSlotPos(ClickOverlay[i], ClickOverlayBase[i] + Delta);
		ClickOverlay[i]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

// Прокрутка страницы клавиш. Эталон двигает содержимое шагом 19 px
//   (`uiCtrl_SetChildScrollbarPos` + вычитание `19 * scroll` в `SkillTab_Select`).
void UAsdaSystemOptionSubsystem::SetHotkeyScroll(int32 Pos)
{
	HotkeyScroll = FMath::Clamp(Pos, 0, FMath::Max(HotkeyScrollMax, 0));
	const int32 P = CurrentPage[2];
	if (P >= 0 && P < 4 && HotkeyPageHost[P] && HotkeyPageBase.IsValidIndex(P))
	{
		AsdaUi::SetSlotPos(HotkeyPageHost[P],
			FVector2D(HotkeyPageBase[P].X, HotkeyPageBase[P].Y - kHotkeyRowStep * (float)HotkeyScroll));
	}
	if (bCapturing) { MoveClickOverlayTo(FindRow(CaptureAction)); }
}

// Порт ветки 69651 (`keybind_loadDefaults(g_pKeybindMgr, 1)` + `keybindWndRefreshAll`).
void UAsdaSystemOptionSubsystem::LoadKeybindDefaults()
{
	if (UAsdaKeybindConfig* Keys = GetKeybinds())
	{
		Keys->LoadDefaults();
		RefreshAllHotkeyLabels();
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[клавиши] загружены хардкод-дефолты (кнопка 69651)"));
	}
}

// Порт ветки 0x11014 (69652): работает ТОЛЬКО в режиме захвата — эталон проверяет `+1096 == 1`.
void UAsdaSystemOptionSubsystem::ClearSelectedBind()
{
	if (!bCapturing || CaptureAction <= 0)
	{
		UE_LOG(LogAsdaSysOpt, Verbose, TEXT("[клавиши] «снять привязку» без выбранной строки — эталон тоже ничего не делает"));
		return;
	}
	if (UAsdaKeybindConfig* Keys = GetKeybinds())
	{
		Keys->SetBind(CaptureAction, 0, EAsdaKeyMod::None);   // эталон `Keybind_SetBind(mgr, +275, 0, 0)`
		RefreshAllHotkeyLabels();
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[клавиши] привязка действия %d снята"), CaptureAction);
	}
}

// Порт `Input_OnKeyDown_Dispatch@0xE3C7D0`, ветка режима захвата (input.c:3915-3944).
void UAsdaSystemOptionSubsystem::AcceptCapturedKey(const FKey& Key, bool bShift, bool bCtrl, bool bAlt)
{
	UAsdaKeybindConfig* Keys = GetKeybinds();
	if (!Keys || CaptureAction <= 0) { return; }

	const int32 RawVk = UAsdaKeybindConfig::FKeyToVk(Key);
	const int32 Vk = UAsdaKeybindConfig::NormalizeVk(RawVk);
	// Эталон: клавиши вне таблицы 74 имён назначить нельзя — нотис 11603 и выход.
	if (Vk == 0 || UAsdaKeybindConfig::VkToKeyName(Vk) == nullptr)
	{
		ChatNotice(SysOptLoc::kKeyNotAllowed);
		return;
	}
	const EAsdaKeyMod Mod = UAsdaKeybindConfig::CurrentModifier(bShift, bCtrl, bAlt);
	// Клавиша уже занята другим действием? Эталон спрашивает ДО назначения (`Keybind_Lookup`).
	const int32 PrevOwner = Keys->LookupCmd(Vk, Mod);
	// ★ 25.10.2026 [ОРИГИНАЛ] (`asda.Keys.SaveOnOkOnly`): эталон в захвате правит только раскладку (`Keybind_SetBind` по
	//   bindMapEdit), файл пишет «ОК»/«Применить» (`optionsWnd_ApplyAndSave` / 0x11329); «Отмена» откатывает (`ReloadLayout`).
	Keys->RebindFromKey(CaptureAction, Key, Mod, /*bSaveIni*/ !UAsdaKeybindConfig::IsSaveOnOkOnly());
	RefreshAllHotkeyLabels();
	ChatNotice((PrevOwner != 0) ? SysOptLoc::kKeyReassigned : SysOptLoc::kKeyAssigned);
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[клавиши] действию %d назначено %s (прежний владелец %d)"),
		CaptureAction, *Key.ToString(), PrevOwner);
	CancelKeyCapture();
}

// ═══════════════════════════ ВВОД: ESC И ЗАХВАТ КЛАВИШИ ════════════════════════════════════════
void UAsdaSystemOptionSubsystem::PollHotkeys(APlayerController* PC, bool bEscConsumed)
{
	if (!PC) { return; }
	// ★ 25.10.2026 [ОРИГИНАЛ]: покадровый учёт «кто ударил своего» (эталонное `netWord54`, стирается входом в покой) — ветки (в) ESC.
	AsdaEscChain::TickSelfState(GetGameInstance());
	auto Pressed = [this, PC](const FKey& K) -> bool
	{
		const bool bDown = PC->IsInputKeyDown(K);
		const bool bPrev = PrevKeyDown.FindRef(K);
		PrevKeyDown.Add(K, bDown);
		return bDown && !bPrev;
	};
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift)   || PC->IsInputKeyDown(EKeys::RightShift);
	const bool bCtrl  = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	const bool bAlt   = PC->IsInputKeyDown(EKeys::LeftAlt)     || PC->IsInputKeyDown(EKeys::RightAlt);

	// ── ПРОКРУТКА СТРАНИЦЫ КЛАВИШ КОЛЕСОМ ──
	//   Эталонный контрол 69649 `SYSTEM_Op_HOTKEYS_Scroll` — это зона КОЛЕСА (скин `WheelWin_Nomal`),
	//   а не полоса прокрутки: `uiTabWindowSelectPage@0xED5BB0` задаёт ему диапазон 1…17 у страницы 0
	//   и 1…1 у остальных (ui_core.c:89042/89047/89053), шаг строки 19 px.
	//   ★ Колесо — только над зоной 69649: у эталона колесо получает контрол под курсором
	//   (скин `WheelWin_Nomal`), а не вся вкладка.
	if (IsOptionsShown() && CurrentTab == 2 && HotkeyScrollMax > 0
		&& (!HotkeyViewport || AsdaUi::IsCursorOverWidget(HotkeyViewport)))
	{
		const float Wheel = PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
		if (!FMath::IsNearlyZero(Wheel))
		{
			SetHotkeyScroll(HotkeyScroll - FMath::RoundToInt(Wheel));
		}
	}

	// ── РЕЖИМ ЗАХВАТА: любая клавиша, кроме ESC, уходит в привязку ──
	//   Эталон `Input_OnKeyDown_Dispatch`: `if (a2 != 229 && a2 != 27)` — ESC в захват НЕ попадает
	//   ВООБЩЕ (им же захват и отменяется ниже).
	// ★ 25.10.2026 (`asda.Keys.CaptureSwallow`): все биндабельные VK, зажатые в момент захвата (и сама клавиша захвата), — в набор
	//   «съеденных до отпускания» (`ConsumeCaptureHeldKey` снимает по отпусканию в `PollKeys`).
	auto NoteHeld = [this, PC]()
	{
		if (CVarAsdaKeysCaptureSwallow.GetValueOnGameThread() == 0) { return; }
		for (int32 V = 1; V < 256; ++V)
		{
			if (UAsdaKeybindConfig::VkToKeyName(V) == nullptr) { continue; }
			const FKey K = UAsdaKeybindConfig::VkToFKey(V);
			const FKey NK = UAsdaKeybindConfig::VkToNumpadFKey(V);
			if ((K.IsValid() && PC->IsInputKeyDown(K)) || (NK.IsValid() && PC->IsInputKeyDown(NK))) { CaptureHeldVk.Add(V); }
		}
	};
	// ★ 26.10.2026 (ревизия wave_1025d 2.1): клавиша из набора, уже отпущенная и не назначенная ни одной команде, — снять (её фронт
	//   отпускания `PollKeys` не увидит: он опрашивает только VK раскладки). Назначенные снимает `ConsumeCaptureHeldKey` в `PollKeys`
	//   (тот идёт ПОСЛЕ этого опроса в том же кадре — снимать их здесь значило бы исполнить команду на отпускании).
	if (CaptureHeldVk.Num() > 0 && CVarAsdaKeysCaptureHeldCleanup.GetValueOnGameThread() != 0)
	{
		const UAsdaKeybindConfig* KB = GetKeybinds();
		for (auto It = CaptureHeldVk.CreateIterator(); It; ++It)
		{
			const int32 V = *It;
			const FKey K = UAsdaKeybindConfig::VkToFKey(V);
			const FKey NK = UAsdaKeybindConfig::VkToNumpadFKey(V);
			if ((K.IsValid() && PC->IsInputKeyDown(K)) || (NK.IsValid() && PC->IsInputKeyDown(NK))) { continue; }
			bool bBound = false;
			if (KB) { for (const TPair<int32, FAsdaKeyBind>& B : KB->Binds) { if (B.Value.Vk == V) { bBound = true; break; } } }
			if (!bBound) { It.RemoveCurrent(); }
		}
	}
	if (bCapturing && IsOptionsShown())
	{
		// Опрашиваем ровно те 74 клавиши, которые оригинал умеет биндить: у каждой есть имя в
		//   таблице `VkToKeyName`, а её `FKey` даёт `VkToFKey`. Так мы не ловим мышь и служебные.
		for (int32 Vk = 1; Vk < 256; ++Vk)
		{
			if (UAsdaKeybindConfig::VkToKeyName(Vk) == nullptr) { continue; }
			const FKey K = UAsdaKeybindConfig::VkToFKey(Vk);
			if (!K.IsValid()) { continue; }
			if (K == EKeys::Escape) { continue; }
			if (Pressed(K))
			{
				NoteHeld();   // ★ 25.10.2026: зажатое в момент захвата — не игровая команда до отпускания
				AcceptCapturedKey(K, bShift, bCtrl, bAlt);
				return;
			}
			// Нумпад-двойник: эталон нормализует 96…105 → 48…57, значит и ловить надо оба.
			const FKey NK = UAsdaKeybindConfig::VkToNumpadFKey(Vk);
			if (NK.IsValid() && Pressed(NK))
			{
				NoteHeld();
				AcceptCapturedKey(NK, bShift, bCtrl, bAlt);
				return;
			}
		}
	}

	// ── ESC ──
	//   Эталон разводит ESC по приоритетам (`GameInput_DispatchEvent` case 2, key 27,
	//   game_main.c:9255-9315 + `inputHandleInventoryAndEscHotkeys@0xC6E710`, input.c:1722):
	//     1) активная зона оконного менеджера → снять её;
	//     2) есть цель → снять цель;
	//     3) иначе → ТОГГЛ ESC-меню 69400.
	//   Пункт 1 — оконная подсистема (`CloseTopmostOnEsc`), пункт 2 — `AsdaEscChain::RunAfterWindowPass` (★ 25.10.2026: до этого
	//   дня пункт 2 не был перенесён никуда — ESC цель не снимал, жалоба игроков); здесь: захват клавиши → проход окон → цель →
	//   окно настроек → ESC-меню.
	if (!Pressed(EKeys::Escape)) { return; }
	const uint64 Frame = GFrameCounter;
	if (EscHandledFrame == Frame) { return; }
	EscHandledFrame = Frame;
	// ★ РЕВИЗИЯ 2026-09-17 (С6): клавишу уже забрала админ-надстройка (закрытие GM-меню либо
	//   выход из свободной камеры). Фронт выше прочитан, латч сдвинут — действий не совершаем.
	if (bEscConsumed)
	{
		UE_LOG(LogAsdaSysOpt, Verbose, TEXT("[система] ESC поглощён админ-надстройкой — ESC-меню не трогаем"));
		return;
	}

	// Отмена захвата клавиши гейтом НЕ закрывается намеренно: в оригинале ESC до захвата вообще
	//   не доходит (`Input_OnKeyDown_Dispatch@0xE3C7D0` первой строкой отсекает VK 27 и 229), а у
	//   нас это единственный выход из режима переназначения — загейтив его, мы заперли бы игрока
	//   в захвате, если поверх окна настроек успела всплыть модалка.
	if (bCapturing)
	{
		CancelKeyCapture();
		return;
	}

	// ── ★★★ ЭТАЛОННЫЙ ПРОХОД «ESC ЗАКРЫВАЕТ ВЕРХНЕЕ ОТКРЫТОЕ ОКНО» (пункт С10-б реестра) ──
	//   `uiContainerHandleKey@0x12AF390` получает клавишу из очереди менеджера окон, а её
	//   `MainWndProc@0x9841A0` наполняет БЕЗУСЛОВНО (`CWindowMgr_QueueKeyDown@0x12EFE60` стоит ВЫШЕ
	//   тройного гейта ввода, гейт закрывает только `Input_OnKeyDown_Dispatch`) ⇒ проход обязан
	//   идти ДО гейта ниже и работать даже при открытом модальном окне. Закрыл окно — эталон
	//   ставит `CWindowMgr+0x208 = 1`, и меню 69400 не тоггается вовсе; у нас это возвращённое
	//   `true`, то есть решение владельца «меню открывается, только если нет активных окон».
	//   Разбор прохода целиком — в шапке `UAsdaWindowSubsystem::CloseTopmostOnEsc`.
	//   Ключ отката: `asda.UI.EscCloseTopWindow 0` (гейт меню откатывается своим `asda.SysOpt.EscGate`).
	//   ★ Стоит ПОСЛЕ отмены захвата клавиши НАМЕРЕННО: захват отменяется только ESC, и загородить
	//   его чем бы то ни было значит запереть игрока в режиме переназначения.
	if (UGameInstance* EscGI = GetGameInstance())
	{
		UAsdaWindowSubsystem* WM = EscGI->GetSubsystem<UAsdaWindowSubsystem>();
		const bool bWindowAnswered = WM && WM->CloseTopmostOnEsc();
		// ── ★ 25.10.2026 [ОРИГИНАЛ] ШАГ ЦЕЛИ (`GameInput_DispatchEvent@0xC6DCA0` case 2, game_main.c:9265-9315; разбор — в шапке
		//   `AsdaEscChain.h`). Окно с флагом `CWindowMgr+0x215` тратит нажатие целиком; иначе выбранная цель снимается — тем же
		//   нажатием, что закрыло окно без флага, — и меню 69400 не тоггается. Гейтов меню у шага нет (как у эталона).
		if (AsdaEscChain::RunAfterWindowPass(EscGI, bWindowAnswered, TEXT("мир")))
		{
			return;
		}
		// ── ★ 25.10.2026 [ОРИГИНАЛ] ВЕТКИ (в): состояние своего персонажа (в бою / на бегу — меню нет, бег с заявкой удара — стоп
		//   4008), затем строка чата. Идут и после окна, закрытого без флага 0x215, — как у эталона (меню гейтит `+0x208` ниже).
		if (AsdaEscChain::RunStateBranches(EscGI, TEXT("мир")))
		{
			return;
		}
		if (bWindowAnswered)
		{
			UE_LOG(LogAsdaSysOpt, Verbose,
				TEXT("[система] ESC закрыл верхнее открытое окно — меню 69400 не трогаем ")
				TEXT("(эталон CWindowMgr+0x208 = 1)"));
			return;
		}
	}

	// ── ★★★ ЧЕТЫРЁХЧАСТНЫЙ ГЕЙТ ОРИГИНАЛА (закрытый пункт С10 реестра отложенного; разбор — в шапке
	//   `IsEscMenuGateBlocked` в заголовке). Стоит ПЕРЕД обеими половинами эталонного
	//   `uiPanelToggleOrClearTabs@0xE8FA70` — и перед закрытием окна настроек, и перед тогглом
	//   меню 69400, потому что в эталоне гейт обёрнут вокруг ВСЕГО вызова, а не вокруг «открыть».
	{
		FString Why;
		if (IsEscMenuGateBlocked(Why))
		{
			UE_LOG(LogAsdaSysOpt, Verbose, TEXT("[система] ESC не тоггает меню 69400: %s"), *Why);
			return;
		}
	}

	if (IsOptionsShown())
	{
		// Эталонный `WM_CLOSE` окна 69500 (`uiOptionsWindow_WndProc` case 0x15, a3 == 69500):
		//   `optionsWnd_ApplyAndSave(this, a2, 0)` — то же, что «Отмена».
		// ★ 10.10.2026: с росписью `EscCloseOptions` окно закрывает ПРОХОД выше (если оно верхнее среди ESC-окон); сюда
		//   ветка доходит только при выключенном проходе (`asda.UI.EscCloseTopWindow 0`) — запасной путь, как до 10.10.
		CloseOptions(false);
		return;
	}
	ToggleMenu();
}

// ══════════════ ГЕЙТ ТОГГЛА ESC-МЕНЮ 69400 — разбор всех четырёх условий в заголовке ════════════
//   Порядок проверок повторяет эталонный: 1) блокирующее состояние интерфейса, 2) перетаскивание,
//   3) модальное окно, 4) «ESC уже съеден» (его проверяет сам `PollHotkeys` по `bEscConsumed`,
//   потому что признак приходит извне — от админ-надстройки, а не из состояния подсистемы).
bool UAsdaSystemOptionSubsystem::IsEscMenuGateBlocked(FString& OutWhy) const
{
	if (CVarEscGate.GetValueOnGameThread() == 0)
	{
		return false;   // ключ отката: гейт снят целиком
	}
	UGameInstance* GI = GetGameInstance();

	// (1) `sub_9323E3` → `CUIManager+0x44`. Воспроизводима ветка «принудительный системный экран»:
	//   меню смерти в порту — `UAsdaAfterDeathSubsystem` (эталон `DeathMenu_AddOption@0xD6D060`
	//   ставит флаг и открывает `SystemMainWnd` вкладку 6). Вторая ветка того же поля —
	//   «защищённый запрос в полёте» — в порту НЕ воспроизводима: одного общего состояния нет,
	//   у каждого окна свой локальный `bPending`. Это записано в реестр отложенного, пункт С10-а.
	if (const UAsdaAfterDeathSubsystem* Death = GI ? GI->GetSubsystem<UAsdaAfterDeathSubsystem>() : nullptr)
	{
		if (Death->IsDeathMenuOpen())
		{
			OutWhy = TEXT("открыто меню смерти (эталон `CUIManager+0x44` = 1)");
			return true;
		}
	}

	// (2) `helper_getField1` → `CUIManager+0x04` = «предмет держат в руке». Общая точка порта —
	//   реестр живых призраков перетаскивания (`FAsdaDragGhost`), один на все семь окон с ячейками.
	if (FAsdaDragGhost::IsAnyActive())
	{
		OutWhy = TEXT("идёт перетаскивание предмета (эталон `CUIManager+0x04` = 1)");
		return true;
	}

	// (3) `sub_930E71` → `CWindowMgr+0x200` = «открыто модальное окно». Готовый эквивалент порта —
	//   `UAsdaWindowSubsystem::IsAnyModalOpen` (им же гейтятся хотбар, инвентарь и чат); второго
	//   такого состояния не заводим (закон `check-existing-before-creating`).
	if (const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsAnyModalOpen())
		{
			OutWhy = TEXT("открыто модальное окно (эталон `CWindowMgr+0x200` = 1)");
			return true;
		}
		// ★ 10.10.2026 (barber_minors): окно, которое эталон показывает модально (`helper_setupFields` ставит тот же
		//   `+0x200`), а порт — без флага `bModal` (общая модалка покупки 1559, `RegisterEscModal`).
		//   ★ 10.10.2026 (modal_lock): при `asda.UI.ModalLock 1` такие окна уже учтены в `IsAnyModalOpen` (строкой выше);
		//   эта проверка держит гейт ESC-меню и при ключе 0 (корневые росписи запирают ESC при любом ключе).
		if (WM->IsAnyEscModalShown())
		{
			OutWhy = TEXT("открыта модалка эталона без флага порта (1559; эталон `CWindowMgr+0x200` = 1)");
			return true;
		}
	}

	return false;
}

// ═══════════════════════════ ВСПОМОГАТЕЛЬНОЕ ═══════════════════════════════════════════════════
FString UAsdaSystemOptionSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// ═══════════════════════════ ★★★ ФАЗА 2 (19.09.2026): ПОДПИСИ, КЛАВИШИ, НОВЫЕ СТРОКИ ═══════════════════
// Наша строка-исправление (таблица `SysOptLocFix`), если она есть в DT_GameText, иначе эталонная.
FString UAsdaSystemOptionSubsystem::LocUi(int32 EtalonLocId) const
{
	const int32 Ours = SysOptLocFix::Find(EtalonLocId);
	if (Ours != EtalonLocId)
	{
		const FString S = Loc(Ours);
		if (!S.IsEmpty()) { return S; }
	}
	return Loc(EtalonLocId);
}

// Эталонная loc подписи строки клавиш (`uiDlgInitControlsByID`, ui_core.c:88359-88480): первая строка
//   каждого блока — заголовок группы, дальше по одной на поле, по порядку кодов действий
//   (`fontMapCodepointToGlyphIndex@0xED6160`). Действие 85 — наше поле, его строка 30232 «Стойка на руках».
int32 UAsdaSystemOptionSubsystem::HotkeyActionLocId(int32 A)
{
	if (A >= 1  && A <= 5)  { return 30040 + A; }          // страницы панели       30041…30045
	if (A >= 6  && A <= 17) { return 30060 + (A - 5); }    // основная панель       30061…30072
	if (A >= 18 && A <= 29) { return 30260 + (A - 17); }   // верхняя панель        30261…30272
	if (A >= 30 && A <= 47) { return 30090 + (A - 29); }   // открыть/закрыть окно  30091…30108
	if (A >= 48 && A <= 50) { return 30120 + (A - 47); }   // отряд                 30121…30123
	if (A >= 51 && A <= 65) { return 30140 + (A - 50); }   // основные действия     30141…30155
	if (A >= 66 && A <= 73) { return 30180 + (A - 65); }   // чат                   30181…30188
	if (A >= 74 && A <= 84) { return 30220 + (A - 73); }   // эмоции                30221…30231
	if (A == 85)            { return 30232; }              // «Стойка на руках» [ИСПРАВЛЕНИЕ ОРИГИНАЛА]
	return 0;
}

// ★ Решение владельца №5: в окне — как подписано на клавиатуре («Space», «Enter», «Shift+F1», «PgUp»),
//   «не назначено» — «—». Файл раскладки не меняется: там по-прежнему имена оригинала (`FormatBind`),
//   по ним `Shortcutkey.ini` и читается (`KeyNameToVk`, регистрозависимо).
FString UAsdaSystemOptionSubsystem::FormatBindForWindow(const FAsdaKeyBind& B)
{
	static const TCHAR* const kNone = TEXT("\u2014");
	if (B.Vk == 0) { return FString(kNone); }
	const TCHAR* Raw = UAsdaKeybindConfig::VkToKeyName(B.Vk);
	if (!Raw) { return FString(kNone); }
	struct FName2 { const TCHAR* Ini; const TCHAR* Shown; };
	static const FName2 kPretty[] = {
		{ TEXT("TAB"), TEXT("Tab") }, { TEXT("ENTER"), TEXT("Enter") }, { TEXT("SPACE"), TEXT("Space") },
		{ TEXT("SHIFT"), TEXT("Shift") }, { TEXT("CTRL"), TEXT("Ctrl") }, { TEXT("ALT"), TEXT("Alt") },
		{ TEXT("UP"), TEXT("Up") }, { TEXT("DOWN"), TEXT("Down") }, { TEXT("LEFT"), TEXT("Left") }, { TEXT("RIGHT"), TEXT("Right") },
		{ TEXT("INSERT"), TEXT("Insert") }, { TEXT("HOME"), TEXT("Home") }, { TEXT("PGUP"), TEXT("PgUp") },
	};
	FString Key(Raw);
	for (const FName2& P : kPretty) { if (Key.Equals(P.Ini, ESearchCase::CaseSensitive)) { Key = P.Shown; break; } }
	switch (B.Mod)
	{
	case EAsdaKeyMod::Shift: return TEXT("Shift+") + Key;
	case EAsdaKeyMod::Ctrl:  return TEXT("Ctrl+") + Key;
	case EAsdaKeyMod::Alt:   return TEXT("Alt+") + Key;
	default:                 return Key;
	}
}
