// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulmateSubsystem_Offline.cpp — ★ R2 10.11.2026 (разрез AsdaSoulmateSubsystem.cpp, дословный перенос): эхо себя/офлайн, сетка старых душ 2243/2244, запрос разрыва, сброс пары, нотисы.
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
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
// GetSelfCharName — общая точка «своё имя» (ветка 5203 код 2)   ★ R3: комментарий снятого включения "World/AsdaWorldSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSoulAppleSubsystem.h"   // ★ ревизия 1-Б С-1: яблочная часть inventoryHidePanelsAndRefresh
// ★ 23.10.2026: панель занятости 79900 — под панелью Друга 6500   ★ R3: комментарий снятого включения "UI/AsdaMinimapSubsystem.h" (файл им не пользовался) — сохранён
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
// DT_smcharexp — таблица опыта Дружбы   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
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

// [ПОРТ] окно прежних Друзей 6640 `OldSoulmate_Win` — ответ 2243 на кнопку 6584 (`uiRefreshSlotGrid3x3@0xE6C2D0`) в порт не был перенесён.
static ASDA_DEV_CVAR(int32, CVarSoulmateOldSoulGrid,
	TEXT("asda.Soulmate.OldSoulGrid"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = ответ 2243 на кнопку 6584 заполняет и показывает окно прежних Друзей 6640 (3 группы × 3 персонажа).\n")
	TEXT("0 = прежний порт: кнопка шлёт 2244, окно не показывается."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarSoulmateOfflineReqInterval,
	TEXT("asda.Soulmate.OfflineFriendReqIntervalSec"), 30.f,
	TEXT("Предел частоты автозапроса 2244 (сведения о Друге вне игры), секунды; по умолчанию 30 (решение координатора 24.10)."),
	ECVF_Default);

// Допуск на расхождение часов ПК игрока и сервера БД при сравнении «время выхода ≥ время 5226», секунды.
static constexpr double kOffLeaveClockTolSec = 120.0;

// ═══════════ ★ 26.10.2026 [ИСПРАВЛЕНИЕ сервера] «ЭХО СЕБЯ» (ключ asda.Soulmate.SelfEchoGuard; разбор — у ключа выше) ═══════════
// Свой charId для сверки: блок выбора персонажа 1017 (`SelectedCharId`) и, запасом, @0 своей 5220 (сервер берёт оба из одного
//   `sub_5324F0`). 0 — неизвестен (эхо не распознаём).
static int32 AsdaSoulOwnCharId(const UGameInstance* GI, const TArray<uint8>& SelfInfo5220)
{
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->SelectedCharId != 0) { return Net->SelectedCharId; }
	return (SelfInfo5220.Num() >= 4) ? AsdaBytes::RdI32(SelfInfo5220.GetData()) : 0;
}

bool UAsdaSoulmateSubsystem::IsSelfEcho5110(const TArray<uint8>& Body) const
{
	if (CVarSoulmateSelfEchoGuard.GetValueOnGameThread() == 0 || Body.Num() < 8) { return false; }
	// rec[4] — charId ОТПРАВИТЕЛЯ (`recv_1`: sClientRes +4 ← sChatRes +12 = `sub_5324F0(отправитель)`), т.е. Друга.
	const int32 Cid = AsdaBytes::RdI32(&Body[4]);
	if (Cid == 0) { return false; }
	const int32 Own = AsdaSoulOwnCharId(GetGameInstance(), SelfInfo5220);
	const int32 Own5220 = (SelfInfo5220.Num() >= 4) ? AsdaBytes::RdI32(SelfInfo5220.GetData()) : 0;
	return (Own != 0 && Cid == Own) || (Own5220 != 0 && Cid == Own5220);
}

bool UAsdaSoulmateSubsystem::IsPartnerFeedTrusted() const
{
	if (CVarSoulmateSelfEchoGuard.GetValueOnGameThread() == 0) { return true; }
	if (bSelfEchoSeen) { return false; }
	// Запись хранит настоящего Друга: 5110 приходила (charId ≠ 0) и это не я. 5111 — подмножество 5110 (HP/MP/тайл/уровень),
	//   5112 и 5227 без записи Друга рисовать не на чем — ранний пакет теряет лишь то, что принесёт ближайшая 5110.
	const int32 P = Rec.PartnerCharId();
	const int32 Own = AsdaSoulOwnCharId(GetGameInstance(), SelfInfo5220);
	return P != 0 && P != Own;
}

void UAsdaSoulmateSubsystem::EnterSelfEchoOffline(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	const bool bWasSeen = bSelfEchoSeen;
	bSelfEchoSeen = true;
	// Номер Друга у сервера указывает на меня — ни цели (ЛКМ по панели), ни кольца по нему. charId Друга (из 5220 @19
	//   до эха) — настоящий, его не трогаем; если в нём уже я — «неизвестен».
	MateUserIdxLive = -1;
	if (MateCharIdLive == AsdaSoulOwnCharId(GI, SelfInfo5220)) { MateCharIdLive = -1; }
	// Друг на деле вне игры — состояние, какое дал бы исправный сервер (5220 @25 = 0): `gap_8B4[6] = 0`.
	const bool bWasPair = Card.bHasSoulmate;
	Card.bHasSoulmate = false;
	bOffStale = true;
	// Вкладка: «Вне игры» + сведения о Друге вне игры (2243, P79) — как после 5226, но БЕЗ нотиса 110104 (Друг не уходил).
	if (CVarSoulmateOfflineInfo.GetValueOnGameThread() != 0) { RefreshSelfBlockControls(Why); }
	else if (bCardReceived) { RefreshCard(); }
	if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() != 0) { RequestOfflineFriendInfo(Why, 0.f); }
	PushCoupleMark(false);
	HidePanelsAndRefresh(Why);
	if (bWasPair)
	{
		// Навыки Дружбы, требующие Друга в игре, — как при 5226.
		if (UAsdaSoulmateSkillSubsystem* Sk = GI ? GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr) { Sk->ClearTrayLit(); }
		if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->RebuildSlots(); }
	}
	UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] ЭХО СЕБЯ (%s): сервер прислал мои данные как Друга — Друг вне игры, панель 6500 скрыта%s"),
		Why, bWasSeen ? TEXT(" (повтор)") : TEXT(""));
}

