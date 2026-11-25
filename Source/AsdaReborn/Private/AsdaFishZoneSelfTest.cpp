// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ЗОН РЫБАЛКИ И ОГРАНИЧЕНИЙ, ВОЛНА В4 (08.10.2026). [ПОРТ]
//
// ★ ЗАЧЕМ. Путь игрока в `-game` на Альпии (карта 3, 10 спотов) без сервера:
//   • ЗОНЫ — актёры, которые подсистема поставила САМА при входе на карту (`worldMapPlaceMarkersByType@0xFC3340`): позиции
//     против формулы (X·100, h, Y·100), h — независимым запросом высоты воды, OBB (2r·100, 100, 2r·100), меш `fs_zone`,
//     клип `fs_zoneanim_00` в петле и его ход во времени, габариты стайки под точкой; контроль — суша без воды, чужая карта;
//   • ПИКИНГ — настоящий `AAsdaPlayerController::OnLeftClick` с экранной точкой, спроецированной из центра зоны (подмена
//     мыши `DebugClickScreen`); пары: одиночный клик зону не видит, промах — 109 нет, соперник ближе — зона проигрывает,
//     террейн ближе — не соперник; грани `collide_rayVsObb6FacesHit` (порядок X−, X+, Y+, Y−, Z+, Z−) — числами;
//   • МЕНЮ cmd 8 — `UAsdaExtraMenuSubsystem::DispatchCommand` и ячейка хотбара типа 5 (`ActivateSlot`): рядом → 109 и
//     ближайшая зона, далеко → чат 110700 и 0;
//   • ГЕЙТЫ — каждый парой «вне рыбалки работает / при рыбалке нет»: мировой клик, хотбар, системное меню (кнопки и
//     свёртка), окно карты 17500, окно заданий (739, 780); клавиши — НАСТОЯЩИМ путём Slate (`ProcessKeyDownEvent` →
//     вьюпорт → `UPlayerInput`, ревизия В2 ВАЖН-2): `IsInputKeyDown` вне рыбалки true, при рыбалке false;
//   • ЧАТ — поле ввода с фокусом: вне рыбалки символ и Backspace меняют текст, при рыбалке — нет (символ, Backspace,
//     стрелка), стрелка в состоянии ввода мини-игры засчитана ответом; после рыбалки поле снова пишет;
//   • МЕТКИ — список FishingArea (51 спот) и квады мини-карты/большой карты: позиции относительно значка игрока против
//     формулы `TileToTexel` (мини-карта (100t+50)·масштаб, большая — тайл·масштаб), квады зон — ПОСЛЕДНИМИ в слое.
//   Скриншотов нет; геометрия — слоты/`GetCachedGeometry`.
//
// ★ РЕВИЗИЯ В4 (исправления 09.10.2026, `Reports/owner_1008/fishing_impl_W4fix.md`):
//   • R1…R6 — ДВОЙНОЙ КЛИК НАСТОЯЩИМ ПУТЁМ МЫШИ Slate (`FSlateApplication::ProcessMouseButtonDown/Up/DoubleClickEvent` →
//     вьюпорт → `UPlayerInput`: второе нажатие = `IE_Pressed` + `IE_DoubleClick`), точка — проходимый берег внутри коробки
//     зоны, 4006 одиночного события — под перехватом (бит 2 «запрос хода в пути» взводится по-настоящему). Экранную точку
//     пикинга по-прежнему даёт `DebugClickScreen` (в `-RenderOffscreen` нет курсора ОС — `GetMousePosition` ложно);
//   • C7 — соперник «объект карты» (экземпляр ISM на `AsdaMapObjects_<карта>`): ближе зоны — зона проиграла, дальше — выиграла;
//     естественная точка на экране (дом/дерево перед зоной) — реальным двойным кликом;
//   • A3 — высота воды против СЫРОГО `.WAT` (прямоугольники элементов `pickSpriteQuadRayTest`/`buildSpriteQuad`), не той же функцией;
//   • L5 — Alt через препроцессор (фокус в чате): нажат — ответ не засчитан, отпущен — засчитан (застревания нет);
//   • L6 — многострочное поле тоже «только чтение» при рыбалке; L7 — `IsGameUiWidget`; K3 — фокус на НЕтекстовом виджете игры:
//     вне рыбалки хоткей (W) доходит, при рыбалке нет;
//   • E1b/E2b — гейты хотбара и системного меню настоящими путями (`ActivateSlot`, `ClickButton`), а не помощниками;
//   • M2 — против формулы эталона `uiSlotIndexToScreenPos` (t·k, БЕЗ +½ тайла).
//
// ★ РЕВИЗИЯ В4/В5 (В45fix2, 09.10.2026, `Reports/owner_1008/fishing_impl_W45fix2.md`):
//   • M2a/M2b — метки мини-карты АБСОЛЮТНО (|Δq − K·De| ≤ 0.1 px при известном K) + контрольная пара «+½ тайла» (не совпадает);
//   • R2 — `N == 2` подписан как [ПОРТ] (лишний 4006 одиночного события второго нажатия);
//   • C7в — контроль сужения соперника «объект карты»: меш системного актора и ISM на непростом акторе — не соперник;
//   • DN0…DN3, DM0/DM1 — двойной клик по NPC (окно в зоне / подбегание вне зоны / ключ `asda.Npc.OpenOnSingleClick 1` с путём
//     исключения ВАЖН-1) и по мобу (боевой режим) настоящим путём мыши; NPC — пакетом 4067, моб — пакетом 4013.
//   (Строка шапки «мини-карта (100t+50)·масштаб» выше устарела с В4fix: формула меток FishingArea — 100·t·масштаб.)
//
// Консоль: `asda.Fish.ZoneSelfTest [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ЗОН-РЫБЫ ok/FAIL …», итог — «САМОТЕСТ-ЗОН-РЫБЫ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "World/AsdaFishZoneSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "World/AsdaFishingSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "GameFramework/SpringArmComponent.h"   // ★ R5-fix8-c (14.11.2026): длина плеча камеры — в исходное положение перед прогоном
#include "World/AsdaSkillCastLock.h"   // ★ R5-fix6-c (13.11.2026): сброс отложенного клика хода при переносе персонажа
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaWalkGrid.h"
#include "World/AsdaCoord.h"
#include "UI/AsdaFishWindowSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaExtraMenuSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSessionReset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableText.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "AsdaNetSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Layout/WidgetPath.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "Npc/AsdaNpcActor.h"                   // ★ В45fix2 (МЕЛК-7): двойной клик по NPC настоящим путём мыши
#include "Npc/AsdaMobActor.h"                   // ★ В45fix2 (МЕЛК-7): двойной клик по мобу
#include "Npc/AsdaNpcDialogSubsystem.h"         // ★ В45fix2 (МЕЛК-7): окно разговора открылось
#include "Camera/PlayerCameraManager.h"         // ★ В45fix2 (МЕЛК-7): направление взгляда — NPC ставится перед камерой
#include "AsdaBytes.h"                          // ★ В45fix2 (МЕЛК-7): тела пакетов 4067/4013
#include "AsdaTestPackets.h"                    // ★ дедуп №2 В1-C (F18): общая раскладка записи 4013
#include "UI/AsdaUiVisibilitySubsystem.h"      // ★ 23.10.2026: гейт окон по курсору ОС — снять на время мировых кликов прогона

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishZoneTest, Log, All);

// Друг `UAsdaFishZoneSubsystem`, `UAsdaFishingSubsystem`, `UAsdaFishWindowSubsystem`, `AAsdaPlayerController`,
//   `UAsdaSystemMenuSubsystem`, `UAsdaMinimapSubsystem`, `UAsdaQuestLogSubsystem` (объявлено в их заголовках).
struct FAsdaFishZoneSelfTest : public TSharedFromThis<FAsdaFishZoneSelfTest>
{
	int32 Ok = 0, Fail = 0;                     // пройдено / провалено
	bool bQuit = false;                         // выйти после прогона
	TWeakObjectPtr<UWorld> World;               // мир прогона
	UGameInstance* GI = nullptr;                // экземпляр игры
	UAsdaFishingSubsystem* F = nullptr;         // автомат рыбалки (В1)
	UAsdaFishZoneSubsystem* Z = nullptr;        // зоны (В4)
	UAsdaFishWindowSubsystem* Win = nullptr;    // окно мини-игры (В2)
	UAsdaLoadManagerSubsystem* LM = nullptr;    // карта
	UAsdaHotbarSubsystem* Hot = nullptr;        // панель быстрого доступа
	UAsdaSystemMenuSubsystem* Sys = nullptr;    // системное меню 67073
	UAsdaMinimapSubsystem* MM = nullptr;        // мини-карта / большая карта
	UAsdaQuestLogSubsystem* QL = nullptr;       // журнал заданий
	UAsdaExtraMenuSubsystem* Extra = nullptr;   // доп-меню
	UAsdaChatWindowSubsystem* Chat = nullptr;   // чат
	UAsdaWindowSubsystem* WM = nullptr;         // окна-сообщения
	AAsdaPlayerController* PC = nullptr;        // контроллер
	AAsdaPlayerCharacter* Ch = nullptr;         // свой персонаж
	TArray<int32> Alp;                          // споты Альпии (порядок таблицы)
	FVector2D Near, Far;                        // точки «рядом со спотом 0» / «вне всех зон»
	// Снимок для восстановления.
	int32 MapWas = 0;                           // CurrentMapId
	FVector2D PosWas;                           // позиция персонажа
	FAsdaHotbarSlot SlotWas;                    // содержимое тестовой ячейки хотбара
	static constexpr int32 kHotSlot = 76;       // тестовая ячейка хотбара (последняя T-ряда)
	// Этап 2.
	TWeakObjectPtr<AAsdaFishZoneActor> ClipZone;   // зона, у которой меряется ход клипа
	float ClipT0 = -1.f;                        // позиция клипа в начале этапа 2
	int32 EatenWas = 0;                         // счётчик поглощённых гейтом окон клавиш

