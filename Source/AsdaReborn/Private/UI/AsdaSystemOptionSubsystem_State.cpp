// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSystemOptionSubsystem_State.cpp — ★ R2 10.11.2026 (разрез AsdaSystemOptionSubsystem.cpp, дословный перенос): состояние настроек: игра↔окно↔конфиг, флаги сервера, пакет 4070; доп. комбо графики.
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
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Resources/AsdaBenchSubsystem.h"   // ★ 12.10.2026: «Автоподбор» = замер настоящего кадра на месте
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.09.2026: профиль игрока — штатное хранилище настроек окна
#include "Resources/AsdaSoundSubsystem.h"      // ★ 19.09.2026: громкость — через профиль, мимо приоритетов ключей консоли
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"
#include "World/AsdaPlayerController.h"
#include "AsdaNetSubsystem.h"

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
#include "World/AsdaWorldSubsystem.h"   // ★ 18.10.2026: самотест строки «Цифры урона» — GetDamageTextMode/ShouldShowDamageText
#include "UnrealClient.h"                // ★ 18.10.2026: FScreenshotRequest — кадры окна в самотесте «Цифры урона»
#include "Misc/FileHelper.h"             // ★ 18.10.2026: чтение GameUserSettings.ini в самотесте «Цифры урона»
#include "Containers/Ticker.h"           // ★ 18.10.2026: пошаговый самотест «Цифры урона»
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 69400/69500
#include "World/AsdaCursorClip.h"           // ★ 01.11.2026: курсор не покидает окно игры
#include "World/AsdaOrbitCursor.h"          // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaCursorSpeed.h"          // ★ 19.10.2026: «Чувствительность мыши» (строка I2) — скорость курсора
#include "AsdaSystemOptionSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaSystemOptionSubsystem*.cpp

namespace SysOptLoc {
	// Предупреждение «настройки вступят в силу после перезапуска» (`optApplySettingsFromConfig`,
	//   game_main.c:12045-12049: модалка типа 3 с текстом loc 61 + строка loc 108 в коллекцию нотисов).
	static constexpr int32 kRestartNeeded = 61;

	static constexpr int32 kBorderless    = 990303;   // «Без рамки»          (пункт «Полный экран» — эталонная loc 30542)
	static constexpr int32 kWindowed      = 990304;   // «Окно»

	static constexpr int32 kNoLimit       = 990307;   // «Без ограничения»

	static constexpr int32 kDx12          = 990310;   // «DirectX 12»
	static constexpr int32 kDx11          = 990311;   // «DirectX 11»

	// ★ [ПОРТ] 09.10.2026 (мультиязычность): строка «Язык» на странице «Экран».
	static constexpr int32 kLangEn        = 990351;   // «English»  (самоназвание — одинаково на всех языках)
	static constexpr int32 kLangRu        = 990352;   // «Русский»

	static constexpr int32 kLangRestart   = 990356;   // «Язык будет применён после перезапуска игры. Перезапустить сейчас?»

	static constexpr int32 kLvlLow        = 990314;   // «Низкое»
	static constexpr int32 kLvlMed        = 990315;   // «Среднее»
	static constexpr int32 kLvlHigh       = 990316;   // «Высокое»
	static constexpr int32 kLvlMax        = 990317;   // «Максимальное»
	static constexpr int32 kLvlUltra      = 990318;   // «Ультра»  (уровень 4 = Cinematic UE, в списке — только если уже стоит)
	static constexpr int32 kLvlCustom     = 990319;   // «Своё»    (группы вразнобой)

	static constexpr int32 kOff           = 990330;   // «Выкл.»
	static constexpr int32 kFXAA          = 990331;   // «FXAA»
	static constexpr int32 kTAA           = 990332;   // «TAA»
	static constexpr int32 kTSR           = 990333;   // «TSR»

	static constexpr int32 kWeak          = 990335;   // «Слабая»
	static constexpr int32 kMedium        = 990336;   // «Средняя»
	static constexpr int32 kStrong        = 990337;   // «Сильная»

	static constexpr int32 kPresetWarn    = 990343;   // нотис: «Общее качество» задаёт все группы сразу
	static constexpr int32 kAutoDone      = 990344;   // нотис: автоподбор подставил уровни
	static constexpr int32 kAutoFail      = 990345;   // нотис: замер железа не дал результата
	static constexpr int32 kFullscreenLoc = 30542;    // «Полный экран» — эталонная строка (пункт 0 списка режима)
}

namespace SysOptExtra {
	static bool IsGroupKey(int32 Key) { return Key >= Q1_Tex && Key <= Q8_GI; }
	// Уровни групп и пресета: подписи 0…3, «Ультра» (4) — только если уже стоит.
	static const int32 kLevelLoc[5] = { SysOptLoc::kLvlLow, SysOptLoc::kLvlMed, SysOptLoc::kLvlHigh, SysOptLoc::kLvlMax, SysOptLoc::kLvlUltra };
}

static ASDA_DEV_CVAR(int32, CVarServerFlags,
	TEXT("asda.SysOpt.ServerFlags"), 1,
	TEXT("1 = как оригинал: флаги персонажа из пакета 1027 (UPCharOption) ставятся в окно настроек; 0 = окно знает только локальный профиль."),
	ECVF_Default);

// ═══════════════════════════ БУФЕР ↔ ИГРА ↔ КОНТРОЛЫ ═══════════════════════════════════════════
// Порт `optionsWndLoadSettingsToControls@0xED1C50`: собрать буфер из ЖИВОГО состояния игры.
void UAsdaSystemOptionSubsystem::LoadStateFromGame()
{
	UGameInstance* GI = GetGameInstance();

	// ★ 19.09.2026: звук, яркость, флаги и экран — из ПРОФИЛЯ игрока (там они и живут; прежде звук
	//   читался из ключей консоли, которые могли держать чужое, консольное значение).
	LoadStateFromConfig();

	// Интерфейс: те настройки, у которых в порту уже есть ЖИВОЙ владелец.
	if (UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		State.Int3[7] = Hot->IsSecondRowVisible() ? 1 : 0;      // `g_bOptChildPairVisible`
		State.Int3[6] = Hot->bDragRequiresShift ? 1 : 0;        // `g_bHotbarRequireShift`
	}
	if (const UAsdaShowOffSubsystem* Show = GI ? GI->GetSubsystem<UAsdaShowOffSubsystem>() : nullptr)
	{
		State.Int3[2] = Show->IsWindowEnabled() ? 1 : 0;        // `g_bOptShowOtherChars`
	}
	// ★ 25.10.2026 [ОРИГИНАЛ] показ хранителей (`g_bFxShowSelfAngel` / `g_bFxShowOtherAngel` → filler[1118]/[1119],
	//   `optionsWndLoadSettingsToControls`, game_main.c:11948-11949) и шлема (живой байт своего персонажа — тот, что сейчас
	//   рисуется; эталон держит его в `bHelmetShow` окна, ставя из 1027 и по «ОК»).
	if (const UAsdaWorldSubsystem* WsLive = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		State.Int3[3] = WsLive->GetShowAngel(/*bSelf*/true) ? 1 : 0;    // `g_bFxShowSelfAngel`
		State.Int3[4] = WsLive->GetShowAngel(/*bSelf*/false) ? 1 : 0;   // `g_bFxShowOtherAngel`
		if (IsValid(WsLive->LocalPlayerActor))
		{
			State.Int3[1] = WsLive->SelfParts.bHelmetShown ? 1 : 0;      // filler[1116] = байт своего персонажа
		}
	}
	// ⚠ Облачка речи — подсистема МИРА (`UTickableWorldSubsystem`), а не GameInstance: берётся у мира.
	if (const UWorld* Wb = GI ? GI->GetWorld() : nullptr)
	{
		if (const UAsdaChatBalloonSubsystem* Bal = Wb->GetSubsystem<UAsdaChatBalloonSubsystem>())
		{
			State.D3[3] = Bal->bShowBalloons ? 1 : 0;           // `g_bShowEntityNames`
		}
	}
	if (UWorld* W = GI ? GI->GetWorld() : nullptr)
	{
		if (const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			State.Int3[5] = PC->IsOptionAlwaysRun() ? 1 : 0;       // `g_bFxAlwaysRun`
		}
	}
}

