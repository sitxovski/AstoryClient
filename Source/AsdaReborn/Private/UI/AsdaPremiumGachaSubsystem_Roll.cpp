// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumGachaSubsystem_Roll.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumGachaSubsystem.cpp, дословный перенос): звук, баланс, ответ розыгрыша, применение приза, актор сцены, вёрстка.
//   Общее для частей — в AsdaPremiumGachaSubsystemInternal.h; шапка и реверс — в AsdaPremiumGachaSubsystem.cpp.
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaPremiumPostSubsystem.h"   // отложенный показ приза: сообщаем ящику итог выбора
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaGachaSceneActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"                          // TActorIterator — поиск актора сцены для диагностики
#include "Kismet/KismetRenderingLibrary.h"   // ★ 24.09: выгрузка буфера сцены гачи в PNG (asda.Gacha.DumpRt)
#include "Components/Image.h"
#include "Engine/World.h"
#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"      // 68058 `Apple_Make3` — кадровый анимплан (RestartAnim = `CControl_Show`)
// общая плашка подсказки + владелец показа   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"   // ★ общая точка модели контейнеров: ApplyExternalRecord (6582)
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaNotice.h"          // ★ 07.11.2026: окно-сообщение ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, -1, …)`)
#include "UI/AsdaUiHelpers.h"
#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"   // ★ 08.11.2026: ключ asda.Gacha.IdleLoop2D
#include "UI/AsdaJuiComboBox.h"   // ★ ОБЩАЯ точка: выпадающий список скина Bk_ComboBox
#include "UI/AsdaJuiWindowMeta.h"   // мета окон: ручка перетаскивания живёт в данных
#include "Framework/Application/SlateApplication.h"   // курсор для зоны колеса над окном 68310
#include "GameFramework/PlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"   // ★ единый перевод UE→игра: тайл входа в сцену (шаг 9)
#include "UI/AsdaCraftSubsystem.h"         // ★ гейт входа: открыты ли окна ремесла 76000/77500 (волна В2)
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "AsdaPremiumGachaSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumGachaSubsystem*.cpp

namespace {
	// ══════════════════ ЗВУК — ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА ══════════════════
	//   Оригинал зовёт ЧЕТЫРЕ файла (`ui_core.c:6642/7437/7442/7692/7693`, `world_map.c:4701`):
	//     S_e_pr_gc_idle_00.wav  — петля покоя (loop=1), стоп в `uiGuildPanelClose` (`ui_core.c:7188`)
	//     S_e_pr_gc_rol_00.wav   — успешный прокрут (одиночный)
	//     S_e_pr_gc_click_00.wav — клик по шару 68057 и момент «клип 2 доиграл → клип 3»
	//     S_e_pr_gc_open_00.wav  — вскрытие шара, вторым за click
	//   Самих `.wav` НЕТ ни в `Data\Sound` (1483 файла), ни в `SFT.BIN`, ни в архивах, ни в четырёх
	//   сборках (проверено `Reports/gacha_ui_re.md` §4.2 и `gacha_fx_re.md`). По решению
	//   владельца подобраны БЛИЖАЙШИЕ ПО СМЫСЛУ из имеющихся 1483 звуков клиента:
	//     ★ `S_e_sm_ro_ready_00` (6.35 с) — «рулетка готова»: в оригинале это звук ГОТОВНОСТИ панели
	//       рулетки (`item_inventory.c:56828`). Берём как петлю покоя автомата.
	//     ★ `S_e_sm_ro_start_00` (4.99 с) — «рулетка закрутилась» (`roulettePanelHandleNetEvent
	//       @0xE6E480`, social.c:8523). Берём как звук прокрута барабана.
	//     ★ `S_e_button06` (0.09 с) — штатный короткий UI-щелчок оригинала (`ui_core.c:74287` и ещё
	//       11 мест). Берём как «клик» (щелчок автомата и клик по шару).
	//     ★ `S_e_sm_ro_item_00` (0.54 с) — «рулетка выдала предмет». Берём как вскрытие шара.
	//   Семейство `S_e_sm_ro_*` выбрано не наугад: это ЕДИНСТВЕННАЯ рулетка-механика оригинала,
	//   то есть ближайший из существующих аналог гачапона. Замена помечена и здесь, и в отчёте
	//   `Reports/gacha_ui_impl.md`; при появлении настоящих `S_e_pr_gc_*` менять только эти
	//   четыре константы.
	const FName kSndIdle  = TEXT("S_e_sm_ro_ready_00");   // ← S_e_pr_gc_idle_00.wav

	constexpr int32 kLocPrizeGranted  = 110671;  // успех 6582 (`uiGachaResultApply`, ui_core.c:7936)

	// Отказы 6578 (`GachaScene_OnServerResult@0xB6FEE0`, ui_core.c:7455-7477). Эталон отдаёт
	//   КАЖДЫЙ отказ в общую плашку `CItem_BuildTooltipText`; в порту тот же смысл несёт
	//   `UAsdaMessageSubsystem` (та же общая точка используется для `kLocNoMoney` в `ConfirmRoll`).
	constexpr int32 kLocRollBusy      = 110623;  // статус 2  (ui_core.c:7457)
	constexpr int32 kLocRollNoRoom    = 110620;  // статус 0xA (ui_core.c:7469)
	constexpr int32 kLocRollNotAllow  = 12960;   // статус 0xB (ui_core.c:7475) — без звука покоя
}

