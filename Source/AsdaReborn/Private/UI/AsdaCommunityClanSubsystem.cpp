// Copyright (c) 2026 @sitxovski (Discord)
// Порт вкладки 2 «Клан» (окно `COMMUNITY_CLAN_WIN` 14033).
//   Разбор-источник: `Reports/community_tabs_frnd_clan_who.md` РАЗДЕЛ 2.
#include "UI/AsdaCommunityClanSubsystem.h"

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaCommunitySubsystem.h"

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"           // обход дерева окна: границы страниц панели управления
#include "Components/EditableText.h"        // поля ввода 14021 «заметка» и 14029 «объявление»
#include "UI/AsdaUiHelpers.h"   // ★ 31.10.2026: AsdaUi::AnchorRelay — общий GC-якорь реле
#include "UI/AsdaJuiComboBox.h"  // ★ 01.11.2026: списки званий страницы «Ранги»
#include "UI/AsdaHotbarSubsystem.h"   // ★ 01.11.2026: значок 13 (предложение стать главой)
#include "AsdaCommunityClanSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCommunityClanSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaClanTab);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

extern bool AsdaClanEnterLeaderSelectsPageOn();

namespace {
// ══════════════════════ КЛЮЧИ ОТКАТА (дефекты оригинала: чиним, но помечаем) ═══════════════
	// Порционированный список членов: порции — 4125, КОНЕЦ — пустой 4126 (payload 0, тело клиентом
	//   не читается вовсе; сервер `sub_4C28F0(…, 4126, 0, …, nullptr, …)`). Опкод 4126 ТЕПЕРЬ
	//   ПРИНИМАЕТСЯ (`UAsdaNetSubsystem::OnClanMemberListEnd`), поэтому границу порций больше не
	//   угадываем. Ключ остаётся ради второй его роли — снятия дублей при повторном открытии
	//   вкладки (эталон в `sub_925C5B` только ДОБАВЛЯЕТ записи, это дефект D-F3 у приятелей).
	ASDA_DEV_CVAR(int32, CVarClearOnRequest,
		TEXT("asda.Community.Clan.ClearListOnRequest"), 1,
		TEXT("1 (по умолчанию) = список членов клана очищается при отправке запроса 4124, порции 4125\n")
		TEXT("наполняют его заново (дубли при повторном открытии вкладки исключены).\n")
		TEXT("0 = только добавление, как в эталоне (дубли возможны).\n")
		TEXT("★ Границу списка держит НЕ этот ключ, а честный опкод 4126."),
		ECVF_Default);

	// ★★★ ГЕЙТ ПОДРЕЖИМА 2 «ПРЕДВОДИТЕЛЬ» — ТЕПЕРЬ НАСТОЯЩИЙ (`clan_soulmate_gaps.md` §2.2).
	//   Эталон (`uiSwitchPanelMode@0xBC2A10`, ветка `a2 == 2`): пропускает, если
	//     `ClanRankPermission(wnd, звание из g_LocalPlayer+0x97B, 2) || IsClanMaster()`.
	//   Обе половины гейта в порту закрыты: звание — `UAsdaNetSubsystem::GetSelfClanGrade()`
	//   (писатели 4105/4133/6523 + восстановление из 4125), маски прав — блок 4123 (смещения
	//   57/59/61/63), сама функция разобрана до примитива и портирована в `FAsdaClanBaseInfo::
	//   RankPermission` (`ClanRankPermission@0xBC44A0`). Ключ остаётся ТОЛЬКО как аварийный откат.
	// ★★★ ДЕФЕКТ ОРИГИНАЛА D-C5 (найден аудитом 31.08.2026, доказан двумя методами).
	//   `communityUI_SwitchTab@0xBD4040`, ветка `case 2`, шлёт 4122+4124 НЕ ВСЕГДА, а только под
	//   условием `if (g_bChannelListReqPending)` (дизасм `00BD412F cmp dword ptr [0x2be5100], 0`).
	//   Писатель у этого флага в бинаре РОВНО ОДИН — обработчик 4126 (`case 0x101E:
	//   g_bChannelListReqPending = 0`), а начальное значение = 0: адрес 0x02BE5100 попадает в
	//   неинициализированный хвост секции `.data` (VA 0x15B5000, VSize 0x23085BC, RawSize 0x17000 —
	//   смещение далеко за сырыми данными), то есть BSS.
	//   ⇒ В ОРИГИНАЛЕ ВКЛАДКА НЕ ЗАПРАШИВАЕТ ДАННЫЕ НИКОГДА. Список и инфо-блок приходят один раз
	//     при входе в мир (`netSendAntiCheatTokenOp4122`/`netSendAntiCheatToken` на VA 0x00B41xxx,
	//     строка 11229 экспорта) и после операций с кланом (0x00BC7xxx, строка 236855) — и дальше
	//     устаревают: вышедшие/вступившие члены, смена званий и ОД во вкладке не видны до перезахода.
	//   Порт (и до аудита тоже) шлёт запрос на каждый показ вкладки. Оставляем ЛЕЧЕНИЕ по умолчанию —
	//   ровно как у ключа `RestoreGradeFrom4125`: без него вкладка показывает протухшие данные, а
	//   логин-пакет порта может и не дойти. Но теперь это ПОМЕЧЕНО и откатывается ключом.
	ASDA_DEV_CVAR(int32, CVarRequestOnTabOpen,
		TEXT("asda.Community.Clan.RequestOnTabOpen"), 1,
		TEXT("1 (по умолчанию) = при каждом показе вкладки шлём 4122+4124 (лечение дефекта D-C5:\n")
		TEXT("оригинал запрашивает состав ОДИН РАЗ за сессию и дальше показывает устаревший список).\n")
		TEXT("0 = как в оригинале: вкладка не шлёт ничего, данные только от входа в мир."),
		ECVF_Default);