namespace {
// ═══════════ ★ 24.10.2026 (sm-offline, P79) СВЕДЕНИЯ О ДРУГЕ ВНЕ ИГРЫ + ОКНО ПРЕЖНИХ ДРУЗЕЙ 6640 ═══════════
//   Провод 2243 (S→C, 243 б): 9 строк × 27 б — @0 Smidx i32, @4 SmLvl u8, @5 Lvl u8, @6 Job u8, @7 Charname char[20]
//   (DBManager `sub_42EA20@0x42EA20` копирует 0xF3 б буфера строк `EXEC UPCharSoulMateInactive`, раскладка — по типам столбцов).
//   Сток: только прежние Друзья (State=0). P79-SQL: ПОСЛЕ них — строки активной пары SmLvl 254 (персонаж) и 253 (место и время).
	constexpr int32 kGridRows  = 9;     // строк в 2243
	constexpr int32 kGridRow   = 27;    // шаг строки
	constexpr uint8 kP79Char   = 254;   // строка P79: персонаж Друга (Lvl, Job = Subjob ?: Job, Charname)
	constexpr uint8 kP79Place  = 253;   // строка P79: Lvl = карта, Job = канал, Charname = время выхода UTC «yyyyMMddHHmmss»

	// Имя строки — cp1251 до NUL в пределах 20 б (как `memcpy`/`SetText` эталона).
	FString GridRowName(const uint8* R)
	{
		int32 Len = 0;
		while (Len < 20 && R[7 + Len] != 0) { ++Len; }
		return Len > 0 ? FAsdaLoginData::DecodeCp1251(R + 7, Len) : FString();
	}

	// Время UTC из строки P79 «yyyyMMddHHmmss» (14 цифр). false — пусто или не дата.
	bool ParseP79Utc(const FString& S, FDateTime& Out)
	{
		if (S.Len() != 14) { return false; }
		for (TCHAR Ch : S) { if (!FChar::IsDigit(Ch)) { return false; } }
		const int32 Y = FCString::Atoi(*S.Mid(0, 4)), Mo = FCString::Atoi(*S.Mid(4, 2)), D = FCString::Atoi(*S.Mid(6, 2));
		const int32 H = FCString::Atoi(*S.Mid(8, 2)), Mi = FCString::Atoi(*S.Mid(10, 2)), Se = FCString::Atoi(*S.Mid(12, 2));
		if (!FDateTime::Validate(Y, Mo, D, H, Mi, Se, 0)) { return false; }
		Out = FDateTime(Y, Mo, D, H, Mi, Se);
		return true;
	}
}

