// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// Порт навыков Дружбы (A) и Зова друга (B). Разбор — `Reports/soulmate_skills_recall_roulette.md`
//   плюс поправки П1..П4, перечисленные в шапке `AsdaSoulmateSkillSubsystem.h`.
//
// ═════════════════════ ТАБЛИЦА НОТИСОВ «СОБЫТИЕ → loc → КАНАЛ» (полная) ════════════════════════
//  A. НАВЫКИ ДРУЖБЫ, коды пакета 5232 (network.c:45475-46351). Канал у ВСЕХ — «подсказка» эталона
//     `CControl_SetTooltipText(g_pChatWnd, текст, −11514 = 0xFFFFD306 жёлтый, 10000 мс)`.
//     У порта это категория `SkillRefused` с перекрытием цвета −11514; маршрут (окно/чат) решает
//     `DT_MessageRoutes` — правится БЕЗ пересборки.
//        код 0 → 8100 (плюс постановка состояния 20 у кастующего) │ 1 → нотиса НЕТ (успех)
//        2 → 8127 │ 3 → 8101 │ 4 → 8102 │ 5 → 8103 │ 6 → 8104 │ 7 → 8105 │ 8 → 8106 │ 9 → 8107
//        10 → 8108 │ 11 → 8109 │ 12 → 8110 │ 13 → 8111 │ 14 → 8112, а при навыке 39 → 2203
//        15 → 8113 │ 16 → 8114 │ 17 → 8115 │ 18 → ВЕТКИ НЕТ (дефект D4) │ 19 → 8117 │ 20 → 8118
//        21 → 8119 │ 22 → 8120 │ 23 → 8121 │ 24 → 8122 │ 25 → 8123 │ 26 → 8124 │ 27 → 8125
//        28 → 8126 │ 29 → 8165 │ 30 → 8166 │ 31 → 2039 │ 32 → 8168 │ 33 → 8169 │ 34 → 8171 │ 35 → 8173
//     2247, персонаж пакета не найден → loc 12846, канал ЧАТ, цвет −21248 (оранжевый),
//        `CChat_AddTokenizedLines(g_pChatWnd, …, −21248, 10000, 0)` (network.c:28894).
//  B. ЗОВ ДРУГА:
//        приход зова (5236)                  → loc 8163, канал ОКНО-вопрос: двухкнопочный msgbox
//                                              (эталон `CItem_BuildTooltipText(…, 1, 38, loc, 1)`,
//                                               у порта — `UAsdaWindowSubsystem::ShowGameConfirm`);
//        5237 код 0 (рассинхрон charId)      → 9576,  подсказка −11514
//        5237 код 1 (согласие)               → нотиса НЕТ
//        5237 код 2 (цель отказалась)        → 9577,  подсказка −11514
//        5237 код 3 (другой канал, не в группе) → 9584, подсказка −11514
//        5237 код 4 (запрещённый тип карты)  → 13920, подсказка −11514
//        5237 код 5 (цель занята)            → 13928, подсказка −11514
//     ★ Строка 8163 у эталона — вопрос «Друг призывает тебя. Ответишь на зов?»; кнопки берут
//       ОБЩИЕ подписи msgbox (loc 2288 «ОК» / 2289 «Отмена»), своих подписей эталон не ставит.
//
// ══════════════════════════ ДЕФЕКТЫ ОРИГИНАЛА: ЧТО 1:1, ЧТО ЧИНИМ ══════════════════════════════
//   D1 (5232: цикл на 20 целей при массивах по 10) — ИСПРАВЛЕН КОНСТРУКТИВНО: разбор блока идёт
//      по фактическим длинам массивов (10), чтения за их пределы у порта физически нет.
//   D2 (5233 читает flagsB из блока A) — воспроизводится 1:1 по умолчанию; исправление за ключом
//      `asda.Soul.Fix5233FlagsB` (0 = как в оригинале, 1 = читать blk+174, как это делает 5232).
//   D4 (кода 18 у 5232 нет ветки) — воспроизводится 1:1 (молчание); при `asda.Soul.Skills.FixSilentCode18 1`
//      выводится служебная строка 990101 (свой диапазон > 990000, закон о своих строках).
//   D5 (5231: два байта payload не инициализированы) — лечится в `UAsdaNetSubsystem` (не наш файл).
//   D7 (серверная запись за границы damagesB) — серверный дефект, к клиенту отношения не имеет.
//   D8 — СНЯТ 31.08.2026 (это была НЕ ошибка оригинала, а наша ошибка метода). Писателей байта
//      «иконка зажжена» ДВА, и оба нашлись (`clan_soulmate_gaps.md` §3): опкоды **5228** (весь
//      блок 101 б, `netRecvSkillHotbarFull@0xF50560`) и **5238** (один слот, i16 индекс,
//      `netHandleSkillSlotEnable@0xF505A0`). Их принимает `UAsdaNetSubsystem`, подсветка идёт по
//      реальным данным сервера, ключ-костыль `asda.Soul.TrayLit` УДАЛЁН.
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "UI/AsdaItemTooltipSubsystem.h"     // ★ 07.11.2026: подсказка ячейки лотка
#include "UI/AsdaSkillWindowSubsystem.h"     // ★ 07.11.2026: текст подсказки навыка Дружбы
#include "UI/AsdaShopDblClick.h"             // ★ 07.11.2026: FAsdaDblClick — порог двойного щелчка
#include "UI/AsdaTipPanel.h"                 // ★ 07.11.2026: EAsdaTipOwner, FAsdaTipMetrics
#include "UI/AsdaWindowSubsystem.h"          // ★ 07.11.2026: окно под курсором
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"   // GetFriendLevel — уровень ДРУЖБЫ (rec[50]) для гейта лотка
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ волна В3 ремесла: `uiWindowHideAndCleanup(g_pItemMainWnd)` при согласии на зов
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: хвост `uiCloseAllWindowsOnLeave` — окно обмена 2008 (`CExchange+0x2C`)
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // ★ волна В4 ремесла: `itemQtyDlgReset(g_pItemQtyMakeDlg)` при согласии на зов
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaNoticeColors.h"   // ★ R5-c (12.11.2026): AsdaNoticeColor::Gold — золотой цвет нотисов (общая точка)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulSkill, Log, All);

const TCHAR* UAsdaSoulmateSkillSubsystem::kSkillTablePath =
	TEXT("/Game/AsdaAssets/Data/ItemDB/DT_soulmateskill.DT_soulmateskill");
const TCHAR* UAsdaSoulmateSkillSubsystem::kTextTablePath =
	TEXT("/Game/AsdaAssets/Data/ItemDB/DT_soulmateskilltable.DT_soulmateskilltable");

namespace
{
	// ── Цвета эталона (sRGB, как их пишет `CControl_SetTooltipText` / `CChat_AddTokenizedLines`) ──
	const FColor kTipYellow = AsdaNoticeColor::Gold;   // −11514   ★ R5-c (12.11.2026): значение — общая точка `AsdaNoticeColor::Gold` (было своё `FColor(0xFF, 0xD3, 0x06, 0xFF)`, те же байты).
	const FColor kChatOrange(0xFF, 0xAC, 0x00, 0xFF);  // −21248

	// ── Свои строки (закон: свои id только > 990000; текст живёт в `Fallback`, если строки нет) ──
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): 990101 был ЗАНЯТ строкой доп-меню («Список доп. меню пуст…», AsdaExtraMenuSubsystem) —
	//   отказ 18 показывал бы чужой текст; своя строка 990493. Строка 990102 заведена в DT_GameText (ведомость).
	constexpr int32 kLocOwnSilent5232 = 990493;   // «Навык Дружбы: сервер отказал (код 18).»
	constexpr int32 kLocOwnNoTable    = 990102;   // «Таблица навыков Дружбы не загружена.»

	// ── Ключи-переключатели дефектов (дефолт = поведение оригинала, закон «спорные — за CVar») ──
	static ASDA_DEV_CVAR(int32, CVarFix5233FlagsB,
		TEXT("asda.Soul.Fix5233FlagsB"), 0,
		TEXT("Дефект D2: 0 — как в оригинале (5233 читает флаги половины B из блока A, network.c:46392); ")
		TEXT("1 — читать их по blk+174, как это делает 5232."));

	static ASDA_DEV_CVAR(int32, CVarFixSilentCodes,
		TEXT("asda.Soul.Skills.FixSilentCode18"), 0,
		TEXT("Дефект D4: 0 — код 18 пакета 5232 остаётся без текста (как в оригинале); ")
		TEXT("1 — показывать служебную строку 990493."));

	// ★★★ КЛЮЧ `asda.Soul.TrayLit` УБРАН 31.08.2026. Он вводился, пока писатель байта «иконка
	//   зажжена» считался ненайденным, и подменял данные догадкой «уровень взят ⇒ зажечь».
	//   Писателей нашлось ДВА (5228 и 5238, `clan_soulmate_gaps.md` §3), оба принимает сеть ⇒
	//   подсветка идёт по РЕАЛЬНОМУ блоку сервера, догадка не нужна и запрещена законом проекта.
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════

void UAsdaSoulmateSkillSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadTables();

	if (UAsdaNetSubsystem* N = Net())
	{
		HState  = N->OnSoulSkillState.AddUObject(this, &UAsdaSoulmateSkillSubsystem::OnSkillState5232);
		HApply  = N->OnSoulSkillApply.AddUObject(this, &UAsdaSoulmateSkillSubsystem::OnSkillApply5233);
		HCool   = N->OnSoulSkillCooldowns.AddUObject(this, &UAsdaSoulmateSkillSubsystem::OnSkillCooldowns2247);
		HRecall = N->OnMateRecall.AddUObject(this, &UAsdaSoulmateSkillSubsystem::OnMateRecall5236);
		// ★★★ ПОДСВЕТКА ИКОНОК ЛОТКА: 5228 (весь блок 101 б) и 5238 (один слот) — оба писателя
		//   байта `объект+1257+k`. Эталон после КАЖДОГО из них зовёт `skillHotbarRefreshSlots`,
		//   поэтому подписка ведёт ровно туда же — в `RefreshTray()`.
		HTray = N->OnSoulSkillTray.AddUObject(this, &UAsdaSoulmateSkillSubsystem::OnSkillTrayLit);
		bNetBound = true;
	}
	UE_LOG(LogAsdaSoulSkill, Log,
		TEXT("[навыки Дружбы] подсистема поднята: таблица %d записей, тексты %d записей, сеть %s"),
		Skills.Num(), Texts.Num(), bNetBound ? TEXT("привязана") : TEXT("НЕ привязана"));
}

void UAsdaSoulmateSkillSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* N = Net())
	{
		N->OnSoulSkillState.Remove(HState);
		N->OnSoulSkillApply.Remove(HApply);
		N->OnSoulSkillCooldowns.Remove(HCool);
		N->OnMateRecall.Remove(HRecall);
		N->OnSoulSkillTray.Remove(HTray);
	}
	ReleaseTray();
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: кулдауны навыков Дружбы (2247) и
//   заявка «зов друга» — состояние сессии. Заявка ещё и адресуется сессионными номерами,
//   поэтому её нельзя переносить ни через перезаход, ни через смену карты.
void UAsdaSoulmateSkillSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	Recall = FAsdaMateRecallRequest();
	// ★ Очередь окон-сообщений (26.09): вопрос — запись (1, 38) общей очереди. Снимаем её через очередь (молча,
	//   без колбэка); голое `RemoveFromParent` оставило бы запись в очереди и окно в реестре оконной подсистемы.
	//   ★ Редакция 1 (ревизия С-2): снимаем ПО ПАРЕ безусловно — указатель `RecallBox` мог не встать (второй 5236 при
	//   открытом первом отброшен дублем). М-3: следующую запись посреди травела не рисуем — её вернёт `CloseAll`.
	if (UAsdaWindowSubsystem* Win = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		Win->RemoveMsgBoxEntries(1, 38, TEXT("зов друга: сброс сессии"), /*bRedisplayNow*/false);
	}
	RecallBox = nullptr;
	ReleaseTray();
	for (int32 i = 0; i < kTraySlots; ++i) { TraySkillId[i] = -1; }
	bTrayVisible = false;
	if (!AsdaIsFullSessionReset(Reason)) { return; }

	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] сброс сессии (%s): кулдаунов снято %d"),
		AsdaSessionResetReasonName(Reason), Cooldowns.Num());
	Cooldowns.Reset();
}

UAsdaNetSubsystem* UAsdaSoulmateSkillSubsystem::Net() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
}

// ═══════════════════════════════ ТАБЛИЦЫ (только данные) ═══════════════════════════════════════

// Обе таблицы строятся из архива `Data\character\Bin.bbz`:
//   `SoulmateSkill.ssi`       — 45 × 112 б, БЕЗ заголовка, индекс записи == id;
//   `soulmateskilltable.bin`  — заголовок 20 б (версия f32 / count i32 / extra i32 / тег «SKT» / pad),
//                               далее 45 × 272 б; +0 i32 id, +4 char[32] имя, +36 char[32] иконка.
// Сырым .bin/.ssi в `Content` не место (см. решение в `UAsdaSoulGuardSubsystem::LoadTables`):
//   нет таблицы — это ОШИБКА, и она видна в журнале, а не «тихо работает без данных».
void UAsdaSoulmateSkillSubsystem::LoadTables()
{
	if (bTablesLoaded) { return; }
	Skills.Reset();
	Texts.Reset();

	if (const UDataTable* Dt = AsdaDt::Cached(kSkillTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			if (const FAsdaSoulmateskillRow* R = reinterpret_cast<const FAsdaSoulmateskillRow*>(It.Value))
			{
				if (R->Id >= 0) { Skills.Add(R->Id, *R); }
			}
		}
	}
	else
	{
		UE_LOG(LogAsdaSoulSkill, Error,
			TEXT("[навыки Дружбы] нет таблицы %s — лоток и кулдауны не заработают ")
			TEXT("(источник: Data\\character\\Bin.bbz → SoulmateSkill.ssi, 45 × 112 б)"), kSkillTablePath);
	}

	if (const UDataTable* Dt = AsdaDt::Cached(kTextTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			if (const FAsdaSoulmateskilltableRow* R = reinterpret_cast<const FAsdaSoulmateskilltableRow*>(It.Value))
			{
				if (R->Id >= 0) { Texts.Add(R->Id, *R); }
			}
		}
	}
	else
	{
		UE_LOG(LogAsdaSoulSkill, Error,
			TEXT("[навыки Дружбы] нет таблицы %s — иконки лотка рисовать нечем ")
			TEXT("(источник: Data\\character\\Bin.bbz → soulmateskilltable.bin, 45 × 272 б)"), kTextTablePath);
	}

	bTablesLoaded = (Skills.Num() > 0) || (Texts.Num() > 0);
}

const FAsdaSoulmateskillRow* UAsdaSoulmateSkillSubsystem::GetSkill(int32 SkillId) const
{
	return Skills.Find(SkillId);
}

const FAsdaSoulmateskilltableRow* UAsdaSoulmateSkillSubsystem::GetSkillText(int32 SkillId) const
{
	return Texts.Find(SkillId);
}

// ═══════════════════════════════ ЛОТОК: ПРИВЯЗКА И ОТРИСОВКА ═══════════════════════════════════

void UAsdaSoulmateSkillSubsystem::OnTabActivated(bool bVisible, UUserWidget* CommunityWindow)
{
	bTrayVisible = bVisible;
	if (!bVisible)
	{
		// Окно скрыто — снимаем латч мыши (иначе удержанная кнопка съест первый клик после показа,
		//   грабли из `FAsdaMouseFrame::Reset`), гасим выбор и НИЧЕГО не защёлкиваем.
		Mouse.Reset();
		bPickArmed = false;
		PickedSkillId = -1;
		return;
	}
	if (!bTrayBound || CommWnd != CommunityWindow) { BindTrayControls(CommunityWindow); }
	RefreshTray();
}

// `CSoulmateWnd_InitControls@0xE5E440` (social.c:6090-6096): 12 ячеек IDNum 6526..6537 и рамка 6538.
//   Импортёр JUI даёт интерактивным узлам ТОЧНОЕ имя idc, поэтому ищем `SOUL_TRAY00..11` и
//   `SOUL_TRAYSEL` точным именем; при промахе пробуем префикс (закон о порядковых хвостах).
void UAsdaSoulmateSkillSubsystem::BindTrayControls(UUserWidget* CommunityWindow)
{
	ReleaseTray();
	CommWnd = CommunityWindow;
	if (!CommWnd) { return; }

	TrayCells.SetNum(kTraySlots);
	int32 Found = 0;
	for (int32 i = 0; i < kTraySlots; ++i)
	{
		const FString Idc = FString::Printf(TEXT("SOUL_TRAY%02d"), i);
		UWidget* W = AsdaUi::FindNodeExact(CommWnd, *Idc);
		if (!W) { W = AsdaUi::FindNodeByPrefix(CommWnd, *Idc); }
		TrayCells[i] = Cast<UImage>(W);
		// ★ 07.11.2026: точное имя носит TextBlock (скин `NoImage_icon` — Type=Text), картинка ячейки — план `{idc}_{обход}_0`.
		if (!TrayCells[i] && AsdaSoulTrayFlowOn() && CommWnd->WidgetTree)
		{
			const FString Plane = Idc + TEXT("_");
			UImage* Found2 = nullptr;
			CommWnd->WidgetTree->ForEachWidget([&Found2, &Plane](UWidget* X)
			{
				if (!Found2 && X && X->GetName().StartsWith(Plane)) { Found2 = Cast<UImage>(X); }
			});
			TrayCells[i] = Found2;
		}
		if (TrayCells[i]) { ++Found; }
		else { UE_LOG(LogAsdaSoulSkill, Warning, TEXT("[навыки Дружбы] ячейка «%s» не найдена"), *Idc); }
	}
	TraySel = AsdaUi::FindNodeExact(CommWnd, TEXT("SOUL_TRAYSEL"));
	if (!TraySel) { TraySel = AsdaUi::FindNodeByPrefix(CommWnd, TEXT("SOUL_TRAYSEL")); }
	if (TraySel) { TraySel->SetVisibility(ESlateVisibility::Collapsed); }

	bTrayBound = true;
	Mouse.Reset();
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] лоток привязан: ячеек %d/12, рамка выбора %s"),
		Found, TraySel ? TEXT("есть") : TEXT("НЕ НАЙДЕНА"));
}