// Порт `optionsWnd_applySettingsToControls@0xED23E0`: разложить буфер по контролам окна.
void UAsdaSystemOptionSubsystem::ApplyStateToControls()
{
	for (FAsdaOptCheck& C : Checks)
	{
		if (!C.Box) { continue; }
		bool bOn = true;
		const int32 Id = C.IdNum;
		if (Id >= 70172 && Id <= 70184)      { bOn = State.Int1[(Id - 70172) / 2] != 0; }
		else if (Id >= 70262 && Id <= 70272) { bOn = State.Int2[(Id - 70262) / 2] != 0; }
		else if (Id >= 70362 && Id <= 70380) { bOn = State.Int3[(Id - 70362) / 2] != 0; }
		else if (Id >= 70102 && Id <= 70118) { bOn = State.D3[(Id - 70102) / 2] != 0; }
		else if (Id == 70012)                { bOn = State.MusicMuted != 0; }   // отмечен = перечёркнутый динамик = тишина
		else if (Id == 70015)                { bOn = State.SfxMuted != 0; }
		else if (Id == SysOptExtra::kIdBase + SysOptExtra::S4_VSync)  { bOn = State.VSync != 0; }
		else if (Id == SysOptExtra::kIdBase + SysOptExtra::E5_MB)     { bOn = State.MotionBlur != 0; }
		else if (Id == SysOptExtra::kIdBase + SysOptExtra::E7_Clouds) { bOn = State.Clouds != 0; }   // ★ 12.10.2026: E4/E6 сняты
		C.Box->SetIsChecked(bOn);
	}
	if (FAsdaOptSlider* S = FindSlider(70013)) { S->Value = State.MusicVolume; LayoutSliderThumb(*S); }
	if (FAsdaOptSlider* S = FindSlider(70016)) { S->Value = State.SfxVolume;   LayoutSliderThumb(*S); }
	if (FAsdaOptSlider* S = FindSlider(70058)) { S->Value = State.Gamma;       LayoutSliderThumb(*S); }
	if (FAsdaOptSlider* S = FindSlider(SysOptExtra::kIdBase + SysOptExtra::Q9_Draw)) { S->Value = State.DrawDistance; LayoutSliderThumb(*S); }
	if (FAsdaOptSlider* S = FindSlider(SysOptExtra::kIdBase + SysOptExtra::I2_Cursor)) { S->Value = State.CursorSpeed; LayoutSliderThumb(*S); }   // ★ 19.10.2026
	if (FAsdaOptSlider* S = FindSlider(SysOptExtra::kIdBase + SysOptExtra::I3_Camera)) { S->Value = State.CameraTurn;  LayoutSliderThumb(*S); }
	if (FlareCombo) { FlareCombo->SetSelectedIndex(FMath::Clamp(State.GlowLevel, 0, 4)); }
	SyncExtraCombos();
	if (ResolutionCombo)
	{
		for (int32 i = 0; i < ResolutionList.Num(); ++i)
		{
			if (ResolutionList[i].X == State.ScreenW && ResolutionList[i].Y == State.ScreenH)
			{
				ResolutionCombo->SetSelectedIndex(i);
				break;
			}
		}
	}
}