// Приём 2243 — порт `uiRefreshSlotGrid3x3@0xE6C2D0` (сетка 6640) + разбор строк P79 активной пары.
void UAsdaSoulmateSubsystem::OnOldSoulGrid2243(const TArray<uint8>& Body)
{
	// ★ 24.10.2026 (ревизия, (а)): ожидание кнопки 6584 со сроком годности — запоздалый (или чужой, на автозапрос) 2243 окно 6640 не поднимает.
	const float GridTtl = CVarSoulmateOldSoulGridReplyTtl.GetValueOnGameThread();
	const bool bGridExpired = bGridShowPending && GridTtl > 0.f && (FPlatformTime::Seconds() - GridShowPendingSec) > (double)GridTtl;
	if (bGridExpired)
	{
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←2243: ожидание ответа на кнопку 6584 истекло (%.1f с > %.0f с) — окно 6640 не поднимаем"),
			FPlatformTime::Seconds() - GridShowPendingSec, GridTtl);
	}
	const bool bForGrid = bGridShowPending && !bGridExpired;
	const bool bAuto    = bAutoReqPending;
	bGridShowPending = false;
	bAutoReqPending  = false;
	if (Body.Num() < kGridRows * kGridRow)
	{
		UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] ←2243 короткое тело %d б (нужно %d) — отброшено"), Body.Num(), kGridRows * kGridRow);
		return;
	}
	// ── Строки P79 своей пары (Smidx = свой слот). Чужой Smidx с SmLvl 253/254 сервер не шлёт — такие строки не берём. ──
	if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() != 0)
	{
		FAsdaOfflineFriend N;
		N.bReplied = true;
		N.Smidx = Card.SlotIndex;
		for (int32 i = 0; i < kGridRows; ++i)
		{
			const uint8* R = Body.GetData() + i * kGridRow;
			const int32 Smidx = AsdaBytes::RdI32(R);
			if (Card.SlotIndex <= 0 || Smidx != Card.SlotIndex) { continue; }
			if (R[4] == kP79Char)
			{
				N.bValid = true;
				N.Level  = R[5];
				N.Grade  = R[6];
				N.Name   = GridRowName(R);
			}
			else if (R[4] == kP79Place)
			{
				N.Map     = R[5];
				N.Channel = R[6];
				N.bHasTime = ParseP79Utc(GridRowName(R), N.LogoutUtc);
			}
		}
		// ★ 24.10.2026 (ревизия, (б)): ответ после 5226 — свежий ли он. Прежний выход (время раньше 5226 с допуском на часы, то же время,
		//   что в прошлом ответе, или без времени — тот же персонаж/место) = БД ещё не записала выход: сведения остаются устаревшими
		//   (поля пусты) и уходит ОДИН повторный 2244 не раньше предела частоты. Повтор тоже прежний — принимаем (без бесконечного опроса).
		//   Ответ без строк P79 (старый сервер) повтора не стоит — сверять нечего.
		bool bLeaveRetry = false;
		if (bOffLeaveCheck)
		{
			const FAsdaOfflineFriend& Prev = OffFriend;
			const bool bPrevSame = Prev.bReplied && Prev.Smidx == N.Smidx;
			const bool bTimeOld  = N.bHasTime && (N.LogoutUtc + FTimespan::FromSeconds(kOffLeaveClockTolSec) < OffLeaveUtc);
			const bool bTimeSame = N.bHasTime && bPrevSame && Prev.bHasTime && Prev.LogoutUtc == N.LogoutUtc;
			const bool bNoTimeSame = !N.bHasTime && N.bValid && bPrevSame && Prev.bValid && !Prev.bHasTime && Prev.Name == N.Name
				&& Prev.Level == N.Level && Prev.Grade == N.Grade && Prev.Map == N.Map && Prev.Channel == N.Channel;
			const bool bOld = (N.bValid || N.bHasTime) && (bTimeOld || bTimeSame || bNoTimeSame);
			if (bOld && !bOffLeaveRetryUsed && CVarSoulmateOfflineLeaveRetry.GetValueOnGameThread() != 0)
			{
				bLeaveRetry = true;
				bOffLeaveRetryUsed = true;
			}
			else
			{
				bOffLeaveCheck = false;
			}
			UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←2243 после 5226: выход %s (раньше 5226: %d, как в прошлом ответе: %d) — %s"),
				N.bHasTime ? *N.LogoutUtc.ToString(TEXT("%Y-%m-%d %H:%M:%S UTC")) : TEXT("—"), bTimeOld ? 1 : 0, (bTimeSame || bNoTimeSame) ? 1 : 0,
				bLeaveRetry ? TEXT("прежний, БД ещё не записала выход — один повторный запрос") : bOld ? TEXT("повтор тоже прежний — принят") : TEXT("свежий — принят"));
		}
		OffFriend = N;
		bOffStale = bLeaveRetry;
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←2243 (%s): сведения о Друге вне игры %s — «%s» ур.%u проф.%u карта %d канал %d выход %s"),
			bForGrid ? TEXT("кнопка 6584") : bAuto ? TEXT("автозапрос") : TEXT("без запроса"),
			N.bValid ? TEXT("есть") : TEXT("нет (старый сервер / нет персонажей)"), *N.Name, (uint32)N.Level, (uint32)N.Grade,
			N.Map, N.Channel, N.bHasTime ? *N.LogoutUtc.ToString(TEXT("%Y-%m-%d %H:%M:%S UTC")) : TEXT("—"));
		RefreshSelfBlockControls(TEXT("2243"));
		if (bLeaveRetry)
		{
			// Не раньше OfflineFriendAfterLeaveSec и предела частоты (запрос только что ушёл — значит, через ~предел); вкладка закрыта —
			//   отложенного нет, спросим при следующем показе (сведения устарели).
			RequestOfflineFriendInfo(TEXT("повтор после 5226"), FMath::Max(0.f, CVarSoulmateOfflineAfterLeave.GetValueOnGameThread()));
		}
	}
	// ── Окно прежних Друзей 6640 — только ответ на кнопку (у оригинала 2243 приходит только на неё). ──
	if (bForGrid && CVarSoulmateOldSoulGrid.GetValueOnGameThread() != 0)
	{
		RenderOldSoulGrid(Body);
	}
}

