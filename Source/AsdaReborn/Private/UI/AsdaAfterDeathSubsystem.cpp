// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaAfterDeathSubsystem — реализация. Полный реверс (клиент + GameServer) — в шапке .h.
#include "UI/AsdaAfterDeathSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока

#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (id кампвар-карт 19/23)
// ★★★ Аудит №4: файл был ЕДИНСТВЕННЫМ в порту, полностью выпавшим из общих точек — он не включал
//   `AsdaUiHelpers.h` вовсе. Подключено то, что применимо: `AsdaUi::SlotOf` (слот канваса, 3 места),
//   `UAsdaWindowSubsystem::IsWindowLive` (гвард травела) и `AsdaLoc::kBtnOk` (нотис кнопки msgbox).
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "UI/AsdaLocIds.h"      // ★ Аудит №4 (A31): AsdaLoc::kBtnOk — нотис кнопки общего msgbox

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"   // ★ U2 (дедуп №2, В2-A): MsgBoxCb — единая политика фолбэка окна-сообщения
#include "Text/AsdaLang.h"     // ★ N9/U10d (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"   // ★ волна В6 (W5-7): 4053 закрывает элитный магазин 6000 его точкой
#include "UI/AsdaHairShopSubsystem.h"      // ★ волна В6 (W5-7): 4053 закрывает парикмахерскую её точкой
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ волна В6 (W5-7): 4053 закрывает барабан гачи 10050 (`tagBoosterShop_CloseWindow`)
#include "UI/AsdaMarkup.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "Text/AsdaTextSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaNameplateComponent.h"   // имя инициатора воскрешения для опций 0/1
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя — своя смерть, окно после смерти, воскрешение
#include "UI/AsdaCampWarBattleSubsystem.h"    // ★ 31.10.2026: ранг армии (очки 6729 / численность 6742) — таймер смерти на карте 19
#include "UI/AsdaDeathInputLock.h"            // ★ 05.11.2026: замок ввода мёртвого (эталон CUIManager+0x44) — реализация здесь
#include "UI/AsdaInventorySubsystem.h"        // ★ 05.11.2026: опция 5 — свиток subType 48 в сумке (контейнер 1) и его применение
#include "UI/AsdaEffectsSubsystem.h"          // ★ 05.11.2026: опция 3 — состояние 189 (Resuscitate) в своём списке
#include "Items/AsdaItemTableSubsystem.h"     // ★ 05.11.2026: special_itemlist.SubType / item_list (эталон Table2Record / ItemRecord)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Engine/Engine.h"               // GEngine → размер вьюпорта для лога рецентровки
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"         // CVar asda.Death.ConfirmGaugeMs
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDeath, Log, All);

static const TCHAR* kDeathWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_After_Death_Win.WBP_After_Death_Win_C");

// ★ Длительность заливки полосы в msgbox «Желаешь воскреснуть?» (loc 2382), мс. ЧЕСТНО ПО ЭТАЛОНУ:
//   в оригинале полоса TWOBUT_GAUGE в ЭТОМ диалоге скрыта (InitControls ui_core.c:55952; ветка 0x4000
//   её не показывает) — гейта согласия для смерти НЕ существует. Полоса-гейт добавлена ПО ТРЕБОВАНИЮ юзера;
//   длительность взята из ЕДИНСТВЕННОГО эталонного гейта согласия в бинаре —
//   uiProgressBar_AnimStart(GAUGE, 10000) @ ui_core.c:94263 (подтверждение удаления персонажа, 10 с).
//   (Таймеры 30000 мс TWOBUT_GAUGE — крафт/сплит, там полоса АВТО-ВЫПОЛНЯЕТ действие; таймеры меню
//   300000/60000 мс — отсчёт САМОГО меню, к полосе модалки отношения не имеют.)
static ASDA_DEV_CVAR(int32, CVarDeathConfirmGaugeMs,
	TEXT("asda.Death.ConfirmGaugeMs"), 10000,
	TEXT("Полоса msgbox воскрешения: мс до разблокировки кнопки ОК (0 = полосы нет, как в эталоне)"));

// ★★ 31.10.2026 «БИТВА АРМИЙ» (отзыв игроков: «resurrection from closest point needs a timer, resurrection in town is wrong, should say in
//   home base; click resurrect in town → kicked out of war mode»; разбор `Reports/campwar_bugs/RE_SPEC.md` §2).
static ASDA_DEV_CVAR(int32, CVarDeathCampLockHalf,
	TEXT("asda.Death.CampWarLockHalf"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Карта 19: блокировка кнопок 7/8 окна смерти = половина отсчёта (15/10/5 с при 30/20/10 по месту армии), "
	     "потом обе кнопки доступны; 0 — как оригинал (блокировка 30 с ≥ отсчёта — «ближайший лагерь» выбрать невозможно, всегда "
	     "авто-«база»). Решение владельца 31.10.2026."));
// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 15, баг 2: «после выбора „ближайший лагерь“ нет отсчёта, не знаю, сколько ждать»). Эталон: подпись
//   опции 7 статична (loc 2385, `DeathMenu_AddOption@0xD6D060`), отсчёт рисуется только на авто-опции 8 (`deathRespawnUpdateTimerLabel@0xD6DAF0`),
//   а клик 7 во время блокировки `[1].buffFxSlots[108]` молча игнорируется (ui_core.c:58472) — [ДЕФЕКТ ОРИГИНАЛА]: игрок жмёт и не видит,
//   когда кнопка заработает. Порт: пока идёт блокировка, к подписи 7 дописывается остаток в формате оригинала loc 2418 «[sd1] мин. [sd2] сек.»
//   (им эталон показывает оставшееся время, ui_core.c:82288); по концу блокировки — снова чистая 2385. Сервер режим 7 исполняет сразу
//   (`CharRegenVillageMove_1` → 4056), своего ожидания у него нет. 0 — как оригинал.
static ASDA_DEV_CVAR(int32, CVarDeathCampLockCountdown,
	TEXT("asda.Death.CampWarLockCountdown"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Карта 19: пока кнопка «ближайший лагерь» (7) заблокирована, на ней остаток блокировки (loc 2418); 0 — как оригинал (подпись без времени)."));
static ASDA_DEV_CVAR(int32, CVarDeathCampMenu,
	TEXT("asda.Death.CampWarMenu"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Карты 19/23: «вошли мёртвым» / рассинхрон 6001 — меню 7/8 вместо окна 2370 («в городе», 4054 режим 0 → 4055 "
	     "снимает режим войны), и свой 4053 закрывает висящее окно 2370; 0 — прежний порт (окно 2370)."));

// ★ 31.10.2026 [ОРИГИНАЛ] Ранг армии для отсчёта смерти на карте 19 (`DeathMenu_AddOption@0xD6D060`, дизасм 0xD6D437…0xD6D4D0):
//   армия A «обходит» B, если очков больше (`CCampWar+0xB9C+4f`, 6729), при равенстве — численность больше (`+0xBA8+2f`, 6742),
//   при равенстве и её — меньший номер армии. Ранг = сколько армий обходит моя: 2 → 30 с, 1 → 20 с, 0 → 10 с.
//   Армии нет (−1, оператор/вне боя) — эталонное умолчание 60 с.
static float AsdaCampDeathTimerSec(const UGameInstance* GI)
{
	const UAsdaCampWarBattleSubsystem* Bt = GI ? GI->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr;
	const int32 Me = Bt ? Bt->GetSelfArmy() : -1;
	if (Me < 0 || Me > 2) { return 60.f; }
	auto Beats = [Bt](int32 A, int32 B)
	{
		if (Bt->GetScore(A) != Bt->GetScore(B)) { return Bt->GetScore(A) > Bt->GetScore(B); }
		if (Bt->GetHeadcount(A) != Bt->GetHeadcount(B)) { return Bt->GetHeadcount(A) > Bt->GetHeadcount(B); }
		return A < B;
	};
	int32 Rank = 0;
	for (int32 F = 0; F < 3; ++F) { if (F != Me && Beats(Me, F)) { ++Rank; } }
	return (Rank >= 2) ? 30.f : (Rank == 1) ? 20.f : 10.f;
}

// ★★★ 05.11.2026 [ОРИГИНАЛ] Замок ввода мёртвого — разбор в `UI/AsdaDeathInputLock.h`.
static ASDA_DEV_CVAR(int32, CVarDeathInputLock,
	TEXT("asda.Death.InputLock"), 1,
	TEXT("[ОРИГИНАЛ] Пока открыто меню смерти (эталон CUIManager+0x44): из раскладки исполняется только команда 44 (снимок экрана), "
	     "хода нет, панель быстрого доступа (мышь и клавиши) молчит, кнопки системного меню открывают только 73406. "
	     "0 — прежний порт (заперт только ESC)."));

// ★★★ 05.11.2026 [ОРИГИНАЛ] Опции 3 и 5 меню смерти (раньше в порту не набирались: «нет модели инвентаря/буфф-листа» — устарело).
static ASDA_DEV_CVAR(int32, CVarDeathScrollOption,
	TEXT("asda.Death.ScrollOption"), 1,
	TEXT("[ОРИГИНАЛ] Опция 5 меню смерти: в сумке есть спец-предмет подтипа 48 (премиум-свиток воскрешения) → кнопка loc 2376, "
	     "её выбор применяет первый такой свиток тем же путём, что двойной клик (5450). 0 — прежний порт (опции нет)."));
static ASDA_DEV_CVAR(int32, CVarDeathResuscitateOption,
	TEXT("asda.Death.ResuscitateOption"), 1,
	TEXT("[ОРИГИНАЛ] Опция 3 меню смерти: на своём персонаже в момент смерти висит состояние 189 вида 0 (Resuscitate) → кнопка "
	     "loc 2381, выбор = 4054 режим 1. 0 — прежний порт (опции нет)."));

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] Мёртвому чат работает полностью — разбор у `kChatCmdFirst` в `UI/AsdaDeathInputLock.h`.
static ASDA_DEV_CVAR(int32, CVarDeathChatKeys,
	TEXT("asda.Death.ChatKeys"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Пока открыто меню смерти, работают и клавиши каналов чата (команды 67–73: быстрый ответ R, шёпот Ctrl+W, отряд, клан...). ")
	TEXT("0 — как эталон: мёртвому только Enter."));

bool AsdaDeathLock::IsAllowedCmd(int32 Cmd)
{
	if (Cmd == kAllowedCmd || Cmd == kAllowedChatCmd) { return true; }
	return CVarDeathChatKeys.GetValueOnGameThread() != 0 && Cmd >= kChatCmdFirst && Cmd <= kChatCmdLast;
}

bool AsdaDeathLock::LockEnabled()
{
	return CVarDeathInputLock.GetValueOnGameThread() != 0;
}

bool AsdaDeathLock::IsInputLocked(const UGameInstance* GI)
{
	if (!GI || !LockEnabled()) { return false; }
	const UAsdaAfterDeathSubsystem* D = GI->GetSubsystem<UAsdaAfterDeathSubsystem>();
	return D && D->IsDeathMenuOpen();
}

namespace
{
	// Подтип спец-предмета «премиум-свиток воскрешения» (эталон `Table2Record->subType == 48`).
	constexpr int32 kReviveScrollSubType = 48;
	// Тип состояния Resuscitate (эталон `listContainsByTypeId(состояния, 189, 0)`, entity_char.c:16894).
	constexpr int32 kResuscitateStateId = 189;

	// ★ Порт `invBagAutoUseFirstType48Item@0xD26740` / `inventoryCheckHasItemType48@0xD07350`: ПЕРВАЯ ячейка сумки
	//   (контейнер 1 = эталонный `g_InventoryBag`), где лежит СПЕЦ-предмет (`!ItemRecord && Table2Record`) подтипа 48.
	//   Эталон обходит 30 ячеек — весь `g_InventoryBag`; [ПОРТ] после P96 сумка 60 ячеек, обходим всю доступную
	//   (`GetContainerSize(1)` = эталонное «вся сумка»). −1 — свитка нет.
	int32 AsdaFindReviveScrollCell(const UGameInstance* GI, int32* OutCode = nullptr)
	{
		const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!Inv || !IT) { return -1; }
		const int32 N = Inv->GetContainerSize(1);
		for (int32 I = 0; I < N; ++I)
		{
			const FAsdaItemInstance* C = Inv->GetCell(1, I);
			if (!C || C->IsEmpty() || C->ItemCode <= 0) { continue; }
			if (IT->GetItem(C->ItemCode)) { continue; }   // обычный предмет (`ItemRecord` есть) — не свиток
			FAsdaSpecialItemlistRow Sp;
			if (IT->GetSpecialItem(C->ItemCode, Sp) && Sp.SubType == kReviveScrollSubType)
			{
				if (OutCode) { *OutCode = C->ItemCode; }
				return I;
			}
		}
		return -1;
	}

	// ★ Порт проверки `CChar_SetState` case 6: на своём персонаже висит Resuscitate (Id 189, вид 0).
	bool AsdaSelfHasResuscitate(const UGameInstance* GI)
	{
		const UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		if (!Eff) { return false; }
		for (const FAsdaEffectEntry& E : Eff->GetSelfList())
		{
			if (E.Id == kResuscitateStateId && E.Type == 0) { return true; }
		}
		return false;
	}
}

// Тип опции → loc-id подписи (эталонная таблица DeathMenu_AddOption).
int32 UAsdaAfterDeathSubsystem::LocForOption(int32 OptionType)
{
	switch (OptionType)
	{
	// ── Предложения от ДРУГОГО игрока (6093, эталон friendListAddEntry@0xD6D740) ──
	case 0: return 2372;   // «<имя> воскрешает вас» — полное HP/MP (server Type 0)
	case 1: return 2380;   // «<имя> воскрешает вас» — процент HP/MP (server Type 2)
	case 2: return 2371;   // воскрешение на 39 % HP/MP (server Type 3), без имени
	case 3: return 2381;   // воскреснуть здесь навыком Resuscitate
	case 4: return 2370;   // воскреснуть в городе (авто через 5 минут)
	case 5: return 2376;   // воскреснуть здесь премиум-свитком
	case 6: return 2383;   // воскреснуть на текущем месте
	case 7: return 2385;   // воскреснуть на ближайшей занятой точке
	case 8: return 2386;   // кампвар-респаун (подпись перекрывается отсчётом 108107)
	default: return -1;
	}
}

void UAsdaAfterDeathSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		HDeath  = Net->OnCharDeath.AddUObject(this, &UAsdaAfterDeathSubsystem::OnCharDeath);
		HRevive = Net->OnCharRevived.AddUObject(this, &UAsdaAfterDeathSubsystem::OnCharRevived);
		HOffer  = Net->OnResurrectOffer.AddUObject(this, &UAsdaAfterDeathSubsystem::OnResurrectOffer);
	}
}

void UAsdaAfterDeathSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnCharDeath.Remove(HDeath);
			Net->OnCharRevived.Remove(HRevive);
			Net->OnResurrectOffer.Remove(HOffer);
		}
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaAfterDeathSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Меню «после смерти» и убийца — состояние конкретной смерти в конкретном мире.
	// ★ Волна В6 ремесла (ревизия В5 МЕЛК-1): своё окно-сообщение при сбросе сессии закрывается МОЛЧА (без 5) —
	//   у эталона такого закрытия нет, клиент перезагружается. `CloseMenu` ниже его уже не найдёт.
	CloseModal(/*bSilent*/ true);
	CloseMenu();   // ★ (проверено В2-B дедупа №2): `CloseMenuWidget` уже снимает After_Death_Win с реестра — пересборки поверх старого дерева нет
	ResetWidget();
	bMenuOpen = false;
	LastKillerIdx = -1;
	(void)Reason;
}

void UAsdaAfterDeathSubsystem::ResetWidget()
{
	Widget = nullptr;
	SlotBtn.Reset();
	SlotTxt.Reset();
	BgBase.Reset();                 // геометрия фона снимается заново с нового экземпляра виджета
	TopBase.Reset();                // базовые offset.Top топ-виджетов — тоже с нового экземпляра
	BgBaseTop = FLT_MAX;
	BgBaseHeight = 0.f;
}

