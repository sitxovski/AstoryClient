// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPartySubsystem_Menu.cpp — ★ R3-c 12.11.2026 (разрез AsdaPartySubsystem.cpp, дословный перенос): роспуск, метки мини-карты, изгнание, меню строки, общая цель, меню отряда, тексты и нотисы.
//   Общее для частей — в AsdaPartySubsystem_Private.h; шапка и реверс — в AsdaPartySubsystem.cpp.
#include "UI/AsdaPartySubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaSoundSubsystem.h" // звук 215 при приходе заявки
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"       // иконка уведомления (statusBarAddIcon)
#include "UI/AsdaMiniStateSubsystem.h"  // ★ 30.10.2026: общая плашка подсказки (ShowSharedTip/HideSharedTip)
#include "UI/AsdaTipPanel.h"            // ★ 30.10.2026: EAsdaTipOwner::PartyHud
#include "World/AsdaPlayerController.h"  // ★ ОБЩАЯ точка выбора цели (внутри — 6538)
#include "UI/AsdaPlayerMenuSubsystem.h"  // ★ ОБЩАЯ точка следования StartFollow
#include "GameFramework/PlayerController.h"
#include "Components/PanelWidget.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"      // ★ волна 1-Б: отметки соотрядников (EAsdaMapMark::Party, §10 заголовка)
#include "World/AsdaLoadManagerSubsystem.h" // ★ волна 1-Б: моя карта (g_nCurMapId) — признак +272 «на моей карте»
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaWorldKind.h"         // ★ 22.10.2026: экран загрузки/логин — панель там не восстанавливаем

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "World/AsdaPlayerCharacter.h"      // ★ 25.10.2026: клавиши отряда — актор соотрядника, его цель хода/атаки
#include "Npc/AsdaMobActor.h"               // ★ 25.10.2026: «общая цель» — сущность (моб) цели соотрядника
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)
#include "AsdaPartySubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPartySubsystem*.cpp

namespace {
	const TCHAR* kDisbandWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYDISPERSION.WBP_PARTYDISPERSION_C");

	// ★ Меню ПКМ по строке участника — `Follow_Another_win` 8100 (пункты 8102/8101/8103).
	const FName  kRowMenuWinId(TEXT("Follow_Another_win"));
	const TCHAR* kRowMenuWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_Follow_Another_win.WBP_Follow_Another_win_C");

	// ★ Окно роспуска 2256 — idc из `gamemain_jui.ini:42512-42760` (дамп ассета `_party_win_dump.txt:562`).
	const TCHAR* kIdcDisbandText = TEXT("PARTYDISPERSION_MESSAGE");   // 2257
	const TCHAR* kIdcDisbandOk   = TEXT("PARTYDISPERSION_OK");        // 2258
	const TCHAR* kIdcDisbandNo   = TEXT("PARTYDISPERSION_CANCEL");    // 2259

	// ★ Дефект оригинала: меню строки отряда 8100 никогда не показывается (доказательство —
	//   в шапке блока меню ниже). Ключ возвращает поведение оригинала.
	ASDA_DEV_CVAR(int32, CVarFixMarkTarget,
		TEXT("asda.Party.FixMarkTarget"), 1,
		TEXT("1 (по умолчанию) = ПКМ по строке участника открывает меню 8100 «Указать цель /\n")
		TEXT("Следовать / Закрыть».\n")
		TEXT("0 = как в оригинале: меню не открывается ничем (в образе нет ни одного\n")
		TEXT("CControl_SetVisible(pFollowWin, 1)), и пункт «Указать цель» недостижим."),
		ECVF_Default);
}

// ═════════════════════════════ ★ ОКНО РОСПУСКА `PARTYDISPERSION` 2256 (волна 1-Б) ═════════════
//   Реверс, доказательство «в оригинале не вызывается», смысл режима 5098 — шапка .h §9.
bool UAsdaPartySubsystem::EnsureDisbandWidget()
{
	// Каркас окна — та же общая точка, что у 2250/2245 (`FAsdaJuiWindowHandle::Ensure`).
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kDisbandWinId, kDisbandWbp, kDisbandZ).Ensure(GetGameInstance(), DisbandWidget, bDisbandReady,
		[](UAsdaWindowSubsystem&) {},   // протухшее окно отдельно не сносится (как у соседей)
		[this](UAsdaWindowSubsystem&)
		{
			SetupDisbandControls();
		},
		[]()
		{
			UE_LOG(LogAsdaParty, Error, TEXT("[отряд] WBP_PARTYDISPERSION не загрузился (%s)"), kDisbandWbp);
		});
	// Скрытие окна (шаг каркаса): .JUI Visible="0". Закрывателя «по началу движения» и по ESC у эталона
	//   нет (`CObject_SetField166(win, 0)` + `CObject_SetFlag668(win, 0)` — как у 2250/2245; в
	//   `kEscCloseWindows` окна 2256 нет).
	return FAsdaJuiWindowHandle::Succeeded(R) && bDisbandReady;
}

void UAsdaPartySubsystem::SetupDisbandControls()
{
	bDisbandReady = false;
	if (!DisbandWidget || !DisbandWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	DisbandWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == kIdcDisbandOk) { DisbandOk = B; }
			else if (N == kIdcDisbandNo) { DisbandCancel = B; }
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == kIdcDisbandText) { DisbandText = T; }
			else if (N == FString(kIdcDisbandOk) + TEXT("_Text")) { DisbandOkText = T; }
			else if (N == FString(kIdcDisbandNo) + TEXT("_Text")) { DisbandCancelText = T; }
		}
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(DisbandOk,     2258, [this](int32) { OnDisbandOk(); });
	Bind(DisbandCancel, 2259, [this](int32) { OnDisbandCancel(); });

	// Текст 2257 — та же грабля авто-переноса, что у 2251/2246 (ширина слота из ассета, 200 по .JUI).
	FixTextWrapJitter(DisbandText, TEXT("2257 PARTYDISPERSION_MESSAGE"));

	// Подписи кнопок — эталон ставит их при привязке (`social.c:2066-2069`: loc 2288 / 2378).
	if (DisbandOkText)     { DisbandOkText->SetText(FText::FromString(Loc(AsdaLoc::kBtnOk))); }
	if (DisbandCancelText) { DisbandCancelText->SetText(FText::FromString(Loc(AsdaLoc::kBtnCancelAlt))); }

	bDisbandReady = (DisbandText != nullptr && DisbandOk != nullptr && DisbandCancel != nullptr);
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] окно 2256 разобрано: 2257=%s 2258=%s(подпись %s) 2259=%s(подпись %s)"),
		DisbandText ? TEXT("да") : TEXT("НЕТ"),
		DisbandOk ? TEXT("да") : TEXT("НЕТ"), DisbandOkText ? TEXT("да") : TEXT("НЕТ"),
		DisbandCancel ? TEXT("да") : TEXT("НЕТ"), DisbandCancelText ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaPartySubsystem::SetDisbandVisible(bool bVisible)
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kDisbandWinId, bVisible);
		if (bVisible) { WM->BringWindowToFront(kDisbandWinId); }
	}
	bDisbandOpen = bVisible;
}

