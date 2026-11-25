// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPartySubsystem_Roster.cpp — ★ R3-c 12.11.2026 (разрез AsdaPartySubsystem.cpp, дословный перенос): состав отряда: виджет, контролы, обновление, значки состояния, мышь и наведение, выбор, выход.
//   Общее для частей — в AsdaPartySubsystem_Private.h; шапка и реверс — в AsdaPartySubsystem.cpp.
#include "UI/AsdaPartySubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaSoundSubsystem.h" // звук 215 при приходе заявки
#include "UI/AsdaHotbarSubsystem.h"       // иконка уведомления (statusBarAddIcon)
#include "UI/AsdaEffectsSubsystem.h"    // ★ stateinfotip/soulstateinfotip — иконки состояний
#include "UI/AsdaMiniStateSubsystem.h"  // ★ 30.10.2026: общая плашка подсказки (ShowSharedTip/HideSharedTip)
#include "UI/AsdaAnimatedPlane.h"       // ★ 30.10.2026: анимированная шестерёнка 2281 (наведение на 2279)
#include "UI/AsdaTipPanel.h"            // ★ 30.10.2026: EAsdaTipOwner::PartyHud
#include "UI/AsdaPlayerMenuSubsystem.h"  // ★ ОБЩАЯ точка следования StartFollow
#include "GameFramework/PlayerController.h"
#include "Components/PanelWidget.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"      // ★ волна 1-Б: отметки соотрядников (EAsdaMapMark::Party, §10 заголовка)
#include "World/AsdaLoadManagerSubsystem.h" // ★ волна 1-Б: моя карта (g_nCurMapId) — признак +272 «на моей карте»
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaWorldKind.h"         // ★ 22.10.2026: экран загрузки/логин — панель там не восстанавливаем

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)
#include "AsdaPartySubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPartySubsystem*.cpp

namespace {
	const TCHAR* kRosterWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTY_WIN.WBP_PARTY_WIN_C");

	constexpr int32 kStateIcons  = 28;   // 28 слотов состояний на строку (33100…33127 и т.д.)

	// ★★★ 22.10.2026: панель 2200 переживает снос окна (травел, `CloseAll` из `RevealWorld`) — разбор у объявления
	//   `HealRosterIfLost` (AsdaPartySubsystem.h). [ИСПРАВЛЕНИЕ ПОРТА] — у оригинала окно постоянное.
	ASDA_DEV_CVAR(int32, CVarRosterSurvivesWipe,
		TEXT("asda.Party.RosterSurvivesWipe"), 1,
		TEXT("1 (по умолчанию) = пока отряд есть, снесённая панель 2200 (смена карты, воскрешение, закрытие всех окон) сразу\n")
		TEXT("пересобирается с тем же составом и свёрнутостью — как постоянное окно оригинала.\n")
		TEXT("0 = как прежде: панель возвращает только следующий 5107 сервера."),
		ECVF_Default);
}

// ═════════════════════════════ ОКНО 2200: ПАНЕЛЬ ОТРЯДА В HUD ═════════════════════════════════
//  Порт `PartyWnd_UpdateMemberList@0xB99EA0` (`social.c:2851-3098`) и
//  `partyUpdateStateIcons@0xB97930` (`social.c:2083-2168`).
//
//  ★★★ ЧТО ПОКАЗЫВАЕТ ПАНЕЛЬ. Только ЧУЖИЕ строки: себя эталон пропускает в обеих ветках
//  (`if (memberRec+4 == myCharId) { ++iter; … }`). Свои HP/MP живут в основном HUD.
//  Глава отряда закреплён за слотом 0 и помечен значком `PARTY_ICON00` — это и есть та самая
//  «звёздочка»: `apClassIcon[0]` показывается РОВНО в ветке `memberRec+72 == 1` (`social.c:2964`).
//  Когда глава — Я САМ, меня в списке нет, и значок не показывается никому (ветка
//  `Party_IsMeBoss`, `social.c:2881-2955`, `apClassIcon` там не трогается вовсе).
void UAsdaPartySubsystem::ClipBar(UImage* Fill, float FullW, float Frac)
{
	// ★★★ ИДЕМПОТЕНТНО И БЕЗУСЛОВНО. Раньше эту функцию звали ТОЛЬКО когда
	//   `FAsdaBarSmoother::Advance` вернул true, и именно на этом «100 % рисовалось как 50 %»:
	//     `SetTarget` при ПЕРВОМ вызове делает `Shown = Target` (`AsdaBarSmoother.h:41`),
	//     а `Advance` первым же условием отвечает `if (Shown == Target) return false`
	//     (`:52`). У соотрядца с ПОЛНЫМ здоровьем доля не меняется никогда ⇒ `Advance` всегда
	//     false ⇒ ширину заливки не писал никто, и она оставалась АВТОРСКОЙ из ассета —
	//     64 при дорожке 128, то есть ровно половина.
	//   На моём стенде баг не воспроизводился, потому что я подавал 5109 с РАЗНЫМИ значениями
	//   (100 -> 50 -> 25): доля менялась, `Advance` возвращал true и заливку кто-то писал.
	//   Теперь пишем всегда, а лишние записи гасит сравнение ниже (перекладка слота дорогая).
	// Полосы `Blue2Ton_Bar`: ES0 подложка, ES1 заливка. Эталон зовёт `CProgressBar_SetValue`
	//   с ПРОЦЕНТОМ (`100 * cur / max`), у нас эквивалент — ширина плана заливки.
	UCanvasPanelSlot* S = Fill ? AsdaUi::SlotOf(Fill) : nullptr;
	if (!S || FullW <= 0.f) { return; }
	FMargin O = S->GetOffsets();
	const float NewW = FullW * FMath::Clamp(Frac, 0.f, 1.f);
	if (FMath::IsNearlyEqual(O.Right, NewW, 0.01f)) { return; }   // ничего не изменилось
	O.Right = NewW;
	S->SetOffsets(O);
}