// Статическая часть под-окна 6640 (`CSoulmateWnd_InitControls`, social.c:6447-6577).
void UAsdaSoulmateSubsystem::ApplyOldSoulGridStatic()
{
	if (!IsBoundTo(CommWnd)) { return; }
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("OldSoulmate_Title"), Loc(12171));                     // 6641 «Информация о предыдущем Друге»
	AsdaUi::ApplyPseudoBold(AsdaCommunityUi::FindLabel(CommWnd, TEXT("OldSoulmate_Title")));     // `SetField926(…, 0x200000)`
	for (int32 G = 1; G <= 3; ++G)
	{
		const FString Title = FString::Printf(TEXT("OldSoulmate_Name%d"), G);                   // 6650/6663/6676 — заголовок группы
		AsdaUi::ApplyPseudoBold(AsdaCommunityUi::FindLabel(CommWnd, Title));
		AsdaCommunityUi::SetTxt(CommWnd, FString::Printf(TEXT("OldSoulmate_N%dText02"), G), Loc(12014));   // «Имя персонажа»
		AsdaCommunityUi::SetTxt(CommWnd, FString::Printf(TEXT("OldSoulmate_N%dText03"), G), Loc(11029));   // «Уровень»
		AsdaCommunityUi::SetTxt(CommWnd, FString::Printf(TEXT("OldSoulmate_N%dText04"), G), Loc(11030));   // «Профессия»
		// Второй цикл эталона (social.c:6540-6577): 7500+10m.. (имена/уровни/профессии 4..6) и время 6643..6645 — пусты и скрыты.
		for (int32 K = 4; K <= 6; ++K)
		{
			SetBlockNodeVisible(*FString::Printf(TEXT("OldSoulmate_N%dName%d"), G, K), false);
			SetBlockNodeVisible(*FString::Printf(TEXT("OldSoulmate_N%dLv%d"),   G, K), false);
			SetBlockNodeVisible(*FString::Printf(TEXT("OldSoulmate_N%dJob%d"),  G, K), false);
		}
		const FString Time = FString::Printf(TEXT("OldSoulmate_N%dTime"), G);
		AsdaCommunityUi::SetTxt(CommWnd, Time, FString());
		SetBlockNodeVisible(*Time, false);
	}
}

// Порт `uiRefreshSlotGrid3x3@0xE6C2D0` 1:1 (ui_core.c:73334-73411). [ПОРТ] строки P79 своей пары перед раскладкой обнуляются —
//   сток-сетка их и так не рисует (`(char)SmLvl > 0` ложно), но они заняли бы место группы.
void UAsdaSoulmateSubsystem::RenderOldSoulGrid(const TArray<uint8>& Body)
{
	if (!IsBoundTo(CommWnd) || Body.Num() < kGridRows * kGridRow) { return; }
	ApplyOldSoulGridStatic();
	TArray<uint8> V;
	V.Append(Body.GetData(), kGridRows * kGridRow);
	for (int32 i = 0; i < kGridRows; ++i)
	{
		uint8* R = V.GetData() + i * kGridRow;
		if (Card.SlotIndex > 0 && AsdaBytes::RdI32(R) == Card.SlotIndex && (R[4] == kP79Char || R[4] == kP79Place))
		{
			FMemory::Memzero(R, kGridRow);
		}
	}
	auto Cell = [](int32 G, int32 Row, int32 Col) -> FString   // G 1..3, Row 0..2, Col 0 имя / 1 уровень / 2 профессия
	{
		const int32 N = 5 + 3 * Row + Col;
		// У группы 1 первый контрол назван `OldSoulmate_N1Txt05` (опечатка разметки), у групп 2/3 — `…Text05`.
		return (G == 1 && N == 5) ? FString(TEXT("OldSoulmate_N1Txt05")) : FString::Printf(TEXT("OldSoulmate_N%dText%02d"), G, N);
	};
	// Сброс: заголовки «Душа: 0» (`SetSlotPair(ctx, 0, 0, 0)` + loc 12182), ячейки пусты.
	for (int32 G = 1; G <= 3; ++G)
	{
		AsdaCommunityUi::SetTxt(CommWnd, FString::Printf(TEXT("OldSoulmate_Name%d"), G), LocSlotNum(12182, 0));
		for (int32 Row = 0; Row < 3; ++Row) { for (int32 Col = 0; Col < 3; ++Col) { AsdaCommunityUi::SetTxt(CommWnd, Cell(G, Row, Col), FString()); } }
	}
	const auto Smidx = [&V](int32 M) { return AsdaBytes::RdI32(V.GetData() + M * kGridRow); };
	int32 Cur = Smidx(0);
	int32 Shown = 0;
	if (Cur > 0)
	{
		int32 M = 0;
		for (int32 i = 0; i < 3 && M < kGridRows; ++i)
		{
			for (int32 Row = 0; Row < 3; ++Row)
			{
				if (M >= kGridRows) { break; }   // [ИСПРАВЛЕНИЕ] эталон после 9-й строки читал бы за массивом (`break` только из внутреннего цикла)
				const uint8* R = V.GetData() + M * kGridRow;
				if (Cur == Smidx(M))
				{
					if ((int8)R[4] > 0)
					{
						const int32 G = i + 1;
						AsdaCommunityUi::SetTxt(CommWnd, FString::Printf(TEXT("OldSoulmate_Name%d"), G), LocSlotNum(12182, (int8)R[4]));
						AsdaCommunityUi::SetTxt(CommWnd, Cell(G, Row, 0), GridRowName(R));
						AsdaCommunityUi::SetTxt(CommWnd, Cell(G, Row, 1), FString::Printf(TEXT("%d"), (int32)(int8)R[5]));
						AsdaCommunityUi::SetTxt(CommWnd, Cell(G, Row, 2), Loc((int32)(int8)R[6] + 12300));   // `sub_ECB9B0`: loc 12300 + профессия
						++Shown;
					}
					++M;
				}
				else
				{
					Cur = Smidx(M);
					if (Row == 0) { --i; }
					Row = 3;
				}
			}
		}
	}
	if (UWidget* W = AsdaUi::FindNodeExact(CommWnd, TEXT("OldSoulmate_Win_grp")))
	{
		W->SetVisibility(AsdaUi::ShownVisibilityFor(W));   // `CControl_SetVisible(this+78, 1)`
	}
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] окно прежних Друзей 6640 показано: персонажей в сетке %d"), Shown);
}

