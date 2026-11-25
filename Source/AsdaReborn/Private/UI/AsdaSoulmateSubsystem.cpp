// Copyright (c) 2026 @sitxovski (Discord)
// Порт подсистемы соулмейта. Разбор — `Reports/soulmate_6400.md` §3–§5.
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
#include "UI/AsdaChatWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"   // GetSelfCharName — общая точка «своё имя» (ветка 5203 код 2)
#include "UI/AsdaHotbarSubsystem.h"
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
// DT_smcharexp — таблица опыта Дружбы   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "Items/AsdaItemDbRows.h"            // FAsdaSmcharexpRow
#include "World/AsdaCoord.h"                  // ★ волна 1-А: мир → тайл (`helper_scaleIfNonzero`/`ratioIfFieldSet`) для 9104
#include "UI/AsdaPremiumShopSubsystem.h"     // ★ волна 1-А: гейт показа 8200 — элитный магазин 6000 (`helper_getField2075(g_pShopWnd)`)
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

DEFINE_LOG_CATEGORY(LogAsdaSoulmate);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

const TCHAR* UAsdaSoulmateSubsystem::kLinkWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SOULMATE_LINK_WIN.WBP_SOULMATE_LINK_WIN_C");
const TCHAR* UAsdaSoulmateSubsystem::kReqAnsrWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SMate_ReqAnsr_Win.WBP_SMate_ReqAnsr_Win_C");
const FName  UAsdaSoulmateSubsystem::kLinkWindowId(TEXT("SOULMATE_LINK_WIN"));
const FName  UAsdaSoulmateSubsystem::kReqAnsrWindowId(TEXT("SMate_ReqAnsr_Win"));
const TCHAR* UAsdaSoulmateSubsystem::kFindWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_Find_Soulmate_Win.WBP_Find_Soulmate_Win_C");
const FName  UAsdaSoulmateSubsystem::kFindWindowId(TEXT("Find_Soulmate_Win"));
const TCHAR* UAsdaSoulmateSubsystem::kPopWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SOULMATE_POP_WIN.WBP_SOULMATE_POP_WIN_C");
const FName  UAsdaSoulmateSubsystem::kPopWindowId(TEXT("SOULMATE_POP_WIN"));
const TCHAR* UAsdaSoulmateSubsystem::kIntroWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_My_Introduction_Win.WBP_My_Introduction_Win_C");
const FName  UAsdaSoulmateSubsystem::kIntroWindowId(TEXT("My_Introduction_Win"));