// Порт функции `0xB99C30` (IDA её не определила; дизасм — шапка .h §9):
//   `if (Party_IsMeBoss(g_CharManager) == 1) { sprintf(buf, "%s", loc 1170); SetText(+0x154, buf);
//    SetVisible(+0x150, 1); } return 1;` — не главе НИЧЕГО: ни нотиса, ни окна.
bool UAsdaPartySubsystem::ShowDisbandConfirm()
{
	if (!IsPartyLeader())
	{
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 2256: я не глава отряда — окно не показывается (гейт Party_IsMeBoss, молча)"));
		return false;
	}
	if (!EnsureDisbandWidget())
	{
		UE_LOG(LogAsdaParty, Error, TEXT("[отряд] 2256: окно не собралось — показать нечего"));
		return false;
	}
	if (DisbandText) { DisbandText->SetText(FText::FromString(Loc(PartyLoc::kDisbandAsk))); }
	SetDisbandVisible(true);
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 2256 показано: «%s» (partyIndex=%d)"), *Loc(PartyLoc::kDisbandAsk), PartyIndex);
	return true;
}

// Общая часть 2258/2259 (`CParty_OnControl`, `social.c:2351-2361`):
//   `Net_RecvUiTick(); if (Party_IsInParty(g_CharManager)) Party_Send_InviteByName(g_nPartyInviteTargetIdx,
//    режим, (char*)(g_pLocalSessionCtx + 1056)); sub_93D82E(this);` — окно прячется В ЛЮБОМ случае.
//   ★ `g_nPartyInviteTargetIdx` у эталона пишут 5093 код 1 (@13) и отправитель 5096 (reqId) — это индекс
//     моего отряда; у порта он один на всё — `PartyIndex` (его же шлёт 2260), второго склада не заводим.
void UAsdaPartySubsystem::SendDisbandChoice(uint8 Mode, const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	bool bOk = false;
	if (bInParty && Net)
	{
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const FString MyName = WS ? WS->GetSelfCharName() : FString();
		bOk = Net->SendPartyLeave5098(PartyIndex, Mode, MyName);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: 5098 (partyIndex=%d, режим %u = %s, «%s») -> %s"),
			Why, PartyIndex, (uint32)Mode, Mode == 1 ? TEXT("IsDestroy, роспуск") : TEXT("выход"),
			*MyName, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	}
	else
	{
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: я не в отряде — 5098 не шлётся (эталон `if (Party_IsInParty)`)"), Why);
	}
	SetDisbandVisible(false);   // `sub_93D82E` → `sub_B99CF0`: SetVisible(m_pDisbandWin, 0)
}

void UAsdaPartySubsystem::OnDisbandOk()
{
	SendDisbandChoice(1, TEXT("2258 «ОК» окна роспуска"));
}

void UAsdaPartySubsystem::OnDisbandCancel()
{
	SendDisbandChoice(0, TEXT("2259 «Отмена» окна роспуска"));
}

// ═════════════════════════════ ★ ОТМЕТКИ СООТРЯДНИКОВ НА МИНИ-КАРТЕ (волна 1-Б) ═══════════════
// Порт `minimap_DrawPartyMarkers@0xB55A00` (`world_map.c:3080`) в части ДАННЫХ: что и где рисовать.
//   Сам квад 16×16 текстуры `miniPartyPos` рисует общая мини-карта (`UAsdaMinimapSubsystem::UpdateMap`
//   и `UpdateBigMap` — список `EAsdaMapMark::Party`), фильтр карты там же: мини-карта — карта отметки ==
//   текущая (эталон `v21[69] != g_nCurMapId`), большая — == показанная (`v21[69] != a3 && a3 >= 0`).
void UAsdaPartySubsystem::PushMinimapMarks()
{
	bMarksDirty = false;
	UGameInstance* GI = GetGameInstance();
	UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
	if (!MM) { return; }
	// Эталон: `result = Party_IsInParty(g_CharManager); if (result) { … }` — вне отряда не рисуется ничего.
	if (!bInParty)
	{
		MM->ClearMarks(EAsdaMapMark::Party);
		if (PushedMarkCount != 0) { UE_LOG(LogAsdaParty, Log, TEXT("[отряд] мини-карта: отметок отряда нет (не в отряде)")); }
		PushedMarkCount = 0;
		return;
	}
	const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	const int32 SelfCharId = Net ? Net->SelectedCharId : 0;
	const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	const int32 MyMap = LM ? LM->CurrentMapId : -1;
	const bool bNeedPos = (CVarMapMarkNeedPos.GetValueOnGameThread() != 0);

	TArray<FAsdaMapMark> Out;
	for (FAsdaPartyMember& M : Members)
	{
		// +272 — признак «на моей карте» (эталон пересчитывает его при смене карты, `objRefreshOwnershipFlags`).
		if (M.MapId >= 0) { M.bOnMyMap = (M.MapId == MyMap); }
		// Себя не рисуем: `if (v21[1] != CharManager_GetLocalPlayer()->userIdx)` (поле +0x41C = charId).
		if (SelfCharId != 0 && M.CharId == SelfCharId) { continue; }
		int32 MarkMap = M.MapId;
		if (!M.bPosKnown || M.MapId < 0)
		{
			if (bNeedPos) { continue; }   // ★ исправление §10: без координат отметки нет
			MarkMap = FMath::Max(0, M.MapId);   // как оригинал: конструктор записи — карта 0, тайл (0,0)
		}
		FAsdaMapMark K;
		K.TileX = M.TileX;
		K.TileY = M.TileZ;
		K.MapId = MarkMap;   // всегда явная карта: «−1 = текущая» мини-карты здесь НЕ используем
		K.Kind  = EAsdaMapMark::Party;
		Out.Add(K);
	}
	MM->SetMarks(EAsdaMapMark::Party, Out);
	if (Out.Num() != PushedMarkCount)
	{
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] мини-карта: отметок отряда %d (участников %d, моя карта %d)"),
			Out.Num(), Members.Num(), MyMap);
	}
	PushedMarkCount = Out.Num();
}