// Порт `optApplySettingsFromConfig@0xED1F10`: разложить буфер ПО ИГРЕ.
//   ★ ЧЕСТНО О ПОКРЫТИИ. Эталон пишет 30 с лишним глобалов рендера D3D9; у порта рендер — UE,
//   и соответствия взяты из уже утверждённой владельцем таблицы
//   `Reports/ui_system/RENDER_QUALITY_FIELDS_RE.md` («Реализация в порту»), а не придуманы.
//   Настройки, у которых в порту ПОКА нет системы-приёмника, честно перечислены в
//   `Docs/Port/PORT_PENDING_ON_SYSTEMS.md` и здесь только ХРАНЯТСЯ (не теряются).
//   ★★★ 19.09.2026: ДВЕ ЧАСТИ.
//   • ВСЕГДА (и по ОК/Применить, и по «Отмене»): то, что окно применяет НА ЛЕТУ ещё до
//     подтверждения, — громкость (полосы 70013/70016) и яркость (70058). Эталон тоже меняет их
//     сразу (`uiOptionsWindow_WndProc`, ветки 0x1117D/0x11180/0x111A9), поэтому откат обязан вернуть их
//     из снимка — иначе после «Отмены» игрок остался бы с громкостью, от которой отказался.
//   • ТОЛЬКО ПО ПОДТВЕРЖДЕНИЮ: экран, группы качества 3D, флаги «Интерфейса», пакет 4070, модалка
//     «нужен перезапуск». Эталонная ветка отката (`optionsWnd_ApplyAndSave@0xED5800`,
//     `RE/etalon/game_main.c:12181-12185`) к игре не обращается вовсе.
void UAsdaSystemOptionSubsystem::ApplyStateToGame(bool bUserConfirmed)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	UAsdaVideoSettingsSubsystem* Vid = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;

	// ── ЗВУК (живое): буфер → профиль → менеджер звука ──
	if (Profile)
	{
		Profile->MusicVolume = (float)FMath::Clamp(State.MusicVolume, 0, 256) / 256.f;
		Profile->SfxVolume   = (float)FMath::Clamp(State.SfxVolume, 0, 256) / 256.f;
		Profile->bMusicMuted = (State.MusicMuted != 0);
		Profile->bSfxMuted   = (State.SfxMuted != 0);
		Profile->ApplyAudioSettings();
	}
	// ── ЯРКОСТЬ (живое): эталон `D3D_SetGammaBrightnessRamp`; видео-подсистема кладёт её в профиль ──
	if (Vid) { Vid->SetGamma(State.Gamma); }

	if (!bUserConfirmed) { return; }

	// ── ЭКРАН: ШТАТНЫЕ поля профиля (`SetScreenResolution`/`SetFullscreenMode`/`ApplyResolutionSettings`
	//    внутри `UAsdaVideoSettingsSubsystem::ApplyResolution`).
	//    ★★★ 19.09.2026 (фаза 2, решение владельца №2): режим — ОДИН список «Полный экран / Без рамки / Окно»
	//    (0/1/2 = нумерация `ApplyResolution`). Применяется, только если игрок сменил разрешение или режим.
	//    [ПОРТ] Модалки «нужен перезапуск» здесь больше нет: оригинал (D3D9) переключал оконный режим только
	//    пересозданием устройства (`optApplySettingsFromConfig`: +1132 ≠ снимка → +1228 = 1 → loc 61), а UE
	//    меняет режим сразу — сообщение «перезапустите игру» было бы неправдой.
	if (Vid)
	{
		const FIntPoint Want(State.ScreenW, State.ScreenH);
		const FIntPoint Have = Vid->GetCurrentResolution();
		const int32 HaveMode = Vid->GetWindowModeIndex();
		const bool bModeChanged = (State.WindowMode != Snapshot.WindowMode) && (State.WindowMode != HaveMode);
		if (Want.X > 0 && Want.Y > 0 && (Want != Have || bModeChanged))
		{
			Vid->ApplyResolution(Want, bModeChanged ? FMath::Clamp(State.WindowMode, 0, 2) : HaveMode);
		}
		// ★ Фаза 2: остальные строки «Экрана» — только сменённые игроком.
		if (State.RenderScale != Snapshot.RenderScale) { Vid->SetRenderScalePercent(State.RenderScale, /*bSave*/ false); }
		if (State.FpsLimit != Snapshot.FpsLimit)       { Vid->SetFrameRateLimitFps(State.FpsLimit, /*bSave*/ false); }
		if (State.VSync != Snapshot.VSync)             { Vid->SetVSync(State.VSync != 0, /*bSave*/ false); }
	}
	// Графический режим — после перезапуска (движок выбирает RHI до старта; `[D3DRHIPreference]`). Смена =
	//   эталонная модалка типа 3 с текстом loc 61 «…Перезапустите игру. Завершить работу сейчас?».
	if (State.RhiDx12 != Snapshot.RhiDx12)
	{
		UAsdaGameUserSettings::SetPendingRHI(State.RhiDx12 != 0);
		bNeedRestartNotice = true;
	}
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): язык — в профиль; действует после перезапуска (атласы с надписями движок
	//   подменяет при загрузке пакета). Совпал с языком сессии (игрок вернул прежний) — перезапуск не нужен.
	if (State.Lang != Snapshot.Lang)
	{
		FAsdaLang::SetSaved((EAsdaLang)State.Lang, /*bSave*/ true);
		bNeedLangRestart = ((EAsdaLang)State.Lang != FAsdaLang::Current());
	}
	// ★ [ПОРТ] 18.10.2026: режим цифр урона — в поле профиля, только если игрок сменил его в окне (как прочие строки). Действует
	//   СРАЗУ: единственный читатель `UAsdaWorldSubsystem::GetDamageTextMode` берёт поле профиля при каждом показе цифры. На диск —
	//   `SaveStateToConfig` (`SaveSettings` профиля) следом за этим вызовом.
	if (Profile && State.DamageTextMode != Snapshot.DamageTextMode)
	{
		Profile->DamageTextMode = FMath::Clamp(State.DamageTextMode, 0, 2);
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] цифры урона: режим %d (0 — только мои, 1 — все, 2 — не показывать)"), Profile->DamageTextMode);
	}
	// ★ [ПОРТ] 19.10.2026: чувствительность мыши (скорость курсора) и поворота камеры — в поля профиля, только сменённые в окне.
	//   Камера читает поле при каждом повороте (`GetCameraTurnScale`) — действует сразу; курсор ставит `FAsdaCursorSpeed`.
	if (Profile && State.CameraTurn != Snapshot.CameraTurn)
	{
		Profile->CameraTurnPercent = FMath::Clamp(State.CameraTurn, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax);
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] чувствительность поворота камеры: %d %%"), Profile->CameraTurnPercent);
	}
	if (Profile && State.CursorSpeed != Snapshot.CursorSpeed)
	{
		Profile->CursorSpeedPercent = FMath::Clamp(State.CursorSpeed, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax);
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] чувствительность мыши: %d %%"), Profile->CursorSpeedPercent);
		FAsdaCursorSpeed::Refresh();
	}
	// ★ [ПОРТ] 25.10.2026: режим Ctrl приседа — в поле профиля, только сменённый в окне. Действует СРАЗУ: персонаж читает
	//   `UAsdaGameUserSettings::GetCrouchMode` каждый кадр опроса Ctrl (`AAsdaPlayerCharacter::TickCrouchInput`).
	if (Profile && State.CrouchMode != Snapshot.CrouchMode)
	{
		Profile->CrouchMode = (State.CrouchMode == UAsdaGameUserSettings::kCrouchHold) ? UAsdaGameUserSettings::kCrouchHold : UAsdaGameUserSettings::kCrouchToggle;
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] присед: %s"), Profile->CrouchMode == UAsdaGameUserSettings::kCrouchHold ? TEXT("зажатие") : TEXT("нажатие"));
	}

	// ── 3D → ГРУППЫ КАЧЕСТВА (штатные `Set*Quality` профиля + `ApplyNonResolutionSettings` + `SaveSettings`).
	//   Таблица соответствий (RENDER_QUALITY_FIELDS_RE.md, раздел «Реализация в порту»):
	//   D3[0] «Эффекты воды» и D3[5] « - Отражение в воде»  → `SetReflectionQuality`;
	//   D3[1] «Сглаживание ландшафта» и D3[2] «Прорисовка травы» → `SetFoliageQuality`;
	//   D3[4] «Шейдеры» = мастер `FXShader` → `SetPostProcessQuality`, и при ВЫКЛЮЧЕНИИ он
	//         ПРИНУДИТЕЛЬНО гасит воду и тени — ровно как `gfxApplyEffectSettings@0xECF130`;
	//   D3[6] « - Тени» → `SetShadowQuality`; D3[7] «MipFilter» → `SetTextureQuality`;
	//   D3[8] «Звёздное небо» и D3[3] «Всплывающие подсказки» рендер-группами UE не выражаются.
	//   ★★★ 19.09.2026 [ПОРТ]: группа трогается ТОЛЬКО если игрок переключил её флаг в этом окне
	//   (сравнение со снимком при открытии), а включение не ПОНИЖАЕТ действующий уровень. Прежде
	//   каждое «ОК» переписывало группы в 3 по флагам буфера — и сносило калибровку владельца
	//   (у него тени 4, эффекты 4 — `owner-build-config-not-epic`), даже если он трогал только звук.
	if (Vid)
	{
		struct FFlags { bool bWater, bShadow, bGrass, bMaster, bMip; };
		auto Derive = [](const FAsdaOptState& St) -> FFlags
		{
			const bool bM = St.D3[4] != 0;
			return { bM && (St.D3[0] != 0 || St.D3[5] != 0), bM && (St.D3[6] != 0),
			         (St.D3[1] != 0) || (St.D3[2] != 0), bM, St.D3[7] != 0 };
		};
		const FFlags Now = Derive(State), Was = Derive(Snapshot);
		const Scalability::FQualityLevels L = Scalability::GetQualityLevels();
		// Включить = оставить действующий уровень, если он не «выключен»; иначе — пресет по умолчанию
		//   (★ 12.10.2026: `kDefaultPreset` = High, умолчание первого запуска `ApplyDefaultQuality`; раньше Epic 3).
		//   Выключить = 0 (у текстур — 1, как было в таблице).
		auto OnLevel = [](int32 Cur, int32 OffLevel) { return (Cur > OffLevel) ? Cur : (int32)UAsdaVideoSettingsSubsystem::kDefaultPreset; };
		bool bChanged = false;
		if (Now.bWater != Was.bWater)   { Vid->SetReflectionQuality(Now.bWater ? OnLevel(L.ReflectionQuality, 0) : 0); bChanged = true; }
		if (Now.bShadow != Was.bShadow) { Vid->SetShadowQuality(Now.bShadow ? OnLevel(L.ShadowQuality, 0) : 0); bChanged = true; }
		if (Now.bGrass != Was.bGrass)   { Vid->SetFoliageQuality(Now.bGrass ? OnLevel(L.FoliageQuality, 0) : 0); bChanged = true; }
		if (Now.bMaster != Was.bMaster) { Vid->SetPostProcessQuality(Now.bMaster ? OnLevel(L.PostProcessQuality, 0) : 0); bChanged = true; }
		if (Now.bMip != Was.bMip)       { Vid->SetTextureQuality(Now.bMip ? OnLevel(L.TextureQuality, 1) : 1); bChanged = true; }
		if (bChanged)
		{
			Vid->CommitQuality();
			UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] 3D: группы качества применены и сохранены; %s"),
				*UAsdaVideoSettingsSubsystem::DescribeQualityLevels());
		}

		// ── ★ ФАЗА 2: СТРАНИЦА «КАЧЕСТВО» — только сменённое игроком ──
		//   «Общее качество» (если выбран в этом окне) ставит ВСЕ группы, включая скрытые в окне (шейдинг,
		//   рельеф, качество сглаживания), и не трогает масштаб рендера (`SetQualityPreset`). Затем — группы,
		//   чей уровень в буфере отличается от получившегося; они же идут ПОСЛЕ 3D-флагов выше: строка группы
		//   конкретнее флага.
		bool bGroups = false;
		int32 Base[8];
		for (int32 i = 0; i < 8; ++i) { Base[i] = Snapshot.Groups[i]; }
		if (State.PresetPick >= 0 && State.PresetPick != Snapshot.PresetPick)
		{
			Vid->SetQualityPreset((EAsdaQualityPreset)FMath::Clamp(State.PresetPick, 0, 4));
			for (int32 i = 0; i < 8; ++i) { Base[i] = State.PresetPick; }
		}
		void (UAsdaVideoSettingsSubsystem::*GroupSet[8])(int32) = {
			&UAsdaVideoSettingsSubsystem::SetTextureQuality, &UAsdaVideoSettingsSubsystem::SetShadowQuality,
			&UAsdaVideoSettingsSubsystem::SetEffectsQuality, &UAsdaVideoSettingsSubsystem::SetPostProcessQuality,
			&UAsdaVideoSettingsSubsystem::SetFoliageQuality, &UAsdaVideoSettingsSubsystem::SetViewDistanceQuality,
			&UAsdaVideoSettingsSubsystem::SetReflectionQuality, &UAsdaVideoSettingsSubsystem::SetGlobalIlluminationQuality };
		for (int32 i = 0; i < 8; ++i)
		{
			if (State.Groups[i] != Base[i]) { (Vid->*GroupSet[i])(State.Groups[i]); bGroups = true; }
		}
		if (bGroups)
		{
			Vid->CommitQuality();
			UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] «Качество»: группы применены и сохранены; %s"),
				*UAsdaVideoSettingsSubsystem::DescribeQualityLevels());
		}
		if (State.DrawDistance != Snapshot.DrawDistance)
		{
			Vid->SetRenderOverride(TEXT("asda.Cull.Scale"), FString::SanitizeFloat(State.DrawDistance / 100.f), /*bSave*/ false);
		}

		// ── ★ ФАЗА 2: СТРАНИЦА «ЭФФЕКТЫ» — общий с dev-окном путь `SetRenderOverride` (поля профиля) ──
		if (State.AAMethod != Snapshot.AAMethod)       { Vid->SetRenderOverride(TEXT("r.AntiAliasingMethod"), FString::FromInt(State.AAMethod), false); }
		if (State.SharpenX100 != Snapshot.SharpenX100) { Vid->SetRenderOverride(TEXT("r.Tonemapper.Sharpen"), FString::SanitizeFloat(State.SharpenX100 / 100.f), false); }
		// ★ 12.10.2026: анизотропия (E3), затенение окружения (E4) и блики (E6) окном больше не меняются — их ставит уровень/пресет
		//   (анизотропия — в группе «Текстуры»); поля `Aniso/AO/LensFlare` буфера читаются только для сводки. Записи здесь нет,
		//   чтобы окно никогда не трогало эти ключи профиля (поле профиля `LensFlare` и его миграция сохранены).
		if (State.MotionBlur != Snapshot.MotionBlur)   { Vid->SetRenderOverride(TEXT("r.MotionBlur.Amount"), State.MotionBlur ? TEXT("0.5") : TEXT("0"), false); }
		if (State.Clouds != Snapshot.Clouds)           { Vid->SetRenderOverride(TEXT("asda.Clouds.Field"), State.Clouds ? TEXT("1") : TEXT("0"), false); }
		// «Свечение» 70121 — один источник с dev-окном (поле `GlowLevel` профиля).
		if (State.GlowLevel != Snapshot.GlowLevel)     { Vid->SetGlowLevel(State.GlowLevel, /*bSave*/ false); }
	}

	// ── ИНТЕРФЕЙС: живые владельцы флагов ──
	ApplyInterfaceStateToGame();

	// ── ★ 25.10.2026 [ОРИГИНАЛ]: показ своего шлема — прямо перед 4070, как в эталоне (game_main.c:12035-12041) ──
	ApplyCharacterDisplayToGame();

	// ── СЕТЬ: эталон в конце этой же функции шлёт серверу флаги приёма заявок ──
	//   ★★★ РЕВИЗИЯ 2026-09-17 (С4): ТОЛЬКО ПО ПОДТВЕРЖДЕНИЮ ИГРОКА (см. шапку функции).
	SendSettingsPacket4070();

	// ── Предупреждение «нужен перезапуск» (эталон: модалка ТИПА 3 с текстом loc 61, game_main.c:12045-12047:
	//    `CItem_BuildTooltipText(g_pItemTooltipUI, 3, −1, loc61, 1)`). Тип 3 — модалка выхода из игры, поэтому
	//    «Да» = выход (`gameReturnToCharSelectTimer`), «Нет» = остаться. ★ Фаза 2: взводится сменой графического
	//    режима (DX12/DX11); прежде порт показывал однокнопочное окно — вопрос «Завершить работу сейчас?» без ответа.
	// ★ 09.10.2026 (ревизия мультиязычности МЕЛК-6): смена языка И графического режима одним «ОК» давала два вопроса
	//   подряд — «Завершить работу?» и «Перезапустить?»; согласие на первый закрывало игру без перезапуска. Перезапуск
	//   применяет оба (режим выбирается при старте процесса из `[D3DRHIPreference]`), поэтому остаётся ОДИН вопрос — о
	//   перезапуске. [ПОРТ]: в оригинале смены языка в настройках нет.
	if (bNeedRestartNotice && bNeedLangRestart)
	{
		bNeedRestartNotice = false;
	}
	if (bNeedRestartNotice)
	{
		bNeedRestartNotice = false;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (WM)
		{
			WM->ShowGameConfirm(Loc(SysOptLoc::kRestartNeeded),
				[this](bool bYes) { if (bYes) { DoExitGame(); } },
				/*GaugeGateSec*/ 0.f, AsdaLoc::kBtnOk, AsdaLoc::kBtnCancel, /*очередь: эталон (3, -1) game_main.c:12047*/-1, 3);
		}
	}
	// ★ [ПОРТ] 09.10.2026: смена языка — вопрос «Перезапустить сейчас?» (loc 990356). «ОК» — перезапуск с той же
	//   командной строкой (`FAsdaLang::RestartGame`), «Отмена» — язык сменится при следующем запуске.
	//   ★ 10.10.2026: в PIE «ОК» перезапускает СЕАНС PIE (прежде в редакторе согласие молча ничего не делало).
	if (bNeedLangRestart)
	{
		bNeedLangRestart = false;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (WM)
		{
			WM->ShowGameConfirm(LocUi(SysOptLoc::kLangRestart),
				[](bool bYes) { if (bYes) { FAsdaLang::RestartGame(); } },
				/*GaugeGateSec*/ 0.f, AsdaLoc::kBtnOk, AsdaLoc::kBtnCancel);
		}
	}
}

