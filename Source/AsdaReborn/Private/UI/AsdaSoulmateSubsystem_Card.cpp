// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulmateSubsystem_Card.cpp — ★ R2 10.11.2026 (разрез AsdaSoulmateSubsystem.cpp, дословный перенос): карточка/всплывающая карточка, действия (сообщение, разрыв, поиск нового), флажки.
//   Общее для частей — в AsdaSoulmateSubsystemInternal.h; шапка и реверс — в AsdaSoulmateSubsystem.cpp.
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaNetSubsystem.h"
#include "Net/NetOpcodes.h"   // OP_SOULMATE_UPDATE_INFO / OP_SOULMATE_WND_APPLY_INFO_PACKET
#include "AsdaBytes.h"
#include "Misc/ScopeExit.h"   // ★ 26.10.2026: ON_SCOPE_EXIT — выход из защиты повторного входа панели 6500
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
// GetSelfCharName — общая точка «своё имя» (ветка 5203 код 2)   ★ R3: комментарий снятого включения "World/AsdaWorldSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSoulAppleSubsystem.h"   // ★ ревизия 1-Б С-1: яблочная часть inventoryHidePanelsAndRefresh
#include "UI/AsdaMinimapSubsystem.h"     // ★ 23.10.2026: панель занятости 79900 — под панелью Друга 6500
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaSoulRouletteSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "UI/AsdaConstellationSubsystem.h"   // SignName / MatchIndex / SelfSign — влечение знаков
#include "UI/AsdaPartySubsystem.h"           // 8265 «Вступить в отряд» — гейты живут ТАМ
#include "UI/AsdaUiHelpers.h"                // ParsePlaneName / ApplyTabState / FormatPrintfArgs
// ★ ОБЩАЯ ТОЧКА работы с узлами окна 6400 (`namespace AsdaCommunityUi`): `Find`/`FindLabel`/
//   `SetTxt`/`SetButtonCaption`/`SetVisByPrefix`/`ShownVisibilityFor`. Заводить здесь свои копии
//   поиска по `<idc>`/`<idc>_grp`/`<idc>_Text` нельзя — закон `port-shared-points`.
#include "UI/AsdaCommunityFriendSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"   // ★ 24.10.2026 (sm-offline): окно 6400 открыто и активна вкладка «Друг» — гейт автозапроса 2244
#include "Text/AsdaLang.h"               // ★ 24.10.2026 (sm-offline): формат «Последнего выхода» 6522 по языку
#include "Items/AsdaItemTableSubsystem.h"    // DT_smcharexp — таблица опыта Дружбы
#include "Items/AsdaItemDbRows.h"            // FAsdaSmcharexpRow
#include "World/AsdaCoord.h"                  // ★ волна 1-А: мир → тайл (`helper_scaleIfNonzero`/`ratioIfFieldSet`) для 9104
// ★ волна 1-А: гейт показа 8200 — элитный магазин 6000 (`helper_getField2075(g_pShopWnd)`)   ★ R3: комментарий снятого включения "UI/AsdaPremiumShopSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaCraftSubsystem.h"           // ★ волна 1-А: гейт показа 8200 — окна ремесла (`g_pItemMainWnd`/`g_pItemQtyMakeDlg`)
#include "Engine/DataTable.h"
#include "Containers/Ticker.h"   // ★ 23.10.2026: отложенная (на кадр) пересборка отметки Couple после сброса сессии
#include "Components/Button.h"
#include "Components/CheckBox.h"   // UCheckBox — галки блока «доступ к персонажу» 6543..6546
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableText.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"   // ★ 10.10.2026: снятие фокуса с поля 8302 (`cFindSoulmateWinHide`)
#include "Engine/GameInstance.h"                    // ★ 10.10.2026: таймер следующего кадра (фокус поля 8302)
#include "TimerManager.h"                           // FTimerManager::SetTimerForNextTick
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 6500/8200/8300/8400/9100
#include "AsdaSoulmateSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaSoulmateSubsystem*.cpp