bool UAsdaPartySubsystem::EnsureRosterWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kRosterWinId, kRosterWbp, kRosterZ).Ensure(GetGameInstance(), RosterWidget, bRosterReady,
		[](UAsdaWindowSubsystem&) {},   // как было: протухшее окно отдельно не сносится
		[this](UAsdaWindowSubsystem&)
		{
			SetupRosterControls();
		},
		[]()
		{
			UE_LOG(LogAsdaParty, Error, TEXT("[отряд] WBP_PARTY_WIN не загрузился (%s)"), kRosterWbp);
		});
	// Скрытие окна (шаг каркаса): .JUI: PARTY_WIN Visible="0"
	return FAsdaJuiWindowHandle::Succeeded(R) && bRosterReady;
}

void UAsdaPartySubsystem::SetupRosterControls()
{
	bRosterReady = false;
	Rows.Reset();
	if (!RosterWidget || !RosterWidget->WidgetTree) { return; }

	// Верхний уровень: две «шапки» (нормальная и свёрнутая) и три кнопки.
	RosterNormal = AsdaUi::FindNodeByPrefix(RosterWidget, TEXT("Party_WinN_grp"));
	RosterHeader = AsdaUi::FindNodeByPrefix(RosterWidget, TEXT("Party_WinH_grp"));
	TArray<UWidget*> All;
	RosterWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("PARTY_TOOLBUT00")) { BtnBanish = B; }
			else if (N == TEXT("PARTY_EXITBUT")) { BtnLeave = B; }
			else if (N == TEXT("PARTY_MINIBUT_H")) { BtnMini = B; }
			continue;
		}
		if (UCheckBox* Cb = Cast<UCheckBox>(W))
		{
			if (N == TEXT("PARTY_MINIBUT")) { BtnMiniCheck = Cb; }
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("PARTY_TOOLBUT00_Text")) { BtnBanishText = T; }
			else if (N == TEXT("PARTY_EXITBUT_Text")) { BtnLeaveText = T; }
		}
	}

	// ★★★ КРЕСТИК ОКНА БЫЛ ТЁМНЫЙ, «как будто нажат» (замечание владельца).
	//   Крестик развёрнутого окна — `PARTY_MINIBUT`, скин `CloseBtn_18_Party` (`Type = "PushButton"`,
	//   кадры SS0 «обычный» UV 0.414,0.469 и SS2 «нажатый» UV 0.916,0.451). Импортёр сделал из
	//   него `UCheckBox`, а у чекбокса «нажатое» состояние — это ВЗВЕДЁННОЕ: пока он `Checked`,
	//   рисуется кадр нажатия. Снимаем взвод и пишем фактическое состояние в лог, чтобы это
	//   было видно и в живой сессии, а не только у меня.
	if (BtnMiniCheck)
	{
		const bool bWasChecked = BtnMiniCheck->IsChecked();
		if (bWasChecked) { BtnMiniCheck->SetIsChecked(false); }
		// ★ UV КИСТЕЙ ЧИТАЕМ ИЗ C++ (через Python `FSlateBrush` не отдаёт `uv_region`).
		//   Скин `CloseBtn_18_Party`: SS0 «обычный» UV (0.414,0.469)-(0.449,0.504),
		//   SS2 «нажатый» UV (0.916,0.451)-(0.951,0.486); SS1 в скине нет.
		//   Если в «обычной» кисти окажется UV нажатого кадра — виноват импорт, правим в ассете.
		const FCheckBoxStyle& St = BtnMiniCheck->GetWidgetStyle();
		auto UvOf = [](const FSlateBrush& B) -> FString
		{
			const FBox2f R = B.GetUVRegion();
			if (R.Min.IsNearlyZero() && R.Max.IsNearlyZero()) { return TEXT("UV не задан"); }
			return FString::Printf(TEXT("(%.3f,%.3f)-(%.3f,%.3f)"), R.Min.X, R.Min.Y, R.Max.X, R.Max.Y);
		};
		auto Frame = [](const FSlateBrush& B) -> const TCHAR*
		{
			const FBox2f R = B.GetUVRegion();
			// Кадры скина `Party_Close`: SS0 покой, SS1 наведение, SS2 взведён, SS3 взведён+наведение.
			if (FMath::IsNearlyEqual(R.Min.X, 0.156f, 0.01f)) { return TEXT("SS0 покой"); }
			if (FMath::IsNearlyEqual(R.Min.X, 0.199f, 0.01f)) { return TEXT("SS1 наведение"); }
			if (FMath::IsNearlyEqual(R.Min.X, 0.414f, 0.01f)) { return TEXT("SS2 ВЗВЕДЁН"); }
			if (FMath::IsNearlyEqual(R.Min.X, 0.457f, 0.01f)) { return TEXT("SS3 взведён+наведение"); }
			return TEXT("кадр не из скина Party_Close");
		};
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] крестик PARTY_MINIBUT: состояние %s -> %s"),
			bWasChecked ? TEXT("ВЗВЕДЁН") : TEXT("снят"),
			BtnMiniCheck->IsChecked() ? TEXT("взведён") : TEXT("снят"));
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] крестик кисти: Unchecked %s [%s] | UncheckedHovered %s [%s] | UncheckedPressed %s [%s]"),
			*UvOf(St.UncheckedImage), Frame(St.UncheckedImage),
			*UvOf(St.UncheckedHoveredImage), Frame(St.UncheckedHoveredImage),
			*UvOf(St.UncheckedPressedImage), Frame(St.UncheckedPressedImage));
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] крестик кисти: Checked %s [%s] | CheckedHovered %s [%s] | CheckedPressed %s [%s]"),
			*UvOf(St.CheckedImage), Frame(St.CheckedImage),
			*UvOf(St.CheckedHoveredImage), Frame(St.CheckedHoveredImage),
			*UvOf(St.CheckedPressedImage), Frame(St.CheckedPressedImage));

		// ★★★ ОТМЕНЁННАЯ ПРАВКА — ЧИТАТЬ ОБЯЗАТЕЛЬНО (2026-08-29, ночь).
		//   Я объявил «импортёр смещает кадры» и ставил в кадр покоя UV из `UncheckedPressed`.
		//   ЭТО БЫЛО НЕВЕРНО: сверялся с ЧУЖИМ скином. `PARTY_MINIBUT` использует скин
		//   **`Party_Close`**, а `CloseBtn_18_Party` — это соседний контрол `PARTY_MINIBUT_H`.
		//   `Party_Close` в `JuiSkin.ini` имеет Type = CheckButton и ЧЕТЫРЕ кадра:
		//       SS0 (0.156,0.469)-(0.191,0.504)  — не взведён, ПОКОЙ
		//       SS1 (0.199,0.469)-(0.234,0.504)  — не взведён, наведение
		//       SS2 (0.414,0.469)-(0.449,0.504)  — ВЗВЕДЁН
		//       SS3 (0.457,0.469)-(0.492,0.504)  — взведён, наведение
		//   Импортёр разложил их ТОЧНО: Unchecked=SS0, UncheckedHovered=SS1,
		//   UncheckedPressed/Checked=SS2, CheckedHovered/CheckedPressed=SS3.
		//   ⇒ дефекта импортёра НЕТ, а моя «правка» подменяла кадр покоя кадром ВЗВЕДЁННОГО,
		//   то есть сама и делала крестик «как будто нажатым». Мутация удалена.
	}

	// Шесть строк. Планы у импортёра нумерованы СКВОЗНО по окну (`PARTY_HP00_31_0`,
	//   `PARTY_HP01_82_0`, …), поэтому ищем строго по ПРЕФИКСУ, а не по точному имени.
	Rows.SetNum(kRosterSlots);
	for (int32 R = 0; R < kRosterSlots; ++R)
	{
		FAsdaPartyRowUi& Row = Rows[R];
		const FString NN = FString::Printf(TEXT("%02d"), R);
		Row.Root      = AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_WIN")) + NN + TEXT("_grp")));
		Row.Name      = Cast<UTextBlock>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_NAME")) + NN)));
		Row.Level     = Cast<UTextBlock>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_LEVEL")) + NN)));
		Row.Sword     = Cast<UImage>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_SWORD")) + NN)));
		Row.Bow       = Cast<UImage>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_BOW")) + NN)));
		Row.Staff     = Cast<UImage>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_STAFF")) + NN)));
		Row.LeaderIcon= Cast<UImage>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_ICON")) + NN + TEXT("_"))));
		Row.Select    = Cast<UImage>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("Party_MemSelect")) + NN)));
		// ★ ЗЕЛЁНАЯ ОБВОДКА «он выбран целью» — это `Party_TargetShare{NN}` (2288…2293).
		//   Доказательство: `partyUI_ShowSelectedSlot@0xB97F30` (`social.c:2192`) показывает
		//   `*(this + i + 15)`, а индекс 15 у `CParty` — это `+0x3C` = `apTargetShare`
		//   (`structs_ru.h`), тогда как `apMemberSelect` лежит по `+0x24` (индексы 9…14) и
		//   гасится отдельной `partyHideMemberControls@0xB97FF0`. Имена контролов тут не врут,
		//   но совпадение проверено ПО СМЕЩЕНИЮ, а не по имени.
		Row.TargetShare = Cast<UImage>(AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("Party_TargetShare")) + NN)));
		Row.StateRoot = AsdaUi::FindNodeByPrefix(RosterWidget, *(FString(TEXT("PARTY_ICONBACK")) + NN + TEXT("_grp")));

		// ★ Заливка полосы — ВТОРОЙ план (`_1`), первый (`_0`) — подложка (скин `Blue2Ton_Bar`,
		//   закон `ui-progressbar-animate-existing`). Поиск по префиксу вернул бы подложку.
		UImage* HpBack = nullptr; UImage* MpBack = nullptr;
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			if (N.StartsWith(FString(TEXT("PARTY_HP")) + NN + TEXT("_")))
			{
				if (N.EndsWith(TEXT("_1"))) { Row.HpFill = Cast<UImage>(W); }
				else if (N.EndsWith(TEXT("_0"))) { HpBack = Cast<UImage>(W); }
			}
			else if (N.StartsWith(FString(TEXT("PARTY_MP")) + NN + TEXT("_")))
			{
				if (N.EndsWith(TEXT("_1"))) { Row.MpFill = Cast<UImage>(W); }
				else if (N.EndsWith(TEXT("_0"))) { MpBack = Cast<UImage>(W); }
			}
		}
		// ★★★ ПОЛНАЯ ШИРИНА — С ПОДЛОЖКИ (план ES0), А НЕ С ЗАЛИВКИ (ES1).
		//   ПРИЧИНА БАГА «100 % рисуется как 50 %»: у скина `Blue2Ton_Bar` (`JuiSkin.ini`)
		//       ES=0 Rect = "0 0 100 10"   ← дорожка
		//       ES=1 Rect = "0 0  50 10"   ← заливка НАРИСОВАНА В ПОЛОВИНУ ДОРОЖКИ
		//   Импортёр перенёс обе ширины как есть, и мы брали за «100 %» ширину ЗАЛИВКИ (50 %
		//   дорожки). При доле 1.0 получалось ровно пол-полосы. Берём ширину подложки —
		//   и левый край заливки тоже равняем по ней, иначе при узкой заливке останется зазор.
		auto TakeTrack = [](UImage* Back, UImage* Fill, float& OutFullW)
		{
			const UCanvasPanelSlot* B = Back ? AsdaUi::SlotOf(Back) : nullptr;
			UCanvasPanelSlot* F = Fill ? AsdaUi::SlotOf(Fill) : nullptr;
			if (!B || !F) { return; }
			const FMargin BO = B->GetOffsets();
			OutFullW = BO.Right;                     // ширина ДОРОЖКИ = 100 %
			FMargin FO = F->GetOffsets();
			FO.Left = BO.Left;                       // заливка начинается там же, где дорожка
			F->SetOffsets(FO);
		};
		TakeTrack(HpBack, Row.HpFill, Row.HpFullW);
		TakeTrack(MpBack, Row.MpFill, Row.MpFullW);

		// 28 иконок состояний: `PARTY_STATE_ICON{строка}{номер 00..27}_{n}_0`.
		Row.StateIcons.SetNum(kStateIcons);
		for (int32 I = 0; I < kStateIcons; ++I)
		{
			const FString Pref = FString::Printf(TEXT("PARTY_STATE_ICON%d%02d_"), R, I);
			Row.StateIcons[I] = Cast<UImage>(AsdaUi::FindNodeByPrefix(RosterWidget, *Pref));
		}
		if (Row.Root) { Row.Root->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// ★★★ ПРИЧИНА «ОКНО ЗАТЕМНЕНО, КАК БУДТО ТИНТ» (замечание владельца).
	//   Свёрнутый заголовок `Party_WinH_grp` и развёрнутое тело `Party_WinN_grp` — ДВА
	//   АЛЬТЕРНАТИВНЫХ вида одного окна, и в ассете ОБА пришли видимыми. Мы переключали их
	//   только в `ToggleRosterCollapsed`, поэтому при открытии рисовались ОДНОВРЕМЕННО:
	//   две полупрозрачные подложки друг на друге дают удвоенную плотность — ровно тот
	//   «тинт», который видно на скриншоте. Никакого множителя цвета никто не ставил.
	//   Ставим исходное состояние ЯВНО (развёрнуто), а не полагаемся на ассет.
	//   Цвета и шрифты при этом не трогаем — правится только видимость поддеревьев.
	bRosterCollapsed = false;
	if (RosterNormal) { RosterNormal->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (RosterHeader) { RosterHeader->SetVisibility(ESlateVisibility::Collapsed); }

	// Кнопки: подписи из таблицы (эталон `social.c:1925-1927`), обработчики — свои реле.
	//   ★ 22.10.2026: якорь — свой массив панели, обнуляется при каждой привязке (панель пересобирается после сноса окна).
	RosterRelays.Reset();
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), RosterRelays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnLeave,  2260, [this](int32) { OnLeaveClicked(); });
	Bind(BtnBanish, 2262, [this](int32) { OnBanishClicked(); });
	Bind(BtnMini,   2280, [this](int32) { ToggleRosterCollapsed(); });
	if (BtnLeaveText)  { BtnLeaveText->SetText(FText::FromString(Loc(PartyLoc::kBtnLeave))); }
	if (BtnBanishText) { BtnBanishText->SetText(FText::FromString(Loc(PartyLoc::kBtnBanish))); }

	int32 RowsOk = 0;
	for (const FAsdaPartyRowUi& Rw : Rows) { if (Rw.Root && Rw.Name && Rw.HpFill) { ++RowsOk; } }
	bRosterReady = (RowsOk == kRosterSlots);
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] окно 2200 разобрано: строк с полным набором %d из %d, «Выгнать»=%s «Выход»=%s "
		     "свернуть=%s, шапки N/H=%s/%s"),
		RowsOk, kRosterSlots, BtnBanish ? TEXT("да") : TEXT("НЕТ"), BtnLeave ? TEXT("да") : TEXT("НЕТ"),
		BtnMini ? TEXT("да") : TEXT("НЕТ"),
		RosterNormal ? TEXT("да") : TEXT("НЕТ"), RosterHeader ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaPartySubsystem::SetRosterVisible(bool bVisible)
{
	if (bVisible && !EnsureRosterWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRosterWinId, bVisible);
	}
	bRosterOpen = bVisible;
	if (bVisible) { bRosterDirty = true; }
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] панель 2200 %s"), bVisible ? TEXT("показана") : TEXT("скрыта"));
}