// Флаги вкладки «Интерфейс» (+ облачка речи 3D №3) у их ЖИВЫХ владельцев в порту.
void UAsdaSystemOptionSubsystem::ApplyInterfaceStateToGame()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hot->SetSecondRowVisible(State.Int3[7] != 0);     // `uiToggleChildPairVisible` + `g_bOptChildPairVisible`
		Hot->bDragRequiresShift = (State.Int3[6] != 0);   // `g_bHotbarRequireShift`
	}
	if (UAsdaShowOffSubsystem* Show = GI ? GI->GetSubsystem<UAsdaShowOffSubsystem>() : nullptr)
	{
		Show->SetWindowEnabled(State.Int3[2] != 0);       // `g_bOptShowOtherChars` (окно 28100)
	}
	// ★★★ 25.10.2026 [ОРИГИНАЛ] «Показывать моего / других хранителей души» (loc 30474/30475, filler[1118]/[1119]):
	//   эталон `optApplySettingsFromConfig` (game_main.c:11990-11991) кладёт их в `g_bFxShowSelfAngel/OtherAngel`, а
	//   `UpdateList_ModelAnim@0xFA7CE0` каждый кадр снимает компаньона при выключенной опции. До 25.10.2026 окно эти строки
	//   никуда не передавало — компаньон не скрывался (жалоба игроков). Опция ЛОКАЛЬНАЯ: в 4070 её байтов нет.
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 (ревизия 3.1, ключ `asda.SoulFx.SingleSource`, шапка в AsdaWorldSubsystem_Peers.cpp): источник галок —
	//   подсистема мира. Накат нового мира их не трогает (иначе буфер окна затирал бы `asda.SoulFx.Show*` и писал ini на каждой
	//   карте); «ОК» отдаёт выбор игрока, а `SetShowAngel` при том же значении ничего не пишет.
	UAsdaWorldSubsystem* WsAngel = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (WsAngel && !(GSysOptWorldApply && AsdaSoulFxSingleSourceOn()))
	{
		WsAngel->SetShowAngel(/*bSelf*/true,  State.Int3[3] != 0);
		WsAngel->SetShowAngel(/*bSelf*/false, State.Int3[4] != 0);
	}
	if (UWorld* Wb = GI ? GI->GetWorld() : nullptr)
	{
		// Облачка речи живут на подсистеме МИРА (`UTickableWorldSubsystem`).
		if (UAsdaChatBalloonSubsystem* Bal = Wb->GetSubsystem<UAsdaChatBalloonSubsystem>())
		{
			Bal->bShowBalloons = (State.D3[3] != 0);          // `g_bShowEntityNames`
		}
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wb->GetFirstPlayerController()))
		{
			PC->SetOptionAlwaysRun(State.Int3[5] != 0);       // `g_bFxAlwaysRun`
		}
	}
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("Asda.Quest.AwaitHint")))
	{
		CV->Set(State.Int2[3] != 0 ? 1 : 0, ECVF_SetByCode);   // `g_bOptFlag_15CB4E8` (чекбокс 70268)
	}
}

// ★★★ Порт `Config_WriteSettings(&g_hMainWnd, "system\\config.txt")` — ШТАТНЫМ путём UE:
//   буфер → поля профиля игрока `UAsdaGameUserSettings` → `SaveSettings` (он же сохраняет штатные
//   поля экрана и группы качества). Прежние строки `GConfig` в `[AsdaOptions]` сняты.
void UAsdaSystemOptionSubsystem::SaveStateToConfig() const
{
	UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	if (!Profile)
	{
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("[настройки] профиля игрока нет — сохранять некуда"));
		return;
	}
	auto PutArr = [](TArray<uint8>& Dst, const uint8* Src, int32 N)
	{
		Dst.SetNum(N);
		for (int32 i = 0; i < N; ++i) { Dst[i] = Src[i] ? 1 : 0; }
	};
	PutArr(Profile->InterfaceCommunity, State.Int1, UAsdaGameUserSettings::kNumCommunity);
	PutArr(Profile->InterfaceNames,     State.Int2, UAsdaGameUserSettings::kNumNames);
	PutArr(Profile->InterfaceOther,     State.Int3, UAsdaGameUserSettings::kNumOther);
	PutArr(Profile->Render3D,           State.D3,   UAsdaGameUserSettings::kNum3D);
	Profile->MusicVolume = (float)FMath::Clamp(State.MusicVolume, 0, 256) / 256.f;
	Profile->SfxVolume   = (float)FMath::Clamp(State.SfxVolume, 0, 256) / 256.f;
	Profile->bMusicMuted = (State.MusicMuted != 0);
	Profile->bSfxMuted   = (State.SfxMuted != 0);
	Profile->Gamma       = FMath::Clamp(State.Gamma, 1, 100);
	// ★ Фаза 2: «Свечение» и вынесенные ручки уже лежат в полях профиля — их записал общий путь
	//   (`SetGlowLevel`, `SetRenderOverride`, штатные сеттеры) ТОЛЬКО для сменённых строк. Здесь их не
	//   переписываем: запись «всего буфера» сбила бы калибровку владельца, которой он в окне не касался.
	Profile->SaveSettings();
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] профиль игрока сохранён: %s"), *Profile->Describe());
}