// ★ 07.11.2026 [ОРИГИНАЛ] Отказы прокрутки эталон отдаёт в `CItem_BuildTooltipText(g_pItemTooltipUI, 0, -1, текст, 1)` — это окно-сообщение
//   ONEBUT_MSGBOX 1500 (ui_core.c:7431-7480, 7499-7506), а не строка чата, как было в порту. 0 — прежняя строка чата.
static ASDA_DEV_CVAR(int32, CVarGachaRefusalMsgBox,
	TEXT("asda.Gacha.RefusalMsgBox"), 1,
	TEXT("[ОРИГИНАЛ] Отказы прокрутки премиум-гачи — окном-сообщением ONEBUT_MSGBOX, как в оригинале. 0 — строкой чата (прежний порт)."),
	ECVF_Default);

// Читатель ключа выше.
static bool AsdaGachaRefusalMsgBoxOn() { return CVarGachaRefusalMsgBox.GetValueOnGameThread() != 0; }

// ═══════════════════════ ЗВУК (замена, см. шапку файла) ═══════════════════════
void UAsdaPremiumGachaSubsystem::PlayGachaSound(FName Name)
{
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(Name, /*bLoop*/ false);   // эталон: CSoundFlag_PlaySound2D(..., 0)
	}
}

// ★ 08.11.2026 [ОРИГИНАЛ] ключ `asda.Gacha.IdleLoop2D`: петля покоя 2D, как у эталона. 0 — прежний путь (3D у слушателя).
static int32 GAsdaGachaIdleLoop2D = 1;
static FAutoConsoleVariableRef CVarAsdaGachaIdleLoop2D(
	TEXT("asda.Gacha.IdleLoop2D"),
	GAsdaGachaIdleLoop2D,
	TEXT("[ОРИГИНАЛ 08.11.2026] Петля покоя автомата гачи — 2D-звук с флагом петли (`CSoundFlag_PlaySound2D(…, 1)`, 7 мест эталона). "
	     "1 = по эталону (умолчание), 0 = прежний путь (3D у позиции персонажа)."),
	ECVF_Default);

// Петля покоя. Эталон зовёт `CSoundFlag_PlaySound2D(..., 1)` (premiumGacha_BeginRoll@0xB6DF80, GachaScene_OnServerResult@0xB6FEE0 ×4,
//   GachaScene_OnControlMessage@0xB704C0, sowelWndConfirmAction@0xB73620) и адресно останавливает её `sub_9349C7`
//   (AsdaStory_game.c:202491, 203247).
//   ★ 08.11.2026 [ОРИГИНАЛ]: прежде петлю заводили 3D у слушателя (управляемый 2D-источник тогда не возвращал компонент), и флаг петли
//   терялся в звуковой подсистеме — звук покоя играл один раз. Теперь 2D-петля с компонентом (`PlaySound2DManaged`), петля — флаг вызова.
void UAsdaPremiumGachaSubsystem::StartIdleLoop()
{
	StopIdleLoop();
	UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (!Snd) { return; }
	if (GAsdaGachaIdleLoop2D != 0) { IdleLoop = Snd->PlaySound2DManaged(kSndIdle); return; }
	FVector Loc = FVector::ZeroVector;
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (APawn* P = PC->GetPawn()) { Loc = P->GetActorLocation(); }
		}
	}
	IdleLoop = Snd->PlaySound3D(kSndIdle, Loc, 100.f, 10000.f, /*bLoop*/ true);
}

void UAsdaPremiumGachaSubsystem::StopIdleLoop()
{
	if (IdleLoop) { IdleLoop->Stop(); IdleLoop = nullptr; }
}

// ───────────────────────────── сеть ─────────────────────────────
void UAsdaPremiumGachaSubsystem::RequestBalance()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	Net->SendPremiumBalanceQuery5466(Net->SelectedCharId);
}

void UAsdaPremiumGachaSubsystem::OnBalance(uint8 Status, int32 NewBalance)
{
	if (Status != 1)
	{
		// 0 = нет данных (нотис 12942), 2 = charId не совпал, 3 = игрок занят.
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] баланс не получен: статус=%d"), (int32)Status);
		return;
	}
	// Эталон 5467 (`netHandleAuthResult@0xECB480`) кладёт сумму в поле персонажа
	//   `Field142C->gap_43B[17]` — это ПЕРВЫЙ кошелёк (строка 68202). Второй (68204) не приходит.
	Balance = NewBalance;
	RefreshClickWinTexts();   // 68202/68204 в окне 68200 (порт `inventoryBag_RefreshGoldUi`)
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] баланс=%d"), Balance);
}

// ★ 07.11.2026 [ОРИГИНАЛ]: отказ прокрутки строкой из таблицы — `CItem_BuildTooltipText(…, 0, -1, …)` = окно-сообщение ONEBUT_MSGBOX.
void UAsdaPremiumGachaSubsystem::PostRollRefusal(int32 LocId)
{
	if (AsdaGachaRefusalMsgBoxOn())
	{
		AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
	}
	else if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostLoc(EAsdaMsgCategory::Trade, LocId);
	}
}

// ★ 07.11.2026 [ОРИГИНАЛ]: то же для готового текста (коды 0/0x62/0x63 эталон показывает числом).
void UAsdaPremiumGachaSubsystem::PostRollRefusalText(const FString& Text)
{
	if (AsdaGachaRefusalMsgBoxOn())
	{
		AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Trade, Text);
	}
	else if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostText(EAsdaMsgCategory::Trade, Text);
	}
}