void UAsdaSoulmateSkillSubsystem::ReleaseTray()
{
	TrayCells.Reset();
	TraySel = nullptr;
	CommWnd = nullptr;
	bTrayBound = false;
	bPickArmed = false;
	PickedSkillId = -1;
	Mouse.Reset();
}

// ★★★ ИСПРАВЛЕНО (аудит 31.08 — было КРИТИЧНО). Гейт лотка сравнивает требование навыка
//   `rec+42` НЕ с уровнем персонажа, а с `HIBYTE(CCharacter::field_8A5)`
//   (`skillHotbar_FindBestSkillSlot@0xE691A0`, game.c:474015-474040;
//    `skillHotbarRefreshSlots@0xE69440`, skill_buff.c:34704-34712).
//   Писателей у `HIBYTE(field_8A5)` в бинаре ровно ТРИ, и все три пишут rec[50] — УРОВЕНЬ ДРУЖБЫ:
//     game.c:472431 `HIBYTE(field_8A5) = v19[1407]`   (5110; 1407 = 1357 + 50)
//     game.c:472533 `HIBYTE(field_8A5) = gap_54D[50]` (5111)
//     game.c:471335 то же, из `uiProgressBar_RefreshPercent`.
//   Читателей — те же гейты плюс печать «%d <loc 12215>» (game.c:471203-471207), то есть
//   строка «уровень Дружбы». Уровень ПЕРСОНАЖА к отбору навыков Дружбы отношения не имеет.
int32 UAsdaSoulmateSkillSubsystem::SelfLevel() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	return Sm ? Sm->GetFriendLevel() : 0;
}

// Поле персонажа `gap_8BE[70]`, с которым сравнивается `rec+87` (skill_buff.c:34814).
//   Отчёт §A.1 трактует +87 как «требование пола»; в порту берём пол персонажа
//   (`ResolveSelfGender`: 1 = муж., 2 = жен.). ⚠ В реальных данных +87 = 0 у всех 45 записей,
//   поэтому ветка сравнения живой игрой не проверяется — помечено как недоказанное.
int32 UAsdaSoulmateSkillSubsystem::SelfGenderField() const
{
	const UAsdaNetSubsystem* N = Net();
	return N ? (int32)N->ResolveSelfGender() : 0;
}

// Гейт эталона «есть ли соулмейт»: `LocalPlayer->gap_878[47] != -1` (uiQuickSlotActivateByMsgId,
//   skillHotbarRefreshSlots). У порта тот же признак держит `UAsdaSkillUseSubsystem::HasSoulmate`
//   (поле `SelfSoulIdx`) — второй копии этого состояния заводить нельзя (закон общих точек).
bool UAsdaSoulmateSkillSubsystem::HasSoulmate() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaSkillUseSubsystem* Use = GI ? GI->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
	return Use && Use->HasSoulmate();
}

// Порт `skillHotbar_FindBestSkillSlot@0xE691A0` (skill_buff.c:34719-34764) — дословно:
//   • группа < 0 → навык одиночный, показываем всегда (эталон возвращает −1);
//   • иначе среди 30..44 той же группы берём ДОСТУПНЫЕ по уровню и выбираем с НАИБОЛЬШИМ
//     требуемым уровнем; если доступных нет — первый недоступный с наименьшим требованием;
//   • показываем навык, только если выбранным оказался он сам.
bool UAsdaSoulmateSkillSubsystem::IsTrayRepresentative(int32 SkillId) const
{
	const FAsdaSoulmateskillRow* Me = GetSkill(SkillId);
	if (!Me) { return false; }
	if (Me->GroupId < 0) { return true; }

	const int32 MyLevel = SelfLevel();
	int32 BestId = -1, BestLevel = -1, Count = 0;
	for (int32 k = kFirstSkillId; k <= kLastSkillId; ++k)
	{
		const FAsdaSoulmateskillRow* R = GetSkill(k);
		if (!R || R->GroupId != Me->GroupId) { continue; }
		if (MyLevel >= R->ReqLevel)
		{
			if (BestLevel == -1 || BestLevel < R->ReqLevel) { BestLevel = R->ReqLevel; BestId = k; }
			++Count;
		}
	}
	if (Count == 0)
	{
		// Ни одной доступной ступени — эталон берёт ПЕРВУЮ недоступную с наименьшим требованием
		//   и обрывает цикл (`v4 = 1; break;`).
		for (int32 k = kFirstSkillId; k <= kLastSkillId; ++k)
		{
			const FAsdaSoulmateskillRow* R = GetSkill(k);
			if (!R || R->GroupId != Me->GroupId) { continue; }
			if (MyLevel < R->ReqLevel)
			{
				if (BestLevel == -1 || BestLevel > R->ReqLevel) { BestId = k; }
				Count = 1;
				break;
			}
		}
	}
	if (Count == 0) { return false; }   // эталон возвращает группу (≥ 0) ⇒ навык НЕ показывается
	return (BestId == SkillId);
}

// Порт `skillHotbarRefreshSlots@0xE69440` (skill_buff.c:34766-34840).
void UAsdaSoulmateSkillSubsystem::ClearAllCooldowns()
{
	Cooldowns.Reset();
	RefreshTray();
}

void UAsdaSoulmateSkillSubsystem::RefreshTray()
{
	if (!bTrayBound) { return; }

	// Шаг 1 эталона: очистить ВСЕ 12 ячеек (кадр UV 0,0,32,32 + скрыть + id = −1).
	for (int32 i = 0; i < kTraySlots; ++i)
	{
		TraySkillId[i] = -1;
		if (UImage* Cell = TrayCells.IsValidIndex(i) ? TrayCells[i].Get() : nullptr)
		{
			AsdaUi::ClearImageBrush(Cell);
			Cell->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (TraySel) { TraySel->SetVisibility(ESlateVisibility::Collapsed); }
	bPickArmed = false;
	PickedSkillId = -1;

	// Нет соулмейта — эталон на этом и останавливается (ветка `aSlotData[523] == -1`).
	if (!HasSoulmate()) { return; }

	const int32 MyGender = SelfGenderField();
	const int32 MyLevel  = SelfLevel();

	int32 Filled = 0, LitCount = 0;
	for (int32 k = kFirstSkillId; k <= kLastSkillId && Filled < kTraySlots; ++k)
	{
		const FAsdaSoulmateskillRow* R = GetSkill(k);
		if (!R) { continue; }
		// Гейт пола/класса: `rec[87] == char[gap_8BE+70] || rec[87] == 0`.
		if (R->ClassReq != 0 && R->ClassReq != MyGender) { continue; }
		// Гейт «представитель группы».
		if (!IsTrayRepresentative(k)) { continue; }

		const FAsdaSoulmateskilltableRow* T = GetSkillText(k);
		// ★ M9 ИСПРАВЛЕНО (аудит 31.08). Эталон (`skillHotbarRefreshSlots@0xE69440`,
		//   skill_buff.c:34818-34824) проверяет ТОЛЬКО наличие записи — `if (skillEntry)`;
		//   пустое имя иконки его не смущает, слот всё равно занимается. Порт дополнительно
		//   требовал непустое `SkillName` и при пустом ПРОПУСКАЛ навык — а это сдвигает ВСЕ
		//   последующие слоты лотка влево относительно оригинала. Условие снято.
		if (!T) { continue; }
		// Зажжённая иконка = имя как есть; погашенная = имя + «-g» (skill_buff.c:34821-34824).
		//   ★ Источник флага — РЕАЛЬНЫЙ блок сервера (5228/5238), а не догадка по уровню.
		const bool bLit = IsSkillLit(k);
		if (bLit) { ++LitCount; }
		const FString IconName = bLit ? T->SkillName : (T->SkillName + TEXT("-g"));

		if (UImage* Cell = TrayCells.IsValidIndex(Filled) ? TrayCells[Filled].Get() : nullptr)
		{
			// Ячейка JUI — 32×32 (`SOUL_TRAY00` Size «32 32»), эталон рисует `DrawTextureRect(0,0,32,32)`.
			AsdaUi::ApplyFlatJuiIcon(Cell, IconName, 32.f);
			Cell->SetVisibility(ESlateVisibility::Visible);
		}
		TraySkillId[Filled] = k;
		++Filled;
	}

	// ★ ИСПРАВЛЕНО: эталон завершает эту ветку `hotbarRebuildSlots(g_pQuickSlotBar)`
	//   (skill_buff.c:34838) — иначе иконки навыков Дружбы на панели быстрых слотов остаются
	//   от прошлого состава лотка. В ветке «соулмейта нет» пересборки нет — там мы уже вышли.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>()) { Hb->RebuildSlots(); }
	}

	UE_LOG(LogAsdaSoulSkill, Log,
		TEXT("[навыки Дружбы] лоток пересобран: занято %d/12 ячеек, зажжено %d (уровень Дружбы %d)"),
		Filled, LitCount, MyLevel);
}

// ═══════════ БАЙТ «ИКОНКА НАВЫКА ЗАЖЖЕНА» — ЕДИНСТВЕННЫЙ ИСТОЧНИК: БЛОК 5228/5238 ══════════════

// Прокси к блоку сети (`объект + 1257 + k` эталона = `blob[k + 1]`, skill_buff.c:34822).
//   Своей копии массива подсистема не держит: два источника правды разъезжаются, один — нет.
bool UAsdaSoulmateSkillSubsystem::IsSkillLit(int32 SkillId) const
{
	const UAsdaNetSubsystem* N = Net();
	return N ? N->IsSoulTraySkillLit(SkillId) : false;
}

// Порт `skillHotbarClearAutoSlots@0xE68ED0` (skill_buff.c:34646-34664): при живой паре гасятся
//   ТОЛЬКО навыки k = 30..44 и только те, у кого байт РОВНО 1; остальной блок не трогается.
void UAsdaSoulmateSkillSubsystem::ClearTrayLit()
{
	UAsdaNetSubsystem* N = Net();
	if (!N) { return; }
	int32 Cleared = 0;
	for (int32 k = kFirstSkillId; k <= kLastSkillId; ++k)
	{
		const int32 Idx = k + 1;                       // база+1, как у читателя эталона
		if (Idx >= 0 && Idx < UAsdaNetSubsystem::kSoulTrayBlobLen && N->SoulSkillTrayBlob[Idx] == 1)
		{
			N->SoulSkillTrayBlob[Idx] = 0;
			++Cleared;
		}
	}
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] погашено иконок лотка: %d (k = 30..44)"), Cleared);
}