static ASDA_DEV_CVAR(int32, CVarSoulmateCheck6546Gate,
	TEXT("asda.Soulmate.Check6546Gate"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = галку 6546 при Друге вне игры не переключать, в чат loc 8026 (событие 0xC).\n")
	TEXT("0 = прежний порт: галка переключается всегда."),
	ECVF_Default);

// ★ 26.10.2026 (smmessage): ключи записок Другу — объявлены в `AsdaSoulmateNote.cpp` (аксессоры, без имён ключей в рабочем коде).
bool AsdaSoulNoteSelfCharIdOn();   // `asda.Soulmate.NoteSelfCharId` — 6052 @0 = свой charId

// [ИСПРАВЛЕНИЕ] 5221 несёт ВСЕ 4 флага сразу (UPSoulMateOptionUp перезаписывает Share1..3 и Info): пока своя 5220 не пришла,
//   состояние сервера неизвестно, и клик затёр бы остальные флаги нулями — так и сбрасывался «share».
static ASDA_DEV_CVAR(int32, CVarSoulmateShareNeedsServer,
	TEXT("asda.Soulmate.ShareSendNeedsServer"), 1,
	TEXT("1 (по умолчанию) = галки 6543..6546 не шлют 5221, пока своя запись 5220 (состояние сервера) не пришла — клик отменяется.\n")
	TEXT("0 = прежний порт: 5221 уходит из того, что есть (до 5220 — нули)."),
	ECVF_Default);

// ★ 26.10.2026 (хотфикс зависания на портале): защита панели 6500 — ключ asda.Soulmate.PopGuard и помощники в AsdaSoulmatePopBar.cpp.
bool AsdaSoulPopEnter(const TCHAR* Where);              // вход в функцию панели; false — лимит повторного входа, вызов отклонить
void AsdaSoulPopLeave();                                 // выход (только после успешного входа)
bool AsdaSoulPopWorldReady(const UGameInstance* GI);     // мир годен для панели (не L_Loading, не сцена входа, игра идёт)

// Порт `CSoulmateWnd_RefreshInfo@0xE63410` — 1:1 по составу и форматам строк.
void UAsdaSoulmateSubsystem::RefreshCard()
{
	// ★★★ ГЕЙТ ЖИВОЙ ПРИВЯЗКИ. `RefreshCard` зовут СЕТЕВЫЕ обработчики (5110/5111/5203/5207), а
	//   они прилетают когда угодно — в том числе когда окно 6400 уже пересоздано и кэш контролов
	//   (`TxtStatus`, `TxtName`, `TabGauge01`…) смотрит в уничтоженное дерево. Рисовать туда
	//   нельзя; перерисует `OnTabActivated` сразу после переприязки.
	//   ⚠ Поп-карточка 6500 живёт СВОИМ окном HUD и от окна 6400 не зависит — её обновляем ВСЕГДА
	//   (вызов `RefreshPopCard` в конце функции), иначе полосы Друга на экране замрут.
	if (!IsBoundTo(CommWnd))
	{
		RefreshPopCard();
		return;
	}

	// 6548: эталон ВСЕГДА пишет сюда loc 8053 («В игре») — без всяких условий.
	if (TxtStatus) { TxtStatus->SetText(FText::FromString(Loc(8053))); }

	// 6520: имя партнёра — cp1251-строка записи с rec[63] до NUL.
	const FString PartnerName = RecName();
	if (TxtName) { TxtName->SetText(FText::FromString(PartnerName)); }

	// 6557: «%d <loc 12215> » — уровень Дружбы rec[50]; при нуле эталон ставит ПУСТУЮ строку.
	if (TxtFriendLv)
	{
		const uint8 FriendLv = Rec.FriendLevel();
		TxtFriendLv->SetText(FText::FromString(
			FriendLv ? FString::Printf(TEXT("%d %s "), (int32)FriendLv, *Loc(12215)) : FString()));
	}

	// 6521: «%d <loc 12215> » — уровень партнёра rec[29] (эталон печатает его безусловно).
	if (TxtLevel)
	{
		TxtLevel->SetText(FText::FromString(
			FString::Printf(TEXT("%d %s "), (int32)Rec.Level(), *Loc(12215))));
	}

	// 6556: имя класса по rec[41] (если задан), иначе по rec[40] — эталонный `sub_926C05`
	//   (тот же источник, что и `FAsdaCharRecord::ClassName` = loc 12556+id).
	if (TxtGrade)
	{
		TxtGrade->SetText(FText::FromString(FAsdaCharRecord::ClassName(Rec.Grade())));
	}

	// 6523: профессия. rec[38] ≥ 101 ⇒ поле ПУСТОЕ. Иначе ступень rec[39]:
	//   ==6 → «<loc 13927> <класс>»; иначе → «  <ступень+1> <loc 1187> <класс>».
	// ★ 23.10.2026 (sm-info) [ИСПРАВЛЕНИЕ порта]: 6523 — «Локация» Друга (rec[38] карта, rec[39] канал), не профессия.
	if (TxtJob && CVarSoulmateLocationText.GetValueOnGameThread() != 0)
	{
		TxtJob->SetText(FText::FromString(LocationText(Rec.MapId(), Rec.Channel())));
	}
	else if (TxtJob)
	{
		FString JobText;
		const uint8 ClassId = Rec.ClassId();
		if (ClassId < 101)
		{
			const FString ClassStr = FAsdaCharRecord::ClassName(ClassId);
			const uint8 Tier = Rec.ClassTier();
			JobText = (Tier == 6)
				? FString::Printf(TEXT("%s %s"), *Loc(13927), *ClassStr)
				: FString::Printf(TEXT("  %d %s %s"), (int32)Tier + 1, *Loc(1187), *ClassStr);
		}
		TxtJob->SetText(FText::FromString(JobText));
	}
	// ★ 24.10.2026 (sm-offline): Друг в игре — «Последний выход» не к месту; поле, заполненное порту вне игры, гасим.
	if (TxtLogout && CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() != 0) { TxtLogout->SetText(FText::GetEmpty()); }
	// ⚠ 6522 `SOUL_TXT04` эталон действительно не заполняет никогда (читателей у `this+6` нет).
	//   А 6558/6559 — заполняет: см. опровержение D9 в `BindTabControls`. Их пишет тот же проход,
	//   что и полосу 6504 поп-карточки, — `uiProgressBar_RefreshPercent`.
	RefreshPopCard();
}

// ★★★ ДОЛЯ ОПЫТА ДРУЖБЫ — порт `uiProgressBar_RefreshPercent@0xE62E20` (game.c:471294-471360).
//   Тело эталона, дословно:
//     lvl = HIBYTE(CCharacter::field_8A5) = уровень Дружбы rec[50] (закон K3 аудита);
//     A   = sub_92EB99(win, lvl)  → `array_GetQwordAt16@0xE63320`  = запись[16*lvl + 8] (QWORD);
//     B   = sub_9317F9(win, lvl)  → `array_GetDwordAt16@0xE63390`  = запись[16*lvl]     (DWORD);
//     r   = A − curExp;  r = B − r;  pct = (B > 0) ? r/B*100 : 0.
//   Таблица — `smcharexp.BIN` из `Data\character\Bin.bbz` (`CharExpTable_LoadFromPack@0xE5DFD0`),
//   в порту это `DT_smcharexp` (`FAsdaSmcharexpRow`: `ExpToNext` = +0 = B, `CumExp` = +8 = A).
//   curExp — QWORD rec[42..49] (`FriendExp()`), эталон передаёт его аргументом `a2`.
//   ★ Ветка «уровень сменился» (`win+493`, ставится в 5111 при rec[50] != body[37]) обнуляет
//   долю РОВНО НА ОДИН показ и снимает признак — воспроизводим 1:1 (R18).
float UAsdaSoulmateSubsystem::FriendExpPercent()
{
	if (bFriendLevelDirty)
	{
		bFriendLevelDirty = false;
		return 0.f;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* DT = Items ? Items->GetDataTable(FName(TEXT("smcharexp"))) : nullptr;
	if (!DT) { return 0.f; }
	// ★ 23.10.2026 (sm-info): уровень и опыт — свой блок (+0x8B3 / +0x8AB), как у эталона (их пишет и 5220).
	const int32 Lv = GetFriendLevel();
	const FAsdaSmcharexpRow* Row =
		DT->FindRow<FAsdaSmcharexpRow>(FName(*FString::FromInt(Lv)), TEXT("soulmate exp"), false);
	if (!Row) { return 0.f; }
	const int64 A = (int64)Row->CumExp;
	const int64 B = (int64)Row->ExpToNext;
	if (B <= 0) { return 0.f; }
	const int64 CurExp = (CVarSoulmateOfflineInfo.GetValueOnGameThread() != 0 && bBlkLevelKnown) ? FriendExpBlk : Rec.FriendExp();
	int64 R = A - CurExp;
	R = B - R;
	return (float)((double)R / (double)B * 100.0);
}

// ★★★ ПОП-КАРТОЧКА 6500 — порт `unitFrame_Refresh@0xE63810` (game.c:471528-471590) плюс те два
//   контрола, что пишет `CSoulmateWnd_RefreshInfo@0xE63410` (6501 и 6502).
//   Что эталон в поп-панель НЕ пишет — не пишем и мы: значков класса `SoulPop_Tender/Sword/Bow/
//   Staff` (у них нет `IDNum`, InitControls их не резолвит) и подписи `SoulPop_Lv_Icon` здесь нет.
void UAsdaSoulmateSubsystem::RefreshPopCard()
{
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (хотфикс зависания на портале): лимит повторного входа круга показ ↔ данные ↔ видимость.
	if (!AsdaSoulPopEnter(TEXT("RefreshPopCard"))) { return; }
	ON_SCOPE_EXIT { AsdaSoulPopLeave(); };
	// Доля опыта Дружбы считается ОДИН раз: эталон гонит её сразу в обе полосы (6559 и 6504)
	//   и в подпись 6558 внутри одного вызова `uiProgressBar_RefreshPercent`.
	const float Pct = FriendExpPercent();
	// ★ 6559 и 6558 принадлежат ВКЛАДКЕ окна 6400, а не поп-плашке: пишем в них только при живой
	//   привязке (окно пересоздаётся при смене карты, см. `IsBoundTo`).
	if (IsBoundTo(CommWnd))
	{
		if (TabGauge01.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(TabGauge01, Pct / 100.f); }
		if (TxtPercent && Pct >= 0.f)
		{
			// Эталон: `sprintf(Buffer, "%.1lf %%", pct)` — знак после запятой и пробел перед «%».
			TxtPercent->SetText(FText::FromString(FString::Printf(TEXT("%.1f %%"), Pct)));
		}
	}

	// Плашку поднимаем ТОЛЬКО когда пара есть: без пары эталон её и не показывает
	//   (`inventoryHidePanelsAndRefresh`), а держать лишний виджет HUD незачем.
	if (Card.bHasSoulmate) { EnsurePopWindow(); }
	// ★ 23.10.2026: показ — В КОНЦЕ, как хвост `unitFrame_Refresh` (сначала данные, потом `sub_92CBC8`).
	if (!PopWnd) { ApplyPopVisibility(); return; }

	// 6502 `SOULMATE_TXT00` ← имя партнёра (`CWidget_SetTextAll4(m_pPopTxt00, &gap_54D[63], 0)`).
	if (PopName) { PopName->SetText(FText::FromString(RecName())); }
	// 6501 `SOULMATE_POP_LV00` ← «%d» от rec[50]. ⚠ ДЕФЕКТ ОРИГИНАЛА D12: эталон пишет сюда БЕЗ
	//   проверки на NULL, в отличие от всех соседей (game.c:471484-471485). У нас проверка есть.
	if (PopLv00)
	{
		PopLv00->SetText(FText::FromString(FString::FromInt((int32)Rec.FriendLevel())));
	}
	// 6505 `SOULMATE_POP_LV01` ← «%d» от rec[29] (уровень партнёра).
	//   ⚠ ДЕФЕКТ ОРИГИНАЛА D13: перед этим эталон собирает строку «%d <loc 12215>» и тут же
	//   затирает её пустой строкой и затем «%d» (game.c:471545-471549) — первая `sprintf` мертва.
	//   Воспроизводим РЕЗУЛЬТАТ (только «%d»), мёртвую работу не повторяем.
	if (PopLv01)
	{
		PopLv01->SetText(FText::FromString(FString::FromInt((int32)Rec.Level())));
	}
	// 6504 `SOULMATE_GAUGE00` — та же доля опыта Дружбы, что и 6559.
	if (PopGauge00.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(PopGauge00, Pct / 100.f); }
	// 6508 `SOULMATE_GAUGE02` (`progressUpdateBar`): при max <= 0 и при cur > max эталон выходит,
	//   НЕ трогая полосу — прежнее значение остаётся на экране. Повторяем.
	{
		const int32 Max = Rec.Bar2Max(), Cur = Rec.Bar2Cur();
		if (Max > 0 && Cur <= Max)
		{
			const float F = (float)((double)Cur / (double)Max);
			if (F >= 0.f && PopGauge02.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(PopGauge02, F); }
		}
	}
	// 6509 `SOULMATE_GAUGE03` (`PartyMark_Helper1`): гейт «max<=0» эталон берёт у ДРУГОГО поля
	//   (`*(int*)(this+1370)` = rec[13..16], то же, что у 6508), а долю считает по rec[21..24].
	//   Это в оригинале так и есть — повторяем дословно.
	{
		const int32 GateMax = Rec.Bar2Max();
		const int32 Max = Rec.Bar3Max(), Cur = Rec.Bar3Cur();
		if (GateMax > 0 && Max != 0 && Cur <= Max)
		{
			const float F = (float)((double)Cur / (double)Max);
			if (F >= 0.f && PopGauge03.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(PopGauge03, F); }
		}
	}
	// 6512..6517 `SOULMATE_ATT00..05`: гасим ВСЕ шесть и зажигаем ОДНУ — с максимальным
	//   значением rec[51 + 2i]. При равенстве эталон оставляет ПОСЛЕДНИЙ совпавший индекс
	//   (присваивание `v6 = j` внутри цикла), а стартовое `v6 = 0` делает проверку `v6 >= 0`
	//   всегда истинной ⇒ ровно одна картинка горит ВСЕГДА, даже когда все атрибуты нулевые.
	if (PopAtt.Num() == 6)
	{
		int32 Best = 0, BestVal = 0;
		for (int32 i = 0; i < 6; ++i) { BestVal = FMath::Max(BestVal, Rec.Attr(i)); }
		for (int32 i = 0; i < 6; ++i)
		{
			if (Rec.Attr(i) == BestVal) { Best = i; }
			if (PopAtt[i]) { PopAtt[i]->SetVisibility(ESlateVisibility::Collapsed); }
		}
		if (PopAtt[Best]) { PopAtt[Best]->SetVisibility(ESlateVisibility::HitTestInvisible); }
	}
	// ★ 23.10.2026 [ИСПРАВЛЕНИЕ]: значок класса Друга (ключ `asda.Soulmate.PopClassIcon`).
	RefreshPopClassIcon();
	// Хвост `unitFrame_Refresh` (ui_core.c:72610-72619): показ панели.
	ApplyPopVisibility();
}

// Поднять окно 6500 и разобрать его состав (идемпотентно).
void UAsdaSoulmateSubsystem::EnsurePopWindow()
{
	UGameInstance* GI = GetGameInstance();
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (хотфикс зависания на портале): на L_Loading / сцене входа окно 6500 не поднимаем.
	if (!AsdaSoulPopWorldReady(GI)) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Дедуп №2, В1-B (U22, 11.10.2026): каркас «открыть при первом показе» — `FAsdaJuiWindowHandle::EnsureLazy`
	//   (кэш годен → ничего; открыть свёрнутым; привязка; закрыватель). Шаги и их порядок — прежние.
	// ★ Волна В4 ремесла (ревизия В3 С-2): эталонный `uiCloseAllWindows@0xD61960` (ui_core.c:54371) безусловно
	//   зовёт `inventoryHidePanelsAndRefresh(g_pSoulmateWnd)@0xE65510` — гасит поп-карточку 6500 вместе с игровыми
	//   окнами; вернуть её может восстановление снимка (слот 11, `sub_92CBC8` = `sub_E65450`). Это закрыватель каркаса.
	const EAsdaJuiEnsure Ens = FAsdaJuiWindowHandle(kPopWindowId, kPopWbp, kPopZ).EnsureLazy(*WM, PopWnd, bPopBound,
		ESlateVisibility::Collapsed,
		[]() {},
		[]() { UE_LOG(LogAsdaSoulmate, Error, TEXT("[соулмейт] WBP_SOULMATE_POP_WIN не загрузился")); },
		[this](UAsdaWindowSubsystem&)
		{
			auto Txt = [this](const TCHAR* N) -> UTextBlock*
			{
				return Cast<UTextBlock>(PopWnd->GetWidgetFromName(FName(N)));
			};
			// ⚠ Имена узлов — по правилу импортёра: Text-контрол носит имя `idc`, а ПОДПИСЬ не-Text
			//   контрола лежит в отдельном узле `{idc}_Text`. 6502 `SOULMATE_TXT00` — контрол со скином
			//   `Box_Black` (не Text), поэтому его текст живёт в `SOULMATE_TXT00_Text`.
			PopTitle = Txt(TEXT("SoulPop_Title"));            // 6503
			PopLv00  = Txt(TEXT("SOULMATE_POP_LV00"));        // 6501
			PopLv01  = Txt(TEXT("SOULMATE_POP_LV01"));        // 6505
			PopName  = Txt(TEXT("SOULMATE_TXT00_Text"));      // 6502
			BindBarFill(PopWnd, TEXT("SOULMATE_GAUGE00"), PopGauge00);   // 6504
			BindBarFill(PopWnd, TEXT("SOULMATE_GAUGE02"), PopGauge02);   // 6508
			BindBarFill(PopWnd, TEXT("SOULMATE_GAUGE03"), PopGauge03);   // 6509
			PopAtt.Reset();
			for (int32 i = 0; i < 6; ++i)
			{
				PopAtt.Add(PlaneOf(PopWnd, FString::Printf(TEXT("SOULMATE_ATT%02d"), i), 0));
			}
			// Заголовок эталон ставит ОДИН раз в InitControls: loc 8014 («Друг»).
			if (PopTitle) { PopTitle->SetText(FText::FromString(Loc(8014))); }
		},
		FSimpleDelegate::CreateWeakLambda(this, [this]() { HidePopCardForCloseAll(); }));
	if (Ens != EAsdaJuiEnsure::Created) { return; }
	bPopBound = true;
	UE_LOG(LogAsdaSoulmate, Log,
		TEXT("[соулмейт] поп-карточка 6500: заголовок=%d имя=%d др.ур=%d ур=%d полосы=%d/%d/%d атрибутов=%d"),
		PopTitle ? 1 : 0, PopName ? 1 : 0, PopLv00 ? 1 : 0, PopLv01 ? 1 : 0,
		PopGauge00.IsValid() ? 1 : 0, PopGauge02.IsValid() ? 1 : 0, PopGauge03.IsValid() ? 1 : 0,
		PopAtt.Num());
}

// ★★★ ПОКАЗ ПОП-КАРТОЧКИ. Порт `sub_E65450` (= `sub_92CBC8`, game.c:472565-472580):
//     `if (win[1853]) { if (m_pPopWin) { SetVisible(m_pPopWin, 1); SetVisible(m_pGhostWin, 0); } … }`
//   где `win+1853` — это галка 6546 `SOUL_CHECK03`, у нас `Card.Modes[3]` (закон K2 аудита).
//   Зовут его два места: ветка 6546 в `Soulmate_SendBuffEnable@0xE66BB0` — только при наличии пары
//   (`gap_8B4[6]`), и хвост `unitFrame_Refresh` — при `gap_8B4[6] && gap_878[47] != -1`.
//   Обратное гашение — `inventoryHidePanelsAndRefresh@0xE65510` (пары нет ⇒ прячем).
void UAsdaSoulmateSubsystem::ApplyPopVisibility()
{
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (хотфикс зависания на портале): лимит повторного входа (см. RefreshPopCard).
	if (!AsdaSoulPopEnter(TEXT("ApplyPopVisibility"))) { return; }
	ON_SCOPE_EXIT { AsdaSoulPopLeave(); };
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★★★ 23.10.2026 [ОРИГИНАЛ] «SM bar is not showing up». Хвост `unitFrame_Refresh@0xE63810` (ui_core.c:72610-72619):
	//     if (local->gap_8BE[4] && local->gap_8B4[6] && local->gap_878[47] != -1) sub_92CBC8(win);   // = sub_E65450
	//   `gap_8BE[4]` — серверный признак «показывать панель» из СВОЕЙ записи 5220 (@33, CUser+99028; при создании пары
	//   сервер ставит 1 — GameServer.c:58641/58672). Порт 5220 не разбирал — панель не появлялась никогда, пока игрок
	//   сам не щёлкнет галку 6546. Эталонный хвост ТОЛЬКО ПОКАЗЫВАЕТ: гасит панель одна `inventoryHidePanelsAndRefresh`.
	//   Ключ `asda.Soulmate.PopFrom5220 0` — прежнее поведение порта (гейт по галке, показ И гашение здесь).
	if (CVarSoulmatePopFrom5220.GetValueOnGameThread() != 0)
	{
		if (bPopSrvOpt && Card.bHasSoulmate && Card.SlotIndex != -1) { TryShowPop(TEXT("хвост unitFrame_Refresh")); }
		return;
	}
	if (!PopWnd) { return; }
	const bool bShow = (Card.Modes[3] != 0) && Card.bHasSoulmate && (Card.SlotIndex != -1);
	const bool bWas = WM->IsWindowVisible(kPopWindowId);
	WM->SetWindowVisible(kPopWindowId, bShow);
	// ★ 23.10.2026 (ревизия, п.3): при ключе 0 — ДОСЛОВНО прежний порт: `bPopWanted` здесь не трогаем (иначе при 0 работали бы
	//   возврат панели после портала и опрос мыши панели — поведение новой ветки).
	// ★ 23.10.2026 (ревизия, п.5): панель занятости 79900 лежит под панелью Друга — перекладывать при ЛЮБОЙ смене видимости,
	//   в обеих ветках (иначе после скрытия при ключе 0 она оставалась сдвинутой вниз).
	if (bWas != bShow)
	{
		if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>()) { Mm->RelayoutOccupationBox(); }
	}
}

bool UAsdaSoulmateSubsystem::IsPopCardShown() const
{
	// `sub_9344D6(g_pSoulmateWnd)` = `sub_B7A590`: байт видимости `*(this + 189) + 292` — контрол поп-карточки 6500.
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && PopWnd && WM->IsWindowVisible(kPopWindowId);
}

void UAsdaSoulmateSubsystem::HidePopCardForCloseAll()
{
	// `uiCloseAllWindows@0xD61960` безусловно зовёт `inventoryHidePanelsAndRefresh(g_pSoulmateWnd)` — эталонные
	//   вызывающие: `premiumGacha_BeginRoll`, `invShopSellConfirm`, `charBeginAutoMove`, `regionWindow_OpenAndPathMove`,
	//   `uiItemUpgradeWindowOpen`. ★ Ревизия 1-Б С-1: прежде здесь гасилась ТОЛЬКО карточка, яблочная часть терялась.
	HidePanelsAndRefresh(TEXT("закрыть всё (uiCloseAllWindows)"));
}

// ★★★ Ревизия 1-Б С-1: порт `inventoryHidePanelsAndRefresh@0xE65510` (game.c:472590-472608) ЦЕЛИКОМ:
//     if (m_pPopWin) SetVisible(m_pPopWin, 0);
//     if (!gap_8B4[6]) { uiPanelResetAndKillFocus(this); uiWindow_ResetView(this); }
//     if (m_pAppleBase) SetVisible(m_pAppleBase, 0);
//   Две последние строки — панель «Яблока Дружбы», её ведёт `UAsdaSoulAppleSubsystem`.
void UAsdaSoulmateSubsystem::HidePanelsAndRefresh(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	bPopWanted = false;   // ★ 23.10.2026: после смены карты панель не возвращать
	HidePopTip();
	if (WM && PopWnd && WM->IsWindowVisible(kPopWindowId))
	{
		WM->SetWindowVisible(kPopWindowId, false);
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] поп-карточка 6500 погашена (%s)"), Why);
		// ★ 23.10.2026: панель занятости 79900 стояла под панелью Друга — вернуть её к низу мини-карты.
		if (UAsdaMinimapSubsystem* Mm = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr) { Mm->RelayoutOccupationBox(); }
	}
	if (UAsdaSoulAppleSubsystem* Apple = GI ? GI->GetSubsystem<UAsdaSoulAppleSubsystem>() : nullptr)
	{
		Apple->OnHidePanelsAndRefresh(Card.bHasSoulmate, Why);
	}
}