	// Одна проверка.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaFishZoneTest, Log, TEXT("САМОТЕСТ-ЗОН-РЫБЫ ok   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaFishZoneTest, Error, TEXT("САМОТЕСТ-ЗОН-РЫБЫ FAIL %s"), *What); }
	}
	// Был ли нотис «канал:loc-id» в журнале автомата.
	bool HadNotice(const TCHAR* Channel, int32 LocId) const
	{
		const FString Pfx = FString::Printf(TEXT("%s:%d:"), Channel, LocId);
		for (const FString& L : F->GetRecentNotices()) { if (L.StartsWith(Pfx)) { return true; } }
		return false;
	}
	// Поставить состояние автомата без шагов (гейты смотрят только fishState ≠ 0).
	void SetFs(int32 V) { F->FishState = V; }
	// Полный выход из рыбалки (как смена карты): 0, зона снята, диалоги сняты.
	void ResetFish() { F->ResetSessionState(EAsdaSessionResetReason::MapChanged); WM->ClearMsgBoxQueue(TEXT("самотест зон рыбалки")); }
	// Остановить и вернуть персонажа в точку.
	// ★ 23.10.2026: перенос между сценариями — ЖЁСТКИЙ снап (без визуального хвоста тела и камеры). Плавный `NetSnap` оставлял
	//   камеру догонять точку (хвост ВАЖНО-А), если прошлый сценарий сдвинул персонажа локальным стартом (`asda.Move.LocalStart 1`),
	//   и экранные точки, найденные при неподвижной камере (ShorePx/WaterPx), смотрели мимо (W2 «вода» → суша).
	// ★ R5-fix6-c (13.11.2026): перенос снимает и ОТЛОЖЕННЫЙ клик хода (`asda.Input.ClickQueue`: второй клик по земле чаще 150 мс ждёт конца
	//   троттла). Этап 1 оставлял такой клик; на этапе 2 он исполнялся ровно тогда, когда тест отпускал W (таймер 0,15 с
	//   против троттла 150 мс) — персонаж убегал к споту, и метки карты M2/M2a/M4 мерились на бегу (невязка 0,1076 px).
	void PlaceAt(const FVector2D& G) { AsdaSkillCastLock::Reset(TEXT("самотест зон рыбалки: перенос персонажа")); Ch->StopMove(); Ch->NetSnapHard(G); }
	// Мировой клик ТЕМ ЖЕ путём, что от мыши (`OnLeftClick`), с подменённой экранной точкой.
	void Click(const FVector2D& Screen, bool bDouble)
	{
		NoteCursorOverUi(TEXT("Click"));
		EnsureCursorNotOverUi(TEXT("Click"));
		PC->DebugClickScreen = Screen;
		PC->OnLeftClick(bDouble);
		PC->DebugClickScreen.Reset();
	}
	// ★ В45fix2: диагностика окружения — гейт окон `OnLeftClick` (`IsCursorOverAnyUiWindow`) смотрит НАСТОЯЩИЙ курсор ОС
	//   (`FSlateApplication::GetCursorPos`), а не подменённую точку клика. Если курсор пользователя стоит над окном игры
	//   (скрытое окно `-RenderOffscreen` тоже имеет экранную геометрию), мировой клик гасится — пишем это в журнал.
	void NoteCursorOverUi(const TCHAR* Where) const
	{
		FName Zn;
		if (PC && PC->IsCursorOverUi(Zn))
		{
			const FVector2D Cur = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
			UE_LOG(LogAsdaFishZoneTest, Warning, TEXT("  [%s] курсор ОС (%.0f,%.0f) над окном «%s» — мировой клик погасит гейт окон (окружение прогона, не код)"),
				Where, Cur.X, Cur.Y, *Zn.ToString());
		}
	}
	// ★ 23.10.2026 (разбор падения 59/19 «4006 0», `Reports/split_2026-10-23/fish_asda_Move_StunHardStop`): гейт окон `OnLeftClick`
	//   читает НАСТОЯЩИЙ курсор ОС, а прогон подменяет только точку клика — если мышь пользователя в этот момент стоит над окном игры
	//   (там — MINISTATE_WIN в (115,115)), все мировые клики сценариев гасятся гейтом окон, и итог зависит от того, где лежит мышь
	//   владельца, а не от кода (прогоны с теми же ключами без курсора над окном — 78/0). У игрока курсор и точка клика — одна точка,
	//   поэтому в игре такого расхождения нет. Самотест в этом случае скрывает интерфейс (Alt+Z: хит-тест окон молчит,
	//   `HiddenBlocksUiHitTest`) до конца прогона — окна в сценариях мировых кликов не участвуют.
	void EnsureCursorNotOverUi(const TCHAR* Where)
	{
		FName Zn;
		if (!PC || !PC->IsCursorOverUi(Zn)) { return; }
		UAsdaUiVisibilitySubsystem* Uv = PC->GetGameInstance() ? PC->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
		if (!Uv) { return; }
		Uv->SetUiShown(false, TEXT("самотест зон рыбалки: курсор ОС над окном — окружение прогона"));
		bUiHiddenForCursor = true;
		UE_LOG(LogAsdaFishZoneTest, Warning, TEXT("  [%s] курсор ОС над окном «%s» — интерфейс скрыт до конца прогона (окружение, не код)"), Where, *Zn.ToString());
	}
	// Текст поля ввода, у которого клавиатурный фокус (пусто — фокуса у поля нет).
	static bool FocusedEditText(FString& Out)
	{
		if (!FSlateApplication::IsInitialized()) { return false; }
		const TSharedPtr<SWidget> Fw = FSlateApplication::Get().GetUserFocusedWidget(0);
		if (!Fw.IsValid() || Fw->GetType() != FName(TEXT("SEditableText"))) { return false; }
		Out = StaticCastSharedPtr<SEditableText>(Fw)->GetText().ToString();
		return true;
	}
	// Настоящее событие клавиши Slate (как физическая клавиатура).
	static void SlateKey(const FKey& K, bool bDown)
	{
		const FKeyEvent Ev(K, FModifierKeysState(), 0, false, 0, 0);
		if (bDown) { FSlateApplication::Get().ProcessKeyDownEvent(Ev); }
		else       { FSlateApplication::Get().ProcessKeyUpEvent(Ev); }
	}
	// Настоящий символ Slate.
	static void SlateChar(TCHAR C)
	{
		FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(C, FModifierKeysState(), 0, false));
	}
	// Слот канваса квада метки: левый-верхний оффсет.
	static FVector2D QuadPos(const UImage* Img)
	{
		const UCanvasPanelSlot* S = Img ? AsdaUi::SlotOf(Img) : nullptr;
		return S ? FVector2D(S->GetOffsets().Left, S->GetOffsets().Top) : FVector2D(-1e9, -1e9);
	}
	// Проверка позиций квадов меток относительно значка игрока: Δквад ∥ Δожидаемое с ОДНИМ коэффициентом на все квады.
	//   Expected — для каждого спота карты вектор (ожидаемая точка − точка игрока) в единицах формулы.
	bool CheckQuadRatio(const TArray<UImage*>& Quads, const FVector2D& PlayerPos, const TArray<FVector2D>& Expected,
		FString& OutInfo) const
	{
		float K = -1.f;
		int32 Matched = 0;
		for (const UImage* Q : Quads)
		{
			const FVector2D Dq = QuadPos(Q) - PlayerPos;
			bool bHit = false;
			for (const FVector2D& De : Expected)
			{
				if (De.Size() < 1e-3f) { continue; }
				if (Dq.Size() < 1e-3f) { continue; }
				const float Cos = FVector2D::DotProduct(Dq.GetSafeNormal(), De.GetSafeNormal());
				if (Cos < 0.999f) { continue; }
				const float Kq = Dq.Size() / De.Size();
				if (K < 0.f) { K = Kq; }
				if (FMath::Abs(Kq - K) <= FMath::Max(0.02f * K, 0.75f / De.Size())) { bHit = true; break; }
			}
			Matched += bHit ? 1 : 0;
		}
		OutInfo = FString::Printf(TEXT("квадов %d, совпало %d, коэффициент %.5f"), Quads.Num(), Matched, K);
		return Quads.Num() > 0 && Matched == Quads.Num();
	}
	// ★ В45fix2 (ревизия В4/В5, МЕЛК-1, 09.10.2026): АБСОЛЮТНАЯ сверка квадов меток с формулой при ИЗВЕСТНОМ коэффициенте.
	//   Для каждого квада ищется спот, у которого |Δq − K·De| ≤ Tol (пиксели), где Δq — оффсет квада минус оффсет значка игрока
	//   (оба 16×16 — сдвиг привязки сокращается), De — (ожидаемая точка − позиция игрока) в игровых единицах. В отличие от
	//   `CheckQuadRatio` (угол/доля) различает сдвиг в доли пикселя: +½ тайла при масштабе 0.005 = 0.25 px по каждой оси.
	//   OutMaxErr — наибольшая из лучших невязок по квадам (px), OutMinErr — наименьшая (для контрольной пары: у «чужой»
	//   формулы даже лучший квад обязан не уложиться в допуск).
	static int32 CountQuadAbs(const TArray<UImage*>& Quads, const FVector2D& PlayerPos, const TArray<FVector2D>& Expected,
		float K, float Tol, float& OutMaxErr, float& OutMinErr)
	{
		int32 Matched = 0;
		OutMaxErr = 0.f; OutMinErr = 1e9f;
		for (const UImage* Q : Quads)
		{
			const FVector2D Dq = QuadPos(Q) - PlayerPos;
			float Best = 1e9f;
			for (const FVector2D& De : Expected) { Best = FMath::Min(Best, (float)(Dq - De * K).Size()); }
			Matched += (Best <= Tol) ? 1 : 0;
			OutMaxErr = FMath::Max(OutMaxErr, Best);
			OutMinErr = FMath::Min(OutMinErr, Best);
		}
		return Matched;
	}

	// ── Ревизия В4 (09.10.2026) ──
	UAsdaNetSubsystem* Net = nullptr;           // сеть (перехват 4006, бит 2)
	uint32 CapGen = 0;                          // поколение своего перехватчика 4006 (0 — нет)
	int32 N4006 = 0;                            // перехвачено 4006
	bool bWasInGame = false;                    // прежний `bInGameConnected`
	FVector2D ShorePx = FVector2D(-1, -1);      // экранная точка: проходимый берег внутри коробки зоны
	int32 ShoreSpot = -1;                       // зона под этой точкой
	FVector2D MissPx = FVector2D(-1, -1);       // экранная точка мимо всех зон (земля под курсором)
	FVector2D WaterPx = FVector2D(-1, -1);      // ★ 12.10.2026 (W1/W2): экранная точка НА ВОДЕ внутри коробки зоны (IsWaterHit)
	int32 WaterSpot = -1;                       // зона под точкой на воде
	FString MouseRoute;                         // каким путём дошла мышь (FSlateApplication / SViewport)
	bool bUiHiddenForCursor = false;            // ★ 23.10.2026: интерфейс скрыт самотестом — курсор ОС стоял над окном (вернуть в конце)
	TSharedPtr<SWidget> TmpOverlay;             // временный виджет в оверлее вьюпорта (L6/K3)
	TSharedPtr<SMultiLineEditableText> TmpMulti;   // многострочное поле L6
	TSharedPtr<SButton> TmpButton;              // фокусируемый НЕтекстовый виджет K3
	FString MultiT0;                            // текст многострочного поля до шагов L6
	int32 ObjSpotTry = 0;                       // C7: перебор спотов для естественной точки «объект карты перед зоной»
	bool bObjNatural = false;                   // C7: естественная точка найдена и проверена
	TSharedPtr<TFunction<void(int32)>> RunHolder;   // R1…R6: цепочка шагов (держится до конца прогона)
	// ── В45fix2 (ревизия В4/В5, МЕЛК-7): двойной клик по NPC и мобу настоящим путём мыши ──
	TWeakObjectPtr<AAsdaNpcActor> DcNpc;        // NPC, принятый пакетом 4067 (adoption запечённого)
	TWeakObjectPtr<AAsdaMobActor> DcMob;        // моб, созданный пакетом 4013
	FVector2D DcPx = FVector2D(-1, -1);         // экранная точка, где пикинг даёт текущую сущность
	FVector2D DcFarG = FVector2D::ZeroVector;   // игровая точка «вне зоны разговора» (проходимая)
	uint64 DcMark1 = MAX_uint64;                // `SinglePressMoveFrame` после первого нажатия (DN3)
	int32 DcN1 = 0;                             // 4006 после первого нажатия
	bool bDcFlag1 = false;                      // признак после первого нажатия (окно / подбегание / цель)
	int32 NpcOpenSingleWas = 0;                 // прежнее значение `asda.Npc.OpenOnSingleClick`
	static constexpr int32 kDcNpcRid = 29001;   // renderId принятого NPC (вне диапазона живого сервера в офлайне)
	static constexpr int32 kDcMobRid = 29002;   // renderId созданного моба

	// Перехватчик строк журнала (E2b: гейт системного меню виден только в журнале). Потокобезопасный, зовётся синхронно.
	struct FLineCatch : public FOutputDevice
	{
		FCriticalSection Cs;                    // защита списка
		TArray<FString> Lines;                  // пойманные строки
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override { FScopeLock L(&Cs); Lines.Add(V); }
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		// Была ли строка с подстрокой.
		bool Has(const TCHAR* Sub) { FScopeLock L(&Cs); for (const FString& X : Lines) { if (X.Contains(Sub)) { return true; } } return false; }
	};

	// Высота воды по СЫРОМУ `.WAT` (эталон `pickElems_testAnyHit@0x116B660`: первый элемент по списку, чей прямоугольник X/Z
	//   содержит точку, `pickSpriteQuadRayTest@0x116B570` → высота плоскости; элемент 840 б = запись файла 0x348:
	//   `buildSpriteQuad@0x116CB80` — угол `+0x2CC` (x, y, z), стороны `+0x324` (X) и `+0x328` (Z), все 4 угла на высоте y).
	//   false — ни один элемент не накрывает точку (у эталона h = 0).
	static bool WatHeight(const TArray<uint8>& B, float X, float Z, float& OutH)
	{
		if (B.Num() < 8) { return false; }
		auto U32 = [&B](int32 O) { return AsdaBytes::RdU32(B.GetData() + O); };   // ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`
		auto F32 = [&B](int32 O) { return AsdaBytes::RdF32(B.GetData() + O); };
		if (U32(0) != 0x20060214u) { return false; }
		const int32 Count = (int32)U32(4);
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Base = 8 + i * 0x348;
			if (Base + 0x348 > B.Num()) { break; }
			const float Ax = F32(Base + 0x2CC), Ay = F32(Base + 0x2D0), Az = F32(Base + 0x2D4);
			const float Wx = F32(Base + 0x324), Wz = F32(Base + 0x328);
			if (Ax > X || Ax + Wx < X || Az > Z || Az + Wz < Z) { continue; }
			OutH = Ay;
			return true;
		}
		return false;
	}
	// Игровой вьюпорт (Slate).
	static TSharedPtr<SViewport> GameVp()
	{
		return (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
	}
	// Пиксель вьюпорта → экранные координаты Slate (обратное к `FSceneViewport`: пиксель = локальная · Scale).
	static FVector2D PixelToAbs(const FVector2D& Px)
	{
		const TSharedPtr<SViewport> Vp = GameVp();
		if (!Vp.IsValid()) { return Px; }
		const FGeometry& G = Vp->GetCachedGeometry();
		return FVector2D(G.LocalToAbsolute(FVector2f(Px / FMath::Max(G.Scale, 0.01f))));
	}
	// Настоящее событие мыши Slate (Kind: 0 — нажатие, 1 — отпускание, 2 — двойной). Сначала полный путь
	//   `FSlateApplication` (поиск окна под точкой → маршрут к вьюпорту); если окно под точкой Slate не нашёл (окна без
	//   курсора в `-RenderOffscreen`) — тот же обработчик вьюпорта `SViewport::OnMouseButton*` (→ `FSceneViewport` →
	//   `UPlayerInput`). Какой путь сработал — в `MouseRoute`.
	void Mouse(const FVector2D& Px, int32 Kind)
	{
		if (Kind != 1) { NoteCursorOverUi(TEXT("Mouse")); }
		FSlateApplication& App = FSlateApplication::Get();
		const FVector2D Abs = PixelToAbs(Px);
		TSet<FKey> Pressed;
		if (Kind != 1) { Pressed.Add(EKeys::LeftMouseButton); }
		const FPointerEvent Ev(0, FSlateApplicationBase::CursorPointerIndex, Abs, Abs, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		const TSharedPtr<SViewport> Vp = GameVp();
		const TSharedPtr<SWindow> GameWin = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetWindow() : nullptr;
		const TSharedPtr<FGenericWindow> Native = GameWin.IsValid() ? GameWin->GetNativeWindow() : nullptr;
		const FWidgetPath Under = App.LocateWindowUnderMouse(Abs, App.GetInteractiveTopLevelWindows(), false, 0);
		const bool bSlate = Under.IsValid() && Vp.IsValid() && Under.ContainsWidget(Vp.Get());
		if (bSlate)
		{
			if (Kind == 0) { App.ProcessMouseButtonDownEvent(Native, Ev); }
			else if (Kind == 1) { App.ProcessMouseButtonUpEvent(Ev); }
			else { App.ProcessMouseButtonDoubleClickEvent(Native, Ev); }
			MouseRoute = TEXT("FSlateApplication");
		}
		else if (Vp.IsValid())
		{
			const FGeometry& G = Vp->GetCachedGeometry();
			if (Kind == 0) { Vp->OnMouseButtonDown(G, Ev); }
			else if (Kind == 1) { Vp->OnMouseButtonUp(G, Ev); }
			else { Vp->OnMouseButtonDoubleClick(G, Ev); }
			MouseRoute = TEXT("SViewport (окно под точкой Slate не нашёл)");
		}
	}
	// Подготовка сценария мыши: автомат 0, бит 2 снят, персонаж у спота, маршрут и троттл сброшены.
	void MousePrep()
	{
		ResetFish();
		if (Net) { Net->ClearMoveRequestInFlight(TEXT("самотест зон: подготовка")); }
		PlaceAt(Near);
		PC->CancelRoute();
		PC->bMovingToPoint = false;
		PC->LastMoveSendTime = 0.0;
		PC->SinglePressMoveFrame = MAX_uint64;
		PC->DebugClickScreen = ShorePx;
		EnsureCursorNotOverUi(TEXT("MousePrep"));
	}
	// ★ В45fix2 (МЕЛК-7): подготовка сценария «двойной по сущности» — автомат 0, бит 2 снят, маршрут/троттл/метка сброшены,
	//   цель и отложенный разговор сняты, окно разговора закрыто, счётчик 4006 обнулён.
	void DcPrep()
	{
		ResetFish();
		if (Net) { Net->ClearMoveRequestInFlight(TEXT("самотест зон: двойной по сущности")); }
		PC->CancelRoute();
		PC->bMovingToPoint = false;
		PC->LastMoveSendTime = 0.0;
		PC->SinglePressMoveFrame = MAX_uint64;
		PC->ClearPendingNpcTalk(TEXT("самотест зон: подготовка"));
		PC->bAutoAttack = false; PC->bCombatStarted = false;
		PC->ClearTarget();
		if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { if (Dlg->IsDialogOpen()) { Dlg->Close(); } }
		N4006 = 0;
	}
	// Игровая точка на расстоянии DistUe от актора A ПРОТИВ взгляда камеры (сущность окажется перед камерой).
	FVector2D BehindAlongView(const AActor* A, float DistUe) const
	{
		FVector Fwd = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector(1, 0, 0);
		Fwd.Z = 0.f;
		Fwd = Fwd.GetSafeNormal();
		if (Fwd.IsNearlyZero()) { Fwd = FVector(1, 0, 0); }
		return AsdaCoord::UeToGame2D(A->GetActorLocation() - Fwd * DistUe);
	}
	// Экранная точка, в которой `PickForClick` даёт именно актор A нужного вида (кольца вокруг проекции центра габаритов).
	//   bNoZoneInFront — ещё и коробка зоны рыбалки на этом луче не ближе сущности (иначе двойной по праву эталона выберет
	//   тип 5: зона побеждает, когда она ближе, g:30370-30383 — это не регресс, а другая ситуация).
	bool FindPickPx(const AActor* A, AAsdaPlayerController::EAsdaPickKind Kind, FVector2D& Out, bool bNoZoneInFront = false) const
	{
		if (!A) { return false; }
		int32 VX = 0, VY = 0; PC->GetViewportSize(VX, VY);
		FVector Org, Ext; A->GetActorBounds(/*bOnlyColliding*/false, Org, Ext);
		FVector2D S;
		if (!PC->ProjectWorldLocationToScreen(Org, S)) { return false; }
		bool bFound = false;
		for (int32 R = 0; R <= 90 && !bFound; R += 3)
		{
			for (int32 dy = -R; dy <= R && !bFound; dy += 3)
			{
				for (int32 dx = -R; dx <= R && !bFound; dx += 3)
				{
					if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R) { continue; }   // только кольцо радиуса R
					const FVector2D P(S.X + dx, S.Y + dy);
					if (P.X < 4 || P.Y < 4 || P.X > VX - 4 || P.Y > VY - 4) { continue; }
					PC->DebugClickScreen = P;
					const AAsdaPlayerController::FAsdaPick Pk = PC->PickForClick();
					if (Pk.Kind != Kind || Pk.Actor.Get() != A) { continue; }
					if (bNoZoneInFront)
					{
						FVector O, D; float ZD = 0.f;
						if (!PC->DeprojectScreenPositionToWorld((float)P.X, (float)P.Y, O, D)) { continue; }
						if (Z->PickZoneByRay(O, D, ZD) >= 0 && !(ZD > FVector::Dist(O, Pk.Point))) { continue; }
					}
					Out = P; bFound = true;
				}
			}
		}
		PC->DebugClickScreen.Reset();
		return bFound;
	}
	// Значение консольного ключа `asda.Npc.OpenOnSingleClick` (запись).
	static void SetNpcOpenSingle(int32 V)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Npc.OpenOnSingleClick"), V);
	}

	bool RunLogic(UWorld* W);   // этап 1 (синхронно)
	void Stage2();              // этап 2 (через кадры): клавиши Slate, чат, метки, клип
	void StageExtra();          // этап 3: K3 (фокус на нетекстовом виджете), L6 (многострочное поле)
	void StageObj();            // этап 4: C7 — естественная точка «объект карты перед зоной» (перебор спотов)
	void StageMouse();          // этап 5: R1…R6 — двойной клик настоящим путём мыши
	void Restore();             // восстановление
	void Finish();              // итог
};

