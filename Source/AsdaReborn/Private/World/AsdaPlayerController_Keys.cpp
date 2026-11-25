// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerController_Keys.cpp — ★ 23.10.2026 часть AAsdaPlayerController, вынесенная из AsdaPlayerController.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaPlayerController.plan.json).
//   Раскладка клавиш и опрос (PollKeys, рыбалка), консоль клавиш asda.Keys.*, проверка кольца цели.
//   #include — те же, что у AsdaPlayerController.cpp, в том же порядке; общее — AsdaPlayerControllerInternal.h.
#include "World/AsdaPlayerController.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.10.2026: GetCameraTurnScale — чувствительность поворота камеры
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaWindowSubsystem.h" // ★ единый реестр зон интерфейса: блок мирового клика под ЛЮБЫМ окном
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaOrbitCursor.h"   // ★ 09.11.2026: сброс клавиш посреди поворота камеры (`NoteFlush`)
#include "GameFramework/PlayerInput.h"   // ★ 09.10.2026 (п. 4): `FKeyState::EventCounts` — второе нажатие двойного клика ЛКМ
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaTargetHighlightComponent.h"   // ★ подсветка выбранной цели (эталон g_pTargetHighlightPosA/B)
#include "World/AsdaWorldSubsystem.h"
#include "AsdaSelf.h"                       // ★ О3: свой userNum для записи пика `g_nPickedUserIdx`
#include "World/AsdaLoadManagerSubsystem.h"   // ★ CurrentMapId — развилка CampWar (mapId 19) перед окном 570
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"       // ★ Editor-Friendly раскладка + путь к system/Shortcutkey.ini
#include "World/AsdaAdminSubsystem.h"
#include "World/AsdaVehicleRideSubsystem.h"   // пасхалка «покататься» (пункт админ-меню)       // админ-режим (GM): хоткеи + свободная камера
#include "World/AsdaLootActor.h"            // наземный лут (клик-подбор, loot_system_re.md)
#include "World/AsdaCoord.h"                // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPathfinder.h"           // ★ поиск пути по сетке проходимости (обход препятствий)
#include "World/AsdaWalkGrid.h"
#include "Items/AsdaItemTableSubsystem.h"   // дальность/тип оружия из item-записи
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"   // ★ cmd 34 INTERFACE QUEST — журнал заданий (окно 700)
#include "UI/AsdaChatWindowSubsystem.h"        // cmd 37 «INTERFACE MAP» — карта мира (окно 1710)
#include "UI/AsdaDevMenuSubsystem.h"        // dev-меню Ctrl+N: пока открыто — игровые хоткеи подавлены
#include "UI/AsdaPremiumGachaSubsystem.h"   // ★ гейт `sub_922D5D@0xB6F370`: сцена премиум-гачи открыта
#include "UI/AsdaMessageSubsystem.h"        // ★★★ ЕДИНЫЙ маршрутизатор сообщений (нотисы клика/скриншота)
#include "UI/AsdaSoulGuardSubsystem.h"      // хоткеи 45/46/47 «SOUL GUARDIAN SKILL Lv1..3» (окно 56000)
#include "UI/AsdaTitleSubsystem.h"          // ★ cmd 38 «INTERFACE TITLE» — окно званий (JUI 18700)
#include "UI/AsdaPetCageSubsystem.h"        // ★ cmd 43 «INTERFACE PETBAG» — окно питомца (JUI 43000)
#include "UI/AsdaItemUpgradeSubsystem.h"    // ★ cmd 42 — тумблер окна улучшения снаряжения (JUI 20000)
#include "UI/AsdaCraftWindowSubsystem.h"      // ★ cmd 41 — окно создания снаряжения 76000; гейт клика по миру
#include "UI/AsdaDeathInputLock.h"            // ★ 05.11.2026: замок ввода мёртвого (эталон CUIManager+0x44 → только команда 44)
#include "UI/AsdaDisassemblySubsystem.h"       // ★ cmd 40 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaTradeStateSubsystem.h"       // ★ 29.09.2026: гейт клика по земле 9568 — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "UI/AsdaWarehouseSubsystem.h"   // ★ 30.09.2026 (ревизия Л4/Л5, СРЕДН-1): свиток возврата склада (+0x5734) — гейт 10305
#include "UI/AsdaSelfStoreSubsystem.h"         // ★ 30.09.2026 (волна Л5 торговли): фокус в поле чата лавки 23403 глушит горячие клавиши
#include "UI/AsdaNotice.h"                    // нотис 9568 в чат (фасад нотисов)
#include "UI/AsdaCraftSubsystem.h"            // ★ предикат «окно ремесла открыто» (гейт клика по миру, input.c:144-157)
#include "UI/AsdaPlayerMenuSubsystem.h"    // ★ контекстное меню по чужому игроку (окно 2000), ПКМ-отпускание
#include "UI/AsdaSkillUseSubsystem.h"       // ★ отмена отложенного «подбежать и применить навык»
#include "UI/AsdaSystemOptionSubsystem.h"   // ★ ESC-меню 69400 и окно настроек 69500 (клавиша ESC, захват клавиш)
#include "UI/AsdaHotbarSubsystem.h"         // ★ 27.09.2026: иконка «сесть/встать» на хотбаре (`ToggleSitStand`)
#include "UI/AsdaFishWindowSubsystem.h"     // ★ 08.10.2026 (рыбалка В2): фильтр клавиш мини-игры `Input_FishingKeyFilter`
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В2): гейт «идёт рыбалка» (fishState ≠ 0)
#include "World/AsdaFishZoneSubsystem.h"    // ★ 08.10.2026 (рыбалка В4): зоны в мире — тип попадания 5 двойного клика
#include "World/AsdaPossessGate.h"          // ★ гейт боевых действий под GM-«скрыться» (AsdaGmHide)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (гейт 4028, темп замаха)
#include "UI/AsdaPremiumShopSubsystem.h"     // ★ 17.10.2026 (Ф4): гейт PvP-клика «элитный магазин открыт» (helper_getField2075)
#include "UI/AsdaCraftMakeWindowSubsystem.h" // ★ Ф4: гейт PvP-клика «окно количества изготовления» (g_pItemQtyMakeDlg)
#include "UI/AsdaHairShopSubsystem.h"        // ★ Ф4: гейт PvP-клика «сессия парикмахерской» (CObject_GetField166)
#include "UI/AsdaBoosterPanelSubsystem.h"    // ★ Ф4: гейт PvP-клика «барабан бустера» (CObject_GetField254)
#include "UI/AsdaEffectsSubsystem.h"         // ★ Ф4: гейт PvP-клика «обездвижен» (нотис 1161)
#include "Resources/AsdaSoundSubsystem.h"    // ★ Ф4: звук 214 «нет оружия»
#include "UI/AsdaChatSubsystem.h"            // ★ Ф4: эталонные цвета нотисов PvP-клика (ColWhite/ColGold)
#include "Npc/AsdaNpcDialogSubsystem.h"     // ★ окно разговора с NPC (JUI 570) — этапы Э1/Э2
#include "Quest/AsdaQuestTypes.h"           // FAsdaNpcQuestNpcRow — высота AABB NPC (npc_list +0x68)
#include "AsdaDataTableCache.h"             // AsdaDt::RowByInt — та же точка, что у плашки/меток
#include "Components/CapsuleComponent.h"    // радиус цели (хитбокс моба)
#include "Components/SkeletalMeshComponent.h" // фильтр по имени модели в asda.Target.RingNearest
#include "Engine/SkeletalMesh.h"
#include "Components/InputComponent.h"  // ★ UInputComponent: сборка без unity (-SingleFile) падала без него
#include "Engine/Engine.h"              // ★ GEngine: то же
#include "AsdaNetSubsystem.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"   // ★ modal_lock2: препроцессор мыши под модалкой (FAsdaModalMouseRelay)
#include "InputKeyEventArgs.h"                       // ★ modal_lock3: оси MouseX/MouseY препроцессора в UPlayerInput
#include "Misc/App.h"                                 // ★ modal_lock3: FApp::GetDeltaTime
#include "Widgets/SViewport.h"                       // ★ modal_lock2: событие — над игровым вьюпортом
#include "HAL/IConsoleManager.h"            // ★ ключи отката: asda.Target.Ring, asda.Npc.OpenOnSingleClick
#include "EngineUtils.h"                    // TActorIterator — живая проверка asda.Target.RingNearest
#include "AsdaEffectActor.h"                // фактика кольца в asda.Target.RingNearest (имя .jeb = цвет)
#include "AsdaFxNames.h"                    // ★ дедуп №2 В1-C (F5/F6): тег кольца цели, имя BP метки клика
#include "Resources/AsdaAssetPaths.h"       // ★ дедуп №2 В1-C (F5): путь класса эффект-BP по имени (FxClassPathFromBp)
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformApplicationMisc.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#endif
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя (клик → цель → бег → дальность → заявка → замах → выход)
#include "AsdaPlayerControllerInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerController*.cpp (LogAsdaPC, чистые хелперы)


// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ВОЛНА В2 РЫБАЛКИ (08.10.2026): ФИЛЬТР КЛАВИШ `MainWndProc@0x9841A0` (game.c:24138-24145, 24218).
//   WM_KEYDOWN: `if (!sub_91F03B(g_pMap, …) && !Input_IsTextInputFocused(g_pChatInputWnd)) { окна; хоткеи; ESC; '/' }`
//     • `sub_91F03B` → `Input_FishingKeyFilter@0xFC6F10` (ключ-гайд, `this+70 == 4`): ЛЮБОЕ WM_KEYDOWN (в т.ч. автоповтор
//       — бит 30 lParam не проверяется) = ответ; нумпад 104/100/98/102 → 38/37/40/39; вернул 1 — клавиша съедена;
//     • `Input_IsTextInputFocused(g_pChatInputWnd)` — МИСНЕЙМ: это fishState ≠ 0 → клавиша не идёт никуда.
//   WM_KEYUP (game.c:24218): при fishState ≠ 0 проходят только 44 (PrintScreen), 27 (Esc), 13 (Enter).
//   В порту клавиатурное состояние для `PollKeys`/движения строит `UPlayerInput` — фильтр стоит ПЕРЕД ним:
//     нажатие/повтор съедаются (клавиша не «зажата»); Esc/Enter пропускаются целиком (их эффект у порта — по фронтам
//     того же состояния: ESC → закрыватель окна 71300 → диалог 80, Enter → cmd 66), отпускание идёт всегда.
//   Мышь/оси/геймпад фильтр не трогает (WM_KEYDOWN — только клавиатура). Alt-сочетания у эталона — WM_SYSKEYDOWN, мимо
//   фильтра гайда (не засчитываются ответом) — так же и здесь.
//   ★ Ревизия В2 МЕЛК-5 (волна В4): WM_SYSKEYDOWN/UP (Alt-сочетания, F10 → `Input_OnKeyDown_Dispatch(121)`, g:24326-24353)
//   эталон при fishState ≠ 0 НЕ глушит — у оригинала Alt+Z и Alt-хоткеи во время рыбалки работают. [ПОРТ] блок «идёт
//   рыбалка» их съедает: Alt-биндов в раскладке порта нет (grep), последствий нет; пункт — в PORT_PENDING (рыбалка В4).
//   ★ Ревизия В4 МЕЛК-6 (09.10.2026): ключ отката `asda.Fish.EatSysKeys 0` — пропускать их, как эталон.
// ══════════════════════════════════════════════════════════════════════════════════════════════════
bool AAsdaPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (FishFilterKey(Params)) { return true; }
	return Super::InputKey(Params);
}

// ★ Ревизия В2 ВАЖН-1 (волна В4, 08.10.2026): сброс клавиш (потеря фокуса окна, Alt+Tab — `UGameViewportClient::LostFocus`)
//   гасит и признак Alt фильтра мини-игры — иначе после возврата все ответы считались бы Alt-сочетаниями (WM_SYSKEYDOWN).
void AAsdaPlayerController::FlushPressedKeys()
{
	bFishAltHeld = false;
	// ★ 22.10.2026 (ревизия WASD М-2, ревизия стана п. 7): сброс сам пришлёт отпускание зажатой ПКМ — оно не клик игрока.
	bNextRmbReleaseSynthetic = IsInputKeyDown(EKeys::RightMouseButton);
	// ★ 09.11.2026: сброс посреди поворота — тик вернёт поворот, если ПКМ зажата (`TakeRearm`). Поворот, уже возвращённый после прошлого
	//   сброса, в `UPlayerInput` ПКМ не держит: отпускания движок не пришлёт, поэтому поворот снимаем здесь, а решение о возврате — за
	//   `TakeRearm` (модалка, Alt+Tab — не вернёт). Ревизия 09.11, п. 1.
	const bool bOrbitAtFlush = bRMBDown;
	if (bRMBDown && !IsInputKeyDown(EKeys::RightMouseButton)) { bRMBDown = false; }
	FAsdaOrbitCursor::NoteFlush(bOrbitAtFlush);
	Super::FlushPressedKeys();
}

// ★ В3 S7 (N14): своей реализации «идёт рыбалка» нет — одна точка подсистемы рыбалки.
bool AAsdaPlayerController::IsFishingNow() const
{
	return UAsdaFishingSubsystem::IsFishingNow(GetGameInstance());
}

// ★ В3 S7 (N14): правило «системной» клавиши гейта рыбалки — разбор у объявления.
bool AAsdaPlayerController::IsFishSysKey(const FKey& K, bool bAltDown)
{
	return bAltDown || K == EKeys::LeftAlt || K == EKeys::RightAlt || K == EKeys::F10;
}

bool AAsdaPlayerController::FishFilterKey(const FInputKeyEventArgs& Params)
{
	const FKey& K = Params.Key;
	if (!K.IsValid() || K.IsMouseButton() || K.IsAnalog() || K.IsGamepadKey() || K.IsTouch()) { return false; }
	const bool bDownEv = (Params.Event == IE_Pressed || Params.Event == IE_Repeat);   // WM_KEYDOWN (+ автоповтор)
	if (K == EKeys::LeftAlt || K == EKeys::RightAlt) { bFishAltHeld = (Params.Event != IE_Released); }
	// ★ Ревизия В2 ВАЖН-1 (волна В4): флаг по событиям застревал после Alt+Tab (отпускание уходило другому окну, а сброс
	//   клавиш при потере фокуса идёт в `UPlayerInput` мимо `InputKey`) — теперь его гасит `FlushPressedKeys` (ниже).
	const bool bPassKey = (K == EKeys::Escape || K == EKeys::Enter);                  // KEYUP-путь эталона: 27 / 13
	UGameInstance* GI = GetGameInstance();
	UAsdaFishWindowSubsystem* Win = GI ? GI->GetSubsystem<UAsdaFishWindowSubsystem>() : nullptr;
	// 1) `Input_FishingKeyFilter`: гайд в state 4.
	if (Win && Win->IsGuideInputState())
	{
		const bool bAlt = bFishAltHeld || (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsAltDown());
		const bool bSys = IsFishSysKey(K, bAlt);   // WM_SYSKEYDOWN (★ В3 S7: одно правило — `IsFishSysKey`)
		if (bDownEv && !bSys)
		{
			int32 Vk = 0;
			if (K == EKeys::NumPadEight)     { Vk = 104; }
			else if (K == EKeys::NumPadFour) { Vk = 100; }
			else if (K == EKeys::NumPadTwo)  { Vk = 98; }
			else if (K == EKeys::NumPadSix)  { Vk = 102; }
			else { Vk = UAsdaKeybindConfig::FKeyToVk(K); }
			if (Vk == 0) { Vk = -2; }   // клавиша без VK в таблице порта — тоже ответ (неверный), как любая иная у эталона
			Win->OnGuideKeyDown(Vk);
		}
		if (bPassKey) { return false; }   // их KEYUP у эталона доходит до окон/диспетча
		// ★ Ревизия В4 МЕЛК-6: `asda.Fish.EatSysKeys 0` — Alt-сочетания и F10 (WM_SYSKEYDOWN) проходят, как у эталона.
		if (bSys && !IsFishEatSysKeys()) { return false; }   // = `ShouldPassFishSysKey` (признак Alt уже собран в bSys)
		return bDownEv;
	}
	// 2) Идёт рыбалка — нажатие не доходит ни до хоткеев, ни до движения.
	if (bDownEv && !bPassKey && IsFishingNow())
	{
		// ★ Ревизия В4 МЕЛК-6: `asda.Fish.EatSysKeys 0` — Alt-сочетания и F10 (WM_SYSKEYDOWN) мимо гейта, как у эталона (g:24326).
		//   ★ В3 S7 (N14): одно правило `ShouldPassFishSysKey`.
		if (ShouldPassFishSysKey(K, bFishAltHeld
			|| (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsAltDown()))) { return false; }
		return true;
	}
	return false;
}

