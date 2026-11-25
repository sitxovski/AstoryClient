// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы AAsdaPlayerController
//   разнесены по группам в AsdaPlayerController_{Npc,Move,Keys}.cpp; общее (LogAsdaPC, чистые константы и хелперы) —
//   в AsdaPlayerControllerInternal.h. Строки перенесены БАЙТ В БАЙТ:
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaPlayerController.plan.json`.
//   Здесь осталось ядро: журнал боя (#if ASDA_COMBAT_TRACE — его читают клик, атака, маршрут, команды, PlayerTick), BeginPlay, цель, клики, AttackTick, маршрут, ExecuteCommand, PlayerTick.
// Реализация AAsdaPlayerController / AAsdaWorldGameMode — см. AsdaPlayerController.h.
#include "World/AsdaPlayerController.h"
#include "World/AsdaOrbitCursor.h"   // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: клик по миру при открытой доске объявлений — только закрыть (input.c:177)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.10.2026: GetCameraTurnScale — чувствительность поворота камеры
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaWindowSubsystem.h" // ★ единый реестр зон интерфейса: блок мирового клика под ЛЮБЫМ окном

#include "World/AsdaPlayerCharacter.h"
#include "GameFramework/PlayerInput.h"   // ★ 09.10.2026 (п. 4): `FKeyState::EventCounts` — второе нажатие двойного клика ЛКМ
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaTargetHighlightComponent.h"   // ★ подсветка выбранной цели (эталон g_pTargetHighlightPosA/B)
#include "World/AsdaWorldSubsystem.h"
#include "AsdaSelf.h"                       // ★ О3: свой userNum для записи пика `g_nPickedUserIdx`
#include "World/AsdaLoadManagerSubsystem.h"   // ★ CurrentMapId — развилка CampWar (mapId 19) перед окном 570
#include "World/AsdaMapRegistry.h"           // ★ 04.11.2026: зоны порталов (kPortalGraph) — клик по графике портала
#include "World/AsdaPortalGfx.h"             // ★ 04.11.2026: меши набора портала (домики, ворота, арки) — клик по модели портала
#include "Components/StaticMeshComponent.h"  // меш попадания клика (экземпляры ISM карты)
#include "Engine/StaticMesh.h"               // имя меша попадания
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcBodyRay.h"   // ★ 08.11.2026: спорный щелчок по NPC — по телу (asda.Npc.ClickByBody)
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
#include "UI/AsdaDisassemblySubsystem.h"       // ★ cmd 40 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaTradeStateSubsystem.h"       // ★ 29.09.2026: гейт клика по земле 9568 — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "UI/AsdaWarehouseSubsystem.h"   // ★ 30.09.2026 (ревизия Л4/Л5, СРЕДН-1): свиток возврата склада (+0x5734) — гейт 10305
#include "UI/AsdaSelfStoreSubsystem.h"         // ★ 30.09.2026 (волна Л5 торговли): фокус в поле чата лавки 23403 глушит горячие клавиши
#include "UI/AsdaNotice.h"                    // нотис 9568 в чат (фасад нотисов)
#include "UI/AsdaCraftSubsystem.h"            // ★ предикат «окно ремесла открыто» (гейт клика по миру, input.c:144-157)
#include "UI/AsdaPlayerMenuSubsystem.h"    // ★ контекстное меню по чужому игроку (окно 2000), ПКМ-отпускание
#include "UI/AsdaSkillUseSubsystem.h"       // ★ отмена отложенного «подбежать и применить навык»
#include "UI/AsdaSystemOptionSubsystem.h"   // ★ ESC-меню 69400 и окно настроек 69500 (клавиша ESC, захват клавиш)
#include "World/AsdaTargetHotkeys.h"        // ★ 25.10.2026: клавиши 59 «атаковать цель» / 60 «ближайший враг» (эталонные тела)
#include "UI/AsdaPartySubsystem.h"          // ★ 25.10.2026: клавиши 62 (Shift+Tab) / 64 (следующая общая цель)
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
// ★ PrintScreen (VK 44, cmd 44 «SCREEN SHOT») — единственная биндабельная клавиша оригинала,
//   которой в `EKeys` UE нет вовсе, поэтому её приходится опрашивать платформенно.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#endif
#include "AsdaPlayerControllerInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerController*.cpp (LogAsdaPC, чистые хелперы)
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя (клик → цель → бег → дальность → заявка → замах → выход)
#include "World/AsdaSkillCastLock.h"  // ★ 05.11.2026: «свой навык занят» — гейты клика/клавиш цели, отложенное действие после навыка
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaPlayerControllerInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPlayerController*.cpp

DEFINE_LOG_CATEGORY(LogAsdaPC);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaPlayerControllerInternal.h (один экземпляр на все части)

#if ASDA_COMBAT_TRACE
namespace AsdaCtPc {
// ★ Журнал боя (волна 2, 18.10.2026): общее с `UI/AsdaSkillUseSubsystem.cpp` (там — `extern`-объявления). Не члены
//   класса: заголовок контроллера (UCLASS) журналом не трогаем. Только редактор/Development.
	// Последняя боевая активность навыком (FPlatformTime, с): заявка навыка ушла (пишет диспетчер навыков) или навык
	//   ещё занят — заявка/показ/накопление/подход (пишет страховка цикла в `PlayerTick`). Страховка не закрывает цикл,
	//   пока от неё не прошло kSkillQuietS: бой «только навыками» не держит ни автоатаку, ни боевую стойку.
	double LastOwnSkillActS = 0.0;
	// Сколько секунд тишины навыков после их активности страховка ждёт, прежде чем закрыть цикл «боя нет».
	constexpr double kSkillQuietS = 3.0;
}
#endif // ASDA_COMBAT_TRACE

// ═════════════ ★★ 10.10.2026 (modal_lock2) ПРЕПРОЦЕССОР МЫШИ ПОД МОДАЛКОЙ ═════════════
//   Полный разбор — у объявления `ModalMouseRelay` (AsdaPlayerController.h). Работает, только пока модалка отобрала ввод мира
//   (`UAsdaWindowSubsystem::IsWorldInputCapturedByModal`) — иначе мир получает мышь обычным путём вьюпорта и пересылать нечего
//   (иначе колесо сработало бы дважды). Событие не съедается (false): Slate разбирает его дальше как обычно.
class FAsdaModalMouseRelay : public IInputProcessor
{
public:
	TWeakObjectPtr<AAsdaPlayerController> Owner;   // контроллер-владелец (слабая ссылка: препроцессор живёт в Slate)

	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

	// Колесо: эталонный путь `OnWheel` (окна по z через гейт модалки → «курсор над окном?» → зум камеры).
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& Ev, const FPointerEvent* Gesture) override
	{
		AAsdaPlayerController* PC = Captured(Ev);
		if (!PC || Gesture || FMath::IsNearlyZero(Ev.GetWheelDelta())) { return false; }
		UE_LOG(LogAsdaPC, Verbose, TEXT("[колесо] под модалкой: %+.0f → путь OnWheel (эталон game_main.c:5864)"), Ev.GetWheelDelta());
		PC->OnWheel(Ev.GetWheelDelta());
		return false;
	}
	// ПКМ нажата — только при ключе отката [ИСПРАВЛЕНИЯ] `asda.UI.ModalLockRightClick 0` (WM_RBUTTONDOWN эталона без гейта).
	virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Ev) override
	{
		AAsdaPlayerController* PC = Captured(Ev);
		if (PC && Ev.GetEffectingButton() == EKeys::RightMouseButton && RmbRollback()) { PC->OnRightPressed(); }
		return false;
	}
	// ПКМ отпущена — то же (WM_RBUTTONUP: меню по игроку на отпускании).
	virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Ev) override
	{
		AAsdaPlayerController* PC = Captured(Ev);
		if (PC && Ev.GetEffectingButton() == EKeys::RightMouseButton && RmbRollback()) { PC->OnRightReleased(); }
		return false;
	}
	// Проводка мыши при зажатой ПКМ (ключ отката) — вращение камеры.
	// ★ 10.10.2026 (modal_lock3, МЕЛК-1): проводка идёт осями MouseX/MouseY в `UPlayerInput` — так же, как её отдаёт вьюпорт без
	//   модалки (`FSceneViewport::OnMouseMove` копит дельту курсора Slate, Y со знаком минус, по одному отсчёту на событие;
	//   `ProcessAccumulatedPointerInput` шлёт `InputAxis`). Дальше `UPlayerInput::MassageAxisInput` применяет чувствительность
	//   оси (DefaultInput.ini, 0.07), поправку на угол обзора и сглаживание, и `GetInputMouseDelta` в `PlayerTick` отдаёт ту же
	//   величину, что и без модалки. Прежде сырые пиксели прибавлялись вручную — вращение было в ~14 раз быстрее.
	virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& Ev) override
	{
		AAsdaPlayerController* PC = Captured(Ev);
		if (PC && PC->bRMBDown && RmbRollback() && PC->PlayerInput)
		{
			const FVector2D D = Ev.GetCursorDelta();   // дельта курсора Slate за событие
			const FInputDeviceId Dev = Ev.GetInputDeviceId();   // устройство события (мышь); фильтр по пользователю не нужен — сразу в UPlayerInput
			FViewport* Vp = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->Viewport : nullptr;   // игровой вьюпорт
			const float Dt = FApp::GetDeltaTime();     // время кадра, как у `ProcessAccumulatedPointerInput`
			if (D.X != 0.0) { PC->PlayerInput->InputKey(FInputKeyEventArgs(Vp, Dev, EKeys::MouseX, (float)D.X, Dt, 1, 0u)); }
			if (D.Y != 0.0) { PC->PlayerInput->InputKey(FInputKeyEventArgs(Vp, Dev, EKeys::MouseY, (float)-D.Y, Dt, 1, 0u)); }
		}
		return false;
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("AsdaModalMouseRelay"); }

private:
	// Контроллер, если модалка отобрала ввод мира и событие пришлось на игровой вьюпорт (окна редактора в PIE не трогаем).
	AAsdaPlayerController* Captured(const FPointerEvent& Ev) const
	{
		AAsdaPlayerController* PC = Owner.Get();
		UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
		const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM || !WM->IsWorldInputCapturedByModal() || !GEngine || !GEngine->GameViewport) { return nullptr; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid() || !Vp->GetTickSpaceGeometry().IsUnderLocation(Ev.GetScreenSpacePosition())) { return nullptr; }
		return PC;
	}
	// Ключ отката [ИСПРАВЛЕНИЯ] «ПКМ под модалкой заперт» снят (0 — ПКМ МИРУ, как у оригинала; окнам — никогда, modal_lock3).
	static bool RmbRollback()
	{
		static const IConsoleVariable* Cv = ASDA_FIND_DEV_CVAR(TEXT("asda.UI.ModalLockRightClick"));
		return Cv && Cv->GetInt() == 0;
	}
};

AAsdaPlayerController::AAsdaPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void AAsdaPlayerController::BeginPlay()
{
	Super::BeginPlay();
	Keys = NewObject<UAsdaKeybindConfig>(this);
	// ★★★ РАСКЛАДКУ РЕШАЕТ INI, А НЕ ХАРДКОД (эталон keybindInputCtrlInit@0xE3BAF0 читает
	//   `<GameDir>\system\Shortcutkey.ini` сразу после дефолтов). Порядок: дефолты → профиль-DataAsset
	//   → ini (побеждает). Раньше порт жил на одних хардкод-дефолтах — отсюда неверные клавиши
	//   (PAGE = Shift+F1..F5 вместо F1..F5, NEW BAR SLOT = F1..F12 вместо Shift+1..9/LEFT/[/]).
	Keys->LoadLayout(KeybindProfile);
	UE_LOG(LogAsdaPC, Log, TEXT("%s"), *Keys->DumpLayout(/*bOnlyBound=*/true));
	// ★ 10.10.2026 (modal_lock2): колесо (и ПКМ при ключе отката) под модалкой — препроцессором Slate, см. объявление.
	if (IsLocalController() && FSlateApplication::IsInitialized())
	{
		TSharedRef<FAsdaModalMouseRelay> Relay = MakeShared<FAsdaModalMouseRelay>();
		Relay->Owner = this;
		ModalMouseRelay = Relay;
		FSlateApplication::Get().RegisterInputPreProcessor(Relay);
	}
}

void AAsdaPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ModalMouseRelay.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(ModalMouseRelay);
	}
	ModalMouseRelay.Reset();
	Super::EndPlay(Reason);
}

AAsdaPlayerCharacter* AAsdaPlayerController::GetChar() const
{
	return Cast<AAsdaPlayerCharacter>(GetPawn());
}

// ★★ Единый гейт «мы мертвы»: поза/клип павна ИЛИ сетевое зеркало серверного CUser+79.
bool AAsdaPlayerController::IsSelfDead() const
{
	if (const AAsdaPlayerCharacter* Ch = GetChar())
	{
		if (Ch->IsIncapacitated()) { return true; }
	}
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			return Net->bSelfDead;
		}
	}
	return false;
}

void AAsdaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Мышь: клик-муф, ПКМ-орбит, колесо-зум (в оригинале камера — только мышь). Клавиатура — опросом (PollKeys).
	// ★ 09.10.2026 (п. 4 владельца): IE_Pressed идёт через `OnLeftPressedInput` — второе нажатие двойного одиночный разбор не зовёт.
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAsdaPlayerController::OnLeftPressedInput);
	// ★★ ДВОЙНОЙ ЛКМ ИДЁТ В ТУ ЖЕ ФУНКЦИЮ РАЗБОРА, ВТОРОЙ КОПИИ НЕТ — меняется только ПРИЗНАК.
	//   Эталон держит две ТОЧКИ ВХОДА (`Input_WorldClickHandler@0x968890` по флагу
	//   `g_WorldInputFlags & 1` для WM_LBUTTONDOWN и `world_OnLeftClickPick@0x966980` по флагу `& 2`
	//   для WM_LBUTTONDBLCLK), но ветка NPC (`g_nInputControlState == 3`) в них ПОБАЙТОВО ОДИНАКОВА
	//   — различается лишь последний аргумент пика. То есть у ОРИГИНАЛА окно открывается с первого
	//   клика, и это ровно тот пункт, который владелец решил изменить 2026-08-21: одиночный клик по
	//   NPC теперь ТОЛЬКО выделяет, окно и подбегание — по двойному (`asda.Npc.OpenOnSingleClick 1`
	//   возвращает эталонное поведение). Разбор разъезда — `Reports/npc_target_ring.md` §3.
	//   ★★★ ПРОВЕРЕНО ПО ИСХОДНИКУ ДВИЖКА (НЕ по интуиции): `IE_DoubleClick` НЕ ЗАМЕНЯЕТ `IE_Pressed`.
	//   `UPlayerInput::InputKey` (Engine/Source/Runtime/Engine/Private/UserInterface/PlayerInput.cpp:461-465)
	//   в ветке `case IE_DoubleClick:` кладёт события В ОБА аккумулятора — `[IE_Pressed]`, затем
	//   `[IE_DoubleClick]`. Значит на втором нажатии сначала приходит `OnLeftClickSingle`, а следом
	//   `OnLeftClickDouble` (порядок — по возрастанию EventCount, по нему же `ProcessInputStack`
	//   сортирует делегаты). Отсюда ДВА следствия, на которых держится вся правка:
	//     1) ветку NPC нельзя писать как «одиночный открывает при повторном клике» — её открыл бы
	//        `IE_Pressed` второго нажатия ДО прихода двойного, и требование владельца сломалось бы;
	//     2) двойной клик по мобу/луту/земле как выполнялся дважды (обе привязки вели в одну
	//        функцию и ДО этой правки), так и выполняется — поведение боя, лута и клик-мува
	//        НЕ ИЗМЕНИЛОСЬ ни на шаг.
	//   ⚠ (МЕЛК-1 ревизии six_items, 09.10.2026) Абзац выше — про ОДИНОЧНЫЙ разбор второго нажатия; с 09.10.2026 он верен
	//   ТОЛЬКО при `asda.Input.DblClickLegacySingle 1`. По умолчанию (0) второго одиночного нет: `OnLeftPressedInput`
	//   пропускает `IE_Pressed`, если у ЛКМ в этом кадре есть `IE_DoubleClick` (`IsLeftPressPartOfDouble`), а
	//   `OnLeftClick(bDouble = true)` не исполняет ветки «земля», «ничего» и «лут» — как эталон (WM_LBUTTONDBLCLK →
	//   только `world_OnLeftClickPick`: моб, NPC, зона). Пункт 1) (NPC) верен в обоих режимах.
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_DoubleClick, this, &AAsdaPlayerController::OnLeftClickDouble);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAsdaPlayerController::OnRightPressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AAsdaPlayerController::OnRightReleased);
	InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AAsdaPlayerController::OnWheel);
}

// ★★★ КЛЮЧ ОТКАТА К ЭТАЛОННОМУ ПОВЕДЕНИЮ КЛИКА ПО NPC (по умолчанию ВЫКЛЮЧЕН).
//   ЭТАЛОН: одиночный и двойной ЛКМ ведут в ОДНУ ветку, окно разговора открывается с ПЕРВОГО
//     клика (обе точки входа побайтово одинаковы — см. комментарий в `SetupInputComponent`).
//   ОТСТУПЛЕНИЕ (решение владельца 2026-08-21): одиночный клик по NPC ТОЛЬКО выделяет цель,
//     окно и подбегание запускает ДВОЙНОЙ. `asda.Npc.OpenOnSingleClick 1` возвращает эталон.
ASDA_DEV_CVAR(int32, CVarAsdaNpcOpenOnSingleClick,
	TEXT("asda.Npc.OpenOnSingleClick"), 0,
	TEXT("Окно разговора с NPC открывается с ОДИНОЧНОГО клика (1 = как в эталоне) ")
	TEXT("или только с ДВОЙНОГО (0 = по умолчанию, решение владельца). ")
	TEXT("Выбор цели одиночным кликом работает в обоих режимах."),
	ECVF_Default);

// ★★★ 27.09.2026 — ЭТАЛОННАЯ СХЕМА СТАРТА И СКОРОСТИ СВОЕГО ХОДА (обоснование — у `IsEtalonSpeedSync` в .h,
//   полный разбор — `Reports/owner_0927/move_4007.md`). 1 = эталон (по умолчанию после самотеста
//   `asda.Move.SelfTest4007`: 0 провалов при обоих значениях), 0 = прежнее поведение порта (оптимистичный старт
//   сразу после 4006 со скоростью сервера, снап в серверный cur при расхождении > 150).
static ASDA_DEV_CVAR(int32, CVarAsdaMoveEtalonSpeedSync,
	TEXT("asda.Move.EtalonSpeedSync"), 1,   // ★ 27.09: умолчание 1 возвращено после исправления К1 (подход в своей клетке 100×100 без 4006, move_4007_fix.md); самотест 124/0 при обоих ключах
	TEXT("Свой ход по клику/маршруту: 1 = как в оригинале — персонаж трогается только по разрешению сервера (4007), ")
	TEXT("скорость подгоняется под серверное время, пока запрос хода в пути — клики по миру и навыки ждут ответа; ")
	TEXT("0 = прежний порт — идти сразу после клика со скоростью сервера. Клавиатура (WASD): старт и скорость шага ключ ")
	TEXT("не меняет, но её 4006 тоже взводят «запрос хода в пути» — при 1 на время ответа сервера навыки и клики ждут ")
	TEXT("и при беге на клавишах. Подход к цели внутри своей клетки 100×100 идёт без 4006 (как в оригинале)."),
	ECVF_Default);

bool AAsdaPlayerController::IsEtalonSpeedSync()
{
	return CVarAsdaMoveEtalonSpeedSync.GetValueOnGameThread() != 0;
}

ASDA_DEV_CVAR(int32, CVarAsdaFishLegacyDeferredClick,
	TEXT("asda.Fish.LegacyDeferredClick"), 0,
	TEXT("0 = клик по миру во время рыбалки отбрасывается (порт); 1 = как оригинал — клик/двойной клик запоминаются и ")
	TEXT("исполняются в первом кадре после конца рыбалки (по текущему курсору)."),
	ECVF_Default);

// ★ Ревизия В4 МЕЛК-6 (рыбалка, 09.10.2026) [ПОРТ]: Alt-сочетания и F10 при рыбалке. Эталон их НЕ глушит: это WM_SYSKEYDOWN/UP
//   (`MainWndProc` g:24326-24353, F10 → `Input_OnKeyDown_Dispatch(121)`), а гейт fishState стоит только у WM_KEYDOWN/UP. Порт по
//   умолчанию съедает их вместе с прочими (Alt-биндов в раскладке порта нет — последствий нет; PORT_PENDING «Рыбалка В4» п.2).
static ASDA_DEV_CVAR(int32, CVarAsdaFishEatSysKeys,
	TEXT("asda.Fish.EatSysKeys"), 1,
	TEXT("1 = при рыбалке Alt-сочетания и F10 съедаются, как прочие клавиши (порт); 0 = как оригинал — проходят ")
	TEXT("(WM_SYSKEYDOWN мимо гейта fishState), ответом мини-игры не засчитываются."),
	ECVF_Default);

bool AAsdaPlayerController::IsFishEatSysKeys()
{
	return CVarAsdaFishEatSysKeys.GetValueOnGameThread() != 0;
}

// ★★★ СЛУШАТЕЛЬ ЗВУКА В ПЕРСОНАЖЕ [ОРИГИНАЛ] (разбор palace_of_queen, 11.09.2026).
//   Эталон Camera_UpdateListener@0xA220E0 (render_core.c:3393-3402), вызов с LocalPlayer (entity_char.c:7687):
//   позиция слушателя FMOD3 = LocalPlayer[111..113] — мировая позиция игрока (+444/+452 = X/Z, ср.
//   game_main.c:6717 `pPlayerWorld`), ориентация — от камеры. UE по умолчанию слушает камерой, отстоящей от
//   персонажа, и дистанции затухания всех 3D-звуков считались не от той точки. Переопределяем только ТОЧКУ
//   затухания (панорама остаётся от камеры — как у оригинала). 0 = слушатель в камере (прежнее поведение).
static int32 GAsdaSoundListenerAtPlayer = 1;
static FAutoConsoleVariableRef CVarAsdaSoundListenerAtPlayer(
	TEXT("asda.Sound.ListenerAtPlayer"),
	GAsdaSoundListenerAtPlayer,
	TEXT("1 = затухание 3D-звука считается от персонажа, как в оригинале; 0 = от камеры (движковое умолчание)."),
	ECVF_Default);

void AAsdaPlayerController::PlayerTick(float DeltaTime)
{
	// Точка затухания звука = персонаж (см. asda.Sound.ListenerAtPlayer выше). Установка — три присваивания,
	//   ставим каждый тик: павн после травела/воскрешения меняется, отдельное отслеживание не нужно.
	{
		AAsdaPlayerCharacter* EarChar = (GAsdaSoundListenerAtPlayer != 0) ? GetChar() : nullptr;
		if (EarChar && EarChar->GetRootComponent())
		{
			SetAudioListenerAttenuationOverride(EarChar->GetRootComponent(), FVector::ZeroVector);
		}
		else
		{
			ClearAudioListenerAttenuationOverride();
		}
	}
	// ── ★ АДМИН-РЕЖИМ (наша надстройка, не порт): хоткеи CTRL+SHIFT+ALT+Q / F12 / ESC. ──
	//   В свободной камере обычное управление ОТКЛЮЧЕНО целиком (иначе WASD одновременно летел бы
	//   камерой и гнал персонажа), ввод целиком уходит камере.
	// ★ РЕВИЗИЯ 2026-09-17 (С6): админ-ветка сообщает, поглотила ли она ESC (закрытие GM-меню /
	//   выход из свободной камеры). Без этого признака то же нажатие уходило ниже и открывало
	//   ESC-меню 69400 — одно нажатие давало ДВА действия.
	bool bEscTakenByAdmin = false;
	if (UAsdaAdminSubsystem* Adm = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAdminSubsystem>() : nullptr)
	{
		bEscTakenByAdmin = Adm->PollHotkeys(this);
		if (Adm->IsFreeCamActive())
		{
			Super::PlayerTick(DeltaTime);
			Adm->TickFreeCam(this, DeltaTime);
			return;
		}
	}
	// ── ★ ESC-МЕНЮ 69400 И ОКНО НАСТРОЕК 69500 (`UAsdaSystemOptionSubsystem`) ──
	//   ESC в эталоне обрабатывает НЕ диспетчер раскладки: `Input_OnKeyDown_Dispatch@0xE3C7D0`
	//   первой же строкой отсекает VK 27, а меню открывает отдельный путь
	//   (`GameInput_DispatchEvent` case 2 / `inputHandleInventoryAndEscHotkeys@0xC6E710`).
	//   Здесь же живёт и приём клавиши в режиме переназначения (окно настроек открыто).
	//   ⚠ Порядок важен: админ-подсистема выше уже могла съесть ESC (выход из свободной камеры
	//   и закрытие GM-меню), и тогда её ветка вернула управление до сюда либо закрыла своё окно.
	if (UAsdaSystemOptionSubsystem* SysOpt = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr)
	{
		SysOpt->PollHotkeys(this, bEscTakenByAdmin);
	}
	// ── ★ ПАСХАЛКА «ПОКАТАТЬСЯ» (пункт админ-меню): пока сидим в машине, обычное управление
	//   персонажем ОТКЛЮЧЕНО целиком — ровно как у свободной камеры выше, иначе WASD одновременно
	//   ехал бы машиной и гнал персонажа. Косметика: сети нет, серверу ничего не уходит.
	if (UAsdaVehicleRideSubsystem* Ride = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaVehicleRideSubsystem>() : nullptr)
	{
		if (Ride->IsRiding())
		{
			Super::PlayerTick(DeltaTime);
			Ride->TickRide(this, DeltaTime);
			return;
		}
	}
	// ★ 29.10.2026 [ОРИГИНАЛ] (asda.Map.DestMarkClear): главный тик эталона в состояниях +47551 из набора LABEL_111
	//   (5 атака, 24 навык, 22, 15, 25, 14/27 каст, 7/8/9 сидение, 102, 105, 16, 6 смерть) и при разговоре с NPC
	//   (`g_bInventoryLocked`) КАЖДЫЙ кадр ставит цель := своя клетка и гасит метку (game_main.c:1668-1719).
	if (AsdaDestMarkClearOn())
	{
		const UAsdaMinimapSubsystem* DmMini = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		if (DmMini && DmMini->IsDestinationActive())
		{
			const int32 St = KbEtalonActionState();
			const bool bStateSet = St == 5 || St == 24 || St == 22 || St == 15 || St == 25 || St == 14 || St == 27
				|| St == 7 || St == 8 || St == 9 || St == 102 || St == 105 || St == 16 || St == 6;
			const UAsdaNpcDialogSubsystem* DmDlg = GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>();
			const bool bTalk = DmDlg && DmDlg->IsDialogOpen();
			if (bStateSet || bTalk)
			{
				AsdaDestMarkClear(GetGameInstance(), bTalk ? TEXT("разговор с NPC (g_bInventoryLocked, LABEL_111)")
					: *FString::Printf(TEXT("состояние %d (LABEL_111, game_main.c:1707)"), St));
			}
		}
	}
	UpdateHover();   // ★ наведение каждый кадр (имя моба видно ТОЛЬКО при наведении — эталон)
	TargetHighlightTick();   // ★ подсветка цели: гаснет на смерти цели (эталон actionState==10)
	// ★ Повтор метки у точки назначения во время автобега (~раз в секунду) — как в оригинале.
	if (bMovingToPoint)
	{
		const AAsdaPlayerCharacter* C = GetChar();
		const bool bArrived = !C || FVector::Dist2D(C->GetActorLocation(), LastMoveDestUe) < 80.f;
		if (bArrived) { bMovingToPoint = false; }
		else if (FPlatformTime::Seconds() - LastMarkerTime > 1.0) { SpawnClickMarker(LastMoveDestUe); }
	}
	// ★ Ревизия В4 МЕЛК-6 (рыбалка, `asda.Fish.LegacyDeferredClick 1`): клик, сделанный во время рыбалки, исполняется в первом
	//   кадре после её конца — как эталон (биты 1/2 `g_WorldInputFlags` пережили гейт `sub_92A968`), по текущему курсору.
	if ((bFishDeferredSingle || bFishDeferredDouble) && !IsFishingNow())
	{
		const bool bS = bFishDeferredSingle, bD = bFishDeferredDouble;
		bFishDeferredSingle = bFishDeferredDouble = false;
		if (CVarAsdaFishLegacyDeferredClick.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[клик] рыбалка окончена — исполняется отложенный клик (одиночный %d, двойной %d)"), bS ? 1 : 0, bD ? 1 : 0);
			if (bS) { OnLeftClickSingle(); }   // `Input_WorldClickHandler` (бит 1)
			if (bD) { OnLeftClickDouble(); }   // `world_OnLeftClickPick` (бит 2)
		}
	}
	// ★ 05.11.2026 (`AsdaSkillCastLock`): навык окончен — отложенные клик/приказ/клавиша/предмет исполняются; до боевого тика, чтобы
	//   отложенный ход (действие 16 эталона) шёл раньше, чем тик атаки вернёт замах (у эталона 16 вытесняет удержание 100).
	AsdaSkillCastLock::Tick(this);
	Super::PlayerTick(DeltaTime);
	RouteTick();    // ★ маршрут с обходом препятствий: продвижение по узлам + пересчёт при перекрытии
	AttackTick();   // ★ боевой цикл (порт FSM=5 + ResolveBasicAttack): подход к цели / удары
#if ASDA_COMBAT_TRACE
	// Журнал боя: свой персонаж умер при открытом цикле (в т.ч. вне автоатаки — в стойке после боя) — цикл закрыт один раз.
	if (AsdaCombatTrace::CurrentCycle() > 0 && IsSelfDead()) { ASDA_CTRACE_END(TEXT("смерть своего персонажа")); }
	// ★ Страховка от зависшего цикла (волна 2): автоатаку гасят и мимо END (отказ сервера OnCombatRefused, гейт окна
	//   ремесла, отказ на подходе, снятие цели посреди подхода) — тогда цикл закрывается здесь, когда боя нет совсем:
	//   ни автоатаки, ни PvP-атаки, ни боевой стойки (CombatStanceUntil > 0 держит цикл ради трупа/награды до
	//   ExitCombatStance), ни навыка. Навык «занят» = заявка/показ/накопление (CharState ≠ 0), активный слот или подход
	//   ради навыка; после него — ещё kSkillQuietS тишины (бой «только навыками» стойку не взводит, а результат/смерть
	//   цели приходят следом за показом).
	if (AsdaCombatTrace::CurrentCycle() > 0 && !bAutoAttack && !bPvpAttack && CombatStanceUntil <= 0.0)
	{
		const UAsdaSkillUseSubsystem* SkCt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		const double NowCt = FPlatformTime::Seconds();
		if (SkCt && (SkCt->HasSkillApproach() || SkCt->GetActiveSkill() >= 0 || SkCt->GetSelfCharState() != 0))
		{
			AsdaCtPc::LastOwnSkillActS = NowCt;   // навык ещё идёт — отсчёт тишины начнётся после него
		}
		else if (NowCt - AsdaCtPc::LastOwnSkillActS >= AsdaCtPc::kSkillQuietS)
		{
			ASDA_CTRACE_END(TEXT("боя нет, стойки нет"));
		}
	}
#endif
	// ★ Отложенный подбор лута (эталон state 103): дошли до ячейки предмета → 5012.
	if (bLootPending)
	{
		if (AAsdaPlayerCharacter* C = GetChar())
		{
			const FVector2D CurG = C->GetGamePosXZ();
			const FIntPoint MyTile = AsdaCoord::GameToTile(CurG);   // ★ Аудит №3 (B2)
			if (MyTile == PendingLootTile)
			{
				bLootPending = false;
				if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
				{
					if (Net->bInGameConnected) { Net->SendItemPick5012(MyTile, PendingLootTile); }
				}
			}
		}
	}
	NpcTalkTick();   // ★ отложенный разговор с NPC: дошли до зоны → 4008 + окно 570 (этап Э1)
	// Кулдаун боевой стойки (если сервер не прислал 4205).
	if (CombatStanceUntil > 0.0 && FPlatformTime::Seconds() >= CombatStanceUntil) { ExitCombatStance(); }
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return; }

	// ── Камера: ПКМ-драг орбит 0.01 рад/px (плавный клавиатурный разворот A/D — ниже, в блоке движения) ──
	// ★ 09.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Camera.OrbitHoldThrough`): сброс клавиш от ухода фокуса полю ввода погасил поворот, а ПКМ зажата —
	//   поворот продолжается (разбор — шапка `Public/World/AsdaOrbitCursor.h`, п. 2).
	if (!bRMBDown && FAsdaOrbitCursor::TakeRearm()) { bRMBDown = true; }
	FAsdaOrbitCursor::ReportOrbit(bRMBDown);   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: сторож захвата курсора (`asda.Camera.OrbitCursorLock`)
	if (bRMBDown)
	{
		float DX = 0.f, DY = 0.f;
		// ★ modal_lock3 (МЕЛК-1): под модалкой при ключе отката ПКМ оси MouseX/MouseY подаёт в `UPlayerInput` препроцессор
		//   (`FAsdaModalMouseRelay`) — значение здесь уже с чувствительностью и сглаживанием, как без модалки.
		GetInputMouseDelta(DX, DY);
		// ★ 19.10.2026 [ПОРТ]: «Чувствительность поворота камеры» окна настроек (строка I3) — множитель к 0.01 рад/px.
		//   RMBDragPixels ниже копит НЕмасштабированные пиксели: порог меню по игроку — про руку, а не про камеру.
		// ★ 07.11.2026: при захвате дельта сырая — `DeltaScale` возвращает скорость указателя Windows, поворот на сдвиг мыши прежний.
		const float TurnRad = 0.01f * UAsdaGameUserSettings::GetCameraTurnScale() * FAsdaOrbitCursor::DeltaScale();
		CamYawDeg   += DX * FMath::RadiansToDegrees(TurnRad);
		CamPitchDeg = FMath::Clamp(CamPitchDeg + DY * FMath::RadiansToDegrees(TurnRad), -80.f, -5.f);
		// ★ Накапливаем «сколько курсор проехал за зажим ПКМ»: контекстное меню по игроку
		//   (окно 2000) не должно выскакивать после разворота камеры. Оригинал такой проверки не
		//   имеет — это наша правка дефекта, выключатель asda.PlayerMenu.OpenAfterDrag.
		RMBDragPixels += FMath::Abs(DX) + FMath::Abs(DY);
	}

	// ── Клавиатура: движение по флагам + прочие команды по отпусканию ──
	PollKeys();

	// ══════════════════════════════════════════════════════════════════════════════════════════════
	//  ★★★ ОТКРЫТА СЦЕНА ПРЕМИУМ-ГАЧИ → ДВИЖЕНИЕ И КАМЕРА НЕ ОБРАБАТЫВАЮТСЯ.
	//
	//  ТОЧКА 1 — `math_geom.c:417`, `updatePlayerMovement@0x95CDF0`. Гейт стоит на ВСЕЙ функции:
	//      if ( !sub_934E86(g_pSellWindow) && !sub_929DC4(g_bSomeFeatureEnabled)
	//           && !sub_922D5D(g_pGachaScene) && sub_9384DC(g_pInventoryBagWindow) != 1
	//           && CharManager_GetLocalPlayer(...)[1].gap_A42[15039] != 1
	//           && !CObject_GetField5581(g_pWarehouseWnd) )
	//  — то есть при открытой сцене НЕ выполняется НИЧЕГО из тела: ни автобег, ни разрешение
	//  конфликта W+S, ни плавный разворот A/D (`g_fCameraYaw`), ни стрейф, ни отправка 4006, ни
	//  ветка полного отпускания клавиш (4008 + сброс цели).
	//  ★ ПОПРАВКА 27.09.2026: `gap_A42[15039]` — НЕ смерть, а «идёт добыча» (состояние 68: пишут 6753/6756 и очередь
	//  `charProcessStateChangeQueue`; при смерти флаг снимается). В порте этой системы нет (реестр отложенного). Весь
	//  верхний гейт при `asda.Move.KeyboardEtalon 1` — `KbTopGateBlocked` (`AsdaPlayerKeyboardEtalon.cpp`).
	//
	//  ТОЧКА 2 — `entity_char.c:7978`, `LocalPlayer_UpdateRender@0x9D4170`:
	//      if ( !sub_922D5D(g_pGachaScene) ) { uiAnimUpdate(g_pRenderer, dt);
	//                                          cameraUpdate(g_pRenderer, позиция игрока); }
	//  `uiAnimUpdate@0x95A960` — тряска экрана, `cameraUpdate@0x959880` — слежение камеры за
	//  персонажем. Пока сцена открыта, камера ЗАМОРОЖЕНА: накопленный ПКМ-орбитом `g_fCameraYaw`
	//  никуда не применяется. В порту роль `cameraUpdate` играет единственная строка
	//  `Ch->CameraArm->SetRelativeRotation(...)` в блоке движения ниже — значит выходим ДО неё
	//  (в отличие от ветки `IsSelfDead()`, где камера намеренно остаётся живой).
	//
	//  ФРОНТ ОТКРЫТИЯ. Эталон `premiumGacha_BeginRoll@0xB6DF80` (`world_map.c:4707-4712`) перед
	//  взводом гейта зовёт `followTargetCancelReset(&g_bMapTransferActive)` и
	//  `CharFx_AutoSabzil_State(localPlayer, 0, 0, 0)` — снимает автобег/маршрут/намерение подбора,
	//  а стоп-пакет 4008 шлёт САМА гача (`Net_SendActionPacket`, `world_map.c:4714`). Поэтому здесь
	//  гасим только ЛОКАЛЬНЫЕ намерения и НИЧЕГО не отправляем: второй 4008 из тика был бы дублем
	//  пакета и нарушением закона «не рвать сеть из колбэка».
	// ══════════════════════════════════════════════════════════════════════════════════════════════
	const bool bGachaSceneOpen = AsdaGachaSceneOpen(GetGameInstance());
	if (bGachaSceneOpen)
	{
		if (!bGachaGateLatched)
		{
			bGachaGateLatched = true;
			CancelRoute();                                            // followTargetCancelReset
			CancelPendingSkillApproach(TEXT("открыта сцена премиум-гачи"));
			bLootPending = false;                                     // намерение подбора (state 103)
			ClearPendingNpcTalk(TEXT("открыта сцена премиум-гачи"));   // ★ D5
			bMovingToPoint = false;
			UE_LOG(LogAsdaPC, Log,
				TEXT("[гача] сцена открыта — движение, клавиши и камера заблокированы ")
				TEXT("(эталон math_geom.c:417 / input.c:3956 / entity_char.c:7978)"));
		}
		bMoveFwd = bMoveBack = bMoveLeft = bMoveRight = false;
		bAutoRunActive = false; AutoRunHoldStart = 0.0;
		KeyMoveCommittedCell = FIntPoint(MAX_int32, MAX_int32);
		bPrevFwdP = bPrevBackP = false;
		if (bWasMoving) { Ch->StopMove(); bWasMoving = false; }       // ЛОКАЛЬНЫЙ стоп, 4008 не шлём
		return;                                                       // камера не обновляется
	}
	if (bGachaGateLatched)
	{
		bGachaGateLatched = false;
		UE_LOG(LogAsdaPC, Log, TEXT("[гача] сцена закрыта — управление возвращено"));
	}

	// ── ★ ВОЛНА В2 РЫБАЛКИ (08.10.2026): идёт рыбалка → клавиатурное движение отключено ─────────────
	//   Эталон: при fishState ≠ 0 ни WM_KEYDOWN (`input_SetMoveFlagFromKey`), ни WM_KEYUP (case 51-54) не доходят до
	//   флагов хода (game.c:24144/24218); вход в рыбалку (FSM 109/102) сам останавливает персонажа (стоп + 4008).
	//   Камера живёт (ПКМ-орбит, колесо) — как у ветки смерти ниже. Пакетов здесь нет.
	if (IsFishingNow())
	{
		bMoveFwd = bMoveBack = bMoveLeft = bMoveRight = false;
		bAutoRunActive = false; AutoRunHoldStart = 0.0;
		bWasMoving = false;
		if (IsKeyboardEtalon()) { KbResetState(); }
		if (Ch->CameraArm) { Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f)); }
		return;
	}

	// ── ★★ МЁРТВ → клавиатурное движение отключено ────────────────────────────────────────────────
	//   Эталон math_geom.c:622 (Player_KeyboardMoveTick): при isMovementActionState нажатие WASD НЕ
	//   двигает персонажа — вместо движения дёргается charToggleSitStand (в состоянии смерти это ничего
	//   не меняет). Камера (ПКМ-орбит, колесо) и UI при этом продолжают работать — как в оригинале.
	if (IsSelfDead())
	{
		bMoveFwd = bMoveBack = bMoveLeft = bMoveRight = false;
		bAutoRunActive = false; AutoRunHoldStart = 0.0;
		bWasMoving = false;
		if (Ch->IsMoving()) { Ch->StopMove(); }
		// ★ 27.09.2026 (`asda.Move.KeyboardEtalon 1`): у эталона при смерти (состояние 6) нажатие ведёт в `charToggleSitStand`
		//   (ничего), а отпускание после смерти сработало бы на трупе (4006 отказан состоянием 6, `SetAction(0)` на трупе).
		//   [ПОРТ] Состояние клавиатурной схемы просто сбрасывается: после воскрешения ветка отпускания не срабатывает.
		if (IsKeyboardEtalon()) { KbResetState(); }
		NoteMove4006WithoutLocalStart();   // ★ 22.10.2026 (ревизия локального старта, ВАЖНО-5): смерть — ожидание ответа снято
		// Камера остаётся живой: ПКМ-орбит уже посчитан выше, применяем его к стреле и выходим.
		if (Ch->CameraArm) { Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f)); }
		return;
	}

	// ★★ 22.10.2026 [ИСПРАВЛЕНИЕ] «стан» — жёсткая остановка своего персонажа (`asda.Move.StunHardStop`, разбор и тело —
	//   `StunHardStopTick`, AsdaPlayerKeyboardEtalon.cpp): под состоянием тик хода (обе схемы клавиш) не исполняется, камера живёт.
	if (StunHardStopTick(Ch))
	{
		if (Ch->CameraArm) { Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f)); }
		return;
	}
	LocalStartTick(Ch);   // ★ 22.10.2026: потолок локального хода без гранта (`asda.Move.LocalStart`, ревизия ВАЖНО-4)

	// ══ ★★★ 27.09.2026 — ХОД КЛАВИШАМИ ПО СХЕМЕ ОРИГИНАЛА (`asda.Move.KeyboardEtalon`, тело — `AsdaPlayerKeyboardEtalon.cpp`) ══
	//   Ключ 1 — дословный порт `updatePlayerMovement@0x95CDF0`; ключ 0 — блок ниже без изменений (прежняя схема порта).
	//   Смена ключа на лету: состояние обеих схем сбрасывается, идущий ход прежней схемы закрывается штатной остановкой.
	{
		const bool bEtalonKb = IsKeyboardEtalon();
		if (bEtalonKb != bKbWasEtalon)
		{
			bKbWasEtalon = bEtalonKb;
			KbResetState();
			if (bWasMoving) { StopMoveAndNotify(TEXT("смена схемы хода клавишами")); bWasMoving = false; }
			KeyMoveCommittedCell = FIntPoint(MAX_int32, MAX_int32);
			bPrevFwdP = bPrevBackP = false;
			UE_LOG(LogAsdaPC, Log, TEXT("[клавиши] схема хода клавишами: %s"),
				bEtalonKb ? TEXT("оригинал (asda.Move.KeyboardEtalon 1)") : TEXT("прежняя схема порта (0)"));
		}
		if (bEtalonKb)
		{
			KeyboardEtalonTick(DeltaTime, Ch);
			return;
		}
	}
	// ★ 30.09.2026 (волна Л5 торговли): верхний гейт `updatePlayerMovement` (math_geom.c:417) и для прежней схемы (ключ 0): открыта
	//   личная лавка (22000/23000) — клавиши не двигают, пакетов нет; камера живёт (как у смерти выше).
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GetGameInstance()))
	{
		bMoveFwd = bMoveBack = bMoveLeft = bMoveRight = false;
		bAutoRunActive = false; AutoRunHoldStart = 0.0;
		bWasMoving = false;
		if (Ch->CameraArm) { Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f)); }
		return;
	}

	// ── ★ Движение 1:1 (эталон Player_KeyboardMoveTick, math_geom.c:380-680) ──
	//   WASD (cmd 51-54) + СТРЕЛКИ как альтернатива. Камеро-относительно.
	//
	//   ★★★ ИСПРАВЛЕНО 2026-08-15. Стрелки как «второй WASD» — надстройка порта, в оригинале
	//   движение только MOVE_W/S/A/D (Shortcutkey.ini, строки 51-54). А в поставочном ini ВСЕ ЧЕТЫРЕ
	//   стрелки ЗАНЯТЫ:
	//       BAR SLOT 11: UP (стр.16) · BAR SLOT 12: DOWN (17) · NEW BAR SLOT 10: LEFT (27) ·
	//       ACTION ATTACK: RIGHT (59)
	//   то есть каждая стрелка одновременно двигала персонажа И выполняла свою команду. Теперь стрелка
	//   работает как движение ТОЛЬКО если раскладка её не заняла (`LookupCmd(vk, без модификатора) == 0`).
	//   С хардкод-дефолтами (`keybindLoadDefaults@0xE3A9C0`, стрелки свободны) поведение прежнее.
	auto ArrowFreeForMove = [this](int32 Vk) -> bool
	{
		return !Keys || Keys->LookupCmd(Vk, EAsdaKeyMod::None) == 0;
	};
	const bool bFwdKey   = bMoveFwd  || (ArrowFreeForMove(38) && IsInputKeyDown(EKeys::Up));
	const bool bBackKey  = bMoveBack || (ArrowFreeForMove(40) && IsInputKeyDown(EKeys::Down));
	const bool bLeftKey  = bMoveLeft || (ArrowFreeForMove(37) && IsInputKeyDown(EKeys::Left));
	const bool bRightKey = bMoveRight || (ArrowFreeForMove(39) && IsInputKeyDown(EKeys::Right));

	// ★ Автобег (эталон: опция g_bFxAlwaysRun + удержание W 3000мс; S отменяет).
	const double NowS = FPlatformTime::Seconds();
	if (bFwdKey && bOptionAlwaysRun)
	{
		if (AutoRunHoldStart <= 0.0) { AutoRunHoldStart = NowS; }
		else if (NowS - AutoRunHoldStart > 3.0) { bAutoRunActive = true; }
	}
	else { AutoRunHoldStart = 0.0; }
	if (bBackKey) { bAutoRunActive = false; }

	// ★ Конфликт W+S: ПОСЛЕДНЯЯ нажатая побеждает (эталонный латч g_nMoveFwdBackPriority 1:1).
	bool bFwdP = bFwdKey || bAutoRunActive, bBackP = bBackKey;
	if (!bFwdP && !bBackP) { MoveFBPriority = 0; }
	if (bFwdP)
	{
		if (bBackP) { if (MoveFBPriority != 1) { bBackP = false; } }
		else        { MoveFBPriority = 1; }
	}
	if (bBackP)
	{
		if (bFwdP)  { if (MoveFBPriority != 2) { bFwdP = false; } }
		else        { MoveFBPriority = 2; }
	}

	// ★ A/D: С ПКМ = СТРЕЙФ (эталон GetAsyncKeyState(2) → ±cameraRight в moveDir);
	//   БЕЗ ПКМ = ПЛАВНЫЙ РАЗВОРОТ камеры+персонажа 91.67°/с (0.05 рад / 31.25 мс),
	//   знак инвертируется при зажатом S — как руль при езде задним ходом.
	const bool bTurnOnlyLR = !bRMBDown;
	if (bLeftKey && !bRightKey && bTurnOnlyLR)
	{
		CamYawDeg += (bBackKey ? +1.f : -1.f) * KeyTurnDegPerSec * DeltaTime;
	}
	if (bRightKey && !bLeftKey && bTurnOnlyLR)
	{
		CamYawDeg += (bBackKey ? -1.f : +1.f) * KeyTurnDegPerSec * DeltaTime;
	}
	// Камеру применяем ПОСЛЕ клавиатурного поворота (иначе разворот отстаёт на кадр).
	Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f));

	// Направление движения в осях камеры (yaw), спроецированное на землю.
	const FRotator YawOnly(0.f, CamYawDeg, 0.f);
	const FVector Fwd = YawOnly.Vector();
	const FVector Right = FRotationMatrix(YawOnly).GetScaledAxis(EAxis::Y);
	FVector UeDir = FVector::ZeroVector;
	if (bFwdP)  { UeDir += Fwd; }
	if (bBackP) { UeDir -= Fwd; }
	if (bRMBDown)   // стрейф только при ПКМ (эталон)
	{
		if (bLeftKey && !bRightKey)  { UeDir -= Right; }
		if (bRightKey && !bLeftKey)  { UeDir += Right; }
	}
	const bool bMovingNow = !UeDir.IsNearlyZero();

	// Ручное движение отменяет и отложенный подбор лута (новое намерение замещает state 103).
	if (bMovingNow && bLootPending) { bLootPending = false; }
	// ★ Ручное движение отменяет и МАРШРУТ (иначе RouteTick перебивал бы клавиатурный курс).
	if (bMovingNow)
	{
		CancelRoute();
		// ★ 29.10.2026 [ПОРТ] (asda.Map.DestMarkClear): маршрут стёрт — и метка цели автобега на картах.
		AsdaDestMarkClear(GetGameInstance(), TEXT("ручное движение клавишами (прежняя схема)"));
		// Ручное движение = новый приказ игрока: отложенный «подбежать и применить» снимается.
		CancelPendingSkillApproach(TEXT("ручное движение клавишами"));
		// ★ D5 + требование владельца: клавиатура — ВТОРОЙ (и последний) источник движения,
		//   маршрут она не строит, поэтому общую точку зовём здесь напрямую.
		NotifyPlayerStartedMoving(TEXT("ручное движение клавишами"));
	}
	// ★ Ручное движение (WASD/стрелки) ВЫВОДИТ ИЗ БОЯ (эталон: движение = другое состояние FSM;
	//   иначе персонаж после ручного шага снова убегал догонять цель — юзер 2026-07-29).
	if (bMovingNow && bAutoAttack)
	{
		ASDA_CTRACE(EAsdaCT::Exit, TEXT("ручное движение клавишами (прежняя схема) — выход из боя, замах остановлен, цель %s остаётся выбранной"),
			*AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()));
		ASDA_CTRACE_END(TEXT("ручное движение клавишами"));
		AsdaLeaveAttackAction(this, TEXT("ручное движение клавишами"));   // ★ 01.11.2026: 5 → 0 — 4024/400 до снятия флагов и до хода (эталон `CCharacter_SetAction` case 0)
		bAutoAttack = false; bCombatStarted = false; bWaitingCombatAck = false; bInCombatStance = false;
		if (Ch) { Ch->StopAttackAnim(); }
		ExitCombatStance();   // ★ движение = мирный покой на остановке (эталон SetState(0,0,**0**))
		UE_LOG(LogAsdaPC, Log, TEXT("Ручное движение — выход из боевого режима (цель остаётся выбранной)"));
	}
	if (bMovingNow)
	{
		UeDir = UeDir.GetSafeNormal();
		const FVector2D DirGame = AsdaCoord::UeDirToGame(FVector2D(UeDir.X, UeDir.Y));   // ★ Аудит №3 (B1): UE→game XZ
		// ★ ПЯТИМСЯ (S без W): движение к камере, ЛИЦОМ вперёд (эталон buffFxSlots[208]) + клип back01.
		const bool bBackpedal = (bBackP && !bFwdP);
		const FVector2D FaceGame = bBackpedal ? -DirGame : DirGame;

		if (!bWasMoving)
		{
			UE_LOG(LogAsdaPC, Log, TEXT("Движение WASD старт: dir game=(%.2f,%.2f) fwd=%d back=%d strafe=%d autorun=%d"),
				DirGame.X, DirGame.Y, bFwdP, bBackP, bRMBDown, bAutoRunActive);
			// ★ Клавиатура не идёт через приказ движения, поэтому путешествие к цели задания
			//   отменяем здесь же — игрок взялся за управление сам.
			CancelTravelIfManual(TEXT("клавиатура"));
		}
		// ★ ЕДИНЫЙ троттл-блок эталона (math_geom.c: repeat-гейт g_nMsgBoxTimeout): и отправка 4006,
		//   и ЛОКАЛЬНОЕ перенацеливание курса происходят только на «репите» — свежее нажатие W/S =
		//   сразу (10мс), смена направления (разворот) = 250мс, стабильный курс = 1000мс.
		const bool bFreshPress = (bFwdP && !bPrevFwdP) || (bBackP && !bPrevBackP)
			|| bBackpedal != bPrevBackpedal || !bWasMoving;
		const bool bDirChanged = !DirGame.Equals(LastSentMoveDir, 0.01f);
		const double MinInterval = bFreshPress ? 0.0 : (bDirChanged ? 0.25 : 1.0);
		bPrevBackpedal = bBackpedal;
		if (NowS - LastMoveSendTime >= MinInterval)
		{
			LastMoveSendTime = NowS;
			LastSentMoveDir = DirGame;
			// ★ МЁРТВАЯ ЗОНА поворота (эталон charMoveToWorldPos@0xA1E3D0, тайловая): курс и лицо
			//   ПЕРЕнацеливаются только когда целевая точка pos+dir×800 попала в ДРУГУЮ 100-юнитовую
			//   ячейку террейна (≈7° на радиусе 800). Малое верчение камеры игнорируется — персонаж
			//   продолжает прежним курсом, разворот идёт плавной дугой, как в оригинале.
			const FVector2D Cur = Ch->GetGamePosXZ();
			const FVector2D TargetGame = Cur + DirGame * 800.f;
			const FIntPoint TargetCell = AsdaCoord::GameToTile(TargetGame);   // ★ Аудит №3 (B2)
			if (bFreshPress || TargetCell != KeyMoveCommittedCell)
			{
				KeyMoveCommittedCell = TargetCell;
				// Едем ЛОКАЛЬНО пока держится клавиша; сервер держит через result=5-снап (OnNetEntityMove).
				Ch->SetMoveDirection(DirGame, Net_LastSpeed(), FaceGame, bBackpedal);
			}
			UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (Net && Net->bInGameConnected)
			{
				Net->SendMove4006(Cur.X, Cur.Y, TargetGame.X, TargetGame.Y);
			}
		}
	}
	else
	{
		KeyMoveCommittedCell = FIntPoint(MAX_int32, MAX_int32);   // сброс мёртвой зоны (движение кончилось)
		// ★ A/D СТОЯ: крутится ТОЛЬКО КАМЕРА (yaw довернулся выше), персонаж НЕ поворачивается
		//   (уточнение юзера 2026-07-29; разворот меша — только при движении, через мёртвую зону).
		if (bWasMoving)
		{
			// Все клавиши движения отпущены → стоп локально + 4008 (эталон: keyup гасит флаги).
			//   ★ Единая дверь остановки: она же гасит павна и шлёт 4008 принудительно. Прежний вызов
			//   `SendStop4008(...)` без bForce молча ничего не слал, пока bActionPending гасился
			//   приходом 4007 (см. правку в AsdaNetSubsystem OP_RECV_PACKET_ENTITY_MOVE).
			Ch->StopMove();
			StopMoveAndNotify(TEXT("клавиши движения отпущены"));
		}
	}
	bPrevFwdP = bFwdP; bPrevBackP = bBackP;
	bWasMoving = bMovingNow;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ ЖИВАЯ ПРОВЕРКА ПРАВИЛА «окно разговора — по двойному клику» (`asda.Npc.ClickRule`).