bool FAsdaFishZoneSelfTest::RunLogic(UWorld* W)
{
	World = W;
	GI = W ? W->GetGameInstance() : nullptr;
	F = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	Z = GI ? GI->GetSubsystem<UAsdaFishZoneSubsystem>() : nullptr;
	Win = GI ? GI->GetSubsystem<UAsdaFishWindowSubsystem>() : nullptr;
	LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	Sys = GI ? GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
	MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
	QL = GI ? GI->GetSubsystem<UAsdaQuestLogSubsystem>() : nullptr;
	Extra = GI ? GI->GetSubsystem<UAsdaExtraMenuSubsystem>() : nullptr;
	Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	Ch = F ? F->SelfChar() : nullptr;
	if (!F || !Z || !Win || !LM || !Hot || !Sys || !MM || !QL || !Extra || !Chat || !WM || !PC || !Ch || !Net)
	{
		Expect(false, TEXT("нет подсистем/контроллера/персонажа"));
		return false;
	}
	MapWas = LM->CurrentMapId; PosWas = Ch->GetGamePosXZ();
	SlotWas = Hot->GetSlot(kHotSlot);
	ResetFish();
	F->ClearRecentNotices();
	F->GetSpotsOfMap(3, Alp);
	if (Alp.Num() != 10) { Expect(false, FString::Printf(TEXT("A0 спотов Альпии %d (ждём 10)"), Alp.Num())); return false; }
	const FAsdaFishSpotRow* S0 = F->GetSpot(Alp[0]);
	Near = FVector2D((float)S0->X * 100.f + 300.f, (float)S0->Y * 100.f);   // внутри r·100 = 1000 от спота 0
	Far = FVector2D((float)S0->X * 100.f, (float)S0->Y * 100.f - 3000.f);   // вне всех зон Альпии (ближайшая — 1900+)
	PlaceAt(Near);

	// ════════ A. ЗОНЫ В МИРЕ ════════
	const TArray<TWeakObjectPtr<AAsdaFishZoneActor>>& Zs = Z->GetZones();
	Expect(Z->GetZonesMapId() == 3 && Zs.Num() == 10,
		FString::Printf(TEXT("A1 при входе на Альпию созданы зоны: карта %d, зон %d (ждём 3 / 10 = споты Map_info.BIN)"), Z->GetZonesMapId(), Zs.Num()));
	if (Zs.Num() != 10) { return false; }
	{
		bool bPos = true, bObb = true, bRot = true, bMesh = true, bClip = true, bWaterH = true, bBounds = true, bGrid = true;
		int32 NWater = 0;
		// ★ Ревизия В4 МЕЛК-3: ожидаемая h — НЕЗАВИСИМО, из сырого `.WAT` Альпии (тот же контейнер, что `CMap_LoadWAT_wrap`, g:188058).
		TArray<uint8> Wat;
		// ★ 23.10.2026: путь по реестру путей проекта (`AstoryClient\Tools\asda_paths.py`: CLIENT_MINE = ASDA_CLIENT_MINE |
		//   ASDA_DONORS\AsdaChroniclesInterlude_MINE | ASDA_ROOT\Donors\…; раскладка 17.10 — `PROJECT_LAYOUT.md`). Прежний
		//   `<ROOT>\AsdaChroniclesInterlude_MINE` после переезда в `Donors\` не существует — тест читал пустой файл.
		const FString WatPath = [] ()
		{
			FString Mine = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_CLIENT_MINE"));
			if (Mine.IsEmpty())
			{
				FString Donors = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_DONORS"));
				if (Donors.IsEmpty())
				{
					FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_ROOT"));
					if (Root.IsEmpty()) { Root = FPaths::ProjectDir() / TEXT("../.."); }   // <ROOT>\AstoryClient\ → <ROOT>
					Donors = Root / TEXT("Donors");
				}
				Mine = Donors / TEXT("AsdaChroniclesInterlude_MINE");
			}
			return FPaths::ConvertRelativePathToFull(Mine / TEXT("map/alpen/alpen.WAT"));
		}();
		const bool bWat = FFileHelper::LoadFileToArray(Wat, *WatPath);
		FString WatInfo;
		const UAsdaWalkGrid* Grid = AsdaGround::GetGrid();
		for (int32 k = 0; k < 10; ++k)
		{
			const AAsdaFishZoneActor* A = Zs[k].Get();
			const FAsdaFishSpotRow* S = F->GetSpot(Alp[k]);
			if (!A || !S) { bPos = false; continue; }
			const float GX = (float)S->X * 100.f, GZ = (float)S->Y * 100.f;
			const FVector2D Ue2 = AsdaCoord::GameToUe2D(FVector2D(GX, GZ));
			const float Wz = AsdaGround::WaterSurfaceZ(W, (float)Ue2.X, (float)Ue2.Y);   // запрос порта (для журнала и A4)
			float WatH = 0.f;
			const bool bWatHit = bWat && WatHeight(Wat, GX, GZ, WatH);                   // эталонная выборка по сырому .WAT
			const float WantH = bWatHit ? WatH : 0.f;
			NWater += bWatHit ? 1 : 0;
			WatInfo += FString::Printf(TEXT(" %d:%.1f/%.1f"), Alp[k], WantH, A ? A->CenterGame.Y : -1.f);
			const FVector WantUe(Ue2.X, Ue2.Y, WantH);
			bPos &= A->SpotIdx == Alp[k] && FMath::IsNearlyEqual(A->CenterGame.X, GX) && FMath::IsNearlyEqual(A->CenterGame.Z, GZ)
				&& A->GetActorLocation().Equals(WantUe, 0.5);
			bWaterH &= FMath::IsNearlyEqual(A->CenterGame.Y, WantH, 0.5f) && A->bOverWater == bWatHit;
			bObb &= A->HalfGame.Equals(FVector(2.f * S->Radius * 100.f, 100.f, 2.f * S->Radius * 100.f), 0.01);
			bRot &= FMath::IsNearlyEqual(A->GetActorRotation().Yaw, 90.f, 0.01f) && A->MeshRoot->GetRelativeScale3D().Equals(FVector(0.01f, -0.01f, 0.01f));
			const USkeletalMesh* SM = A->Mesh ? A->Mesh->GetSkeletalMeshAsset() : nullptr;
			bMesh &= SM && SM->GetName() == TEXT("fs_zone");
			const UAnimSingleNodeInstance* Inst = A->Mesh ? A->Mesh->GetSingleNodeInstance() : nullptr;
			const UAnimationAsset* Clip = Inst ? Inst->GetAnimationAsset() : nullptr;
			bClip &= Clip && Clip->GetName() == TEXT("fs_zoneanim_00") && Inst->IsLooping() && Inst->IsPlaying();
			// Стайка под точкой зоны: габарит меша ниже/около h, по горизонтали в пределах ±4 м (glb: ±2,2 м, Y −154…−7).
			const FBox Bx = A->Mesh->Bounds.GetBox();
			const FVector L = A->GetActorLocation();
			bBounds &= Bx.Max.Z <= L.Z + 60.f && Bx.Min.Z >= L.Z - 400.f && FVector2D(Bx.GetCenter().X - L.X, Bx.GetCenter().Y - L.Y).Size() < 400.f;
			if (Grid) { bGrid &= Grid->IsWorldOverWater(GX, GZ) == FMath::IsFinite(Wz); }
			UE_LOG(LogAsdaFishZoneTest, Log, TEXT("  зона %d: UE (%.0f,%.0f,%.0f), вода %.1f, габарит Z %.0f…%.0f, центр XY Δ %.0f"),
				Alp[k], L.X, L.Y, L.Z, Wz, Bx.Min.Z, Bx.Max.Z, FVector2D(Bx.GetCenter().X - L.X, Bx.GetCenter().Y - L.Y).Size());
		}
		Expect(bPos, TEXT("A2 позиции: индекс спота по порядку, центр игры (X·100, Y·100), актор в GameToUe3D(X·100, h, Y·100)"));
		Expect(bWat && bWaterH && NWater == 10, FString::Printf(TEXT("A3 h = высота ВОДЫ (sub_926A07 → pickSpriteQuadRayTest) против СЫРОГО .WAT %s (%d б): над водой %d из 10, спот:эталон/порт%s"),
			*WatPath, Wat.Num(), NWater, *WatInfo));
		Expect(bGrid, TEXT("A4 сверка вторым методом: «над водой» по геометрии воды = бит воды тайла DA_WalkGrid (растр .WAT)"));
		Expect(bObb, TEXT("A5 OBB: полуразмеры (2·r·100, 100, 2·r·100) (sub_B5B030; центр ± размер — aabbSizeZ)"));
		Expect(bRot, TEXT("A6 единичная матрица эталона: курс +90°, узел (0.01, −0.01, 0.01) как у мобов/питомцев"));
		Expect(bMesh, TEXT("A7 меш — скелетный fs_zone (Models/Skeletal/Fishing, волна В5а)"));
		Expect(bClip, TEXT("A8 клип fs_zoneanim_00 играет в петле (sceneList_UpdateAndRender)"));
		Expect(bBounds, TEXT("A9 стайка рыб под точкой зоны (габарит меша: верх ≤ h+60, низ ≥ h−400, центр по горизонтали < 4 м)"));
	}
	{
		// Контроль высоты: тайл суши (сам и 8 соседей без воды по растру) — воды над ним нет (NaN → h = 0 у эталона).
		const UAsdaWalkGrid* Grid = AsdaGround::GetGrid();
		bool bFound = false, bNaN = false;
		for (int32 ty = 200; ty < 320 && !bFound && Grid; ++ty)
		{
			for (int32 tx = 200; tx < 320 && !bFound; ++tx)
			{
				bool bDry = true;
				for (int32 dy = -1; dy <= 1; ++dy) { for (int32 dx = -1; dx <= 1; ++dx) { bDry &= !Grid->IsTileOverWater(tx + dx, ty + dy); } }
				if (!bDry) { continue; }
				bFound = true;
				const FVector2D U = AsdaCoord::GameToUe2D(AsdaCoord::TileCenterGame(tx, ty));
				bNaN = !FMath::IsFinite(AsdaGround::WaterSurfaceZ(W, (float)U.X, (float)U.Y));
			}
		}
		Expect(bFound && bNaN, TEXT("A10 контроль: над тайлом суши воды нет (высота воды NaN → у эталона h = 0)"));
	}
	{
		// Контроль карты: номер карты сменился, а мир ещё Альпия (окно между BeginMapLoad и травелом) — чужие зоны НЕ ставятся.
		LM->CurrentMapId = 1;
		Z->ReconcileZones();
		const bool bKept = Z->GetZonesMapId() == 3 && Z->GetZones().Num() == 10;
		LM->CurrentMapId = 3;
		// Пересоздание: прежние актёры удалены, новые — на тех же местах.
		TArray<TWeakObjectPtr<AAsdaFishZoneActor>> Old = Z->GetZones();
		Z->ForceRebuild();
		Z->ReconcileZones();
		bool bOldGone = true;
		for (const TWeakObjectPtr<AAsdaFishZoneActor>& O : Old) { bOldGone &= !IsValid(O.Get()); }
		Expect(bKept && bOldGone && Z->GetZones().Num() == 10 && Z->GetZonesMapId() == 3,
			TEXT("A11 контроль: номер карты 1 при мире Альпии — зоны не пересоздаются; пересборка удаляет старые актёры (mapReleaseAllSceneObjects) и ставит 10 новых"));
	}

	// ════════ B. ГРАНИ OBB (collide_rayVsObb6FacesHit) ════════
	{
		const FVector C(0, 0, 0), H(10, 1, 10);
		float T = -1.f;
		const bool b1 = UAsdaFishZoneSubsystem::RayVsObb6FacesHit(FVector(0, 50, 0), FVector(0, -1, 0), C, H, T) && FMath::IsNearlyEqual(T, 49.f);
		float T2 = -1.f;
		const bool b2 = UAsdaFishZoneSubsystem::RayVsObb6FacesHit(FVector(-50, 0, 0), FVector(1, 0, 0), C, H, T2) && FMath::IsNearlyEqual(T2, 40.f);
		float T3 = -1.f;
		const bool b3 = UAsdaFishZoneSubsystem::RayVsObb6FacesHit(FVector(50, 0, 0), FVector(-1, 0, 0), C, H, T3) && FMath::IsNearlyEqual(T3, 60.f);
		float T4 = -1.f;
		const bool b4 = !UAsdaFishZoneSubsystem::RayVsObb6FacesHit(FVector(0, 50, 20), FVector(0, -1, 0), C, H, T4)
			&& !UAsdaFishZoneSubsystem::RayVsObb6FacesHit(FVector(0, 50, 0), FVector(0, 1, 0), C, H, T4);
		float T5 = -1.f;
		const bool b5 = UAsdaFishZoneSubsystem::RayVsObb6FacesHit(FVector(10, 50, 0), FVector(0, -1, 0), C, H, T5) && FMath::IsNearlyEqual(T5, 49.f);
		Expect(b1 && b2, FString::Printf(TEXT("B1 сверху на крышку Y+: t = %.1f (49); слева через X−: t = %.1f (40)"), T, T2));
		Expect(b3, FString::Printf(TEXT("B2 справа налево: первая по порядку грань X− (ВЫХОДНАЯ) → t = %.1f (60), а не ближняя X+ (40) — 1:1 с эталоном"), T3));
		Expect(b4, TEXT("B3 контроль: мимо (z = 20 > 10) и назад (t < 0) — промах"));
		Expect(b5, TEXT("B4 край грани включительно (x = 10 = граница)"));
	}

	// ════════ C. ДВОЙНОЙ КЛИК ПО ЗОНЕ (world_OnLeftClickPick → тип 5 → inputHotkeyAction109) ════════
	FVector2D ZoneScreen(-1, -1);
	int32 WantSpot = -1;
	{
		int32 VX = 0, VY = 0;
		PC->GetViewportSize(VX, VY);
		for (int32 k = 0; k < 10 && WantSpot < 0; ++k)
		{
			// ★ R5-fix (13.11.2026) [ИСПРАВЛЕНИЕ теста]: зон может не быть (тест запущен не на Альпии — например, сразу после
			//   WarpShots с картой 1): раньше `GetZones()[k]` по пустому массиву ронял игру; теперь шаг C честно краснеет.
			const AAsdaFishZoneActor* A = Z->GetZones().IsValidIndex(k) ? Z->GetZones()[k].Get() : nullptr;
			FVector2D Sp;
			if (!A || !PC->ProjectWorldLocationToScreen(A->GetActorLocation(), Sp)) { continue; }
			if (Sp.X < 2 || Sp.Y < 2 || Sp.X > VX - 2 || Sp.Y > VY - 2) { continue; }
			FVector O, D; float Dist = 0.f;
			if (!PC->DeprojectScreenPositionToWorld(Sp.X, Sp.Y, O, D)) { continue; }
			const int32 Hit = Z->PickZoneByRay(O, D, Dist);
			if (Hit >= 0) { ZoneScreen = Sp; WantSpot = Hit; }
		}
		Expect(WantSpot >= 0, FString::Printf(TEXT("C0 зона на экране: точка (%.0f,%.0f) вьюпорта %d×%d, луч → спот %d"), ZoneScreen.X, ZoneScreen.Y, VX, VY, WantSpot));
	}
	if (WantSpot >= 0)
	{
		Click(ZoneScreen, /*bDouble*/false);
		const int32 FsSingle = F->GetFishState();
		PlaceAt(Near);
		Expect(FsSingle == 0, FString::Printf(TEXT("C1 контроль: ОДИНОЧНЫЙ клик по зоне — тип 5 не участвует (Input_WorldClickHandler, a6 = 0): автомат %d"), FsSingle));
		Click(ZoneScreen, /*bDouble*/true);
		PlaceAt(Near);
		Expect(F->GetFishState() == 109 && F->GetPickedSpot() == WantSpot,
			FString::Printf(TEXT("C2 ДВОЙНОЙ клик: тип 5 → PickSpot + inputHotkeyAction109: автомат %d, зона %d (ждём 109 / %d)"), F->GetFishState(), F->GetPickedSpot(), WantSpot));
		// Гейт клика при рыбалке: ранний выход ДО сброса автобега (bAutoRunActive остаётся).
		PC->bAutoRunActive = true;
		Click(ZoneScreen, false);
		const bool bGateKept = PC->bAutoRunActive;
		ResetFish();
		PC->bAutoRunActive = true;
		Click(ZoneScreen, false);
		const bool bNoGateReset = !PC->bAutoRunActive;
		PC->bAutoRunActive = false;
		PlaceAt(Near);
		Expect(bGateKept && bNoGateReset, TEXT("C3 гейт мирового клика: при рыбалке клик не доходит до разбора (game.c:12566/13266); контроль — без рыбалки доходит"));
		// Соперники: моб ближе зоны — зона проигрывает; моб дальше — выигрывает; террейн ближе — не соперник.
		{
			FVector O, D; float ZD = 0.f;
			PC->DeprojectScreenPositionToWorld(ZoneScreen.X, ZoneScreen.Y, O, D);
			Z->PickZoneByRay(O, D, ZD);
			AAsdaPlayerController::FAsdaPick Riv;
			Riv.Kind = AAsdaPlayerController::EAsdaPickKind::Mob; Riv.Actor = Ch;
			PC->DebugClickScreen = ZoneScreen;
			Riv.Point = O + D * (ZD * 0.5f);
			const int32 rNear = PC->PickFishZoneForDoubleClick(Riv);
			Riv.Point = O + D * (ZD + 100.f);
			const int32 rFar = PC->PickFishZoneForDoubleClick(Riv);
			Riv.Point = O + D * ZD;   // равенство — за ранее найденным типом
			const int32 rEq = PC->PickFishZoneForDoubleClick(Riv);
			AStaticMeshActor* Terr = nullptr;
			for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
			{
				const UStaticMesh* SM = It->GetStaticMeshComponent() ? It->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
				if (SM && SM->GetName().StartsWith(TEXT("SM_terrain"))) { Terr = *It; break; }
			}
			AAsdaPlayerController::FAsdaPick Gr;
			Gr.Kind = AAsdaPlayerController::EAsdaPickKind::Ground; Gr.Actor = Terr; Gr.Point = O + D * (ZD * 0.5f);
			Gr.Component = Terr ? Terr->GetStaticMeshComponent() : nullptr;
			const int32 rTerr = PC->PickFishZoneForDoubleClick(Gr);
			// ★ Ревизия В4 ВАЖН-2: соперник «объект карты» — экземпляр ISM на `AsdaMapObjects_<карта>` (как ставит сборщик уровня).
			UInstancedStaticMeshComponent* Ism = nullptr;
			for (TActorIterator<AActor> It(W); It && !Ism; ++It)
			{
				if (!It->GetActorNameOrLabel().StartsWith(TEXT("AsdaMapObjects_"))) { continue; }
				TInlineComponentArray<UInstancedStaticMeshComponent*> Cs(*It);
				for (UInstancedStaticMeshComponent* C : Cs) { if (C && C->GetStaticMesh() && C->GetInstanceCount() > 0) { Ism = C; break; } }
			}
			AAsdaPlayerController::FAsdaPick Ob;
			Ob.Kind = AAsdaPlayerController::EAsdaPickKind::Ground; Ob.Actor = Ism ? Ism->GetOwner() : nullptr; Ob.Component = Ism;
			Ob.Point = O + D * (ZD * 0.5f);
			const int32 rObjNear = PC->PickFishZoneForDoubleClick(Ob);
			Ob.Point = O + D * (ZD + 100.f);
			const int32 rObjFar = PC->PickFishZoneForDoubleClick(Ob);
			// Контроль старой ошибки: тот же экземпляр, классифицированный по актору (простой `AActor`), соперником не был бы.
			const bool bOldMissed = Ism && Cast<AStaticMeshActor>(Ism->GetOwner()) == nullptr;
			PC->DebugClickScreen.Reset();
			Expect(Ism && UAsdaFishZoneSubsystem::IsMapObjectHit(Ism) && rObjNear < 0 && rObjFar == WantSpot && bOldMissed,
				FString::Printf(TEXT("C7 соперник «объект карты» (тип 4 collide_rayVsMapObjects): экземпляр ISM %s на %s — ближе зоны: зона проиграла (%d), дальше: выиграла (%d); актор — не AStaticMeshActor (прежняя проверка его не видела)"),
					Ism ? *Ism->GetName() : TEXT("НЕТ"), (Ism && Ism->GetOwner()) ? *Ism->GetOwner()->GetActorNameOrLabel() : TEXT("-"), rObjNear, rObjFar));
			// ★ В45fix2 (ревизия В4/В5, МЕЛК-4): КОНТРОЛЬ сужения — статический меш НЕ объекта карты (временный
			//   `AStaticMeshActor` с кубом движка, как системные акторы «земли») ближе зоны соперником не является: зона выбрана.
			//   Прежняя проверка (любой `UStaticMeshComponent` с мешем) засчитала бы его типом 4 и зона проиграла бы.
			{
				FActorSpawnParameters SpP;
				SpP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AStaticMeshActor* SysA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(O + D * (ZD * 0.5f)), SpP);
				UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				if (SysA && Cube) { SysA->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); SysA->GetStaticMeshComponent()->SetStaticMesh(Cube); }
				const bool bSysHasMesh = SysA && Cube && SysA->GetStaticMeshComponent()->GetStaticMesh() == Cube;   // меш реально стоит (контроль не пустой)
				AAsdaPlayerController::FAsdaPick So;
				So.Kind = AAsdaPlayerController::EAsdaPickKind::Ground; So.Actor = SysA; So.Point = O + D * (ZD * 0.5f);
				So.Component = SysA ? SysA->GetStaticMeshComponent() : nullptr;
				const bool bSysObj = UAsdaFishZoneSubsystem::IsMapObjectHit(So.Component.Get());
				PC->DebugClickScreen = ZoneScreen;   // луч тот же, что у C4/C7
				const int32 rSys = PC->PickFishZoneForDoubleClick(So);
				PC->DebugClickScreen.Reset();
				// Контроль второй стороны: экземпляр ISM на НЕпростом акторе (держатель не `AActor`) — тоже не объект карты.
				UInstancedStaticMeshComponent* SysIsm = SysA ? NewObject<UInstancedStaticMeshComponent>(SysA, TEXT("I_selftest_cube")) : nullptr;
				if (SysIsm && Cube) { SysIsm->SetStaticMesh(Cube); SysIsm->RegisterComponent(); }
				const bool bSysIsmObj = UAsdaFishZoneSubsystem::IsMapObjectHit(SysIsm);
				const bool bSysIsmHasMesh = SysIsm && Cube && SysIsm->GetStaticMesh() == Cube;
				if (SysA) { SysA->Destroy(); }
				Expect(bSysHasMesh && bSysIsmHasMesh && !bSysObj && !bSysIsmObj && rSys == WantSpot && Ism && UAsdaFishZoneSubsystem::IsMapObjectHit(Ism),
					FString::Printf(TEXT("C7в КОНТРОЛЬ МЕЛК-4: статический меш системного актора (AStaticMeshActor+куб) ближе зоны — не объект карты %d, зона выбрана (%d, ждём %d); ISM «I_» на НЕпростом акторе — не объект карты %d; экземпляр на AsdaMapObjects — объект карты"),
						bSysObj ? 1 : 0, rSys, WantSpot, bSysIsmObj ? 1 : 0));
			}
			Expect(rNear < 0 && rFar == WantSpot && rEq < 0, FString::Printf(TEXT("C4 соперник-сущность: ближе — зона проиграла (%d), дальше — выиграла (%d), на равной дальности — проиграла (%d)"), rNear, rFar, rEq));
			Expect(Terr && rTerr == WantSpot && !UAsdaFishZoneSubsystem::IsMapObjectHit(Terr->GetStaticMeshComponent()),
				TEXT("C5 террейн ближе зоны — не соперник (в пикинге эталона террейна нет), зона выбрана"));
		}
		// Промах: точка экрана, луч которой не задевает ни одной зоны, — 109 нет.
		{
			int32 VX = 0, VY = 0; PC->GetViewportSize(VX, VY);
			FVector2D Miss(-1, -1);
			for (int32 y = 4; y < VY && Miss.X < 0; y += 16)
			{
				for (int32 x = 4; x < VX && Miss.X < 0; x += 16)
				{
					FVector O, D; float Dd = 0.f; FHitResult Hh;
					if (!PC->DeprojectScreenPositionToWorld((float)x, (float)y, O, D)) { continue; }
					if (Z->PickZoneByRay(O, D, Dd) >= 0) { continue; }
					if (!PC->TraceScreenSkippingUnrenderedChars(FVector2D(x, y), Hh)) { continue; }   // нужна земля под курсором
					if ((Hh.ImpactPoint - Ch->GetActorLocation()).Size2D() > 3000.f) { continue; }
					Miss = FVector2D(x, y);
				}
			}
			if (Miss.X >= 0)
			{
				Click(Miss, true);
				PlaceAt(Near);
				Expect(F->GetFishState() == 0, FString::Printf(TEXT("C6 контроль: двойной клик мимо всех зон (%.0f,%.0f) — 109 нет (автомат %d)"), Miss.X, Miss.Y, F->GetFishState()));
			}
			else { Expect(false, TEXT("C6 не найдена точка экрана мимо зон (зоны Альпии 4r×4r перекрывают весь кадр)")); }
		}
	}
	ResetFish();

	// ════════ D. ДОП-МЕНЮ cmd 8 (targetSelectNearestEntity) И ЯЧЕЙКА ХОТБАРА ТИПА 5 ════════
	F->ClearRecentNotices();
	PlaceAt(Near);
	Extra->DispatchCommand(8, TEXT("самотест: рыбалка"));
	Expect(F->GetFishState() == 109 && F->GetPickedSpot() == Alp[0],
		FString::Printf(TEXT("D1 cmd 8 рядом со спотом 0: ближайшая зона в радиусе → 109 (автомат %d, зона %d, ждём %d)"), F->GetFishState(), F->GetPickedSpot(), Alp[0]));
	ResetFish();
	F->ClearRecentNotices();
	PlaceAt(Far);
	Extra->DispatchCommand(8, TEXT("самотест: рыбалка"));
	Expect(F->GetFishState() == 0 && HadNotice(TEXT("chat"), 110700),
		FString::Printf(TEXT("D2 cmd 8 далеко: чат 110700 «Расстояние не позволяет…», автомат 0 (%d)"), F->GetFishState()));
	ResetFish();
	PlaceAt(Near);
	{
		// Ячейка хотбара типа 5 с ключом «код команды» 8 → `Hotbar_SlotActivate` → общий диспетчер доп-меню.
		const bool bSet = Hot->SetSlotContent(kHotSlot, (uint8)EAsdaHotbarSlotType::ExtraMenu, (uint8)UAsdaExtraMenuSubsystem::kExtraKeyCmd, 8, 0, 0);
		FString WhyBase;
		const bool bBlockedBase = Hot->IsActivationBlockedForCheck(WhyBase);
		Hot->ActivateSlot(kHotSlot);
		const int32 FsHot = F->GetFishState();
		Expect(bSet && !bBlockedBase && FsHot == 109 && F->GetPickedSpot() == Alp[0],
			FString::Printf(TEXT("D3 ячейка хотбара типа 5 (пункт «рыбалка») → общий диспетчер доп-меню → 109 (автомат %d; блок панели «%s»)"), FsHot, *WhyBase));
		// Гейт хотбара: при рыбалке слот молчит (тот же слот повторно ничего не делает — автомат остаётся 109, причина — рыбалка).
		FString WhyFish;
		const bool bBlockedFish = Hot->IsActivationBlockedForCheck(WhyFish);
		Expect(bBlockedFish && WhyFish.Contains(TEXT("рыбалка")) && !bBlockedBase,
			FString::Printf(TEXT("E1 гейт хотбара (game.c:427372): вне рыбалки слот работает, при рыбалке — молча отклонён («%s»)"), *WhyFish));
		// ★ Ревизия В4 МЕЛК-8: тот же гейт НАСТОЯЩИМ путём слота (`ActivateSlot`). Далеко от зон пункт «рыбалка» даёт 110700 и
		//   автомат 0 (Д9). При рыбалке (115) слот молчит: ни нотиса, ни сброса автомата; контроль — вне рыбалки нотис есть.
		ResetFish();
		PlaceAt(Far);
		SetFs(115);
		F->ClearRecentNotices();
		Hot->ActivateSlot(kHotSlot);
		const bool bHeldFs = F->GetFishState() == 115 && !HadNotice(TEXT("chat"), 110700);
		ResetFish();
		F->ClearRecentNotices();
		Hot->ActivateSlot(kHotSlot);
		const bool bWorks = F->GetFishState() == 0 && HadNotice(TEXT("chat"), 110700);
		PlaceAt(Near);
		Expect(bHeldFs && bWorks, TEXT("E1b хотбар настоящим путём ActivateSlot: при рыбалке слот «рыбалка» молчит (автомат 115, 110700 нет); вне рыбалки — 110700 и 0"));
		ResetFish();
	}

	// ════════ E. ГЕЙТЫ ОКОН (каждый — парой) ════════
	{
		const bool bSys0 = Sys->IsTextInputFocused();
		SetFs(109);
		const bool bSys1 = Sys->IsTextInputFocused();
		SetFs(0);
		Expect(!bSys0 && bSys1, TEXT("E2 системное меню: кнопки 73401…73410 (uiHotkey_DispatchCommand g:488438) — вне рыбалки открывают, при рыбалке нет"));
		// ★ Ревизия В4 МЕЛК-8: настоящим путём кнопки (`ClickButton` → `OnButtonToggled`): кнопка без обработчика эталона (73408/
		//   73411 — действия нет, окна не открываются). При рыбалке — строка гейта «подавлено», вне рыбалки — её нет.
		int32 BtnId = 0;
		for (const FAsdaSysMenuButton& B : Sys->Buttons) { if (B.IdNum == 73408 || B.IdNum == 73411) { BtnId = B.IdNum; break; } }
		if (BtnId != 0)
		{
			FLineCatch Catch;
			GLog->AddOutputDevice(&Catch);
			const FString Gate = FString::Printf(TEXT("открытие по кнопке %d подавлено"), BtnId);
			SetFs(109);
			Sys->ClickButton(BtnId);    // → открыть: гейт
			Sys->ClickButton(BtnId);    // вернуть флажок (закрытие гейта не имеет)
			GLog->Flush();
			const bool bGated = Catch.Has(*Gate);
			SetFs(0);
			{ FScopeLock L(&Catch.Cs); Catch.Lines.Reset(); }
			Sys->ClickButton(BtnId);
			Sys->ClickButton(BtnId);
			GLog->Flush();
			const bool bFree = !Catch.Has(*Gate) && Catch.Has(TEXT("обработчика в эталоне нет"));
			GLog->RemoveOutputDevice(&Catch);
			Expect(bGated && bFree, FString::Printf(TEXT("E2b системное меню настоящим путём ClickButton(%d): при рыбалке открытие подавлено гейтом, вне рыбалки доходит до разбора кнопки"), BtnId));
		}
		else { Expect(false, TEXT("E2b в панели системного меню нет кнопки 73408/73411")); }
		const bool bExp0 = Sys->IsExpanded();
		Sys->OnCollapseButton(bExp0 ? 1 : 0);
		const bool bToggled = Sys->IsExpanded() != bExp0;
		Sys->OnCollapseButton(bExp0 ? 0 : 1);   // вернуть
		SetFs(109);
		Sys->OnCollapseButton(bExp0 ? 1 : 0);
		const bool bHeld = Sys->IsExpanded() == bExp0;
		SetFs(0);
		Expect(bToggled && bHeld, TEXT("E3 свёртка 73425/73426 (uiWindowMsgProc 0x10401 g:488802): вне рыбалки работает, при рыбалке нет"));
	}
	{
		const bool bOpen0 = MM->bChannelPanelOpen;
		MM->OnChannelBtnClicked();
		const bool bToggled = MM->bChannelPanelOpen != bOpen0;
		if (bToggled) { MM->OnChannelBtnClicked(); }
		SetFs(109);
		MM->OnChannelBtnClicked();
		const bool bHeld = MM->bChannelPanelOpen == bOpen0;
		SetFs(0);
		Expect(bToggled && bHeld, TEXT("E4 окно карты 17500 (MiniMap_BigMapWindowProc 0xB g:374375): вне рыбалки панель каналов открывается, при рыбалке нет"));
	}
	{
		const bool bP0 = QL->IsPageExpanded();
		if (bP0) { QL->OnCollapseClicked(); } else { QL->OnExpandClicked(); }
		const bool bToggled = QL->IsPageExpanded() != bP0;
		if (bToggled) { if (bP0) { QL->OnExpandClicked(); } else { QL->OnCollapseClicked(); } }
		SetFs(109);
		if (bP0) { QL->OnCollapseClicked(); } else { QL->OnExpandClicked(); }
		const bool bHeld = QL->IsPageExpanded() == bP0;
		const bool b780 = AsdaQuestEpisodeFishingGate(GI, 780);
		SetFs(0);
		const bool b780c = AsdaQuestEpisodeFishingGate(GI, 780);
		Expect(bToggled && bHeld && b780 && !b780c,
			TEXT("E5 окно заданий (CQuest_Episode_OnEvent 66561 g:285554): 739/735 и 780 — вне рыбалки работают, при рыбалке событие поглощено"));
	}
	return true;
}