// Кнопка 2262 «Выгнать» (`social.c:2371-2392`): гейты 1089 (не глава) и 1090 (некого) → 5100.
void UAsdaPartySubsystem::OnBanishClicked()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!bIsLeader)
	{
		Notice(PartyLoc::kNotLeader);          // 1089 — эталонный порядок: сперва «я не глава»
		return;
	}
	const int32 M = (SelectedSlot >= 0 && Rows.IsValidIndex(SelectedSlot)) ? Rows[SelectedSlot].MemberIdx : INDEX_NONE;
	if (!bInParty || !Members.IsValidIndex(M))
	{
		Notice(PartyLoc::kNobodyToBanish);      // 1090 «Некого выгонять из отряда»
		return;
	}
	const FAsdaPartyMember& Mem = Members[M];
	const bool bOk = Net && Net->SendPartyBanish5100(Mem.CharId, PartyIndex, Mem.Name);
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 2262 «Выгнать» «%s» (charId=%d, partyIndex=%d) -> %s"),
		*Mem.Name, Mem.CharId, PartyIndex, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
}

// ═════════════════════ КЛИК ПО СТРОКЕ УЧАСТНИКА И МЕНЮ `Follow_Another_win` 8100 ══════════════
//
//  ★★★ ЧТО ЭТО ЗА МЕНЮ (по данным `gamemain_jui.ini:421957-422036`).
//  `Follow_Another_win` **8100**, скин `Popup_SmWin`, Size 160×76, Visible="0". Три пункта:
//      **8102** `Target_Share`   «Указать цель»  (корейская подпись «타겟_공유» = ОБЩАЯ ЦЕЛЬ)
//      **8101** `Follow_Another` «Следовать»     («따라가기»)
//      **8103** `Follow_CloseBtn` «Закрыть»      («닫기»)
//  Владелец окна — `CExchange` (`shop_trade.c:7443`, поле `pFollowWin`).
//
//  ★★★ ДЕФЕКТ ОРИГИНАЛА: ЭТО МЕНЮ НИКОГДА НЕ ПОКАЗЫВАЕТСЯ — вот почему «Указать цель»
//  «никогда не работало». Обработчики всех трёх пунктов ЖИВЫ (`exchangeUiOnCommand`, msgId
//  8101/8102/8103), но `CControl_SetVisible(pFollowWin, 1)` в образе НЕТ НИ ОДНОГО.
//  ДОКАЗАНО ДВУМЯ МЕТОДАМИ:
//    (1) по потоку: во ВСЁМ декомпиле `pFollowWin` встречается ровно четырежды и все четыре —
//        строки привязки (`FindControlByID(8100)`, две проверки на null, `SetField166/SetFlag668`);
//    (2) сплошной байтовый скан образа: `push 8100` в `.text` встречается ДВАЖДЫ —
//        `0x00C7810A` (та самая привязка) и `0x00F5065F`, где 8100 это НЕ контрол, а id строки
//        локализации: рядом лежат `push 10000` и `push 0FFFFD306h` (−11514) — пара «канал/цвет»
//        функции печати в чат. Форм `mov reg, 8100` нет вовсе.
//  ⇒ Меню собрано, подписи есть, ветки кода живые, а открыть его нечем.
//  ФИКС: вешаем меню на ПКМ по строке участника (ключ отката `asda.Party.FixMarkTarget 0`).
//
//  ★★★ ПОЛНЫЙ ФЛОУ «ОБЩЕЙ ЦЕЛИ» (кто, что, куда — см. также отчёт §11):
//    1. ЛКМ по строке участника (контролы `Party_MemClick{NN}`, 2294…2299) — эталон
//       (`AsdaStory_ida.c:430480`) кладёт номер строки в `g_nPartySelIdxPending`
//       (`= wParam - 2294`) и сбрасывает `g_nPartySelStatePending = -1`.
//    2. ПКМ по строке открывает меню 8100.
//    3. Пункт **8102** «Указать цель» (`AsdaStory_ida.c:504418-504441`):
//         if (g_nPartySelIdxPending != -1) {
//             g_nPartySelIdx = g_nPartySelIdxPending;      // запомнили, за кем следим
//             partyUI_ShowSelectedSlot(g_pPartyWnd);       // зажгли `Party_TargetShare{NN}`
//             copyStringFromArrayElem(g_pPartyWnd, g_nPartySelIdx, имя);
//             CObject_SetSlotString(ctx, 0, имя); нотис 20307 подсказкой
//         }
//         if (g_nPartySelStatePending != -1) { g_nPartySelIdx = -1; }   // снятие пометки
//       ⇒ ПАКЕТ НА СЕРВЕР НЕ УХОДИТ ВООБЩЕ. Это чисто КЛИЕНТСКАЯ пометка «я ассистирую вот
//       этому соотряднику»; другие участники её не видят, метка живёт только у меня.
//    4. Пользуются пометкой ГОРЯЧИЕ КЛАВИШИ: `UI_DispatchTargetContextMenu@0xC532D0`,
//       категория 2, случай 6 (`ui_core.c:32207-32285`) — «взять цель того, кого я пометил»:
//         гейты: не в отряде → 20306 · моё состояние 6 → 20302 · несу предмет → 20305 ·
//                `g_nPartySelIdx == -1` → 20304 · участник не найден → 20308 ·
//                его состояние 6 → 20301 · у него нет цели → **20300**;
//         иначе: renderId = LOWORD(его `targetRenderId`), если такой сущности нет — HIWORD;
//                и этот renderId становится МОЕЙ целью (`hudShowMobTargetInfo`,
//                `uiSwitchToBattlePanel`, запись в мой `targetRenderId`).
//       ★ `targetRenderId` соседа обновляется живыми пакетами: боевые пишут LOWORD
//         (`AsdaStory_ida.c:764632/764972/765403`), пакет движения — HIWORD (`:763605`),
//         снапшот появления — при входе в зону видимости (`GameNet_OnUserSpawn`, `:264351`).
//    5. Снимается пометка тем же пунктом при `g_nPartySelStatePending != -1`, а также когда
//       участник пропал из состава (`g_nPartySelIdx = -1`, `AsdaStory_ida.c:494068`).
//    ⇒ ГДЕ ВИДНА: только в строке отряда (`Party_TargetShare{NN}`). Ни панель цели, ни иконка
//      над головой, ни мини-карта в этом не участвуют — общей на весь отряд метки в протоколе НЕТ.
bool UAsdaPartySubsystem::EnsureRowMenu()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kRowMenuWinId, kRowMenuWbp, kRowMenuZ).Ensure(GetGameInstance(), RowMenuWidget, bRowMenuReady,
		[](UAsdaWindowSubsystem&) {},   // как было: протухшее окно отдельно не сносится
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupRowMenu();
			// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: щелчок мимо гасит меню; разбор — `AsdaWindowSubsystem_TopPress.cpp`.
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kRowMenuWinId, this, RowMenuWidget, bRowMenuOpen, TEXT("Follow_Another_win_0_%d"), 9);
			WM.RegisterClickAwayMenu(kRowMenuWinId, FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseRowMenu(); }));
		},
		[]()
		{
			UE_LOG(LogAsdaParty, Error, TEXT("[отряд] WBP_Follow_Another_win не загрузился (%s)"), kRowMenuWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bRowMenuReady;
}

void UAsdaPartySubsystem::SetupRowMenu()
{
	bRowMenuReady = false;
	if (!RowMenuWidget || !RowMenuWidget->WidgetTree) { return; }
	TArray<UWidget*> All;
	RowMenuWidget->WidgetTree->GetAllWidgets(All);
	UTextBlock* TMark = nullptr; UTextBlock* TFollow = nullptr; UTextBlock* TClose = nullptr;
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Target_Share"))        { RowMenuMark = B; }
			else if (N == TEXT("Follow_Another")) { RowMenuFollow = B; }
			else if (N == TEXT("Follow_CloseBtn")){ RowMenuClose = B; }
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Target_Share_Text"))        { TMark = T; }
			else if (N == TEXT("Follow_Another_Text")) { TFollow = T; }
			else if (N == TEXT("Follow_CloseBtn_Text")){ TClose = T; }
		}
	}
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(RowMenuMark,   8102, [this](int32) { OnRowMenuMarkTarget(); });
	Bind(RowMenuFollow, 8101, [this](int32) { OnRowMenuFollow(); });
	Bind(RowMenuClose,  8103, [this](int32) { CloseRowMenu(); });
	// Подписи — из таблицы текста, id взяты из `GameText.csv` по корейским подписям .JUI.
	if (TMark)   { TMark->SetText(FText::FromString(Loc(PartyLoc::kMenuMarkTarget))); }
	if (TFollow) { TFollow->SetText(FText::FromString(Loc(PartyLoc::kMenuFollow))); }
	if (TClose)  { TClose->SetText(FText::FromString(Loc(PartyLoc::kMenuClose))); }

	bRowMenuReady = (RowMenuMark && RowMenuFollow && RowMenuClose);
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] меню строки 8100 разобрано: 8102=%s 8101=%s 8103=%s"),
		RowMenuMark ? TEXT("да") : TEXT("НЕТ"), RowMenuFollow ? TEXT("да") : TEXT("НЕТ"),
		RowMenuClose ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaPartySubsystem::OpenRowMenu(int32 Slot)
{
	if (CVarFixMarkTarget.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogAsdaParty, Warning,
			TEXT("[отряд] 1:1 оригинал (asda.Party.FixMarkTarget 0): меню строки 8100 не показывается — "
			     "в эталоне `CControl_SetVisible(pFollowWin, 1)` нет ни одного вызова"));
		return;
	}
	if (!Rows.IsValidIndex(Slot) || Rows[Slot].MemberIdx == INDEX_NONE) { return; }
	if (!EnsureRowMenu()) { return; }
	MenuRowSlot = Slot;
	PendingRowSlot = Slot;                       // эталонный `g_nPartySelIdxPending = wParam - 2294`

	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ ПОЗИЦИЯ У КУРСОРА. Двигаем ДЕТЕЙ корневого канваса на дельту, а не корень: у детей
	//   запечена база из .JUI (Origin "519 266"), и сдвиг корня увёл бы окно ровно на неё
	//   (грабля, на которой уже обжигались с меню игрока). Анкоры учитывает общая точка.
	FVector2D Mouse2D(0.f, 0.f);
	if (APlayerController* PC = GI->GetWorld() ? GI->GetWorld()->GetFirstPlayerController() : nullptr)
	{
		float MX = 0.f, MY = 0.f;
		if (PC->GetMousePosition(MX, MY)) { Mouse2D = FVector2D(MX, MY); }
	}
	FVector2D Cur = AsdaUi::ScreenToCanvasLocal(RowMenuWidget, Mouse2D, GI->GetWorld());
	// ★ 08.10.2026 [ОРИГИНАЛ] (window_default_pos.md §6 п.11): эталон ставит окно 8100 НЕ у курсора, а справа от строки отряда —
	//   ui_core.c:11500-11510: `uiCalcAnchoredScreenPos(строка 2201+N)` → x = X_строки + w_строки + 30; y = СЫРОЙ Origin.y
	//   строки (+0x12C, относительно родителя — взят ДО пересчёта) + 10 → `uiWindowShowAndRegister@0xC86FB0` →
	//   `CControl_SetField74_75`. Якорь окна «5 0 0» ничего не добавляет. Откат: `asda.UI.JuiDefaultPos 0` — у курсора.
	if (UAsdaWindowSubsystem::UseOriginalDefaultPos() && Rows.IsValidIndex(Slot) && Rows[Slot].Root)
	{
		const UCanvasPanelSlot* RS = AsdaUi::SlotOf(Rows[Slot].Root);
		if (RS)
		{
			// Группа `PARTY_WIN{NN}_grp` несёт сырой Origin.y строки (её Top = Origin.y контрола 2201+N относительно окна 2200),
			//   а экранный X и ширину даёт план самого контрола `PARTY_WIN{NN}_<idx>_0` (группа шире — в ней значки и полосы).
			const FMargin RO = RS->GetOffsets();
			const FString PlanePrefix = FString::Printf(TEXT("PARTY_WIN%02d_"), Slot);
			const UWidget* Plane = nullptr;
			if (const UPanelWidget* G = Cast<UPanelWidget>(Rows[Slot].Root))
			{
				for (int32 i = 0; i < G->GetChildrenCount() && !Plane; ++i)
				{
					const UWidget* Ch = G->GetChildAt(i);
					const FString N = Ch ? Ch->GetName() : FString();
					if (N.StartsWith(PlanePrefix) && N.EndsWith(TEXT("_0")) && !N.EndsWith(TEXT("_grp"))) { Plane = Ch; }
				}
			}
			const UCanvasPanelSlot* PS = Plane ? AsdaUi::SlotOf(Plane) : nullptr;
			if (PS)
			{
				const FVector2D RowPos = AsdaUi::CanvasRootPos(Plane);   // экранная позиция контрола строки (корень окна во весь экран)
				const float RowW = PS->GetOffsets().Right;               // ширина контрола строки (точечный анкор)
				Cur = FVector2D(RowPos.X + RowW + 30.0, RO.Top + 10.0);
				UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 8100 по эталону: строка %d (%s) x=%.0f w=%.0f сырой y=%.0f → (%.0f,%.0f)"),
					Slot, *Plane->GetName(), RowPos.X, RowW, RO.Top, Cur.X, Cur.Y);
			}
		}
	}
	// ★ Двигаем ДЕТЕЙ корневого канваса на дельту (у них запечена база Origin "519 266" из .JUI),
	//   а не корень — иначе окно уедет ровно на эту базу (грабля меню игрока).
	if (UPanelWidget* Root = Cast<UPanelWidget>(RowMenuWidget->GetRootWidget()))
	{
		// ★ База берётся ОДИН РАЗ. Если считать её заново на каждом открытии, дельта ляжет
		//   поверх уже сдвинутых детей и меню будет уползать с каждым разом (в прогоне это
		//   видно как −2564;−139 на втором открытии).
		if (!bRowMenuBaseTaken)
		{
			FVector2D TL(FLT_MAX, FLT_MAX);
			for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
			{
				if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Root->GetChildAt(i)))
				{
					const FMargin O = CS->GetOffsets();
					TL.X = FMath::Min(TL.X, O.Left);
					TL.Y = FMath::Min(TL.Y, O.Top);
				}
			}
			if (TL.X < FLT_MAX) { RowMenuBaseTopLeft = TL; bRowMenuBaseTaken = true; }
		}
		const FVector2D TopLeft = RowMenuBaseTopLeft;
		if (bRowMenuBaseTaken)
		{
			// Текущий левый-верхний угол (он мог уехать с прошлого открытия) и дельта ОТ НЕГО
			//   до курсора: так позиция получается абсолютной и накопления не бывает.
			FVector2D CurTL(FLT_MAX, FLT_MAX);
			for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
			{
				if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Root->GetChildAt(i)))
				{
					const FMargin O = CS->GetOffsets();
					CurTL.X = FMath::Min(CurTL.X, O.Left);
					CurTL.Y = FMath::Min(CurTL.Y, O.Top);
				}
			}
			const FVector2D Delta = (CurTL.X < FLT_MAX) ? (Cur - CurTL) : FVector2D::ZeroVector;
			// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): SPAN-безопасный сдвиг детей корня (было `Left/Top += d`).
			AsdaUi::ShiftRootChildren(Root, Delta);
		}
	}
	WM->SetWindowVisible(kRowMenuWinId, true);
	WM->BringWindowToFront(kRowMenuWinId);
	bRowMenuOpen = true;
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] меню строки 8100 открыто по слоту %d у курсора (%.0f,%.0f)"),
		Slot, Cur.X, Cur.Y);
}