	// ★ 01.11.2026 (clan-window, жалоба владельца «на Information/Leader пропадает верхний блок с рамкой») [ОРИГИНАЛ]:
	//   `uiSwitchPanelMode@0xBC2A10` переключает РОВНО `modePanel0` (+0x10), `modePanel1` (+0x18), `modePanel2` (+0xB4) и 14101.
	//   По полям класса (`sfields_out_CmtyClan.tsv`: +0x10 = 14100 `CmtyClan_Mem_Win`, +0x18 = 14035, +0xB4 = 35000; второй
	//   источник — `gap_breakout.tsv`: `CCommunityWnd+0x10 modePanel0`) это СПИСОК членов, а рамка 14001 `CMTY_CLAN_INFO_WIN` и
	//   инфо-блок 14099 `CmtyClan_Icon` (название, уровень, глава, ОД) — братья под 14033 и не гасятся ни в одном подрежиме.
	//   Порт держал их в списке подрежима 0 (прежний разбор ошибочно читал +0x10 как 14001). 0 — прежнее поведение.
	ASDA_DEV_CVAR(int32, CVarInfoBlockAllModes,
		TEXT("asda.Clan.InfoBlockAllModes"), 1,
		TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = рамка 14001 и инфо-блок клана 14099 видны во всех подрежимах вкладки «Клан» (как эталон); ")
		TEXT("0 = прежнее (гасли на «Информации» и «Предводителе»)."),
		ECVF_Default);
}

	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] по решению владельца («Нет, просто скрой эту кнопку вообще и сдвинь остальные выше»):
	//   пункт 0 «Кандидаты» полосы 35001 убран (в ассете свёрнут, пункты 1…5 подняты на 44 px — `Tools/_fix_clan_hide_applicants.py`).
	//   У оригинала страница 0 не открывается никогда (`uiSelectPanelTab` case 0 → нотис 17107), а системы заявок в клиенте нет.
	//   Здесь: стартовая страница панели — 1 «Права», показ подрежима по префиксам пункт 0 не зажигает, страница 0 недостижима.
	ASDA_DEV_CVAR(int32, CVarHideApplicantsPage,
		TEXT("asda.Clan.HideApplicantsPage"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = пункта «Кандидаты» на панели «Предводитель» нет, первая страница — «Права»; ")
		TEXT("0 = прежнее (пункт показывается, вход в «Предводителя» даёт нотис 17107; раскладку пунктов в ассете ключ не возвращает)."),
		ECVF_Default);

namespace {
	// ★ 07.11.2026 [ПОРТ]: окно 6400 пересоздаётся при смене карты, а объявления приходят только НОВЫЕ (6511 с курсором): прочитанные оставались
	//   в модели, но новое окно их не рисовало — панель пустела, и удалить объявление было нечем. 1 — привязка к окну рисует их заново; 0 — прежнее.
	ASDA_DEV_CVAR(int32, CVarNoticeRedrawOnBind,
		TEXT("asda.Clan.NoticeRedrawOnBind"), 1,
		TEXT("[ПОРТ] 1 (по умолчанию) = после пересоздания окна сообщества объявления клана рисуются заново; 0 = прежнее (панель пуста до нового объявления)."),
		ECVF_Default);

	ASDA_DEV_CVAR(int32, CVarManageGate,
		TEXT("asda.Community.Clan.ManageGate"), 1,
		TEXT("1 (по умолчанию) = подрежим «Предводитель» гейтится ПРАВАМИ, как в эталоне:\n")
		TEXT("право 2 по маске моего звания из блока 4123, либо «я глава» (звание 1);\n")
		TEXT("отказ = нотис 17006 «Запрещено» и возврат на прежний подрежим.\n")
		TEXT("0 = открывать подрежим всегда (аварийный откат, если 4123 в сборке не приходит)."),
		ECVF_Default);

	// loc-идентификаторы вкладки (таблица «событие → loc → канал», отчёт §2.7).
	constexpr int32 kLocTabList   = 12201;   // «Состав»       — пункт 0 списка 14000
	constexpr int32 kLocTabInfo   = 12200;   // «Информация»   — пункт 1
	constexpr int32 kLocTabLead   = 12202;   // «Предводитель» — пункт 2
	constexpr int32 kLocClanName  = 12203;   // «- Название клана»
	constexpr int32 kLocChief     = 17001;   // «Глава клана»
	constexpr int32 kLocLevel     = 17002;   // «Уровень клана»
	constexpr int32 kLocTotalPt   = 17003;   // «Всего ОД: »
	constexpr int32 kLocNeedPt    = 17004;   // «Необходимо ОД: »
	constexpr int32 kLocRemainPt  = 17005;   // «Осталось ОД: »

	constexpr int32 kLocMyGrade   = 17021;   // «Положение в клане»
	constexpr int32 kLocUsePoint  = 17022;   // «ОД передано клану»
	constexpr int32 kLocMyPoint   = 17023;   // «ОД в наличии на данный момент»
	constexpr int32 kLocMyIntro   = 17024;   // «Заметки»
	constexpr int32 kLocMyIntroOk = 17025;   // «Сохранить»
	constexpr int32 kLocNoticeHdr = 12208;   // «Примечание»
	constexpr int32 kLocNoticeUpd = 17026;   // «Обновить»
	constexpr int32 kLocNoticeDel = 2374;    // «Очистить» — подписи кнопок 14090…14094

	// ── Панель «написать объявление» (35800 `CmtyClan_SubMenu4`, эталон `CmtyClan_Manage_BuildUI`) ──
	//   Подписи взяты ДОСЛОВНО из эталонного построителя (`AsdaStory_game.c:233064-233087`):
	//     35801 `CmtyClan_Sub4_Txt00` ← loc 17181 · 14031 `..._OK` ← loc 17183 · 14030 `..._CAN` ← 17184.
	constexpr int32 kLocNoticeWtHdr = 17181;   // пояснение над полем ввода объявления
	constexpr int32 kLocNoticeWtOk  = 17183;   // подпись кнопки 14031
	constexpr int32 kLocNoticeWtCan = 17184;   // подпись кнопки 14030

	// ★ «Обвязка» панели управления: она видна ВСЁ время, пока открыт подрежим 2, и страницами
	//   не переключается. Эталон `uiSelectPanelTab` трогает РОВНО шесть контролов-страниц
	//   (`this + 53 … this + 58`) и ничего больше — фон `BG` (`Box_GrayYellow`, брат страниц) и
	//   полосу 35001 с её шестью подписями 35002…35007 он не гасит никогда.
	static const TCHAR* kMgrChrome[] = {
		TEXT("CmtyClan_Manage"), TEXT("CmtyClan_SubMenuBtn"), TEXT("CmtyClan_SubMenuTxt"),
		TEXT("BG_"),
	};

	// ★★★ УЗЛЫ, КОТОРЫЕ В РАЗМЕТКЕ ИМЕЮТ `Visible = "0"` И ПОКАЗЫВАЮТСЯ ТОЛЬКО КОДОМ.
	//   Импортёр кладёт их в дерево обычными виджетами, а `AsdaCommunitySubsystem` раскрывает
	//   ВСЁ поддерево вкладки — значит их нужно гасить явно (закон `jui-uneven-node-layout`,
	//   раздел «AlwaysHidden»). Все шесть имён взяты из дампа разметки (`vis=0`):
	//     `CmtyClan_Notice0N_T3` 14039/14044/14049/14054 — автор объявления: эталон текст туда
	//        ПИШЕТ (`uiMemberListPopulate`), но контрол невидим ⇒ автора в строке не видно (1:1);
	//     `CmtyClan_Notice0N_S`  14040/14045/14050/14055 — рамка выбора строки объявления;
	//     `CmtyClan_Sub5_COver0…4` 35924…36004 — «шторки» строк страницы 4;
	//     `CmtyClan_Sub5_Sel`    36020 — рамка выбора заместителя;
	//     `CmtyClan_InsgSel`     36200 и `CmtyClan_BorderSel` 36398 — рамки выбора герба/рамки.
	static const TCHAR* kAlwaysHidden[] = {
		TEXT("CmtyClan_Sub5_COver"), TEXT("CmtyClan_Sub5_Sel"),
		TEXT("CmtyClan_InsgSel"),    TEXT("CmtyClan_BorderSel"),
	};

	constexpr int32 kLocLeaveBtn  = 12210;   // «Выйти из клана»

	constexpr int32 kLocSubMenu0  = 17101;   // 17101…17106 — 6 пунктов меню управления
}

// ★★★ ЦВЕТА ПОДПИСЕЙ ПОЛОСЫ ПОДРЕЖИМОВ 14000 `CMTY_CLAN_3BTN` — ИЗ СКИНА, НЕ «НА ГЛАЗ».
//   Скин контрола — `BoxBar_W69_3Btn`, `Type = "RadioButton"` (`RE/docs/JuiSkin.ini:13662`).
//   У КАЖДОГО из трёх планов четыре кадра, `CaptionColor` по кадрам одинаков:
//     кадр 0 (покой)            `FF3F3F36`   ← совпадает с `CaptionInfo` контрола
//                                              (`gamemain_jui.ini:196586` = `ff3f3f36 5`);
//     кадр 1 (наведение)        `FFFFFFFF`;
//     кадр 2 (ВЫБРАН)           `FFFBE96C`;
//     кадр 3 (выбран+наведение) `FFFFFFFF`.
//   ⚠ Блок `EXS` у контрола 14000 ПУСТ (`gamemain_jui.ini:196589-196590`) ⇒ закон
//     `ui-text-color-exs` (цвет из `EXS TextColor`) к этой полосе НЕПРИМЕНИМ — проверено,
//     а не перенесено с соседней полосы 6401.
//   ⇒ ОТСТУПЛЕНИЯ ВЛАДЕЛЬЦА («активная белая, неактивные чёрные», ключ
//     `asda.UI.CommunityTabCaptionColors`) здесь НЕ ТРЕБУЕТСЯ: у оригинала неактивные и так
//     почти чёрные (`FF3F3F36`), а не серые, как у полосы 6401. Держим 1:1 оригинал.
//   Сами цвета применяет ОБЩАЯ точка `AsdaUi::ApplyTabState` через
//   `AsdaCommunityUi::SetOptSelected` (файл соседнего агента) — здесь только подписи.
// ═══════════════════════════ ЗАПИСЬ ЧЛЕНА КЛАНА ═══════════════════════════════════════════════
int32 FAsdaClanMemberRecord::UserIndex() const { return AsdaBytes::RdI32(B); }          // [0..3]
// ★★★ ИСПРАВЛЕНО АУДИТОМ: было `B + 29` — колонка «ОД в наличии» показывала чужое поле.
//   Клиент: `00BC33C7 mov eax, [ebp-0x72]` при буфере записи `[ebp-0x94]` ⇒ 0x94−0x72 = 34.
//   Сервер: `GameServer.c:sub_405260` кладёт это значение в `v19` (`[ebp-0x6B]`, база `[ebp-0x90]`,
//   проводная запись начинается с +3) ⇒ [34..37]. Поле [29..32] (`v17`) клиентом НЕ читается.
int32 FAsdaClanMemberRecord::Points()    const { return AsdaBytes::RdI32(B + 34); }     // [34..37]
FString FAsdaClanMemberRecord::Name()    const { return FAsdaLoginData::DecodeCp1251(B + 5, 20); }

// ═══════════════════════════ ИНФО-БЛОК КЛАНА (4123, 191 б) ════════════════════════════════════
int32   FAsdaClanBaseInfo::ClanIndex()   const { return AsdaBytes::RdI16(B); }            // [0..1]
FString FAsdaClanBaseInfo::ClanName()    const { return FAsdaLoginData::DecodeCp1251(B + 2, 18); }
FString FAsdaClanBaseInfo::ChiefName()   const { return FAsdaLoginData::DecodeCp1251(B + 20, 27); }
int32   FAsdaClanBaseInfo::RemainPoints() const { return AsdaBytes::RdI32(B + 49); }      // [49..52] → 14013
int32   FAsdaClanBaseInfo::TotalPoints()  const { return AsdaBytes::RdI32(B + 53); }      // [53..56] → 14009

// Порт `ClanRankPermission@0xBC44A0` (дизасм: `mov dx, word [ecx+eax*2+0x649]`, база блока 0x614).
//   Точный эталон: `return ((__int16)(1 << ((perm − 1) % 16)) & *(__int16*)(wnd + 2*rank + 1609)) != 0;`
//   Смещение в блоке = 1609 − 1556 = 53 ⇒ маска = u16 @ (53 + rank*2).
bool FAsdaClanBaseInfo::RankPermission(int32 Rank, int32 Perm) const
{
	if (!bValid) { return false; }
	// ★★★ ДЕФЕКТ ОРИГИНАЛА D-C4 (аудит 31.08.2026), ВОСПРОИЗВОДИМ 1:1: диапазон звания эталон НЕ
	//   проверяет. Для звания 0 читается u16 @53, для 1 — u16 @55, а [53..56] — это «Всего ОД»:
	//   член без прав получает право по случайному биту суммы ОД клана. Раньше порт молча «лечил»
	//   это отсечкой `Rank < 2 → false` — то есть менял поведение оригинала без пометки и без ключа.
	//   Ограничиваем ТОЛЬКО выход за пределы 191-байтового буфера (эталону такой защиты не нужно:
	//   у него блок лежит внутри окна и чтение соседних байт памяти легально).
	const int32 Ofs = 53 + Rank * 2;
	if (Rank < 0 || Ofs < 0 || Ofs + 1 >= kSize) { return false; }
	const uint32 Mask = AsdaBytes::RdU16(B + Ofs);
	const int32  Bit  = (Perm - 1) & 15;            // для perm ≥ 1 совпадает с эталонным `% 16`
	return (Mask & (1u << Bit)) != 0;
}

// ═══════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════════
void UAsdaCommunityClanSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HMemberList = Net->OnClanMemberList.AddUObject(this, &UAsdaCommunityClanSubsystem::OnMemberList);
			// ★★★ ЧЕТЫРЕ ОПКОДА, ЗАКРЫТЫЕ РЕВЕРСОМ 31.08.2026 (`clan_soulmate_gaps.md` §2):
			//   4126 — честный конец списка членов · 4123 — инфо-панель клана (191 б) ·
			//   6512 — объявления клана (546 б) · 4117 — ответ на выход из клана (25 б).
			HMemberListEnd = Net->OnClanMemberListEnd.AddUObject(this, &UAsdaCommunityClanSubsystem::OnMemberListEnd);
			HBaseInfo      = Net->OnClanBaseInfo.AddUObject(this, &UAsdaCommunityClanSubsystem::OnBaseInfo);
			HNotice        = Net->OnClanNotice.AddUObject(this, &UAsdaCommunityClanSubsystem::OnNotice);
			HLeaveResult   = Net->OnClanLeaveResult.AddUObject(this, &UAsdaCommunityClanSubsystem::OnLeaveResult);
			// ★ 27.09.2026: 4131/4132 — блок клана «сверху» (в т.ч. после расширения клана).
			HBaseInfoPush  = Net->OnClanBaseInfoPush.AddUObject(this, &UAsdaCommunityClanSubsystem::OnBaseInfoPush);
		}
	}
	// ★ ревизия 01.11 (БЛОКЕР): тикер панели — с подъёма подсистемы, а не с первой привязки вкладки: отложенные 4124/4138/4122,
	//   списание сертификата (6525) и подписка на значок 13 нужны и игроку, который вкладку «Клан» ни разу не открывал.
	ManageSubPage = (CVarHideApplicantsPage.GetValueOnGameThread() != 0) ? 1 : 0;   // ★ 01.11: без «Кандидатов» — с «Прав»
	if (!ManageTickHandle.IsValid())
	{
		ManageTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaCommunityClanSubsystem::TickManage));
	}
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] подсистема поднята (окно 14033, подрежимы 12201/12200/12202)"));
}

void UAsdaCommunityClanSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnClanMemberList.Remove(HMemberList);
			Net->OnClanMemberListEnd.Remove(HMemberListEnd);
			Net->OnClanBaseInfo.Remove(HBaseInfo);
			Net->OnClanNotice.Remove(HNotice);
			Net->OnClanLeaveResult.Remove(HLeaveResult);
			Net->OnClanBaseInfoPush.Remove(HBaseInfoPush);
		}
		// ★ 01.11.2026: тикер панели «Предводитель», колесо окна 6400 и значок 13.
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>()) { Hb->OnNoticeIconClicked.Remove(HNoticeIcon); }
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->UnregisterWheelHandler(FName(TEXT("COMMUNITY_WIN"))); }
	}
	if (ManageTickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(ManageTickHandle); ManageTickHandle.Reset(); }
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: список участников клана, базовая
//   информация и объявления приходят пакетами 4123/4125/4126/6512 НАШЕЙ сессии. Клан у другого
//   персонажа другой (или его нет вовсе), поэтому переживать выход они не должны.
void UAsdaCommunityClanSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Признак «список грузится» — при любой причине: соединение пересоздано, дозагрузки не будет.
	bMemberListLoading = false;
	if (!AsdaIsFullSessionReset(Reason)) { return; }

	if (Members.Num() > 0 || Notices.Num() > 0)
	{
		UE_LOG(LogAsdaClanTab, Log, TEXT("[клан] сброс сессии (%s): участников %d, объявлений %d"),
			AsdaSessionResetReasonName(Reason), Members.Num(), Notices.Num());
	}
	Members.Reset();
	Notices.Reset();
	Info = FAsdaClanBaseInfo();
	++InfoRevision;   // ★ 27.09.2026: окно расширения 14450 перечитает числа
	Page = 1; TotalPages = 1; SelRow = -1; Mode = 0;
	NoticeCursor = 0;
	NoticeDetailKey = INDEX_NONE;
	ManageSubPage = (CVarHideApplicantsPage.GetValueOnGameThread() != 0) ? 1 : 0;   // ★ 01.11: без «Кандидатов» — с «Прав»
	// ★ 01.11.2026: состояние панели «Предводитель» (отложенная запись, выбор заместителя, герб, отложенные запросы).
	bPendingRec = false; MasterSelRow = -1; MasterHoverRow = -1; bWant4124 = bWant4138 = false; EmCertRec.Reset();
	for (int32 I = 0; I < 3; ++I) { EmBg[I] = EmSign[I] = EmBorder[I] = 255; }
	EmSignCell = EmSignPage = EmBorderCell = EmBorderPage = 0;
	if (bTabVisible)
	{
		DispatchListRefresh();
		RefreshClanInfoPanel();
		RefreshNoticePanel();
		// Строки страниц панели управления живут тем же списком членов — чистим их тоже.
		for (int32 R = 0; R < 7; ++R) { ClearGradeRow(R); }
		for (int32 R = 0; R < 5; ++R) { ClearMasterRow(R); }
	}
}