// Этап 2: клавиши настоящим путём Slate, поле чата, метки карты, ход клипа зоны. Шаги — по таймеру (нужны кадры).
void FAsdaFishZoneSelfTest::Stage2()
{
	UWorld* W = World.Get();
	if (!W) { Finish(); return; }
	TSharedRef<FAsdaFishZoneSelfTest> Self = AsShared();
	// Автомат на этапе 2 не тикает сам (как в самотестах В1/В2): состояние ставит тест, гайд не уходит в таймаут, диалог 81 не всплывает.
	F->bTestNoNetDrain = true;
	// Зона, у которой меряется ход клипа: ближайшая к персонажу.
	{
		float Best = 1e18f;
		for (const TWeakObjectPtr<AAsdaFishZoneActor>& Zz : Z->GetZones())
		{
			const AAsdaFishZoneActor* A = Zz.Get();
			if (!A) { continue; }
			const float D = FVector::DistSquared(A->GetActorLocation(), Ch->GetActorLocation());
			if (D < Best) { Best = D; ClipZone = const_cast<AAsdaFishZoneActor*>(A); }
		}
		const UAnimSingleNodeInstance* I = ClipZone.IsValid() ? ClipZone->Mesh->GetSingleNodeInstance() : nullptr;
		ClipT0 = I ? I->GetCurrentTime() : -1.f;
	}
	// K1: вне рыбалки W через Slate → удержание в UPlayerInput.
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
	SetFs(0);
	SlateKey(EKeys::W, true);
	auto Seq = [W](float Delay, TFunction<void()> Fn)
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), Delay, false);
	};
	Seq(0.15f, [Self, Seq]()
	{
		const bool bHeld0 = Self->PC->IsInputKeyDown(EKeys::W);
		SlateKey(EKeys::W, false);
		Self->PlaceAt(Self->Near);
		Self->SetFs(109);
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
		SlateKey(EKeys::W, true);
		SlateKey(EKeys::One, true);
		Seq(0.15f, [Self, Seq, bHeld0]()
		{
			const bool bHeldW = Self->PC->IsInputKeyDown(EKeys::W);
			const bool bHeld1 = Self->PC->IsInputKeyDown(EKeys::One);
			SlateKey(EKeys::W, false);
			SlateKey(EKeys::One, false);
			Self->SetFs(0);
			Self->Expect(bHeld0, TEXT("K1 контроль метода: вне рыбалки W через Slate (ProcessKeyDownEvent → вьюпорт → UPlayerInput) — зажата"));
			Self->Expect(bHeld0 && !bHeldW && !bHeld1,
				TEXT("K2 при рыбалке W и «1» тем же путём НЕ зажаты (ни хода, ни хотбара; MainWndProc g:24144)"));
			// L. Поле ввода чата.
			Self->Chat->OpenInputWithText(TEXT("abc"));
			Seq(0.25f, [Self, Seq]()
			{
				FString T0; const bool bFocus = FocusedEditText(T0);
				SlateChar(TEXT('x'));
				FString T1; FocusedEditText(T1);
				// Backspace у поля Slate — СИМВОЛ 0x08 (WM_CHAR), нажатие клавиши BackSpace поле не правит.
				SlateKey(EKeys::BackSpace, true); SlateChar((TCHAR)8); SlateKey(EKeys::BackSpace, false);
				FString T2; FocusedEditText(T2);
				Self->Expect(bFocus && T0 == TEXT("abc") && T1 == TEXT("abcx") && T2 == TEXT("abc"),
					FString::Printf(TEXT("L1 контроль: вне рыбалки поле чата с фокусом пишет: «%s» → «%s» → «%s»"), *T0, *T1, *T2));
				// Рыбалка с гайдом в состоянии ввода стрелок.
				Self->SetFs(115);
				Self->Win->Guide.State = 4;
				Self->Win->Guide.Recs.Reset();
				for (int32 j = 0; j < 4; ++j) { FAsdaFishKeyRec R; R.Vk = 38; Self->Win->Guide.Recs.Add(R); }
				Self->EatenWas = Self->Z->GetKeyGuardEaten();
				Seq(0.15f, [Self, Seq]()
				{
					const bool bLocked = Self->Z->IsTextFieldLocked();
					// ★ Ревизия В4 МЕЛК-9: поле чата — игровой UI (внутри вьюпорта); виджет вне окон (как панель редактора) — нет.
					{
						const TSharedPtr<SWidget> Fw = FSlateApplication::Get().GetUserFocusedWidget(0);
						const TSharedRef<SEditableText> Orphan = SNew(SEditableText);
						Self->Expect(UAsdaFishZoneSubsystem::IsGameUiWidget(Fw) && !UAsdaFishZoneSubsystem::IsGameUiWidget(Orphan),
							TEXT("L7 препроцессор/замок трогают только игровой UI: поле чата — внутри игрового вьюпорта, поле вне окон игры — нет (PIE: окна редактора)"));
					}
					SlateChar(TEXT('y'));
					SlateKey(EKeys::BackSpace, true); SlateChar((TCHAR)8); SlateKey(EKeys::BackSpace, false);
					SlateKey(EKeys::Up, true); SlateKey(EKeys::Up, false);
					FString T3; const bool bFocus = FocusedEditText(T3);
					// Backspace — тоже ответ (эталон: ЛЮБОЕ WM_KEYDOWN в состоянии 4, VK 8 ≠ стрелке → неверно), стрелка ↑ — второй ответ, верный.
					const TArray<FAsdaFishKeyRec>& Rc = Self->Win->Guide.Recs;
					const bool bAnswered = Rc.Num() > 1 && Rc[0].bAnswered && !Rc[0].bCorrect && Rc[1].bAnswered && Rc[1].bCorrect;
					const int32 Eaten = Self->Z->GetKeyGuardEaten() - Self->EatenWas;
					Self->Expect(bLocked && bFocus && T3 == TEXT("abc") && Eaten >= 4,
						FString::Printf(TEXT("L2 при рыбалке поле чата клавиш не получает: символ, Backspace, стрелка — текст «%s», поглощено событий %d, поле заперто %d"),
							*T3, Eaten, bLocked ? 1 : 0));
					Self->Expect(bAnswered, TEXT("L3 при фокусе в чате нажатия ушли в мини-игру (Input_FishingKeyFilter первым): Backspace — ответ «неверно», ↑ — «верно»"));
					// ★ Ревизия В4 ВАЖН-3: Alt через препроцессор (фокус в чате). Нажат — ↑ не засчитан (Alt-сочетание = WM_SYSKEYDOWN);
					//   отпущен — признак погас, ↑ засчитан «верно». Прежде отпускание только поглощалось, и признак застревал.
					{
						Self->Win->Guide.Recs.Reset();
						for (int32 j = 0; j < 4; ++j) { FAsdaFishKeyRec R; R.Vk = 38; Self->Win->Guide.Recs.Add(R); }
						SlateKey(EKeys::LeftAlt, true);
						const bool bAltOn = Self->PC->IsFishAltHeldForCheck();
						SlateKey(EKeys::Up, true); SlateKey(EKeys::Up, false);
						const bool bNotCounted = !Self->Win->Guide.Recs[0].bAnswered;
						SlateKey(EKeys::LeftAlt, false);
						const bool bAltOff = !Self->PC->IsFishAltHeldForCheck();
						SlateKey(EKeys::Up, true); SlateKey(EKeys::Up, false);
						const bool bCounted = Self->Win->Guide.Recs[0].bAnswered && Self->Win->Guide.Recs[0].bCorrect;
						Self->Expect(bAltOn && bNotCounted && bAltOff && bCounted,
							FString::Printf(TEXT("L5 Alt через препроцессор (фокус в чате): нажат — признак %d, ↑ не засчитан %d; отпущен — признак снят %d, ↑ засчитан «верно» %d"),
								bAltOn ? 1 : 0, bNotCounted ? 1 : 0, bAltOff ? 1 : 0, bCounted ? 1 : 0));
					}
					Self->Win->Guide.State = 0; Self->Win->Guide.Recs.Reset();
					// ★ fishfix 09.10 (ревизия МЕЛК-3): контрольная пара ключа `asda.Fish.KeyGuard`. Рыбалка идёт (115), фокус в
					//   поле чата: при 0 (прежний порт) замок поля снимается и препроцессор клавиш не глушит — символ и Backspace
					//   доходят до поля; при возврате 1 поле снова заперто. Кадр замка — прямым вызовом `TickTextFieldLock`.
					{
						IConsoleVariable* CvKG = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.KeyGuard"));
						const int32 KGWas = CvKG ? CvKG->GetInt() : 1;
						if (CvKG) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.KeyGuard"), 0); }
						Self->Z->TickTextFieldLock();
						const bool bUnlockedK0 = !Self->Z->IsTextFieldLocked();
						const int32 E0 = Self->Z->GetKeyGuardEaten();
						SlateChar(TEXT('q'));
						FString T5; const bool bF5 = FocusedEditText(T5);
						SlateKey(EKeys::BackSpace, true); SlateChar((TCHAR)8); SlateKey(EKeys::BackSpace, false);
						FString T6; FocusedEditText(T6);
						const int32 Eaten0 = Self->Z->GetKeyGuardEaten() - E0;
						if (CvKG) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.KeyGuard"), KGWas); }
						Self->Z->TickTextFieldLock();
						const bool bRelocked = Self->Z->IsTextFieldLocked();
						Self->Expect(CvKG && bUnlockedK0 && bF5 && T5 == TEXT("abcq") && T6 == TEXT("abc") && Eaten0 == 0 && bRelocked,
							FString::Printf(TEXT("L6 контроль МЕЛК-3: asda.Fish.KeyGuard 0 при рыбалке — поле чата отперто %d, пишет «%s» → «%s», поглощено %d (ждём 0); ключ 1 — снова заперто %d"),
								bUnlockedK0 ? 1 : 0, *T5, *T6, Eaten0, bRelocked ? 1 : 0));
					}
					Self->SetFs(0);
					Seq(0.15f, [Self, Seq]()
					{
						const bool bUnlocked = !Self->Z->IsTextFieldLocked();
						SlateChar(TEXT('z'));
						FString T4; FocusedEditText(T4);
						Self->Expect(bUnlocked && T4 == TEXT("abcz"), FString::Printf(TEXT("L4 после рыбалки поле отперто и пишет: «%s»"), *T4));
						Self->Chat->CloseInput();
						FSlateApplication::Get().SetAllUserFocusToGameViewport();
						// M. Метки карты.
						const TArray<FAsdaMapMark>* Mk = Self->MM->FindMarks(EAsdaMapMark::Fishing);
						bool bList = Mk && Mk->Num() == Self->F->GetSpotNum();
						for (int32 i = 0; bList && i < Mk->Num(); ++i)
						{
							const FAsdaFishSpotRow* S = Self->F->GetSpot(i);
							bList &= S && (*Mk)[i].TileX == S->X && (*Mk)[i].TileY == S->Y && (*Mk)[i].MapId == S->MapId;
						}
						Self->Expect(bList, FString::Printf(TEXT("M1 список FishingArea: %d меток = все споты таблицы (тайлы и карта) — фильтр «показываемая карта» у отрисовки"), Mk ? Mk->Num() : -1));
						TArray<UImage*> FishQ, PlQ;
						Self->MM->CountDrawnMarks(EAsdaMapMark::Fishing, false, &FishQ);
						Self->MM->CountDrawnMarks(EAsdaMapMark::Player, false, &PlQ);
						if (FishQ.Num() > 0 && PlQ.Num() == 1)
						{
							const FVector2D P = Self->Ch->GetGamePosXZ();
							TArray<FVector2D> Exp;
							for (const int32 s : Self->Alp)
							{
								const FAsdaFishSpotRow* S = Self->F->GetSpot(s);
								// ★ Ревизия В4 МЕЛК-1: эталон `uiSlotIndexToScreenPos@0xB4DBC0` — x = base − 259 + t·k (g:190918), без +½ тайла.
								Exp.Add(FVector2D(100.f * S->X - (float)P.X, 100.f * S->Y - (float)P.Y));
							}
							// ★ В45fix2 (ревизия В4/В5, МЕЛК-1): коэффициент ИЗВЕСТЕН — K = масштаб мир→тексел × пикселей на тексел
							//   (`UpdateMap`: Px = (Texel − Scroll)·MarkPxPerTexel, Texel = 100·t·Scale, значок игрока = Pos·Scale), и
							//   сверка абсолютная: |Δq − K·De| ≤ 0.1 px. Контрольная пара — прежняя формула «+½ тайла» (100·t + 50):
							//   её сдвиг K·50 = 0.25 px по оси, и ни один квад не обязан уложиться в допуск.
							AAsdaPlayerCharacter* GridP = nullptr;
							const UAsdaWalkGrid* Grid = Self->MM->GetGrid(GridP);
							const float KPx = Grid ? Self->MM->WorldToTexelScale(Grid) * Self->MM->MarkPxPerTexel() : -1.f;
							TArray<FVector2D> ExpHalf;
							for (const FVector2D& De : Exp) { ExpHalf.Add(De + FVector2D(50.f, 50.f)); }
							constexpr float kTolPx = 0.1f;
							float MaxErr = 0.f, MinErr = 0.f, MaxErrH = 0.f, MinErrH = 0.f;
							const int32 NAbs = CountQuadAbs(FishQ, QuadPos(PlQ[0]), Exp, KPx, kTolPx, MaxErr, MinErr);
							const int32 NHalf = CountQuadAbs(FishQ, QuadPos(PlQ[0]), ExpHalf, KPx, kTolPx, MaxErrH, MinErrH);
							const bool bShiftResolvable = KPx > 0.f && KPx * 50.f > 2.f * kTolPx;   // сдвиг «+½» больше двух допусков
							Self->Expect(KPx > 0.f && NAbs == FishQ.Num() && bShiftResolvable,
								FString::Printf(TEXT("M2a мини-карта АБСОЛЮТНО: |Δq − K·(100·t − позиция)| ≤ %.1f px у %d из %d квадов (K = %.5f px/ед., наибольшая невязка %.4f px) — формула эталона uiSlotIndexToScreenPos (t·k)"),
									kTolPx, NAbs, FishQ.Num(), KPx, MaxErr));
							Self->Expect(KPx > 0.f && NHalf == 0 && MinErrH > kTolPx,
								FString::Printf(TEXT("M2b КОНТРОЛЬ: прежняя формула «+½ тайла» (100·t + 50) НЕ совпадает — уложилось %d квадов, наименьшая невязка %.4f px > %.1f px (сдвиг K·50 = %.3f px)"),
									NHalf, MinErrH, kTolPx, KPx * 50.f));
							FString Info;
							const bool bRatio = Self->CheckQuadRatio(FishQ, QuadPos(PlQ[0]), Exp, Info);
							UCanvasPanel* Layer = Self->MM->GetMarkLayer(false);
							bool bLast = Layer != nullptr;
							const int32 PIdx = Layer ? Layer->GetChildIndex(PlQ[0]) : -1;
							for (UImage* Q : FishQ) { bLast &= Layer && Layer->GetChildIndex(Q) > PIdx; }
							bool bSize = true;
							for (UImage* Q : FishQ) { bSize &= FMath::IsNearlyEqual(Q->GetBrush().ImageSize.X, 16.f) && FMath::IsNearlyEqual(Q->GetBrush().ImageSize.Y, 16.f); }
							Self->Expect(bRatio && bSize, FString::Printf(TEXT("M2 мини-карта: квады FishingArea 16×16 относительно значка игрока = (100·t − позиция)·масштаб — формула эталона uiSlotIndexToScreenPos (t·k, без +½) (%s)"), *Info));
							Self->Expect(bLast, TEXT("M3 мини-карта: квады зон ПОСЛЕДНИЕ в слое (поверх значка игрока; minimapDrawMarkerList после minimapDrawTargetMarker)"));

							// ★ M2c (решение владельца 09.10.2026, п. 3): ПРОЧИЕ метки мини-карты — отряд, клан, соулмейт, квест-NPC (оба
							//   вида), точка автобега — той же формулой эталона t·k (без +½), что и рыбалка. Временные метки ставятся на тайлы
							//   спотов Alpen, сверка абсолютная (CountQuadAbs). Контроль: ключ `asda.Minimap.LegacyHalfTile 1` — совпадает
							//   «+½», а t·k нет. Прежние списки, точка автобега и ключ восстанавливаются.
							{
								static const EAsdaMapMark kOther[] = { EAsdaMapMark::Party, EAsdaMapMark::Guild, EAsdaMapMark::Couple,
									EAsdaMapMark::Quest, EAsdaMapMark::Quest2 };
								TMap<uint8, TArray<FAsdaMapMark>> Saved;          // прежние списки (восстановление)
								TSet<uint8> HadList;                               // какие списки были до проверки
								for (const EAsdaMapMark Kd : kOther)
								{
									if (const TArray<FAsdaMapMark>* Was = Self->MM->FindMarks(Kd)) { Saved.Add((uint8)Kd, *Was); HadList.Add((uint8)Kd); }
									TArray<FAsdaMapMark> Tmp;
									for (const int32 s : Self->Alp)
									{
										const FAsdaFishSpotRow* S = Self->F->GetSpot(s);
										FAsdaMapMark Mm; Mm.TileX = S->X; Mm.TileY = S->Y; Mm.MapId = S->MapId; Mm.Kind = Kd;
										Tmp.Add(Mm);
									}
									Self->MM->SetMarks(Kd, Tmp);
								}
								const bool bDestWas = Self->MM->bDestActive;      // прежняя точка автобега
								const int32 DxWas = Self->MM->DestTileX, DzWas = Self->MM->DestTileZ;
								const FAsdaFishSpotRow* S0 = Self->F->GetSpot(Self->Alp[0]);
								Self->MM->bDestActive = true; Self->MM->DestTileX = S0->X; Self->MM->DestTileZ = S0->Y;
								const double BlinkWas = Self->MM->QuestBlinkT;    // квест-точки в фазе «видно»
								Self->MM->QuestBlinkT = 0.0;
								IConsoleVariable* CvHalf = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Minimap.LegacyHalfTile"));
								const int32 HalfWas = CvHalf ? CvHalf->GetInt() : 0;
								// Один проход: ключ → перерисовка → по каждому виду (абсолютно t·k, абсолютно «+½»).
								auto Pass = [&](int32 HalfKey, FString& OutLine, bool& bOkEtalon, bool& bOkHalf)
								{
									if (CvHalf) { AsdaCVarSave::SetForTest(TEXT("asda.Minimap.LegacyHalfTile"), HalfKey); }
									Self->MM->QuestBlinkT = 0.0;
									Self->MM->UpdateMap();
									TArray<UImage*> PlQ2;
									Self->MM->CountDrawnMarks(EAsdaMapMark::Player, false, &PlQ2);
									bOkEtalon = PlQ2.Num() == 1; bOkHalf = PlQ2.Num() == 1;
									static const EAsdaMapMark kAll[] = { EAsdaMapMark::Party, EAsdaMapMark::Guild, EAsdaMapMark::Couple,
										EAsdaMapMark::Quest, EAsdaMapMark::Quest2, EAsdaMapMark::Destination };
									for (const EAsdaMapMark Kd : kAll)
									{
										TArray<UImage*> Q;
										Self->MM->CountDrawnMarks(Kd, false, &Q);
										float E1 = 0.f, E2 = 0.f, H1 = 0.f, H2 = 0.f;
										const int32 NE = PlQ2.Num() == 1 ? CountQuadAbs(Q, QuadPos(PlQ2[0]), Exp, KPx, kTolPx, E1, E2) : -1;
										const int32 NH = PlQ2.Num() == 1 ? CountQuadAbs(Q, QuadPos(PlQ2[0]), ExpHalf, KPx, kTolPx, H1, H2) : -1;
										bOkEtalon &= Q.Num() > 0 && NE == Q.Num();
										bOkHalf   &= Q.Num() > 0 && NH == Q.Num() && NE == 0;
										OutLine += FString::Printf(TEXT(" %s:%d квад(t·k %d, +½ %d)"),
											*StaticEnum<EAsdaMapMark>()->GetNameStringByValue((int64)Kd), Q.Num(), NE, NH);
									}
								};
								FString L0, L1;
								bool bE0 = false, bH0 = false, bE1 = false, bH1 = false;
								Pass(0, L0, bE0, bH0);
								Pass(1, L1, bE1, bH1);
								// Восстановление: ключ, списки, точка автобега, мигание; перерисовка.
								if (CvHalf) { AsdaCVarSave::SetForTest(TEXT("asda.Minimap.LegacyHalfTile"), HalfWas); }
								for (const EAsdaMapMark Kd : kOther)
								{
									if (HadList.Contains((uint8)Kd)) { Self->MM->SetMarks(Kd, Saved.FindChecked((uint8)Kd)); }
									else { Self->MM->ClearMarks(Kd); }
								}
								Self->MM->bDestActive = bDestWas; Self->MM->DestTileX = DxWas; Self->MM->DestTileZ = DzWas;
								Self->MM->QuestBlinkT = BlinkWas;
								Self->MM->UpdateMap();
								Self->Expect(CvHalf && bE0,
									FString::Printf(TEXT("M2c мини-карта: отряд/клан/соулмейт/квест/квест2/автобег — формула эталона t·k без +½ (ключ 0):%s"), *L0));
								Self->Expect(CvHalf && bH1 && !bE1,
									FString::Printf(TEXT("M2d КОНТРОЛЬ asda.Minimap.LegacyHalfTile 1: те же метки — «+½ тайла», t·k не совпадает:%s"), *L1));
							}
						}
						else
						{
							Self->Expect(false, FString::Printf(TEXT("M2 мини-карта: квадов зон %d, значков игрока %d — мини-карта не рисует (нет окна?)"), FishQ.Num(), PlQ.Num()));
						}
						Self->MM->ToggleWorldMap();
						Seq(0.4f, [Self, Seq]()
						{
							TArray<UImage*> FishB, PlB;
							Self->MM->CountDrawnMarks(EAsdaMapMark::Fishing, true, &FishB);
							Self->MM->CountDrawnMarks(EAsdaMapMark::Player, true, &PlB);
							if (FishB.Num() > 0 && PlB.Num() == 1)
							{
								const FVector2D P = Self->Ch->GetGamePosXZ();
								TArray<FVector2D> Exp;
								for (const int32 s : Self->Alp)
								{
									const FAsdaFishSpotRow* S = Self->F->GetSpot(s);
									Exp.Add(FVector2D((float)S->X - (float)P.X / 100.f, (float)S->Y - (float)P.Y / 100.f));   // тайл·масштаб
								}
								FString Info;
								const bool bRatio = Self->CheckQuadRatio(FishB, QuadPos(PlB[0]), Exp, Info);
								Self->Expect(bRatio && FishB.Num() == 10,
									FString::Printf(TEXT("M4 большая карта (показана своя): 10 квадов зон, позиции = (тайл − позиция)·масштаб (%s)"), *Info));
							}
							else
							{
								Self->Expect(false, FString::Printf(TEXT("M4 большая карта: квадов зон %d, значков игрока %d"), FishB.Num(), PlB.Num()));
							}
							Self->MM->ToggleWorldMap();
							// N. Ход клипа зоны.
							const UAnimSingleNodeInstance* I = Self->ClipZone.IsValid() ? Self->ClipZone->Mesh->GetSingleNodeInstance() : nullptr;
							const float T1 = I ? I->GetCurrentTime() : -1.f;
							Self->Expect(I && Self->ClipT0 >= 0.f && !FMath::IsNearlyEqual(T1, Self->ClipT0, 0.01f),
								FString::Printf(TEXT("N1 клип зоны идёт во времени: %.3f → %.3f с (длина %.1f с, петля)"), Self->ClipT0, T1,
									I && I->GetAnimationAsset() ? Cast<UAnimSequence>(I->GetAnimationAsset())->GetPlayLength() : -1.f));
							Self->StageExtra();
						});
					});
				});
			});
		});
	});
}