// ── 4053: смерть ───────────────────────────────────────────────────────────────────────────────
void UAsdaAfterDeathSubsystem::OnCharDeath(int32 UserIdx, int32 KillerIdx)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx))
	{
		return;   // чужая смерть — анимация на акторе соседа (мир), меню не наше
	}
	ASDA_CTRACE(EAsdaCT::Death, TEXT("4053 СВОЯ СМЕРТЬ: userIdx=%d убийца userIdx=%d, карта %d, вошли мёртвым=%s, меню было открыто=%s"),
		UserIdx, KillerIdx, Net->EnterMapId, Net->bSelfDeathNoClip ? TEXT("да (модалка 2370)") : TEXT("нет (окно выбора 25000)"),
		bMenuOpen ? TEXT("да") : TEXT("нет"));
	LastKillerIdx = KillerIdx;

	// Эталон Recv_CharDeath_Apply (network.c:42597-42605): сброс прежнего меню и закрытие окон — ИХ ИГРОВЫМИ точками.
	//   ★ Волна В6 ремесла (W5-7): раньше здесь стояли голые `CloseWindow` окон `SHOP_WIN` (такого окна в порту нет),
	//   `HAIRSHOP_WIN` и `BOOSTER_SHOP_WIN` мимо подсистем — их состояние «открыто» оставалось. Теперь ровно ветки эталона:
	//     `if (g_pPartyTargetObj) uiInventoryResetAndLeave(g_pShopWnd)` — миснейм: адрес 0x2BE779C = поле 2075 магазина,
	//        «открыт ЭЛИТНЫЙ магазин 6000» (разбор — `AsdaNpcDialogSubsystem.cpp`, гейт 2 разговора);
	//     `if (CObject_GetField166(g_pHairShopWnd)) uiLeaveAndResetAllWindows(g_pHairShopWnd)` — парикмахерская 15000;
	//     `if (CObject_GetField254(g_pInventoryWnd)) tagBoosterShop_CloseWindow(g_pInventoryWnd)` — это барабан гачи
	//        10050 (`boosterApplyResult@0xD1F830`, имя врёт), а НЕ магазин бустеров 15300. Магазин 15300 эталон закрывает
	//        в 5045 (`boosterShopCloseWindow`, ui_core.c:61165) — у порта это `UAsdaNpcDialogSubsystem` (хвост смерти).
	//   `uiInventoryBag_ShowFadeIn` (ui_core.c:76075, плавный показ `g_InventoryBag`) — отдельного эффекта в порту нет.
	if (UGameInstance* GI = GetGameInstance())
	{
		int32 Closed = 0;
		if (UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>())
		{ if (Ps->IsOpen()) { Ps->CloseWindow(); ++Closed; } }
		if (UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>())
		{ if (Hs->IsSessionOpen()) { Hs->CloseWindow(); ++Closed; } }
		if (UAsdaBoosterPanelSubsystem* Bp = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
		{ if (Bp->IsWindowOpen()) { Bp->ApplyResultAndClose(); ++Closed; } }
		UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] Recv_CharDeath_Apply: закрыто окон подсистемами %d (6000/парикмахерская/барабан 10050)"), Closed);
	}
	Options.Reset();
	AutoOption = -1;
	RespawnTimerSec = 0.f;
	ClickLockSec = 0.f;
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Death.CampWarMenu`): окно 2370, поднятое страховкой порта по рассинхрону 6001 причина 1, НЕ
	//   закрывалось пришедшим следом 4053 — игрок жал в нём «ОК» = 4054 режим 0, сервер `CharRegenVillage` отвечал 4055, и клиент
	//   (`netHandler_EnterWorld` при +0x2388 == 1) снимал HUD боя: «выкинуло из режима войны, а карта та же» (отзыв игроков).
	//   Своя смерть начинается заново (эталон `deathMenuWnd_reset`) — висящая 2370 снимается молча.
	const bool bCampMenuFix = CVarDeathCampMenu.GetValueOnGameThread() != 0;
	const int32 DeathMapId = Net->EnterMapId;
	const bool bCampMap = (DeathMapId == AsdaMapRegistry::kBattlefield || DeathMapId == AsdaMapRegistry::kMineWar);
	if (bCampMenuFix) { CloseModal(/*bSilent*/ true); }   // снимает только свою пару (0, 2370), если она есть

	// ★★★ ДВА РАЗНЫХ UI, А НЕ ОДИН (найдено при сверке со скриншотами оригинала):
	//   • ГИБЕЛЬ В ИГРЕ (4053, состояние 6) → окно ВЫБОРА After_Death_Win 25000 (Recv_CharDeath_Apply).
	//   • ВОШЁЛ В МИР УЖЕ МЁРТВЫМ (состояние 29) → ОДНОКНОПОЧНАЯ МОДАЛКА, эталон game_main.c:3464:
	//       `if (localPlayer->effectSlots[112].field_8 == 29) { CItem_BuildTooltipText(ui, 0, 2370, null, 0);
	//         g_GameState = 8; }` — msgClass 0 = one-button msgbox (кнопка loc 2288 «OK»), текст loc 2370
	//       «Немедленно воскреснуть в городе. Через 5 мин. …». Никакого выбора там НЕТ.
	//       ОК → ui_core.c:56981 `case 0x942` (0x942 = 2370): `Send_RespawnRequest_thunk(0)` = 4054 режим 0
	//       (город) + снятие флага 0x4000 + обнуление отсчёта.
	//       Та же модалка при ОБРЫВЕ СВЯЗИ мёртвым, если нет свитка (network.c:769/1318:
	//       `sub_91FE69(LocalPlayer) && !inventoryCheckHasItemType48()`).
	//   Состояние 29 ставится по КОНЦУ клипа гибели: CChar_UpdateState32Anim@0xA17A70 —
	//     `if (Game_IsPausedOrMenu(pModelObj+448)) { CChar_SetState(this, 29,0,0); g_GameState = 8; }`.
	//   Наш признак «вошли уже мёртвыми» = UAsdaNetSubsystem::bSelfDeathNoClip (тот же, что гасит клип).
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Death.CampWarMenu`): на картах 19/23 вместо окна 2370 — меню 7/8 (режимы 7/8 отвечаются 4056,
	//   режим войны сохраняется). [ДЕФЕКТ ОРИГИНАЛА]: эталон и здесь показывает 2370 (game_main.c:3464) → режим 0 → выход из боя.
	if (Net->bSelfDeathNoClip && bCampMenuFix && bCampMap)
	{
		UE_LOG(LogAsdaDeath, Warning, TEXT("[смерть] вошли/оказались мёртвым на карте %d → меню 7/8 вместо окна 2370 (режим 0 снял бы режим войны)"), DeathMapId);
	}
	else if (Net->bSelfDeathNoClip)
	{
		bMenuOpen = true;
		AutoOption = 4;              // отсчёт модалки — 300 000 мс, по нулю тот же 4054 режим 0
		RespawnTimerSec = 300.f;
		ShowEntryDeadModal();
		ASDA_CTRACE(EAsdaCT::Death, TEXT("вошли в мир мёртвым → однокнопочная модалка loc 2370 (ОК = 4054 режим 0), авто через 300 с; модалка показана=%s"),
			Modal ? TEXT("да") : TEXT("нет"));
		UE_LOG(LogAsdaDeath, Warning,
			TEXT("[смерть] вошли в мир мёртвым → ОДНОКНОПОЧНАЯ МОДАЛКА (loc 2370, ОК = 4054 режим 0), авто через 300 с"));
		return;
	}

	BuildOptionSet();
	bMenuOpen = true;

	if (!EnsureWidget())
	{
		ASDA_CTRACE(EAsdaCT::Death, TEXT("окно после смерти НЕ показано: WBP_After_Death_Win не собран (опций %d, авто-опция %d через %.0f с — отсчёт идёт)"),
			Options.Num(), AutoOption, RespawnTimerSec);
		return;
	}
	RefreshSlots();
	ASDA_CTRACE(EAsdaCT::Death, TEXT("окно после смерти ПОКАЗАНО (After_Death_Win 25000): опций %d, авто-опция %d через %.0f с, блок клика 7/8 %.0f с; до 4054 сервер отклоняет действия (6001 причина 1)"),
		Options.Num(), AutoOption, RespawnTimerSec, ClickLockSec);

	UE_LOG(LogAsdaDeath, Warning,
		TEXT("[смерть] персонаж убит (убийца userIdx=%d). Опций меню: %d, авто-опция %d через %.0f с. "
		     "До 4054 сервер отклоняет ВСЕ действия (6001 причина 1)."),
		KillerIdx, Options.Num(), AutoOption, RespawnTimerSec);
}

// ── ★ ОДНОКНОПОЧНАЯ МОДАЛКА «вошёл в мир мёртвым» (эталон one-button msgbox, JUI ONEBUT_MSGBOX) ──
//   ★★★ СВЕДЕНО (`fix_helpers_dedup.md`): раньше это окно собиралось ЗДЕСЬ вручную — 4-я копия
//   сборки `ONEBUT_MSGBOX` мимо общей точки `UAsdaWindowSubsystem::ShowGameMessage`. Она звала
//   `OnClicked.Clear()` на ОБЩЕМ `ONEBUT_MSGBOX` и вешала свой обработчик, СТИРАЯ реле-владельца
//   этой общей точки (порча чужого состояния: любой последующий `ShowGameMessage` терял кнопку,
//   а нас — терял тот). Теперь окно строит общая точка, а специфика смерти приходит ей параметрами:
//     • текст loc 2370 («воскреснуть в городе, авто-возрождение через 5 минут»),
//     • подпись ОК loc 2288 (`AsdaLoc::kBtnOk`),
//     • действие ОК — воскрешение 4054 режим 0 (колбэк `OnOk`; окно закрывает сама общая точка),
//     • z = 800 (`kDeathZ`, поверх HUD и нотисов).
//   Полосу `ONEBUT_GAUGE*` общая точка прячет сама (ветка kind 0 эталона, ui_core.c:55951).
void UAsdaAfterDeathSubsystem::ShowEntryDeadModal()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было `if (!WM) return;` — без оконной подсистемы модалка терялась МОЛЧА;
	//   стало общая политика `AsdaNotice::MsgBoxCb` (окна нет ⇒ текст в ленту той же категорией ItemRefused).
	if (WM) { CloseMenuWidget(); }   // окно выбора в этом сценарии не показываем

	const FString Msg = AsdaNotice::Loc(GI, 2370);   // = прежний `GetTextById` (теги срезаны)
	// ★ ОК модалки = воскрешение (4054 режим 0). GC-safe: подсистема (this) корневая, лямбда живёт в реле,
	//   которое держит `UAsdaWindowSubsystem`. Закрытие делает общая точка после колбэка.
	// ★ Очередь окон-сообщений (26.09): эталонная пара (0, 2370) — `CItem_BuildTooltipText(ui, 0, 2370, …)`
	//   (game_main.c:3466); повторный показ, пока она в очереди, отбрасывается.
	//   ★ Редакция 1 (ревизия С-2): отброшенный дубль возвращает nullptr — указатель НЕ затираем: модалка уже в
	//   очереди (снимается по паре в `CloseModal`), а прежний `Modal = nullptr` оставлял её у живого персонажа.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10d): заглушка пропавшей строки была «loc 2370» — стала единая «[#2370]».
	UUserWidget* Shown = AsdaNotice::MsgBoxCb(GI, EAsdaMsgCategory::ItemRefused, Msg.IsEmpty() ? FAsdaLang::MissingTag(2370) : Msg,
		[this]() { OnModalOk(); }, kDeathZ, AsdaLoc::kBtnOk, /*EtalonNoticeId*/2370);
	if (!Shown)
	{
		if (!WM)
		{
			UE_LOG(LogAsdaDeath, Warning, TEXT("[смерть] нет UAsdaWindowSubsystem — текст модалки (0, 2370) ушёл в ленту ItemRefused"));
			return;
		}
		if (WM->IsMsgBoxQueued(0, 2370))
		{
			UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] модалка (0, 2370) уже в очереди — повторный показ отброшен, как эталон"));
			return;
		}
		UE_LOG(LogAsdaDeath, Error, TEXT("[смерть] ONEBUT_MSGBOX не показан — модалку показать нечем"));
		return;
	}
	Modal = Shown;
	UE_LOG(LogAsdaDeath, Warning, TEXT("[смерть] модалка (через общую точку ShowGameMessage): loc2370='%s' (len=%d), ОК=loc 2288 → 4054 режим 0, z=%d"),
		*Msg, Msg.Len(), kDeathZ);
}

// ОК модалки = эталон ui_core.c:56981 case 0x942 → 4054 режим 0 (город).
//   ⚠ Эталон сперва проверяет свиток (sub_933455 → invBagAutoUseFirstType48Item) — зависимость «инвентарь».
//   ★ Вызывается колбэком `OnOk` из `ShowGameMessage`: САМО ОКНО закрывает общая точка (после этого
//     колбэка), поэтому здесь `CloseWindow` НЕ зовём (иначе двойное закрытие во время диспетчеризации
//     клика) — только шлём 4054 и обнуляем указатель на окно, чтобы состояние подсистемы сошлось.
void UAsdaAfterDeathSubsystem::OnModalOk()
{
	UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] ОК модалки → 4054 режим 0 (воскрешение в городе привязки)"));
	ASDA_CTRACE(EAsdaCT::Input, TEXT("ОК модалки «вошёл мёртвым» (loc 2370) → опция 4 (город)"));
	SelectOption(4);
	Modal = nullptr;
}

// ★ Волна В6 ремесла (ревизия В5 МЕЛК-1): закрывается ТОЛЬКО своё окно — `ONEBUT_MSGBOX` общий, и если в нём уже чужое
//   сообщение (модалка смерти закрыта ОК или заменена), его не трогаем. Звук 5 — общая точка `CloseWindow`
//   (`CGameUI_CloseWindow` эталона играет его при любом закрытии); при сбросе сессии — молча.
//   ★★★ Очередь окон-сообщений (26.09): модалка смерти — запись (0, 2370) очереди, а эталон убирает её при входе
//   своего персонажа на карту `uiCloseOnExchangeItemType@0xD79D30` (entity_char.c:53192, ui_core.c:58576) — МОЛЧА:
//   без 4 и без 5 (прежний звук 5 здесь был отступлением). Чужие записи очереди не трогаются.
//   ★ Редакция 1 (ревизия С-2): снимаем ПО ПАРЕ (0, 2370), а не по указателю — указатель мог не встать (дубль).
//   Флаг смерти 0x4000 и отсчёт `field_32DE4`, которые эталон обнуляет там же (ui_core.c:58600-58605), в порту —
//   `bMenuOpen` и `RespawnTimerSec`: их обнуляет `CloseMenu`, из которого этот вызов и идёт.
void UAsdaAfterDeathSubsystem::CloseModal(bool bSilent)
{
	Modal = nullptr;
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !WM->IsMsgBoxQueued(0, 2370)) { return; }
	// bSilent = сброс сессии: при смене карты виджеты уже снесены травелом — гейт видимости эталона неприменим.
	WM->CloseDeathMsgBoxOnMapEntry(/*bRequireVisible*/!bSilent);
}

// Набор опций 1:1 по Recv_CharDeath_Apply (network.c:42588-42620).
void UAsdaAfterDeathSubsystem::BuildOptionSet()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 Level = ResolveSelfLevel();

	FString Diag;
	// ── ★ 05.11.2026 [ОРИГИНАЛ] Опция 3 (Resuscitate, `asda.Death.ResuscitateOption`): эталон `CChar_SetState` case 6
	//   (entity_char.c:16894) взводит `effectSlots[112].field_48`, если в списке состояний персонажа есть 189 вида 0, —
	//   ДО `charClearAllSkillEffects`. Порт на 4053 список состояний не трогает (их снимает сервер своими пакетами),
	//   поэтому читаем его здесь же, на приёме своей смерти. Выбор → 4054 режим 1 (сервер: навык 189 → на месте).
	if (CVarDeathResuscitateOption.GetValueOnGameThread() != 0 && AsdaSelfHasResuscitate(GetGameInstance()))
	{
		AddOption(3);
		Diag += TEXT("опция3=да (состояние 189) ");
	}
	else { Diag += TEXT("опция3=нет ");  }
	// ── ★ 05.11.2026 [ОРИГИНАЛ] Опция 5 (премиум-свиток, `asda.Death.ScrollOption`): эталон `inventoryCheckHasItemType48@0xD07350`
	//   (зовёт `Recv_CharDeath_Apply`) — спец-предмет подтипа 48 в сумке → `DeathMenu_AddOption(5)` (loc 2376). Раньше порт
	//   опцию не набирал («модели инвентаря нет» — давно неправда), и свиток можно было применить только двойным кликом.
	{
		int32 ScrollCode = 0;
		const int32 Cell = (CVarDeathScrollOption.GetValueOnGameThread() != 0) ? AsdaFindReviveScrollCell(GetGameInstance(), &ScrollCode) : -1;
		if (Cell >= 0)
		{
			AddOption(5);
			Diag += FString::Printf(TEXT("опция5=да (свиток %d в ячейке %d) "), ScrollCode, Cell);
		}
		else { Diag += TEXT("опция5=нет "); }
	}

	// ★★ КАМПВАР-КАРТЫ. Эталон Recv_CharDeath_Apply сравнивает getField3036(&g_pLocalPlayer) с 19 и 23 —
	//   это mapId ТЕКУЩЕЙ карты, а не «тип» (доказательство: в map_table.csv признак campwar/PK (+69)
	//   равен 1 ровно у двух карт — 19 «battlefield» и 23 «mine»; других значений в таблице нет).
	//     mapId 19 → опции 7 и 8;  mapId 23 → ТОЛЬКО 8;  иначе → 4 и (level ≤ 20) 6.
	const int32 MapId = Net ? Net->EnterMapId : -1;
	if (MapId == AsdaMapRegistry::kBattlefield || MapId == AsdaMapRegistry::kMineWar)
	{
		if (MapId != AsdaMapRegistry::kMineWar) { AddOption(7); Diag += TEXT("опция7=да "); }
		else             { Diag += TEXT("опция7=нет (карта 23) "); }
		AddOption(8); Diag += TEXT("опция8=да ");
	}
	else
	{
		AddOption(4);                        // воскреснуть в городе (авто через 5 мин)
		Diag += TEXT("опция4=да ");
		if (Level > 0 && Level <= 20)
		{
			AddOption(6);                    // «Воскреснуть в этой области» — привилегия низких уровней
			Diag += TEXT("опция6=да ");
		}
		else
		{
			Diag += FString::Printf(TEXT("опция6=НЕТ (уровень %d %s) "), Level,
				(Level <= 0) ? TEXT("НЕИЗВЕСТЕН") : TEXT("> 20"));
		}
	}
	// ★ Диагностика набора: юзер должен видеть, ПОЧЕМУ опции нет (требование живого теста).
	UE_LOG(LogAsdaDeath, Warning,
		TEXT("[смерть] сбор опций: level=%d (SelfLevel=%d, char-запись=%d) mapId=%d | %s| ИТОГО %d"),
		Level, Net ? (int32)Net->SelfLevel : -1, CharListLevel(), MapId, *Diag, Options.Num());
}

// ★★★ УРОВЕНЬ ПЕРСОНАЖА ДЛЯ ГЕЙТА ОПЦИИ 6 — тем же путём, что и HUD (UAsdaHudSubsystem::ResolveLevelExp):
//   рантайм-поле `Net->SelfLevel` (сид 4005-self @+40, далее 4036 @+2), а если оно ещё 0 —
//   ЗАПИСЬ CHAR-SELECT (0x3FF rec+26).
//   ЖИВОЙ ТЕСТ 2026-08-03: персонаж 15 уровня получил ОДНУ опцию вместо двух, потому что читали
//   ТОЛЬКО `Net->SelfLevel`, а он был 0 (4005-self с уровнем не пришёл); HUD в той же сессии писал
//   `[EXP] level=15` именно из фолбэка на char-запись. Оригинал в этом же случае даёт две опции и 60 с.
int32 UAsdaAfterDeathSubsystem::ResolveSelfLevel() const
{
	// ★ Волна 1 (14.09.2026, В9): общая точка `AsdaSelf::GetLevel` — рантайм-поле, иначе запись ВЫБРАННОГО персонажа (как было).
	return AsdaSelf::GetLevel(GetGameInstance());
}

// Уровень из записи выбранного персонажа char-select (фолбэк, когда рантайм-поле ещё не сидировано).
int32 UAsdaAfterDeathSubsystem::CharListLevel() const
{
	// ★ Волна 1 (14.09.2026, В9): общая точка `AsdaSelf::FindSelectedCharRecord` (то же правило выбранного слота).
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaCharRecord* R = AsdaSelf::FindSelectedCharRecord(Net);
	return R ? (int32)R->Level : 0;
}

// ── Эталон DeathMenu_AddOption@0xD6D060 (ui_core.c:56139) ───────────────────────────────────────
//   Дедуп по типу, лимит 6, текст из loc, подгон высоты панели (66·n + 10) и, главное, ТАЙМЕРЫ:
//     • тип 4 → field_32DE4 = 300 000 мс (5 мин) и он же становится авто-опцией;
//     • ЛЮБОЙ другой тип → field_32DE4 = 60 000 мс по умолчанию; по рангу нации 30 000 / 20 000 / 10 000;
//       на карте 23 — 30 000; и ВСЕГДА [1].buffFxSlots[108] = 30 000 (блокировка ручного клика 7/8).
//     • «авто-опцией» (v16[84]/v16[3253]) помечаются ТОЛЬКО типы 4, 7 и 8.
//   ⚠ Следствие, которое порт раньше терял: при level ≤ 20 после AddOption(4) идёт AddOption(6) —
//     и отсчёт становится 60 с, а не 5 мин, хотя авто-опцией остаётся 4 (город).
//   ⚠ Ранг нации (g_aRankCompare/g_aRankTiebreak — таблица очков трёх наций) в порту не поднят:
//     используем эталонный ДЕФОЛТ 60 000 мс; карта 23 обрабатывается точно (30 000).
void UAsdaAfterDeathSubsystem::AddOption(int32 OptionType, int32 CasterUserIdx, int32 Percent)
{
	// Дедуп: собственные типы — по типу; предложения 0/1/2 — по паре (тип, инициатор) (эталон).
	for (const FAsdaDeathOption& O : Options)
	{
		if (O.Type != OptionType) { continue; }
		if (OptionType > 2 || O.CasterUserIdx == CasterUserIdx) { return; }
	}
	if (Options.Num() >= kMaxOptions) { return; }

	FAsdaDeathOption Opt;
	Opt.Type = OptionType;
	Opt.CasterUserIdx = CasterUserIdx;
	Opt.Percent = Percent;
	Options.Add(Opt);

	if (OptionType == 4)
	{
		AutoOption = 4;
		RespawnTimerSec = 300.f;             // 300 000 мс — ровно как в тексте loc 2370 («5 минут»)
	}
	else
	{
		const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const int32 MapId = Net ? Net->EnterMapId : -1;
		// ★ 31.10.2026 [ОРИГИНАЛ]: на карте 19 отсчёт — по рангу армии 30/20/10 с (`AsdaCampDeathTimerSec`); прежде всегда 60 с
		//   («ранг в порту не поднят» — поднят 30.10 вместе с боем: очки 6729, численность 6742). Карта 23 — 30 с, прочие — 60 с.
		RespawnTimerSec = (MapId == AsdaMapRegistry::kMineWar) ? 30.f
			: (MapId == AsdaMapRegistry::kBattlefield) ? AsdaCampDeathTimerSec(GetGameInstance()) : 60.f;
		ClickLockSec = 30.f;                 // эталон: [1].buffFxSlots[108] = 30 000 при ЛЮБОМ типе ≠ 4
		// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Death.CampWarLockHalf`, решение владельца): на карте 19 блокировка 30 с ≥ отсчёта 10…30 с —
		//   «ближайший лагерь» (7) у оригинала выбрать невозможно, всегда авто-«база». Блокировка = половина отсчёта (15/10/5 с).
		if (MapId == AsdaMapRegistry::kBattlefield && CVarDeathCampLockHalf.GetValueOnGameThread() != 0)
		{
			ClickLockSec = RespawnTimerSec * 0.5f;
		}
		if (OptionType == 7 || OptionType == 8) { AutoOption = OptionType; }
	}
}