void UAsdaSoulmateSubsystem::ShowPopCardFromSnapshot()
{
	// `sub_E65450@0xE65450` (восстановление слота 11): показ только при галке 6546 (`+1853` = `Modes[3]`) — та же
	//   формула, что в `ApplyPopVisibility`.
	// ★ 23.10.2026: ровно `sub_E65450` — гейт ОДНА галка 6546 (без хвоста `unitFrame_Refresh`).
	//   Окно у оригинала существует всегда (строится при входе) — поднимаем его и без галки, как прежде.
	if (CVarSoulmatePopFrom5220.GetValueOnGameThread() != 0) { EnsurePopWindow(); TryShowPop(TEXT("восстановление слота 11")); return; }
	EnsurePopWindow();
	ApplyPopVisibility();
}

// Имя партнёра из записи: cp1251 с rec[63] до NUL (эталон отдаёт указатель на gap_54D[63]).
FString UAsdaSoulmateSubsystem::RecName() const
{
	const int32 Ofs = Rec.NameOffset();
	int32 Len = 0;
	while (Ofs + Len < FAsdaSoulmateRecord::kSize && Rec.B[Ofs + Len] != 0) { ++Len; }
	return (Len > 0) ? FAsdaLoginData::DecodeCp1251(&Rec.B[Ofs], Len) : FString();
}