// ★★★ 23.10.2026 ПАНЕЛЬ ДРУГА 6500 («SM bar is not showing up», шапка `AsdaSoulmatePopBar.cpp`).
//   `AsdaSoulmatePopBar.cpp` читает первый через `AsdaSoulmatePopFrom5220On()` (ниже).
ASDA_DEV_CVAR(int32, CVarSoulmatePopFrom5220,
	TEXT("asda.Soulmate.PopFrom5220"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = своя запись 5220 даёт галки 6546/6543..6545 и признак «Друг в игре»; панель Друга 6500\n")
	TEXT("показывается по ней (Recv_PartyMinimapMark / хвост unitFrame_Refresh / sub_E65450) и гасится только inventoryHidePanelsAndRefresh.\n")
	TEXT("0 = прежний порт: 5220 не разбирается, панель только по галке 6546, щёлкнутой в этой сессии."),
	ECVF_Default);

ASDA_DEV_CVAR(int32, CVarSoulmateBarFullFromBack,
	TEXT("asda.Soulmate.BarFullFromBack"), 1,
	TEXT("Полосы соулмейта (6504/6508/6509/6559): 1 (по умолчанию) [ИСПРАВЛЕНИЕ] — 100 % = ширина подложки ES0.\n")
	TEXT("0 = прежний порт: 100 % = ширина плана заливки из разметки (HP/MP упирались в 2/3)."),
	ECVF_Default);
// Значение `asda.Soulmate.PopFrom5220` для `AsdaSoulmatePopBar.cpp` (ключ живёт здесь — его читают оба файла).
bool AsdaSoulmatePopFrom5220On() { return CVarSoulmatePopFrom5220.GetValueOnGameThread() != 0; }

// ★★★ 23.10.2026 ВОЛНА «СВЕДЕНИЯ ДРУГА / SHARE / ТОЧКА НА КАРТЕ» (баги владельца 23.10: «Can't see soulmate info while he or
//   she is offline», «"share" flag is dropping», розовая точка Друга на картах). Разбор — ответ агента sm-info (этап 1),
//   серверная половина share — P78 (`AstoryServer/Docs/DBManager/soulmate-share-slots-p78.md`).
//   [ОРИГИНАЛ] Вкладку соулмейта перерисовывают ДВЕ функции эталона: `CSoulmateWnd_RefreshInfo` (5110/5111, Друг в игре) и
//   `itemTooltip_RefreshControls@0xE62710` (МИСНЕЙМ; своя 5220, 5226, 5207): Друг вне игры → 6548 «Вне игры» (8054), имя/
//   уровень/класс/локация пусты, уровень и опыт Дружбы — из СВОЕГО блока (CCharacter+0x8B3 / +0x8AB, их пишет и 5220).
//   Порт переносил только первую — без Друга в игре вкладка была пустой, а после 5226 показывала «В игре».
ASDA_DEV_CVAR(int32, CVarSoulmateOfflineInfo,
	TEXT("asda.Soulmate.OfflineInfo"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = вкладка соулмейта по своему блоку (itemTooltip_RefreshControls): «Вне игры», уровень и опыт\n")
	TEXT("Дружбы из 5220, ветки 5226/5207 и показ вкладки. 0 = прежний порт: вкладка рисуется только по 5110/5111."),
	ECVF_Default);
// [ИСПРАВЛЕНИЕ порта] 6523 — «Локация» (rec[38] карта, rec[39] канал), порт печатал туда имя КЛАССА по номеру карты.
ASDA_DEV_CVAR(int32, CVarSoulmateLocationText,
	TEXT("asda.Soulmate.LocationText"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = 6523 «Локация»: «  <канал+1> кан. <карта>» (карта ≥ 101 — пусто; канал 6 — loc 13927).\n")
	TEXT("0 = прежний порт: имя класса по rec[38] (мис-нейм)."),
	ECVF_Default);

// ★★★ 24.10.2026 (агент sm-offline, задача владельца «вкладка Друг: вся информация, когда Друг не в сети»). [ИСПРАВЛЕНИЕ]
//   Оригинал при Друге вне игры гасит 6520/6521/6556/6523 (`itemTooltip_RefreshControls@0xE62710`), а 6522 «Последний выход»
//   (подпись 6575 = loc 12103) не пишет НИКОГДА — данных у клиента нет: GameServer о Друге вне игры знает только номер аккаунта.
//   Источник — существующий путь «прежний Друг» 2244 → 2243 (кнопка 6584): серверная процедура P79-SQL `UPCharSoulMateInactive`
//   после стоковых строк дописывает две строки активной пары (SmLvl 254 — персонаж, 253 — карта/канал/время выхода UTC). Провод тот же
//   (243 б = 9 × 27 б). Порт: автозапрос 2244 при показе вкладки и после 5226 (не чаще раза в 30 с — каждый 2244 у сервера +1 к
//   славе 16, `sub_54A050(…, 16)`), заполнение пяти полей. Со стоковой процедурой строк 254/253 нет — вкладка как у оригинала.
ASDA_DEV_CVAR(int32, CVarSoulmateOfflineFriendInfo,
	TEXT("asda.Soulmate.OfflineFriendInfo"), 1,
	TEXT("1 (по умолчанию) [ИСПРАВЛЕНИЕ] = при Друге вне игры вкладка показывает имя, уровень, профессию, локацию и «Последний выход»\n")
	TEXT("последнего персонажа Друга (автозапрос 2244 → строки P79 ответа 2243; не чаще раза в OfflineFriendReqIntervalSec).\n")
	TEXT("0 = как оригинал: поля пусты, автозапроса нет, строки 254/253 ответа 2243 не читаются."),
	ECVF_Default);

ASDA_DEV_CVAR(float, CVarSoulmateOfflineAfterLeave,
	TEXT("asda.Soulmate.OfflineFriendAfterLeaveSec"), 5.f,
	TEXT("Задержка автозапроса 2244 после 5226 «Друг уходит», секунды (запись выхода в БД отстаёт от 5226); по умолчанию 5."),
	ECVF_Default);
// ★ 24.10.2026 (ревизия сведений о Друге вне сети, (а)) [ИСПРАВЛЕНИЕ ПОРТА]: ожидание ответа на кнопку 6584 (`bGridShowPending`) снималось
//   ТОЛЬКО приходом 2243 и переживало смену карты (неполный сброс выходит из `ResetSessionState` рано), хотя ответ на старом соединении уже
//   не придёт; при ошибке БД 2243 не приходит вовсе. Следующий ответ на АВТОзапрос поднимал окно 6640 без нажатия. Теперь ожидание
//   снимается любым сбросом сессии (и смена карты) и истекает через столько секунд.
ASDA_DEV_CVAR(float, CVarSoulmateOldSoulGridReplyTtl,
	TEXT("asda.Soulmate.OldSoulGridReplyTtlSec"), 15.f,
	TEXT("Срок годности ожидания ответа 2243 на кнопку 6584, секунды (по умолчанию 15): позже пришедший 2243 окно прежних Друзей 6640 не\n")
	TEXT("поднимает; ожидание и защёлка автозапроса снимаются и сменой карты. 0 = прежний порт: ожидание снимает только приход 2243."),
	ECVF_Default);
// ★ 24.10.2026 (ревизия сведений о Друге вне сети, (б)) [ИСПРАВЛЕНИЕ ПОРТА]: через OfflineFriendAfterLeaveSec после 5226 БД может ещё не
//   записать выход — ответ 2243 несёт ПРЕЖНИЙ выход, а флаг «устарело» снимался, и нового запроса не было до следующего 5226/5110.
//   Теперь ответ после 5226, чьё время выхода раньше 5226 (с допуском на расхождение часов ПК и сервера) или не изменилось против
//   прежнего ответа, оставляет сведения устаревшими и даёт ОДИН повторный 2244 позже (не раньше предела частоты); второй такой ответ принимается.
ASDA_DEV_CVAR(int32, CVarSoulmateOfflineLeaveRetry,
	TEXT("asda.Soulmate.OfflineLeaveRetry"), 1,
	TEXT("1 (по умолчанию) [ИСПРАВЛЕНИЕ] = ответ 2243 после 5226 с прежним временем выхода (раньше 5226 или как в прошлом ответе) —\n")
	TEXT("поля пусты, один повторный 2244 через предел частоты; 0 = прежний порт: любой ответ принимается сразу."),
	ECVF_Default);

// ★★★ 26.10.2026 [ИСПРАВЛЕНИЕ сервера] «ЭХО СЕБЯ» — баг Player17: панель Друга 6500 показала СВОЁ имя и ур. 70 вместо Друга
//   Player52 (ур. 24). Журнал игрока (LogDrop 26.10 22:17): 5220 своя @19 = 75230 (Player52), @23 = 0, @25 = 1 «в игре» — хотя
//   1025 при входе: онлайн=0; затем 5111×10 со СВОИМИ HP 86706, 5112 со своими эффектами, 5110 rec[4] = 75214 (СВОЙ charId),
//   имя «Player17», rec[29] = 70; и вторая 5220 @19 = 75214 (сервер записал Другом меня самого).
//   ЦЕПОЧКА СЕРВЕРА: `UPCharSoulMate` берёт номер и «в игре» Друга из `dbo.CharPos WHERE Userindex=@Mate AND Usernum<>-1`;
//   строка учётки 75230 стояла «в игре» (Usernum 0, State 1) при Друге вне игры (на 22:23 она такая же — после сессии
//   одержимости персонажем Player52 в 22:19, SoulState 2; выход её не сбрасывает) →
//   CUser+99018 = 0, +99020 = 1 (`charLogonCommunityRes_0@0x5285F0`) → `sub_4940C0` шлёт 3313 «Друг вошёл» на номер 0, а
//   номер 0 — я сам → `recv_1@0x4945B0` (GameServer.c:59300-59351) без сверки получателя отдаёт 5110 с МОИМИ данными мне же и
//   пишет мне +99014 = мой charId, +99018 = мой номер → 5220 @19 = я; HP-поток 3319 → 5111 (`sub_494010`) тоже уходит мне.
//   Оригинал (`soulmateUpdateInfo@0xE64C30`, social.c:7300) копирует 5110 без проверки — показал бы то же самое.
//   Клиент: 5110 с rec[4] == свой charId и 5220 @19 == свой — эхо (Друг всегда другая учётка): данные не применяем, Друга
//   считаем ВНЕ ИГРЫ (каков он на деле), поток 5111/5112/5227 до настоящей 5110 Друга не принимаем. Серверная причина —
//   устаревший CharPos + отсутствие сверки в recv_1 — в отчёте агента (сервер старый, протокол не трогаем).
ASDA_DEV_CVAR(int32, CVarSoulmateSelfEchoGuard,
	TEXT("asda.Soulmate.SelfEchoGuard"), 1,
	TEXT("1 (по умолчанию) [ИСПРАВЛЕНИЕ] = 5110 с charId Друга == свой и своя 5220 с Другом == я — эхо своих данных: не применять,\n")
	TEXT("Друг вне игры (панель 6500 скрыта, вкладка «Вне игры»), 5111/5112/5227 — только после 5110 настоящего Друга.\n")
	TEXT("0 = как оригинал: эхо применяется (панель Друга показывает своё имя/уровень/HP)."),
	ECVF_Default);
// Значение `asda.Soulmate.SelfEchoGuard` для `AsdaSoulmatePopBar.cpp` (ключ живёт здесь — его читают оба файла).
bool AsdaSoulmateSelfEchoGuardOn() { return CVarSoulmateSelfEchoGuard.GetValueOnGameThread() != 0; }

// ★ 08.10.2026 (баг владельца п.2, `Reports/owner_1008/ui_bugs.md`): рантайм-сдвиг подписи «Делиться» (ключ
//   `asda.Soulmate.ShareLabelShiftX`, 03.09) СНЯТ. Он двигал на −12 и группу `SHARE_0N_grp` (в ней галка), и подпись внутри
//   неё — подпись уезжала на −24, галка на −12 и наезжала на «Д». Раскладка теперь В АССЕТЕ (закон no-runtime-widget-fixes):
//   шаг `asda_ui_widen_windows` (таблица `Tools/data/ui_widen_windows.json`, генератор `asda_ui_widen_windows_plan.py`,
//   EXTRA_NODES COMMUNITY_WIN) — группа 141 → 117, подпись там же, где её видел владелец, галка 1 px левее подписи.
// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════
void UAsdaSoulmateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★ 23.10.2026: слоты эффектов Друга панели 6500 — «пусто» (эталон: `pBuffWnd[2*i+258] = -1` у незанятых).
	for (int32 i = 0; i < kPopBuffs; ++i) { PopBuffId[i] = -1; PopBuffSrc[i] = -1; }
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HCard         = Net->OnSoulmateCard.AddUObject(this, &UAsdaSoulmateSubsystem::OnCard);
			HBond         = Net->OnSoulmateBond.AddUObject(this, &UAsdaSoulmateSubsystem::OnBond);
			HBreak        = Net->OnSoulmateBreak.AddUObject(this, &UAsdaSoulmateSubsystem::OnBreak);
			HPartnerLeft  = Net->OnSoulmatePartnerLeft.AddUObject(this, &UAsdaSoulmateSubsystem::OnPartnerLeft5226);
			HBreakRefused = Net->OnSoulmateBreakRefused.AddUObject(this, &UAsdaSoulmateSubsystem::OnBreakRefused);
			// ★ T2 ЗАКРЫТ: 5205 приходит ЦЕЛИКОМ (61 б) — без тела ветку кода 0 собрать нечем.
			HBreakReq     = Net->OnSoulmateBreakRequest.AddUObject(this, &UAsdaSoulmateSubsystem::OnBreakRequest);
			HSearch       = Net->OnSoulmateSearch.AddUObject(this, &UAsdaSoulmateSubsystem::OnSearch);
			// ★ Волна 1-А: 5485 — сервер сам поднимает окно 8200 после списка 5484 (`Send_SoulSearch@0x54DFD0`).
			HSearchShow   = Net->OnSoulmateSearchShow.AddUObject(this, &UAsdaSoulmateSubsystem::OnSearchShow);
			HMenu         = Net->OnPlayerMenuResult.AddUObject(this, &UAsdaSoulmateSubsystem::OnPlayerMenuResult);
			bNetBound = true;
		}
		// Клик по значку уведомления типа 1 — эталон `uiNoticeButtonDispatch` case 1.
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			HNotice = Hb->OnNoticeIconClicked.AddUObject(this, &UAsdaSoulmateSubsystem::OnNoticeIcon);
		}
	}
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] подсистема поднята (вкладка 6402 + окна 8400/9100)"));
}

void UAsdaSoulmateSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSoulmateCard.Remove(HCard);
			Net->OnSoulmateBond.Remove(HBond);
			Net->OnSoulmateBreak.Remove(HBreak);
			Net->OnSoulmatePartnerLeft.Remove(HPartnerLeft);
			Net->OnSoulmateBreakRefused.Remove(HBreakRefused);
			Net->OnSoulmateBreakRequest.Remove(HBreakReq);
			Net->OnSoulmateSearch.Remove(HSearch);
			Net->OnSoulmateSearchShow.Remove(HSearchShow);
			Net->OnPlayerMenuResult.Remove(HMenu);
		}
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			Hb->OnNoticeIconClicked.Remove(HNotice);
		}
	}
	CancelOfflineReqTimer();   // ★ 24.10.2026: одноразовый тикер автозапроса 2244 не должен пережить подсистему
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: подсистема живёт на
//   `GameInstance` и переживала разрыв связи и выход в выбор персонажа вместе с карточкой
//   пары, текстом «о себе» и защёлками «запрос в полёте».
void UAsdaSoulmateSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Защёлки снимаем всегда: при смене карты соединение пересоздаётся и ответа
	// на старый запрос не будет уже никогда.
	bBreakReqInFlight = false;
	Proposal = FAsdaSoulmateProposal();
	bBreakAskPending = false;
	BreakAskCharId = -1; BreakAskUserNum = -1;
	BreakAskB7 = 0; BreakAskB8 = 0; BreakAskChannel = 0;
	BreakAskName.Empty();
	// ★ 23.10.2026: подсказка панели 6500 и кадр мыши не переживают ни смену карты, ни сброс (виджет умирает с миром).
	HidePopTip();
	PopMouse.Reset();
	PopHoverId = -1;
	// ★ 24.10.2026 (ревизия сведений о Друге вне сети, (а)): ответа на 2244, отправленный по прежнему соединению, не будет — ожидание кнопки
	//   6584 и защёлка автозапроса снимаются и при смене карты (иначе ответ на следующий автозапрос поднял бы 6640 без нажатия).
	if (CVarSoulmateOldSoulGridReplyTtl.GetValueOnGameThread() > 0.f)
	{
		bGridShowPending = false;
		bAutoReqPending = false;
	}
	// ★ 26.10.2026 (smmessage): записки Другу. Панель уведомлений пересоздаётся (и при смене карты) — наших значков на ней больше
	//   нет: счётчик к нулю, тик выставит их заново по очереди. [ОРИГИНАЛ] Очередь (`this+511`) и окна-свитки смену карты
	//   ПЕРЕЖИВАЮТ (их никто не чистит; `uiNoticeIconPanelReset` гасит только значки, а сервер в CharPortalMake присылает 6055
	//   заново — его разбор снимает до трёх старых записей и кладёт пришедшие). Полный сброс — очередь и окна к виду «новый вход».
	NoteIconsShown = 0;
	bNoteIconsDirty = true;
	if (AsdaIsFullSessionReset(Reason))
	{
		NoteQueue.Reset();
		for (int32 i = 0; i < kNoteWinCount; ++i) { bNoteWinWanted[i] = false; NoteShown[i] = FAsdaSoulNote(); CloseNoteWindow(i); }
	}
	if (!AsdaIsFullSessionReset(Reason))
	{
		// Смена карты: запись, галки и «панель на экране» (`bPopWanted`) ЖИВУТ — оригинал окон не пересоздаёт,
		//   панель остаётся; порт вернёт её на новом виджете в `TickPopCard`.
		// ★ 23.10.2026 (sm-info): мини-карта при смене карты чистит ВСЕ отметки — отметку Друга вернуть на следующем кадре.
		PushCoupleMark(true);
		return;
	}

	// ★ 23.10.2026: состояние панели Друга — к виду «записи не было».
	SelfInfo5220.Reset();
	bSelfEchoSeen = false;   // ★ 26.10.2026: «эхо себя» — признак сессии (новый вход спрашивает сервер заново)
	bPopSrvOpt = false;
	MateUserIdxLive = -1;
	MateCharIdLive = -1;
	bPopWanted = false;
	PopClassIdx = -1;
	for (int32 i = 0; i < kPopBuffs; ++i) { PopBuffId[i] = -1; PopBuffSrc[i] = -1; }
	for (int32 i = 0; i < 4; ++i) { Card.Modes[i] = 0; }
	// ★ 23.10.2026 (sm-info): свой блок соулмейта — к виду «ещё не приходил».
	FriendLevelBlk = 0; FriendExpBlk = 0; PartnerMapBlk = -1; PartnerChannelBlk = -1; SelfConnectType = 0;
	bBlkLevelKnown = false;
	ShareSendRefused = 0;
	// ★ 24.10.2026 (sm-offline): сведения о Друге вне игры — от прошлого персонажа не переживают; отложенный запрос снят.
	//   `LastOldSoulReqSec` (предел частоты 2244) НЕ сбрасываем: он ограничивает поток к серверу, а не состояние вкладки.
	OffFriend = FAsdaOfflineFriend();
	bOffStale = true;
	bGridShowPending = false;
	bAutoReqPending = false;
	bOffLeaveCheck = false;          // ★ 24.10.2026: проверка «ответ после 5226 свежее выхода» — от прошлого персонажа не переживает
	bOffLeaveRetryUsed = false;
	CancelOfflineReqTimer();

	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] сброс сессии (%s): карточка «%s»"),
		AsdaSessionResetReasonName(Reason), *Card.PartnerName);
	// ★ Новая сессия = состояние «записи ещё не было»: вкладка должна вернуться к тому виду,
	//   какой эталон даёт СРАЗУ после построения окна (все поля данных пустые), а не к виду
	//   «разорванная пара» («0 Ур.», класс 0). Признак снимаем ДО `ClearBond` — он его читает.
	bCardReceived = false;
	// `ClearBond` чистит `Rec`/`Card` и перерисовывает вкладку — отправок в нём нет.
	ClearBond();
	IntroText.Empty();
	LinkWinMode = 1;
	bFriendLevelDirty = false;
	SexFilter = 0;
	// ★ Волна 1-А: окно поиска — к значениям `CFindSoulmateWin_Init@0xE70EC0` (страницы 1/1/1, выбора нет, список пуст).
	Candidates.Reset();
	SearchPage = 1; SearchReqPage = 1; SearchLast = 1;
	SelectedRow = -1;
	HoverRow = -1;   // С-3: подсветка наведения — к виду «окно только построено»
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kLinkWindowId, false);
			WM->SetWindowVisible(kReqAnsrWindowId, false);
			WM->SetWindowVisible(kFindWindowId, false);
			WM->SetWindowVisible(kPopWindowId, false);
			WM->SetWindowVisible(kIntroWindowId, false);
		}
	}
	// ★ 23.10.2026 (sm-info): пары больше нет — отметку снять (и ещё раз после сброса мини-карты).
	PushCoupleMark(true);
}