// Этап 3: K3 — фокус на НЕтекстовом виджете игры (вне рыбалки хоткей доходит до вьюпорта, при рыбалке нет); L6 — многострочное
//   поле: вне рыбалки пишет, при рыбалке «только чтение», после — снова пишет. Виджеты — временные, в оверлее вьюпорта.
void FAsdaFishZoneSelfTest::StageExtra()
{
	UWorld* W = World.Get();
	if (!W || !GEngine || !GEngine->GameViewport) { Finish(); return; }
	TSharedRef<FAsdaFishZoneSelfTest> Self = AsShared();
	auto Seq = [W](float Delay, TFunction<void()> Fn)
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), Delay, false);
	};
	SetFs(0);
	TmpOverlay = SNew(SBox).WidthOverride(240.f).HeightOverride(90.f).HAlign(HAlign_Left).VAlign(VAlign_Top)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(TmpButton, SButton).IsFocusable(true).Text(FText::FromString(TEXT("K3"))) ]
		+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).HeightOverride(40.f)[ SAssignNew(TmpMulti, SMultiLineEditableText).Text(FText::FromString(TEXT("ab"))) ] ]
	];
	GEngine->GameViewport->AddViewportWidgetContent(TmpOverlay.ToSharedRef(), 10000);
	Seq(0.2f, [Self, Seq]()
	{
		// K3 вне рыбалки: фокус на кнопке, W — до вьюпорта (всплытие) → зажата.
		FSlateApplication::Get().SetUserFocus(0, Self->TmpButton);
		const bool bBtnFocus = FSlateApplication::Get().GetUserFocusedWidget(0) == Self->TmpButton;
		SlateKey(EKeys::W, true);
		Seq(0.15f, [Self, Seq, bBtnFocus]()
		{
			const bool bHeld0 = Self->PC->IsInputKeyDown(EKeys::W);
			SlateKey(EKeys::W, false);
			Self->SetFs(109);
			FSlateApplication::Get().SetUserFocus(0, Self->TmpButton);
			SlateKey(EKeys::W, true);
			Seq(0.15f, [Self, Seq, bBtnFocus, bHeld0]()
			{
				const bool bHeld1 = Self->PC->IsInputKeyDown(EKeys::W);
				SlateKey(EKeys::W, false);
				Self->SetFs(0);
				Self->PlaceAt(Self->Near);
				Self->Expect(bBtnFocus && bHeld0 && !bHeld1,
					FString::Printf(TEXT("K3 фокус на НЕтекстовом виджете игры (кнопка): вне рыбалки W доходит до вьюпорта — зажата %d; при рыбалке — нет %d (препроцессор вне рыбалки ввод не трогает)"),
						bHeld0 ? 1 : 0, bHeld1 ? 0 : 1));
				// L6 многострочное поле.
				FSlateApplication::Get().SetUserFocus(0, Self->TmpMulti);
				Self->MultiT0 = Self->TmpMulti->GetText().ToString();
				SlateChar(TEXT('c'));
				const FString T1 = Self->TmpMulti->GetText().ToString();
				Self->SetFs(115);
				Seq(0.15f, [Self, Seq, T1]()
				{
					const bool bLocked = Self->Z->IsTextFieldLocked();
					SlateChar(TEXT('d'));
					const FString T2 = Self->TmpMulti->GetText().ToString();
					Self->SetFs(0);
					Seq(0.15f, [Self, Seq, T1, T2, bLocked]()
					{
						const bool bUnlocked = !Self->Z->IsTextFieldLocked() && !Self->TmpMulti->IsTextReadOnly();
						SlateChar(TEXT('e'));
						const FString T3 = Self->TmpMulti->GetText().ToString();
						Self->Expect(T1 != Self->MultiT0 && bLocked && T2 == T1 && bUnlocked && T3 != T2,
							FString::Printf(TEXT("L6 многострочное поле (SMultiLineEditableText): вне рыбалки пишет «%s»→«%s», при рыбалке заперто %d и символ не прошёл «%s», после — отперто и пишет «%s»"),
								*Self->MultiT0, *T1, bLocked ? 1 : 0, *T2, *T3));
						if (GEngine && GEngine->GameViewport && Self->TmpOverlay.IsValid())
						{
							GEngine->GameViewport->RemoveViewportWidgetContent(Self->TmpOverlay.ToSharedRef());
						}
						Self->TmpOverlay.Reset(); Self->TmpButton.Reset(); Self->TmpMulti.Reset();
						FSlateApplication::Get().SetAllUserFocusToGameViewport();
						Self->ObjSpotTry = 0;
						Self->StageObj();
					});
				});
			});
		});
	});
}