// ★★★ 22.10.2026 — разбор и эталон у объявления (AsdaPartySubsystem.h). Зовётся из `Tick` при `bRosterOpen`.
void UAsdaPartySubsystem::HealRosterIfLost()
{
	if (CVarRosterSurvivesWipe.GetValueOnGameThread() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	if (bRosterReady && WM->IsWindowLive(RosterWidget)) { return; }   // окно на месте — обычный кадр
	// Мир ещё не тот: экран загрузки/логин (окно умрёт следующим травелом) или загрузка не снята (`RevealWorld` по 4003
	//   снесёт все окна ещё раз) — ждём, состав и `bRosterOpen` при этом целы.
	const UWorld* W = GI->GetWorld();
	if (!W || AsdaWorldKind::IsLoadingMap(W) || AsdaWorldKind::IsLoginScene(W)) { return; }
	if (const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
	{
		if (LM->IsLoadingScreenVisible()) { return; }
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < NextRosterHealTry) { return; }
	// Свёрнутость — состояние окна, у оригинала оно живёт вместе с окном; сборка ставит развёрнутый вид — вернуть.
	const bool bWasCollapsed = bRosterCollapsed;
	SetRosterVisible(true);
	if (!bRosterReady || !WM->IsWindowLive(RosterWidget))
	{
		NextRosterHealTry = Now + 1.0;
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] панель 2200 снесена, а пересобрать окно не удалось — повтор через 1 с"));
		return;
	}
	if (bWasCollapsed) { ToggleRosterCollapsed(); }
	++CntRosterHeal;
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] ★ панель 2200 восстановлена после сноса окна (смена карты / воскрешение / закрытие всех окон) — №%d: "
		     "участников %d, свёрнута=%s [ИСПРАВЛЕНИЕ ПОРТА, у оригинала окно постоянное]"),
		CntRosterHeal, Members.Num(), bWasCollapsed ? TEXT("да") : TEXT("нет"));
}