// ═══════════════════════════ ВКЛАДКА ══════════════════════════════════════════════════════════
void UAsdaCommunityClanSubsystem::OnTabActivated(bool bVisible, UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	bTabVisible = bVisible;
	if (!bVisible) { return; }
	// ★★★ КРИТ, НАЙДЕН 02.09.2026 (тот же дефект — во всех четырёх вкладках окна 6400).
	//   БЫЛО: `if (!флаг || !CommWnd) BindTabControls(...)`. Строкой выше `CommWnd` уже
	//     присвоен, поэтому вторая половина условия не срабатывала НИКОГДА, а флаг
	//     оставался `true` от ПРЕДЫДУЩЕГО виджета окна. Окно 6400 пересоздаётся при каждой
	//     смене карты (`UAsdaWindowSubsystem::EnsureWindow` → `IsWindowLive`: мир виджета обязан
	//     совпадать с миром GameInstance и виджет обязан быть во вьюпорте), и после травела
	//     привязка указывала на УНИЧТОЖЕННОЕ дерево: подписи не ставились, кнопки и пагинация
	//     были мертвы, а наружу выходил текст-заглушка дизайнера, запечённый в ассет.
	//   СТАЛО: признак привязки — САМ ВИДЖЕТ (`BoundWnd`), а не булев флаг. Тем же признаком
	//     закрыты все ранние выходы, чтобы сетевые обработчики не рисовали в мёртвое дерево.
	//   Рецепт общий для всех вкладок окна 6400 (впервые применён во вкладке «Приятель»).
	if (!IsBoundTo(CommunityWindow)) { BindTabControls(CommunityWindow); }

	// Порт `communityUI_SwitchTab` case 2: гейт «клана нет» держит контейнер 6400
	//   (`UAsdaCommunitySubsystem`), сюда мы попадаем уже с кланом. Дальше эталон шлёт 4122 + 4124
	//   и сбрасывает подрежим в 0 (`uiSwitchPanelMode(g_pCommunityWnd, −1)`).
	// ★★★ ХИТ-ТЕСТ ПОЛОСЫ ПОДРЕЖИМОВ 14000 (найдено при доводке 31.08.2026).
	//   `UAsdaCommunitySubsystem::ApplyTabVisibility` раскрывает поддерево вкладки, но ставит
	//   КАЖДОМУ узлу `SelfHitTestInvisible`, а `UButton` с такой видимостью мышь НЕ ЛОВИТ
	//   (закон `selfhittest-vs-hittest-invisible`). Панелям подрежимов хит-тест возвращает
	//   `ApplyModeVisibility` (её префиксы), а САМА полоса 14000 `CMTY_CLAN_3BTN` в её списки
	//   не входит — она видима всегда, поэтому её кнопки-опции чиним здесь.
	//   (Правильное место лечения — сам `ApplyTabVisibility`, см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА».)
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("CMTY_CLAN_3BTN"), true);

	RequestClanData();
	// 1:1 `uiSwitchPanelMode(g_pCommunityWnd, −1)`: подрежим 0 И сброс выбранного пункта полосы
	//   14000 в 0 (см. объявление `SwitchMode`). Раньше звался `SwitchMode(0)`, и полоса
	//   оставалась подсвеченной на том пункте, где игрок бросил вкладку в прошлый раз.
	SwitchMode(-1);
	RefreshMemberList();
}