void UAsdaPremiumGachaSubsystem::OnRollResult(const TArray<uint8>& Body63)
{
	bRequestInFlight = false;
	if (Body63.Num() < 63) { return; }

	const uint8 Status = Body63[0];

	// ★★★ ЭТАЛОН ДЕЛАЕТ ТРИ ВЕЩИ ДО switch — БЕЗУСЛОВНО (ui_core.c:7423-7430):
	//       *(this + 644) = 0;            // антидубль снят — уже сделано выше
	//       sub_92387F(thisSelf);         // = SetVisible(ctrl 68100, 0) — закрыть диалог оплаты
	//       thisSelf[221] = -1;           // gap_254[288] = «вариант не выбирают»
	//   Порт закрывал диалог только в `ConfirmRoll` и сбрасывал `VariantBaseCode` только в
	//   успешной ветке — на отказе диалог мог остаться висеть, а старый код варианта — жить.
	HideConfirmDialog();
	SetSceneVariantBase(-1);

	if (Status != 1)
	{
		// ★★★ БЫЛО: порт МОЛЧА возвращал автомат в покой (только `UE_LOG`). Именно поэтому
		//   отказ сервера по второму автомату выглядел у владельца как «кнопка ничего не делает»:
		//   ни анимации (её и не должно быть), ни единого слова о причине.
		//   ЭТАЛОН (`GachaScene_OnServerResult`, ui_core.c:7431-7480) на КАЖДЫЙ отказ пишет текст
		//   в общую плашку и (кроме 0xB) заводит петлю покоя `S_e_pr_gc_idle_00.wav`.
		bool bKnown = true;
		switch (Status)
		{
		case 0:
		case 0x62:
		case 0x63:
			// `sprintf(msgBuf, "%d", payload[0])` — эталон показывает КОД КАК ЕСТЬ, без локализации.
			PostRollRefusalText(FString::Printf(TEXT("%d"), (int32)Status));
			StartIdleLoop();
			break;
		case 2:
			PostRollRefusal(kLocRollBusy);
			StartIdleLoop();
			break;
		case 3:
			PostRollRefusal(kLocNoMoney);
			StartIdleLoop();
			break;
		case 0xA:
			PostRollRefusal(kLocRollNoRoom);
			StartIdleLoop();
			break;
		case 0xB:
			// ★ У эталона в этой ветке звука НЕТ (ui_core.c:7474-7477) — не добавляем.
			PostRollRefusal(kLocRollNotAllow);
			break;
		default:
			// `default: return;` эталона — неизвестный код не показывает НИЧЕГО.
			bKnown = false;
			break;
		}
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] прокрут ОТКАЗАН сервером: статус=%d, слот=%d%s. ")
			TEXT("Статус 0 у слота > 0 = сервер не знает такого автомата: в его ")
			TEXT("`ItemTable_Info\\PrimiumGaChaPriceTable.txt` меньше строк, чем автоматов ")
			TEXT("(гейт `Recv_UserPrimiumGaCha@0x4F6F10`, лог сервера `Recv_UserPrimiumGaCha(3)`). ")
			TEXT("Разбор — Reports/gacha_slot2_fix.md"),
			(int32)Status, SelectedSlot, bKnown ? TEXT("") : TEXT(" (код эталону неизвестен — реакции нет)"));
		EnterPhase(EAsdaGachaPhase::Idle);
		return;
	}

	PrizeRecord.SetNumUninitialized(56);
	FMemory::Memcpy(PrizeRecord.GetData(), &Body63[1], 56);
	PrizeItemCode = *reinterpret_cast<const int32*>(PrizeRecord.GetData());   // запись+0 = код предмета
	// ★★★ `VariantBaseCode` (= эталонное `thisSelf[221]`/`gap_254[288]`) уже сброшен ВЫШЕ, до switch:
	//   эталон делает это на КАЖДЫЙ ответ 6578 (ui_core.c:7429), а не только на успешный. Из-за
	//   этого кнопка 68452 сразу после прокрута всегда идёт в ветку «покой» (разбор — у `TakePrize`).
	Balance = *reinterpret_cast<const int32*>(&Body63[59]);                   // новый баланс с сервера
	RefreshClickWinTexts();

	// ★ ДИАГНОСТИКА ВЫДАЧИ. В оригинале предмет кладёт СЕРВЕР: `PrimiumGaCha@0x4F70B0` зовёт
	//   `sub_4D63F0(userNum, &запись, 0)`, а тот пишет запись в контейнер по её ЖЕ полям
	//   (запись+4 контейнер: 1 сумка / 2 второй / 3 надетое / 5 склад; запись+5 слот i16).
	//   Клиент в этот момент предмет НИКУДА не кладёт — `GachaScene_OnServerResult` только
	//   перерисовывает сумку (`CInventoryUI_ResetAndRefresh`, ui_core.c:7444). Поэтому если сервер
	//   прислал запись без ячейки, предмета не будет ни у оригинала, ни у нас — и это надо ВИДЕТЬ.
	const int32 RecCont = (PrizeRecord.Num() >= 7) ? (int32)PrizeRecord[4] : 0;
	const int32 RecSlot = (PrizeRecord.Num() >= 7) ? (int32)(*reinterpret_cast<const int16*>(PrizeRecord.GetData() + 5)) : -1;
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] выпал предмет %d, ячейка записи: контейнер=%d слот=%d, баланс стал %d"),
		PrizeItemCode, RecCont, RecSlot, Balance);
	if (RecCont == 0 || RecSlot < 0)
	{
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] ★ СЕРВЕР НЕ НАЗНАЧИЛ ЯЧЕЙКУ призу %d (контейнер=%d слот=%d). ")
			TEXT("`sub_4D63F0` на сервере кладёт запись ТОЛЬКО в контейнер 1/2/3/5, поэтому предмет ")
			TEXT("в сумку не попадёт, а забор приза (6581) получит отказ 10. Разбор — Reports/gacha_prize_fix.md §4"),
			PrizeItemCode, RecCont, RecSlot);
	}
	// ★★★ 6578 СО СТАТУСОМ 1 = НАЧАЛО ПРОКРУТКИ, а не её конец.
	//   `GachaScene_OnServerResult@0xB6FEE0`, case 1 (ui_core.c:7441-7443):
	//       *((_BYTE *)thisSelf + 888) = 1;
	//       gachaSceneSetActiveChild((int)thisSelf[162], 2);            // КЛИП ПРОКРУТКИ
	//       CSoundFlag_PlaySound2D(g_pSoundMgr, "S_e_pr_gc_rol_00.wav", 0);
	//   Дальше клип 2 (2.000 с) доигрывает до конца, и только тогда — клип 3 (2.333 с).
	//   Итого машинный участок после ответа сервера = 4.333 с (было ~2.33 с).
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: ответ пришёл, когда сцена уже закрыта (разрешено после 15 с молчания или вынужденно). Эталон крутил бы
	//   пустой контейнер, шар бы пропал; запоминаем приз — при открытии автомат выдаст шар.
	if (!bSceneOpen && AsdaGachaPrizeCloseLockOn())
	{
		bResumePrize = true;
		ResumePrizeSlot = SelectedSlot;
		ResumePrizeCode = PrizeItemCode;
		ResumePrizeRecord = PrizeRecord;
		PrizeItemCode = -1;
		PrizeRecord.Reset();
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 6578 при закрытой сцене: приз %d запомнен, шар появится при открытии"), ResumePrizeCode);
		return;
	}
	EnterPhase(EAsdaGachaPhase::Rolling);
}