// 5228 (SkillId == −1, пришёл весь блок) либо 5238 (SkillId = открытый навык).
//   Эталон после ОБОИХ пакетов зовёт `skillHotbarRefreshSlots` — делаем то же самое.
void UAsdaSoulmateSkillSubsystem::OnSkillTrayLit(int32 SkillId)
{
	++RxTrayLit;
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] блок подсветки обновлён (%s) — пересобираю лоток"),
		(SkillId < 0) ? TEXT("5228, весь блок") : *FString::Printf(TEXT("5238, навык %d"), SkillId));
	RefreshTray();
}

// Порт `uiQuickSlotCursorSet@0xE68A20`: рамка встаёт в позицию ячейки и показывается; SlotIdx < 0 — прячется.
void UAsdaSoulmateSkillSubsystem::SetTraySelection(int32 SlotIdx)
{
	if (!TraySel) { return; }
	if (SlotIdx < 0 || !TrayCells.IsValidIndex(SlotIdx) || !TrayCells[SlotIdx])
	{
		TraySel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// Позиция берётся у ЖИВОГО слота ячейки (эталон читает `Class_GetVec2_74` контрола ячейки).
	FVector2D Pos, Size;
	if (AsdaUi::SlotRect(TrayCells[SlotIdx].Get(), Pos, Size))
	{
		AsdaUi::SetSlotPos(TraySel, Pos);
	}
	TraySel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

// ═══════════════════════════════ КЛИК ПО ЛОТКУ (живая геометрия) ═══════════════════════════════
//
// Эталон разводит нажатие и отпускание по двум функциям:
//   `uiQuickSlotActivateByMsgId@0xE68940` — нажатие: гейт «соулмейт есть», IDNum в 6526..6537,
//       id в ячейке != −1 → запомнить id (+488), взвести флаг (+490), показать рамку;
//   `uiQuickSlotSkillActivate@0xE68AC0`   — отпускание: те же гейты И совпадение id ячейки
//       с запомненным → `skillCastActivate(g_pSkillWnd, id, 0)`.
// Ловим по ЖИВОЙ ГЕОМЕТРИИ (`AsdaUi::IsCursorOverWidget`), а не по слоту канваса: иерархия JUI
//   расплющена, и координаты слота ячейке не принадлежат (законы `jui-cell-chain-nesting`,
//   `ui-check-game-path-geometry`).
//
// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] Весь поток лотка за ключом `asda.Soul.TrayFlow` (баг игроков «пустая сетка Soulmate Skills»).
//   Причина пустоты: скин `NoImage_icon` имеет Type=Text, поэтому импортёр дал ячейке пару — TextBlock с точным именем `SOUL_TRAYnn` и
//   план-картинку `SOUL_TRAYnn_{обход}_0`. `BindTrayControls` брал TextBlock, `Cast<UImage>` давал пусто, и 12 ячеек оставались без картинок
//   (журнал Player52 05.11: «лоток привязан: ячеек 0/12», затем «занято 9/12»). Без ячеек не работали и щелчки.
//   Заодно поток приведён к `SoulmateUI_HandleEvent`: msg 8/0xA — подсказка (`SkillTooltip_BuildSoulmateTooltip`, для 30/36/41 —
//   `CSoulmateUI_BuildRecordTooltip`); msg 0xB — выбор; msg 0xD — ДВОЙНОЙ щелчок применяет (`uiQuickSlotSkillActivate`), порт применял
//   одиночным отпусканием; перетаскивание на панель (`hotbarAssignQuickSlot@0xE68E00`, Type 4) не было портировано — разрешено всем,
//   кроме 30/36/41, в том числе серым, при `gap_878[47] != −1`. 1 — так; 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarAsdaSoulTrayFlow, TEXT("asda.Soul.TrayFlow"), 1,
	TEXT("Лоток навыков Дружбы: 1 = как эталон (картинки ячеек, подсказка, двойной щелчок, перетаскивание на панель), 0 = прежний порт"),
	ECVF_Default);

bool AsdaSoulTrayFlowOn() { return CVarAsdaSoulTrayFlow.GetValueOnGameThread() != 0; }

void UAsdaSoulmateSkillSubsystem::HideTrayTip()
{
	if (TipSkillId < 0) { return; }
	TipSkillId = -1;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::SoulTray);
	}
}

void UAsdaSoulmateSkillSubsystem::TickTrayTip(int32 Hovered)
{
	const int32 Want = (Hovered >= 0 && HasSoulmate() && DragSkillId < 0) ? TraySkillId[Hovered] : -1;   // эталон: `gap_878[47] != −1`, ничего не тащим
	if (Want == TipSkillId) { return; }
	HideTrayTip();
	if (Want < 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaSkillWindowSubsystem* Sw = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	FString Title, Body;
	if (!Tip || !Sw || !Sw->BuildSoulTrayTooltip(Want, Title, Body)) { return; }
	Tip->ShowTextTip(EAsdaTipOwner::SoulTray, Title, Body, FAsdaTipMetrics());
	TipSkillId = Want;
}

void UAsdaSoulmateSkillSubsystem::BeginTrayDrag(int32 Cell)
{
	const int32 Id = (Cell >= 0 && Cell < kTraySlots) ? TraySkillId[Cell] : -1;
	if (Id < 0 || !HasSoulmate()) { DragArm.Disarm(); return; }
	DragSkillId = Id;
	HideTrayTip();
	UImage* Src = TrayCells.IsValidIndex(Cell) ? TrayCells[Cell].Get() : nullptr;
	UCanvasPanel* Root = (CommWnd && CommWnd->WidgetTree) ? Cast<UCanvasPanel>(CommWnd->WidgetTree->RootWidget) : nullptr;
	if (Src && Root)
	{
		DragGhost.Begin(CommWnd, Root, Src->GetBrush(), FVector2D(32.f, 32.f), 0.4f, FVector2D(-16.f, -16.f), TEXT("SOUL_DragGhost"),
			GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	}
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] захват навыка %d из ячейки %d"), Id, Cell);
}

void UAsdaSoulmateSkillSubsystem::CancelTrayDrag()
{
	DragGhost.Cancel();
	DragSkillId = -1;
	DragArm.Disarm();
}

void UAsdaSoulmateSkillSubsystem::EndTrayDrag()
{
	const int32 Id = DragSkillId;
	CancelTrayDrag();   // призрак снимаем до хит-теста: иначе он сам оказался бы под курсором
	UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	const int32 Abs = Hb ? Hb->HitTestSlotUnderCursor() : -1;
	if (Abs < 0)
	{
		UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] бросок навыка %d мимо панели — ничего не делаем (эталон `if (a2)`)"), Id);
		return;
	}
	Hb->AssignSoulSkillToSlot(Abs, Id);
}

void UAsdaSoulmateSkillSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.SoulmateSkill");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Протухшие кулдауны убираем всегда — их читают панель быстрого доступа и тултип.
	const double Now = FPlatformTime::Seconds();
	for (int32 i = Cooldowns.Num() - 1; i >= 0; --i)
	{
		if (Cooldowns[i].EndSeconds <= Now) { Cooldowns.RemoveAtSwap(i); }
	}

	if (!bTrayVisible || !bTrayBound)
	{
		HideTrayTip();
		if (DragSkillId >= 0) { CancelTrayDrag(); }
		return;
	}

	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал, — здесь это был бы ПРИМЕНЁННЫЙ навык соулмейта.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (bPickArmed) { bPickArmed = false; PickedSkillId = -1; SetTraySelection(-1); }
		Mouse.Reset();
		HideTrayTip();
		if (DragSkillId >= 0) { CancelTrayDrag(); }
		return;
	}
	const bool bFlow = AsdaSoulTrayFlowOn();
	if (!bFlow && !Mouse.bLmbDown && !Mouse.bLmbUp) { return; }

	int32 Hovered = -1;
	for (int32 i = 0; i < kTraySlots; ++i)
	{
		if (TraySkillId[i] < 0) { continue; }
		const UImage* Cell = TrayCells.IsValidIndex(i) ? TrayCells[i].Get() : nullptr;
		if (Cell && AsdaUi::IsCursorOverWidget(Cell)) { Hovered = i; break; }
	}

	if (bFlow)
	{
		// Окно 6400 под другим окном ячейки не получает: и подсказка, и нажатие — только верхнему окну.
		UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (Hovered >= 0 && WM && WM->FindWindowCatchingAt(Mouse.Cursor) != FName(TEXT("COMMUNITY_WIN"))) { Hovered = -1; }
		TickTrayTip(Hovered);
		if (Mouse.bLmbDown)
		{
			const double NowS = FPlatformTime::Seconds();
			const bool bDouble = Hovered >= 0 && Hovered == LastDownCell && (NowS - LastDownTimeS) <= FAsdaDblClick::WindowSec();
			LastDownCell = bDouble ? -1 : Hovered;
			LastDownTimeS = NowS;
			if (Hovered < 0 || !HasSoulmate())
			{
				bPickArmed = false;
				PickedSkillId = -1;
				SetTraySelection(-1);
				return;
			}
			const int32 Id = TraySkillId[Hovered];
			if (bDouble && bPickArmed && PickedSkillId == Id) { UseSkill(Id); }   // msg 0xD `uiQuickSlotSkillActivate`
			PickedSkillId = Id;
			bPickArmed = true;
			SetTraySelection(Hovered);
			if (Id != 30 && Id != 36 && Id != 41) { DragArm.Arm(Hovered, -1, Mouse.Cursor); }   // 30/36/41 на панель не кладутся
			return;
		}
		if (Mouse.bLmb && DragSkillId < 0 && DragArm.ShouldStart(Mouse.Cursor))
		{
			BeginTrayDrag(DragArm.KeyA);
			DragArm.MarkStarted();
		}
		if (DragSkillId >= 0) { DragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr); }
		if (Mouse.bLmbUp)
		{
			if (DragSkillId >= 0) { EndTrayDrag(); }
			DragArm.Disarm();
		}
		return;
	}

	if (Mouse.bLmbDown)
	{
		// Гейт эталона: без соулмейта выбор не взводится вовсе.
		if (Hovered < 0 || !HasSoulmate())
		{
			bPickArmed = false;
			PickedSkillId = -1;
			SetTraySelection(-1);
			return;
		}
		PickedSkillId = TraySkillId[Hovered];
		bPickArmed = true;
		SetTraySelection(Hovered);
		return;
	}

	// Отпускание: применяем, только если отпустили НАД ТОЙ ЖЕ ячейкой (эталонная сверка id).
	if (bPickArmed && Hovered >= 0 && TraySkillId[Hovered] == PickedSkillId && PickedSkillId >= 0)
	{
		UseSkill(PickedSkillId);
	}
	bPickArmed = false;
}

// ═══════════════════════════════ ПРИМЕНЕНИЕ НАВЫКА ═════════════════════════════════════════════

bool UAsdaSoulmateSkillSubsystem::UseSkill(int32 SkillId)
{
	if (SkillId < kFirstSkillId || SkillId > kLastSkillId)
	{
		UE_LOG(LogAsdaSoulSkill, Warning, TEXT("[навыки Дружбы] id %d вне диапазона 30..44 — не применяем"), SkillId);
		return false;
	}
	if (Skills.Num() == 0)
	{
		PostTip(kLocOwnNoTable, FString());   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaSkillUseSubsystem* Use = GI ? GI->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
	if (!Use) { return false; }

	// ★ Отправку и гейты состояния персонажа держит уже портированная общая точка
	//   `CastSoulmateSkill` (порт `skillCastActivate@0xE4D900` → 5231). Своей копии не заводим.
	const bool bOk = Use->CastSoulmateSkill(SkillId);
	if (bOk) { ++Tx5231; }
	return bOk;
}

// ═══════════════════════════════ КУЛДАУНЫ ══════════════════════════════════════════════════════

int32 UAsdaSoulmateSkillSubsystem::GetCooldownRemainMs(int32 SkillId) const
{
	const double Now = FPlatformTime::Seconds();
	for (const FAsdaSoulSkillCooldown& C : Cooldowns)
	{
		if (C.SkillId == SkillId)
		{
			const double Rem = (C.EndSeconds - Now) * 1000.0;
			return (Rem > 0.0) ? (int32)Rem : 0;
		}
	}
	return 0;
}

// Порт `Soulmate_StartSkillCooldown@0xE48E70` (social.c:5847-5885): гейты — навык есть в таблице
//   и `record[46] > 0`; далее запись в вектор кулдаунов и обновление слотов панели по ссылке
//   типа 1 (навык) с itemRef 0xFFFF («любой слот»).
void UAsdaSoulmateSkillSubsystem::StartCooldown(int32 SkillId)
{
	const FAsdaSoulmateskillRow* R = GetSkill(SkillId);
	if (!R || R->CooldownMs <= 0) { return; }

	const double End = FPlatformTime::Seconds() + (double)R->CooldownMs / 1000.0;
	bool bReplaced = false;
	for (FAsdaSoulSkillCooldown& C : Cooldowns)
	{
		if (C.SkillId == SkillId) { C.EndSeconds = End; C.TotalMs = (float)R->CooldownMs; bReplaced = true; break; }
	}
	if (!bReplaced)
	{
		FAsdaSoulSkillCooldown C;
		C.SkillId = SkillId;
		C.TotalMs = (float)R->CooldownMs;
		C.EndSeconds = End;
		Cooldowns.Add(C);
	}
	// Шторка на слотах панели быстрого доступа: слоты соулмейта имеют ТИП 4 и ключуются id навыка.
	//   Реестр откатов один на порт — `UAsdaHotbarSubsystem` (закон общих точек).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			Hb->PushSkillCooldown(SkillId, R->CooldownMs);
		}
	}
}

// ═══════════════════════════════ 5232 «состояние применения» ═══════════════════════════════════

// Таблица кодов — см. шапку файла. Возвращает 0, если нотиса у кода нет.
int32 UAsdaSoulmateSkillSubsystem::LocIdForState5232(int32 Code, int32 SkillId)
{
	switch (Code)
	{
	case 0:  return 8100;
	case 1:  return 0;      // успех — нотиса нет
	case 2:  return 8127;
	case 3:  return 8101;
	case 4:  return 8102;
	case 5:  return 8103;
	case 6:  return 8104;
	case 7:  return 8105;
	case 8:  return 8106;
	case 9:  return 8107;
	case 10: return 8108;
	case 11: return 8109;
	case 12: return 8110;
	case 13: return 8111;
	// ★ Единственный код с развилкой по навыку: `if (*(WORD*)(pPacket+3) == 39) loc 2203`
	//   (network.c:45960-45964). Навык 39 — «Whisper of Resurrection».
	case 14: return (SkillId == 39) ? 2203 : 8112;
	case 15: return 8113;
	case 16: return 8114;
	case 17: return 8115;
	case 18: return 0;      // ★ ДЕФЕКТ D4: ветки в клиенте НЕТ (между 0x11 и 0x13)
	case 19: return 8117;
	case 20: return 8118;
	case 21: return 8119;
	case 22: return 8120;
	case 23: return 8121;
	case 24: return 8122;
	case 25: return 8123;
	case 26: return 8124;
	case 27: return 8125;
	case 28: return 8126;
	case 29: return 8165;
	case 30: return 8166;
	case 31: return 2039;
	case 32: return 8168;
	case 33: return 8169;
	case 34: return 8171;
	case 35: return 8173;
	default: return 0;
	}
}