// ═══════════════════════════════ ДЕЙСТВИЯ КНОПОК ══════════════════════════════════════════════
// 6583 `Soul_MessageBtn`: гейт «есть соулмейт» (иначе loc 6035), затем показать панель ввода
//   заметки `Soul_MsgWin` (6605) и дать фокус полю `Soul_Msg_Input` (6602) — как в эталоне.
void UAsdaSoulmateSubsystem::ActionMessage()
{
	// ★ ИСПРАВЛЕНО ПО ЭТАЛОНУ (`SoulmateUI_HandleButtonCommand@0xE69F90`, game.c:474332-474345):
	//   гейт — `*(int*)&CCharacter::gap_878[47] <= 0` (СЛОТ соулмейта), а не «признак пары»;
	//   отказ печатается loc 6035 в ЧАТ цветом −11514, а не цветом Social.
	if (Card.SlotIndex <= 0) { ChatTip(Loc(6035)); return; }
	if (!CommWnd) { return; }
	// ★ ПОКАЗ — только через общую точку: раньше здесь ставилась видимость узлу `Soul_MsgWin`,
	//   которого в ассете НЕТ (импортёр называет группу `Soul_MsgWin_grp`), так что показ и
	//   гашение работали «через раз» — см. комментарий у `ShowNotePanel`.
	ShowNotePanel(true);
	if (UEditableText* E = Cast<UEditableText>(CommWnd->GetWidgetFromName(TEXT("Soul_Msg_Input"))))
	{
		E->SetText(FText::GetEmpty());
		// ★ 26.10.2026 (smmessage) [ОРИГИНАЛ] `CControl_SetFocus(+60, 1)`: фокус полю — по образцу окна 1580 (`FocusNoteInput`).
		//   Прежний `SetKeyboardFocus` перебивал фокус, который Slate отдаёт нажатой кнопке 6583, — набор шёл в мир.
		FocusNoteInput();
	}
	// ⚠ СНЯТО: подпись 6601 loc 12175 порт ставил САМ — эталон в этой ветке пишет ТОЛЬКО
	//   пустую строку в поле ввода (+60) и даёт ему фокус (game.c:474341-474344). Заполнять
	//   то, чего эталон не заполняет, запрещено (закон «не выдумывать»).
}