// ★ Эмуляция ответа 6582 (описание — у объявления). Тело собирается НАСТОЯЩЕЕ, 61 байт:
//   @0 статус · @1 контейнер очищаемой ячейки · @2 i16 её слот · @4 запись 57 б · @60 хвост.
//   В успехе сервер шлёт контейнер 0 и слот −1 (очищать нечего) — так и в живом логе владельца.
void UAsdaPremiumGachaSubsystem::SimulatePrizeApply(int32 VariantCode, int32 ContainerType,
                                                    int32 SlotIndex, int32 Amount)
{
	TArray<uint8> B;
	B.SetNumZeroed(61);
	B[0] = 1;
	B[1] = 0;
	*reinterpret_cast<int16*>(&B[2]) = (int16)-1;
	uint8* Rec = &B[4];
	*reinterpret_cast<int32*>(Rec + 0) = VariantCode;
	Rec[4] = (uint8)ContainerType;
	*reinterpret_cast<int16*>(Rec + 5) = (int16)SlotIndex;
	*reinterpret_cast<int32*>(Rec + 9) = Amount;
	UE_LOG(LogAsdaGacha, Log,
		TEXT("[премиум-гача] эмуляция 6582: вариант %d в ячейку %d/%d (сцена %s)"),
		VariantCode, ContainerType, SlotIndex,
		bSceneOpen ? TEXT("ОТКРЫТА") : TEXT("закрыта — маршрут из ящика"));
	OnPrizeApplied(B);
}

// ★ 10.10.2026 (barber_minors): эмуляция ответа 6578 (описание — у объявления). Запись приза без ячейки (контейнер 0,
//   слот −1) — офлайн в сумку ничего не кладётся; баланс — текущий.
void UAsdaPremiumGachaSubsystem::SimulateRollResult(uint8 Status, int32 PrizeCode)
{
	TArray<uint8> B;
	B.SetNumZeroed(63);
	B[0] = Status;
	uint8* Rec = &B[1];
	*reinterpret_cast<int32*>(Rec + 0) = PrizeCode;
	Rec[4] = 0;                                                   // контейнер записи: 0 = ячейки нет
	*reinterpret_cast<int16*>(Rec + 5) = (int16)-1;               // слот записи
	*reinterpret_cast<int32*>(&B[59]) = Balance;                  // баланс не меняется
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] эмуляция 6578: статус %d, приз %d (сцена %s, фаза %d)"),
		(int32)Status, PrizeCode, bSceneOpen ? TEXT("открыта") : TEXT("ЗАКРЫТА"), (int32)Phase);
	OnRollResult(B);
}

// 6582 приходит из сетевого колбэка ⇒ модель инвентаря отсюда НЕ трогаем (закон
//   `no-reentrant-net-callbacks`): кладём тело и применяем в `Tick` (`ApplyPrizeResult`).
void UAsdaPremiumGachaSubsystem::OnPrizeApplied(const TArray<uint8>& Body61)
{
	if (Body61.Num() < 61) { return; }
	PendingApplyBody = Body61;
	bApplyPending = true;
}