bool UAsdaSoulmateSubsystem::IsOldSoulGridShown() const
{
	const UWidget* W = CommWnd ? AsdaUi::FindNodeExact(CommWnd, TEXT("OldSoulmate_Win_grp")) : nullptr;
	return W && W->IsVisible();
}

void UAsdaSoulmateSubsystem::CloseOldSoulGrid()
{
	if (UWidget* W = CommWnd ? AsdaUi::FindNodeExact(CommWnd, TEXT("OldSoulmate_Win_grp")) : nullptr)
	{
		W->SetVisibility(ESlateVisibility::Collapsed);   // вид импортёра для скрытого узла .JUI
	}
}

// Гейт автозапроса: ключ, пара есть, Друг вне игры, я не одержим, вкладка привязана (bRequireOpen — и окно 6400 открыто на «Друге»).
bool UAsdaSoulmateSubsystem::CanAutoRequestOfflineFriend(bool bRequireOpen) const
{
	if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() == 0) { return false; }
	if (Card.SlotIndex <= 0 || Card.bHasSoulmate || SelfConnectType == 2) { return false; }
	if (!IsBoundTo(CommWnd)) { return false; }
	if (!bRequireOpen) { return true; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaCommunitySubsystem* Cm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr;
	return Cm && Cm->IsWindowOpen() && Cm->GetActiveTab() == 0;
}

// Автозапрос 2244: сейчас (если предел частоты позволяет и MinDelaySec = 0) или ОДИН отложенный — не опрос по таймеру.
void UAsdaSoulmateSubsystem::RequestOfflineFriendInfo(const TCHAR* Why, float MinDelaySec, bool bTabShowing)
{
	if (!CanAutoRequestOfflineFriend(/*bRequireOpen*/!bTabShowing)) { return; }
	const double Interval = FMath::Max(0.0, (double)CVarSoulmateOfflineReqInterval.GetValueOnGameThread());
	const double Since    = FPlatformTime::Seconds() - LastOldSoulReqSec;
	// Ответ на прошлый автозапрос ещё в пути (окно открылось — вкладку показали дважды за кадр) — второй 2244 не шлём и не откладываем.
	//   Ждём не дольше предела частоты и не меньше 5 с (потерянный ответ не должен навсегда закрыть автозапрос).
	if (bAutoReqPending && Since < FMath::Max(Interval, 5.0)) { return; }
	const double Wait     = FMath::Max((double)MinDelaySec, Interval - Since);
	if (Wait <= 0.0)
	{
		CancelOfflineReqTimer();
		SendOldSoulReq2244(/*bAuto*/true, Why);
		return;
	}
	CancelOfflineReqTimer();
	const FString WhyStr(Why);
	TWeakObjectPtr<UAsdaSoulmateSubsystem> Weak(this);
	OffReqTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, WhyStr](float)
	{
		if (UAsdaSoulmateSubsystem* Self = Weak.Get())
		{
			Self->OffReqTicker.Reset();
			// Условия могли смениться (Друг вошёл, вкладку закрыли, пара разорвана) — проверяем заново.
			if (Self->CanAutoRequestOfflineFriend(/*bRequireOpen*/true) && (Self->bOffStale || !Self->OffFriend.bReplied))
			{
				Self->SendOldSoulReq2244(/*bAuto*/true, *WhyStr);
			}
		}
		return false;   // одноразовый
	}), (float)Wait);
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] автозапрос 2244 (%s) отложен на %.1f с (предел частоты %.0f с)"), Why, Wait, Interval);
}

// Единая точка 2244: кнопка 6584 (ответ поднимет 6640) и автозапрос (ответ — только сведения о Друге).
bool UAsdaSoulmateSubsystem::SendOldSoulReq2244(bool bAuto, const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	const bool bOk = Net->SendSoulmateOldPartnerReq2244();
	LastOldSoulReqSec = FPlatformTime::Seconds();
	++OldSoulReqSent;
	if (bAuto) { ++OfflineAutoReqSent; bAutoReqPending = true; }
	else       { bGridShowPending = true; GridShowPendingSec = LastOldSoulReqSec; }   // ★ 24.10.2026: отсчёт срока годности ожидания
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] →2244 (%s, %s): %s"), bAuto ? TEXT("автозапрос") : TEXT("кнопка"), Why,
		bOk ? TEXT("отправлен") : TEXT("ОШИБКА отправки"));
	return bOk;
}

void UAsdaSoulmateSubsystem::CancelOfflineReqTimer()
{
	if (OffReqTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(OffReqTicker);
		OffReqTicker.Reset();
	}
}

// 6520/6521/6556/6523/6522 у Друга вне игры — те же форматы, что `CSoulmateWnd_RefreshInfo` у Друга в игре.
bool UAsdaSoulmateSubsystem::FillOfflineFriendFields()
{
	if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() == 0 || !IsBoundTo(CommWnd)) { return false; }
	if (bOffStale || !OffFriend.bReplied || !OffFriend.bValid || OffFriend.Smidx != Card.SlotIndex || Card.SlotIndex <= 0) { return false; }
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT02"), OffFriend.Name);                                                  // 6520 имя
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT03"), FString::Printf(TEXT("%d %s "), (int32)OffFriend.Level, *Loc(12215)));   // 6521
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT10"), FAsdaCharRecord::ClassName(OffFriend.Grade));                       // 6556 профессия
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT05"), LocationText(OffFriend.Map, OffFriend.Channel));                    // 6523 локация
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT04"), OfflineLogoutText());                                               // 6522 последний выход
	return true;
}