void UAsdaSoulmateSkillSubsystem::OnSkillState5232(const TArray<uint8>& Body)
{
	if (Body.Num() < 197) { return; }
	++Rx5232;
	const int32 Code        = (int32)Body[0];                    // @0  u8  код состояния/ошибки
	const int32 CasterUidx  = AsdaBytes::RdI16(&Body[1]);        // @1  i16 userNum кастующего
	const int32 SkillId     = AsdaBytes::RdI16(&Body[3]);        // @3  i16 id навыка Дружбы

	if (Code == 1)
	{
		// Успех: кулдаун себе; партнёру — только если `record[85] == 1` (network.c:45518-45519).
		//   ★ У порта «кулдаун партнёру» = тот же локальный реестр: своего экземпляра состояния
		//     партнёра клиент не держит, а эталон в этой ветке зовёт тот же `Soulmate_StartSkillCooldown`.
		const UAsdaNetSubsystem* N = Net();
		const bool bMine = N && AsdaSelf::IsSelfUserIdx(N, CasterUidx);
		const FAsdaSoulmateskillRow* R = GetSkill(SkillId);
		// ⚠ ОТСТУПЛЕНИЕ, ПОМЕЧЕНО. У эталона ветка партнёра дополнительно требует, чтобы
		//   кастующий был ИМЕННО МОИМ соулмейтом: `*((DWORD*)pCaster + 557) == myUserIdx`
		//   (network.c:45511-45519). Поля +557 (userIdx соулмейта чужого персонажа) на этом
		//   слое нет — оно живёт в мире; поэтому кулдаун партнёра ставится по одному лишь
		//   `record[85] == 1`. Для чужой пары рядом это даст лишний локальный кулдаун.
		if (bMine || (R && R->PartnerCooldown == 1)) { StartCooldown(SkillId); }

		ApplyCombatBlock(CasterUidx, SkillId, Body, /*BlockOffset*/5, /*bFlagsBFromBlockA*/false);
		UE_LOG(LogAsdaSoulSkill, Log,
			TEXT("[навыки Дружбы] 5232 успех: навык %d от userNum %d%s"),
			SkillId, CasterUidx, bMine ? TEXT(" (свой)") : TEXT(""));
		return;
	}

	// ⚠ КОД 0 УСЛОВНЫЙ. Эталон (network.c:45484-45499) сначала ищет кастующего по userNum;
	//   строка 8100 печатается ТОЛЬКО если персонаж найден, у него взведён `CClass_GetField47565`
	//   И `effectSlots[112].field_C & 6 == 0`; в противном случае он молча ставит состояние 20.
	//   Ни признака +47565, ни поля состояния у порта на этом слое нет ⇒ печатаем безусловно и
	//   помечаем: это отступление, а не «как в оригинале». Постановки состояния 20 тут тоже нет
	//   (её ставит боевой слой) — см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА».
	const int32 LocId = LocIdForState5232(Code, SkillId);
	if (LocId != 0)
	{
		PostTip(LocId, FString());
	}
	else if (Code == 18 && CVarFixSilentCodes.GetValueOnGameThread() != 0)
	{
		// ★ Исправление дефекта D4 — только под ключом, по умолчанию молчим, как оригинал.
		PostTip(kLocOwnSilent5232, FString());   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
	}
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] 5232 код %d (навык %d, кастующий %d) → loc %d"),
		Code, SkillId, CasterUidx, LocId);
}

// ═══════════════════════════════ 5233 «навык применён» ═════════════════════════════════════════

void UAsdaSoulmateSkillSubsystem::OnSkillApply5233(const TArray<uint8>& Body)
{
	if (Body.Num() < 196) { return; }
	++Rx5233;
	const int32 TargetUidx = AsdaBytes::RdI16(&Body[0]);   // @0 i16 userNum получателя
	const int32 SkillId    = AsdaBytes::RdI16(&Body[2]);   // @2 i16 id навыка
	// ★ ДЕФЕКТ D2: эталон здесь читает флаги половины B по blk+62 (блок A) вместо blk+174.
	const bool bFlagsBFromA = (CVarFix5233FlagsB.GetValueOnGameThread() == 0);
	ApplyCombatBlock(TargetUidx, SkillId, Body, /*BlockOffset*/4, bFlagsBFromA);

	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] 5233: навык %d на userNum %d (флаги B из блока %s)"),
		SkillId, TargetUidx, bFlagsBFromA ? TEXT("A — дефект D2 воспроизведён") : TEXT("B — исправлено"));
}

// ═══════════════════════════════ ОБЩИЙ БОЕВОЙ БЛОК 192 б ═══════════════════════════════════════
//
// Раскладка (одна и та же у 5231/5232/5233, отчёт §A.2):
//   blk+0   i16      основная цель A (renderId моба)      blk+2   i16[10] targetsA
//   blk+22  i32[10]  damagesA                             blk+62  u8[10]  flagsA
//   blk+72  i32[10]  значения удара/лечения A             blk+112 i16     основная цель B (userNum)
//   blk+114 i16[10]  targetsB                             blk+134 i32[10] damagesB
//   blk+174 u8[10]   flagsB                               blk+184 8 б     хвост
//
// ★ ДЕФЕКТ D1 ИСПРАВЛЕН КОНСТРУКТИВНО: эталон читал значения циклом `for (i = 1; i < 20; ++i)` по
//   `pPacket + 4*i + 77` — двадцать целей при массивах по десять, и при i ≥ 10 чтение уходило в
//   поля половины B. Здесь границы массивов заданы явно (10), выхода за них нет физически.
//
// ⚠ ЧТО ЭТОТ РАЗБОР ДЕЛАЕТ, А ЧЕГО НЕ ДЕЛАЕТ. Урон/лечение по целям в порту раздаёт единый
//   боевой канал (`FOnAsdaCombatHit` в `UAsdaNetSubsystem` + мир), и второй раздатчик завёл бы
//   ДВОЙНОЙ урон. Поэтому здесь блок только РАЗБИРАЕТСЯ и уходит в журнал/показ навыка; чисел в
//   витальные полосы эта подсистема не пишет.
void UAsdaSoulmateSkillSubsystem::ApplyCombatBlock(int32 CasterUserIdx, int32 SkillId,
	const TArray<uint8>& Body, int32 BlockOffset, bool bFlagsBFromBlockA)
{
	const int32 Need = BlockOffset + 192;
	if (Body.Num() < Need) { return; }
	const uint8* Blk = Body.GetData() + BlockOffset;

	const int32 MainA = AsdaBytes::RdI16(&Blk[0]);
	const int32 MainB = AsdaBytes::RdI16(&Blk[112]);
	const int32 FlagsBOfs = bFlagsBFromBlockA ? 62 : 174;

	int32 CountA = 0, CountB = 0;
	for (int32 i = 0; i < 10; ++i)
	{
		if (AsdaBytes::RdI16(&Blk[2   + 2 * i]) != -1) { ++CountA; }
		if (AsdaBytes::RdI16(&Blk[114 + 2 * i]) != -1) { ++CountB; }
	}

	// Ветвление эталона (network.c:45530-45700): какая половина заполнена.
	//   1) A пуста → работает только половина B (цели-игроки);
	//   2) B пуста → только половина A (мобы);
	//   3) иначе → обе, при `record[50] != 4`.
	const bool bOnlyB = (MainA == -1) && (AsdaBytes::RdI16(&Blk[2]) == -1);
	const bool bOnlyA = (MainB == -1) && (AsdaBytes::RdI16(&Blk[114]) == -1);

	// Показ навыка соулмейта у КАСТУЮЩЕГО — тот же четвёртый канал эффектов, что заводит
	//   `UAsdaSkillUseSubsystem::CastSoulmateSkill` локально; здесь он нужен для СОСЕДЕЙ.
	//   ★ На карте типа 6 эталон визуал не проигрывает (`Net_GetCurrentMapType == 6`,
	//     network.c:45521) — тип карты у порта живёт в мире, поэтому решение принимает канал показа,
	//     а не эта подсистема (здесь дублировать проверку нельзя, иначе появится второй источник истины).
	UE_LOG(LogAsdaSoulSkill, Verbose,
		TEXT("[навыки Дружбы] блок навыка %d от %d: цельA=%d (списком %d), цельB=%d (списком %d), ")
		TEXT("флаги B со смещения blk+%d, половина: %s"),
		SkillId, CasterUserIdx, MainA, CountA, MainB, CountB, FlagsBOfs,
		bOnlyB ? TEXT("только B") : bOnlyA ? TEXT("только A") : TEXT("обе"));
}

// ═══════════════════════════════ 2247 «таблица кулдаунов» ══════════════════════════════════════
//
// 246 б: @0 i16 userNum · @2 i32 charId · далее 30 пар {@6+8i i32 id навыка, @10+8i i32 остаток мс}.
//   Терминатор списка — id == −1. Сервер только пересылает готовый ответ DBAgent (опкод 0x8C7);
//   сама таблица `SMSkillCoolTime` живёт в `DBManager.exe` (отчёт §A.5).
void UAsdaSoulmateSkillSubsystem::OnSkillCooldowns2247(const TArray<uint8>& Body)
{
	if (Body.Num() < 246) { return; }
	++Rx2247;
	const int32 UserNum = AsdaBytes::RdI16(&Body[0]);
	const int32 CharId  = AsdaBytes::RdI32(&Body[2]);

	// Гейт эталона: пакет адресован персонажу, найденному по userNum; если это не мы — нотис 12846
	//   в ЧАТ оранжевым (network.c:28894). Проверяем ТОЛЬКО через IsSelfUserIdx (памятка про userNum 0).
	const UAsdaNetSubsystem* N = Net();
	if (N && N->IsSelfUserNumKnown() && !AsdaSelf::IsSelfUserIdx(N, UserNum))
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>())
			{
				FAsdaMsgParams P;
				P.Category  = EAsdaMsgCategory::Social;
				P.LocId     = 12846;
				P.bUseColor = true;
				P.Color     = kChatOrange;      // эталонный −21248
				P.TtlMs     = 10000.f;
				P.SrcOpcode = 2247;
				Msg->Post(P);
			}
		}
		UE_LOG(LogAsdaSoulSkill, Warning,
			TEXT("[навыки Дружбы] 2247 адресован userNum %d (свой %d) — loc 12846"), UserNum, (int32)N->SelfUserNum);
		return;
	}
	// Сверка синхронизации charId (сервер делает ровно её: `*result->gap_16B == *&v7[1]`).
	if (N && N->SelectedCharId != 0 && CharId != N->SelectedCharId)
	{
		UE_LOG(LogAsdaSoulSkill, Warning,
			TEXT("[навыки Дружбы] 2247: charId пакета %d ≠ свой %d — таблица кулдаунов НЕ применена"),
			CharId, N->SelectedCharId);
		return;
	}

	Cooldowns.Reset();
	int32 Applied = 0;
	for (int32 i = 0; i < 30; ++i)
	{
		const int32 Id     = AsdaBytes::RdI32(&Body[6  + 8 * i]);
		const int32 Remain = AsdaBytes::RdI32(&Body[10 + 8 * i]);
		if (Id < 0) { break; }                                   // терминатор −1
		const FAsdaSoulmateskillRow* R = GetSkill(Id);
		if (!R || Remain <= 0) { continue; }

		FAsdaSoulSkillCooldown C;
		C.SkillId    = Id;
		C.TotalMs    = (float)R->CooldownMs;                     // полный КД берётся из ТАБЛИЦЫ (rec+46)
		C.EndSeconds = FPlatformTime::Seconds() + (double)Remain / 1000.0;
		Cooldowns.Add(C);
		++Applied;

		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
			{
				Hb->PushSkillCooldown(Id, Remain);
			}
		}
	}
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] 2247: применено %d кулдаунов (charId %d)"), Applied, CharId);
}