// ═══════════ ПОРТ `uiGachaResultApply@0xB72440` (ui_core.c:7883-7956) ═══════════
//   Порядок эталона важен: ПЕРВЫЕ ТРИ ДЕЙСТВИЯ БЕЗУСЛОВНЫ, статус проверяется только после них.
//     g_bGachaRollActive = 1;
//     uiResetHideControl(v15);                                   // скрыть 68500 + сбросить опцию
//     ctrlSetActiveChildByIndex(g_pGachaSceneCtrl, v15[162], 1);  // клип 1 = ПОКОЙ
//     if (pktBody[0] == 1) { …зачисление…, нотис 110671, при подходящем типе v15[221] = код… }
//   Прежний порт при статусе ≠ 1 выходил СРАЗУ и оставлял окно выбора висеть, а сцену — в чужой фазе.
void UAsdaPremiumGachaSubsystem::ApplyPrizeResult(const TArray<uint8>& Body61)
{
	if (Body61.Num() < 61) { return; }

	CloseVariantWindow();                     // `uiResetHideControl`
	EnterPhase(EAsdaGachaPhase::Idle);        // клип 1

	const uint8 Status = Body61[0];
	if (Status != 1)
	{
		// Коды сервера (`Recv_UserPrimiumGaChaReward@0x4F72B0`, GameServer.exe): 10 = запись по
		//   (контейнер, слот) не найдена ЛИБО её код не совпал с телом[0..3] ЛИБО у кода варианта
		//   `item_table[+0x24] != 3` (поле CategoryFlag).
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] приз НЕ зачислен: статус=%d (10 = сервер не нашёл предмет в ячейке ")
			TEXT("контейнер=%d слот=%d или не принял код варианта). Разбор — Reports/gacha_prize_fix.md §4"),
			(int32)Status, (int32)Body61[1], (int32)(*reinterpret_cast<const int16*>(&Body61[2])));
		// Сервер отказал ⇒ обмена нет, в ячейке лежит вариант «по умолчанию».
		NotifyPostVariantFinished(false);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	// (1) Очистить ИСХОДНУЮ ячейку: тело[1] контейнер, тело[2..3] слот (эталон `memset` по
	//     `g_InventoryBag` / `g_InventoryArray2`, ui_core.c:7916-7920).
	const int32 OldCont = (int32)Body61[1];
	const int32 OldSlot = (int32)(*reinterpret_cast<const int16*>(&Body61[2]));
	if (Inv && OldSlot >= 0 && (OldCont == 1 || OldCont == 2))
	{
		// Контракт общей точки `ApplyExternalRecord`: запись с `amount(+9) == −1` ⇒ ячейка чистится.
		const TArray<uint8> Clear = FAsdaItemRecordWriter::ClearCell(OldCont, OldSlot);   // ★ U17
		Inv->ApplyExternalRecord(Clear);
	}

	// (2) Положить НОВУЮ запись (тело+4, 57 б) по её собственным полям — эталон делает то же самое
	//     (`CClass_BuildItemRecord` + запись в контейнер по полю +4 и слоту +5, ui_core.c:7921-7935).
	TArray<uint8> Rec;
	Rec.SetNumUninitialized(57);
	FMemory::Memcpy(Rec.GetData(), &Body61[4], 57);
	const int32 NewCode = *reinterpret_cast<const int32*>(Rec.GetData());
	if (Inv) { Inv->ApplyExternalRecord(Rec); }

	// (3) Нотис 110671 (эталон шлёт его в ОБЩУЮ плашку `CItem_BuildTooltipText`, ui_core.c:7936-7938).
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostLoc(EAsdaMsgCategory::Trade, kLocPrizeGranted);
	}

	// (4) `v15[221] = itemSlotRecord[0]` — но ТОЛЬКО для подходящего типа (ui_core.c:7943-7951):
	//     контейнер записи == 1 и `itemType ≤ 4 | 8…9 | subType 200/201`.
	int32 NewBase = -1;   // новое `gap_254[288]` сцены
	if (Rec[4] == 1)
	{
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemRecord* R = Items ? Items->GetItem(NewCode) : nullptr;
		if (R && ((R->ItemType <= 4) || (R->ItemType >= 8 && R->ItemType <= 9)
		          || R->ItemSubType == 200 || R->ItemSubType == 201))
		{
			NewBase = NewCode;
		}
	}
	SetSceneVariantBase(NewBase);
	UE_LOG(LogAsdaGacha, Log,
		TEXT("[премиум-гача] приз зачислен: предмет=%d в ячейку %d/%d (очищена %d/%d), gap_254[288] → %d"),
		NewCode, (int32)Rec[4], (int32)(*reinterpret_cast<const int16*>(Rec.GetData() + 5)),
		OldCont, OldSlot, VariantBaseCode);
	// Обмен состоялся: выбранный вариант только что положен выше ⇒ отложенная запись ящика не нужна.
	NotifyPostVariantFinished(true);
}

void UAsdaPremiumGachaSubsystem::OnMenuButton(int32 IdNum, int32 TargetWindowId, bool bChecked)
{
	if (IdNum != 73410) { return; }
	// ★ 07.11.2026: при отказе закрыть кнопка меню могла разойтись со сценой, поэтому решает состояние сцены, а не флаг нажатия.
	if (bSceneOpen) { RequestCloseScene(); }
	else if (bChecked) { OpenScene(); }
}

void UAsdaPremiumGachaSubsystem::PopulatePrizeGrid()
{
	// Сетка 68300 `Premium_Pick_View_Win`: 25 ячеек 68311…68335 (шаг 66 px), страницы прокруткой.
	// Источник — `DT_premium_gatya` (импорт `./Data/premium_gatya.bin`, поле ItemCode).
	// ⚠ Таблица порта была унаследована от ЧУЖОЙ сборки (356 строк / 37 кодов, как в арабской);
	//   2026-08-17 пересобрана из нашего клиента (106) + серверного пула (35) = 141 приз, чтобы
	//   витрина показывала ровно то, что может выпасть (до этого общих кодов было ДВА).
	// ★★★ ФИЛЬТР ПО АВТОМАТУ (2026-08-18). Эталон `uiRefreshItemGridPage@0xB6EE70`
	//   (ui_core.c:7333-7335) обходит ВСЮ таблицу и берёт только записи выбранного автомата:
	//       for (j = 0; j < v10[154]; ++j)
	//           if ( *(_DWORD *)(v4 + 20 * j + 12) == v10[162] )   // +12 = ViewSlot, 162 = слот
	//   Порт этого фильтра НЕ ИМЕЛ — обе витрины показывали ОДИН И ТОТ ЖЕ список, то есть у
	//   второго автомата в «Виде» лежали призы первого. Поле `+12` = `ViewSlot`
	//   (в оригинале −1 = «не показывать ни в одной витрине»).
	static const TCHAR* kPrizeTable = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_premium_gatya.DT_premium_gatya");
	if (!PrizeTable)
	{
		// ★ R5-fix2-c (13.11.2026): таблица — у подсистемы предметов (манифест item-БД, имя `premium_gatya`: тот же путь ассета, что
		//   `kPrizeTable`, загружен при её старте и держится сильно). Был свой `LoadObject` того же пути — второй вход к тому же объекту.
		const UAsdaItemTableSubsystem* ItemDb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		PrizeTable = ItemDb ? ItemDb->GetDataTable(TEXT("premium_gatya")) : nullptr;
	}
	Prizes.Reset();
	if (!PrizeTable)
	{
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] нет %s — сетка призов пуста"), kPrizeTable);
		return;
	}
	for (const TPair<FName, uint8*>& It : PrizeTable->GetRowMap())
	{
		if (const FAsdaPremiumGatyaRow* Row = reinterpret_cast<const FAsdaPremiumGatyaRow*>(It.Value))
		{
			if (Row->ItemCode > 0 && Row->ViewSlot == SelectedSlot) { Prizes.Add(Row->ItemCode); }
		}
	}
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] сетка призов автомата %d: %d позиций, рядов %d, позиций прокрутки %d"),
		SelectedSlot, Prizes.Num(), GridRowCount(), GridMaxScrollRow() + 1);
	RefreshPrizeGridPage();
}