// 6603 `Soul_Msg_OkBtn` — отправить заметку. Гейт эталона: длина строго 1..64 (иначе не шлём).
void UAsdaSoulmateSubsystem::SendNote()
{
	if (!CommWnd) { return; }
	FString Text;
	if (UEditableText* E = Cast<UEditableText>(CommWnd->GetWidgetFromName(TEXT("Soul_Msg_Input"))))
	{
		Text = E->GetText().ToString();
	}
	// [ПОРТ] С4 08.10.2026: длина — в байтах провода (`WireTextByteLen`), как `strlen` эталона по cp1251-буферу.
	if (Text.IsEmpty() || FAsdaLoginData::WireTextByteLen(Text) > 64)
	{
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] заметка не отправлена: %d симв. / %d б провода вне 1..64"), Text.Len(), FAsdaLoginData::WireTextByteLen(Text));
		return;
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// ★★★ 26.10.2026 (smmessage) [ИСПРАВЛЕНИЕ ПОРТА]: @0 = СВОЙ charId (`*(g_pLocalSessionCtx+1052)`, network.c:29651).
			//   Порт слал charId Друга — сервер (`Soulmate_SendNoteReq@0x4992E0`: `CUser+372 == @0`) молча выбрасывал записку:
			//   журналы игроков 19.10 — два 6052 без единого ответа 6053, в БД `SoulMateNote` новых строк нет.
			const int32 Cid = AsdaSoulNoteSelfCharIdOn() ? Net->SelectedCharId : Rec.PartnerCharId();
			Net->SendSoulmateNote6052(Cid, Text);
		}
	}
	HideNotePanel();
}