// ═══════════════════════════════ ЗОВ ДРУГА: 5236 → вопрос ══════════════════════════════════════

void UAsdaSoulmateSkillSubsystem::OnMateRecall5236(const TArray<uint8>& Body)
{
	if (Body.Num() < 42) { return; }
	++Rx5236;
	const uint8* B = Body.GetData();

	Recall = FAsdaMateRecallRequest();
	Recall.bPending      = true;
	Recall.InitCharId    = AsdaBytes::RdI32(&B[0]);
	Recall.InitUserNum   = AsdaBytes::RdI16(&B[4]);
	Recall.InitWorld     = B[6];
	Recall.InitMap       = B[7];
	Recall.InitZone      = B[8];
	Recall.InitChannel   = B[9];
	// @10 char[16] — IP канального сервера; строка ASCII, обрезаем по первому NUL.
	{
		int32 Len = 0;
		while (Len < 16 && B[10 + Len] != 0) { ++Len; }
		Recall.ServerIp = FString(Len, reinterpret_cast<const ANSICHAR*>(&B[10]));
	}
	Recall.ServerPort    = AsdaBytes::RdI16(&B[26]);
	Recall.TileX         = AsdaBytes::RdI16(&B[28]);
	Recall.TileZ         = AsdaBytes::RdI16(&B[30]);
	Recall.TargetCharId  = AsdaBytes::RdI32(&B[32]);
	Recall.TargetUserNum = AsdaBytes::RdI16(&B[36]);
	Recall.TargetB38     = B[38];
	Recall.TargetMap     = B[39];
	Recall.TargetB40     = B[40];
	Recall.TargetChannel = B[41];
	// ★ МИСНЕЙМ эталона `g_bUpgradeResultGradeChanged` — это «зов ведёт на другую карту/канал».
	Recall.bOtherMapOrChannel = (B[7] != B[39]) || (B[41] != B[9]);

	// Окно-вопрос: эталон `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 38, loc(8163), 1)` —
	//   двухкнопочная модалка типа 38. У порта это `ShowGameConfirm` (TWOBUT_MSGBOX 1520);
	//   подписи кнопок — общие 2288 «ОК» / 2289 «Отмена», своих эталон не ставит.
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* Win = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FString Question = Loc(8163);
	if (Win && !Question.IsEmpty())
	{
		TWeakObjectPtr<UAsdaSoulmateSkillSubsystem> Weak(this);
		// ★ Редакция 1 (ревизия С-2): дубль (1, 38) возвращает nullptr — прежний указатель не затираем.
		if (UUserWidget* Box = AsdaEnterGuard::NoEnter(Win)->ShowGameConfirm(Question,
			[Weak](bool bYes) { if (Weak.IsValid()) { Weak->AnswerMateRecall(bYes); } },
			/*GaugeGateSec*/0.f, 2288, 2289, /*очередь: эталон (1, 38) network.c:47173*/38, 1))
		{
			RecallBox = Box;
		}
		else if (Win->IsMsgBoxQueued(1, 38))
		{
			UE_LOG(LogAsdaSoulSkill, Log, TEXT("[зов друга] вопрос (1, 38) уже в очереди — повторный показ отброшен, как эталон"));
		}
	}
	else
	{
		// Окна нет — игрок обязан увидеть зов хотя бы строкой, иначе он «зависнет» без ответа.
		PostTip(8163, FString());   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		UE_LOG(LogAsdaSoulSkill, Warning, TEXT("[зов друга] нет окна подтверждения — вопрос ушёл строкой"));
	}

	UE_LOG(LogAsdaSoulSkill, Log,
		TEXT("[зов друга] 5236: инициатор charId=%d userNum=%d, карта %d→%d, канал %d→%d, плитка (%d,%d)%s"),
		Recall.InitCharId, Recall.InitUserNum, Recall.InitMap, Recall.TargetMap,
		Recall.InitChannel, Recall.TargetChannel, Recall.TileX, Recall.TileZ,
		Recall.bOtherMapOrChannel ? TEXT(" — ДРУГАЯ карта/канал") : TEXT(""));
}

// Ответ игрока (ui_core.c:57404-57417 «Да» и :57711-57722 / :58895-58920 «Нет»).
void UAsdaSoulmateSkillSubsystem::AnswerMateRecall(bool bYes)
{
	if (!Recall.bPending)
	{
		UE_LOG(LogAsdaSoulSkill, Warning, TEXT("[зов друга] ответ без активного зова — игнор"));
		return;
	}
	UAsdaNetSubsystem* N = Net();
	if (!N) { Recall.bPending = false; return; }

	if (!bYes)
	{
		// «Нет» → 5237 код 2. Это ЕДИНСТВЕННЫЙ код, который шлёт эталонный клиент.
		N->SendMateRecallAnswer5237(2, Recall.InitUserNum);
		UE_LOG(LogAsdaSoulSkill, Log, TEXT("[зов друга] отказ → 5237 код 2 (инициатор userNum=%d)"),
			Recall.InitUserNum);
		Recall.bPending = false;
		return;
	}

	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): «Да» (`OnSendItemUpgrade` case 0x26, ui_core.c:57404-57417) в конце
	//   зовёт `uiCloseAllWindowsOnLeave@0xD62790` при ЛЮБОМ маршруте; её хвост (ui_core.c:54737-54740) закрывает окно
	//   ремесла 76000 (`uiWindowHideAndCleanup`) и сбрасывает 77500. Перенесена ТОЛЬКО эта часть — прочие строки
	//   функции (сумка, барабан, квест-гид, магазины, персонаж, обмен, парикмахерская, NPC, сообщество, меню) в порту
	//   не перенесены: строка реестра отложенного ремесла В3.
	if (UGameInstance* GIc = GetGameInstance())
	{
		if (UAsdaCraftWindowSubsystem* CraftWin = GIc->GetSubsystem<UAsdaCraftWindowSubsystem>())
		{
			if (CraftWin->IsOpen())
			{
				CraftWin->HideAndCleanup();
				UE_LOG(LogAsdaSoulSkill, Log, TEXT("[зов друга] согласие: окно ремесла 76000 закрыто (uiCloseAllWindowsOnLeave, ui_core.c:54737)"));
			}
		}
		// ★ 29.09.2026 (волна 0 торговли): строка того же `uiCloseAllWindowsOnLeave` (ui_core.c:54726-54730) — при ОТКРЫТОМ
		//   окне обмена 2008 (`g_bPartyInviteFlag` = `CExchange+0x2C`) `CExchange_SendWindowCancel` + `uiInventoryDialogReset`.
		//   Общая точка `UAsdaTradeStateSubsystem::OnCloseAllWindowsOnLeave` (отправитель выхода — волна О1; пока окна нет — no-op).
		if (UAsdaTradeStateSubsystem* Trade = GIc->GetSubsystem<UAsdaTradeStateSubsystem>())
		{
			Trade->OnCloseAllWindowsOnLeave(TEXT("зов друга: согласие (uiCloseAllWindowsOnLeave, ui_core.c:54726)"));
		}
		// ★ Волна В4 ремесла: вторая строка того же хвоста (ui_core.c:54739-54740) — `if (isBoundControlVisible(g_pItemQtyMakeDlg))
		//   itemQtyDlgReset` — окно изготовления 77500. (Ревизия В3 МЕЛК-12: у эталона хвост идёт ПОСЛЕ отправки 5427 —
		//   порядок здесь другой, на поведение не влияет: сброс окон пакетов не шлёт.)
		if (UAsdaCraftMakeWindowSubsystem* MakeWin = GIc->GetSubsystem<UAsdaCraftMakeWindowSubsystem>())
		{
			if (MakeWin->IsOpen())
			{
				MakeWin->ResetDialog(TEXT("зов друга: согласие (uiCloseAllWindowsOnLeave, ui_core.c:54739)"));
			}
		}
	}

	if (Recall.bOtherMapOrChannel)
	{
		// «Да» на другой карте/канале: эталон ПАКЕТ НЕ ШЛЁТ — он сам инициирует переход канала
		//   по полям @10 (IP) / @26 (порт) / @28,@30 (плитка) через `netHandleChannelPortal`.
		//   ⚠ Перехода канала у порта пока нет отдельной точкой — см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА».
		//     Выдумывать свой переход здесь нельзя: это чужая подсистема соединения.
		UE_LOG(LogAsdaSoulSkill, Warning,
			TEXT("[зов друга] согласие ведёт на ДРУГУЮ карту/канал (%s:%d, плитка %d,%d) — ")
			TEXT("эталон здесь пакета не шлёт, а меняет канал; точки перехода в порту нет"),
			*Recall.ServerIp, Recall.ServerPort, Recall.TileX, Recall.TileZ);
		Recall.bPending = false;
		return;
	}

	// «Да» в пределах той же карты и канала → 5427 с плиточными координатами инициатора.
	N->SendMateRecallPosition5427(Recall.TileX, Recall.TileZ);
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[зов друга] согласие → 5427 (плитка %d,%d)"),
		Recall.TileX, Recall.TileZ);
	Recall.bPending = false;
}