void UAsdaPremiumGachaSubsystem::EnsureSceneActor()
{
	UWorld* W = GetWorld();
	if (!W || SceneActor) { return; }

	FActorSpawnParameters SP;
	SP.ObjectFlags |= RF_Transient;              // рантайм-объект: в уровень не сохраняется
	// ★ Сцена ставится ДАЛЕКО от игрока: капчер снимает только автоматы, а мир в кадр попадать
	//   не должен. В оригинале это отдельный render-target (общий с окном питомца PetWindow2).
	const FVector Far(0.f, 0.f, 200000.f);
	SceneActor = W->SpawnActor<AAsdaGachaSceneActor>(AAsdaGachaSceneActor::StaticClass(), Far, FRotator::ZeroRotator, SP);
	if (!SceneActor) { return; }

	// Число автоматов = число слотов в таблице цен. ⚠ Оригинал строит ОДИН (читает dataCount = 1
	// из заголовка, хотя записей две) — у нас чинится дефект данных, поэтому автоматов столько,
	// сколько строк в DT_premium_gatya_data.
	//
	// ★★★ ОСОЗНАННОЕ РАСШИРЕНИЕ СВЕРХ ОРИГИНАЛА (2026-08-18, по прямому указанию владельца).
	//   БЫЛО В ОРИГИНАЛЕ: `premium_gatya_data.bin` — две записи цен ({0,690} и {1,690}) при
	//     счётчике 1 в заголовке; `premium_gatya.bin` — `ViewSlot` только 0 и 1; серверная
	//     `PrimiumGaChaPriceTable.txt` — один слот. То есть данными задан ОДИН автомат.
	//   СТАЛО: пять (слоты 0..4) — цена и пул слотов 2..4 скопированы с работающего слота 0
	//     (`Tools/asda_build_premium_gatya_slots.py`, отчёт `Reports/gacha_slots5.md`).
	//   ПОЧЕМУ ЭТО НЕ ВЫДУМКА: и КОД, и АРТ оригинала рассчитаны ровно на пять — пять полос
	//     68010+10i / кнопок витрины 68011+10i / кнопок «крутить» 68061+i, пять моделей
	//     `md_gachya_{a..e}_00`, пять углов веера и пять экранных смещений в
	//     `uiBuildPortraitCameraMatrices`/`PetWindow_RenderToTarget`, пять цифр-скинов
	//     `GOLD_roullete_FONT3_{1..5}` в атласе `NE_PvP2.tga` (номер автомата — картинка, не текст).
	//   ГРАНИЦА: больше пяти данные не позволяют, поэтому `BuildMachines` клампит счёт 1..5.
	int32 Slots = 1;
	if (PriceTable) { Slots = FMath::Max(1, PriceTable->GetRowMap().Num()); }
	else { GetSlotPrice(0); if (PriceTable) { Slots = FMath::Max(1, PriceTable->GetRowMap().Num()); } }
	SceneActor->BuildMachines(Slots);
	SceneActor->PlayClipAll(1, true);            // покой

	ApplySceneBrush();
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 3D-сцена создана: автоматов %d"), Slots);
}

void UAsdaPremiumGachaSubsystem::ApplySceneBrush()
{
	// Вывести render-target в контейнер 68056 окна 68000 (в оригинале ему задаётся размер
	// бэкбуфера, vtable[5] — то есть 3D ложится во весь экран).
	if (!SceneActor || !RootWin) { return; }
	UImage* Img = Cast<UImage>(SceneHost);
	if (!Img)
	{
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] контейнер 68056 найден как %s, но это не UImage — RT не выведен"),
			SceneHost ? *SceneHost->GetName() : TEXT("НЕТ"));
		return;
	}
	UMaterialInstanceDynamic* MID = SceneActor->GetSceneBrushMaterial();
	if (!MID)
	{
		UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] материал сцены не собран — 3D не выведен"));
		return;
	}

	// ★★★ ПРИЧИНА «АВТОМАТОВ НЕ ВИДНО» (найдено по данным, не подобрано).
	//   Контрол 68056 носит скин `NoImage_icon`, у которого в `JuiSkin.ini` план указывает
	//   МИКРО-КУСОК атласа: `UV = "0.994 0.000 … 1.000 0.004"` (0.6 % × 0.4 % от `N_01.tga`).
	//   Импортёр честно записал это в браш, а `SetBrushResourceObject(материал)` UV-регион НЕ
	//   снимает. Поэтому кисть собирается ЗАНОВО.
	// ★★★ 24.09.2026: ТЕЛО ЛЕЧЕНИЯ ВЫНЕСЕНО В ОБЩУЮ ТОЧКУ `AsdaUi::ApplySceneMaterialBrush`.
	//   Повод: ровно этой же болезнью болели превью питомца 43052 (дефект владельца) и 43576
	//   окна 43500 — то есть грабля ОБЩАЯ для всех контролов со скином-атласом, а не частный
	//   случай гачи. Здесь остался только ВЫЗОВ; разбор, цифры и список пострадавших — у
	//   объявления общей точки (закон `port-shared-points`).
	//   ⚠ Размер кисти у гачи СВОЙ — вьюпорт целиком (`vtable[5]` оригинала ставит бэкбуфер),
	//   поэтому он и передаётся аргументом, а не зашит в общей точке.
	AsdaUi::ApplySceneMaterialBrush(Img, MID, AsdaUi::ViewportSizeUnits(GetWorld()), TEXT("сцена гачи 68056"));
	SceneImage = Img;
}