void UAsdaPartySubsystem::CloseRowMenu()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRowMenuWinId, false);
	}
	bRowMenuOpen = false;
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] меню строки 8100 закрыто (пункт 8103)"));
}

// Пункт 8102 «Указать цель» — 1:1 `exchangeUiOnCommand`, msgId 8102.
void UAsdaPartySubsystem::OnRowMenuMarkTarget()
{
	// Эталон: `if (g_nPartySelIdxPending != -1) { g_nPartySelIdx = pending; ShowSelectedSlot();
	//   имя из строки; нотис 20307 }`. ПАКЕТ НЕ УХОДИТ — пометка чисто клиентская.
	if (PendingRowSlot != INDEX_NONE && Rows.IsValidIndex(PendingRowSlot)
		&& Rows[PendingRowSlot].MemberIdx != INDEX_NONE)
	{
		SharedTargetSlot = PendingRowSlot;
		const FAsdaPartyMember& M = Members[Rows[SharedTargetSlot].MemberIdx];
		ApplyTargetShareHighlight();
		Notice(PartyLoc::kMarkTargetSet, M.Name);   // 20307 со слотом-именем
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] 8102 «Указать цель»: общая цель = слот %d («%s»); пакет НЕ шлётся (так в эталоне)"),
			SharedTargetSlot, *M.Name);
	}
	else
	{
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] 8102 «Указать цель»: строка не выбрана (эталонный pending == -1)"));
	}
	CloseRowMenu();
}