// ★ 25.10.2026 (присед, AsdaPlayerCharacter_Move.cpp): «Зажатие» и клавиши хода при зажатом Ctrl — без сочетаний чата
//   (ключ asda.Crouch.HoldNoMoveCombo; аксессор — чтобы имя ключа не попадало в этот файл).
bool AsdaCrouchHoldBlocksMoveCombos();

void AAsdaPlayerController::PollKeys()
{
	if (!Keys) { return; }
	const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	const bool bCtrl  = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
	const bool bAlt   = IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt);
	const EAsdaKeyMod Mod = UAsdaKeybindConfig::CurrentModifier(bShift, bCtrl, bAlt);

	// ★ Dev-меню (Ctrl+N, UAsdaDevMenuSubsystem) открыто → игровые хоткей-команды подавлены (движение
	//   оставляем — удобно сравнивать графику на бегу). Сам Ctrl+N поглощается Slate-препроцессором меню
	//   до PlayerInput → cmd 67 CHAT NORMAL дублем не стреляет; при фокусе в текстовом поле препроцессор
	//   Ctrl+N пропускает (игровой чат-переключатель сохранён).
	const UAsdaDevMenuSubsystem* DevMenu = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaDevMenuSubsystem>() : nullptr;
	const bool bDevMenuOpen = DevMenu && DevMenu->IsMenuOpen();

	// ★★★ НАБОР ТЕКСТА В ЧАТЕ ГЛУШИТ ИГРОВЫЕ КЛАВИШИ. Эталон: `Hotbar_SlotActivate@0xDEDDD0`
	//   первым же условием проверяет `Input_IsTextInputFocused(g_pChatInputWnd)`, а сам ввод
	//   съедает WindowMgr до игрового диспетча. Без этого набранное «1» жало бы «BAR SLOT 1»,
	//   а «I» открывало инвентарь прямо во время печати.
	//   ИСКЛЮЧЕНИЕ — cmd 66 CHAT OPEN CLOSE (Enter): его поведение не трогаем, чат закрывается/шлёт
	//   как и раньше.
	const UAsdaChatWindowSubsystem* ChatWnd =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	const bool bTypingInChat = ChatWnd && ChatWnd->IsInputActive();
	// ★ 30.09.2026 (волна Л5 торговли): фокус в поле чата личной лавки 23403 — у эталона клавиши съедает само поле (менеджер окон), в
	//   игровой диспетч не приходит НИ ОДНА, включая Enter (он у поля = 67587 → 6313). У UE буква, не обработанная `SEditableText`,
	//   всплывает до вьюпорта — поэтому глушим явно: ни команд, ни движения.
	const UAsdaSelfStoreSubsystem* StallStore = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
	const bool bTypingInStall = StallStore && StallStore->IsChatInputFocused();

	// ★★★ ОТКРЫТА СЦЕНА ПРЕМИУМ-ГАЧИ → НИ ОДНА КОМАНДА РАСКЛАДКИ НЕ ДИСПЕТЧИТСЯ.
	//   Эталон `Input_OnKeyDown_Dispatch@0xE3C7D0` (`input.c:3956`):
	//       if ( a4 && a4 == v10 || !a4 && !sub_922D5D(g_pGachaScene) )  dispatch(v10);
	//   `a4` — «разрешён только вот этот actionId»: обычный игровой путь зовёт диспетч с `a4 == 0`
	//   (`game_main.c:6230`), и тогда ЕДИНСТВЕННОЕ условие выполнения — гейт ложен. Белые списки
	//   `a4 == 33` (INTERFACE INVENTORY) и `a4 == 44` (SCREEN SHOT) в оригинале включаются другими
	//   состояниями UI (`game_main.c:6220/6227`), к гаче отношения не имеют и гейт не обходят.
	//   ⇒ пока сцена открыта, гасим и фронты команд, и уровневые флаги движения (в оригинале
	//   MOVE_W/S/A/D — те же команды 51..54, идущие тем же диспетчем).
	//   Механизм тот же, что у dev-меню Ctrl+N (`bDevMenuOpen`), но ОТДЕЛЬНЫМ флагом: dev-меню
	//   движение оставляет, гача — нет. Дублирования нет: подавление считается один раз, здесь.
	const bool bGachaSceneOpen = AsdaGachaSceneOpen(GetGameInstance());
	// ★ Волна В2 рыбалки (08.10.2026): пока идёт рыбалка, KEYUP эталона пропускает в диспетч только 44 и 13 (game.c:24218):
	//   ни движения, ни прочих команд — даже от клавиш, зажатых ДО начала рыбалки (новые нажатия съедает `InputKey`).
	const bool bFishingGate = IsFishingNow();
	// ★★★ 10.10.2026 ЗАМОК ВВОДА МОДАЛКОЙ (решение владельца, `Reports/owner_1010/modal_lock.md`). Эталон `MainWndProc@0x9841A0`
	//   зовёт диспетч раскладки только при `!sub_930E71(g_pWindowMgr)` = `get_o200` — флаг модального контрола `CWindowMgr+0x200`:
	//   KeyUp (game_main.c:6140), KeyDown (6223), Alt-сочетания WM_SYSKEYDOWN/UP (6300, 6316 — там же Alt+Z и тоггл строки чата),
	//   а Enter → строка чата идёт через `GameInput_DispatchEvent@0xC6DCA0` с тем же условием (9357). Порт: `IsAnyModalOpen` —
	//   общая точка этого флага (окна `bModal` и модальные контролы эталона без `bModal`: 1559, 74200). Под `bModal` клавиши и так
	//   не доходят (`FInputModeUIOnly` → вьюпорт игнорирует ввод), гейт здесь — тот же эталонный, явный и для любого пути ввода.
	//   Движение (команды 51..54 — та же раскладка) тоже стоит. Ключ диагностики — `asda.UI.ModalLock 0` (снимает замок у окон без `bModal`).
	//   ★ modal_lock2 (МЕЛК-3 ревизии): МИМО этого гейта у эталона идут три пути `MainWndProc` — WM_SYSKEYUP F10 →
	//   `Input_OnKeyDown_Dispatch(121)` (game_main.c:6314), «/» (0xBF) → `uiControlSetTextAndFocus(чат, "/")` (6158) и Insert/Delete
	//   войны лагерей (6123–6138, `byte_2C00C81`). Сейчас расхождения нет: F10 по умолчанию — хотбар 27 со своим гейтом 67351, а «/» и
	//   кампвар в порт не перенесены. При переносе «/» и кампвара гейт модалки к ним НЕ применять; F10 через отпускание — тоже мимо
	//   (реестр отложенного, «ESC и модалки», п. 6).
	const UAsdaWindowSubsystem* ModalWM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bModalLock = ModalWM && ModalWM->IsAnyModalOpen();
	// ★ 25.10.2026 [ОРИГИНАЛ] (ключ `asda.Keys.CaptureSwallow`): окно настроек ждёт клавишу (захват) — ни хода, ни команд
	//   (эталон: `Input_OnKeyUp_Dispatch@0xE3C730` ставит флаги хода только при `!captureMode`, `Input_OnKeyDown_Dispatch@0xE3C7D0`
	//   в захвате не диспетчит). Клавиши, зажатые в момент захвата, съедаются до отпускания (`ConsumeCaptureHeldKey`) — захват у
	//   порта по нажатию, и отпускание иначе исполняло бы только что назначенную команду.
	UAsdaSystemOptionSubsystem* CaptureOpt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr;
	const bool bKeyCapture = CaptureOpt && CaptureOpt->IsGameInputCaptured();
	// ★★★ 05.11.2026 [ОРИГИНАЛ] ЗАМОК ВВОДА МЁРТВОГО (`asda.Death.InputLock`, разбор — `UI/AsdaDeathInputLock.h`). Эталон `MainWndProc`
	//   при `CUIManager+0x44 == 1` (меню смерти открыто) зовёт диспетч раскладки с `a4 = 44` (game_main.c:6225-6227) — исполняется
	//   ТОЛЬКО команда 44 SCREEN SHOT; на нажатии (флаги хода, :6140) и в Alt-сочетаниях (:6300/:6316) диспетча нет совсем ⇒ хода
	//   нет, хотбар/окна/эмоции/Alt+Z молчат. ИСКЛЮЧЕНИЕ — строка чата: у эталона Enter идёт НЕ через раскладку, а через
	//   `GameInput_DispatchEvent@0xC6DCA0` (game_main.c:9258: VK 13 → `uiCanOpenWindowGate()` → `uiChatInputToggle`), где флага
	//   смерти нет; у `hotkeyDispatchAction` команды 66 нет вовсе. В порту строка чата — команда 66 раскладки ⇒ мёртвому её
	//   пропускаем (`kAllowedChatCmd`), чтобы чат работал, как в оригинале. ★ 06.11.2026 [ИСПРАВЛЕНИЕ]: и клавиши каналов 67–73
	//   (`AsdaDeathLock::IsAllowedCmd`, ключ `asda.Death.ChatKeys`) — владелец: мёртвому чат работает полностью.
	const bool bDeathLock = AsdaDeathLock::IsInputLocked(GetGameInstance());

	// Сброс латчей движения — пересчёт по факту нажатия ниже.
	bMoveFwd = bMoveBack = bMoveLeft = bMoveRight = false;

	// ══════════════════════════════════════════════════════════════════════════════════════════════
	//  ★★★ ИСПРАВЛЕНО 2026-08-15 — ОПРОС ИДЁТ ПО КЛАВИШАМ, А НЕ ПО БИНДАМ.
	//
	//  Прежний цикл шёл `for (бинд : Keys->Binds)` и в КАЖДОЙ итерации писал `PrevKeyDown[Vk]`.
	//  С раскладкой ОРИГИНАЛА (`system/Shortcutkey.ini`) на одном VK сидят ДВА действия:
	//      cmd 6  «BAR SLOT 1»     = 1
	//      cmd 18 «NEW BAR SLOT 1» = SHIFT+1        (и так все девять: cmd 7..14 ↔ cmd 19..26)
	//  Бинды лежат в TMap в порядке вставки 1..85, поэтому cmd 6 обрабатывался ПЕРВЫМ, не совпадал
	//  с `LookupCmd(49, Shift) = 18`, ничего не выполнял — и тут же затирал `PrevKeyDown[49] = false`.
	//  До итерации cmd 18 фронт отпускания уже был съеден (`bPrev == false`), и SHIFT+1..9
	//  не срабатывали НИКОГДА. Ровно это и видно в живом тесте: обычные цифры работают, Shift+цифра — нет.
	//
	//  Эталон устроен иначе и в принципе не может так сломаться: обработчик окна на WM_KEYUP
	//  (`game_main.c:6230`) один раз зовёт `Input_OnKeyDown_Dispatch@0xE3C7D0`, а тот делает
	//      v9  = Keybind_NormalizeVK(vk);
	//      v4  = Keybind_ResolveModifier(0);          // живой GetKeyState(SHIFT/CTRL/ALT)
	//      v10 = Keybind_Lookup(bindMapActive, v9, v4);
	//      if (v10) dispatch(v10);
	//  то есть СОБЫТИЕ КЛАВИШИ → РОВНО ОДНА команда, выбранная по живому модификатору.
	//  Ниже воспроизведено именно это: сначала снимаем состояние УНИКАЛЬНЫХ VK, затем один раз
	//  на клавишу разбираем фронт отпускания и выполняем единственную найденную команду.
	// ══════════════════════════════════════════════════════════════════════════════════════════════

	// ── Шаг 1. Состояние уникальных клавиш раскладки (один опрос на VK, а не на бинд) ──
	TMap<int32, bool> DownNow;
	DownNow.Reserve(Keys->Binds.Num());
	for (const TPair<int32, FAsdaKeyBind>& P : Keys->Binds)
	{
		const int32 Vk = P.Value.Vk;
		if (Vk == 0 || DownNow.Contains(Vk)) { continue; }
		DownNow.Add(Vk, IsBindKeyDown(Vk));
	}
	// ★ 25.10.2026 (`asda.Keys.CaptureSwallow`): VK, зажатые в момент захвата клавиши окном настроек, — до отпускания не игровые.
	TSet<int32> CaptureHeld;
	if (CaptureOpt)
	{
		for (const TPair<int32, bool>& KV : DownNow) { if (CaptureOpt->ConsumeCaptureHeldKey(KV.Key, KV.Value)) { CaptureHeld.Add(KV.Key); } }
	}

	// ── Шаг 2. Движение (cmd 51-54) — уровневое по флагам, отдельно от фронтов ──
	// ★★★ МОДИФИКАТОР ДЛЯ ДВИЖЕНИЯ НЕ ТРЕБУЕТСЯ, если его нет в самом бинде. Эталон ставит флаг
	//   в key-DOWN (`input_SetMoveFlagFromKey`) и снимает в key-UP, между ними модификатор роли
	//   не играет — зажатый по дороге Shift бег не прерывает. Наш опрос переоценивает состояние
	//   каждый тик, и строгое `Bind.Mod == Mod` останавливало бы персонажа, как только игрок
	//   берётся за Shift, — а второй ряд хотбара сидит именно на SHIFT+1..9.
	if (!bTypingInChat && !bTypingInStall && !bGachaSceneOpen && !bFishingGate && !bModalLock && !bKeyCapture && !bDeathLock)
	{
		for (int32 MoveCmd = 51; MoveCmd <= 54; ++MoveCmd)
		{
			FAsdaKeyBind B;
			if (!Keys->GetBind(MoveCmd, B) || B.Vk == 0) { continue; }
			if (!DownNow.FindRef(B.Vk)) { continue; }
			if (CaptureHeld.Contains(B.Vk)) { continue; }   // ★ 25.10.2026: клавиша захвата — не ход до отпускания
			if (B.Mod != EAsdaKeyMod::None && B.Mod != Mod) { continue; }
			switch (MoveCmd) { case 51: bMoveFwd = true; break; case 52: bMoveBack = true; break;
			                   case 53: bMoveLeft = true; break; case 54: bMoveRight = true; break; }
		}
	}

	// ★ 25.10.2026 [ПОРТ] (присед «Зажатие», запрос владельца): клавиши хода (команды 51–54) — ход вприсядку держит Ctrl, поэтому
	//   отпускание W/S при зажатом Ctrl НЕ исполняет их Ctrl-сочетание (по умолчанию Ctrl+W 70 «шёпот», Ctrl+S 73 — строка чата
	//   открылась бы на ходу, а набор в чате глушит и ход, и присед). Прочие Ctrl-сочетания (Ctrl+C/N/Y/P) работают как прежде;
	//   в «Нажатии» не меняется ничего. Ключ отката — asda.Crouch.HoldNoMoveCombo 0.
	TArray<int32, TInlineAllocator<4>> CrouchHoldMoveVks;   // VK клавиш хода (пусто — «Зажатие» выключено)
	if (Mod == EAsdaKeyMod::Ctrl && AsdaCrouchHoldBlocksMoveCombos())
	{
		for (int32 MoveCmd = 51; MoveCmd <= 54; ++MoveCmd)
		{
			FAsdaKeyBind B;
			if (Keys->GetBind(MoveCmd, B) && B.Vk != 0 && B.Mod == EAsdaKeyMod::None) { CrouchHoldMoveVks.AddUnique(B.Vk); }
		}
	}

	// ── Шаг 3. Прочие команды — ПО ФРОНТУ ОТПУСКАНИЯ (эталон: диспетч на WM_KEYUP) ──
	for (const TPair<int32, bool>& KV : DownNow)
	{
		const int32 Vk = KV.Key;
		const bool  bDown = KV.Value;

		// ЖИВОЙ модификатор, а не модификатор бинда (`Keybind_ResolveModifier@0xE3CA80`): на одну
		//   клавишу приходится РОВНО ОДНА команда. «1»(cmd 6) и «SHIFT+1»(cmd 18) делят VK 49 —
		//   с зажатым Shift выигрывает cmd 18, без Shift cmd 6. Так же разводятся «N»(57)/«CTRL+N»(67)
		//   и «TAB»(60)/«SHIFT+TAB»(62). Двойного срабатывания больше нет по построению.
		const int32 Cmd = Keys->LookupCmd(Vk, Mod);

		// ★ 25.10.2026 [ОРИГИНАЛ]: захват клавиши окном настроек — ни одной команды; клавиша захвата — до своего отпускания.
		if (bKeyCapture || CaptureHeld.Contains(Vk)) { PrevKeyDown.Add(Vk, false); continue; }
		// ★★★ НАБОР ТЕКСТА В ЧАТЕ ГЛУШИТ ВСЁ, КРОМЕ cmd 66 CHAT OPEN CLOSE (см. шапку выше).
		//   Латч гасим, чтобы клавиша, отпущенная во время набора, не выстрелила после закрытия чата.
		if (bTypingInChat && Cmd != 66) { PrevKeyDown.Add(Vk, false); continue; }
		if (bTypingInStall) { PrevKeyDown.Add(Vk, false); continue; }   // ★ Л5: поле 23403 съедает все клавиши
		// ★ Рыбалка В2: KEYUP эталон при fishState ≠ 0 пропускает ПО КЛАВИШЕ — `wParam == 44 || 27 || 13` (g:24218), дальше
		//   `Input_OnKeyDown_Dispatch(wParam)` исполняет ту команду, что назначена на клавишу (27 в диспетче отсекается).
		//   ★ Ревизия В2 МЕЛК-4 (волна В4): условие по VK (13 Enter, 44 PrintScreen), а не по номеру команды 66/44 — иначе
		//   переназначение чата с Enter развело бы порт и эталон.
		if (bFishingGate && Vk != 13 && Vk != 44) { PrevKeyDown.Add(Vk, false); continue; }
		// ★ 10.10.2026: замок модалки (`get_o200`, см. выше) — ни одна команда раскладки, в т.ч. Enter (66), Alt-сочетания, хотбар
		//   (6..29), окна (30..43). Латч гасим: клавиша, отпущенная под модалкой, не выстрелит после её закрытия (у эталона KeyUp под
		//   модалкой просто не диспетчится). Исключение эталона — PrintScreen (VK 44): снимок делается ДО гейта (game_main.c:6193).
		if (bModalLock && Vk != 44) { PrevKeyDown.Add(Vk, false); continue; }
		// ★ 05.11.2026 [ОРИГИНАЛ] Замок мёртвого: белый список эталона — по НОМЕРУ КОМАНДЫ (`a4 && a4 == v10`, input.c:3956), не по VK.
		//   Латч гасим, как у модалки: клавиша, отпущенная мёртвым, не выстрелит после воскрешения.
		if (bDeathLock && !AsdaDeathLock::IsAllowedCmd(Cmd)) { PrevKeyDown.Add(Vk, false); continue; }

		const bool bPrev = PrevKeyDown.FindRef(Vk);
		PrevKeyDown.Add(Vk, bDown);
		if (!bPrev || bDown) { continue; }              // не фронт отпускания
		if (Cmd == 0) { continue; }                     // клавиша не назначена при текущем модификаторе
		if (Cmd >= 51 && Cmd <= 54) { continue; }       // движение обработано уровнево на шаге 2
		if (CrouchHoldMoveVks.Contains(Vk))             // ★ 25.10.2026: «Зажатие» — Ctrl+клавиша хода = ход вприсядку (см. выше)
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[присед] зажатие: VK %d отпущена при зажатом Ctrl — команда %d не исполняется (клавиша хода)"), Vk, Cmd);
			continue;
		}
		// ★ НАШИ расширения (86+) диспетчеризует не этот цикл, а сами подсистемы-владельцы
		//   (свободная камера, dev-меню, транспорт-пасхалка): у них своя фаза опроса и свои гейты.
		//   Здесь они только ЛЕЖАТ В КОНФИГЕ, чтобы владелец мог их переназначить.
		if (UAsdaKeybindConfig::IsExtensionCmd(Cmd)) { continue; }
		if (bDevMenuOpen) { continue; }
		// ★ Гейт сцены премиум-гачи (`input.c:3956`) — см. шапку блока выше: молча, без нотиса
		//   (эталон в этой ветке ничего не пишет, просто не зовёт диспетч).
		if (bGachaSceneOpen) { continue; }
		ExecuteCommand(Cmd);
	}
}