void UAsdaPartySubsystem::ToggleRosterCollapsed()
{
	bRosterCollapsed = !bRosterCollapsed;
	// ★ Расплющенная иерархия: свёрнутый и развёрнутый вид — ДВА РАЗНЫХ поддерева
	//   (`Party_WinH_grp` и `Party_WinN_grp`), гасим/показываем их ЦЕЛИКОМ (закон
	//   `jui-uneven-node-layout`), иначе от «скрытого» вида остаются отдельные планы.
	if (RosterNormal) { RosterNormal->SetVisibility(bRosterCollapsed ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }
	if (RosterHeader) { RosterHeader->SetVisibility(bRosterCollapsed ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] панель 2200 %s"), bRosterCollapsed ? TEXT("свёрнута") : TEXT("развёрнута"));
}

// Порядок слотов 1:1 с эталоном (`social.c:2881` — ветка «я глава», `:2957` — ветка «я рядовой»).
void UAsdaPartySubsystem::BuildSlotOrder(TArray<int32>& Out) const
{
	Out.Reset();
	Out.Init(INDEX_NONE, kRosterSlots);
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 SelfCharId = Net ? Net->SelectedCharId : 0;

	int32 Slot = bIsLeader ? 0 : 1;          // эталон: рядовой начинает с 1, слот 0 — за главой
	for (int32 M = 0; M < Members.Num(); ++M)
	{
		const FAsdaPartyMember& Mem = Members[M];
		if (SelfCharId != 0 && Mem.CharId == SelfCharId) { continue; }   // себя эталон пропускает
		if (!bIsLeader && Mem.bLeader)
		{
			Out[0] = M;                       // глава закреплён за слотом 0 (и получает значок)
			continue;
		}
		if (Slot >= kRosterSlots) { break; }
		Out[Slot++] = M;
	}
}

void UAsdaPartySubsystem::RefreshRoster()
{
	if (!bRosterReady) { return; }
	TArray<int32> Order;
	BuildSlotOrder(Order);

	// `partyWndShowSelfHideMembers@0xB99D30` — эталон сперва гасит ВСЕ строки.
	auto HideAll = [](UWidget* W)
	{ if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } };
	for (FAsdaPartyRowUi& Row : Rows)
	{
		// ★ РАСПЛЮЩЕННАЯ ИЕРАРХИЯ: гасим не только корень строки, но и КАЖДЫЙ её собственный
		//   узел. Иначе у скрытой строки остаются «зажжёнными» значок главы и иконка класса
		//   из ассета, и стоит корню появиться — они всплывают вместе с ним (та же грабля,
		//   на которой уже обжигались с «пустой кнопкой» в меню игрока).
		HideAll(Row.Root);
		HideAll(Row.LeaderIcon); HideAll(Row.Select);
		HideAll(Row.Sword); HideAll(Row.Bow); HideAll(Row.Staff);
		Row.MemberIdx = INDEX_NONE;
	}

	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	auto SetVis = [](UWidget* W, bool bOn) { AsdaUi::SetShownDecor(W, bOn); };

	int32 Shown = 0;
	for (int32 S = 0; S < kRosterSlots; ++S)
	{
		const int32 M = Order.IsValidIndex(S) ? Order[S] : INDEX_NONE;
		if (M == INDEX_NONE || !Members.IsValidIndex(M)) { continue; }
		FAsdaPartyRowUi& Row = Rows[S];
		const FAsdaPartyMember& Mem = Members[M];
		Row.MemberIdx = M;
		Members[M].SlotIndex = S;             // эталонное `*(memberRec + 100) = slotIndex`

		SetVis(Row.Root, true);
		if (Row.Name)  { Row.Name->SetText(FText::FromString(Mem.Name)); }
		if (Row.Level) { Row.Level->SetText(FText::AsNumber(Mem.Level)); }

		// Эталон страхуется от деления на ноль: `if (max <= 0) max = 1`.
		const float MaxHp = (float)FMath::Max(1, Mem.MaxHp);
		const float MaxMp = (float)FMath::Max(1, Mem.MaxMp);
		Row.HpBar.SetTarget(FMath::Clamp((float)Mem.Hp / MaxHp, 0.f, 1.f));
		Row.MpBar.SetTarget(FMath::Clamp((float)Mem.Mp / MaxMp, 0.f, 1.f));
		// Рисуем сразу: строка должна быть правильной уже в первом кадре, а не ждать тика.
		ClipBar(Row.HpFill, Row.HpFullW, Row.HpBar.Shown);
		ClipBar(Row.MpFill, Row.MpFullW, Row.MpBar.Shown);
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] строка %d «%s»: HP %d/%d доля %.3f, дорожка %.0f -> заливка %.0f | MP %d/%d доля %.3f"),
			S, *Mem.Name, Mem.Hp, Mem.MaxHp, Row.HpBar.Shown, Row.HpFullW,
			Row.HpFullW * Row.HpBar.Shown, Mem.Mp, Mem.MaxMp, Row.MpBar.Shown);

		// Иконка класса: 1..4 меч, 0xC..0xF лук, 0x17..0x1A посох (`social.c:2917-2949`).
		const int32 C = Mem.ClassCode;
		SetVis(Row.Sword, (C >= 1  && C <= 4));
		SetVis(Row.Bow,   (C >= 12 && C <= 15));
		SetVis(Row.Staff, (C >= 23 && C <= 26));
		// ★ ЗНАЧОК ГЛАВЫ — только у слота 0 и только когда глава НЕ я (эталон `social.c:2964`).
		SetVis(Row.LeaderIcon, (S == 0) && Mem.bLeader && !bIsLeader);
		++Shown;
	}

	RefreshStateIcons();
	UE_LOG(LogAsdaParty, Verbose, TEXT("[отряд] панель 2200 перерисована: строк показано %d"), Shown);
}