void UAsdaPartySubsystem::OnRowMenuFollow()
{
	// Эталон msgId 8101: гейт `g_bWorldTickActive && !g_pPartyTargetObj && !sub_922D5D(gacha)`,
	//   затем `uiPreparePartyTradeRequest(ctx, режим 1, userNum, charId)` — ТОТ ЖЕ механизм,
	//   что и у пункта 2062 меню по игроку, но с режимом 1.
	// ★ ОБЩАЯ ТОЧКА: `UAsdaPlayerMenuSubsystem::StartFollow(UserNum, CharId, Name, Mode)`.
	//   Копию логики следования не заводим (закон `port-shared-points`).
	const int32 M = (MenuRowSlot >= 0 && Rows.IsValidIndex(MenuRowSlot)) ? Rows[MenuRowSlot].MemberIdx : INDEX_NONE;
	if (!Members.IsValidIndex(M))
	{
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] 8101 «Следовать»: строка меню не указывает на участника"));
		CloseRowMenu();
		return;
	}
	const FAsdaPartyMember& Mem = Members[M];
	UGameInstance* GI = GetGameInstance();
	UAsdaPlayerMenuSubsystem* PM = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
	if (!PM)
	{
		UE_LOG(LogAsdaParty, Error, TEXT("[отряд] 8101 «Следовать»: подсистема меню игрока недоступна"));
		CloseRowMenu();
		return;
	}
	// ⚠ `StartFollow` ищет актора по userNum, а у участника из 5107 userNum отсутствует (−1):
	//   этот пакет несёт только charId и имя. Доразрешаем через реестр соседей, иначе вход
	//   молча откажет нотисом «цель не найдена».
	int32 UserNum = Mem.UserNum;
	if (UserNum < 0)
	{
		if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			UserNum = WS->GetUserNumByCharId(Mem.CharId);
		}
	}
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] 8101 «Следовать» за «%s» (userNum=%d charId=%d) -> StartFollow(режим 1)"),
		*Mem.Name, UserNum, Mem.CharId);
	PM->StartFollow(UserNum, Mem.CharId, Mem.Name, /*Mode*/ 1);
	CloseRowMenu();
}