float AAsdaPlayerController::Net_LastSpeed() const
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		return Net->LastServerSpeed;
	}
	return 0.35f;
}

void AAsdaPlayerController::SetOrbitAnglesForBench(float YawDeg, float PitchDeg)
{
	// ★ 12.10.2026 (автоподбор графики): те же поля и тот же способ применения, что у ПКМ-драга (см. выше).
	CamYawDeg = YawDeg;
	CamPitchDeg = FMath::Clamp(PitchDeg, -80.f, -5.f);
	if (AAsdaPlayerCharacter* Ch = GetChar())
	{
		if (Ch->CameraArm) { Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f)); }
	}
}

AAsdaWorldGameMode::AAsdaWorldGameMode()
{
	PlayerControllerClass = AAsdaPlayerController::StaticClass();
	DefaultPawnClass = nullptr;   // павн спавнит UAsdaWorldSubsystem по 4003 (позиция сервера) и Possess'ит
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  КОНСОЛЬ — раскладка клавиш
// ════════════════════════════════════════════════════════════════════════════════════════════════
namespace
{
	static AAsdaPlayerController* KeysPcOf(UWorld* W)
	{
		return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): было одной строкой в журнал + `AddOnScreenDebugMessage` (в Shipping
	//   оверлей пуст) — стало общая `AsdaNotice::ConsoleEcho` (журнал построчно, непустые строки — в ленту Tip).
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
	static void KeysEcho(UWorld* W, const FString& Text) { AsdaNotice::ConsoleEcho(W, Text, LogAsdaPC); }
#endif // !UE_BUILD_SHIPPING
}

// asda.Keybinds [all] — показать ДЕЙСТВУЮЩУЮ раскладку и её источник (ini / профиль / хардкод).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GKeybindsDump(
	TEXT("asda.Keybinds"),
	TEXT("Клавиши: текущая раскладка и источник (ini/профиль/дефолты). Аргумент 'all' — включая неназначенные"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AAsdaPlayerController* PC = KeysPcOf(W);
		if (!PC || !PC->Keys) { KeysEcho(W, TEXT("asda.Keybinds: контроллер/конфиг клавиш недоступен")); return; }
		const bool bAll = A.Num() > 0 && A[0].Equals(TEXT("all"), ESearchCase::IgnoreCase);
		KeysEcho(W, PC->Keys->DumpLayout(/*bOnlyBound=*/!bAll));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Keybinds.Hold <клавиша> <0|1> — УДЕРЖАТЬ/ОТПУСТИТЬ клавишу.
//   ⚠ Нужна отдельно от `Press`: `PollKeys` работает ОПРОСОМ состояния и стреляет по ФРОНТУ
//   ОТПУСКАНИЯ, поэтому нажатие и отпускание в ОДНОМ кадре он не увидит вовсе (проверено
//   прогоном: `Press I` не дал ни одной команды). Ставим удержание, ждём кадры, потом отпускаем.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GKeysHold(
	TEXT("asda.Keybinds.Hold"),
	TEXT("asda.Keybinds.Hold <клавиша> <0|1> — 1 = зажать, 0 = отпустить (настоящий ввод)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетический ввод клавиш — при живом соединении игровые команды (4006/4008, 4026/4028, 5012, навыки).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Keybinds.Hold"))) { return; }
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || A.Num() < 2) { return; }
		const FKey K(*A[0]);
		if (!K.IsValid()) { UE_LOG(LogAsdaPC, Warning, TEXT("asda.Keybinds.Hold: клавиша «%s» не опознана"), *A[0]); return; }
		const bool bDown = (FCString::Atoi(*A[1]) != 0);
		// ★★★ ЧЕРЕЗ SLATE, А НЕ ЧЕРЕЗ `PC->InputKey`. Проверено прогоном: `InputKey(CreateSimulated)`
		//   доставляет СОБЫТИЕ (его видят привязки `BindKey`), но НЕ выставляет удерживаемое
		//   состояние клавиши, которое опрашивает `PollKeys` через `IsInputKeyDown` — команды не
		//   срабатывали вовсе. `FSlateApplication::ProcessKeyDownEvent` идёт ровно тем путём, что и
		//   физическая клавиатура: Slate → вьюпорт → `UPlayerInput` → состояние клавиши.
		if (!FSlateApplication::IsInitialized())
		{
			UE_LOG(LogAsdaPC, Warning, TEXT("asda.Keybinds.Hold: Slate не инициализирован"));
			return;
		}
		// ★ Без фокуса на игровом вьюпорте Slate уносит событие в никуда (headless-PIE стартует без
		//   фокуса) — состояние клавиши не выставляется, и опросный `PollKeys` ничего не видит.
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
		const uint32* CharCode = nullptr;
		const uint32* KeyCode = nullptr;
		const FKeyEvent Ev(K, FModifierKeysState(), 0 /*UserIndex*/, /*bIsRepeat=*/false,
			CharCode ? *CharCode : 0, KeyCode ? *KeyCode : 0);
		if (bDown) { FSlateApplication::Get().ProcessKeyDownEvent(Ev); }
		else       { FSlateApplication::Get().ProcessKeyUpEvent(Ev); }
		UE_LOG(LogAsdaPC, Log, TEXT("[клавиши] «%s» %s (настоящий ввод через Slate)"), *A[0],
			bDown ? TEXT("ЗАЖАТА") : TEXT("ОТПУЩЕНА"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Keybinds.HoldSim <клавиша> <0|1> — УДЕРЖАТЬ/ОТПУСТИТЬ через `UPlayerInput`.
//   ⚠ ЗАЧЕМ ВТОРОЙ СПОСОБ УДЕРЖАНИЯ (закон [[false-negatives-method]]): `asda.Keybinds.Hold` шлёт
//   событие через Slate, и в HEADLESS-прогоне 2026-08-29 это дало ЛОЖНОЕ «клавиши не работают» —
//   в логе были «ЗАЖАТА/ОТПУЩЕНА», но ни одного `[cmdN]`: без настоящего окна Slate не доносит
//   событие до вьюпорта, и состояние клавиши в `UPlayerInput` не выставляется.
//   Здесь событие кладётся ПРЯМО в `UPlayerInput::KeyStateMap` — ровно туда, откуда его читает
//   `IsInputKeyDown` внутри `PollKeys`. Путь остаётся игроцким на всём остатке цепочки
//   (`UPlayerInput` → `PollKeys` → `ExecuteCommand`), обработчик напрямую НЕ зовётся.
//   ★ Нажатие и отпускание обязаны прийтись на РАЗНЫЕ кадры: `PollKeys` стреляет по фронту
//   отпускания, а фронт он видит только сравнением состояния прошлого кадра с текущим.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GKeysHoldSim(
	TEXT("asda.Keybinds.HoldSim"),
	TEXT("asda.Keybinds.HoldSim <клавиша> <0|1> — 1 = зажать, 0 = отпустить (через UPlayerInput)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетический ввод клавиш (см. asda.Keybinds.Hold).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Keybinds.HoldSim"))) { return; }
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || A.Num() < 2) { return; }
		const FKey K(*A[0]);
		if (!K.IsValid()) { UE_LOG(LogAsdaPC, Warning, TEXT("asda.Keybinds.HoldSim: клавиша «%s» не опознана"), *A[0]); return; }
		const bool bDown = (FCString::Atoi(*A[1]) != 0);
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
		UE_LOG(LogAsdaPC, Log, TEXT("[клавиши] «%s» %s (UPlayerInput); IsInputKeyDown=%d"), *A[0],
			bDown ? TEXT("ЗАЖАТА") : TEXT("ОТПУЩЕНА"), PC->IsInputKeyDown(K) ? 1 : 0);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ asda.Keybinds.Press <ИмяКлавишиUE> — НАСТОЯЩЕЕ нажатие клавиши через привязки ввода.