void UAsdaCommunityClanSubsystem::BindTabControls(UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	if (!CommWnd) { return; }
	using namespace AsdaCommunityUi;

	// ★★★ Реле ПРОШЛОГО дерева окна держать нельзя: они ведут в уничтоженные виджеты (окно 6400
	//   пересоздаётся при каждой смене карты — см. `IsBoundTo`). Чистим ровно здесь, перед новой
	//   привязкой, иначе массив рос бы на каждое пересоздание.
	Relays.Reset();

	// ── Подписи (эталонный `CmtyClan_Info_BuildUI` + `CmtyClan_MyClanInfo_BuildUI`, отчёт §2.2) ──
	SetTxt(CommWnd, TEXT("CMTY_CLAN_NAME"),            Loc(kLocClanName));    // 14002
	SetTxt(CommWnd, TEXT("CMTY_CLAN_LevelTxt"),        Loc(kLocLevel));       // 14004
	SetTxt(CommWnd, TEXT("CMTY_CLAN_CHIEF"),           Loc(kLocChief));       // 14006
	SetTxt(CommWnd, TEXT("CmtyClan_TotalPointsTxt"),   Loc(kLocTotalPt));     // 14008
	SetTxt(CommWnd, TEXT("CmtyClan_NeededPointsTxt"),  Loc(kLocNeedPt));      // 14010
	SetTxt(CommWnd, TEXT("CmtyClan_RemainPointsTxt"),  Loc(kLocRemainPt));    // 14012
	// Значения инфо-панели эталон при инициализации ставит в «0»/пусто — приходят пакетом 4123.
	SetTotalPointsValue(TEXT("0"));                                           // 14009 (см. метод)
	SetTxt(CommWnd, TEXT("CmtyClan_NeededPoints"),     TEXT("0"));            // 14011
	SetTxt(CommWnd, TEXT("CmtyClan_RemainPoints"),     TEXT("0"));            // 14013
	SetTxt(CommWnd, TEXT("CMTY_CLAN_Level"),           TEXT("0"));            // 14005
	SetTxt(CommWnd, TEXT("CMTY_CLAN_NAME_V"),          FString());            // 14003
	SetTxt(CommWnd, TEXT("CMTY_CLAN_CHIEF_V"),         FString());            // 14007

	SetTxt(CommWnd, TEXT("Clan_MyGrade"),              Loc(kLocMyGrade));     // 14014
	SetTxt(CommWnd, TEXT("Clan_UsePoint"),             Loc(kLocUsePoint));    // 14016
	SetTxt(CommWnd, TEXT("Clan_MyPoint"),              Loc(kLocMyPoint));     // 14018
	SetTxt(CommWnd, TEXT("Clan_MyIntro"),              Loc(kLocMyIntro));     // 14020
	// ★ ДОБАВЛЕНО АУДИТОМ: `CmtyClan_MyClanInfo_BuildUI@0xBBD9D0` инициализирует ещё пять контролов,
	//   которых порт не трогал вовсе (в разметке у них остаётся текст дизайнера).
	SetTxt(CommWnd, TEXT("Clan_MyGrade2"),             FString());            // 14015 ← "" (эталон)
	SetTxt(CommWnd, TEXT("Clan_UsePoint2"),            TEXT("0"));            // 14017 ← "0"
	SetTxt(CommWnd, TEXT("Clan_MyPoint2"),             TEXT("0"));            // 14019 ← "0"
	// ★ ИСПРАВЛЕНО: 14021 — это ПОЛЕ ВВОДА (`UEditableText`), а не подпись, и `SetTxt` (он ищет
	//   `UTextBlock`) до него не доставал — вызов был пустышкой. Чистим поле напрямую, как эталон
	//   (`CmtyClan_MyClanInfo_BuildUI@0xBBD9D0`: `CControl_SetText(pIntroEdit, "")`).
	if (UEditableText* MemoEdit = FindEdit(TEXT("Clan_MyIntroTxt")))          // 14021 ← ""
	{
		MemoEdit->SetText(FText::GetEmpty());
	}
	SetTxt(CommWnd, TEXT("CMTY_CLAN_NOTICE_TEXT"),    FString());            // 14089 ← ""
	SetTxt(CommWnd, TEXT("CMTY_CLAN_NOTICE_TEXT2"),    FString());            // 14027 ← ""
	for (int32 i = 1; i <= kNotices; ++i)   // `ui_ClearThreeControlsText` по каждой из 4 строк
	{
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T1"), i), FString());
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T2"), i), FString());
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T3"), i), FString());
	}
	SetButtonCaption(CommWnd, TEXT("Clan_MyIntroSave"),    Loc(kLocMyIntroOk));   // 14023
	// ★ ДОБАВЛЕНО: панель «написать объявление» (`CmtyClan_Manage_BuildUI@0xBBE320`,
	//   `AsdaStory_game.c:233064-233087`) — три подписи, которых порт не ставил вовсе, из-за чего
	//   на кнопках оставался корейский текст дизайнера («확인~» / «초기화~») прямо из разметки.
	SetTxt(CommWnd, TEXT("CmtyClan_Sub4_Txt00"),           Loc(kLocNoticeWtHdr));  // 35801 ← 17181
	SetButtonCaption(CommWnd, TEXT("CMTY_CLAN_NOITICE_OK"),  Loc(kLocNoticeWtOk));  // 14031 ← 17183
	SetButtonCaption(CommWnd, TEXT("CMTY_CLAN_NOITICE_CAN"), Loc(kLocNoticeWtCan)); // 14030 ← 17184
	SetTxt(CommWnd, TEXT("CMTY_CLAN_NOTICE"),          Loc(kLocNoticeHdr));   // 14024
	SetButtonCaption(CommWnd, TEXT("CMTY_CLAN_NOTICE_BTN"), Loc(kLocNoticeUpd));  // 14025
	SetButtonCaption(CommWnd, TEXT("CMTY_CLAN_SEC_BTN"),    Loc(kLocLeaveBtn));   // 14032
	for (int32 i = 0; i <= kNotices; ++i)   // 14090 (открытое объявление) + 14091…14094 (строки)
	{
		SetButtonCaption(CommWnd, FString::Printf(TEXT("CmtyClan_NoticeDel%02d"), i), Loc(kLocNoticeDel));
	}
	for (int32 i = 0; i < 6; ++i)           // 35002…35007 — пункты меню управления
	{
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_SubMenuTxt%02d"), i), Loc(kLocSubMenu0 + i));
	}
	// ★★★ ПОДПИСИ ТРЁХ ПОДРЕЖИМОВ 14000 `CMTY_CLAN_3BTN` — ИСПРАВЛЕНО 02.09.2026 (были ПУСТЫЕ).
	//   БЫЛО: `SetButtonCaption(… "CMTY_CLAN_3BTN_OPT0" …)`. Узлов `<idc>_OPT<i>_Text` в ассете
	//     НЕТ ВООБЩЕ, а сам `CMTY_CLAN_3BTN_OPT0` — это `UButton`, поэтому цепочка `FindLabel`
	//     (точное имя → `<idc>_Text` → первый `UTextBlock` поддерева `<idc>_*`) не находила
	//     ничего, `SetTxt` молча выходил, и на полосе оставался корейский текст дизайнера
	//     из `CaptionText = "Ŭ����_����~"` разметки (`gamemain_jui.ini:196587`).
	//   СТАЛО: пишем в те узлы, которые импортёр реально создаёт для многосекционной подписи
	//     `RadioButton`: `<idc>_Text`, `<idc>_Text2`, `<idc>_Text3` (проверено по таблице имён
	//     ассета `Content/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN.uasset`).
	//   ⚠ Тот же дефект был найден координатором у полосы вкладок окна 6401 `COMMUNITY_MENU`.
	//     Цвета состояний покой/наведение/выбрано ставит общая точка `AsdaUi::ApplyTabState`
	//     через `AsdaCommunityUi::SetOptSelected` — она ищет подписи по той же неверной схеме
	//     `<idc>_OPT<i>` и чинится в файле соседнего агента; значения цветов этой полосы разобраны
	//     у объявления `kLocEmblemUndo` выше (скин `BoxBar_W69_3Btn`).
	static const TCHAR* kModeCaptionIdc[3] = {
		TEXT("CMTY_CLAN_3BTN_Text"), TEXT("CMTY_CLAN_3BTN_Text2"), TEXT("CMTY_CLAN_3BTN_Text3"),
	};
	SetTxt(CommWnd, kModeCaptionIdc[0], Loc(kLocTabList));   // 12201 «Состав»
	SetTxt(CommWnd, kModeCaptionIdc[1], Loc(kLocTabInfo));   // 12200 «Информация»
	SetTxt(CommWnd, kModeCaptionIdc[2], Loc(kLocTabLead));   // 12202 «Предводитель»

	// ── Подписи ШЕСТИ СТРАНИЦ панели управления 35000 (`CmtyClan_Manage_BuildUI@0xBBE320`) ──
	//   До этой правки порт не ставил НИ ОДНОЙ из них, и на всех страницах, кроме «Объявлений»,
	//   висел корейский/английский текст дизайнера прямо из разметки.
	BindManagePageCaptions();
	// 1:1 хвост построителя: `CControl_SetSelectedIndex(bar35001, this[386])` — полоса страниц
	//   сразу подсвечивает текущую страницу панели управления (при сборке это 0 «Кандидаты»).
	AsdaCommunityUi::SetOptSelected(CommWnd, TEXT("CmtyClan_SubMenuBtn"), ManageSubPage);

	// ── Кнопки ──
	int32 nBound = 0;
	auto B = [this, &nBound](const FString& Idc, int32 Tag, TFunction<void(int32)> Fn)
	{
		if (AsdaCommunityUi::BindButton(this, CommWnd, Idc, Tag, MoveTemp(Fn), Relays)) { ++nBound; }
		else { UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] кнопка «%s» не найдена"), *Idc); }
	};
	for (int32 i = 0; i < 3; ++i)
	{
		B(FString::Printf(TEXT("CMTY_CLAN_3BTN_OPT%d"), i), 14000 + i, [this, i](int32) { SwitchMode(i); });
	}
	// ★★★ ПОЛОСА СТРАНИЦ ПАНЕЛИ УПРАВЛЕНИЯ 35001 `CmtyClan_SubMenuBtn` — ДО 02.09.2026 НЕ БЫЛА
	//   ПРИВЯЗАНА ВООБЩЕ. Эталон: `uiWndDispatchCommand@0xBC2240`, сообщение 66817:
	//     `if (a3 == 35001) return uiSelectPanelTab((int *)this, a6);`
	//   Импортёр делает `RadioButton` шестью кнопками `<idc>_OPT0…OPT5` (проверено по таблице
	//   имён ассета). Без этой привязки страницы не переключались — их показывал разом
	//   `ApplyModeVisibility`, что и видел владелец.
	for (int32 i = 0; i < kManagePages; ++i)
	{
		B(FString::Printf(TEXT("CmtyClan_SubMenuBtn_OPT%d"), i), 35001,
			[this, i](int32) { SelectManagePage(i); });
	}
	B(TEXT("CMTY_CLAN_STATBTN1"), 14230, [this](int32) { PageFirst(); });
	B(TEXT("CMTY_CLAN_PREBTN1"),  14231, [this](int32) { PagePrev();  });
	B(TEXT("CMTY_CLAN_NEXTBTN1"), 14232, [this](int32) { PageNext();  });
	B(TEXT("CMTY_CLAN_ENDBTN1"),  14233, [this](int32) { PageLast();  });
	B(TEXT("CMTY_CLAN_SEC_BTN"),  14032, [this](int32) { ActionLeaveClan(); });
	// 14025 «Обновить» объявления — эталонная ветка `cmdId 0x36C9` шлёт тот же 6511, что и вход
	//   в подрежим 1 (`uiPanel_HandleCommand` + `uiSwitchPanelMode`).
	B(TEXT("CMTY_CLAN_NOTICE_BTN"), 14025, [this](int32) { ActionReadNotices(); });
	// ★★★ КНОПКИ «ОЧИСТИТЬ» 14090…14094 — БОЛЬШЕ НЕ МЕРТВЫ (находка M7 аудита 31.08.2026).
	//   `CmtyClan_NoticeDel00` = 14090 (раскрытое объявление), `…01…04` = 14091…14094 (строки 0…3).
	//   Разбор веток эталона и раскладка 6515 — в объявлении `ActionDeleteNotice`.
	for (int32 i = 0; i <= kNotices; ++i)
	{
		B(FString::Printf(TEXT("CmtyClan_NoticeDel%02d"), i), 14090 + i,
			[this, i](int32) { ActionDeleteNotice(i); });
	}
	// ★★★ ТРИ КНОПКИ КЛАН-ПАНЕЛИ, КОТОРЫЕ ДО 02.09.2026 БЫЛИ МЕРТВЫ (аудит 14_soulmate, КРИТ №1).
	//   Подписи им ставились, обработчиков не было ни по имени, ни по idc, ни по префиксу.
	//   У эталона это три живые ветки диспетчера `uiPanel_HandleCommand@0xBC16D0`:
	//     0x36C7 = 14023 (`ui_core.c:15311`) · 0x36CE = 14030 (`:15317`) · 0x36CF = 14031 (`:15320`).
	//   Разбор каждой — в объявлениях трёх методов ниже (`AsdaCommunityClanSubsystem.h`).
	B(TEXT("Clan_MyIntroSave"),      14023, [this](int32) { ActionSaveMyMemo();    });
	B(TEXT("CMTY_CLAN_NOITICE_OK"),  14031, [this](int32) { ActionWriteNotice();   });
	B(TEXT("CMTY_CLAN_NOITICE_CAN"), 14030, [this](int32) { ActionCancelNotice();  });
	// Жёсткий стоп набора обоим полям — значения `LimitText` из разметки (см. `SetupEditLimits`).
	SetupEditLimits();
	// ★ 01.11.2026 (clan-window): действия страниц «Предводителя» — до этой правки на них были ТОЛЬКО подписи.
	BindManageActions();

	// Клик по строкам списка членов (эталон ловит его в `uiChannelListDispatch`-семействе;
	//   у нас — прозрачная подложка под всей строкой, см. `AsdaCommunityUi::EnsureRowHit`).
	for (int32 i = 0; i < kRows; ++i)
	{
		const FString RowIdc = FString::Printf(TEXT("CMTY_CLAN_LIST%d"), i + 1);
		if (!EnsureRowHit(this, CommWnd, RowIdc, i, [this](int32 Row) { SelectRow(Row); }, Relays))
		{
			UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] строка «%s» в разметке не найдена"), *RowIdc);
		}
	}

	Sel.Collect(CommWnd, TEXT("CMTY_CLAN_MEMSEL"));   // рамка 14110
	Sel.Hide();
	CollectManagePageAnchors();   // границы поддеревьев шести страниц панели управления
	BoundWnd = CommWnd;           // ★ признак «привязано» — КОНКРЕТНЫЙ виджет, а не булев флаг
	if (CVarNoticeRedrawOnBind.GetValueOnGameThread() != 0) { RefreshNoticePanel(); }   // ★ 07.11.2026: см. ключ
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] контролы привязаны: кнопок %d, строк %d, планов рамки %d"),
		nBound, kRows, Sel.Parts.Num());
}