// ── ★ 6093: предложение воскрешения от другого игрока (эталон friendListAddEntry@0xD6D740) ──────
void UAsdaAfterDeathSubsystem::OnResurrectOffer(int32 CasterUserIdx, uint8 Type, int32 Percent)
{
	// Серверный Type → индекс записи меню: 0 → 0, 2 → 1, 3 → 2 (эталон `--BYTE2(a2)` для 2 и 3).
	int32 OptType = -1;
	if (Type == 0)      { OptType = 0; }
	else if (Type == 2) { OptType = 1; }
	else if (Type == 3) { OptType = 2; }
	else
	{
		UE_LOG(LogAsdaDeath, Warning, TEXT("[смерть] 6093: неизвестный Type=%u — опция не добавлена"), Type);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("6093 предложение воскрешения от userIdx=%d отброшено: неизвестный Type=%d"), CasterUserIdx, (int32)Type);
		return;
	}
	ASDA_CTRACE(EAsdaCT::Recv, TEXT("6093 ПРЕДЛОЖЕНИЕ ВОСКРЕШЕНИЯ от userIdx=%d: серверный Type=%d → опция %d, %d%% HP/MP, меню было открыто=%s"),
		CasterUserIdx, (int32)Type, OptType, Percent, bMenuOpen ? TEXT("да") : TEXT("нет"));

	bMenuOpen = true;                        // эталон показывает панель, даже если её ещё не открывали
	AddOption(OptType, CasterUserIdx, Percent);
	// Отсчёт предложения — 300 000 мс (эталон friendListAddEntry ставит field_32DE4 = 5 мин).
	RespawnTimerSec = 300.f;
	if (!EnsureWidget()) { return; }
	RefreshSlots();
	UE_LOG(LogAsdaDeath, Warning, TEXT("[смерть] предложение воскрешения от userIdx=%d (тип %d, %d%%) — опция %d"),
		CasterUserIdx, (int32)Type, Percent, OptType);
}

bool UAsdaAfterDeathSubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(Widget)) { return true; }
	ResetWidget();

	Widget = WM->OpenWindowByPath(FName(TEXT("After_Death_Win")), kDeathWbp, kDeathZ,
	                              ESlateVisibility::SelfHitTestInvisible);
	if (!Widget || !Widget->WidgetTree)
	{
		UE_LOG(LogAsdaDeath, Error, TEXT("[смерть] WBP_After_Death_Win не загрузился (%s)"), kDeathWbp);
		return false;
	}

	// Подписи Txt0..5 и прозрачные кнопки-реле над Choice0..5 (планы = визуал, кнопка = ввод;
	//   каждая кнопка получает hover/pressed от своего скина Resize_BlackBtn — закон ui-button-hover-click).
	SlotBtn.SetNum(kMaxOptions);
	SlotTxt.SetNum(kMaxOptions);
	UCanvasPanel* Root = Cast<UCanvasPanel>(Widget->WidgetTree->RootWidget);

	for (int32 i = 0; i < kMaxOptions; ++i)
	{
		const FString TxtName = FString::Printf(TEXT("After_Death_Txt%d"), i);
		SlotTxt[i] = Widget->GetWidgetFromName(FName(*TxtName));
		if (UTextBlock* Tb = Cast<UTextBlock>(SlotTxt[i]))
		{
			// ★ 1:1 по JUI (EXS After_Death_Txt: TextAlign 5, TextColor FFFFFFFF, rect 360×42):
			//   центр/центр + перенос по авторской ширине 360; цвет — БЕЛЫЙ из EXS (импортёр брал
			//   CaptionInfo ff400040 — тёмно-пурпурный, это цвет ПОДПИСИ-caption, а не текста).
			Tb->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UAsdaWindowSubsystem::CenterTextBlockInSlot(Tb, /*WrapW*/360.f, FVector2D(0.f, -1.f));   // TextPos "0 -1"
			UE_LOG(LogAsdaDeath, Log,
				TEXT("[смерть] подпись слота %d: шрифт %.0f (единый кегль оригинала, cap 8 px), центр/центр, wrap=360, цвет FFFFFFFF"),
				i, Tb->GetFont().Size);
		}

		// Кнопка Choice{i} импортирована как StyledButton (Resize_BlackBtn = PushButton) — используем её.
		const FString BtnName = FString::Printf(TEXT("After_Death_Choice%d"), i);
		SlotBtn[i] = Cast<UButton>(Widget->GetWidgetFromName(FName(*BtnName)));
		if (!SlotBtn[i] && Root)
		{
			// Скин не дал интерактива (план-картинка) — кладём прозрачное реле на rect слота из JUI.
			UButton* B = NewObject<UButton>(Widget, UButton::StaticClass(), FName(*(BtnName + TEXT("_OPT"))));
			FButtonStyle S;
			S.Normal = FSlateNoResource(); S.Hovered = FSlateNoResource(); S.Pressed = FSlateNoResource();
			S.NormalPadding = FMargin(0); S.PressedPadding = FMargin(0);
			B->SetStyle(S);
			if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(B))
			{
				const float Y = 116.2f + kSlotStep * (float)i;
				CS->SetAnchors(FAnchors(322.f / 1024.f, Y / 768.f, (322.f + 380.f) / 1024.f, (Y + 52.f) / 768.f));
				CS->SetOffsets(FMargin(0, 0, 0, 0));
				CS->SetZOrder(20);
			}
			SlotBtn[i] = B;
		}
	}

	if (SlotBtn.IsValidIndex(0) && SlotBtn[0]) { SlotBtn[0]->OnClicked.AddDynamic(this, &UAsdaAfterDeathSubsystem::OnSlot0); }
	if (SlotBtn.IsValidIndex(1) && SlotBtn[1]) { SlotBtn[1]->OnClicked.AddDynamic(this, &UAsdaAfterDeathSubsystem::OnSlot1); }
	if (SlotBtn.IsValidIndex(2) && SlotBtn[2]) { SlotBtn[2]->OnClicked.AddDynamic(this, &UAsdaAfterDeathSubsystem::OnSlot2); }
	if (SlotBtn.IsValidIndex(3) && SlotBtn[3]) { SlotBtn[3]->OnClicked.AddDynamic(this, &UAsdaAfterDeathSubsystem::OnSlot3); }
	if (SlotBtn.IsValidIndex(4) && SlotBtn[4]) { SlotBtn[4]->OnClicked.AddDynamic(this, &UAsdaAfterDeathSubsystem::OnSlot4); }
	if (SlotBtn.IsValidIndex(5) && SlotBtn[5]) { SlotBtn[5]->OnClicked.AddDynamic(this, &UAsdaAfterDeathSubsystem::OnSlot5); }
	return true;
}