//   Нужно проверке «путём игрока»: событие идёт тем же путём, что от клавиатуры (UPlayerInput →
//   PollKeys → ExecuteCommand), а не вызовом обработчика напрямую.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GKeysPress(
	TEXT("asda.Keybinds.Press"),
	TEXT("asda.Keybinds.Press <клавиша> [SHIFT|CTRL|ALT] — послать настоящее нажатие+отпускание"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетическое нажатие клавиши (см. asda.Keybinds.Hold).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Keybinds.Press"))) { return; }
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || A.Num() < 1) { return; }
		const FKey K(*A[0]);
		if (!K.IsValid()) { UE_LOG(LogAsdaPC, Warning, TEXT("asda.Keybinds.Press: клавиша «%s» не опознана"), *A[0]); return; }
		FKey ModKey = EKeys::Invalid;
		if (A.Num() > 1)
		{
			if (A[1].Equals(TEXT("SHIFT"), ESearchCase::IgnoreCase))     { ModKey = EKeys::LeftShift; }
			else if (A[1].Equals(TEXT("CTRL"), ESearchCase::IgnoreCase)) { ModKey = EKeys::LeftControl; }
			else if (A[1].Equals(TEXT("ALT"), ESearchCase::IgnoreCase))  { ModKey = EKeys::LeftAlt; }
		}
		if (ModKey.IsValid()) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(ModKey, IE_Pressed, 1.f)); }
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE_Released, 0.f));
		if (ModKey.IsValid()) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(ModKey, IE_Released, 0.f)); }
		UE_LOG(LogAsdaPC, Log, TEXT("[клавиши] послано настоящее нажатие «%s»%s%s"), *A[0],
			ModKey.IsValid() ? TEXT(" с модификатором ") : TEXT(""), ModKey.IsValid() ? *A[1] : TEXT(""));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.Keybinds.Reload — перечитать system/Shortcutkey.ini на лету (правка файла без перезапуска).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GKeybindsReload(
	TEXT("asda.Keybinds.Reload"),
	TEXT("Клавиши: перечитать раскладку (дефолты → профиль → system/Shortcutkey.ini) без перезапуска"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		AAsdaPlayerController* PC = KeysPcOf(W);
		if (!PC || !PC->Keys) { KeysEcho(W, TEXT("asda.Keybinds.Reload: контроллер/конфиг клавиш недоступен")); return; }
		const bool bIni = PC->Keys->ReloadLayout();
		KeysEcho(W, FString::Printf(TEXT("Раскладка перечитана (%s)\n%s"),
			bIni ? TEXT("ini прочитан") : TEXT("ini НЕ найден — дефолты/профиль"),
			*PC->Keys->DumpLayout(/*bOnlyBound=*/true)));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.Keybinds.Set <cmd> <клавиша> — переназначить, как в окне опций оригинала.
//   Клавиша пишется именем ИЗ ТАБЛИЦЫ ОРИГИНАЛА: «F1», «SHIFT+1», «[», «PGUP», «PrtSc», «NONE».
// ★ ДЕФЕКТ ПОРТА — ИСПРАВЛЕН (аудит 2026-09-02, находка №11, третья отсечка).
//   БЫЛО: `Cmd > 85` → отказ. Это была ПОСЛЕДНЯЯ из трёх отсечек, из-за которых наши расширения
//   86..94 нельзя было переназначить ни одним предусмотренным способом (две другие сняты
//   в `AsdaKeybindConfig.cpp`: `RebindFromKey` и слой профиля).
//   СТАЛО: допускаем любой код, у которого ЕСТЬ лейбл, — 1..85 (эталон) + 86..94 (наши).
//   Проверка та же, что в `RebindFromKey`, чтобы три пути не разъехались снова.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GKeybindsSet(
	TEXT("asda.Keybinds.Set"),
	TEXT("Клавиши: asda.Keybinds.Set <cmd 1..94> <клавиша: F1 | SHIFT+1 | [ | PGUP | NONE> — переназначить и сохранить ini"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AAsdaPlayerController* PC = KeysPcOf(W);
		if (!PC || !PC->Keys) { KeysEcho(W, TEXT("asda.Keybinds.Set: контроллер/конфиг клавиш недоступен")); return; }
		if (A.Num() < 2) { KeysEcho(W, TEXT("asda.Keybinds.Set <cmd 1..94> <клавиша>")); return; }
		const int32 Cmd = FCString::Atoi(*A[0]);
		if (Cmd < 1 || UAsdaKeybindConfig::GetActionLabel(Cmd) == nullptr)
		{
			KeysEcho(W, FString::Printf(TEXT("cmd %d не существует (эталон 1..85, наши расширения 86..%d)"),
				Cmd, UAsdaKeybindConfig::GetLastActionCmd()));
			return;
		}
		FAsdaKeyBind B;
		if (!A[1].Equals(TEXT("NONE"), ESearchCase::CaseSensitive) && !UAsdaKeybindConfig::ParseBindToken(A[1], B))
		{
			KeysEcho(W, FString::Printf(TEXT("Клавиша «%s» не из таблицы оригинала (74 клавиши)"), *A[1]));
			return;
		}
		const int32 Prev = PC->Keys->SetBind(Cmd, B.Vk, B.Mod, /*bPlayerRebind*/ true);   // ★ 25.10.2026: как назначение игроком
		PC->Keys->SaveShortcutkeyIni(UAsdaKeybindConfig::ResolveShortcutkeyIniPath(PC->Keys->Profile, /*bForWrite=*/true));
		KeysEcho(W, FString::Printf(TEXT("cmd %d «%s» → %s%s"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd),
			*UAsdaKeybindConfig::FormatBind(B),
			Prev ? *FString::Printf(TEXT("  (снято с «%s»)"), *UAsdaKeybindConfig::GetCmdName(Prev)) : TEXT("")));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.Keybinds.Save — записать текущую раскладку в system/Shortcutkey.ini (формат оригинала).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GKeybindsSave(
	TEXT("asda.Keybinds.Save"),
	TEXT("Клавиши: записать текущую раскладку в system/Shortcutkey.ini в формате оригинала"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		AAsdaPlayerController* PC = KeysPcOf(W);
		if (!PC || !PC->Keys) { KeysEcho(W, TEXT("asda.Keybinds.Save: контроллер/конфиг клавиш недоступен")); return; }
		const FString Path = UAsdaKeybindConfig::ResolveShortcutkeyIniPath(PC->Keys->Profile, /*bForWrite=*/true);
		KeysEcho(W, FString::Printf(TEXT("Запись в «%s» — %s"), *Path,
			PC->Keys->SaveShortcutkeyIni(Path) ? TEXT("успешно") : TEXT("ОШИБКА")));
	}));
#endif // ASDA_DEV_CONSOLE

// ════════════ ★ ЖИВАЯ ПРОВЕРКА КОЛЬЦА ПОД ЦЕЛЬЮ БЕЗ СЕРВЕРА (`asda.Target.RingNearest`) ════════════
//   ЗАЧЕМ. Мышью по цели в PIE без сервера не кликнуть: игровые NPC/мобы появляются по сетевому
//   спавну, а запечённые в уровне акторы — каталог-превью (`BeginPlay` их прячет и снимает
//   коллизию). Но ветка выделения читает у актора только его класс и позицию, поэтому её можно
//   прогнать по ЗАПЕЧЁННОМУ актору — ТЕМ ЖЕ кодом, что дёргает мышь (`SetTarget`), без единой
//   параллельной «тестовой» ветки. Тот же приём уже применён у диалога (`asda.Npc.TalkNearest`).
// ★ Фактика кольца, висящего на `Target` (одна точка на оба режима команды — «выбрать» и «dump»,
//   чтобы вторая копия печати не разъехалась с первой).
static void AsdaDumpTargetRingFacts(const AActor* Target, FOutputDevice& Ar)
{
	if (!Target) { Ar.Logf(TEXT("[кольцо] цели нет")); return; }
	// ★★★ ВЫСОТА — ФАКТОМ (задача 2026-08-21 «кольцо должно быть в нижней части ног»).
	//   Подошву спрашиваем У ТОЙ ЖЕ функции, что и постановка кольца
	//   (`UAsdaWorldSubsystem::TargetSoleZ`), иначе диагностика мерила бы «своё».
	const float TgtZ  = Target->GetActorLocation().Z;
	const float SoleZ = UAsdaWorldSubsystem::TargetSoleZ(Target);
	TArray<AActor*> Attached;
	Target->GetAttachedActors(Attached);
	int32 N = 0;
	for (const AActor* A : Attached)
	{
		const AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A);
		if (!E || !A->ActorHasTag(FName(AsdaCharFxTags::kTargetRing))) { continue; }   // ★ В1-C F6: общий тег
		++N;
		// ★ РАЗОВОСТЬ — ФАКТОМ, А НЕ НА СЛОВО (`npc_target_ring_fix.md` §4): печатаем
		//   `EndMode` эмиттера из АССЕТА (0 = стоп после одной проходки; 1/2/3 = повтор/фриз),
		//   его живое состояние (`bStopped` = ветка `case 0` уже отработала) и возраст актора.
		//   Имя объекта нужно, чтобы отличить «эффект тот же» от «пересоздан молча».
		const int32 EndMode = E->Emitters.Num() > 0 ? E->Emitters[0].EndMode : -1;
		bool bStopped = false; float LifeTimer = -1.f, NodeAlpha = -1.f, HalfExt = -1.f;
		const bool bHaveRt = E->GetEmitterRuntime(0, bStopped, LifeTimer, &NodeAlpha, &HalfExt);
		// ★ «Кольцо ВИДНО» — фактом: радиус реально построенной геометрии (−1 = секции нет)
		//   и альфа текущего кадра. Без них «замер на конечном кадре» ничего не доказывает:
		//   замерший кадр с alpha 0 — это невидимое кольцо ([[false-negatives-method]]).
		const float GeoR = E->EmitterSectionRadius(0);
		// ★★★ РЕЖИМ КОНЦА (правка 2026-08-21 по замечанию владельца «кольцо должно оставаться
		//   видимым»): ПРАВИЛЬНО теперь `3` — «фриз на конечном узле» (квад замирает на конечном
		//   диаметре и продолжает рисоваться, пока цель выбрана). `0` = прежнее «отыграл и погас»
		//   (кольца на земле не остаётся), `1`/`2` = вечный перезапуск — оба теперь дефекты.
		const float Per = E->Emitters.Num() > 0 ? E->Emitters[0].Period : 0.f;
		const TCHAR* ModeWhat =
			EndMode == 3 ? TEXT("ДЕРЖИТ конечный кадр")
			             : (EndMode == 0 ? TEXT("гаснет после проходки — это баг")
			                             : TEXT("ПОВТОР — это баг"));
		// Состояние: у `endMode 3` эмиттер не «останавливается» — он замирает на `LifeTimer == Period`.
		const TCHAR* StateWhat =
			!bHaveRt ? TEXT("нет состояния")
			         : (bStopped ? TEXT("ПОГАШЕН")
			                     : (LifeTimer >= Per - KINDA_SMALL_NUMBER ? TEXT("ЗАМЕР на конечном кадре")
			                                                              : TEXT("играет")));
		const float RingZ = A->GetActorLocation().Z;
		Ar.Logf(TEXT("[кольцо] эффект «%s» (%s): эмиттеров %d, endMode=%d (%s), состояние: %s, ")
		        TEXT("таймер %.2f c, возраст %.2f c, позиция (%.0f,%.0f,%.0f); ")
		        TEXT("цель Z=%.1f, подошва Z=%.1f, кольцо НАД ПОДОШВОЙ %+.1f; ")
		        TEXT("ВИДИМОСТЬ: альфа %.2f, полу-экстент %.1f, радиус геометрии %.1f — %s"),
			*E->EffectName, *A->GetName(), E->Emitters.Num(), EndMode, ModeWhat, StateWhat,
			LifeTimer, A->GetGameTimeSinceCreation(),
			A->GetActorLocation().X, A->GetActorLocation().Y, RingZ,
			TgtZ, SoleZ, RingZ - SoleZ,
			NodeAlpha, HalfExt, GeoR,
			(GeoR > 0.f && NodeAlpha > 0.01f) ? TEXT("ВИДНО") : TEXT("НЕ ВИДНО"));
	}
	if (N == 0)
	{
		Ar.Logf(TEXT("[кольцо] ЭФФЕКТА НЕТ: либо asda.Target.Ring 0, либо BP не собран ")
		        TEXT("(Tools/asda_build_target_ring_fx.py)"));
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldArgsAndOutputDevice GAsdaCmdRingNearest(
	TEXT("asda.Target.RingNearest"),
	TEXT("asda.Target.RingNearest <npc|mob|dump|off> [подстрока] — выбрать целью ближайший NPC/моб ")
	TEXT("уровня (тот же код, что мышь) и показать под ним кольцо; dump — фактика кольца ТЕКУЩЕЙ ")
	TEXT("цели без её смены; off — снять выделение. Необязательная ПОДСТРОКА фильтрует цель по ")
	TEXT("имени модели или имени сущности — так проверяется посадка кольца на моделях разного ")
	TEXT("роста (напр. «asda.Target.RingNearest npc wendy»)."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W, FOutputDevice& Ar)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		AAsdaPlayerController* PC = GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
		if (!PC) { Ar.Logf(TEXT("[кольцо] нет AsdaPlayerController — PIE поднят без AsdaWorldGameMode?")); return; }
		const FString Kind = Args.Num() > 0 ? Args[0].ToLower() : TEXT("npc");
		if (Kind == TEXT("off")) { PC->ClearTarget(); Ar.Logf(TEXT("[кольцо] выделение снято")); return; }
		// ★ `dump` НИЧЕГО не выбирает: только печатает фактику уже висящего кольца. Нужен, чтобы
		//   доказать разовость — второй замер через несколько секунд должен показать ТОТ ЖЕ объект
		//   (не пересоздан) в состоянии ПОГАШЕН (не крутится).
		if (Kind == TEXT("dump")) { AsdaDumpTargetRingFacts(PC->TargetActor.Get(), Ar); return; }

		// Точка отсчёта: павн, если он есть (живая игра), иначе точка обзора контроллера (PIE без сервера).
		FVector From = FVector::ZeroVector;
		if (const APawn* P = PC->GetPawn()) { From = P->GetActorLocation(); }
		else { FRotator R; PC->GetPlayerViewPoint(From, R); }

		// ★ НЕОБЯЗАТЕЛЬНЫЙ ФИЛЬТР ПО ИМЕНИ (задача 2026-08-21: «проверь на NPC разного роста»).
		//   Без него команда всегда берёт БЛИЖАЙШУЮ цель, а это может оказаться модель, у которой
		//   низ геометрии уходит под корень (у `opara` — на 54.8), и посадка кольца на подошву
		//   на ней просто не видна. Сравниваем с именем МОДЕЛИ (скелетал-ассет) и именем сущности —
		//   оба доступны и в игре, редакторских API здесь нет.
		const FString Want = Args.Num() > 1 ? Args[1].ToLower() : FString();
		auto NameMatches = [&Want](const AActor* A, const FString& EntityName) -> bool
		{
			if (Want.IsEmpty()) { return true; }
			if (EntityName.ToLower().Contains(Want)) { return true; }
			if (const USkeletalMeshComponent* Sk = A->FindComponentByClass<USkeletalMeshComponent>())
			{
				if (const USkeletalMesh* M = Sk->GetSkeletalMeshAsset())
				{
					if (M->GetName().ToLower().Contains(Want)) { return true; }
				}
			}
			return false;
		};

		AActor* Best = nullptr;
		float BestD = TNumericLimits<float>::Max();
		if (Kind == TEXT("mob"))
		{
			for (TActorIterator<AAsdaMobActor> It(W); It; ++It)
			{
				AAsdaMobActor* A = *It;
				if (!A || !NameMatches(A, A->MobName)) { continue; }
				const float D = FVector::Dist2D(A->GetActorLocation(), From);
				if (D < BestD) { BestD = D; Best = A; }
			}
		}
		else
		{
			for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
			{
				AAsdaNpcActor* A = *It;
				if (!A || A->NpcId <= 0 || !NameMatches(A, A->NpcName)) { continue; }
				const float D = FVector::Dist2D(A->GetActorLocation(), From);
				if (D < BestD) { BestD = D; Best = A; }
			}
		}
		if (!Best)
		{
			Ar.Logf(TEXT("[кольцо] на уровне нет акторов вида «%s»%s"), *Kind,
				Want.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" с именем, содержащим «%s»"), *Want));
			return;
		}
		// ★ Запечённый актор спрятан BeginPlay'ем (каталог-превью) — для КАДРА его надо показать.
		//   Это делается ТОЛЬКО здесь, в диагностической команде, и на игровой путь не влияет.
		Best->SetActorHiddenInGame(false);
		// ★ ДОВОДКА 07.09.2026 (В1 ревизии review_cpu_insights.md): показать МАЛО. После волны «сна
		//   запечённых мобов» (asda.Tick.DormantBaked) у такого актора усыплены меш и плашка, и команда
		//   показывала бы моба БЕЗ имени над головой и БЕЗ позы — то есть ломала сама себя как инструмент
		//   проверки посадки кольца/высоты плашки. Будим ровно тот актор, который показали.
		if (AAsdaMobActor* BakedMob = Cast<AAsdaMobActor>(Best))
		{
			if (!BakedMob->bNetSpawned) { BakedMob->SetDormantComponentTicks(true); }
		}
		PC->SetTarget(Best);
		Ar.Logf(TEXT("[кольцо] цель «%s» (%s), дистанция %.0f, позиция (%.0f,%.0f,%.0f)"),
			*Best->GetName(), *Kind, BestD,
			Best->GetActorLocation().X, Best->GetActorLocation().Y, Best->GetActorLocation().Z);
		// Фактика: какой эффект реально висит на цели (имя .jeb = цвет кольца) — ЕДИНАЯ точка печати.
		AsdaDumpTargetRingFacts(Best, Ar);
	}));
#endif // ASDA_DEV_CONSOLE