// ЛКМ по строке: выбрать участника СВОЕЙ целью + обвести строку.
void UAsdaPartySubsystem::ClickRow(int32 Slot)
{
	if (!Rows.IsValidIndex(Slot) || Rows[Slot].MemberIdx == INDEX_NONE) { return; }
	PendingRowSlot = Slot;                        // эталонный `g_nPartySelIdxPending`
	SelectMemberSlot(Slot);                       // подсветка строки `Party_MemSelect{NN}`

	const FAsdaPartyMember& M = Members[Rows[Slot].MemberIdx];
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AActor* Actor = (WS && M.UserNum >= 0) ? WS->GetPlayerActorByUserNum(M.UserNum) : nullptr;
	if (!Actor)
	{
		// ⚠ Строка отряда — не актор: участник может быть далеко и не отрисован. Эталон в
		//   аналогичной ситуации (нет сущности по renderId) печатает 20308 и цель не меняет.
		Notice(PartyLoc::kTargetNotFound);         // 20308
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] клик по строке %d («%s»): актора нет (участник вне зоны отрисовки) — цель не меняем"),
			Slot, *M.Name);
		return;
	}
	// ★ ОБЩАЯ ТОЧКА ВЫБОРА ЦЕЛИ — `AAsdaPlayerController::SetTarget`, ровно та, которой
	//   пользуется клик в мире. Внутри она зовёт `UAsdaTargetFrameSubsystem::FillForPlayer`
	//   (у соседа он приватный), а тот, как аналог `hud_setTarget@0xE7B470`, сам отправляет
	//   запрос состояния **6538** в момент САМОЙ смены цели. Своей копии не пишем и в чужой
	//   файл не лезем.
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(
			GI->GetWorld() ? GI->GetWorld()->GetFirstPlayerController() : nullptr))
	{
		PC->SetTarget(Actor);
	}
	TargetedUserNum = M.UserNum;
	ApplyTargetShareHighlight();
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] клик по строке %d: цель = «%s» (userNum=%d)"),
		Slot, *M.Name, M.UserNum);
}