// ═══════════════════════════ ЗАПРОС ДАННЫХ ════════════════════════════════════════════════════
void UAsdaCommunityClanSubsystem::RequestClanData()
{
	// ★ ДЕФЕКТ ОРИГИНАЛА D-C5 (см. ключ `RequestOnTabOpen`): эталон здесь молчит.
	if (CVarRequestOnTabOpen.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogAsdaClanTab, Log,
			TEXT("[клан-вкладка] запрос при открытии отключён ключом — 1:1 оригинал ")
			TEXT("(g_bChannelListReqPending == 0, вкладка данных не просит)"));
		return;
	}
	SendClanDataRequests();
}

// ★ 27.09.2026: хвост 4109 код 1 (network.c:12061-12066) — обоснование в объявлении.
bool UAsdaCommunityClanSubsystem::RequestClanDataIfTabShown(const TCHAR* Why)
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaCommunitySubsystem* Cm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr;
	const int32 Tab = Cm ? Cm->GetActiveTab() : -1;   // флаг видимости панели `g_CommunityUI+0x20` = выбрана вкладка 2
	if (Tab != 2)
	{
		UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] %s: панель клана не видима (вкладка %d) — 4122/4124 не нужны (эталон)"), Why, Tab);
		return false;
	}
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] %s: панель клана видима — 4122 + 4124 (network.c:12064-12065)"), Why);
	return SendClanDataRequests();
}

bool UAsdaCommunityClanSubsystem::SendClanDataRequests()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }

	const int32 ClanId = SelfClanId();
	if (ClanId < 0)
	{
		UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] запрос не отправлен: клана нет (clanId=%d)"), ClanId);
		return false;
	}

	// ★ ЧИСТКА ПЕРЕД ПОРЦИЯМИ: 4125 приходит частями и в эталоне только ДОБАВЛЯЕТ записи.
	//   Конец списка теперь отмечает ЧЕСТНЫЙ 4126 (см. `OnMemberListEnd`), а ключ снимает дубли.
	if (CVarClearOnRequest.GetValueOnGameThread() != 0)
	{
		Members.Reset();
		SelRow = -1;
	}
	bMemberListLoading = true;

	// ★★★ Имена приведены к РОЛЯМ опкодов (оркестратор, 2026-08-31):
	//   4122 = базовая ИНФА клана (ответ 4123) · 4124 = СПИСОК ЧЛЕНОВ (ответ 4125, конец 4126).
	//   Тело у обоих — i16 clanIndex; «страницы» в проводе нет, сервер отдаёт список порциями сам.
	const bool bA = Net->SendClanBaseInfoReq4122(ClanId);
	const bool bB = Net->SendClanMemberListReq4124(ClanId);
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] запрос клана %d: инфа 4122=%s, список 4124=%s"),
		ClanId, bA ? TEXT("да") : TEXT("нет"), bB ? TEXT("да") : TEXT("нет"));
	return bA || bB;
}