// Разложить набранные опции по слотам; лишние слоты скрыть (эталон ресайзит панель под 66·n+10).
void UAsdaAfterDeathSubsystem::RefreshSlots()
{
	if (!Widget) { return; }
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	for (int32 i = 0; i < kMaxOptions; ++i)
	{
		const bool bUsed = Options.IsValidIndex(i);
		// ★★ ДИНАМИКА: гасим/показываем ВСЕ виджеты слота (кнопка + её планы + подпись + подложка).
		//   Импортёр даёт планам имена «{idc}_{idx}_{plane}» (jui_positions.tsv: After_Death_BtnBase0_3),
		//   поэтому единственный надёжный способ — ПРЕФИКС (закон jui-importer-widget-naming).
		//   ⚠ Именно этого не хватало: раньше искали ровно «After_Death_BtnBase0» — не находили, и в окне
		//     оставались 5 пустых серых рамок (скриншот юзера).
		SetSlotWidgetsVisible(i, bUsed);
		if (SlotTxt.IsValidIndex(i) && SlotTxt[i])
		{
			if (bUsed)
			{
				const int32 Loc = LocForOption(Options[i].Type);
				FString S = (Txt && Loc > 0) ? Txt->GetTextById(Loc) : FString();
				if (S.IsEmpty()) { S = FAsdaLang::MissingTag(Loc); }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10d): было «loc N», стало единая «[#N]»
				// Опции 0 и 1 (предложение от игрока): в текст подставляется ИМЯ инициатора
				//   (эталон CObject_SetSlotString(g_pLocNoticeCtx, 0, имя) перед UI_FormatLocalizedNotice).
				if (Options[i].Type == 0 || Options[i].Type == 1)
				{
					FString Name;
					if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
					{
						if (AActor* A = WS->ActorByUserIdx(Options[i].CasterUserIdx))
						{
							if (UAsdaNameplateComponent* NP = A->FindComponentByClass<UAsdaNameplateComponent>())
							{
								Name = NP->DisplayName;
							}
						}
					}
					if (Name.IsEmpty()) { Name = FString::Printf(TEXT("#%d"), Options[i].CasterUserIdx); }
					S.ReplaceInline(TEXT("[ss1]"), *Name, ESearchCase::CaseSensitive);
					S.ReplaceInline(TEXT("[sd1]"), *Name, ESearchCase::CaseSensitive);
				}
				SetSlotLabel(i, S);
			}
		}
	}
	// ★★ Ужать фон окна под ЧИСЛО опций — ровно как эталон DeathMenu_AddOption:
	//   `SetSize(width, 66 * n + 10)` (проверка: 6 опций → 66·6+10 = 406 = высота окна в JUI-дампе).
	ApplyDynamicLayout(Options.Num());
	AsdaUi::SetShownDecor(Widget, bMenuOpen);
	RefreshTimerLabel();
}

// ── ★ Видимость ВСЕХ виджетов слота по префиксам имён ───────────────────────────────────────────
void UAsdaAfterDeathSubsystem::SetSlotWidgetsVisible(int32 SlotIdx, bool bVisible)
{
	if (!Widget || !Widget->WidgetTree) { return; }
	const FString P1 = FString::Printf(TEXT("After_Death_Choice%d"), SlotIdx);
	const FString P2 = FString::Printf(TEXT("After_Death_Txt%d"), SlotIdx);
	const FString P3 = FString::Printf(TEXT("After_Death_BtnBase%d"), SlotIdx);

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	int32 Hit = 0;
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (!N.StartsWith(P1) && !N.StartsWith(P2) && !N.StartsWith(P3)) { continue; }
		++Hit;
		if (!bVisible) { W->SetVisibility(ESlateVisibility::Collapsed); continue; }
		// Показ: интерактив кликабелен, всё остальное (планы/подписи) мышь не перехватывает
		//   (закон ui-button-hover-click-rule: подписи SelfHitTestInvisible, иначе у кнопки нет hover).
		// ★★★ ДЕДУПЛИКАЦИЯ 2026-09-02. Здесь была своя копия правила видимости (в форме `if/else`,
		//   поэтому её не находил поиск по тернарнику). Правило одно на весь порт —
		//   `AsdaCommunityUi::ShownVisibilityFor`, откат `asda.UI.InteractiveHitTest 0`.
		//   ⚠ ПОВЕДЕНИЕ НЕ МЕНЯЕТСЯ: в разметке `After_Death_Win` (25000, 19 узлов) только
		//   `PushButton` 6, `Text` 7 и `ResizeWindow` 6 — ни полей ввода, ни галочек, ни комбо.
		W->SetVisibility(AsdaCommunityUi::ShownVisibilityFor(W));
	}
	// ★ Доказательство в рантайме: сколько виджетов слота реально найдено (0 = имена не совпали).
	UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] слот %d → %s: виджетов затронуто %d (префиксы %s* / %s* / %s*)"),
		SlotIdx, bVisible ? TEXT("ПОКАЗАН") : TEXT("СКРЫТ"), Hit, *P1, *P2, *P3);
	if (Hit == 0)
	{
		UE_LOG(LogAsdaDeath, Error, TEXT("[смерть] слот %d: НИ ОДНОГО виджета не найдено — имена в WBP другие, "
			"смотри asda.Death.DumpWidgets"), SlotIdx);
	}
}

// ── ★ Компоновка окна под число опций (эталон: высота = 66·n + 10) ─────────────────────────────
void UAsdaAfterDeathSubsystem::ApplyDynamicLayout(int32 UsedSlots)
{
	const float NewH = kSlotStep * (float)FMath::Max(1, UsedSlots) + 10.f;   // 66·n + 10 (JUI-единицы)
	ResizeWindowBackground(NewH);
	RecenterWindow(NewH);   // эталон пересчитывает анкор-позицию от живого размера окна каждый кадр
}

// ── ★ Пересборка компоновки открытого меню (динамическое добавление опции «на лету») ────────────
void UAsdaAfterDeathSubsystem::RebuildMenuLayout()
{
	if (!bMenuOpen || !Widget) { return; }
	RefreshSlots();   // слоты + ужимание фона + рецентровка (ApplyDynamicLayout внутри)
	UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] пересборка меню: опций %d"), Options.Num());
}

// ── ★ Рецентровка окна по фактической высоте ────────────────────────────────────────────────────
//   ЭТАЛОН (gamemain_jui.ini, After_Death_Win 25000): AlignXPos "16 50 40" → x = 50 %·W − width/2;
//   AlignYPos "32 50 40" → y = 40 %·H − height/2 (X берёт 2-й токен, Y — 3-й; jui_align_layout.md).
//   Проверка на данных: 1024×768, высота 406 → (512−202, 307.2−203) = (310, 104.2) = jui_positions.tsv ✓.
//   Пересчёт идёт от ЖИВОГО размера окна каждый кадр (uiCalcAnchoredScreenPos@0x12DFBB0) — поэтому после
//   SetSize(66·n+10) оригинал сам оказывается по центру. В UMG повторяем: точечный анкор (0.5, 0.4),
//   offsets (−width/2, −NewH/2) в единицах дизайн-базы 1024×768 (DPI-скейл проекта домножает).
void UAsdaAfterDeathSubsystem::RecenterWindow(float NewHeightJui)
{
	if (!Widget || !Widget->WidgetTree) { return; }
	// ★★★ ФАКТИЧЕСКАЯ структура WBP (рантайм-дамп 2026-08-03): единой группы окна НЕТ — верхний уровень
	//   ПЛОСКИЙ: RootCanvas → { After_Death_Win_0_0 (фон, Image), After_Death_Win (пустой EXS-TextBlock),
	//   After_Death_Choice{i}_grp (6 групп слотов) }, КАЖДЫЙ с точечным анкором (0.5, 0.4) и оффсетом,
	//   рассчитанным импортёром под ПОЛНУЮ высоту 406 (верх окна −203). Эталон центрирует окно по его
	//   ЖИВОЙ высоте → сдвигаем ВСЕ топ-виджеты на Δ = (406 − NewH)/2 от их БАЗОВЫХ (импортных) оффсетов:
	//   верх окна становится −NewH/2, центр видимой части = (50 % W, 40 % H). Δ=0 при 6 опциях (импортная
	//   раскладка и есть полное окно). Базы кешируются (идемпотентно), X не меняется.
	UCanvasPanel* Root = Cast<UCanvasPanel>(Widget->WidgetTree->RootWidget);
	if (!Root)
	{
		UE_LOG(LogAsdaDeath, Error, TEXT("[смерть] RootCanvas не найден — рецентровка невозможна (asda.Death.DumpWidgets)"));
		return;
	}
	const float kJuiFullH = kSlotStep * (float)kMaxOptions + 10.f;   // 406
	const float Delta = (kJuiFullH - NewHeightJui) * 0.5f;
	int32 Moved = 0;
	for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
	{
		UWidget* Ch = Root->GetChildAt(i);
		UCanvasPanelSlot* CS = Ch ? AsdaUi::SlotOf(Ch) : nullptr;
		if (!CS) { continue; }
		// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): было «растянутые (span) — не трогаем» — растянутый ребёнок остался бы на
		//   месте, окно уехало бы без него. Стало: каждый ребёнок едет к «база + Δ» SPAN-безопасно (`AsdaUi::ShiftCanvasSlot`
		//   на разницу до цели; у POINT-оси это ровно прежнее `Top = база + Δ`). У WBP_After_Death_Win все 8 детей корня
		//   POINT (0.5, 0.4) — перепись якорей, результат прежний.
		float* Base = TopBase.Find(CS);
		if (!Base) { Base = &TopBase.Add(CS, (float)CS->GetOffsets().Top); }
		AsdaUi::ShiftCanvasSlot(Ch, FVector2D(0.0, (double)(*Base + Delta) - CS->GetOffsets().Top));
		++Moved;
	}
	// Рантайм-доказательство: сдвиг и ожидаемый экранный центр.
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U11): размер вьюпорта — в единицах UI (`AsdaUi::ViewportSizeUnits`), как и оффсеты слотов;
	//   были пиксели (при масштабе UI ≠ 1 лог называл неверный центр). Только строка лога — раскладку это не меняет.
	const FVector2D VS = AsdaUi::ViewportSizeUnits(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	UE_LOG(LogAsdaDeath, Log,
		TEXT("[смерть] окно отцентровано: высота %.0f (JUI), сдвиг топ-виджетов Δ=%.1f (VERT), сдвинуто %d, верх окна −203→%.1f → экранный центр (%.0f, %.0f) ед. при вьюпорте %.0fx%.0f ед."),
		NewHeightJui, Delta, Moved, -203.f + Delta, VS.X * 0.5f, VS.Y * 0.4f, VS.X, VS.Y);
	if (Moved == 0)
	{
		UE_LOG(LogAsdaDeath, Error, TEXT("[смерть] рецентровка: НИ ОДНОГО точечного топ-виджета — структура иная, смотри asda.Death.DumpWidgets"));
	}
}