// Обводка «этот участник сейчас моя цель» + пометка общей цели (эталон `partyUI_ShowSelectedSlot`).
// ★★★ ДВЕ РАЗНЫЕ ПОДСВЕТКИ СТРОКИ — не путать (правка 2026-08-29 по замечанию владельца
//   «выделение не уходит, когда игрок больше не выбран»).
//   • `Party_TargetShare{NN}` (2288…2293) — ЗЕЛЁНАЯ обводка «этот участник — МОЯ ТЕКУЩАЯ ЦЕЛЬ».
//     Она обязана ГАСНУТЬ сама, поэтому считается КАЖДЫЙ тик от живого источника цели —
//     публичного `AAsdaPlayerController::TargetActor` (геттера у панели цели нет, а править
//     чужой файл нельзя). Это закрывает все четыре случая: цель снята, цель сменилась на
//     другого участника, цель сменилась на не-участника, участник вышел из отряда.
//   • `Party_MemSelect{NN}` (2282…2287) — пометка «ОБЩАЯ ЦЕЛЬ»: чью цель я забираю по
//     `ACTION TARGET SHARE`. Она НЕ гаснет сама и живёт до следующей пометки — так в эталоне.
//   ⚠ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА, ОСОЗНАННОЕ: в оригинале пометку «общая цель» рисует именно
//     `apTargetShare` (`partyUI_ShowSelectedSlot@0xB97F30` → `CParty+0x3C`, индексы 15…20),
//     а `apMemberSelect` (`+0x24`) гасится отдельной `partyHideMemberControls@0xB97FF0`.
//     Мы поменяли их ролями, потому что владелец видит зелёной именно `TargetShare` и просит,
//     чтобы зелёное обозначало ТЕКУЩУЮ ЦЕЛЬ. Возврат к эталонному распределению —
//     ключ `asda.Party.SwapRowHighlights 0`.
void UAsdaPartySubsystem::ApplyTargetShareHighlight()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W ? W->GetFirstPlayerController() : nullptr);
	const AActor* CurTarget = PC ? PC->TargetActor.Get() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const bool bSwap = CVarSwapRowHighlights.GetValueOnGameThread() != 0;

	for (int32 S = 0; S < Rows.Num(); ++S)
	{
		const int32 M = Rows[S].MemberIdx;
		// «Он моя текущая цель» — сравниваем АКТОРАМИ: участник мог выйти из отряда, уйти из
		//   зоны отрисовки или смениться в слоте, и любое из этих событий должно гасить обводку.
		// ★★★ ПРИЧИНА, ПО КОТОРОЙ ЗЕЛЁНОЙ ОБВОДКИ НЕ БЫЛО ВООБЩЕ (найдено 2026-08-29):
		//   состав отряда приходит пакетом **5107**, а он несёт ТОЛЬКО charId и имя — userNum
		//   в записи участника так и оставался −1. Условие `UserNum >= 0` не выполнялось
		//   никогда, поэтому `bIsMyTarget` был всегда false. Ищем актора и по charId.
		bool bIsMyTarget = false;
		if (CurTarget && WS && Members.IsValidIndex(M))
		{
			const FAsdaPartyMember& Mem = Members[M];
			const AActor* MemActor = (Mem.UserNum >= 0) ? WS->GetPlayerActorByUserNum(Mem.UserNum) : nullptr;
			if (!MemActor && Mem.CharId != 0) { MemActor = WS->GetPlayerActorByCharId(Mem.CharId); }
			bIsMyTarget = (MemActor != nullptr && MemActor == CurTarget);
		}
		const bool bShared = Members.IsValidIndex(M) && (S == SharedTargetSlot);

		UImage* Green = bSwap ? Rows[S].TargetShare.Get() : Rows[S].Select.Get();
		UImage* Mark  = bSwap ? Rows[S].Select.Get()      : Rows[S].TargetShare.Get();
		if (Green) { Green->SetVisibility(bIsMyTarget ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
		if (Mark)  { Mark->SetVisibility(bShared     ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	}
	// Цель ушла совсем — снимаем и наш кэш, иначе он соврёт следующему вызову.
	if (!CurTarget) { TargetedUserNum = -1; }
}

// ═════════════ ★ 25.10.2026 [ОРИГИНАЛ] ГОРЯЧИЕ КЛАВИШИ / ЭМОЦИИ ОТРЯДА (разбор — у объявлений) ═════════════
bool UAsdaPartySubsystem::DispatchPartyMenu(int32 Idx, int32 InTargetUserNum, const FString& InTargetName, const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	switch (Idx)
	{
	case 0:
		// `if (!g_nInputControlState) { g_nPartyUiMode = 0; g_nPartyInviteTargetUid = g_nTargetUserIdx; exchangeUiOnCommand(0x7D1) }` —
		//   цель-персонаж обязательна (моб/NPC — ветки нет). Гейты окна 2250 — `OpenInviteWindow` (вход клавиши).
		if (InTargetUserNum < 0)
		{
			UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: «пригласить» — цель не персонаж (g_nInputControlState ≠ 0), молча"), Why);
			return false;
		}
		return OpenInviteWindow(InTargetUserNum, InTargetName, /*bSkipPresenceGate*/ false, /*bFromHotkey*/ true);
	case 1:
	{
		// `if (Party_IsInParty) Party_Send_InviteByName(Party_GetMyPartyIdx, 0, имя)` — главе тоже без окна роспуска.
		if (!bInParty || !Net) { UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: «выйти» — не в отряде, молча"), Why); return false; }
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		const FString MyName = WS ? WS->GetSelfCharName() : FString();
		const bool bOk = Net->SendPartyLeave5098(PartyIndex, 0, MyName);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: «выйти» → 5098 (partyIndex=%d, режим 0, «%s») -> %s"), Why, PartyIndex, *MyName,
			bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
		return bOk;
	}
	case 2:
	{
		// `if (!Party_IsMeBoss) → 1089; else if (Party_IsInParty && partyGetMemberMenuIdx() != -1) { участник по слоту +100 →
		//   5100, нет → 1090 }` — при невыбранной строке эталон молчит (в отличие от кнопки 2262).
		if (!IsPartyLeader()) { Notice(PartyLoc::kNotLeader); return false; }
		if (SelectedSlot < 0) { UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: «выгнать» — строка панели не выбрана, молча (эталон)"), Why); return false; }
		const FAsdaPartyMember* Mem = Members.FindByPredicate([this](const FAsdaPartyMember& M) { return M.SlotIndex == SelectedSlot; });
		if (!Mem) { Notice(PartyLoc::kNobodyToBanish); return false; }
		const bool bOk = Net && Net->SendPartyBanish5100(Mem->CharId, PartyIndex, Mem->Name);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: «выгнать» «%s» (charId=%d, слот %d) → 5100 -> %s"), Why, *Mem->Name, Mem->CharId,
			SelectedSlot, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
		return bOk;
	}
	case 3:
		if (!IsPartyLeader()) { Notice(PartyLoc::kNotLeader); }   // эталон: только нотис 1089, иначе ничего
		return false;
	default:
		return false;
	}
}

bool UAsdaPartySubsystem::TakeSharedTarget(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W ? W->GetFirstPlayerController() : nullptr);
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!PC || !WS) { return false; }
	// `Party_IsInParty != -1 && Party_IsInParty` (размер списка состава) — иначе 20306 и `g_nPartySelIdxPending = -1`.
	if (!bInParty || Members.Num() == 0)
	{
		Notice(PartyLoc::kShareNotInParty);
		PendingRowSlot = -1;
		return false;
	}
	if (PC->IsSelfDead()) { Notice(PartyLoc::kShareSelfDead); return false; }                   // моё состояние 6
	if (Me && Me->IsMounted()) { Notice(PartyLoc::kShareOnVehicle); return false; }             // `listContainsByTypeId(15, 2)`
	if (SharedTargetSlot == -1) { Notice(PartyLoc::kShareNotMarked); return false; }
	// Имя строки `g_nPartySelIdx` → участник по имени (`Party_FindMemberByName`); нет — пометка снимается молча.
	const int32 M = Rows.IsValidIndex(SharedTargetSlot) ? Rows[SharedTargetSlot].MemberIdx : INDEX_NONE;
	if (!Members.IsValidIndex(M))
	{
		SharedTargetSlot = -1;
		ApplyTargetShareHighlight();
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: помеченная строка пуста — пометка снята (эталон `g_nPartySelIdx = -1`)"), Why);
		return false;
	}
	const FAsdaPartyMember& Mem = Members[M];
	// `CharManager_FindByCharId(+4)` — соотрядник в зоне видимости.
	const AAsdaPlayerCharacter* Mate = Cast<AAsdaPlayerCharacter>(Mem.CharId != 0 ? WS->GetPlayerActorByCharId(Mem.CharId) : nullptr);
	if (!Mate) { Notice(PartyLoc::kTargetNotFound); return false; }                               // 20308
	if (Mate->IsIncapacitated()) { Notice(PartyLoc::kShareMateDead); return false; }               // его состояние 6
	// Цель = LOWORD(`targetRenderId`) — цель его атаки (у порта `AttackFaceTarget`: моб — renderId, персонаж — userIdx, как
	//   у эталона одно слово); нет такой СУЩНОСТИ → HIWORD — цель его хода (`MoveTargetRenderId`, пишет 4007 поле +25).
	int32 Rid = -1;
	if (const AActor* T = Mate->AttackFaceTarget.Get())
	{
		if (const AAsdaMobActor* TM = Cast<AAsdaMobActor>(T)) { Rid = TM->NetRenderId; }
		else { Rid = WS->FindUserIdxByActor(T); }
	}
	if (Rid < 0 || !WS->FindNetMobByRid(Rid)) { Rid = Mate->MoveTargetRenderId; }
	AAsdaMobActor* Mob = (Rid >= 0) ? WS->FindNetMobByRid(Rid) : nullptr;
	if (!Mob) { Notice(PartyLoc::kShareNoTarget); return false; }                                  // 20300
	// `g_nInputControlState = 1; g_nTargetUserIdx = rid; hudShowMobTargetInfo; uiSwitchToBattlePanel` — общая точка выбора цели.
	//   [ПОРТ] запись в мой `targetRenderId` (LOWORD и `gap_A42[15544]`) — поле разворота атаки; у порта его ставит сам бой.
	PC->SetTarget(Mob);
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: общая цель от «%s» — моб rid=%d «%s»"), Why, *Mem.Name, Rid, *Mob->MobName);
	return true;
}

bool UAsdaPartySubsystem::ShareTargetNext(const TCHAR* Why)
{
	// `if (Party_IsInParty != -1) { v10 = size − 1; if (++g_nPartySelIdx >= v10) g_nPartySelIdx = 0; … }` — размер списка состава
	//   (со мной), то есть круг по строкам 0 … size−2 (у главы и у рядового строк соотрядников ровно size−1).
	const int32 Count = bInParty ? Members.Num() : 0;
	const int32 Last = Count - 1;
	if (++SharedTargetSlot >= Last) { SharedTargetSlot = 0; }
	const int32 M = Rows.IsValidIndex(SharedTargetSlot) ? Rows[SharedTargetSlot].MemberIdx : INDEX_NONE;
	bool bOk = false;
	if (Members.IsValidIndex(M))
	{
		// Нотис 20307 «[ss1] может выбрать монстра, которого будет атаковать» (`CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)`).
		Notice(PartyLoc::kMarkTargetSet, Members[M].Name);
		bOk = true;
	}
	else
	{
		SharedTargetSlot = -1;   // строка пуста (`Party_FindMemberByName` не нашёл) — пометка снята
	}
	ApplyTargetShareHighlight();   // `partyUI_ShowSelectedSlot`
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: пометка общей цели → слот %d%s"), Why, SharedTargetSlot,
		bOk ? *FString::Printf(TEXT(" («%s»)"), *Members[M].Name) : TEXT(" (снята)"));
	return bOk;
}

bool UAsdaPartySubsystem::SelectSelfOrNextMember(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W ? W->GetFirstPlayerController() : nullptr);
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!PC || !WS) { return false; }
	auto TargetSelf = [PC, Why]()
	{
		// `g_nInputControlState = 0; g_nTargetUserIdx = мой; TargetNameplate_Update; HUD_UpdateTargetFrame; hud_setTarget` — я.
		PC->SetTarget(PC->GetPawn());
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: Shift+Tab — цель = я"), Why);
	};
	// `g_nInputControlState == 11 || !Party_IsInParty` — вне отряда (11 = переподключение) цель = я, круг не трогается.
	if (!bInParty || Members.Num() == 0) { TargetSelf(); return true; }
	// Круг по строкам 0…5 (`switch (g_nDlgStateMachine)` с проваливанием): участник по имени строки найден — шаг сделан, даже
	//   если его актора рядом нет (тогда цель не меняется); строки нет — следующая. После шестой строки — я (−1 → 0).
	for (int32 S = FMath::Max(SelectCycleState, 0); SelectCycleState >= 0 && S < kRosterSlots; ++S)
	{
		const int32 M = Rows.IsValidIndex(S) ? Rows[S].MemberIdx : INDEX_NONE;
		if (!Members.IsValidIndex(M)) { continue; }                              // `if (!MemberByName) goto LABEL_next`
		const FAsdaPartyMember& Mem = Members[M];
		AActor* Mate = Mem.CharId != 0 ? WS->GetPlayerActorByCharId(Mem.CharId) : nullptr;
		if (Mate) { PC->SetTarget(Mate); }
		SelectCycleState = (S + 1 < kRosterSlots) ? S + 1 : -1;
		ApplyTargetShareHighlight();   // `partyHideMemberControls` + `uiPartySlotControlShow(слот цели)` — обводка «моя цель»
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: Shift+Tab — строка %d «%s»%s, следующий шаг %d"), Why, S, *Mem.Name,
			Mate ? TEXT(" — цель") : TEXT(" — нет рядом, цель не меняется"), SelectCycleState);
		return Mate != nullptr;
	}
	// `default:` — цель = я, круг заново.
	// [ИСПРАВЛЕНИЕ] эталон в этой ветке зовёт `uiPartySlotControlShow(g_pPartyWnd, −1)` (индекс −1 → чужое поле `this[8]`);
	//   у порта обводка строк считается от цели — для «цель = я» не горит ни одна.
	TargetSelf();
	SelectCycleState = 0;
	ApplyTargetShareHighlight();
	return true;
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════
FString UAsdaPartySubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