// Поля профиля → буфер окна. Экран — ШТАТНЫЕ поля родителя (разрешение и оконный режим).
void UAsdaSystemOptionSubsystem::LoadStateFromConfig()
{
	UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	if (!Profile) { return; }
	auto GetArr = [](const TArray<uint8>& Src, uint8* Dst, int32 N)
	{
		for (int32 i = 0; i < N && i < Src.Num(); ++i) { Dst[i] = Src[i] ? 1 : 0; }
	};
	GetArr(Profile->InterfaceCommunity, State.Int1, UAsdaGameUserSettings::kNumCommunity);
	GetArr(Profile->InterfaceNames,     State.Int2, UAsdaGameUserSettings::kNumNames);
	GetArr(Profile->InterfaceOther,     State.Int3, UAsdaGameUserSettings::kNumOther);
	GetArr(Profile->Render3D,           State.D3,   UAsdaGameUserSettings::kNum3D);
	State.MusicVolume = FMath::Clamp(FMath::RoundToInt(Profile->MusicVolume * 256.f), 0, 256);
	State.SfxVolume   = FMath::Clamp(FMath::RoundToInt(Profile->SfxVolume * 256.f), 0, 256);
	State.MusicMuted  = Profile->bMusicMuted ? 1 : 0;
	State.SfxMuted    = Profile->bSfxMuted ? 1 : 0;
	State.Gamma       = FMath::Clamp(Profile->Gamma, 1, 100);
	State.GlowLevel   = FMath::Clamp(Profile->GlowLevel, 0, 4);
	const FIntPoint R = Profile->GetScreenResolution();
	if (R.X > 0 && R.Y > 0) { State.ScreenW = R.X; State.ScreenH = R.Y; }
	const EWindowMode::Type WM = Profile->GetFullscreenMode();
	State.WindowMode = (WM == EWindowMode::Fullscreen) ? 0 : (WM == EWindowMode::WindowedFullscreen) ? 1 : 2;

	// ── ★ Фаза 2: вынесенные ручки — ДЕЙСТВУЮЩИЕ значения (профиль, иначе проект/уровень), чтобы окно
	//    показывало то, что сейчас на экране, а «ОК» без изменений ничего не переписывал. ──
	auto CVarF = [](const TCHAR* N, float Def) { IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(N); return CV ? CV->GetFloat() : Def; };
	auto CVarI = [](const TCHAR* N, int32 Def) { IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(N); return CV ? CV->GetInt() : Def; };
	State.RenderScale = Profile->GetRenderScalePercent();   // ★ 19.09.2026: 10…200 % (выше 100 — суперсэмплинг)
	State.FpsLimit    = FMath::Max(0, FMath::RoundToInt(Profile->GetFrameRateLimit()));
	State.VSync       = Profile->IsVSyncEnabled() ? 1 : 0;
	State.RhiDx12     = UAsdaGameUserSettings::GetPendingRHIIsDx12() ? 1 : 0;
	{
		// ★ 09.10.2026: язык — выбор игрока в профиле (может ждать перезапуска), иначе язык сессии.
		EAsdaLang L = FAsdaLang::Current();
		FAsdaLang::GetSaved(L);
		// ★ 09.10.2026 (ревизия мультиязычности МЕЛК-4): сессия с неактивным языком (ключ `-AsdaLang=ar`) не выводит пункт
		//   за пределы списка из двух строк — показываем английский (цепочка текста такой сессии и так идёт в EN).
		if (!FAsdaLang::IsSelectable(L)) { L = EAsdaLang::En; }
		State.Lang = (uint8)L;
	}
	State.DamageTextMode = FMath::Clamp(Profile->DamageTextMode, 0, 2);   // ★ 18.10.2026: строка «Цифры урона»
	State.CursorSpeed = FMath::Clamp(Profile->CursorSpeedPercent, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax);   // ★ 19.10.2026
	State.CameraTurn  = FMath::Clamp(Profile->CameraTurnPercent, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax);
	State.CrouchMode  = UAsdaGameUserSettings::GetCrouchMode();   // ★ 25.10.2026: строка «Присед»
	State.Groups[0] = Profile->GetTextureQuality();
	State.Groups[1] = Profile->GetShadowQuality();
	State.Groups[2] = Profile->GetVisualEffectQuality();
	State.Groups[3] = Profile->GetPostProcessingQuality();
	State.Groups[4] = Profile->GetFoliageQuality();
	State.Groups[5] = Profile->GetViewDistanceQuality();
	State.Groups[6] = Profile->GetReflectionQuality();
	State.Groups[7] = Profile->GetGlobalIlluminationQuality();
	State.PresetPick  = -1;
	State.DrawDistance = FMath::Clamp(FMath::RoundToInt(CVarF(TEXT("asda.Cull.Scale"), 1.f) * 100.f), 50, 200);
	State.AAMethod    = CVarI(TEXT("r.AntiAliasingMethod"), 1);
	State.SharpenX100 = FMath::RoundToInt(CVarF(TEXT("r.Tonemapper.Sharpen"), 1.2f) * 100.f);
	// ★ 12.10.2026: Aniso/AO/LensFlare — строк в окне нет; значения читаются только для сводки `asda.SysOpt.Dump`.
	State.Aniso       = CVarI(TEXT("r.MaxAnisotropy"), 8);
	State.AO          = (CVarI(TEXT("r.AmbientOcclusionLevels"), -1) != 0) ? 1 : 0;
	State.MotionBlur  = (CVarF(TEXT("r.MotionBlur.Amount"), 0.f) > 0.f) ? 1 : 0;
	State.LensFlare   = (Profile->LensFlare > 0) ? 1 : 0;   // «не менялось» = проект: бликов нет (сила 0)
	State.Clouds      = (CVarI(TEXT("asda.Clouds.Field"), 1) != 0) ? 1 : 0;
}

// ★★★ 25.10.2026 [ОРИГИНАЛ] Хвост `optApplySettingsFromConfig@0xED1F10`: `LocalPlayer+0x32F78 = filler[1116]` и
//   `CCharacter_ApplyEquippedHelmet(LocalPlayer)`. Сосед узнает о смене от сервера (4070 → 4025 действие 111).
void UAsdaSystemOptionSubsystem::ApplyCharacterDisplayToGame()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		WS->ApplySelfHelmetShow(State.Int3[1] != 0, TEXT("окно настроек «ОК»"));
	}
}

// ★★★ 25.10.2026 [ОРИГИНАЛ] Порт `optionsApplyServerFlags@0xED1B40` — разбор у объявления. Байт блока k = байт k тела 4070.
void UAsdaSystemOptionSubsystem::ApplyServerOptionFlags(const TArray<uint8>& Block19)
{
	if (CVarServerFlags.GetValueOnGameThread() == 0) { return; }
	if (Block19.Num() < 19)
	{
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("[настройки] блок опций 1027: %d б < 19 — не применён"), Block19.Num());
		return;
	}
	auto B = [&Block19](int32 k) -> uint8 { return Block19[k] != 0 ? 1 : 0; };
	// Обратная раскладка `SendSettingsPacket4070` (= `apKeyCat7Label[41…47]`, `bSrvFlag1112/1113`, `bHelmetShow`).
	State.Int1[0] = B(7);    // filler[1105] «Предложить Дружбу»                 — HIBYTE(a3)
	State.Int1[1] = B(9);    // filler[1106] «Предложение вступить в отряд»     — BYTE1(a4)
	State.Int1[2] = B(10);   // filler[1107] «Предложение вступить в клан»      — BYTE2(a4)
	State.Int1[3] = B(11);   // filler[1108] «Предложение торговли»             — HIBYTE(a4)
	State.Int1[4] = B(12);   // filler[1109] «Предложение обмена личными вещами» — (BYTE)a5
	State.Int1[5] = B(5);    // filler[1110] «Услышанный шепот»                 — BYTE1(a3)
	State.Int1[6] = B(8);    // filler[1111] «Предложение стать приятелями»     — (BYTE)a4
	State.Int2[0] = B(13);   // filler[1112] «Показывать ОЗ монстра»            — BYTE1(a5)
	State.Int2[1] = B(14);   // filler[1113] «Показывать мое имя и ОЗ»          — BYTE2(a5)
	State.Int3[1] = B(15);   // filler[1116] «Показывать шлем»                  — HIBYTE(a5)
	// Поля профиля — в памяти (окно при открытии читает буфер из профиля); на диск — только по «ОК».
	if (UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get())
	{
		auto Put = [](TArray<uint8>& Dst, const uint8* Src, int32 N)
		{
			Dst.SetNum(N);
			for (int32 i = 0; i < N; ++i) { Dst[i] = Src[i] ? 1 : 0; }
		};
		Put(Profile->InterfaceCommunity, State.Int1, UAsdaGameUserSettings::kNumCommunity);
		Put(Profile->InterfaceNames,     State.Int2, UAsdaGameUserSettings::kNumNames);
		Put(Profile->InterfaceOther,     State.Int3, UAsdaGameUserSettings::kNumOther);
	}
	// Эталон ставит и байт локального персонажа, если тот уже есть (без пересборки: шлем подтянет следующий
	//   `LoadHelmetPart`). 1027 приходит на выборе персонажа, своего в мире ещё нет — ветка для полноты.
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		if (IsValid(WS->LocalPlayerActor)) { WS->SelfParts.bHelmetShown = (State.Int3[1] != 0); }
	}
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] опции персонажа с сервера (1027): заявки=%d%d%d%d%d%d%d имена=%d%d шлем=%d"),
		State.Int1[0], State.Int1[1], State.Int1[2], State.Int1[3], State.Int1[4], State.Int1[5], State.Int1[6],
		State.Int2[0], State.Int2[1], State.Int3[1]);
}