void UAsdaPremiumGachaSubsystem::ApplyFullScreenPanels()
{
	// ★★★ ПОРТ ШАГОВ 11 и 13 `premiumGacha_BeginRoll@0xB6DF80` (world_map.c:4725-4736):
	//     gachaResultCtrl = FindControlByID(68059);  vtable[5](ctrl, sub_93632B(g_pRenderer),
	//                                                            sub_91EBD1(g_pRenderer));
	//     vtable[5](this->tileCountY /* = 68056 */,  sub_93632B(...), sub_91EBD1(...));
	//   `sub_93632B` = ширина бэкбуфера, `sub_91EBD1` = высота (сверено по `npc_mob_pet.c:96-97`,
	//   где ими же задаются `rtWidth`/`rtHeight`, и по `ui_core.c:35126-35136`, где ими клампят
	//   окно к экрану). `vtable[5]` = УСТАНОВКА РАЗМЕРА, а не позиции: в `combat_pvp.c:9044`
	//   тем же слотом ставят (70, gaugeHeight), а следом `CControl_DrawTextureRect(…, 0, 130−h,
	//   70, h)` рисует полосу шириной 70 и высотой h — то есть аргументы = (ширина, высота);
	//   позицию же ставит отдельная `CControl_SetField74_75`.
	//   В `.JUI` оба контрола объявлены 1024×768 в точке (0,0) — код растягивает их на весь экран.
	const FVector2D Vp = AsdaUi::ViewportSizeUnits(GetWorld());
	if (Vp.X < 16.f || Vp.Y < 16.f) { return; }
	if (Vp.Equals(FullScreenApplied, 0.5f)) { return; }   // размер не менялся — слоты не трогаем

	auto Stretch = [&Vp](UWidget* W)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W))
		{
			// Импортёр дал обоим POINT-анкор [0,0] с выравниванием (0,0) ⇒ офсеты = (X, Y, W, H).
			S->SetPosition(FVector2D::ZeroVector);
			S->SetSize(Vp);
		}
	};
	Stretch(SceneBack);    // 68059 `capback` — подложка под 3D
	Stretch(SceneHost);    // 68056 контейнер render-target

	// ★★★ Группа шара: сажаем в (0,0), РАСТЯГИВАЕМ НА ВЕСЬ ЭКРАН и снимаем обрезку.
	//   Её дети (зона шара 68057 и вспышка 68058) ездят по всему экрану за костью `Bone19`, а сама
	//   группа у импортёра всего 128×128. Размер обязателен, а не косметика: импортёр специально
	//   считает группам union-габариты именно потому, что ребёнок, торчащий за rect группы,
	//   ПЕРЕСТАЁТ ЛОВИТЬ КЛИК (`asda_jui_generic_umg.py:686-688` — «hit-test обрезается границей
	//   группы, симптом „кнопка не реагирует“»). Для группы с бегающими детьми union-габариты
	//   посчитать нельзя ⇒ даём ей размер вьюпорта, и дети всегда внутри.
	if (BallGroup)
	{
		if (UCanvasPanelSlot* GS = AsdaUi::SlotOf(BallGroup))
		{
			GS->SetPosition(FVector2D::ZeroVector);
			GS->SetSize(Vp);
		}
		BallGroup->SetClipping(EWidgetClipping::Inherit);
	}

	// Кисть контейнера растягивается по геометрии, но desired-size брать тоже от вьюпорта.
	if (SceneImage)
	{
		FSlateBrush B = SceneImage->GetBrush();
		B.ImageSize = Vp;
		SceneImage->SetBrush(B);
	}
	FullScreenApplied = Vp;
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 68059/68056 растянуты на экран: %.0fx%.0f"), Vp.X, Vp.Y);
}