// ═══════════════════════════════ ВКЛАДКА 0 ════════════════════════════════════════════════════
void UAsdaSoulmateSubsystem::OnTabActivated(bool bVisible, UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	if (!bVisible)
	{
		// ★★★ ЭТАЛОН ПРИ УХОДЕ С ВКЛАДКИ ГАСИТ ОКНО ЗАМЕТКИ. `communityUI_SwitchTab@0xBD4040`
		//   в ветках 1/2/3 и `communityUI_Close@0xBD3F20` зовут `sub_92F599(g_pSoulmateWnd)`
		//   — это трамплин на `sub_E6AD20@0xE6AD20` (`RE/ida/export/excluded_thunks.tsv:12632`),
		//   тело: `SetVisible(this+57, 0)` + `SetFocus(this+60, 0)`, а `this+57` = `m_pMsgWin`
		//   (6605) и `this+60` = `m_pMsgInput` (6602) по разметке `structs_ru.h:1513/1516`.
		//   Порт этого не делал — окно заметки оставалось поднятым и «само» открывалось снова
		//   при возврате на вкладку.
		ShowNotePanel(false);
		// ★ 24.10.2026 (sm-offline): вкладку закрыли — отложенный автозапрос 2244 не нужен (при следующем показе спросим снова).
		CancelOfflineReqTimer();
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaSoulmateSkillSubsystem* Sk = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>())
			{
				Sk->OnTabActivated(false, CommunityWindow);
			}
		}
		return;
	}
	// ★★★ ДЕФЕКТ ПОРТА (не оригинала), найден 02.09.2026. БЫЛО: `if (!bTabBound || !CommWnd)`.
	//   `CommWnd` присваивается СТРОКОЙ ВЫШЕ, поэтому второе условие не выполнялось НИКОГДА, а
	//   `bTabBound` оставался взведённым от ПРЕДЫДУЩЕГО виджета окна. Окно 6400 пересоздаётся
	//   после каждой смены карты (`EnsureWindow` → `IsWindowLive`), и вкладка приходила на НОВОЕ
	//   дерево без привязки: кэш контролов и реле кнопок смотрели в уничтоженные виджеты,
	//   наполнение не выполнялось, на экране оставались заглушки разметки. Разбор — в объявлении
	//   `IsBoundTo`. СТАЛО: привязка проверяется по САМОМУ виджету.
	if (!IsBoundTo(CommWnd)) { BindTabControls(CommunityWindow); }
	// ★ ПОРЯДОК ОБЯЗАТЕЛЕН. Контейнер (`ApplyTabVisibility`) прямо перед этим вызовом показал ВСЁ
	//   поддерево вкладки, вернув на экран и заглушки разметки, и узлы с `Visible="0"`. Поэтому
	//   сначала перезаписываем ВСЕ подписи из таблицы текста, затем гасим то, что скрыто самой
	//   разметкой, и только потом рисуем данные (закон `jui-uneven-node-layout`).
	ApplyStaticCaptions();
	ApplyAlwaysHidden();
	// ★ 23.10.2026 [ОРИГИНАЛ]: галки 6543..6546 — из своей записи 5220 (`itemTooltip_RefreshControls@0xE62710`).
	SyncShareChecksFromModes();
	// ★★★ Данные рисуем ТОЛЬКО если запись соулмейта хоть раз приходила: эталон зовёт
	//   `CSoulmateWnd_RefreshInfo` из обработчиков 5110/5111 и из `CSoulmateWnd_ResetAndRefresh`,
	//   но НЕ при показе вкладки. Без этого гейта порт при нулевой записи печатал «0 Ур.» и имя
	//   класса 0 игроку, у которого соулмейта нет вовсе.
	if (bCardReceived) { RefreshCard(); }
	// ★ 23.10.2026 (sm-info) [ПОРТ]: окно 6400 у порта пересоздаётся (смена карты), у оригинала — живёт с последней
	//   перерисовкой `itemTooltip_RefreshControls`. Повторяем её на новом дереве: «Вне игры», уровень/опыт Дружбы, share.
	RefreshSelfBlockControls(TEXT("показ вкладки"));
	// ★ 24.10.2026 (sm-offline) [ИСПРАВЛЕНИЕ]: Друг вне игры, сведений о нём нет или они устарели — автозапрос 2244 (не чаще раза в 30 с).
	if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() != 0
		&& (bOffStale || !OffFriend.bReplied || OffFriend.Smidx != Card.SlotIndex))
	{
		RequestOfflineFriendInfo(TEXT("показ вкладки"), 0.f, /*bTabShowing*/true);
	}
	// Лоток навыков Дружбы и рулетка живут на этой же вкладке отдельными подсистемами.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSoulmateSkillSubsystem* Sk = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>())
		{
			Sk->OnTabActivated(true, CommunityWindow);
		}
		if (UAsdaSoulRouletteSubsystem* Ro = GI->GetSubsystem<UAsdaSoulRouletteSubsystem>())
		{
			Ro->BindTabControls(CommunityWindow);
		}
	}
}