int32 UAsdaSystemOptionSubsystem::GetInterfaceFlag(int32 Group, int32 Idx) const
{
	if (Group == 1 && Idx >= 0 && Idx < 7)  { return State.Int1[Idx]; }
	if (Group == 2 && Idx >= 0 && Idx < 6)  { return State.Int2[Idx]; }
	if (Group == 3 && Idx >= 0 && Idx < 10) { return State.Int3[Idx]; }
	return -1;
}

void UAsdaSystemOptionSubsystem::DebugConfirmCharacterDisplay(int32 Group, int32 Idx, bool bOn)
{
	uint8* Arr = (Group == 1) ? State.Int1 : (Group == 2) ? State.Int2 : (Group == 3) ? State.Int3 : nullptr;
	const int32 N = (Group == 1) ? 7 : (Group == 2) ? 6 : 10;
	if (!Arr || Idx < 0 || Idx >= N) { return; }
	Arr[Idx] = bOn ? 1 : 0;
	// То, что «ОК» делает для показа персонажа: хранители (общий накат «Интерфейса»), свой шлем и пакет 4070.
	ApplyInterfaceStateToGame();
	ApplyCharacterDisplayToGame();
	SendSettingsPacket4070();
}

// Порт `uiOptionsSendSettingsPacket2@0xED69E0` — опкод 4070, тело 19 байт, конверт
//   `[0..15] MD5(payload) + [16..34] payload` = `UAsdaNetSubsystem::SendMd5Signed`.
void UAsdaSystemOptionSubsystem::SendSettingsPacket4070() const
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	uint8 Body[19];
	FMemory::Memzero(Body, sizeof(Body));
	// Первые пять байт эталон ставит в 1 безусловно (ui_core.c:89302-89307).
	Body[0] = Body[1] = Body[2] = Body[3] = Body[4] = 1;
	Body[5]  = State.Int1[5];   // filler[1110] — «Услышанный шепот»
	// ★ Дефект Д5: эталон читает сюда `filler[1104]`, в который никто не пишет. Шлём 0.
	Body[6]  = 0;
	Body[7]  = State.Int1[0];   // filler[1105] — «Предложить Дружбу»
	Body[8]  = State.Int1[6];   // filler[1111] — «Предложение стать приятелями»
	Body[9]  = State.Int1[1];   // filler[1106] — «Предложение вступить в отряд»
	Body[10] = State.Int1[2];   // filler[1107] — «Предложение вступить в клан»
	Body[11] = State.Int1[3];   // filler[1108] — «Предложение торговли»
	Body[12] = State.Int1[4];   // filler[1109] — «Предложение обмена личными вещами»
	Body[13] = State.Int2[0];   // filler[1112] — «Показывать ОЗ монстра»
	Body[14] = State.Int2[1];   // filler[1113] — «Показывать мое имя и ОЗ»
	Body[15] = State.Int3[1];   // filler[1116] — «Показывать шлем»
	// [16..18] эталон не заполняет (остаются нулями после `sub_924E32`).
	const bool bOk = Net->SendOptionFlags4070(Body);
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] 4070 (флаги приёма заявок) -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
}

// Найти в ассете клоны новых строк и подключить их. Разметку не создаём — только подписи и обработчики.
void UAsdaSystemOptionSubsystem::SetupExtraControls()
{
	if (!OptWidget) { return; }
	for (FAsdaOptExtraCombo& X : ExtraCombos) { if (X.Combo) { X.Combo->Reset(); } }
	ExtraCombos.Reset();
	AutoDetectBtn = nullptr;
	int32 Missing = 0;
	for (const SysOptExtra::FRowDef& R : SysOptExtra::kRows)
	{
		// Подпись строки (у флажков её ставит общая таблица чекбоксов, у кнопки — `_BTN_Text`).
		if (R.Kind != SysOptExtra::Check)
		{
			const FString Node = (R.Kind == SysOptExtra::Button) ? SysOptExtra::NodeName(R.Tag, TEXT("BTN_Text"))
			                                                      : SysOptExtra::NodeName(R.Tag, TEXT("TXT"));
			if (UTextBlock* T = Cast<UTextBlock>(OptWidget->GetWidgetFromName(FName(*Node))))
			{
				T->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
				const FString S = LocUi(R.LabelLoc);
				if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
			}
			else
			{
				++Missing;
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("[настройки] нет подписи «%s» — не прогнан шаг ассета asda_ui_option_extra_rows?"), *Node);
			}
		}
		if (R.Kind == SysOptExtra::Combo)
		{
			const FString Idc = SysOptExtra::NodeName(R.Tag, TEXT("CMB"));
			UWidget* W = OptWidget->GetWidgetFromName(FName(*Idc));
			UAsdaJuiComboBox* Box = W ? NewObject<UAsdaJuiComboBox>(this) : nullptr;
			if (!Box || !Box->Setup(OptWidget, Idc, kOptionWindowId, /*bOpenUp*/ false, SysOptPlaneIdcOf(W)))
			{
				++Missing;
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("[настройки] список «%s» не подключён"), *Idc);
				continue;
			}
			FAsdaOptExtraCombo X;
			X.Key = R.Key; X.Tab = R.Tab; X.Page = R.Page; X.Combo = Box;
			const int32 Key = R.Key;
			Box->OnPicked.BindLambda([this, Key](int32 Idx)
			{
				for (const FAsdaOptExtraCombo& C : ExtraCombos)
				{
					if (C.Key == Key && C.Values.IsValidIndex(Idx)) { OnExtraPicked(Key, C.Values[Idx]); break; }
				}
			});
			ExtraCombos.Add(X);
		}
		else if (R.Kind == SysOptExtra::Button)
		{
			AutoDetectBtn = Cast<UButton>(OptWidget->GetWidgetFromName(FName(*SysOptExtra::NodeName(R.Tag, TEXT("BTN")))));
			if (!AutoDetectBtn) { ++Missing; continue; }
			UAsdaButtonRelay* Rel = NewObject<UAsdaButtonRelay>(this);
			Rel->Callback = [this](int32) { OnAutoDetect(); };
			AutoDetectBtn->OnClicked.Clear();
			AutoDetectBtn->OnClicked.AddDynamic(Rel, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, Rel, AutoDetectBtn);   // ★ 31.10.2026: подписка уже сделана — якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
	}
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] фаза 2: новых списков %d, кнопка «Автоподбор» %s, не найдено узлов %d"),
		ExtraCombos.Num(), AutoDetectBtn ? TEXT("есть") : TEXT("НЕТ"), Missing);
}