// Порт `partyUpdateStateIcons@0xB97930`. ★ ИСТОЧНИК ИКОНОК — пакет 5108 (запись участника +104,
//   28 записей по 6 б: вид @0, id @2), а НЕ канал `buff0/1/2` собственных эффектов.
//   Вид 1 → таблица `stateinfotip` (иконка +36, ячейка +68), вид 3 → `soulstateinfotip`.
//   Обе таблицы уже читает `UAsdaEffectsSubsystem` — своих копий не заводим.
void UAsdaPartySubsystem::RefreshStateIcons()
{
	if (!bRosterReady) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	for (int32 S = 0; S < Rows.Num(); ++S)
	{
		FAsdaPartyRowUi& Row = Rows[S];
		for (const TObjectPtr<UImage>& Ic : Row.StateIcons)
		{
			if (Ic) { Ic->SetVisibility(ESlateVisibility::Collapsed); }
		}
		const bool bHasMember = Members.IsValidIndex(Row.MemberIdx);
		if (Row.StateRoot)
		{
			Row.StateRoot->SetVisibility(bHasMember ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
		if (!bHasMember || !Fx) { continue; }
		const FAsdaPartyMember& Mem = Members[Row.MemberIdx];
		int32 Used = 0;
		Row.StateTipId.Init(-1, kStateIcons);
		Row.StateTipKind.Init(0, kStateIcons);
		Row.StateTipRaw.Init(-1, kStateIcons);
		for (int32 I = 0; I < kStateIcons && Used < kStateIcons; ++I)
		{
			const uint8 Kind = Mem.StateKind.IsValidIndex(I) ? Mem.StateKind[I] : 0;
			const int32 Id   = Mem.StateId.IsValidIndex(I) ? Mem.StateId[I] : 0;
			if (Kind != 1 && Kind != 3) { continue; }
			FString Name, Atlas, Text; int32 Cell = 0;
			const bool bOk = (Kind == 1) ? Fx->GetStateInfo(Id, Name, Atlas, Cell, Text)
			                             : Fx->GetSoulStateInfo(Id, Name, Atlas, Cell, Text);
			if (!bOk) { continue; }           // эталон: нет записи в таблице — иконки нет
			UImage* SlotImg = Row.StateIcons.IsValidIndex(Used) ? Row.StateIcons[Used].Get() : nullptr;
			if (!SlotImg) { continue; }
			// ★ ОБЩАЯ точка отрисовки ячейки атласа (её же зовёт мини-статус).
			if (!AsdaUi::ApplyAtlasCellIcon(SlotImg, Atlas, Cell, 24, 10)) { continue; }
			SlotImg->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Row.StateTipId[Used] = Id;       // для подсказки при наведении (`TickRosterHover`)
			Row.StateTipKind[Used] = Kind;
			Row.StateTipRaw[Used] = I;
			++Used;
		}
	}
}

// Мышь по строкам панели: ЛКМ — выбрать целью, ПКМ — меню 8100.
//   ⚠ Зоны клика в ассете — `Party_MemClick{NN}` и её план `_HIT` (это `UImage`/`UTextBlock`,
//     кнопок там нет), поэтому попадание считаем по ЖИВОЙ ГЕОМЕТРИИ, как у выпадающего списка
//     (закон `jui-cell-chain-nesting`: ловить клик по геометрии, а не по слоту).
void UAsdaPartySubsystem::TickRosterMouse()
{
	if (!bRosterOpen || !bRosterReady || bRosterCollapsed) { return; }
	RosterMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлку кадра сбрасываем: иначе ПКМ, нажатая ДО нотиса, после его закрытия открыла бы
	//   меню 8100 по строке, которой игрок не выбирал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		RosterMouse.Reset();
		return;
	}
	if (!RosterMouse.bLmbDown && !RosterMouse.bRmbDown) { return; }
	for (int32 S = 0; S < Rows.Num(); ++S)
	{
		const FAsdaPartyRowUi& Row = Rows[S];
		if (Row.MemberIdx == INDEX_NONE || !Row.Root) { continue; }
		if (!AsdaUi::IsCursorOverWidget(Row.Root)) { continue; }
		if (RosterMouse.bRmbDown) { OpenRowMenu(S); }
		else                      { ClickRow(S); }
		return;
	}
}

// ★ 30.10.2026 [ОРИГИНАЛ] справка и подсказки панели 2200 (`asda.Party.HelpTips`). Эталон `partyWnd_onCommand` (social.c:2565-2688):
//   событие 8 — иконки состояний 33100+100·строка+i → `uiHelpEntryLayoutAndShow[Alt]` (вид записи 1/3, пока id ≥ 0), затем
//   2262/2260/2279/2209+5k/2210+5k → `uiShowHelpTooltipByControlId@0xB9B9C0` (темы 18/19/20/21/22), для 2279 — шестерёнка 2280
//   прячется, анимированная 2281 показывается; событие 9 — плашка за курсором; 0xA — гашение и шестерёнки обратно.
//   Гейта «предмет в руке» у отряда нет (в отличие от прочих окон). Опрос — по живой геометрии (кнопки — `IsHovered` Slate).
static ASDA_DEV_CVAR(int32, CVarPartyHelpTips,
	TEXT("asda.Party.HelpTips"),
	1,
	TEXT("1 = как в оригинале: при наведении на кнопки, значок главы, имена и иконки состояний участников панели отряда — "
	     "подсказки; 0 = прежний порт (подсказок нет)."),
	ECVF_Default);
// ★ [ИСПРАВЛЕНИЕ] Дефект оригинала в подсказке иконки состояния: id берётся из УПАКОВАННОГО ряда иконок (`aStateIconId[i]`),
//   а вид записи (1 — stateinfotip, 3 — soulstateinfotip) — из СЫРОЙ записи пакета с тем же номером i (social.c:2575 против
//   :2136/:2150). При «дыре» в записях подсказка бралась не из той таблицы или не показывалась. То же исправлено у панели Друга.
static ASDA_DEV_CVAR(int32, CVarPartyStateTipFix,
	TEXT("asda.Party.StateTipFix"),
	1,
	TEXT("1 = вид записи подсказки иконки состояния — от той же записи, что и id; 0 = как оригинал (вид из сырой записи с тем же номером)."),
	ECVF_Default);

void UAsdaPartySubsystem::SetGearHover(bool bOn)
{
	if (AsdaCloseGear::IsCommonOn()) { bGearHover = bOn && RosterWidget != nullptr; return; }   // ★ 08.11.2026: ведёт общая точка UAsdaCloseGearSubsystem
	if (bGearHover == bOn || !RosterWidget) { bGearHover = bOn && RosterWidget != nullptr; return; }
	bGearHover = bOn;
	// `Party_Gear` (2280, видима) и `Party_AniGear` (2281, в JUI `Visible = "0"`) — группы импортёра целиком.
	UWidget* Gear = AsdaUi::FindNodeByPrefix(RosterWidget, TEXT("Party_Gear_"));
	UWidget* Ani  = AsdaUi::FindNodeByPrefix(RosterWidget, TEXT("Party_AniGear_"));
	if (Gear) { AsdaUi::SetShownDecor(Gear, !bOn); }
	if (Ani)
	{
		AsdaUi::SetShownDecor(Ani, bOn);
		if (bOn)
		{
			// `CControl_Show(m_pAniGear)` — анимация с первого кадра.
			if (UAsdaAnimatedPlane* Ap = Cast<UAsdaAnimatedPlane>(Ani)) { Ap->RestartAnim(); }
			if (const UPanelWidget* P = Cast<UPanelWidget>(Ani))
			{
				TArray<UWidget*> Kids;
				UWidgetTree::GetChildWidgets(const_cast<UPanelWidget*>(P), Kids);
				for (UWidget* K : Kids) { if (UAsdaAnimatedPlane* Ak = Cast<UAsdaAnimatedPlane>(K)) { Ak->RestartAnim(); } }
			}
		}
	}
}

void UAsdaPartySubsystem::TickRosterHover()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bLive = CVarPartyHelpTips.GetValueOnGameThread() != 0 && bRosterOpen && bRosterReady && RosterWidget && WM
		&& !AsdaUi::IsModalDispatchBlocked(GI) && !WM->IsPointerBlockedByTopmost(kRosterWinId);
	if (!bLive)
	{
		HelpTip.Hide(GI, EAsdaTipOwner::PartyHud);
		SetGearHover(false);
		return;
	}
	auto Hov = [](const UWidget* W) { return W && W->IsHovered() && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden; };
	int32 Key = -1, Topic = -1, Row = -1, Icon = -1;
	// Кнопки — у свёрнутой панели остаётся только `PARTY_MINIBUT_H` (своей справки у эталона нет).
	if (!bRosterCollapsed)
	{
		if (Hov(BtnBanish))         { Key = 2262; Topic = 18; }
		else if (Hov(BtnLeave))     { Key = 2260; Topic = 19; }
		else if (Hov(BtnMiniCheck)) { Key = 2279; Topic = 20; }
		for (int32 S = 0; Key < 0 && S < Rows.Num(); ++S)
		{
			const FAsdaPartyRowUi& R = Rows[S];
			if (!Members.IsValidIndex(R.MemberIdx)) { continue; }
			// Иконки состояний — строки 0…4 (`PARTY_STATE_ICON{N}{ii}`), id 33100+100·N+i.
			for (int32 I = 0; I < R.StateIcons.Num() && I < R.StateTipId.Num(); ++I)
			{
				if (R.StateTipId[I] >= 0 && AsdaUi::IsCursorOverWidget(R.StateIcons[I])) { Key = 33100 + 100 * S + I; Row = S; Icon = I; break; }
			}
			if (Key >= 0) { break; }
			if (AsdaUi::IsCursorOverWidget(R.LeaderIcon))    { Key = 2209 + 5 * S; Topic = 21; }
			else if (AsdaUi::IsCursorOverWidget(R.Name))     { Key = 2210 + 5 * S; Topic = 22; }
		}
	}
	SetGearHover(Key == 2279);
	if (Icon < 0)
	{
		HelpTip.UpdateTopic(GI, EAsdaTipOwner::PartyHud, Key, Topic);
		return;
	}
	// Иконка состояния: заголовок и тело — записи stateinfotip / soulstateinfotip (`uiHelpEntryLayoutAndShow[Alt]`).
	HelpTip.Update(GI, EAsdaTipOwner::PartyHud, Key, [this, GI, Row, Icon]() -> bool
	{
		const FAsdaPartyRowUi& R = Rows[Row];
		const int32 Id = R.StateTipId[Icon];
		uint8 Kind = R.StateTipKind[Icon];
		if (CVarPartyStateTipFix.GetValueOnGameThread() == 0 && Members.IsValidIndex(R.MemberIdx))
		{
			const FAsdaPartyMember& Mem = Members[R.MemberIdx];
			Kind = Mem.StateKind.IsValidIndex(Icon) ? Mem.StateKind[Icon] : 0;   // эталон: вид из сырой записи с тем же номером
		}
		UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
		FString Title, Atlas, Body; int32 Cell = 0;
		bool bOk = false;
		if (Fx && Kind == 1)      { bOk = Fx->GetStateInfo(Id, Title, Atlas, Cell, Body); }
		else if (Fx && Kind == 3) { bOk = Fx->GetSoulStateInfo(Id, Title, Atlas, Cell, Body); }
		if (!bOk || !Mini) { return false; }
		Mini->ShowSharedTip(Title, Body, EAsdaTipOwner::PartyHud);
		return true;
	}, /*bDragGate*/ false);
}