// ── ★ Подпись слота: текст без rich-подмены, стиль 1:1 по JUI (см. объявление) ──────────────────
void UAsdaAfterDeathSubsystem::SetSlotLabel(int32 SlotIdx, const FString& Raw)
{
	UTextBlock* Tb = SlotTxt.IsValidIndex(SlotIdx) ? Cast<UTextBlock>(SlotTxt[SlotIdx]) : nullptr;
	if (!Tb)
	{
		UE_LOG(LogAsdaDeath, Error, TEXT("[смерть] слот %d: After_Death_Txt%d не UTextBlock/не найден — текст не поставлен"), SlotIdx, SlotIdx);
		return;
	}
	// Теги разметки (<FC=..> и пр.) в текстах смерти не встречаются, но подстрахуемся StripTags (плейн-текст).
	Tb->SetText(FText::FromString(FAsdaMarkup::StripTags(Raw)));
	// Центровка/цвет/шрифт выставлены один раз в EnsureWidget (CenterTextBlockInSlot идемпотентен).
}

// Фон окна = планы контрола After_Death_Win (импортёр: «After_Death_Win_0_<plane>»), разложенные
//   9-slice-сеткой. Вертикальный ресайз 9-slice: верхний ряд не трогаем, средний тянем, нижний двигаем.
//   Рамки/углы остаются нативными — окно просто становится короче, как в оригинале.
void UAsdaAfterDeathSubsystem::ResizeWindowBackground(float NewHeightJui)
{
	if (!Widget || !Widget->WidgetTree) { return; }

	// ★★★ СТРУКТУРА ОКНА (РАНТАЙМ-дамп asda.Death.DumpWidgets 2026-08-03, закон full-structural-analysis):
	//     RootCanvas — ПЛОСКИЙ верхний уровень, единой группы окна НЕТ:
	//       After_Death_Win_0_0            — ★ UImage, ЕДИНСТВЕННЫЙ видимый фон (скин Box_Black_Ot50),
	//                                          точечный анкор (0.5, 0.4), offsets (−202, −203, 404, 406)
	//       After_Death_Win                — ПУСТОЙ EXS-TextBlock окна (текста нет; НЕ группа!)
	//       After_Death_Choice{i}_grp      — группа слота i (6 шт., анкор (0.5, 0.4), offsets −193, −194+66i):
	//         After_Death_Choice{i}            UButton (3,3,380×52)
	//         After_Death_Txt{i}               UTextBlock
	//         After_Death_BtnBase{i}_{idx}_0..7  планы подложки (9-slice без центра)
	//   ⚠ ПРЕЖНЯЯ ОШИБКА (лог живого теста «высота 602 → 76»): ужимали ВСЕ виджеты с префиксом
	//     After_Death_Win, включая саму ГРУППУ. CanvasPanel по умолчанию НЕ клипает — визуально
	//     не менялось ничего, а число 602 показывало, что размеры в WBP уже ЭКРАННЫЕ (после
	//     ApplyResponsiveSlot), а не JUI-единицы (окно в JUI = 404×406). Теперь: ужимаем ТОЛЬКО
	//     фон-картинку и ПРОПОРЦИОНАЛЬНО — коэффициентом (66·n + 10) / 406.
	const float kJuiFullH = kSlotStep * (float)kMaxOptions + 10.f;   // 66·6 + 10 = 406 (JUI)
	const float Scale = FMath::Clamp(NewHeightJui / kJuiFullH, 0.05f, 1.f);

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	int32 Applied = 0;
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (!N.StartsWith(TEXT("After_Death_Win_0"))) { continue; }   // группу After_Death_Win НЕ трогаем
		UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		if (!CS) { continue; }
		TPair<float, float>* Base = BgBase.Find(CS);
		if (!Base)
		{
			Base = &BgBase.Add(CS, TPair<float, float>((float)CS->GetPosition().Y, (float)CS->GetSize().Y));
		}
		FVector2D Sz = CS->GetSize();
		Sz.Y = FMath::Max(4.f, Base->Value * Scale);   // фон растёт вниз от своей исходной верхней кромки
		CS->SetSize(Sz);
		++Applied;
		BgBaseHeight = Base->Value;
	}
	UE_LOG(LogAsdaDeath, Log,
		TEXT("[смерть] фон окна ужат: опций %d → JUI-высота %.0f из 406 (коэф. %.3f), виджетов фона %d, база %.0f px"),
		FMath::RoundToInt((NewHeightJui - 10.f) / kSlotStep), NewHeightJui, Scale, Applied, BgBaseHeight);
	if (Applied == 0)
	{
		UE_LOG(LogAsdaDeath, Error, TEXT("[смерть] фон окна (After_Death_Win_0*) НЕ НАЙДЕН — смотри asda.Death.DumpWidgets"));
	}
}

// ── ★ ПОЛНЫЙ СТРУКТУРНЫЙ ДАМП ОКНА (asda.Death.DumpWidgets) ────────────────────────────────────
//   Печатает КАЖДЫЙ виджет: имя, класс, видимость, позицию и размер слота, родителя. По закону
//   full-structural-analysis — чтобы правки делались по фактической структуре, а не по догадке.
// Дамп одного дерева виджетов; для UTextBlock дополнительно шрифт/цвет/wrap (рантайм-доказательство §1).
static void AsdaDeathDumpTree(const TCHAR* Label, const UUserWidget* Root)
{
	if (!Root || !Root->WidgetTree)
	{
		UE_LOG(LogAsdaDeath, Display, TEXT("[дамп] %s: не открыт"), Label);
		return;
	}
	TArray<UWidget*> All;
	Root->WidgetTree->GetAllWidgets(All);
	UE_LOG(LogAsdaDeath, Display, TEXT("[дамп] %s: виджетов %d"), Label, All.Num());
	static const TCHAR* kVis[] = { TEXT("Visible"), TEXT("Collapsed"), TEXT("Hidden"),
	                               TEXT("HitTestInvisible"), TEXT("SelfHitTestInvisible") };
	for (const UWidget* W : All)
	{
		if (!W) { continue; }
		const UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		const FVector2D P = CS ? CS->GetPosition() : FVector2D::ZeroVector;
		const FVector2D S = CS ? CS->GetSize()     : FVector2D::ZeroVector;
		const int32 V = (int32)W->GetVisibility();
		FString Extra;
		if (const UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			const FLinearColor C = Tb->GetColorAndOpacity().GetSpecifiedColor();
			Extra = FString::Printf(TEXT(" | шрифт=%.0f wrapAt=%.0f autosize=%d цвет=(%.2f %.2f %.2f) текст='%s'"),
				Tb->GetFont().Size, Tb->GetWrapTextAt(), (CS && CS->GetAutoSize()) ? 1 : 0,
				C.R, C.G, C.B, *Tb->GetText().ToString().Left(40));
		}
		if (CS)
		{
			const FAnchors A = CS->GetAnchors();
			if (!A.Minimum.Equals(FVector2D::ZeroVector) || !A.Maximum.Equals(FVector2D::ZeroVector))
			{
				Extra += FString::Printf(TEXT(" | анкор=(%.2f,%.2f)-(%.2f,%.2f)"),
					A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y);
			}
		}
		UE_LOG(LogAsdaDeath, Display, TEXT("[дамп]   %-34s %-18s vis=%-20s pos=(%7.1f,%7.1f) size=(%7.1f,%7.1f) родитель=%s%s"),
			*W->GetName(), *W->GetClass()->GetName(),
			(V >= 0 && V < 5) ? kVis[V] : TEXT("?"), P.X, P.Y, S.X, S.Y,
			W->GetParent() ? *W->GetParent()->GetName() : TEXT("RootCanvas"), *Extra);
	}
}

void UAsdaAfterDeathSubsystem::DumpWidgetTree() const
{
	if (!Widget && !Modal)
	{
		UE_LOG(LogAsdaDeath, Warning, TEXT("[дамп] окно смерти не создано (сначала asda.Death.Test)"));
	}
	AsdaDeathDumpTree(TEXT("After_Death_Win"), Widget);
	AsdaDeathDumpTree(TEXT("ONEBUT_MSGBOX (модалка)"), Modal);
	// msgbox подтверждения (TWOBUT), если открыт менеджером окон.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		AsdaDeathDumpTree(TEXT("TWOBUT_MSGBOX (подтверждение)"), WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX"))));
	}
}

// ★ 31.10.2026: подпись и кнопка слота — для самотеста боя (окно смерти на карте 19).
FString UAsdaAfterDeathSubsystem::DebugSlotLabel(int32 I) const
{
	const UTextBlock* Tb = SlotTxt.IsValidIndex(I) ? Cast<UTextBlock>(SlotTxt[I]) : nullptr;
	return Tb ? Tb->GetText().ToString() : FString();
}
UWidget* UAsdaAfterDeathSubsystem::DebugSlotButton(int32 I) const
{
	return SlotBtn.IsValidIndex(I) ? SlotBtn[I].Get() : nullptr;
}

// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] Подпись опции 7 на карте 19: 2385 + остаток блокировки (loc 2418), по концу блокировки — 2385 (разбор у ключа).
void UAsdaAfterDeathSubsystem::RefreshLockLabel()
{
	if (CVarDeathCampLockCountdown.GetValueOnGameThread() == 0) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || Net->EnterMapId != AsdaMapRegistry::kBattlefield) { return; }
	int32 Idx = INDEX_NONE;
	for (int32 i = 0; i < Options.Num(); ++i) { if (Options[i].Type == 7) { Idx = i; break; } }
	if (Idx == INDEX_NONE || Options[Idx].Type == AutoOption) { return; }   // авто-опцию подписывает отсчёт ниже
	UAsdaTextSubsystem* Txt = GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>();
	if (!Txt) { return; }
	FString L = Txt->GetTextById(2385);
	if (L.IsEmpty()) { return; }
	const int32 Left = FMath::CeilToInt(ClickLockSec);
	if (Left > 0)
	{
		FString T = Txt->GetTextById(2418);
		if (!T.IsEmpty())
		{
			T.ReplaceInline(TEXT("[sd1]"), *FString::FromInt(Left / 60), ESearchCase::CaseSensitive);
			T.ReplaceInline(TEXT("[sd2]"), *FString::FromInt(Left % 60), ESearchCase::CaseSensitive);
			L += TEXT(" ") + T;
		}
	}
	SetSlotLabel(Idx, L);
}