int32 UAsdaSoulmateSkillSubsystem::LocIdForRecall5237(int32 Code)
{
	switch (Code)
	{
	case 0:  return 9576;    // рассинхрон charId цели
	case 1:  return 0;       // согласие — нотиса нет
	case 2:  return 9577;    // цель отказалась
	case 3:  return 9584;    // другой канал и цель не в группе
	case 4:  return 13920;   // запрещённый тип карты инициатора
	case 5:  return 13928;   // цель занята (sub_4CB030)
	default: return 0;
	}
}

// Приём 5237 (S→C, 12 б) — итог зова у ИНИЦИАТОРА. Порт `Recv_CharStateNotice@0xF548D0`
//   (network.c:47178-47245). Делегата под этот опкод контракт сети не даёт, поэтому точка
//   публичная: `UAsdaNetSubsystem` должен позвать её из своего `case` (см. отчёт оркестратору).
void UAsdaSoulmateSkillSubsystem::OnMateRecallAnswer5237(const TArray<uint8>& Body)
{
	if (Body.Num() < 12) { return; }
	++Rx5237;
	const int32 Code = (int32)Body[0];
	const int32 LocId = LocIdForRecall5237(Code);
	if (LocId != 0) { PostTip(LocId, FString()); }
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[зов друга] 5237 код %d → loc %d (инициатор charId=%d userNum=%d)"),
		Code, LocId, AsdaBytes::RdI32(&Body[1]), AsdaBytes::RdI16(&Body[5]));
}

// ═══════════════════════════════ СБРОС ПРИ РАЗРЫВЕ ДРУЖБЫ ══════════════════════════════════════

// Порт social.c:7806-7817 (ветка 5207 код 3): чистка флагов 30..44, всех 12 ячеек лотка,
//   рамки выбора, слотов панели типа 4 и пересборка панели.
void UAsdaSoulmateSkillSubsystem::ResetTrayAndHotbar()
{
	ClearTrayLit();   // 1:1 `skillHotbarClearAutoSlots`: гасим k = 30..44 в блоке 5228/5238
	Cooldowns.Reset();
	for (int32 i = 0; i < kTraySlots; ++i)
	{
		TraySkillId[i] = -1;
		if (UImage* Cell = TrayCells.IsValidIndex(i) ? TrayCells[i].Get() : nullptr)
		{
			AsdaUi::ClearImageBrush(Cell);
			Cell->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (TraySel) { TraySel->SetVisibility(ESlateVisibility::Collapsed); }
	bPickArmed = false;
	PickedSkillId = -1;

	// Слоты панели быстрого доступа ТИПА 4 (`hotbarClearType4Slots` + `hotbarRebuildSlots`
	//   эталона, game.c:473377-473378) снимает `UAsdaSoulmateSubsystem::ResetSkillTrayAndHotbar`,
	//   которая и зовёт эту функцию из ветки 5207 кода 3. Здесь их трогать нельзя — иначе
	//   пересборка панели пойдёт дважды. ⚠ Прежний комментарий «публичной очистки типа 4 у
	//   панели нет» устарел: `UAsdaHotbarSubsystem::GetSlot/ClearSlot/RebuildSlots` есть.
	UE_LOG(LogAsdaSoulSkill, Log, TEXT("[навыки Дружбы] лоток и кулдауны сброшены (разрыв Дружбы)"));
}

// ═══════════════════════════════ НОТИСЫ И УТИЛИТЫ ══════════════════════════════════════════════

// Канал «подсказка» эталона: `CControl_SetTooltipText(g_pChatWnd, текст, −11514, 10000)`.
//   У порта приёмник выбирает `DT_MessageRoutes` по категории; цвет и TTL передаём эталонные.
void UAsdaSoulmateSkillSubsystem::PostTip(int32 LocId, const FString& Fallback) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg) { return; }
	FAsdaMsgParams P;
	P.Category  = EAsdaMsgCategory::SkillRefused;
	P.LocId     = LocId;
	P.Fallback  = Fallback;
	P.bUseColor = true;
	P.Color     = kTipYellow;      // эталонный −11514
	P.TtlMs     = 10000.f;
	P.SrcOpcode = 5232;
	Msg->Post(P);
}

FString UAsdaSoulmateSkillSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

FString UAsdaSoulmateSkillSubsystem::DumpState() const
{
	FString S = TEXT("══ НАВЫКИ ДРУЖБЫ И ЗОВ ДРУГА ══\n");
	S += FString::Printf(TEXT("  таблицы: навыков %d, текстов %d (%s / %s)\n"),
		Skills.Num(), Texts.Num(), kSkillTablePath, kTextTablePath);
	S += FString::Printf(TEXT("  сеть: %s · принято 5232=%d 5233=%d 2247=%d 5236=%d 5237=%d 5228/5238=%d · отправлено 5231=%d\n"),
		bNetBound ? TEXT("привязана") : TEXT("НЕТ"), Rx5232, Rx5233, Rx2247, Rx5236, Rx5237, RxTrayLit, Tx5231);
	// Подсветка иконок — из блока 5228/5238 (ключа-костыля `asda.Soul.TrayLit` больше нет).
	int32 LitNow = 0;
	for (int32 k = kFirstSkillId; k <= kLastSkillId; ++k) { if (IsSkillLit(k)) { ++LitNow; } }
	S += FString::Printf(TEXT("  лоток: %s, соулмейт %s, уровень %d, зажжено навыков %d/%d\n"),
		bTrayBound ? (bTrayVisible ? TEXT("привязан и видим") : TEXT("привязан, скрыт")) : TEXT("не привязан"),
		HasSoulmate() ? TEXT("есть") : TEXT("нет"), SelfLevel(), LitNow, kSkillCount);
	for (int32 i = 0; i < kTraySlots; ++i)
	{
		if (TraySkillId[i] < 0) { continue; }
		const FAsdaSoulmateskilltableRow* T = GetSkillText(TraySkillId[i]);
		const FAsdaSoulmateskillRow*     R = GetSkill(TraySkillId[i]);
		S += FString::Printf(TEXT("    ячейка %2d → навык %d «%s» (иконка %s, ур. %d, группа %d, КД %d мс, осталось %d мс)\n"),
			i, TraySkillId[i], T ? *T->DisplayName : TEXT("?"), T ? *T->SkillName : TEXT("?"),
			R ? R->ReqLevel : -1, R ? R->GroupId : -1, R ? R->CooldownMs : 0,
			GetCooldownRemainMs(TraySkillId[i]));
	}
	S += FString::Printf(TEXT("  зов друга: %s"), Recall.bPending ? TEXT("ЖДЁТ ОТВЕТА") : TEXT("нет"));
	if (Recall.bPending)
	{
		S += FString::Printf(TEXT(" (инициатор charId=%d userNum=%d, плитка %d,%d, %s)"),
			Recall.InitCharId, Recall.InitUserNum, Recall.TileX, Recall.TileZ,
			Recall.bOtherMapOrChannel ? TEXT("другая карта/канал") : TEXT("та же карта"));
	}
	S += TEXT("\n");
	return S;
}

// ── Консольная проверка живьём (закон «диагностика реальным путём») ────────────────────────────
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulSkillsDump(
	TEXT("asda.Soul.Skills.Dump"),
	TEXT("Состояние навыков Дружбы: таблицы, лоток, кулдауны, активный зов друга."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& /*Args*/, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			if (UAsdaSoulmateSkillSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr)
			{
				UE_LOG(LogAsdaSoulSkill, Log, TEXT("%s"), *S->DumpState());
			}
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulSkillsUse(
	TEXT("asda.Soul.Skills.Use"),
	TEXT("asda.Soul.Skills.Use <id 30..44> — применить навык Дружбы тем же путём, что и клик по лотку."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaSoulmateSkillSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr;
			if (S && Args.Num() >= 1) { S->UseSkill(FCString::Atoi(*Args[0])); }
		}));
#endif // ASDA_DEV_CONSOLE