// 6604 `Soul_Msg_ClBtn` — отмена ввода. Эталон (case 0x19CC) прячет 6605 и снимает фокус с 6602.
void UAsdaSoulmateSubsystem::HideNotePanel()
{
	ShowNotePanel(false);
}

// ★★★ T3 ЗАКРЫТ. 6584 `Soul_OldSoul_Btn` — ветка `case 0x19B8` эталона
//   (`SoulmateUI_HandleButtonCommand`, game.c:474350-474351) состоит РОВНО из одной строки:
//   `antiCheat_SendBitMaskCheck(soulmateWnd)` — то есть отправка опкода 2244 (конверт B,
//   payload 0, size 24). Никаких гейтов у этой ветки НЕТ: ни «есть пара», ни защёлки —
//   проверено дословно по switch. Ответ приходит обычным путём записи соулмейта.
void UAsdaSoulmateSubsystem::ActionOldSoul()
{
	// ★ 24.10.2026 (sm-offline): общая точка отправки 2244 — ответ на кнопку поднимет окно 6640 (`bGridShowPending`).
	SendOldSoulReq2244(/*bAuto*/false, TEXT("кнопка 6584"));
}

void UAsdaSoulmateSubsystem::ActionCutoff()
{
	// 6587 «Разорвать Дружбу»: гейт «есть соулмейт», иначе loc 6035. Оригинал показывает диалог
	//   подтверждения 8044; у нас окна выбора «да/нет» для этого нет — шлём запрос сразу, а нотис
	//   8044 печатаем в чат (подтверждающий диалог — в отложенном).
	// ★ ИСПРАВЛЕНО ПО ЭТАЛОНУ (case 0x19BB, game.c:474356-474360): гейт СОСТАВНОЙ —
	//   `слот <= 0 || sub_938F2C(g_pSoulmateWnd)`.
	// ★★★ T6 ЗАКРЫТ: отказ идёт НОТИСОМ-ОКНОМ типа 0 — `CItem_BuildTooltipText(…, 0, 24, txt, 1)`.
	//   Тип содержимого 0 у `CMsgBox_SetContent` (ui_core.c:55951) — это ОДНОКНОПОЧНОЕ окно
	//   `ONEBUT_MSGBOX` 1500, а тип 1 — двухкнопочное `TWOBUT_MSGBOX` 1520. В порту это
	//   `UAsdaWindowSubsystem::ShowGameMessage` (уже общая точка) и `ShowGameConfirm`.
	//   Раньше отказ уходил строкой чата — это было отступление, теперь его нет.
	// ★★★ ВТОРОЙ ГЕЙТ ДОКАЗАН (аудит §7 п.3 закрыт). `sub_938F2C` — это ТРАМПЛИН:
	//   `938F2C: jmp 0xD88930`, а `get_o1D9@0xD88930` = `return *((DWORD*)this + 473)`.
	//   Поле `this+473` (= +1892) — та самая защёлка «мой запрос разрыва в полёте», которую
	//   ставит `Soulmate_SendBondRequest@0xE66F70` и снимает конец 5205/5207 (R22).
	if (Card.SlotIndex <= 0 || bBreakReqInFlight)
	{
		ShowNotice0(Loc(6035), /*эталон (0, 24), social.c:8295*/24);
		return;
	}
	// Эталон поднимает диалог подтверждения (тип 1, id 8044) с именем партнёра в слоте `[ss1]`.
	//   У нас это игровой двухкнопочный msgbox — общая точка `ShowGameConfirm`.
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// ★ ИСПРАВЛЕНО: эталон кладёт в слот `[ss1]` имя В КВАДРАТНЫХ СКОБКАХ —
	//   `sprintf(buf, "[%s]", soulmateWnd + 1420)` (game.c:474368), 1420 = 1357 + 63 = rec[63].
	const FString Msg = LocSlotStr(8044, FString::Printf(TEXT("[%s]"), *RecName()));
	if (!WM)
	{
		// Диалога нет — шлём напрямую, чтобы действие игрока не потерялось.
		SendBreakRequest5204(TEXT("6587 без окна подтверждения"));
		return;
	}
	// [ОРИГИНАЛ] «Да» у (1, 8044) — `OnSendItemUpgrade@0xD74180` case 0x1F6C → LABEL_60: закрыть окно, затем
	//   `soulmateSendBondRequest()` = `Soulmate_SendBondRequest(g_pSoulmateWnd, 0)`. Слот берётся НА МОМЕНТ «Да».
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Msg, [this](bool bYes)
	{
		if (!bYes) { return; }
		SendBreakRequest5204(TEXT("6587 → (1, 8044) «Да»"));
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 8044) social.c:8312*/8044, 1);
}