// Состав списков. Собирается при открытии окна: часть пунктов зависит от текущих значений.
void UAsdaSystemOptionSubsystem::RebuildExtraComboItems()
{
	using namespace SysOptExtra;
	for (FAsdaOptExtraCombo& X : ExtraCombos)
	{
		if (!X.Combo) { continue; }
		TArray<FString> Labels;
		X.Values.Reset();
		auto AddLoc = [&](int32 LocId, int32 V) { Labels.Add(LocUi(LocId)); X.Values.Add(V); };
		auto AddTxt = [&](const FString& S, int32 V) { Labels.Add(S); X.Values.Add(V); };
		// Действующее значение вне стандартного ряда (задано консолью/уровнем) показывается отдельным пунктом —
		//   честно, а не «ближайшим»: иначе окно показало бы не то, что на экране.
		auto KeepCurrent = [&](int32 Cur, const FString& S) { if (!X.Values.Contains(Cur)) { AddTxt(S, Cur); } };
		switch (X.Key)
		{
		case S1_Mode:
			AddLoc(SysOptLoc::kFullscreenLoc, 0); AddLoc(SysOptLoc::kBorderless, 1); AddLoc(SysOptLoc::kWindowed, 2);
			break;
		case S2_Scale:
			// ★ 19.09.2026: и выше 100 % — суперсэмплинг, как было в Ctrl+N (125/150) и сверх того.
			for (int32 P : { 50, 60, 70, 80, 90, 100, 125, 150, 175, 200 }) { AddTxt(FString::Printf(TEXT("%d %%"), P), P); }
			KeepCurrent(State.RenderScale, FString::Printf(TEXT("%d %%"), State.RenderScale));
			break;
		case S3_Fps:
			AddLoc(SysOptLoc::kNoLimit, 0);
			for (int32 F : { 30, 60, 120, 144, 240 }) { AddTxt(FString::FromInt(F), F); }
			KeepCurrent(State.FpsLimit, FString::FromInt(State.FpsLimit));
			break;
		case S5_Api:
			AddLoc(SysOptLoc::kDx12, 1); AddLoc(SysOptLoc::kDx11, 0);
			break;
		case S7_Lang:
			// ★ 09.10.2026: только выбираемые языки (арабский пока неактивен — решение владельца), порядок окна первого
			//   запуска: English, Русский. Значение пункта — `EAsdaLang`.
			AddLoc(SysOptLoc::kLangEn, (int32)EAsdaLang::En); AddLoc(SysOptLoc::kLangRu, (int32)EAsdaLang::Ru);
			break;
		case I1_DmgText:
			// ★ 18.10.2026: три режима поля `DamageTextMode` в порядке значений (разбор режимов — у поля профиля).
			AddLoc(SysOptLoc::kDmgMine, 0); AddLoc(SysOptLoc::kDmgAll, 1); AddLoc(SysOptLoc::kDmgOff, 2);
			break;
		case I4_Crouch:
			// ★ 25.10.2026: два режима поля `CrouchMode` в порядке значений (разбор — у поля профиля).
			AddLoc(SysOptLoc::kCrouchToggle, UAsdaGameUserSettings::kCrouchToggle); AddLoc(SysOptLoc::kCrouchHold, UAsdaGameUserSettings::kCrouchHold);
			break;
		case Q0_Preset:
			for (int32 i = 0; i < 4; ++i) { AddLoc(kLevelLoc[i], i); }
			if (OpenSingleLevel == 4) { AddLoc(kLevelLoc[4], 4); }   // «Ультра» (Cinematic) — только если уже стоит
			AddLoc(SysOptLoc::kLvlCustom, -1);                       // «Своё» — группы вразнобой (пункт-состояние)
			break;
		case E1_AA:
			AddLoc(SysOptLoc::kOff, 0); AddLoc(SysOptLoc::kFXAA, 1); AddLoc(SysOptLoc::kTAA, 2); AddLoc(SysOptLoc::kTSR, 4);
			KeepCurrent(State.AAMethod, FString::FromInt(State.AAMethod));
			break;
		case E2_Sharpen:
			AddLoc(SysOptLoc::kWeak, 60); AddLoc(SysOptLoc::kMedium, 90); AddLoc(SysOptLoc::kStrong, 120);
			KeepCurrent(State.SharpenX100, FString::Printf(TEXT("%.2f"), State.SharpenX100 / 100.f));
			break;
		// ★ 12.10.2026: списка «Анизотропия» (E3) в окне нет — ветка снята.
		default:
			if (IsGroupKey(X.Key))
			{
				const int32 G = X.Key - Q1_Tex;
				for (int32 i = 0; i < 4; ++i) { AddLoc(kLevelLoc[i], i); }
				if (OpenGroupLevel[G] == 4) { AddLoc(kLevelLoc[4], 4); }
			}
			break;
		}
		X.Combo->SetItems(Labels);
	}
	SyncExtraCombos();
}

// Выбранные пункты ← буфер.
void UAsdaSystemOptionSubsystem::SyncExtraCombos()
{
	using namespace SysOptExtra;
	for (FAsdaOptExtraCombo& X : ExtraCombos)
	{
		if (!X.Combo) { continue; }
		int32 V = 0;
		switch (X.Key)
		{
		case S1_Mode:    V = State.WindowMode; break;
		case S2_Scale:   V = State.RenderScale; break;
		case S3_Fps:     V = State.FpsLimit; break;
		case S5_Api:     V = State.RhiDx12 ? 1 : 0; break;
		case S7_Lang:    V = State.Lang; break;
		case I1_DmgText: V = State.DamageTextMode; break;
		case I4_Crouch:  V = State.CrouchMode; break;   // ★ 25.10.2026
		case E1_AA:      V = State.AAMethod; break;
		case E2_Sharpen: V = State.SharpenX100; break;
		case Q0_Preset:
		{
			// Пресет показывается, только если ВСЕ группы в одном уровне: выбран игроком в этом окне либо уже
			//   стоял при открытии (включая скрытые группы). Иначе — «Своё».
			bool bSame = true;
			for (int32 i = 1; i < 8; ++i) { bSame &= (State.Groups[i] == State.Groups[0]); }
			const int32 G = State.Groups[0];
			V = (bSame && (State.PresetPick == G || (State.PresetPick < 0 && OpenSingleLevel == G))) ? G : -1;
			break;
		}
		default:
			if (IsGroupKey(X.Key)) { V = State.Groups[X.Key - Q1_Tex]; }
			break;
		}
		X.Combo->SetSelectedIndex(X.Values.IndexOfByKey(V));
	}
}

// Пункт выбран в одном из новых списков → буфер (в игру — по «ОК/Применить»).
void UAsdaSystemOptionSubsystem::OnExtraPicked(int32 Key, int32 V)
{
	using namespace SysOptExtra;
	// Уровни групп ↔ 3D-флажки оригинала (та же таблица, что в `ApplyStateToGame`, в обратную сторону):
	//   текстуры → «Трилинейная фильтрация» (выкл = уровень 1), тени → « - Тени», растительность → «Трава»,
	//   отражения → «Эффекты воды» и « - Отражения в воде». Постобработка НЕ связывается: её флаг — мастер
	//   эффект-пути, выключение которого по оригиналу гасит ещё и воду с тенями (`gfxApplyEffectSettings`).
	auto SyncFlags = [this](int32 G, int32 Level)
	{
		switch (G)
		{
		case 0: State.D3[7] = (Level > 1) ? 1 : 0; break;
		case 1: State.D3[6] = (Level > 0) ? 1 : 0; break;
		case 4: State.D3[2] = (Level > 0) ? 1 : 0; break;
		case 6: State.D3[0] = State.D3[5] = (Level > 0) ? 1 : 0; break;
		default: break;
		}
	};
	switch (Key)
	{
	case S1_Mode:    State.WindowMode = FMath::Clamp(V, 0, 2); break;
	case S2_Scale:   State.RenderScale = FMath::Clamp(V, UAsdaGameUserSettings::kRenderScaleMin, UAsdaGameUserSettings::kRenderScaleMax); break;
	case S3_Fps:     State.FpsLimit = FMath::Max(0, V); break;
	case S5_Api:     State.RhiDx12 = V ? 1 : 0; break;
	case S7_Lang:    { EAsdaLang L = (EAsdaLang)FMath::Clamp(V, 0, 2); if (FAsdaLang::IsSelectable(L)) { State.Lang = (uint8)L; } } break;
	case I1_DmgText: State.DamageTextMode = FMath::Clamp(V, 0, 2); break;   // ★ 18.10.2026: в профиль — по «ОК/Применить»
	case I4_Crouch:  State.CrouchMode = (V == UAsdaGameUserSettings::kCrouchHold) ? UAsdaGameUserSettings::kCrouchHold : UAsdaGameUserSettings::kCrouchToggle; break;   // ★ 25.10.2026: в профиль — по «ОК/Применить»
	case E1_AA:      State.AAMethod = V; break;
	case E2_Sharpen: State.SharpenX100 = V; break;
	case Q0_Preset:
		if (V < 0) { SyncExtraCombos(); return; }   // «Своё» — не выбор, а состояние
		State.PresetPick = FMath::Clamp(V, 0, 4);
		for (int32 i = 0; i < 8; ++i) { State.Groups[i] = State.PresetPick; SyncFlags(i, State.PresetPick); }
		// Нотис: пресет задаёт ВСЕ группы разом (план фазы 1, риск 2) — один раз за открытие окна.
		if (!bPresetWarned)
		{
			AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::System, SysOptLoc::kPresetWarn);
			bPresetWarned = true;
		}
		break;
	default:
		if (IsGroupKey(Key))
		{
			const int32 G = Key - Q1_Tex;
			State.Groups[G] = FMath::Clamp(V, 0, 4);
			SyncFlags(G, State.Groups[G]);
		}
		break;
	}
	ApplyStateToControls();   // пересчитать галочки 3D, пункт «Общее качество» и прочее по буферу
}