// 6522 — местное время игрока (UTC из P79 + смещение часового пояса ПК). Порядок — как у макета разметки «2008년 12월 31일 23:59».
FString UAsdaSoulmateSubsystem::OfflineLogoutText() const
{
	if (!OffFriend.bHasTime) { return FString(); }
	const FDateTime L = OffFriend.LogoutUtc + (FDateTime::Now() - FDateTime::UtcNow());
	return FAsdaLang::IsRu()
		? FString::Printf(TEXT("%02d.%02d.%04d %02d:%02d"), L.GetDay(), L.GetMonth(), L.GetYear(), L.GetHour(), L.GetMinute())
		: FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d"), L.GetYear(), L.GetMonth(), L.GetDay(), L.GetHour(), L.GetMinute());
}

// ★★★ ПОРТ `itemEnchant_OnResultPacket@0xE67130` (★ МИСНЕЙМ: это НЕ заточка — диспетчер зовёт
//   его с `g_pSoulmateWnd` на опкоде 0x1455 = 5205, game.c:15530-15532). Коды и строки сняты
//   дословно (game.c:473175-473240):
//     0 — партнёр ПРОСИТ разрыва: тело 61 б кладётся в объект (+1772) и поднимается
//         ДВУХКНОПОЧНЫЙ вопрос loc 20220 с именем (+1783) в слоте `[ss1]`
//         (`CItem_BuildTooltipText(…, 1, 8030, …, 1)`); ответ уходит пакетом 5206
//         (`soulmateSendRegisterPacket@0xE67360`). ЕСЛИ у нас уже висит СВОЙ запрос разрыва —
//         вместо вопроса печатается loc 8038 в чат.
//     1 → loc 8027 · 2 → loc 8028 · 3 → loc 8029 — чат, цвет −8847556, TTL 10000.
//   Раньше порт на ВСЕ коды печатал loc 20223 (это строка ветки 2 пакета 5207) — неверно.
void UAsdaSoulmateSubsystem::OnBreakRefused(int32 Reason)
{
	switch (Reason)
	{
	case 0:
		// Ветка кода 0 целиком построена в `OnBreakRequest` — там есть тело (T2 закрыт).
		//   ⚠ ПОРЯДОК ВАЖЕН: `UAsdaNetSubsystem` шлёт СНАЧАЛА этот делегат, а `OnBreakRequest` —
		//   вторым. Эталон же снимает `m_nField473` В КОНЦЕ обработчика, ПОСЛЕ ветвления, и код 0
		//   как раз читает эту защёлку («мой запрос уже в полёте ⇒ вместо вопроса loc 8038»).
		//   Поэтому здесь её НЕ трогаем — снимет `OnBreakRequest`, отработав ветку.
		return;
	case 1: ChatSocial(Loc(8027)); break;
	case 2: ChatSocial(Loc(8028)); break;
	case 3: ChatSocial(Loc(8029)); break;
	default: break;
	}
	// ★★★ R22: конец обработчика 5205 снимает защёлку `m_nField473 = 0` — свой запрос разрыва
	//   больше не «в полёте», кнопка 6587 снова доступна (её второй гейт = это же поле).
	bBreakReqInFlight = false;
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5205 отказ разрыва: причина=%d"), Reason);
}

// ★★★ T2 ЗАКРЫТ — ВЕТКА 0 ПАКЕТА 5205 «ПАРТНЁР ПРОСИТ РАЗОРВАТЬ ДРУЖБУ».
//   Порт `itemEnchant_OnResultPacket@0xE67130` (★ миснейм: диспетчер зовёт его с `g_pSoulmateWnd`
//   на опкоде 0x1455 = 5205, game.c:15530-15532), код 0, game.c:473175-473205:
//     `memcpy(v11 + 1772, Src, 0x3D)` — блок 61 б в объект; если СВОЙ запрос уже в полёте
//     (`this+473`), печатается loc 8038 в чат; иначе — ДВУХКНОПОЧНЫЙ вопрос
//     `CItem_BuildTooltipText(…, 1, 8030, текст, 1)` с именем (+1783 = блок+11) в слоте `[ss1]`
//     строки 20220, а ответ уходит пакетом 5206 (`soulmateSendRegisterPacket@0xE67360`).
//   Раскладка блока — та же семья, что у 5202/5206 (см. `SendSoulmateBreakResp5206`):
//     [0] код · [1..4] i32 charId партнёра · [5..6] i16 userNum · [7] [8] эхо · [9] −1 · [10] канал
//     · [11..30] имя партнёра.
void UAsdaSoulmateSubsystem::OnBreakRequest(const TArray<uint8>& Body)
{
	if (Body.Num() < 1 || Body[0] != 0) { return; }   // прочие коды — в `OnBreakRefused`
	bBreakAskPending = true;
	BreakAskCharId  = (Body.Num() >= 5)  ? AsdaBytes::RdI32(Body.GetData() + 1) : -1;
	BreakAskUserNum = (Body.Num() >= 7)  ? AsdaBytes::RdI16(Body.GetData() + 5) : -1;
	BreakAskB7      = (Body.Num() >= 8)  ? Body[7]  : 0;
	BreakAskB8      = (Body.Num() >= 9)  ? Body[8]  : 0;
	BreakAskChannel = (Body.Num() >= 11) ? Body[10] : 0;
	BreakAskName    = (Body.Num() >  11)
		? FAsdaLoginData::DecodeCp1251(Body.GetData() + 11, FMath::Min(20, Body.Num() - 11))
		: FString();

	// Гейт эталона: свой запрос разрыва уже ушёл ⇒ вопроса нет, печатается loc 8038.
	if (bBreakReqInFlight)
	{
		ChatSocial(Loc(8038));
		bBreakAskPending  = false;
		bBreakReqInFlight = false;   // конец обработчика 5205: `m_nField473 = 0`
		UE_LOG(LogAsdaSoulmate, Log,
			TEXT("[соулмейт] 5205 код 0 от «%s»: свой запрос в полёте — вместо вопроса loc 8038"),
			*BreakAskName);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FString Msg = LocSlotStr(20220, BreakAskName);
	if (!WM)
	{
		ChatSocial(Msg);   // окна нет — игрок хотя бы увидит вопрос
		return;
	}
	// Тип содержимого 1 = двухкнопочный `TWOBUT_MSGBOX` 1520 (`ShowGameConfirm`).
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Msg, [this](bool bYes)
	{
		if (!bBreakAskPending) { return; }
		bBreakAskPending = false;
		if (UGameInstance* G = GetGameInstance())
		{
			if (UAsdaNetSubsystem* Net = G->GetSubsystem<UAsdaNetSubsystem>())
			{
				Net->SendSoulmateBreakResp5206(bYes, BreakAskCharId, BreakAskUserNum,
					BreakAskB7, BreakAskB8, BreakAskChannel, BreakAskName);
			}
		}
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 8030) item_inventory.c:57157*/8030, 1);
	bBreakReqInFlight = false;   // конец обработчика 5205: `m_nField473 = 0`
	UE_LOG(LogAsdaSoulmate, Log,
		TEXT("[соулмейт] 5205 код 0: «%s» (charId=%d userNum=%d) просит разрыв — вопрос 20220 поднят"),
		*BreakAskName, BreakAskCharId, BreakAskUserNum);
}