void UAsdaPremiumGachaSubsystem::LayoutByProjection()
{
	// Порт `deathRespawnUI_LayoutResurrectPanel@0xB6E480` (ui_core.c:7128-7180) — мисномер, на деле
	// это ПОКАДРОВАЯ раскладка сцены гачи: в `.JUI` все дети корня 68000 лежат в (0,0), а экранные
	// позиции им ставит код по проекции 3D. Полоса слота i садится на проекцию ЯКОРЯ (вторая
	// матрица, ctrl+556) со сдвигом −w/2, −h/2 + yOff; шар 68057 — на проекцию кости Bone19
	// (это кость самого призового шара: в клипах покоя он спрятан под автоматом, в клипе выдачи
	// выкатывается, поэтому UI-шар буквально следует за 3D-шаром).
	if (!SceneActor || !RootWin) { return; }

	// ★★★ ЕДИНИЦЫ. Проекция актора отдаёт ПИКСЕЛИ кадра (кадр = размер вьюпорта), а офсеты
	//   CanvasPanelSlot живут в единицах UMG = пиксели / UI-масштаб. При DPI = 1.0 (текущая
	//   настройка проекта, [[resolution-aspect-system]]) это одно и то же, но делить надо всё
	//   равно — иначе на любой другой кривой DPI раскладка разъедется.
	const float Scale = FMath::Max(AsdaUi::ViewportDpiScale(GetWorld()), KINDA_SMALL_NUMBER);

	// Позиция слота в координатах КОРНЕВОГО канваса окна (все узлы 68000 сидят прямо в нём).
	auto PlaceAt = [](UWidget* W, const FVector2D& Pos)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W)) { S->SetPosition(Pos); }
	};

	// ── ПОЛОСЫ СЛОТОВ 68010+10i ────────────────────────────────────────────────────────────────
	//   Эталон (ui_core.c:7128-7148): сперва ВСЕ ПЯТЬ гасятся, затем показываются первые N
	//   (N = число автоматов), и каждой ставится позиция
	//       (screenX − w/2, screenY − h/2 + yOffset),
	//   где (w,h) — размер САМОГО контрола (vtable[6] `GetSize`), а yOffset = 0 / −40 (N=4) / −50 (N=5).
	const int32 N = SceneActor->GetMachineCount();
	const float YOff = SceneActor->GetSlotScreenYOffset();
	for (int32 i = 0; i < SlotBars.Num(); ++i)
	{
		UWidget* Grp = SlotBars[i];
		if (!Grp) { continue; }
		// Сама группа кликов не ловит — их ловят её дети-кнопки (закон `ui-button-hover-click-rule`).
		const ESlateVisibility Want = (i < N) ? ESlateVisibility::SelfHitTestInvisible
		                                      : ESlateVisibility::Collapsed;
		if (Grp->GetVisibility() != Want) { Grp->SetVisibility(Want); }
		if (i >= N) { continue; }

		FVector2D P;
		if (!SceneActor->GetSlotScreenPos(i, P)) { continue; }        // промах проекции — не двигаем

		// Размер и локальное смещение контрола-полосы ВНУТРИ группы берём из самого WBP, а не
		// цифрами в коде: импортёр сдвигает группу на union детей (у гачи это (−128, 0)).
		FVector2D Local = FVector2D::ZeroVector;
		FVector2D Size(76.f, 62.f);                                   // размер 68010 из `gamemain_jui.ini`
		if (UCanvasPanelSlot* IS = AsdaUi::SlotOf(SlotBarInner.IsValidIndex(i) ? SlotBarInner[i] : nullptr))
		{
			Local = IS->GetPosition();
			Size  = IS->GetSize();
		}
		const FVector2D Target(P.X / Scale - Size.X * 0.5f, P.Y / Scale - Size.Y * 0.5f + YOff);
		PlaceAt(Grp, Target - Local);
	}

	// ── ШАР 68057, ПОДСКАЗКА 68055 И ВСПЫШКА 68058 ─────────────────────────────────────────────
	//   Эталон (ui_core.c:7163-7172) от проекции кости `Bone19` активного клипа ВЫБРАННОГО слота
	//   ставит РОВНО ДВА контрола, у каждого вычитая половину ЕГО СОБСТВЕННОГО размера:
	//     `dword_2BDE140`   → (bone.x − w/2 + 15, bone.y − h/2 + 5)   = ШАР 68057
	//     `g_pPauseMenuCtrl`→ (bone.x − w/2,      bone.y − h/2 − 70)  = ПОДСКАЗКА 68055
	//   ★★★ ИСПРАВЛЕНО: сюда была подставлена вспышка 68058 — из-за этого «−70» уезжала она, а
	//     подсказку не двигал никто. Кто есть кто, доказано двумя независимыми путями:
	//       (1) `helper_setByte295(dword_2BDE140, …)` — байт +295 это `NoSelect` (ui_core.c:104983),
	//           а «включить/выключить мышь» осмысленно ТОЛЬКО у кликаемого контрола, то есть у шара;
	//           диалог оплаты ставит 1 (ui_core.c:7372), конец клипа 3 — 0 (ui_core.c:6635);
	//       (2) 68058 позиционируется ОТДЕЛЬНО и только в момент клика —
	//           `CControl_SetField74_75(ctrl68058, −13, −1)` (ui_core.c:7698). Это не экранная точка:
	//           +74/+75 — позиция ОТНОСИТЕЛЬНО РОДИТЕЛЯ, а родитель 68058 в `.JUI` — сам шар 68057
	//           (у полос и шара родитель = корень 68000 в (0,0), потому у них те же числа, что на
	//           экране). Импортёр иерархию расплющил в группу-канвас, поэтому склеиваем вручную:
	//           вспышка = позиция шара + (−13, −1).
	FVector2D BallPos;
	if (SceneActor->GetBoneScreenPos(SelectedSlot, BallPos))
	{
		const FVector2D B(BallPos.X / Scale, BallPos.Y / Scale);
		auto PlaceCentered = [&PlaceAt](UWidget* W, const FVector2D& At, const FVector2D& Extra) -> FVector2D
		{
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W))
			{
				const FVector2D Sz = S->GetSize();
				const FVector2D P = At - Sz * 0.5f + Extra;
				PlaceAt(W, P);
				return P;
			}
			return FVector2D::ZeroVector;
		};
		const FVector2D BallAt = PlaceCentered(Ball, B, FVector2D(15.f, 5.f));   // `…+ 15.0` / `…+ 5.0`
		PlaceCentered(BallHit,   B, FVector2D(15.f, 5.f));   // клик-зона едет вместе с шаром
		PlaceCentered(HintPlane, B, FVector2D(0.f, -70.f));  // `…+ 0.0` / `…− 70.0` — подсказка НАД шаром
		// Вспышка — ребёнок шара в `.JUI`: её точка = точка шара + (−13, −1).
		if (AsdaUi::SlotOf(Ball)) { PlaceAt(BallBang, BallAt + FVector2D(-13.f, -1.f)); }
	}
}