void UAsdaPartySubsystem::SelectMemberSlot(int32 Slot)
{
	SelectedSlot = (Slot >= 0 && Slot < kRosterSlots) ? Slot : -1;
	// Сами подсветки строк считает `ApplyTargetShareHighlight` (она же гасит их при смене цели).
	ApplyTargetShareHighlight();
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] выбран слот панели %d"), SelectedSlot);
}

// Кнопка 2260 «Выход» (`social.c:2360-2368`): `Party_Send_InviteByName(myPartyIdx, 0, myName)`.
void UAsdaPartySubsystem::OnLeaveClicked()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!bInParty || !Net)
	{
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] 2260 «Выход»: я не в отряде — пакет не отправлен"));
		return;
	}
	// ★ [ОРИГИНАЛ] Волна 1-Б: у ГЛАВЫ выход идёт через окно роспуска 2256 (шапка .h §9) — оживлённая
	//   заготовка оригинала; пакет 5098 (режим 1 или 0) уйдёт по кнопке окна. Рядовой — сразу, 1:1.
	if (CVarFixDisbandConfirm.GetValueOnGameThread() != 0 && IsPartyLeader())
	{
		const bool bShown = ShowDisbandConfirm();
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 2260 «Выход» у главы → окно роспуска 2256 %s"),
			bShown ? TEXT("показано") : TEXT("НЕ показано (см. лог выше)"));
		if (bShown) { return; }
		// Окно не поднялось (ассет не загрузился) — не оставляем главу без выхода: путь оригинала.
	}
	// Своё имя — общая точка мира (её же зовёт меню игрока для 4302).
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const FString MyName = WS ? WS->GetSelfCharName() : FString();
	const bool bOk = Net->SendPartyLeave5098(PartyIndex, 0, MyName);
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 2260 «Выход»: 5098 (partyIndex=%d, режим 0, «%s») -> %s"),
		PartyIndex, *MyName, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
}