// ═══════════════════════════ ПОДРЕЖИМЫ ════════════════════════════════════════════════════════
void UAsdaCommunityClanSubsystem::ApplyModeVisibility(int32 InMode)
{
	// ★ Зовётся не только из `SwitchMode`, но и с сетевых путей (4123 принёс маски прав, 4125
	//   восстановил звание) — окно к этому моменту может быть ещё не привязано.
	if (!IsBoundTo(CommWnd)) { return; }
	using namespace AsdaCommunityUi;
	// Порт `uiSwitchPanelMode@0xBC2A10`:
	//   0 → показать 14001 (панель «Состав») и 14101 (пагинация), скрыть 14035 и 35000;
	//   1 → показать 14035 (мои данные + объявления), остальное скрыть;
	//   2 → показать 35000 (панель управления), остальное скрыть.
	// ★★★ ГАСИМ НЕ «ПАНЕЛЬ», А ВСЁ ПОДДЕРЕВО ПО ПРЕФИКСАМ ИМЁН (закон `jui-uneven-node-layout`):
	//   импортёр расплющивает иерархию, «дети» панели лежат её СОСЕДЯМИ и сами по себе не гаснут.
	//   Списки собраны по структурному дампу разметки окна 14033 (отчёт §2.1, ветки «а», «б», «в», «г»).
	// Рамка 14001 и инфо-блок 14099 с детьми — общие для всех подрежимов (см. ключ `InfoBlockAllModes`).
	static const TCHAR* kInfoBlock[] = {
		TEXT("CMTY_CLAN_INFO_WIN"), TEXT("CmtyClan_Icon"), TEXT("Clan_Flag_Tail"), TEXT("CmtyClan_Line"),
		TEXT("CMTY_CLAN_NAME"), TEXT("CMTY_CLAN_Level"), TEXT("CMTY_CLAN_CHIEF"),
		TEXT("CmtyClan_TotalPoints"), TEXT("CmtyClan_NeededPoints"), TEXT("CmtyClan_RemainPoints"),
	};
	static const TCHAR* kMode0[] = {   // «Состав» (`modePanel0` = 14100): список членов + пагинация
		TEXT("CmtyClan_Mem_Win"), TEXT("CMTY_CLAN_LIST"), TEXT("CMTY_CLAN_MEM"),
		TEXT("CmtyClan_DAPoints"), TEXT("CmtyClan_MemSex"), TEXT("CmtyClan_PAPoints"),
		TEXT("CMTY_CLAN_SLASH"), TEXT("CMTY_CLAN_PAGE"), TEXT("CMTY_CLAN_NEXTBTN"),
		TEXT("CMTY_CLAN_PREBTN"), TEXT("CMTY_CLAN_ENDBTN"), TEXT("CMTY_CLAN_STATBTN"),
	};
	static const TCHAR* kMode1[] = {   // «Информация»: мои данные + объявления клана
		TEXT("CmtyClan_MyClanInfo_Win"), TEXT("CMTY_CLAN_SEC_BTN"),
		TEXT("Clan_MyGrade"), TEXT("Clan_UsePoint"), TEXT("Clan_MyPoint"), TEXT("Clan_MyIntro"),
		TEXT("CmtyClan_MyIntro"), TEXT("CMTY_CLAN_NOTICE"), TEXT("CmtyClan_Notice"),
	};
	// ★★★ ПОДРЕЖИМ 2 «Предводитель»: ОБВЯЗКА панели 35000 (фон, полоса страниц 35001 и её шесть
	//   подписей 35002…35007). САМИ СТРАНИЦЫ здесь НЕ показываются — их переключает
	//   `SelectManagePage` (порт `uiSelectPanelTab@0xBC5C20`), см. ниже.
	//   ★ БЫЛО ДО 02.09.2026: в списке стоял общий префикс `CmtyClan_Sub`, и подрежим «Предводитель»
	//     раскрывал ВСЕ ШЕСТЬ страниц разом поверх друг друга (жалоба владельца). Отдельного
	//     префикса `CmtyClan_PosFunc` (галочки прав) больше не нужно: он ушёл в список страницы 1.
	const bool bInfoAll = CVarInfoBlockAllModes.GetValueOnGameThread() != 0;
	for (const TCHAR* P : kInfoBlock) { SetVisByPrefix(CommWnd, P, bInfoAll || InMode == 0); }
	for (const TCHAR* P : kMode0) { SetVisByPrefix(CommWnd, P, InMode == 0); }
	for (const TCHAR* P : kMode1) { SetVisByPrefix(CommWnd, P, InMode == 1); }
	for (const TCHAR* P : kMgrChrome) { SetVisByPrefix(CommWnd, P, InMode == 2); }
	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Clan.HideApplicantsPage`): пункт 0 «Кандидаты» (фон, кнопка, подпись, хит-зона) в ассете
	//   свёрнут — префикс полосы зажёг бы его обратно.
	if (CVarHideApplicantsPage.GetValueOnGameThread() != 0)
	{
		for (const TCHAR* N : { TEXT("CmtyClan_SubMenuBtn_OPT0"), TEXT("CmtyClan_SubMenuTxt00") })
		{
			if (UWidget* W = AsdaCommunityUi::Find(CommWnd, N)) { W->SetVisibility(ESlateVisibility::Collapsed); }
		}
		CommWnd->WidgetTree->ForEachWidget([](UWidget* W)
		{
			if (!W) { return; }
			const FString N = W->GetName();
			if (N == TEXT("CmtyClan_SubMenuBtn_521_0") || (N.StartsWith(TEXT("CmtyClan_SubMenuTxt00_")) && N.EndsWith(TEXT("_HIT"))))
			{
				W->SetVisibility(ESlateVisibility::Collapsed);
			}
		});
	}
	// Страницы панели управления: снаружи подрежима 2 скрыты ВСЕ, внутри — ровно текущая.
	//   1:1 `uiSelectPanelTab`: сначала гасятся все шесть (`for i<6 SetVisible(page[i], 0)`),
	//   затем показывается выбранная.
	for (int32 P = 0; P < kManagePages; ++P)
	{
		SetManagePageVisible(P, InMode == 2 && P == ManageSubPage);
	}
	// ★ ДОБАВЛЕНО АУДИТОМ: «нижняя полоса» эталона — это контрол 14101 `CMTY_CLAN_SLASH`
	//   (`uiSwitchPanelMode` дёргает ровно четыре контрола: wnd+0x10 = 14001, +0x18 = 14035,
	//    +0xB4 = 35000 и +0x424 = 14101; ID подтверждены дизасмом `00BBD597 push 0x3715`).
	//   В подрежиме 2 эталон показывает её обратно, если страница панели управления 0 или 2:
	//     `if (!gap_608 || gap_608 == 2) CControl_SetVisible(bottomBar, 1)`.
	//   Пагинация 14230…14235 лежит ВНУТРИ 14101 (в разметке PAGE1 — её прямой ребёнок), поэтому
	//   гасится/показывается вместе с ней по префиксам (список `kBottomBar` — в шапке файла,
	//   его же использует хвост отправки объявления `sub_922772`).
	if (InMode == 2 && (ManageSubPage == 0 || ManageSubPage == 2))
	{
		SetBottomBarVisible(true);
	}

	// ★★★ ЛОВУШКА `selfhittest-vs-hittest-invisible` — ПОЛЯ ВВОДА ТЕРЯЮТ МЫШЬ: ВЫЛЕЧЕНА В ОБЩЕЙ
	//   ТОЧКЕ (2026-09-02). Здесь стоял точечный обход: `Clan_MyIntroTxt` (14021) и
	//   `CMTY_CLAN_NOITICE_WRITE` (14029) принудительно переводились в `Visible`, потому что
	//   `ShownVisibilityFor` давал полную видимость ТОЛЬКО `UButton`, и по полю нельзя было
	//   ни щёлкнуть, ни поставить каретку. Теперь `AsdaCommunityUi::ShownVisibilityFor`
	//   (`UI/AsdaCommunityFriendSubsystem.h`) выдаёт `Visible` ЛЮБОМУ интерактиву, включая
	//   `UEditableText`, — обход стал избыточным и удалён, чтобы правило жило в одном месте.
	//   Оба поля показываются теперь тем же `SetVisByPrefix`, что и остальной подрежим:
	//   14021 — по префиксу `Clan_MyIntro` из `kMode1`, 14029 — по `CMTY_CLAN_NOITICE`
	//   из списка СТРАНИЦЫ 3 панели управления (`kMgrPage3`).
	//   Откат правила — `asda.UI.InteractiveHitTest 0` (тогда оба поля снова онемеют).
	// ⚠ ОТМЕЧЕННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (аудит 31.08.2026): контейнер списка членов
	//   `CmtyClan_Mem_Win` (14100) — в разметке ЭТО БРАТ 14001/14035/35000 (все на `CHS = "2"`),
	//   и `uiSwitchPanelMode` его НЕ ГАСИТ вовсе: в оригинале строки состава остаются под панелями
	//   «Информация»/«Предводитель», которые перекрывают их своими картинками. В UE иерархия
	//   расплющена и перекрытия не гарантированы, поэтому порт гасит поддерево `CmtyClan_Mem_Win`
	//   вместе с подрежимом 0. Это СОЗНАТЕЛЬНОЕ отступление, а не совпадение с эталоном.
	// ⚠ Рамку выбора и «мёртвые» узлы разметки трогаем отдельно: рамка видима только при выборе.
	if (InMode != 0) { Sel.Hide(); }

	// ★★★ УЗЛЫ `Visible = "0"` РАЗМЕТКИ — ГАСИМ ЯВНО, ПОСЛЕДНИМ ДЕЙСТВИЕМ.
	//   `UAsdaCommunitySubsystem::ApplyTabVisibility` раскрывает ВСЁ поддерево вкладки
	//   (`AsdaSetSubtreeVisibility(COMMUNITY_CLAN_WIN_grp)`), поэтому невидимые по данным узлы
	//   иначе всплывают: рамки выбора, «шторки» строк и скрытый автор объявления. Мотивация и
	//   перечень с IDNum — у объявления `kAlwaysHidden` в шапке файла.
	for (const TCHAR* P : kAlwaysHidden) { SetVisByPrefix(CommWnd, P, false); }
	for (int32 R = 1; R <= kNotices; ++R)
	{
		SetVisByPrefix(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T3"), R), false);
		SetVisByPrefix(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_S"),  R), false);
	}
}

void UAsdaCommunityClanSubsystem::SwitchMode(int32 InMode)
{
	if (!IsBoundTo(CommWnd)) { return; }

	// ★★★ ПЕРВЫЙ ИЗ ДВУХ ВЫЗОВОВ `CControl_SetSelectedIndex(modeTabCombo, …)` ЭТАЛОНА
	//   (`uiSwitchPanelMode@0xBC2A10`, самое начало):
	//     `if (a2 == -1) { a2 = 0; CControl_SetSelectedIndex(this->modeTabCombo, 0); }`
	//   `modeTabCombo` — контрол 14000 `CMTY_CLAN_3BTN`; примитив выбора живёт в общем
	//   `AsdaCommunityUi::SetOptSelected` (см. его объявление).
	if (InMode == -1)
	{
		InMode = 0;
		AsdaCommunityUi::SetOptSelected(CommWnd, TEXT("CMTY_CLAN_3BTN"), 0);
	}
	InMode = FMath::Clamp(InMode, 0, 2);

	// ★★★ ГЕЙТ ПОДРЕЖИМА 2 «ПРЕДВОДИТЕЛЬ» — 1:1 `uiSwitchPanelMode@0xBC2A10`, ветка `a2 == 2`.
	//   ★ ИСПРАВЛЕНО АУДИТОМ 31.08.2026: гейт НЕ безусловный, он зависит от текущей страницы панели
	//     управления (`this->gap_608`, оно же `ManageSubPage`):
	//         if (gap_608 == 3)  v4 = ClanRankPermission(wnd, звание, 2) || IsClanMaster();
	//         else               v4 = IsClanMaster();
	//     То есть по МАСКЕ ПРАВ пускают только на страницу 3 («объявления»), на все прочие — ТОЛЬКО
	//     главу клана. Порт пускал по маске всегда, то есть был ЛИБЕРАЛЬНЕЕ оригинала.
	//     Подтверждение семантики страниц — `uiSelectPanelTab@0xBC5C20`: case 1/2/4/5 → `IsClanMaster()`,
	//     case 3 → `ClanRankPermission(…, 2) || IsClanMaster()`, case 0 → отказ с нотисом 17107.
	const bool bManageAllowed = (ManageSubPage == 3)
		? SelfHasPermission(kPermManage)                 // маска права 2 ЛИБО «я глава»
		: (SelfGrade() == 1);                            // `IsClanMaster@0xBC7090`: char+0x97B == 1
	if (InMode == 2 && CVarManageGate.GetValueOnGameThread() != 0 && !bManageAllowed)
	{
		// ★★★ ВТОРОЙ ВЫЗОВ `CControl_SetSelectedIndex` ЭТАЛОНА — ветка отказа гейта:
		//     `CControl_SetSelectedIndex(this->modeTabCombo, this->currentSubTab);`
		//     затем нотис 17006. Без него полоса 14000 остаётся подсвеченной на «Предводителе»,
		//     хотя подрежим не сменился (находка M3 аудита).
		AsdaCommunityUi::SetOptSelected(CommWnd, TEXT("CMTY_CLAN_3BTN"), Mode);
		NoticeBox(Loc(kLocForbidden));   // 17006 «Запрещено»
		ApplyModeVisibility(Mode);       // эталон возвращает комбо на прежний подрежим
		UE_LOG(LogAsdaClanTab, Log,
			TEXT("[клан-вкладка] подрежим «Предводитель» запрещён: моё звание %d, инфо-блок 4123 %s"),
			SelfGrade(), Info.bValid ? TEXT("получен") : TEXT("НЕ получен (маски прав неизвестны)"));
		return;
	}

	Mode = InMode;
	// ★ Полоса 14000 обязана показать фактический подрежим. В оригинале это делает САМ контрол
	//   (список `CControl` переносит выбор на нажатый пункт), а в UMG кнопка-опция состояния не
	//   держит — поэтому выбор ставим явно тем же примитивом, что и оба вызова эталона.
	AsdaCommunityUi::SetOptSelected(CommWnd, TEXT("CMTY_CLAN_3BTN"), Mode);
	ApplyModeVisibility(Mode);
	// ★ 01.11.2026 [ОРИГИНАЛ] (`asda.Clan.EnterLeaderSelectsPage`): успех ветки `a2 == 2` эталона заканчивается
	//   `uiSelectPanelTab(this, gap_608)` — текущая страница открывается так же, как щелчком (перерисовка «Рангов», прав,
	//   заместителей, герба; на «Кандидатах» — нотис 17107). Прежде порт только показывал её поддерево — данные не рисовались.
	if (Mode == 2 && AsdaClanEnterLeaderSelectsPageOn()) { SelectManagePage(ManageSubPage); }

	if (Mode == 1)
	{
		// 1:1 `uiSwitchPanelMode`: при переходе в подрежим 1 эталон шлёт 6511 «прочитать
		//   объявления клана» (ответ — порции 6512 по 546 б, до пяти, последняя с маркером 2).
		ActionReadNotices();
	}
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] подрежим → %d"), Mode);
	// 1:1 `uiWndDispatchCommand@0xBC2240`: сразу за `uiSwitchPanelMode` идёт `uiChannelListDispatch`
	//   — общий диспетчер перерисовки списка (см. `DispatchListRefresh`). Раньше здесь стоял прямой
	//   `RefreshMemberList()` для подрежима 0, и страница званий панели управления не обновлялась.
	DispatchListRefresh();
}