// Этап 4: C7 — естественная точка экрана, где луч упирается в ОБЪЕКТ карты (экземпляр ISM) ближе коробки зоны. Персонаж ставится
//   по очереди у спотов Альпии (камера следует за ним), кадр сканируется; найденная точка — двойной клик (логика `OnLeftClick`).
void FAsdaFishZoneSelfTest::StageObj()
{
	UWorld* W = World.Get();
	if (!W) { Finish(); return; }
	TSharedRef<FAsdaFishZoneSelfTest> Self = AsShared();
	if (ObjSpotTry >= Alp.Num())
	{
		Expect(bObjNatural, TEXT("C7б естественная точка «объект карты перед зоной» на экране у одного из 10 спотов Альпии — найдена и проверена"));
		PlaceAt(Near);
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Self]() { Self->StageMouse(); }));
		return;
	}
	const FAsdaFishSpotRow* S = F->GetSpot(Alp[ObjSpotTry]);
	PlaceAt(FVector2D((float)S->X * 100.f + 300.f, (float)S->Y * 100.f));
	FTimerHandle H;
	W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Self]()
	{
		int32 VX = 0, VY = 0; Self->PC->GetViewportSize(VX, VY);
		int32 NIsm = 0;
		for (int32 y = 6; y < VY && !Self->bObjNatural; y += 12)
		{
			for (int32 x = 6; x < VX && !Self->bObjNatural; x += 12)
			{
				Self->PC->DebugClickScreen = FVector2D(x, y);
				const AAsdaPlayerController::FAsdaPick P = Self->PC->PickForClick();
				const UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(P.Component.Get());
				if (P.Kind != AAsdaPlayerController::EAsdaPickKind::Ground || !Ism) { continue; }
				++NIsm;
				FVector O, D; float ZD = 0.f;
				if (!Self->PC->DeprojectScreenPositionToWorld((float)x, (float)y, O, D)) { continue; }
				const int32 Spot = Self->Z->PickZoneByRay(O, D, ZD);
				if (Spot < 0 || !(FVector::Dist(O, P.Point) < ZD)) { continue; }
				// Найдено: луч проходит коробку зоны, но объект карты ближе. Двойной клик → тип 5 не выбран, 109 нет.
				Self->ResetFish();
				const int32 rPick = Self->PC->PickFishZoneForDoubleClick(P);
				Self->PC->OnLeftClick(/*bDouble*/true);
				const int32 Fs = Self->F->GetFishState();
				Self->bObjNatural = true;
				Self->Expect(rPick < 0 && Fs == 0 && Self->PC->GetPawn() != nullptr,
					FString::Printf(TEXT("C7а у спота %d точка (%d,%d): луч в зону %d (%.0f), но ближе объект карты %s/%s (%.0f) — двойной клик: тип 5 не выбран (%d), автомат %d"),
						Self->Alp[Self->ObjSpotTry], x, y, Spot, ZD, Ism->GetOwner() ? *Ism->GetOwner()->GetActorNameOrLabel() : TEXT("-"),
						*Ism->GetName(), FVector::Dist(O, P.Point), rPick, Fs));
				Self->ResetFish();
			}
		}
		Self->PC->DebugClickScreen.Reset();
		UE_LOG(LogAsdaFishZoneTest, Log, TEXT("  C7 спот %d: попаданий луча в экземпляры ISM на экране %d"), Self->Alp[Self->ObjSpotTry], NIsm);
		++Self->ObjSpotTry;
		Self->StageObj();
	}), 0.35f, false);
}