// Полная очистка связи — ветки 1 и 3 эталона: слот соулмейта = −1, признак пары снят,
//   запись обнулена (`CSoulmateWnd_ResetAndRefresh` заливает её значениями по умолчанию).
void UAsdaSoulmateSubsystem::ClearBond()
{
	Rec  = FAsdaSoulmateRecord();
	Card = FAsdaSoulmateCard();
	Card.SlotIndex = -1;
	// ★ 24.10.2026 (sm-offline): пары нет — сведения о Друге вне игры и отложенный автозапрос 2244 тоже.
	OffFriend = FAsdaOfflineFriend();
	bOffStale = true;
	bOffLeaveCheck = false;   // ★ 24.10.2026: пары нет — и проверки свежести ответа после 5226
	CancelOfflineReqTimer();
	HideNotePanel();
	// Разрыв связи В ИГРЕ: эталон гонит перерисовку по обнулённой записи (то самое «0 Ур.»).
	//   Сброс СЕССИИ (`ResetSessionState` снимает `bCardReceived`) — другое дело: там вкладка
	//   обязана вернуться к состоянию сразу после построения окна, то есть к пустым полям.
	if (bCardReceived) { RefreshCard(); }
	else               { ApplyStaticCaptions(); }
}

// ★ ТОЛЬКО ветка 3 (принудительный разрыв): эталон дополнительно чистит лоток навыков Дружбы
//   (12 слотов `SOUL_TRAY00..11`, гасит рамку `SOUL_TRAYSEL`) и снимает слоты хотбара ТИПА 4
//   (`hotbarClearType4Slots` + `hotbarRebuildSlots`) — навыки соулмейта больше недоступны.
void UAsdaSoulmateSubsystem::ResetSkillTrayAndHotbar()
{
	UGameInstance* GI = GetGameInstance();

	// 1) Лоток и флаги подсветки 30..44 — владелец `UAsdaSoulmateSkillSubsystem`
	//    (`skillHotbarClearAutoSlots` + гашение 12 ячеек и рамки 6538 у эталона).
	//    Своей копии этой чистки здесь быть не должно — иначе две подсистемы правят одни узлы.
	if (UAsdaSoulmateSkillSubsystem* Sk = GI ? GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr)
	{
		Sk->ResetTrayAndHotbar();
	}

	// 2) Слоты хотбара типа 4 (навык соулмейта) — снять и пересобрать кэш.
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return; }
	int32 nCleared = 0;
	for (int32 i = 0; i < UAsdaHotbarSubsystem::kSlotCount; ++i)
	{
		if (Hb->GetSlot(i).Type == (uint8)EAsdaHotbarSlotType::SkillAuto)
		{
			Hb->ClearSlot(i);
			++nCleared;
		}
	}
	Hb->RebuildSlots();
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] принудительный разрыв: снято слотов хотбара типа 4: %d"), nCleared);
}