//   Спрашивает ТОТ ЖЕ предикат, что и ветка клика (`ShouldOpenNpcTalkOnClick`), — второй копии
//   правила нет, поэтому проверка не может «разъехаться» с кодом.
static FAutoConsoleCommandWithWorldArgsAndOutputDevice GAsdaCmdNpcClickRule(
	TEXT("asda.Npc.ClickRule"),
	TEXT("Показать, что сделает клик по NPC при текущем asda.Npc.OpenOnSingleClick."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>&, UWorld* W, FOutputDevice& Ar)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		AAsdaPlayerController* PC = GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
		if (!PC) { Ar.Logf(TEXT("[клик-NPC] нет AsdaPlayerController")); return; }
		const int32 Key = CVarAsdaNpcOpenOnSingleClick.GetValueOnGameThread();
		Ar.Logf(TEXT("[клик-NPC] asda.Npc.OpenOnSingleClick=%d (0 = решение владельца, 1 = эталон); ")
		        TEXT("одиночный открывает окно: %s; двойной открывает окно: %s"),
			Key,
			PC->ShouldOpenNpcTalkOnClick(/*bDouble*/false) ? TEXT("ДА") : TEXT("НЕТ"),
			PC->ShouldOpenNpcTalkOnClick(/*bDouble*/true)  ? TEXT("ДА") : TEXT("НЕТ"));
	}));
#endif // ASDA_DEV_CONSOLE