// ★★★ ЕДИНАЯ ТОЧКА 5204 — порт `Soulmate_SendBondRequest@0xE66F70` (AsdaStory_game.c:473130-473160).
//   [ОРИГИНАЛ] Гейт `if (!*(this + 473))` стоит ВНУТРИ отправителя (прежний порт его в колбэках не проверял).
//   Payload 5: `[0] = 0` всегда — второй аргумент функции (0 у 8044/8036, 1 у id 29) в теле НЕ используется;
//   `[1..4] = gap_878[47]` (слот Друга) В МОМЕНТ отправки. После отправки `+473 = 1` (гейт) и `+474 = 1`
//   (читателей `+474` в срезе соулмейта нет). [ПРОТОКОЛ] Сервер `RequestUser_SoulMateDestory@0x4931D0` при
//   `[0] != 0` требует ещё `CUser+99020 == 1` — клиент этот режим не шлёт никогда, мы тоже.
void UAsdaSoulmateSubsystem::SendBreakRequest5204(const TCHAR* Why)
{
	if (bBreakReqInFlight)
	{
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5204 (%s) не отправлен: запрос разрыва уже в полёте (гейт эталона +473)"), Why);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// ★★★ R22: `+473` ставится БЕЗУСЛОВНО после `CNetwork_Send` (у эталона нет проверки успеха отправки), а снимается в
	//   конце обработчиков 5205 и 5207 (`m_nField473 = 0`). [ПОРТ] Ставим только при реальной отправке: отказ шлюза
	//   (одержимость/«душа», нет связи) иначе защёлкнул бы кнопку до перезахода — ответа на неотправленное не будет.
	const bool bOk = Net->SendSoulmateBreakReq5204(Card.SlotIndex);
	if (bOk) { bBreakReqInFlight = true; }
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5204 (%s) слот=%d → %s"), Why, Card.SlotIndex, bOk ? TEXT("ушёл") : TEXT("ОТКАЗ шлюза/связи"));
}

void UAsdaSoulmateSubsystem::ActionFindNew()
{
	// ★ ИСПРАВЛЕНО ПО ЭТАЛОНУ (case 0x19BC, game.c:474380-474382): 6588 делает РОВНО ОДНО —
	//   `netSendMaskedPacket_op5483(g_pFindSoulmateWin)`, то есть шлёт 5483.
	// ★ ВОЛНА 1-А (06.10.2026): прежнее отступление «окно открываем сами» СНЯТО — приём 5485 заведён, и окно 8200
	//   теперь поднимает сервер (`Send_SoulSearch@0x54DFD0` шлёт 5484… и в конце 5485), как в оригинале.
	SendSearch();
}

// [ОРИГИНАЛ] 30023 при `gap_878[47] >= 0`: `CChat_AddTokenizedLines(g_pChatWnd, 8505, −1, 10000, 0)` и
//   `CControl_SetTooltipText(g_pChatWnd, 8505, −1, 10000)` — журнал ЗАТЕМ плашка 221, цвет −1 = 0xFFFFFFFF (белый),
//   10 000 мс (AsdaStory_game.c:387984-387988). Разговор с NPC при этом остаётся открытым. Звука нет — канал Social.
void UAsdaSoulmateSubsystem::NoticeFriendAlreadyExists8505() const
{
	AsdaNotice::PlankAndChatLoc(GetGameInstance(), EAsdaMsgCategory::Social, 8505,
		FColor(0xFF, 0xFF, 0xFF, 0xFF), 10000.f, /*SrcId*/30023, /*bChatFirst*/true);
}

// [ОРИГИНАЛ] 30024 (AsdaStory_game.c:387996-387999 + LABEL_49): `слот <= 0 || sub_938F2C(g_pSoulmateWnd)` →
//   `CItem_BuildTooltipText(ui, 0, 24, loc 6035)`; иначе `soulmateShowNotice8036Tooltip@0xE67E40` =
//   `CItem_BuildTooltipText(ui, 1, 8036, loc 8036)`. «Да» — `OnSendItemUpgrade` case 0x1F64: закрыть окно → 5204.
void UAsdaSoulmateSubsystem::NpcForceBreak30024()
{
	if (Card.SlotIndex <= 0 || bBreakReqInFlight)
	{
		ShowNotice0(Loc(6035), /*эталон (0, 24)*/24);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(8036), [this](bool bYes)
	{
		if (!bYes) { return; }
		SendBreakRequest5204(TEXT("NPC 30024 → (1, 8036) «Да»"));
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 8036)*/8036, 1);
}

// [ОРИГИНАЛ] 30025 (AsdaStory_game.c:388000-388011): тот же гейт → (0, 24) loc 6035; иначе
//   `CItem_BuildTooltipText(ui, 1, 29, loc 8030)`. «Да» — `OnSendItemUpgrade` case 0x1D: закрыть окно →
//   `Soulmate_SendBondRequest(g_pSoulmateWnd, 1)` — тот же 5204 (аргумент 1 телом не используется).
//   ⚠ В данных диалогов (`NpcNsrButton.json`, 5 сборок) кнопки с кодом 30025 нет ни у одного NPC — код живой, входа нет.
void UAsdaSoulmateSubsystem::NpcBreak30025()
{
	if (Card.SlotIndex <= 0 || bBreakReqInFlight)
	{
		ShowNotice0(Loc(6035), /*эталон (0, 24)*/24);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(8030), [this](bool bYes)
	{
		if (!bYes) { return; }
		SendBreakRequest5204(TEXT("NPC 30025 → (1, 29) «Да»"));
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 29)*/29, 1);
}

// Клик по галке 6543/6544/6545 (share) или 6546 (окно сведений). `UCheckBox` УЖЕ переключил свой
//   кадр и передал СВОЁ новое состояние — берём его как истину (без внутреннего флипа: иначе бы
//   разошлись визуал галки и `Card.Modes`). Эталон на эти события шлёт 67073 (включили) / 67074
//   (выключили) → `Soulmate_SendBuffEnable`/`Disable`, оба кладут весь 4-байтовый блок в 5221.
void UAsdaSoulmateSubsystem::OnShareCheckChanged(int32 Which, bool bChecked)
{
	if (Which < 0 || Which > 3) { return; }
	// ★★★ 23.10.2026 (sm-info) «"share" flag is dropping». 5221 несёт ВСЕ 4 флага, сервер перезаписывает ими Share1..3 и Info
	//   (`UPSoulMateOptionUp`). Два гейта (клик отменяется, `UCheckBox` возвращается в прежнее состояние, 5221 не уходит):
	//   1) я одержим — флаги принадлежат ХОЗЯИНУ тела (сервер пишет по его Userindex) — ключ asda.Soulmate.ShareLockPossessed;
	//   2) своя 5220 ещё не пришла — состояние сервера неизвестно, клик затёр бы остальные флаги нулями — ключ
	//      asda.Soulmate.ShareSendNeedsServer (только при включённом разборе 5220, asda.Soulmate.PopFrom5220).
	const bool bLocked = IsShareLocked();
	const bool bUnknown = !bLocked && CVarSoulmateShareNeedsServer.GetValueOnGameThread() != 0
		&& CVarSoulmatePopFrom5220.GetValueOnGameThread() != 0 && SelfInfo5220.Num() < 101;
	if (bLocked || bUnknown)
	{
		if (UCheckBox* Cb = FindCheck(*FString::Printf(TEXT("SOUL_CHECK%02d"), Which))) { Cb->SetIsChecked(!bChecked); }
		++ShareSendRefused;
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] галка %d: 5221 НЕ отправлен — %s"), 6543 + (Which == 3 ? 3 : Which),
			bLocked ? TEXT("я одержим (флаги хозяина тела)") : TEXT("своя 5220 не пришла (состояние сервера неизвестно)"));
		return;
	}
	// ★★★ 23.10.2026 [ОРИГИНАЛ] событие 0xC по 6546 (`SoulmateUI_HandleEvent`, social.c:8086-8098): Друга нет в игре
	//   (`gap_8B4[6] == 0`) — галка НЕ переключается (`set_o261` → +609 = 1), в чат loc 8026 цветом −8847556, 5221 не уходит.
	//   `UCheckBox` уже переключился — возвращаем прежнее состояние (SetIsChecked события не шлёт).
	//   Ключ `asda.Soulmate.Check6546Gate 0` — прежнее поведение порта.
	if (Which == 3 && !Card.bHasSoulmate && CVarSoulmateCheck6546Gate.GetValueOnGameThread() != 0)
	{
		if (UCheckBox* Cb = FindCheck(TEXT("SOUL_CHECK03"))) { Cb->SetIsChecked(!bChecked); }
		ChatSocial(Loc(8026));   // «Друга нет в игре»
		++PopNoticeSeq; PopLastNoticeLoc = 8026;
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] галка 6546: Друга нет в игре — отказ (loc 8026), 5221 не ушёл"));
		return;
	}
	Card.Modes[Which] = bChecked ? 1 : 0;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// ★★★ ПОРЯДОК БАЙТОВ. Эталон шлёт четыре байта, лежащих в объекте подряд с
			//   `CSoulmateWnd+1853` (= gap_54D[496]): [1853] = SOUL_CHECK03 (6546),
			//   [1854] = 6543, [1855] = 6544, [1856] = 6545
			//   (`Soulmate_SendBuffEnable@0xE66BB0` / `Soulmate_SendBuffDisable@0xE66DA0`:
			//    `memcpy(&buf[16], (char*)self + 1853, 4)`).
			//   Modes[0..3] у нас — это 6543..6546, поэтому первым уходит Modes[3].
			Net->SendSoulmateModes5221(Card.Modes[3], Card.Modes[0], Card.Modes[1], Card.Modes[2]);
		}
	}
	// ★★★ Галка 6546 (Modes[3]) — это ещё и выключатель поп-карточки 6500. Эталон в ветке 6546
	//   `Soulmate_SendBuffEnable@0xE66BB0` сразу зовёт `sub_92CBC8` (показать плашку), а
	//   `Soulmate_SendBuffDisable@0xE66DA0` — обратную `inventoryHidePanelsAndRefresh`.
	//   ★ Ревизия 1-Б С-1: снятие галки — это `inventoryHidePanelsAndRefresh` ЦЕЛИКОМ (карточка + яблоко, game.c:473111),
	//     постановка — показ карточки (`sub_92CBC8`).
	if (Which == 3)
	{
		// ★ 23.10.2026: `Soulmate_SendBuffEnable` ветка 6546 — `if (gap_8B4[6]) sub_92CBC8(self)` (гейт хвоста тут не нужен).
		if (bChecked)
		{
			if (CVarSoulmatePopFrom5220.GetValueOnGameThread() != 0) { if (Card.bHasSoulmate) { TryShowPop(TEXT("галка 6546 поставлена")); } }
			else { ApplyPopVisibility(); }
		}
		else          { HidePanelsAndRefresh(TEXT("галка 6546 снята")); }
	}
}