// Этап 5 (ревизия В4 ВАЖН-1): двойной клик НАСТОЯЩИМ путём мыши. Второе нажатие у UE = `IE_Pressed` + `IE_DoubleClick` в одном
//   кадре; одиночное событие на проходимом берегу шлёт свой 4006 (бит 2). Эталон — WM_LBUTTONDBLCLK без нового 4006.
void FAsdaFishZoneSelfTest::StageMouse()
{
	UWorld* W = World.Get();
	if (!W || !Net) { Expect(false, TEXT("R0 нет мира/сети")); Finish(); return; }
	TSharedRef<FAsdaFishZoneSelfTest> Self = AsShared();
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
	PlaceAt(Near);
	// Точка: проходимый берег (земля, не объект карты, WalkGrid проходим, ≥ 3 м от персонажа), луч проходит коробку зоны, зона
	//   побеждает соперника, под точкой Slate — игровой вьюпорт (не окно UI). И точка мимо всех зон (R6).
	{
		int32 VX = 0, VY = 0; PC->GetViewportSize(VX, VY);
		for (int32 y = 8; y < VY; y += 8)
		{
			for (int32 x = 8; x < VX; x += 8)
			{
				if (ShorePx.X >= 0 && MissPx.X >= 0 && WaterPx.X >= 0) { break; }
				PC->DebugClickScreen = FVector2D(x, y);
				const AAsdaPlayerController::FAsdaPick P = PC->PickForClick();
				if (P.Kind != AAsdaPlayerController::EAsdaPickKind::Ground || UAsdaFishZoneSubsystem::IsMapObjectHit(P.Component.Get())) { continue; }
				const FVector2D G = AsdaCoord::UeToGame2D(P.Point);
				const bool bWalk = Ch->WalkGrid && Ch->WalkGrid->IsWorldWalkable((float)G.X, (float)G.Y);
				// ★ 12.10.2026 (W1/W2): суша/вода под точкой — тем же предикатом, что ключ `asda.Fish.ZonePickWaterOnly`.
				const bool bWater = UAsdaFishZoneSubsystem::IsWaterHit(W, P.Component.Get(), P.Point);
				const float Dist = (P.Point - Ch->GetActorLocation()).Size2D();
				FVector O, D; float ZD = 0.f;
				if (!PC->DeprojectScreenPositionToWorld((float)x, (float)y, O, D)) { continue; }
				const int32 Spot = Z->PickZoneByRay(O, D, ZD);
				if (ShorePx.X < 0 && Spot >= 0 && bWalk && !bWater && Dist > 300.f && Dist < 2500.f && PC->PickFishZoneForDoubleClick(P) == Spot)
				{
					ShorePx = FVector2D(x, y); ShoreSpot = Spot;
				}
				if (WaterPx.X < 0 && Spot >= 0 && bWater && Dist > 300.f && Dist < 3000.f && PC->PickFishZoneForDoubleClick(P) == Spot)
				{
					WaterPx = FVector2D(x, y); WaterSpot = Spot;
				}
				if (MissPx.X < 0 && Spot < 0 && bWalk && Dist > 300.f && Dist < 3000.f) { MissPx = FVector2D(x, y); }
			}
		}
		PC->DebugClickScreen.Reset();
	}
	Expect(ShorePx.X >= 0 && MissPx.X >= 0, FString::Printf(TEXT("R0 точки: берег (суша, не вода) в коробке зоны (%.0f,%.0f) → зона %d; мимо зон (%.0f,%.0f); вода в коробке зоны (%.0f,%.0f) → зона %d"),
		ShorePx.X, ShorePx.Y, ShoreSpot, MissPx.X, MissPx.Y, WaterPx.X, WaterPx.Y, WaterSpot));
	if (ShorePx.X < 0) { Finish(); return; }
	// Сеть: «в мире» + узкий перехват 4006 (сокета нет; бит 2 взводится как у настоящей отправки).
	bWasInGame = Net->bInGameConnected;
	Net->bInGameConnected = true;
	CapGen = AsdaSelfTestKit::ArmCapture(Net, [WSelf = TWeakPtr<FAsdaFishZoneSelfTest>(Self)](uint16 Op, const uint8*, int32 Len, bool, TCHAR)
	{
		if (TSharedPtr<FAsdaFishZoneSelfTest> T = WSelf.Pin()) { if (Op == 4006 && Len == 43) { ++T->N4006; } }
	}, (uint16)4006, TEXT("asda.Fish.ZoneSelfTest"));
	AsdaCVarSave::SetForTest(TEXT("asda.Move.EtalonSpeedSync"), 1);

	// Сценарии — список шагов с паузами. Перед ПЕРВЫМ нажатием каждого сценария — пауза 0.35 с: `UPlayerInput` сам делает
	//   двойной из двух `IE_Pressed` ближе `DoubleClickTime` (0.2 с, DefaultInput.ini; PlayerInput.cpp:448), и нажатие нового
	//   сценария не должно склеиться с последним нажатием прежнего.
	struct FStep { float Delay; TFunction<void()> Fn; };
	TSharedRef<TArray<FStep>> Steps = MakeShared<TArray<FStep>>();
	TSharedRef<int32> FsAfterFirst = MakeShared<int32>(-1);
	auto Press = [Self](const FVector2D& Px) { Self->Mouse(Px, 0); Self->Mouse(Px, 1); };          // WM_LBUTTONDOWN + UP
	auto Dbl = [Self](const FVector2D& Px) { Self->Mouse(Px, 2); Self->Mouse(Px, 1); };            // WM_LBUTTONDBLCLK + UP
	auto Answer4007 = [Self]() { Self->Net->ClearMoveRequestInFlight(TEXT("самотест зон: ответ 4007 на 4006 первого нажатия")); };
	// Двойной клик: первое нажатие → (ответ 4007?) → второе нажатие двойного через 0.3 с (> троттла 150 мс).
	auto AddDouble = [Steps, Press, Dbl, Answer4007, FsAfterFirst, Self](const FVector2D& Px, bool bAnswer)
	{
		Steps->Add({ 0.35f, [Self, Press, Px]() { Self->MousePrep(); Self->PC->DebugClickScreen = Px; Self->N4006 = 0; Press(Px); } });
		Steps->Add({ 0.10f, [Self, Answer4007, FsAfterFirst, bAnswer]() { *FsAfterFirst = Self->F->GetFishState(); if (bAnswer) { Answer4007(); } } });
		Steps->Add({ 0.20f, [Dbl, Px]() { Dbl(Px); } });
	};
	// R1 — контроль: ОДИНОЧНЫЙ клик по берегу. 4006 ушёл, бит 2 взведён, зона не выбрана (Input_WorldClickHandler, a6 = 0).
	Steps->Add({ 0.35f, [Self, Press]() { Self->MousePrep(); Self->N4006 = 0; Press(Self->ShorePx); } });
	Steps->Add({ 0.12f, [Self]()
	{
		const bool bR1 = Self->N4006 == 1 && Self->Net->IsMoveRequestInFlight() && Self->F->GetFishState() == 0;
		Self->Expect(bR1, FString::Printf(TEXT("R1 контроль: одиночный клик настоящим путём мыши (%s) по берегу в коробке зоны: 4006 ушёл (%d), бит 2 взведён, зона не выбрана (автомат %d)"),
			*Self->MouseRoute, Self->N4006, Self->F->GetFishState()));
	} });
	// R2 — ДВОЙНОЙ: первое нажатие → 4006 → ответ 4007 пришёл (бит 2 снят) → второе нажатие (IE_Pressed + IE_DoubleClick).
	// ★ 12.10.2026: умолчание `asda.Fish.ZonePickWaterOnly` теперь 1 (решение владельца) — R2 проверяет ПОВЕДЕНИЕ ОРИГИНАЛА
	//   (с суши в коробке зоны → 109), поэтому ключ на время R-блока явно 0; W1/W2 ниже ставят 1 и возвращают сами.
	Steps->Add({ 0.05f, []() { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ZonePickWaterOnly"), 0); } });
	AddDouble(ShorePx, true);
	Steps->Add({ 0.12f, [Self, FsAfterFirst]()
	{
		// ★ В45fix2 (ревизия В4/В5, МЕЛК-2): `N == 2` — это [ПОРТ], а не эталон. У оригинала второе нажатие — WM_LBUTTONDBLCLK
		//   ВМЕСТО WM_LBUTTONDOWN (g:23838-23847): второго 4006 нет, сервер видит 4006 → (4007) → 109. У порта второе нажатие —
		//   `IE_Pressed` + `IE_DoubleClick`: `IE_Pressed` шлёт лишний 4006, и сервер видит 4006, 4007, 4006, 109 (без 4007 между
		//   последними двумя). Ключ `asda.Fish.ZoneDblClickOwnMove 1` лишний 4006 НЕ убирает — он лишь не даёт двойному заглохнуть
		//   на бите 2 этого 4006, т.е. эталонным остаётся только итог (109 по зоне). Общий разбор привязок — `SetupInputComponent`.
		//   ★ 09.10.2026 (п. 4 владельца): лишний 4006 убран — второе нажатие одиночный разбор не исполняет (`OnLeftPressedInput`),
		//   теперь `N == 1`, как у эталона. Прежний порт (`N == 2`) — контроль R4a ключом `asda.Input.DblClickLegacySingle 1`.
		const int32 Fs = Self->F->GetFishState(), Pk = Self->F->GetPickedSpot(), N = Self->N4006;
		Self->Expect(*FsAfterFirst == 0 && Fs == 109 && Pk == Self->ShoreSpot && N == 1,
			FString::Printf(TEXT("R2 ДВОЙНОЙ клик настоящим путём мыши по проходимому берегу: после первого нажатия автомат %d; тип 5 → 109 (автомат %d, зона %d, ждём %d) — итог как WM_LBUTTONDBLCLK эталона; 4006 всего %d (эталон 1): второе нажатие пакета движения не шлёт"),
				*FsAfterFirst, Fs, Pk, Self->ShoreSpot, N));
	} });
	// R3 — контроль эталона: ответа 4007 на ПЕРВОЕ нажатие ещё нет (бит 2 от него) → у оригинала тип 5 глохнет (g:30224).
	AddDouble(ShorePx, false);
	Steps->Add({ 0.12f, [Self]()
	{
		const int32 Fs3 = Self->F->GetFishState(), N3 = Self->N4006;
		Self->Expect(Fs3 == 0 && N3 == 1,
			FString::Printf(TEXT("R3 контроль: 4007 на 4006 ПЕРВОГО нажатия ещё не пришёл — второе нажатие 4006 не шлёт (всего %d), двойной глохнет на бите 2 (автомат %d), как эталон"), N3, Fs3));
		AsdaCVarSave::SetForTest(TEXT("asda.Fish.ZoneDblClickOwnMove"), 0);
		// ★ 09.10.2026: ключ ZoneDblClickOwnMove касается только прежнего порта (одиночный разбор второго нажатия) — R4/R4a под ним.
		AsdaCVarSave::SetForTest(TEXT("asda.Input.DblClickLegacySingle"), 1);
	} });
	// R4 — контроль ключа: `asda.Fish.ZoneDblClickOwnMove 0` — прежний порт (двойной глох на бите 2 своего же 4006).
	AddDouble(ShorePx, true);
	Steps->Add({ 0.12f, [Self]()
	{
		IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.ZoneDblClickOwnMove"));
		const int32 Fs4 = Self->F->GetFishState(), N4 = Self->N4006;
		if (Key) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ZoneDblClickOwnMove"), 1); }
		Self->Expect(Key && Fs4 == 0 && N4 == 2, FString::Printf(TEXT("R4 контроль ключа (asda.Input.DblClickLegacySingle 1): asda.Fish.ZoneDblClickOwnMove 0 — прежний порт, двойной глохнет на бите 2 своего же 4006 (4006 %d, автомат %d)"), N4, Fs4));
	} });
	// R4a — контроль отката п. 4: `asda.Input.DblClickLegacySingle 1` (ZoneDblClickOwnMove 1) — прежний порт: 109 по зоне, но лишний
	//   4006 второго нажатия (всего 2).
	AddDouble(ShorePx, true);
	Steps->Add({ 0.12f, [Self]()
	{
		IConsoleVariable* Lg = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Input.DblClickLegacySingle"));
		const int32 Fs4a = Self->F->GetFishState(), N4a = Self->N4006;
		if (Lg) { AsdaCVarSave::SetForTest(TEXT("asda.Input.DblClickLegacySingle"), 0); }
		Self->Expect(Lg && Fs4a == 109 && N4a == 2, FString::Printf(TEXT("R4a контроль отката: asda.Input.DblClickLegacySingle 1 — прежний порт, 109 по зоне (автомат %d), но 4006 всего %d (лишний от одиночного второго нажатия)"), Fs4a, N4a));
		Self->SetFs(0);
	} });
	// R5 — одиночный клик тем же путём после R2–R4 (состояние не протекло): зоны нет.
	Steps->Add({ 0.35f, [Self, Press]() { Self->MousePrep(); Self->N4006 = 0; Press(Self->ShorePx); } });
	Steps->Add({ 0.12f, [Self]()
	{
		const int32 Fs5 = Self->F->GetFishState();
		Self->Expect(Fs5 == 0 && Self->N4006 == 1, FString::Printf(TEXT("R5 контроль: одиночный клик по той же точке — 109 нет (автомат %d, 4006 %d)"), Fs5, Self->N4006));
	} });
	// R6 — двойной клик настоящим путём мимо всех зон: 109 нет.
	AddDouble(MissPx, true);
	Steps->Add({ 0.12f, [Self]()
	{
		const int32 Fs6 = Self->F->GetFishState();
		Self->Expect(Self->MissPx.X >= 0 && Fs6 == 0 && Self->N4006 == 1,
			FString::Printf(TEXT("R6 двойной клик настоящим путём по земле мимо всех зон (%.0f,%.0f) — 109 нет (автомат %d); ход только от первого нажатия: 4006 всего %d (ждём 1, п. 4 владельца)"), Self->MissPx.X, Self->MissPx.Y, Fs6, Self->N4006));
		AsdaCVarSave::SetForTest(TEXT("asda.Input.DblClickLegacySingle"), 1);
	} });
	// R6a — контроль: тот же двойной по земле при `asda.Input.DblClickLegacySingle 1` — прежний порт, второе нажатие шлёт свой 4006.
	AddDouble(MissPx, true);
	Steps->Add({ 0.12f, [Self]()
	{
		IConsoleVariable* Lg = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Input.DblClickLegacySingle"));
		const int32 N6a = Self->N4006;
		if (Lg) { AsdaCVarSave::SetForTest(TEXT("asda.Input.DblClickLegacySingle"), 0); }
		Self->Expect(Lg && N6a == 2, FString::Printf(TEXT("R6a контроль отката: asda.Input.DblClickLegacySingle 1 — двойной по земле шлёт 4006 всего %d (прежний порт: лишний от второго нажатия)"), N6a));
	} });
	// ★ W1/W2 (12.10.2026, разбор жалобы «рыбалка по клику на суше»; `owner_1012/fishzone/fishzone_fix.md`). Эталонное поведение
	//   (ключ 0) — R2: двойной по СУШЕ в коробке зоны → 109. Ключ `asda.Fish.ZonePickWaterOnly 1` [ПОРТ]: та же точка суши →
	//   109 нет (ход от первого нажатия остаётся: 4006 = 1); точка НА ВОДЕ в коробке зоны → 109 и та же зона.
	Steps->Add({ 0.05f, []() { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ZonePickWaterOnly"), 1); } });
	AddDouble(ShorePx, true);
	Steps->Add({ 0.12f, [Self]()
	{
		const int32 FsW1 = Self->F->GetFishState(), NW1 = Self->N4006;
		Self->Expect(FsW1 == 0 && NW1 == 1,
			FString::Printf(TEXT("W1 [ПОРТ] asda.Fish.ZonePickWaterOnly 1: двойной клик по СУШЕ в коробке зоны (%.0f,%.0f) — 109 нет (автомат %d), ход первого нажатия жив (4006 %d, ждём 1); при ключе 0 (R2) эта же точка давала 109"),
				Self->ShorePx.X, Self->ShorePx.Y, FsW1, NW1));
	} });
	if (WaterPx.X >= 0)
	{
		AddDouble(WaterPx, true);
		Steps->Add({ 0.12f, [Self]()
		{
			const int32 FsW2 = Self->F->GetFishState(), PkW2 = Self->F->GetPickedSpot();
			AsdaCVarSave::SetForTest(TEXT("asda.Fish.ZonePickWaterOnly"), 0);
			Self->Expect(FsW2 == 109 && PkW2 == Self->WaterSpot,
				FString::Printf(TEXT("W2 [ПОРТ] asda.Fish.ZonePickWaterOnly 1: двойной клик по ВОДЕ в коробке зоны (%.0f,%.0f) → тип 5 → 109 (автомат %d, зона %d, ждём %d)"),
					Self->WaterPx.X, Self->WaterPx.Y, FsW2, PkW2, Self->WaterSpot));
			Self->SetFs(0);
		} });
	}
	else
	{
		Steps->Add({ 0.05f, [Self]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Fish.ZonePickWaterOnly"), 0);
			Self->Expect(false, TEXT("W2 не найдена экранная точка на воде внутри коробки зоны (IsWaterHit) — проверка ключа по воде не выполнена"));
		} });
	}
	// R7/R8 — ключ `asda.Fish.LegacyDeferredClick` (ревизия В4 МЕЛК-6). 1 = эталон: клик во время рыбалки не теряется (бит
	//   `g_WorldInputFlags` пережил гейт), исполняется в первом кадре после её конца → 4006. 0 (порт) — отброшен.
	for (const int32 KeyV : { 1, 0 })
	{
		Steps->Add({ 0.35f, [Self, Press, KeyV]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyDeferredClick"), KeyV);
			Self->MousePrep(); Self->N4006 = 0; Self->SetFs(115); Press(Self->ShorePx);
		} });
		Steps->Add({ 0.12f, [Self, FsAfterFirst]() { *FsAfterFirst = Self->N4006; Self->SetFs(0); } });
		Steps->Add({ 0.12f, [Self, FsAfterFirst, KeyV]()
		{
			const int32 NAfter = Self->N4006;
			AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyDeferredClick"), 0);
			const bool bOk = (*FsAfterFirst == 0) && (KeyV == 1 ? NAfter == 1 : NAfter == 0);
			Self->Expect(bOk, FString::Printf(TEXT("%s asda.Fish.LegacyDeferredClick %d: клик по берегу во время рыбалки — 4006 при рыбалке %d, после её конца %d (%s)"),
				KeyV == 1 ? TEXT("R7") : TEXT("R8 контроль:"), KeyV, *FsAfterFirst, NAfter,
				KeyV == 1 ? TEXT("эталон: клик исполнен в первом кадре после рыбалки") : TEXT("порт: клик отброшен")));
		} });
	}
	// K4/K5 — ключ `asda.Fish.EatSysKeys` (ревизия В4 МЕЛК-6): 0 = эталон — Alt (WM_SYSKEYDOWN) при рыбалке доходит до
	//   `UPlayerInput`; 1 (порт) — съеден.
	for (const int32 KeyV : { 0, 1 })
	{
		Steps->Add({ 0.35f, [Self, KeyV]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Fish.EatSysKeys"), KeyV);
			Self->MousePrep(); Self->SetFs(115);
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			SlateKey(EKeys::LeftAlt, true);
		} });
		Steps->Add({ 0.12f, [Self, KeyV]()
		{
			const bool bHeld = Self->PC->IsInputKeyDown(EKeys::LeftAlt);
			SlateKey(EKeys::LeftAlt, false);
			Self->SetFs(0);
			AsdaCVarSave::SetForTest(TEXT("asda.Fish.EatSysKeys"), 1);
			Self->Expect(KeyV == 0 ? bHeld : !bHeld, FString::Printf(TEXT("%s asda.Fish.EatSysKeys %d: Alt при рыбалке в UPlayerInput — зажат %d (%s)"),
				KeyV == 0 ? TEXT("K4") : TEXT("K5 контроль:"), KeyV, bHeld ? 1 : 0,
				KeyV == 0 ? TEXT("эталон: WM_SYSKEYDOWN мимо гейта fishState") : TEXT("порт: съеден")));
		} });
	}
	// ── DN/DM (В45fix2, ревизия В4/В5, МЕЛК-7): ДВОЙНОЙ КЛИК ПО NPC И ПО МОБУ настоящим путём мыши (`FSlateApplication`) —
	//   регресс-пары к исключению ВАЖН-1 (`SinglePressMoveFrame`): NPC — окно разговора (в зоне) и подбегание (вне зоны) при
	//   `asda.Npc.OpenOnSingleClick 0`; при ключе 1 одиночный клик САМ шлёт 4006 и ставит метку, двойной того же нажатия идёт
	//   «только тип 5» → зоны ближе NPC на луче нет (точка выбрана так) → выход, подбегание одиночного живо, 109 нет. Моб — второй
	//   клик по выбранной цели = боевой режим. Сущности приходят ШТАТНЫМИ пакетами: NPC — 4067 (adoption запечённого), моб —
	//   4013 (динамический спаун); перехват — весь трафик (бою/подходу могут понадобиться 4008/4026), считается 4006.
	auto DcPress = [Self, Press]() { Self->PC->DebugClickScreen = Self->DcPx; Press(Self->DcPx); };
	auto DcDbl = [Self, Dbl]() { Self->PC->DebugClickScreen = Self->DcPx; Dbl(Self->DcPx); };
	Steps->Add({ 0.35f, [Self]()
	{
		// Перехват — на весь трафик (своё поколение), счётчик — по-прежнему 4006 длиной 43.
		AsdaSelfTestKit::DisarmCapture(Self->Net, Self->CapGen);
		Self->CapGen = AsdaSelfTestKit::ArmCapture(Self->Net, [WSelf = TWeakPtr<FAsdaFishZoneSelfTest>(Self)](uint16 Op, const uint8*, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FAsdaFishZoneSelfTest> T = WSelf.Pin()) { if (Op == 4006 && Len == 43) { ++T->N4006; } }
		}, (uint16)0, TEXT("asda.Fish.ZoneSelfTest"));
		if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Npc.OpenOnSingleClick"))) { Self->NpcOpenSingleWas = K->GetInt(); }
		// Запечённый NPC Альпии (шаблон, не принятый сервером), ближайший к точке у спота 0 — принимается пакетом 4067.
		UWorld* Wd = Self->World.Get();
		AAsdaNpcActor* Best = nullptr; double BestD = 1e30;
		const FVector2D NearUe = AsdaCoord::GameToUe2D(Self->Near);
		for (TActorIterator<AAsdaNpcActor> It(Wd); It; ++It)
		{
			if (It->bNetSpawned || It->bDisabled || It->NpcId <= 0) { continue; }
			const double D = FVector2D::DistSquared(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y), NearUe);
			if (D < BestD) { BestD = D; Best = *It; }
		}
		Self->DcNpc = Best;
		if (Best)
		{
			TArray<uint8> B; B.SetNumZeroed(108);
			AsdaBytes::WrI16(B.GetData() + 0, (int16)kDcNpcRid);
			AsdaBytes::WrI16(B.GetData() + 2, (int16)Best->NpcId);
			AsdaBytes::WrI16(B.GetData() + 8, (int16)Best->TileX);
			AsdaBytes::WrI16(B.GetData() + 10, (int16)Best->TileZ);
			Self->Net->DebugFeedPacket(4067, B);
		}
	} });
	Steps->Add({ 0.30f, [Self]()
	{
		AAsdaNpcActor* Npc = Self->DcNpc.Get();
		Self->Expect(Npc && Npc->bNetSpawned && Npc->NetRenderId == kDcNpcRid && !Npc->IsHidden(),
			FString::Printf(TEXT("DN0 NPC %d (%s) принят пакетом 4067 — показан, renderId %d"), Npc ? Npc->NpcId : -1, Npc ? *Npc->GetName() : TEXT("НЕТ"), Npc ? Npc->NetRenderId : -1));
		Self->DcPrep();
		if (Npc) { Self->PlaceAt(Self->BehindAlongView(Npc, 110.f)); }   // в зоне разговора (±150), NPC перед камерой
	} });
	// DN1 — в зоне разговора, ключ 0: одиночный — только выделение; двойной — окно разговора открылось, хода нет.
	Steps->Add({ 0.35f, [Self, DcPress]()
	{
		SetNpcOpenSingle(0);
		Self->DcPx = FVector2D(-1, -1);
		const bool bPx = Self->FindPickPx(Self->DcNpc.Get(), AAsdaPlayerController::EAsdaPickKind::Npc, Self->DcPx, /*bNoZoneInFront*/true);
		UE_LOG(LogAsdaFishZoneTest, Log, TEXT("  DN1 точка NPC на экране (%.0f,%.0f) найдена %d, в зоне разговора %d"), Self->DcPx.X, Self->DcPx.Y, bPx ? 1 : 0,
			Self->PC->IsInsideNpcTalkZone(Self->DcNpc.Get()) ? 1 : 0);
		if (bPx) { DcPress(); }
	} });
	Steps->Add({ 0.10f, [Self]()
	{
		const UAsdaNpcDialogSubsystem* Dlg = Self->GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
		Self->bDcFlag1 = Dlg && Dlg->IsDialogOpen();
		Self->DcN1 = (Self->PC->TargetActor.Get() == Self->DcNpc.Get()) ? 1 : 0;   // выделение одиночным
	} });
	Steps->Add({ 0.20f, [Self, DcDbl]() { if (Self->DcPx.X >= 0) { DcDbl(); } } });
	Steps->Add({ 0.15f, [Self]()
	{
		UAsdaNpcDialogSubsystem* Dlg = Self->GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
		const bool bOpen = Dlg && Dlg->IsDialogOpen();
		const bool bTgt = Self->PC->TargetActor.Get() == Self->DcNpc.Get() && Self->DcNpc.IsValid();
		Self->Expect(Self->DcPx.X >= 0 && Self->DcN1 == 1 && !Self->bDcFlag1 && bOpen && bTgt && Self->N4006 == 0 && Self->F->GetFishState() == 0,
			FString::Printf(TEXT("DN1 ДВОЙНОЙ по NPC настоящим путём мыши (%s), в зоне разговора, OpenOnSingleClick 0: первое нажатие — только выделение (цель %d, окно %d); двойной — окно разговора открыто %d, цель NPC %d, 4006 %d, автомат %d"),
				*Self->MouseRoute, Self->DcN1, Self->bDcFlag1 ? 1 : 0, bOpen ? 1 : 0, bTgt ? 1 : 0, Self->N4006, Self->F->GetFishState()));
		Self->PC->DebugClickScreen.Reset();
		Self->DcPrep();
		// Точка вне зоны разговора, перед NPC по взгляду камеры, проходимая (подбегание строит маршрут).
		Self->DcFarG = FVector2D::ZeroVector;
		if (AAsdaNpcActor* Npc = Self->DcNpc.Get())
		{
			for (float D = 500.f; D <= 1200.f && Self->DcFarG.IsZero(); D += 100.f)
			{
				const FVector2D G = Self->BehindAlongView(Npc, D);
				if (Self->Ch->WalkGrid && Self->Ch->WalkGrid->IsWorldWalkable((float)G.X, (float)G.Y)) { Self->DcFarG = G; }
			}
			if (Self->DcFarG.IsZero()) { Self->DcFarG = Self->BehindAlongView(Npc, 600.f); }
			Self->PlaceAt(Self->DcFarG);
		}
	} });
	// DN2 — вне зоны, ключ 0: двойной — подбегание (4006, отложенный разговор); одиночный перед ним — только выделение.
	Steps->Add({ 0.35f, [Self, DcPress]()
	{
		Self->DcPx = FVector2D(-1, -1);
		const bool bPx = Self->FindPickPx(Self->DcNpc.Get(), AAsdaPlayerController::EAsdaPickKind::Npc, Self->DcPx, /*bNoZoneInFront*/true);
		UE_LOG(LogAsdaFishZoneTest, Log, TEXT("  DN2 точка NPC (%.0f,%.0f) найдена %d, в зоне разговора %d"), Self->DcPx.X, Self->DcPx.Y, bPx ? 1 : 0,
			Self->PC->IsInsideNpcTalkZone(Self->DcNpc.Get()) ? 1 : 0);
		if (bPx) { DcPress(); }
	} });
	Steps->Add({ 0.10f, [Self]() { Self->bDcFlag1 = Self->PC->bNpcTalkPending; Self->DcN1 = Self->N4006; } });
	Steps->Add({ 0.20f, [Self, DcDbl]() { if (Self->DcPx.X >= 0) { DcDbl(); } } });
	Steps->Add({ 0.15f, [Self]()
	{
		const bool bPend = Self->PC->bNpcTalkPending && Self->PC->GetPendingNpcActor() == Self->DcNpc.Get() && Self->DcNpc.IsValid();
		Self->Expect(Self->DcPx.X >= 0 && !Self->bDcFlag1 && Self->DcN1 == 0 && bPend && Self->N4006 == 1 && Self->F->GetFishState() == 0,
			FString::Printf(TEXT("DN2 ДВОЙНОЙ по NPC вне зоны разговора, OpenOnSingleClick 0: первое нажатие — без хода (4006 %d, подбегание %d); двойной — подбегание к NPC %d, 4006 всего %d, автомат %d"),
				Self->DcN1, Self->bDcFlag1 ? 1 : 0, bPend ? 1 : 0, Self->N4006, Self->F->GetFishState()));
		Self->PC->DebugClickScreen.Reset();
		Self->DcPrep();
		Self->PlaceAt(Self->DcFarG);
	} });
	// DN3 — вне зоны, ключ 1 (эталон: окно/подбегание с первого клика): одиночный шлёт 4006 и ставит метку; 4007 пришёл; второе
	//   нажатие — одиночный снова шлёт 4006 и ставит метку ЭТОГО кадра, двойной идёт «только тип 5» → зоны нет → выход.
	Steps->Add({ 0.35f, [Self, DcPress]()
	{
		SetNpcOpenSingle(1);
		Self->DcPx = FVector2D(-1, -1);
		const bool bPx = Self->FindPickPx(Self->DcNpc.Get(), AAsdaPlayerController::EAsdaPickKind::Npc, Self->DcPx, /*bNoZoneInFront*/true);
		if (bPx) { DcPress(); }
	} });
	Steps->Add({ 0.10f, [Self, Answer4007]()
	{
		Self->DcN1 = Self->N4006; Self->DcMark1 = Self->PC->SinglePressMoveFrame;
		Self->bDcFlag1 = Self->PC->bNpcTalkPending && Self->PC->GetPendingNpcActor() == Self->DcNpc.Get();
		Answer4007();
	} });
	Steps->Add({ 0.20f, [Self, DcDbl]() { if (Self->DcPx.X >= 0) { DcDbl(); } } });
	Steps->Add({ 0.15f, [Self]()
	{
		const bool bPend = Self->PC->bNpcTalkPending && Self->PC->GetPendingNpcActor() == Self->DcNpc.Get() && Self->DcNpc.IsValid();
		const bool bMark2 = Self->PC->SinglePressMoveFrame != MAX_uint64 && Self->PC->SinglePressMoveFrame != Self->DcMark1;
		const UAsdaNpcDialogSubsystem* Dlg = Self->GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
		const bool bOpen = Dlg && Dlg->IsDialogOpen();
		Self->Expect(Self->DcPx.X >= 0 && Self->DcN1 == 1 && Self->bDcFlag1 && Self->DcMark1 != MAX_uint64 && bPend && !bOpen && Self->F->GetFishState() == 0,
			FString::Printf(TEXT("DN3 ДВОЙНОЙ по NPC вне зоны, OpenOnSingleClick 1: одиночный первого нажатия сам послал 4006 (%d) и поставил метку, подбегание %d; после двойного подбегание к NPC живо %d, окна нет %d, 109 нет (автомат %d), 4006 всего %d"),
				Self->DcN1, Self->bDcFlag1 ? 1 : 0, bPend ? 1 : 0, bOpen ? 0 : 1, Self->F->GetFishState(), Self->N4006));
		// ★ 09.10.2026 (п. 4 владельца): одиночный второго нажатия больше не исполняется (метки своего кадра нет); второй 4006 —
		//   от ветки NPC ДВОЙНОГО (`world_OnLeftClickPick` эталона: ветка NPC побайтово та же, что у одиночного, — подбегание).
		Self->Expect(!bMark2 && Self->N4006 == 2,
			FString::Printf(TEXT("DN3a второе нажатие: одиночный разбор пропущен (метка своего кадра %d, ждём 0), подбегание — веткой NPC двойного, как world_OnLeftClickPick эталона (4006 всего %d)"),
				bMark2 ? 1 : 0, Self->N4006));
		SetNpcOpenSingle(Self->NpcOpenSingleWas);
		Self->PC->DebugClickScreen.Reset();
		Self->DcPrep();
		Self->PlaceAt(Self->DcFarG);   // моб — у NPC (суша), не у воды: зона на луче к мобу по праву эталона перебила бы его
	} });
	// DM — моб: создан пакетом 4013 (тип — как у запечённого шаблона Альпии) в 4 клетках перед камерой.
	Steps->Add({ 0.35f, [Self]()
	{
		UWorld* Wd = Self->World.Get();
		const AAsdaMobActor* Tmpl = nullptr; double BestD = 1e30;
		const FVector PlUe = Self->Ch->GetActorLocation();
		for (TActorIterator<AAsdaMobActor> It(Wd); It; ++It)
		{
			if (It->bNetSpawned || It->MobId <= 0) { continue; }
			const double D = FVector::DistSquared2D(It->GetActorLocation(), PlUe);
			if (D < BestD) { BestD = D; Tmpl = *It; }
		}
		FVector Fwd = Self->PC->PlayerCameraManager ? Self->PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector(1, 0, 0);
		Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
		const FVector2D G = AsdaCoord::UeToGame2D(PlUe + Fwd * 450.f);
		const int32 Tx = FMath::FloorToInt(G.X / 100.f), Tz = FMath::FloorToInt(G.Y / 100.f);
		UE_LOG(LogAsdaFishZoneTest, Log, TEXT("  DM0 шаблон моба %s (тип %d), клетка (%d,%d)"), Tmpl ? *Tmpl->GetName() : TEXT("НЕТ"), Tmpl ? Tmpl->MobId : -1, Tx, Tz);
		if (!Tmpl) { return; }
		// ★ Дедуп №2 В1-C (F18): раскладка — общая фикстура; значения стенда прежние (состояние 0, знаменатель скорости 100 = 1.0).
		const TArray<uint8> B = AsdaTestPackets::MobSpawn4013(kDcMobRid, Tmpl->MobId, FIntPoint(Tx, Tz), /*State*/0, /*Speed*/100);
		Self->Net->DebugFeedPacket(4013, B);
	} });
	Steps->Add({ 0.60f, [Self, DcPress]()
	{
		for (TActorIterator<AAsdaMobActor> It(Self->World.Get()); It; ++It)
		{
			if (It->bNetSpawned && It->NetRenderId == kDcMobRid) { Self->DcMob = *It; break; }
		}
		Self->DcPrep();
		Self->DcPx = FVector2D(-1, -1);
		const bool bPx = Self->FindPickPx(Self->DcMob.Get(), AAsdaPlayerController::EAsdaPickKind::Mob, Self->DcPx, /*bNoZoneInFront*/true);
		Self->Expect(Self->DcMob.IsValid() && bPx, FString::Printf(TEXT("DM0 моб создан пакетом 4013 (renderId %d) и виден пикингу в (%.0f,%.0f), коробки зоны рыбалки ближе на луче нет"), kDcMobRid, Self->DcPx.X, Self->DcPx.Y));
		if (bPx) { DcPress(); }
	} });
	Steps->Add({ 0.10f, [Self]()
	{
		Self->DcN1 = (Self->PC->TargetActor.Get() == Self->DcMob.Get() && Self->DcMob.IsValid()) ? 1 : 0;
		Self->bDcFlag1 = Self->PC->bAutoAttack;
	} });
	Steps->Add({ 0.20f, [Self, DcDbl]() { if (Self->DcPx.X >= 0) { DcDbl(); } } });
	Steps->Add({ 0.12f, [Self]()
	{
		const bool bTgt = Self->PC->TargetActor.Get() == Self->DcMob.Get() && Self->DcMob.IsValid();
		const bool bFight = Self->PC->bAutoAttack;
		Self->Expect(Self->DcPx.X >= 0 && Self->DcN1 == 1 && !Self->bDcFlag1 && bTgt && bFight && Self->F->GetFishState() == 0,
			FString::Printf(TEXT("DM1 ДВОЙНОЙ по мобу настоящим путём мыши: первое нажатие — выбор цели %d без боя %d; второе — боевой режим %d, цель — моб %d, автомат рыбалки %d"),
				Self->DcN1, Self->bDcFlag1 ? 0 : 1, bFight ? 1 : 0, bTgt ? 1 : 0, Self->F->GetFishState()));
		Self->PC->DebugClickScreen.Reset();
		Self->PC->bAutoAttack = false; Self->PC->bCombatStarted = false; Self->PC->bWaitingCombatAck = false;
		Self->PC->ExitCombatStance();
		if (Self->Ch) { Self->Ch->StopAttackAnim(); }
		Self->DcPrep();
		if (AAsdaMobActor* M = Self->DcMob.Get()) { M->Destroy(); }
		if (AAsdaNpcActor* Npc = Self->DcNpc.Get())   // NPC — обратно в шаблон (как до приёма 4067)
		{
			Npc->bNetSpawned = false; Npc->NetRenderId = -1;
			Npc->SetActorHiddenInGame(true); Npc->SetActorEnableCollision(false);
		}
		Self->PlaceAt(Self->Near);
	} });
	// Прогон списка.
	TSharedRef<TFunction<void(int32)>> Run = MakeShared<TFunction<void(int32)>>();
	TWeakPtr<TFunction<void(int32)>> RunW = Run;
	*Run = [Self, Steps, RunW, W](int32 i)
	{
		if (i >= Steps->Num()) { Self->Finish(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Self, Steps, RunW, i]()
		{
			// ★ 11.10.2026 (В3 S1, загрязнение пакета): самотест взводит `bInGameConnected` без сокета, а ЧУЖИЕ периодические
			//   отправители (например 11004 элитного ящика) в узкую фазу перехвата шли мимо него и копили `SendFailStreak` —
			//   на третьей неудаче `TrackSend` объявлял «соединение потеряно» и сбрасывал сессию для всех следующих самотестов
			//   пакета (`wave2_A.md` §2). Счётчик неудач обнуляется перед каждым шагом; сами проверки его не используют.
			if (Self->Net) { Self->Net->SendFailStreak = 0; }
			(*Steps)[i].Fn();
			if (TSharedPtr<TFunction<void(int32)>> R = RunW.Pin()) { (*R)(i + 1); }
		}), (*Steps)[i].Delay, false);
	};
	RunHolder = Run;   // держит цепочку живой до конца
	(*Run)(0);
}