// ═══════════════════════════════ УТИЛИТЫ ══════════════════════════════════════════════════════
FString UAsdaSoulmateSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// `CObject_SetSlotString(ctx, 0, S)` + `UI_FormatLocalizedNotice(ctx, Id)` — маркер `[ss1]`.
FString UAsdaSoulmateSubsystem::LocSlotStr(int32 Id, const FString& S) const
{
	// Маркер `[ss1]` — общая подстановка порта (закон loc-string-placeholders).
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocSlot` (слот 1 + FormatNotice, как было).
	return AsdaNotice::LocSlot(GetGameInstance(), Id, S);
}

// `CClass_SetSlotPair(ctx, 0, V)` + формат — маркер `[sd1]`.
FString UAsdaSoulmateSubsystem::LocSlotNum(int32 Id, int32 V) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!Txt) { return FString(); }
	Txt->NoticeSetInt(0, V);
	return Txt->FormatNotice(Id);
}

// ★★★ ИСПРАВЛЕНО: раньше строки уходили БЕЗ цвета. Эталон печатает ВСЕ строки соулмейта
//   `CChat_AddTokenizedLines(g_pChatWnd, buf, −8847556, 10000, 0)` — game.c:472765, 472842,
//   473311, 473334 и др. −8847556 = 0xFF78FF3C = (120, 255, 60), светло-зелёный.
void UAsdaSoulmateSubsystem::ChatSocial(const FString& Text) const
{
	if (Text.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		FAsdaMsgParams P;
		P.Category  = EAsdaMsgCategory::Social;
		P.Text      = Text;
		P.bUseColor = true;
		P.Color     = FColor(0x78, 0xFF, 0x3C, 0xFF);   // −8847556
		P.TtlMs     = 10000.f;
		P.SrcOpcode = 5203;
		Msg->Post(P);
	}
}

// ★★★ T6 ЗАКРЫТ — НОТИС-ОКНО ТИПА 0. Эталон выводит им отказы соулмейта:
//   `CItem_BuildTooltipText(g_pItemTooltipUI, 0, id, текст, 1)`. Первый аргумент после `this` —
//   `contentKind` окна сообщения (`CMsgBox_SetContent`, ui_core.c:55951): 0 = ОДНОКНОПОЧНОЕ
//   `ONEBUT_MSGBOX` 1500 (кнопка `ONEBUT_BUT00` ← loc 2288 «ОК»), 1 = двухкнопочное
//   `TWOBUT_MSGBOX` 1520. В порту первое — `UAsdaWindowSubsystem::ShowGameMessage`, второе —
//   `ShowGameConfirm`; заводить своё окно нельзя (закон `port-shared-points`).
//   Места вызова с типом 0: 6035 у кнопки 6587, 8008/8009 и 20167/20168 у пакета 5203.
void UAsdaSoulmateSubsystem::ShowNotice0(const FString& Text, int32 EtalonNoticeId) const
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было «окна нет ⇒ `ChatTip` (жёлтая строка Tip-канала)» — стало общая
	//   политика `AsdaNotice::MsgBox` (окна нет ⇒ лента категорией ItemRefused — тот же канал, что у окна).
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text, AsdaLoc::kBtnOk, EtalonNoticeId);
}

// Канал «подсказка» эталона: `CChat_AddTokenizedLines(..., −11514, 10000, 0)`.
//   −11514 = 0xFFFFD306 = (255, 211, 6), жёлтый. Им идёт отказ 6035 у кнопки 6583
//   (`case 0x19B7`: это ЕДИНСТВЕННОЕ место, где отказ печатается строкой, а не окном).
void UAsdaSoulmateSubsystem::ChatTip(const FString& Text) const
{
	if (Text.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		FAsdaMsgParams P;
		P.Category  = EAsdaMsgCategory::Social;
		P.Text      = Text;
		P.bUseColor = true;
		P.Color     = AsdaNoticeColor::Gold;   // −11514
		P.TtlMs     = 10000.f;
		P.SrcOpcode = 5207;
		Msg->Post(P);
	}
}

void UAsdaSoulmateSubsystem::WhisperTo(const FString& Name) const
{
	if (Name.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		// Порт `CChatList_OnSelectWhisper`: в поле ввода встаёт литерал «/w имя ».
		Chat->BeginWhisperTo(Name);
	}
}

FString UAsdaSoulmateSubsystem::DumpState() const
{
	return FString::Printf(
		TEXT("пара=%d слот=%d партнёр='%s' предложение=%d(от '%s') др.ур=%u опыт=%lld ")
		TEXT("режим9100=%u защёлки: 5204=%d вопрос5205=%d фильтр=%d кандидатов=%d страницы cur/req/last=%d/%d/%d ")
		TEXT("поп-карточка: узлы=%d видима=%d ")
		TEXT("вне игры: ответ=%d сведения=%d устарели=%d «%s» 2244 всего/авто=%d/%d отложен=%d 6640=%d"),
		Card.bHasSoulmate ? 1 : 0, Card.SlotIndex, *Card.PartnerName,
		Proposal.bPending ? 1 : 0, *Proposal.InitName,
		(uint32)Rec.FriendLevel(), (long long)Rec.FriendExp(), (uint32)LinkWinMode,
		bBreakReqInFlight ? 1 : 0, bBreakAskPending ? 1 : 0,
		SexFilter, Candidates.Num(), SearchPage, SearchReqPage, SearchLast,
		bPopBound ? 1 : 0, (PopWnd && PopWnd->GetVisibility() != ESlateVisibility::Collapsed) ? 1 : 0,
		OffFriend.bReplied ? 1 : 0, OffFriend.bValid ? 1 : 0, bOffStale ? 1 : 0, *OffFriend.Name,
		OldSoulReqSent, OfflineAutoReqSent, OffReqTicker.IsValid() ? 1 : 0, IsOldSoulGridShown() ? 1 : 0);
}