EAsdaMsgCategory UAsdaPartySubsystem::CategoryForNotice(int32 /*LocId*/)
{
	// Вся цепочка отряда — «социальное» (категория 12 → системный журнал по DT_MessageRoutes).
	//   Отдельного канала «отряд» в разведённых маршрутах нет, выдумывать его не будем.
	return EAsdaMsgCategory::Social;
}

void UAsdaPartySubsystem::Notice(int32 LocId, const FString& NameSlot,
                                 const FString& NamePrefix, bool bWarnColor) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!TS) { return; }
	const FString Raw = TS->GetTextById(LocId);
	// ★ Маркеры [ss1]/[sd1] обязаны заполняться через UAsdaTextSubsystem, иначе уйдут сырыми.
	const bool bHasMarkers = UAsdaTextSubsystem::HasNoticeMarkers(Raw);
	FString Line = (bHasMarkers || !NameSlot.IsEmpty())
		? TS->FormatById(LocId, { NameSlot }, {})
		: Raw;
	// ★ Эталон в части строк ПРИКЛЕИВАЕТ имя спереди: `sprintf(buf, "%s%s", имя, текст)`
	//   (Party_DispatchEventNotice, Notice_ShowResultByCode, Recv_GM_*). Слота там нет —
	//   воспроизводим именно склейку, а не подстановку.
	if (!NamePrefix.IsEmpty()) { Line = NamePrefix + Line; }

	const EAsdaMsgCategory Cat = CategoryForNotice(LocId);
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::ChatText(GI, Cat, Line);
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] нотис %d (категория %d%s): %s"),
		LocId, (int32)Cat, bWarnColor ? TEXT(", эталонный цвет КРАСНЫЙ") : TEXT(""), *Line);
}