void FAsdaFishZoneSelfTest::Restore()
{
	if (!F) { AsdaCVarSave::RestoreAll(TEXT("asda.Fish.ZoneSelfTest")); return; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
	if (Net)
	{
		if (CapGen != 0) { AsdaSelfTestKit::DisarmCapture(Net, CapGen); CapGen = 0; Net->bInGameConnected = bWasInGame; }
		Net->ClearMoveRequestInFlight(TEXT("самотест зон: восстановление"));
	}
	if (GEngine && GEngine->GameViewport && TmpOverlay.IsValid()) { GEngine->GameViewport->RemoveViewportWidgetContent(TmpOverlay.ToSharedRef()); }
	TmpOverlay.Reset(); TmpButton.Reset(); TmpMulti.Reset();
	if (PC) { PC->CancelRoute(); PC->bMovingToPoint = false; PC->SinglePressMoveFrame = MAX_uint64; }
	F->bTestNoNetDrain = false;
	ResetFish();
	if (Win) { Win->Guide.State = 0; Win->Guide.Recs.Reset(); }
	if (Hot) { Hot->SetSlotContent(kHotSlot, SlotWas.Type, SlotWas.SubType, SlotWas.Index, SlotWas.Count, SlotWas.Id); }
	if (LM) { LM->CurrentMapId = MapWas; }
	if (Ch) { Ch->StopMove(); Ch->NetSnap(PosWas); }
	if (PC) { PC->DebugClickScreen.Reset(); PC->bAutoRunActive = false; }
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): все ключи прогона (Npc.OpenOnSingleClick, Move.EtalonSpeedSync, Fish.KeyGuard,
	//   Minimap.LegacyHalfTile, Fish.ZoneDblClickOwnMove, Input.DblClickLegacySingle, Fish.LegacyDeferredClick, Fish.EatSysKeys) —
	//   к исходному значению И приоритету SetBy. Прежде возвращался только OpenOnSingleClick (с SetByConsole; при раннем
	//   выходе — нулём), EtalonSpeedSync оставался 1, остальные — константами.
	AsdaCVarSave::RestoreAll(TEXT("asda.Fish.ZoneSelfTest"));
	if (AAsdaMobActor* M = DcMob.Get()) { M->Destroy(); }
}

void FAsdaFishZoneSelfTest::Finish()
{
	if (bUiHiddenForCursor && PC)
	{
		if (UAsdaUiVisibilitySubsystem* Uv = PC->GetGameInstance() ? PC->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr) { Uv->SetUiShown(true, TEXT("самотест зон рыбалки: возврат")); }
		bUiHiddenForCursor = false;
	}
	Restore();
	// Цепочка шагов R1…R6 держит самотест (цикл ссылок) — разорвать на следующем кадре (сейчас может исполняться её же шаг).
	if (RunHolder.IsValid() && World.IsValid())
	{
		TSharedRef<FAsdaFishZoneSelfTest> Me = AsShared();
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Me]() { Me->RunHolder.Reset(); }));
	}
	UE_LOG(LogAsdaFishZoneTest, Log, TEXT("\n%s"), Z ? *Z->DumpZones() : TEXT("-"));
	UE_LOG(LogAsdaFishZoneTest, Log, TEXT("САМОТЕСТ-ЗОН-РЫБЫ ИТОГ: ok=%d fail=%d"), Ok, Fail);
	AsdaSelfTestKit::Quit(World.Get(), bQuit);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaFishZoneSelfTest(
	TEXT("asda.Fish.ZoneSelfTest"),
	TEXT("asda.Fish.ZoneSelfTest [quit] — ОФЛАЙН-самотест волны В4 на Альпии: зоны в мире (позиции/высота воды/OBB/меш/клип), ")
	TEXT("двойной клик по зоне → 109, доп-меню cmd 8 и ячейка хотбара типа 5, гейты рыбалки парами, клавиши через Slate, ")
	TEXT("поле чата при рыбалке, метки FishingArea на мини-карте и большой карте"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Fish.ZoneSelfTest"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		// ★ 12.10.2026: умолчание `asda.Fish.ZonePickWaterOnly` = 1 (решение владельца: с суши рыбалку не предлагать). Блоки C/R
		//   проверяют ПОВЕДЕНИЕ ОРИГИНАЛА (террейн не соперник зоне, двойной по суше → 109), поэтому на весь прогон ключ 0;
		//   W1/W2 сами ставят 1 и возвращают 0. Исходное значение вернёт RestoreAll/сторож мира.
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Fish.ZoneSelfTest"), KitArgs)) { return; }
		if (!W) { return; }
		// ★ ревизия СРЕДН-3: ключ «как оригинал» — только ПОСЛЕ гейтов запуска (иначе отказ онлайн оставлял его 0 до смены карты).
		AsdaCVarSave::SetForTest(TEXT("asda.Fish.ZonePickWaterOnly"), 0);
		bool bQuit = false;
		for (const FString& Arg : A) { if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } }
		TWeakObjectPtr<UWorld> WW = W;
		const TSharedRef<bool> bMoved = MakeShared<bool>(false);                              // персонаж перенесён на исходную точку
		const TSharedRef<FVector2D> Orig = MakeShared<FVector2D>(FVector2D::ZeroVector);       // где он стоял до переноса
		auto Go = [WW, bQuit, bMoved, Orig]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.Fish.ZoneSelfTest"), bQuit)) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
			TSharedRef<FAsdaFishZoneSelfTest> T = MakeShared<FAsdaFishZoneSelfTest>();
			T->bQuit = bQuit;
			const bool bOk = T->RunLogic(Wd);
			if (*bMoved) { T->PosWas = *Orig; }   // по концу — туда, где персонажа оставил сосед пачки (как до правки)
			if (!bOk) { T->Finish(); return; }
			T->Stage2();
		};
		// Свой персонаж офлайн — штатные крюки (как самотесты В1/В2) у спота 0 Альпии; зоны ставит подсистема сама.
		APlayerController* P = W->GetFirstPlayerController();
		if ((!P || !P->GetPawn()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 139 77"));
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), 3.f, false);
			return;
		}
		// ★ R5-fix8-c (14.11.2026): ИСХОДНОЕ СОСТОЯНИЕ СТАВИТ САМ ТЕСТ. Персонаж уже есть (тест не первый в игре) — ставим его туда же,
		//   куда его ставит ветка выше (тайл 139,77), и даём камере прийти к нему. Прежде `RunLogic` переносил персонажа к
		//   споту и В ТОМ ЖЕ КАДРЕ искал точки клика шага C по камере, оставшейся у соседа пачки: зона оказывалась в
		//   20 000 ед. (первым тестом — 508), двойной клик проигрывал земле, точки «мимо зон» не находилось — 75/3 (C2, C3, C6).
		if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(P ? P->GetPawn() : nullptr))
		{
			*Orig = Me->GetGamePosXZ();
			*bMoved = true;
			AsdaSkillCastLock::Reset(TEXT("самотест зон рыбалки: исходная точка"));
			Me->StopMove();
			Me->NetSnapHard(AsdaCoord::TileCenterGame(139, 77));
			// Камера — как у свежей игры (сосед мог оставить её повёрнутой или отодвинутой): поворот 0, наклон −20, плечо 700
			//   (умолчания `AAsdaPlayerController::CamYawDeg/CamPitchDeg` и `AAsdaPlayerCharacter::CameraArm`).
			if (AAsdaPlayerController* APc = Cast<AAsdaPlayerController>(P)) { APc->SetOrbitAnglesForBench(0.f, -20.f); }
			if (Me->CameraArm) { Me->CameraArm->TargetArmLength = 700.f; }
			UE_LOG(LogAsdaFishZoneTest, Log, TEXT("САМОТЕСТ-ЗОН-РЫБЫ инфо: тест не первый в игре — персонаж перенесён с (%.0f,%.0f) на исходную точку (тайл 139,77), камера в исходном положении; прогон через 1,5 с"), Orig->X, Orig->Y);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), 1.5f, false);
			return;
		}
		Go();
	}));

#endif // !UE_BUILD_SHIPPING