// Подпись отсчёта на авто-опции: loc 20192 «…через [sd1] мин.» / 20193 «…через [sd1] сек.».
void UAsdaAfterDeathSubsystem::RefreshTimerLabel()
{
	if (!bMenuOpen || AutoOption < 0) { return; }
	RefreshLockLabel();   // ★ 08.11.2026: остаток блокировки на опции 7 (`asda.Death.CampWarLockCountdown`)
	int32 Idx = INDEX_NONE;
	for (int32 i = 0; i < Options.Num(); ++i) { if (Options[i].Type == AutoOption) { Idx = i; break; } }
	if (!SlotTxt.IsValidIndex(Idx) || !SlotTxt[Idx]) { return; }
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!Txt) { return; }

	const bool bMinutes = (RespawnTimerSec > 60.f);
	// ★ 31.10.2026 [ОРИГИНАЛ] `deathRespawnUpdateTimerLabel@0xD6DAF0` (G:377809): на картах 19/23 при отсчёте ≤ 60 с в подпись авто-опции
	//   идёт loc 108107 «Через 30 секунд после смерти … (Автоматическое воскрешение произойдет через [sd1] сек.)», и если индекс
	//   авто-опции ≥ 1 — в ту же подпись loc 2386 «Персонаж воскреснет на базе через [sd1] с.» (на 19 подсказка 108107 затирается в том же
	//   кадре — [ДЕФЕКТ ОРИГИНАЛА], оставлено 1:1). [sd1] — секунды с отбрасыванием дробной части (`__ftol2`).
	//   Прежде порт писал сюда 20192/20193 «Немедленно воскреснуть В ГОРОДЕ…» — на кнопке «база» (отзыв игроков «resurrection in town»).
	{
		const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const int32 MapId = Net ? Net->EnterMapId : -1;
		if (!bMinutes && (MapId == AsdaMapRegistry::kBattlefield || MapId == AsdaMapRegistry::kMineWar))
		{
			const FString Sec = FString::FromInt(FMath::Max(0, FMath::FloorToInt(RespawnTimerSec)));
			FString L = Txt->GetTextById(108107);
			if (Idx >= 1) { L = Txt->GetTextById(2386); }
			if (L.IsEmpty()) { return; }
			L.ReplaceInline(TEXT("[sd1]"), *Sec, ESearchCase::CaseSensitive);
			SetSlotLabel(Idx, L);
			return;
		}
	}
	const int32 Value = bMinutes ? FMath::CeilToInt(RespawnTimerSec / 60.f) : FMath::CeilToInt(RespawnTimerSec);
	FString S = Txt->GetTextById(bMinutes ? 20192 : 20193);
	if (S.IsEmpty()) { return; }
	S.ReplaceInline(TEXT("[sd1]"), *FString::FromInt(FMath::Max(0, Value)), ESearchCase::CaseSensitive);
	SetSlotLabel(Idx, S);
}

// ── Тик: клиентский отсчёт авто-воскрешения (эталон entity_char.c:22843-22855) ──────────────────
void UAsdaAfterDeathSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.AfterDeath");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	if (!bMenuOpen) { return; }
	if (ClickLockSec > 0.f)
	{
		const int32 LockBefore = FMath::CeilToInt(ClickLockSec);
		ClickLockSec = FMath::Max(0.f, ClickLockSec - DeltaTime);
		if (FMath::CeilToInt(ClickLockSec) != LockBefore) { RefreshLockLabel(); }   // ★ 08.11.2026: остаток блокировки на опции 7
	}
	if (RespawnTimerSec <= 0.f || AutoOption < 0) { return; }

	const int32 Before = (RespawnTimerSec > 60.f) ? FMath::CeilToInt(RespawnTimerSec / 60.f)
	                                              : FMath::CeilToInt(RespawnTimerSec);
	const int32 FloorBefore = FMath::FloorToInt(RespawnTimerSec);
	RespawnTimerSec -= DeltaTime;
	const int32 After = (RespawnTimerSec > 60.f) ? FMath::CeilToInt(RespawnTimerSec / 60.f)
	                                             : FMath::CeilToInt(RespawnTimerSec);
	// ★ 31.10.2026: подпись карт 19/23 показывает отброшенную дробь (эталон `__ftol2`) — перерисовка и на смене целой части.
	if (After != Before || FMath::FloorToInt(RespawnTimerSec) != FloorBefore) { RefreshTimerLabel(); }

	if (RespawnTimerSec <= 0.f)
	{
		RespawnTimerSec = 0.f;
		UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] отсчёт истёк → авто-опция %d"), AutoOption);
		ASDA_CTRACE(EAsdaCT::Death, TEXT("отсчёт авто-воскрешения истёк → авто-опция %d"), AutoOption);
		SelectOption(AutoOption);
	}
}

// ── Клик по слоту: msgbox loc 2382 «Воскреснуть?» → ОК → SelectOption (эталон ui_core.c:58452) ──
void UAsdaAfterDeathSubsystem::OnSlot0() { OnSlotClicked(0); }
void UAsdaAfterDeathSubsystem::OnSlot1() { OnSlotClicked(1); }
void UAsdaAfterDeathSubsystem::OnSlot2() { OnSlotClicked(2); }
void UAsdaAfterDeathSubsystem::OnSlot3() { OnSlotClicked(3); }
void UAsdaAfterDeathSubsystem::OnSlot4() { OnSlotClicked(4); }
void UAsdaAfterDeathSubsystem::OnSlot5() { OnSlotClicked(5); }

void UAsdaAfterDeathSubsystem::OnSlotClicked(int32 SlotIdx)
{
	if (!bMenuOpen || !Options.IsValidIndex(SlotIdx))
	{
		ASDA_CTRACE(EAsdaCT::Input, TEXT("клик по слоту %d окна после смерти проигнорирован: %s (опций %d)"), SlotIdx,
			!bMenuOpen ? TEXT("меню закрыто") : TEXT("слота нет"), Options.Num());
		return;
	}
	const int32 Type = Options[SlotIdx].Type;
	ASDA_CTRACE(EAsdaCT::Input, TEXT("клик по слоту %d окна после смерти: опция %d (инициатор %d, %d%%), до авто %.0f с, блок 7/8 %.0f с"),
		SlotIdx, Type, Options[SlotIdx].CasterUserIdx, Options[SlotIdx].Percent, RespawnTimerSec, ClickLockSec);

	// Кампварные 7/8: пока идёт 30-секундная блокировка — клик просто игнорируется (эталон).
	if ((Type == 7 || Type == 8) && ClickLockSec > 0.f)
	{
		UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] опция %d заблокирована ещё %.0f с"), Type, ClickLockSec);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("опция %d (кампвар) НЕ выбрана: блокировка клика ещё %.0f с (эталон 30 с)"), Type, ClickLockSec);
		return;
	}

	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const FString Q = Txt ? Txt->GetTextById(2382) : FString();
	if (WM && !Q.IsEmpty())
	{
		// ★★ Эталонный путь клика (ui_core.c:58476): CMsgBox_SetContent(contentKind 1, noticeId 0x4000,
		//   текст loc 2382) → окно TWOBUT_MSGBOX, кнопки loc 2288 «ОК» / 2289 «Отмена», центр экрана.
		//   ОК → DeathMenu_OnSelect (case 0x4000, ui_core.c:57068). Раньше тут открывался ЛОГИН-диалог
		//   MESSAGE2_WIN с плейсхолдерами «Текст» — не эталонное окно мира.
		//   Полоса-гейт согласия — asda.Death.ConfirmGaugeMs (10 000 мс; по требованию юзера, см. CVar).
		const float GaugeSec = FMath::Max(0, CVarDeathConfirmGaugeMs.GetValueOnGameThread()) / 1000.f;
		UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] клик по слоту %d (тип %d) → msgbox loc 2382, gauge %.1f с"),
			SlotIdx, Type, GaugeSec);
		// ★ Волна В6 ремесла (ревизия В5 МЕЛК-2): ветка «Да» 0x4000 эталона зовёт `CGameUI_CloseWindow` сама
		//   (ui_core.c:57068), затем `DeathMenu_OnSelect` и ЕЩЁ РАЗ `CGameUI_CloseWindow` через `LABEL_300` (:57675) —
		//   закрытие вида дважды. Первое — здесь, ДО действия (`RepeatMsgBoxClose`: 5, а при непустой очереди ещё и
		//   возврат записи с 25 — как у эталона), второе делает общая точка после ответа.
		//   Пара очереди (26.09): (1, 0x4000) — `CItem_BuildTooltipText(ui, 1, 0x4000, loc 2382, 1)` (ui_core.c:58477).
		//   ★ Редакция 1 (ревизия С-1): порядок эталона «закрытие (:57068) → DeathMenu_OnSelect → закрытие (LABEL_300)»:
		//   первое закрытие — здесь ДО действия, второе делает общая точка после колбэка.
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Q, [this, SlotIdx](bool bYes)
		{
			ASDA_CTRACE(EAsdaCT::Input, TEXT("msgbox «Воскреснуть?» (loc 2382) слот %d: %s"), SlotIdx, bYes ? TEXT("ОК") : TEXT("Отмена — опция не выбрана"));
			if (!bYes) { return; }
			if (UAsdaWindowSubsystem* WM2 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
			{
				WM2->RepeatMsgBoxClose(1, TEXT("0x4000: CGameUI_CloseWindow до DeathMenu_OnSelect (ui_core.c:57068)"));
			}
			SelectOptionAt(SlotIdx);
		}, GaugeSec, 2288, 2289, /*EtalonNoticeId*/0x4000, /*EtalonKind*/1);
	}
	else
	{
		SelectOptionAt(SlotIdx);   // без таблицы/менеджера окон — прямое исполнение (иначе игрок заперт)
	}
}

// Исполнить опцию по индексу: типам 0/1/2 нужны инициатор и процент из записи.
void UAsdaAfterDeathSubsystem::SelectOptionAt(int32 Index)
{
	if (!Options.IsValidIndex(Index)) { return; }
	const FAsdaDeathOption Opt = Options[Index];
	if (Opt.Type >= 0 && Opt.Type <= 2)
	{
		UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return; }
		// Эталон DeathMenu_OnSelect: action = тип, но для 1 и 2 он увеличивается на 1 →
		//   серверные Type 0 / 2 / 3 для CharRegenSelfPosition.
		const uint8 Action = (uint8)((Opt.Type == 1 || Opt.Type == 2) ? Opt.Type + 1 : Opt.Type);
		ASDA_CTRACE(EAsdaCT::Send, TEXT("6093 согласие на воскрешение: опция %d → Type=%d, инициатор userIdx=%d, %d%%"),
			Opt.Type, (int32)Action, Opt.CasterUserIdx, Opt.Percent);
		Net->SendResurrectConfirm6093(Action, Opt.CasterUserIdx, Opt.Percent);
		UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] опция %d (предложение от %d) → 6093 Type=%u процент=%d"),
			Opt.Type, Opt.CasterUserIdx, Action, Opt.Percent);
		return;
	}
	SelectOption(Opt.Type);
}