// ★ Кнопка «Автоподбор». ★ 12.10.2026 (решение владельца): основной путь — замер НАСТОЯЩЕГО кадра на месте
//   (`UAsdaBenchSubsystem::StartInPlace`: модальное окно хода, применение по «ОК», «Пропустить» возвращает прежнее).
//   Запасной путь (замер не запустился): штатный синтетический замер движка `Scalability::BenchmarkQualityLevels`
//   (SynthBenchmark, пороги PerfIndexThresholds_*) БЕЗ записи в профиль — рекомендации подставляются в строки страницы
//   и уходят по «ОК/Применить»; прямой вызов, а не `RunHardwareBenchmark`, чтобы «Отмена» не меняла НИЧЕГО (тот пишет
//   LastCPU/GPUBenchmarkResult в профиль).
void UAsdaSystemOptionSubsystem::OnAutoDetect()
{
	// ★★★ 12.10.2026 (решение владельца): «Автоподбор» = замер настоящего кадра на месте (`UAsdaBenchSubsystem::StartInPlace`)
	//   с модальным окном хода и применением по «ОК»; окно настроек закрывается без сохранения своего буфера (замер пишет
	//   профиль сам). Прежний путь — синтетический пробник движка — остаётся запасным, если замер не запустился.
	if (UAsdaBenchSubsystem* Bench = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBenchSubsystem>() : nullptr)
	{
		UWorld* W = GetGameInstance()->GetWorld();
		if (W && !Bench->IsRunning())
		{
			HideMenu();
			CloseOptions(false);
			if (Bench->StartInPlace(W))
			{
				UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] «Автоподбор»: запущен замер кадра на месте (окно замера)"));
				return;
			}
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("[настройки] «Автоподбор»: замер на месте не запустился — запасной пробник движка"));
		}
	}
	const Scalability::FQualityLevels Rec = Scalability::BenchmarkQualityLevels(/*WorkScale*/ 10, 1.f, 1.f);
	if (!(Rec.CPUBenchmarkResults > 0.f) || !(Rec.GPUBenchmarkResults > 0.f))
	{
		AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::System, SysOptLoc::kAutoFail);
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("[настройки] «Автоподбор»: пробник не дал индексов — уровни не тронуты"));
		return;
	}
	const int32 G[8] = { Rec.TextureQuality, Rec.ShadowQuality, Rec.EffectsQuality, Rec.PostProcessQuality,
	                     Rec.FoliageQuality, Rec.ViewDistanceQuality, Rec.ReflectionQuality, Rec.GlobalIlluminationQuality };
	State.PresetPick = -1;
	for (int32 i = 0; i < 8; ++i)
	{
		OnExtraPicked(SysOptExtra::Q1_Tex + i, FMath::Clamp(G[i], 0, 4));
	}
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::System, SysOptLoc::kAutoDone);
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] «Автоподбор»: CPU %.1f GPU %.1f → группы %d%d%d%d%d%d%d%d (применятся по «ОК»)"),
		Rec.CPUBenchmarkResults, Rec.GPUBenchmarkResults, G[0], G[1], G[2], G[3], G[4], G[5], G[6], G[7]);
}

void UAsdaSystemOptionSubsystem::CloseAllCombos()
{
	if (ResolutionCombo && ResolutionCombo->IsOpen()) { ResolutionCombo->SetOpen(false); }
	if (FlareCombo && FlareCombo->IsOpen())           { FlareCombo->SetOpen(false); }
	for (FAsdaOptExtraCombo& X : ExtraCombos) { if (X.Combo && X.Combo->IsOpen()) { X.Combo->SetOpen(false); } }
}

bool UAsdaSystemOptionSubsystem::IsAnyComboOpen() const
{
	if (ResolutionCombo && ResolutionCombo->IsOpen()) { return true; }
	if (FlareCombo && FlareCombo->IsOpen())           { return true; }
	for (const FAsdaOptExtraCombo& X : ExtraCombos) { if (X.Combo && X.Combo->IsOpen()) { return true; } }
	return false;
}

// Эталонные нотисы захвата идут в чат КРАСНЫМ с TTL 10 с (`CChat_AddTokenizedLines(txt, −65536, 10000, 0)`).
void UAsdaSystemOptionSubsystem::ChatNotice(int32 LocId) const
{
	const FString S = Loc(LocId);
	if (S.IsEmpty()) { return; }
	AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, S, FColor(255, 0, 0, 255), 10000.f);
}

FString UAsdaSystemOptionSubsystem::DebugState() const
{
	FString S = TEXT("=== SYSTEM_MenuWIN_NEW 69400 / SYSTEM_Option_NEW 69500 ===\n");
	S += FString::Printf(TEXT("  ESC-меню: %s (виджет %s)\n"),
		IsMenuShown() ? TEXT("открыто") : TEXT("закрыто"), MenuWidget ? TEXT("есть") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  Настройки: %s (виджет %s), вкладка %d, под-страницы %d/%d/%d\n"),
		IsOptionsShown() ? TEXT("открыты") : TEXT("закрыты"), OptWidget ? TEXT("есть") : TEXT("НЕТ"),
		CurrentTab, CurrentPage[0], CurrentPage[1], CurrentPage[2]);
	int32 BoundChecks = 0;
	for (const FAsdaOptCheck& C : Checks) { if (C.Box) { ++BoundChecks; } }
	S += FString::Printf(TEXT("  Строк клавиш: %d, чекбоксов привязано: %d из %d, полос: %d\n"),
		HotkeyRows.Num(), BoundChecks, Checks.Num(), Sliders.Num());
	S += FString::Printf(TEXT("  Захват клавиши: %s (действие %d), прокрутка %d/%d\n"),
		bCapturing ? TEXT("идёт") : TEXT("нет"), CaptureAction, HotkeyScroll, HotkeyScrollMax);
	// Гейт ESC — чтобы «меню не открывается» сразу читалось причиной, а не искалось в Verbose-логе.
	{
		FString Why;
		const bool bBlocked = IsEscMenuGateBlocked(Why);
		S += FString::Printf(TEXT("  Гейт ESC (asda.SysOpt.EscGate=%d): %s\n"),
			CVarEscGate.GetValueOnGameThread(),
			bBlocked ? *FString::Printf(TEXT("ЗАКРЫТ — %s"), *Why) : TEXT("открыт"));
	}
	S += FString::Printf(TEXT("  Звук: музыка %s %d/256, эффекты %s %d/256\n"),
		State.MusicMuted ? TEXT("выкл") : TEXT("вкл"), State.MusicVolume,
		State.SfxMuted ? TEXT("выкл") : TEXT("вкл"), State.SfxVolume);
	static const TCHAR* ModeNames[3] = { TEXT("полный экран"), TEXT("без рамки"), TEXT("окно") };
	S += FString::Printf(TEXT("  Экран: %dx%d, %s, яркость %d, свечение %d, масштаб %d%%, лимит %d, vsync %d, %s\n"),
		State.ScreenW, State.ScreenH, ModeNames[FMath::Clamp(State.WindowMode, 0, 2)],
		State.Gamma, State.GlowLevel, State.RenderScale, State.FpsLimit, State.VSync, State.RhiDx12 ? TEXT("DX12") : TEXT("DX11"));
	S += FString::Printf(TEXT("  Качество: группы %d%d%d%d%d%d%d%d пресет %d дальность %d%% | Эффекты: AA %d резкость %.2f размытие %d облака %d | только чтение (строк нет с 12.10.2026): аниз %d AO %d блики %d\n"),
		State.Groups[0], State.Groups[1], State.Groups[2], State.Groups[3], State.Groups[4], State.Groups[5], State.Groups[6], State.Groups[7],
		State.PresetPick, State.DrawDistance, State.AAMethod, State.SharpenX100 / 100.f, State.MotionBlur, State.Clouds, State.Aniso, State.AO, State.LensFlare);
	S += FString::Printf(TEXT("  Новых списков %d, полос %d\n"), ExtraCombos.Num(), Sliders.Num());
	{
		// ★ 12.10.2026: состав строк фазы 2 в ассете — что есть, чего нет, не остались ли снятые.
		int32 Missing = 0, Stale = 0;
		S += FString::Printf(TEXT("  Строки фазы 2: %s\n"), *SysOptRowsAudit(OptWidget, Missing, Stale));
	}
	S += TEXT("  3D: ");
	for (int32 i = 0; i < 9; ++i) { S += State.D3[i] ? TEXT("1") : TEXT("0"); }
	S += TEXT("\n  Интерфейс 1/2/3: ");
	for (int32 i = 0; i < 7; ++i)  { S += State.Int1[i] ? TEXT("1") : TEXT("0"); }
	S += TEXT(" / ");
	for (int32 i = 0; i < 6; ++i)  { S += State.Int2[i] ? TEXT("1") : TEXT("0"); }
	S += TEXT(" / ");
	for (int32 i = 0; i < 10; ++i) { S += State.Int3[i] ? TEXT("1") : TEXT("0"); }
	S += TEXT("\n");
	// Что лежит в профиле игрока (то, что переживёт перезапуск), — рядом с буфером окна.
	if (const UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get())
	{
		S += FString::Printf(TEXT("  Профиль игрока: %s\n"), *Profile->Describe());
	}
	else
	{
		S += TEXT("  Профиль игрока: НЕ ЗАРЕГИСТРИРОВАН (GameUserSettingsClassName)\n");
	}
	// Ячейки списков под-страниц: найдены ли все три узла каждой ячейки (план/кнопка/подпись).
	{
		const int32 Count[3] = { PageCount(0), PageCount(1), PageCount(2) };
		S += TEXT("  Списки под-страниц (план/кнопка/подпись): ");
		for (int32 t = 0; t < 3; ++t)
		{
			for (int32 i = 0; i < Count[t]; ++i)
			{
				const int32 k = PageSlot(t, i);
				S += FString::Printf(TEXT("%d.%d=%d%d%d "), t, i,
					OptListPlane[k] != nullptr ? 1 : 0, OptListBtn[k] != nullptr ? 1 : 0, OptListText[k] != nullptr ? 1 : 0);
			}
		}
		S += TEXT("\n");
	}
	return S;
}