// ── Исполнение опции (эталон DeathMenu_OnSelect@0xD6DE70) ──────────────────────────────────────
void UAsdaAfterDeathSubsystem::SelectOption(int32 OptionType)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	if (OptionType == 5)
	{
		// ★ 05.11.2026 [ОРИГИНАЛ] Премиум-свиток: пакета 4054 НЕТ. Эталон `DeathMenu_OnSelect` case 5 → `sub_92C150(CharMgr, 0)` +
		//   `invBagAutoUseFirstType48Item@0xD26740`: первый спец-предмет подтипа 48 в сумке применяется обычным
		//   `CItemManager_UseInventoryItem` — ровно тем, что делает двойной клик. У порта эта общая точка —
		//   `UAsdaInventorySubsystem::UseSpecialItemDispatch` (→ `UsePremiumItem` → 5450). Воскрешает ответ сервера
		//   (4056 своего userIdx), и меню закрывается тем же `OnCharRevived`, что у прочих опций.
		//   [ПОРТ] Свитка уже нет (продан/перемещён, пока висело меню): эталон молча закрывал меню и снимал отсчёт — игрок
		//   оставался мёртвым без кнопок. Порт меню не закрывает (оно закрывается только по 4055/4056), так что можно
		//   выбрать другую опцию; пишем причину в журнал.
		UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		int32 ScrollCode = 0;
		const int32 Cell = AsdaFindReviveScrollCell(GetGameInstance(), &ScrollCode);
		const bool bUsed = Inv && Cell >= 0 && Inv->UseSpecialItemDispatch(1, Cell);
		UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] опция 5 (премиум-свиток): ячейка %d код %d → применение %s"),
			Cell, ScrollCode, bUsed ? TEXT("отправлено (5450)") : (Cell < 0 ? TEXT("НЕТ — свитка в сумке больше нет") : TEXT("НЕ отправлено")));
		ASDA_CTRACE(bUsed ? EAsdaCT::Send : EAsdaCT::Refuse, TEXT("опция 5 воскрешение свитком: ячейка %d код %d — %s"),
			Cell, ScrollCode, bUsed ? TEXT("применён как двойной клик") : TEXT("не применён"));
		return;
	}

	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Death.CampWarMenu`): на картах 19/23 режим 0 («город», сервер `CharRegenVillage` → 4055) клиент
	//   эталона воспринимает как выход из боя (HUD снят, карта та же). В меню этих карт опции 4 нет; эта проверка закрывает прочие пути
	//   (консоль, окно 2370, если оно осталось) — режим 0 здесь не уходит.
	if (OptionType == 4 && CVarDeathCampMenu.GetValueOnGameThread() != 0)
	{
		const int32 MapId = Net->EnterMapId;
		if (MapId == AsdaMapRegistry::kBattlefield || MapId == AsdaMapRegistry::kMineWar)
		{
			UE_LOG(LogAsdaDeath, Warning, TEXT("[смерть] опция 4 (4054 режим 0) на карте %d не отправлена — сняла бы режим войны; выбери 7/8"), MapId);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("4054 режим 0 на карте %d не отправлен (asda.Death.CampWarMenu)"), MapId);
			return;
		}
	}
	// Режим пакета 4054 по типу опции (таблица RecvCharRegen@0x52A570).
	uint8 Mode = 0;
	switch (OptionType)
	{
	case 3: Mode = 1; break;
	case 4: Mode = 0; break;
	case 6: Mode = 6; break;
	case 7: Mode = 7; break;
	case 8: Mode = 8; break;
	default:
		UE_LOG(LogAsdaDeath, Warning, TEXT("[смерть] неизвестный тип опции %d"), OptionType);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("опция воскрешения %d не исполнена: неизвестный тип"), OptionType);
		return;
	}
	const bool bSent = Net->SendCharRegen(Mode);
	ASDA_CTRACE(EAsdaCT::Send, TEXT("4054 воскрешение: опция %d → режим %d — %s; меню закроется по 4055/4056"),
		OptionType, (int32)Mode, bSent ? TEXT("отправлен") : TEXT("НЕ ОТПРАВЛЕН (сеть отказала)"));
	UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] опция %d → 4054 режим %u -> %s"),
		OptionType, Mode, bSent ? TEXT("OK") : TEXT("FAIL"));

	// Меню закрывается по ПОДТВЕРЖДЕНИЮ сервера (4055/4056) — эталон deathMenuWnd_reset зовётся оттуда.
}

void UAsdaAfterDeathSubsystem::OnCharRevived()
{
	ASDA_CTRACE(EAsdaCT::Death, TEXT("4055/4056 ВОСКРЕШЕНИЕ подтверждено сервером: окно после смерти закрыто (было открыто=%s, опций %d), действия разблокированы"),
		bMenuOpen ? TEXT("да") : TEXT("нет"), Options.Num());
	CloseMenu();
	// ★★ УБРАНО СООБЩЕНИЕ loc 2117. ФАКТ из таблицы (`system/error list.txt`):
	//     2116 = «GW has killed you»       — GM УБИЛ персонажа
	//     2117 = «GW has resurrected you»  — GM ВОСКРЕСИЛ персонажа («Служба Поддержки воскрешает тебя»)
	//   Это тексты ветки ОПЕРАТОРА (сервер `Send_KillCommand`@GameServer.c:128880 → 6025 +
	//   `CharRegenSelfPosition(Type 5)`; клиент — обработчик, печатающий loc 2116 и добавляющий
	//   опцию 4). К ОБЫЧНОМУ воскрешению по 4054 они отношения не имеют — эталон при 4055/4056
	//   в чат НИЧЕГО не пишет. Строка появлялась у обычного игрока только потому, что порт слал её
	//   сам. 2117 вернуть в систему сообщений вместе с веткой GM-команд (6025), не раньше.
	UE_LOG(LogAsdaDeath, Log, TEXT("[смерть] воскрешение подтверждено — меню закрыто, действия разблокированы"));
}

void UAsdaAfterDeathSubsystem::CloseMenu()
{
	bMenuOpen = false;
	Options.Reset();
	AutoOption = -1;
	RespawnTimerSec = 0.f;
	ClickLockSec = 0.f;
	CloseMenuWidget();
	CloseModal();
}

// Закрыть только окно ВЫБОРА (модалку и состояние не трогаем).
void UAsdaAfterDeathSubsystem::CloseMenuWidget()
{
	if (Widget) { Widget->SetVisibility(ESlateVisibility::Collapsed); }
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindow(FName(TEXT("After_Death_Win")));
	}
	ResetWidget();
}

FString UAsdaAfterDeathSubsystem::DebugState() const
{
	FString Opts;
	for (const FAsdaDeathOption& O : Options)
	{
		if (O.Type <= 2) { Opts += FString::Printf(TEXT("%d(от %d,%d%%) "), O.Type, O.CasterUserIdx, O.Percent); }
		else             { Opts += FString::Printf(TEXT("%d "), O.Type); }
	}
	return FString::Printf(TEXT("меню=%s опции=[%s] авто=%d отсчёт=%.1f с блокировка=%.1f с убийца=%d"),
		bMenuOpen ? TEXT("открыто") : TEXT("закрыто"), *Opts, AutoOption, RespawnTimerSec, ClickLockSec, LastKillerIdx);
}

// ═══════════════════════════ Консольные команды проверки (PIE) ═════════════════════════════════
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaRevive(
	TEXT("asda.Revive"),
	TEXT("Отправить 4054 (воскрешение): asda.Revive <тип опции 3|4|6|7|8> (по умолчанию 4 — город)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaAfterDeathSubsystem* D = W && W->GetGameInstance()
			? W->GetGameInstance()->GetSubsystem<UAsdaAfterDeathSubsystem>() : nullptr;
		if (!D) { return; }
		D->SelectOption(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 4);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЭМУЛЯЦИЯ СМЕРТИ БЕЗ СЕРВЕРА: asda.Death.Test [уровень] [1 = «вошёл мёртвым»]
//   Прогоняет ТОТ ЖЕ путь, что и пакет 4053 (EnterSelfDeadState → OnCharDeath), поэтому проверяет
//   всё разом: позу/клип, ЧБ, блокировку ввода, набор опций, компоновку окна и модалку.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaDeathTest(
	TEXT("asda.Death.Test"),
	TEXT("Эмулировать смерть локально: asda.Death.Test [уровень] [1=вошёл мёртвым (модалка)]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: локальная смерть живого персонажа — через 300 с отсчёт сам шлёт воскрешение 4054, SelfLevel подменяется.
		static const uint16 kGuardOps[] = { 4054, 6093 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Death.Test"), 310.f, MakeArrayView(kGuardOps))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return; }
		if (Args.Num() > 0)
		{
			const int32 L = FCString::Atoi(*Args[0]);
			if (L > 0) { Net->SelfLevel = (uint8)FMath::Clamp(L, 1, 255); }
		}
		const bool bEntry = (Args.Num() > 1) && (FCString::Atoi(*Args[1]) != 0);
		Net->bSelfDead = false;                       // чтобы EnterSelfDeadState не вышел по идемпотентности
		Net->EnterSelfDeadState(TEXT("ЛОКАЛЬНЫЙ ТЕСТ asda.Death.Test"), bEntry);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ТЕСТ ДИНАМИЧЕСКОГО ДОБАВЛЕНИЯ ОПЦИИ «НА ЛЕТУ» (пока меню открыто): эмулирует 6093 S→C —
//   «другой игрок повесил скилл воскрешения». Ожидание: в меню появляется НОВАЯ кнопка, окно
//   заново ужимается и РЕЦЕНТРУЕТСЯ (лог «[смерть] окно отцентровано: …»).
//   asda.Death.TestOffer [сервТип 0|2|3] [процент] — по умолчанию 3 (Resuscitate 39 %).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaDeathTestOffer(
	TEXT("asda.Death.TestOffer"),
	TEXT("Эмулировать 6093 (предложение воскрешения): asda.Death.TestOffer [Type 0|2|3] [процент]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный заклинатель 777 — клик по варианту отправил бы 6093.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Death.TestOffer"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return; }
		const uint8 Type = (uint8)(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 3);
		const int32 Pct  = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 39;
		Net->OnResurrectOffer.Broadcast(/*CasterUserIdx*/ 777, Type, Pct);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ТЕСТ MSGBOX ПОДТВЕРЖДЕНИЯ без клика мышью: открывает эталонный TWOBUT_MSGBOX (loc 2382,
//   кнопки 2288/2289) с полосой-гейтом из CVar asda.Death.ConfirmGaugeMs. Результат в лог.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaDeathTestConfirm(
	TEXT("asda.Death.TestConfirm"),
	TEXT("Открыть msgbox воскрешения (TWOBUT_MSGBOX, loc 2382) с полосой-гейтом — проверка оформления"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		if (!WM) { return; }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10d): было «loc 2382» только при отсутствии подсистемы текста (пустая строка
		//   оставалась пустой) — стало единая заглушка «[#2382]» в обоих случаях.
		const FString Q0 = Txt ? Txt->GetTextById(2382) : FString();
		const FString Q = Q0.IsEmpty() ? FAsdaLang::MissingTag(2382) : Q0;
		const float GaugeSec = FMath::Max(0, CVarDeathConfirmGaugeMs.GetValueOnGameThread()) / 1000.f;
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Q, [](bool bYes)
		{
			UE_LOG(LogAsdaDeath, Display, TEXT("[смерть] тест msgbox: исход=%s"), bYes ? TEXT("ОК") : TEXT("Отмена"));
		}, GaugeSec);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaDeathDumpWidgets(
	TEXT("asda.Death.DumpWidgets"),
	TEXT("Полный структурный дамп окна After_Death_Win в лог (имя/класс/видимость/позиция/размер/родитель)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaAfterDeathSubsystem* D = W && W->GetGameInstance()
			? W->GetGameInstance()->GetSubsystem<UAsdaAfterDeathSubsystem>() : nullptr;
		if (D) { D->DumpWidgetTree(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaDeathDump(
	TEXT("asda.Death.Dump"),
	TEXT("Состояние меню смерти: опции/отсчёт/блокировка"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaAfterDeathSubsystem* D = W && W->GetGameInstance()
			? W->GetGameInstance()->GetSubsystem<UAsdaAfterDeathSubsystem>() : nullptr;
		if (!D) { return; }
		UE_LOG(LogAsdaDeath, Display, TEXT("%s"), *D->DebugState());
	}));
#endif // ASDA_DEV_CONSOLE
