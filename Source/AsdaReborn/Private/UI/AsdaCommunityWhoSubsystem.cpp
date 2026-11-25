// Copyright (c) 2026 @sitxovski (Discord)
// Порт вкладки 3 «Кто?» (окно `COMMUNITY_Who_WIN` 14600).
//   Разбор-источник: `Reports/community_tabs_frnd_clan_who.md` РАЗДЕЛ 3.
#include "UI/AsdaCommunityWhoSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "UI/AsdaUiHelpers.h"        // AsdaUi::FindNodeByPrefix — поиск узлов без idc (подсказка 14780)
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaPartySubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWho, Log, All);

namespace
{
	// ══════════════════════ КЛЮЧИ ОТКАТА (дефекты оригинала: чиним, но помечаем) ═══════════════

	// D-W2 (★★): клиент не читает байт [2] записи (код 2-й профессии), хотя сервер его передаёт;
	//   на «Кто?» всегда показан класс 1-й профессии, тогда как «Приятель» и «Клан» показывают
	//   вторую, если она есть. Спорно (разное поведение — возможно, замысел) ⇒ дефолт «как в оригинале».
	ASDA_DEV_CVAR(int32, CVarUseSecondJob,
		TEXT("asda.Community.Who.UseSecondJob"), 0,
		TEXT("0 (по умолчанию) = как в оригинале: показывается класс 1-й профессии (дефект D-W2).\n")
		TEXT("1 = как на вкладках «Приятель»/«Клан»: приоритет у 2-й профессии, если она задана."),
		ECVF_Default);

	// ★★★ D-W8 (НОВЫЙ, аудит 31.08.2026): «шепот» без выбора строки шепчет ЧУЖОМУ.
	//   Эталон `friendListCmdHandler@0xEBF0E0`, ветка 0x3910 = кнопка 14608 (game.c:505538-505540):
	//     v8 = (int)&v9[74][7 * (_DWORD)v9[70]];   // = SelRow + 7 * Page
	//     if ( v8 >= 0 ) { ... Chat_BeginWhisperTo(имя записи v8) }
	//   Проверяется НЕ `SelRow != −1`, а СУММА. При снятом выборе SelRow = −1, и на странице 2+
	//   сумма равна 7*Page − 1 ≥ 6 ⇒ гейт проходит и шепот уходит ПОСЛЕДНЕЙ записи ПРЕДЫДУЩЕЙ
	//   страницы. Асимметрия доказывает, что это дефект, а не замысел: соседняя ветка 0x390F
	//   (кнопка 14607) проверяет ровно то, что нужно — `if ( v9[74] == (char *)-1 ) return;`
	//   (game.c:505498). Кнопки листания сбрасывают выбор в −1 (game.c:505471/505477/505485/505493),
	//   так что сценарий достигается штатно: открыть вкладку → «след. страница» → «Шепот».
	ASDA_DEV_CVAR(int32, CVarFixWhisperNoSelection,
		TEXT("asda.Community.Who.FixWhisperNoSelection"), 1,
		TEXT("★ ДЕФОЛТ 1 = ИСПРАВЛЕНО (закон проекта fix-original-defects:\n")
		TEXT("дефекты оригинала чиним и помечаем; шепот постороннему — явный баг, не задумка).\n")
		TEXT("0 = как в оригинале (дефект D-W8): гейт кнопки 14608 — `SelRow + 7*Page >= 0`,\n")
		TEXT("поэтому без выбора на странице 2+ шепот уходит последней записи предыдущей страницы.\n")
		TEXT("1 = исправление: без выбранной строки кнопка «Шепот» ничего не делает."),
		ECVF_Default);

	// ★★★ D-W9 (НОВЫЙ, аудит 31.08.2026): у игрока БЕЗ отряда надпись `Cmty_Who_PtyStat{n}` (14703)
	//   не очищается. Эталон `uiCommunityWhoRefreshList@0xEBE630` (game.c:505252-505255):
	//     if ( *(int *)((char *)v12 + 35 * v10 + 323) < 0 )   // partyId < 0 — не в отряде
	//       CControl_SetVisible((CControl *)v12[8 * i + 17], 0);   // ТОЛЬКО иконка 14702
	//   — ни текст 14703, ни значок главы 14704 в этой ветке не трогаются, а 14703 в
	//   `InitControls@0xEBDC10` выставлен Visible = 1 навсегда (game.c:505143). Значит в ПАМЯТИ
	//   на строке остаётся «Глава отряда»/«Член отряда» от ПРЕДЫДУЩЕЙ отрисовки этой же строки.
	//   ★★★ ПОПРАВКА ПЕРЕРАЗБОРА 02.09.2026 — ПРОВЕРЕНО ВТОРЫМ МЕТОДОМ, НА ЭКРАН ЭТО НЕ ПОПАДАЕТ.
	//     В JUI 14703 `Cmty_Who_PtyStat{n}` и 14704 `Cmty_Who_PtyBoss{n}` — ДЕТИ контрола 14702
	//     `Cmty_Who_InPartyIcon{n}` (`gamemain_jui.ini`: 14702 на уровне CHS=6, оба — CHS=7),
	//     а обход отрисовки `CControl_RenderRecursive@0x12DF320` (`ui_core.c:108948-108952`)
	//     заходит в ребёнка ТОЛЬКО при `CControl_IsVisible(child)` — то есть скрытый родитель
	//     14702 уносит с собой и надпись, и значок. ⇒ D-W9 — избыточность кода оригинала,
	//     а не видимый дефект; «Член отряда» у одиночки НЕ показывается и в оригинале.
	//     Правку и ключ оставляем: они делают состояние узлов честным (порт держит текст пустым,
	//     а не «спрятанным»), а поведение при этом совпадает с эталоном при ЛЮБОМ значении ключа.
	//   Пустая строка появляется также через ветку очистки (game.c:505274-505280) — когда строка
	//   вообще без записи.
	ASDA_DEV_CVAR(int32, CVarClearPartyStatusWhenSolo,
		TEXT("asda.Community.Who.ClearPartyStatusWhenSolo"), 1,
		TEXT("★ ДЕФОЛТ 1 = ИСПРАВЛЕНО (закон fix-original-defects). 0 = как в оригинале (дефект D-W9): у игрока без отряда надпись 14703\n")
		TEXT("и значок главы 14704 остаются от предыдущей отрисовки строки.\n")
		TEXT("1 = исправление: у игрока без отряда надпись гасится, значок главы скрывается."),
		ECVF_Default);

	// ── Константы разметки (отчёт §3.1): РАБОЧИХ строк 7 (подложки 14707 + 10·i).
	//   Строки 8 и 9 в JUI есть, но у них IDNum = −1 и Visible = 0 — мёртвая разметка.
	//   ★ Нумерация нестандартная: подложка строки идёт ПОСЛЕ детей по IDNum (14707 против 14700…14706),
	//     но по ИМЕНАМ узлов это ровно `CMTY_Who_LIST{n}` и `CMTY_Who_NAME{n}` и т. д.
	constexpr int32 kRows = 7;

	// ★★★ D-W1: ёмкость клиентского массива эталона — 40 записей, серверный лимит выборки — 42,
	//   и `communityWhoAppendListFromPacket` пишет без проверки границ ⇒ 41-я и 42-я запись
	//   затирают память за массивом. У нас хранилище динамическое, дополнительно ЗАЖИМАЕМ длину
	//   по серверному лимиту, чтобы не принимать заведомо мусорный хвост.
	//   ★ Аудит 31.08.2026 — ЧТО ИМЕННО ЗАТИРАЕТСЯ, доказано арифметикой смещений:
	//     · ёмкость 40 — `sub_EBFE00@0xEBFE00` (game.c:505742):
	//         `CRT_ArrayCallbackIterator((int)this + 300, 35, 40, sub_924040)` — база +300, шаг 35, 40 шт.;
	//     · массив занимает [+300 … +1699], потому что 300 + 40*35 = 1700;
	//     · по смещению +1700 лежит флаг «контролы привязаны»: `*((_BYTE *)this + 1700) = 1`
	//       в `CCommunityWhoUI_InitControls@0xEBDC10` (game.c:505075), он же проверяется во ВСЕХ
	//       входных точках окна (game.c:505178/505197/505223/505702/505728).
	//   ⇒ 41-я запись пишет свой байт [0] (уровень) ровно в этот флаг и ещё 34 байта за ним.
	//     Лимит сервера 42 (GameServer.c:127913 `if ( v8 >= 42 ) break;`) — переполнение достижимо.
	constexpr int32 kMaxEntries = 42;

	// loc-идентификаторы вкладки (таблица «событие → loc → канал», отчёт §3.7).
	constexpr int32 kLocInviteBtn  = 6800;    // «Пригласить в отряд» — подпись 14607
	constexpr int32 kLocWhisperBtn = 6921;    // «Шепот» — подпись 14608
	constexpr int32 kLocTitle      = 12860;   // «Найди игрока соответствующего уровня.» — 14609
	constexpr int32 kLocJobBase    = 12556;   // loc 12556 + код класса — надпись JOB (★ НЕ 12300!)
	constexpr int32 kLocPartyBoss  = 9139;    // «Глава отряда» — надпись PtyStat
	constexpr int32 kLocPartyMember= 9484;    // «Член отряда» — надпись PtyStat
	// ★★★ УДАЛЕНО 02.09.2026: `kLocNotLeader = 1089` и хелпер `ChatSocial` были МЁРТВЫМИ.
	//   Эталон `friendListCmdHandler@0xEBF0E0`, ветка 0x390F (14607) печатает по кнопке ТРИ нотиса
	//   через `chatPrintFormattedLine(g_pPartyWnd, …)`: 1056 «Нельзя приглашать нескольких человек
	//   одновременно.» (social.c:9247), 1045 «Получено приглашение.» (social.c:9236) и 1089
	//   «Ты не являешься главой отряда.» (social.c:9260). В порту эти же три нотиса в том же
	//   порядке печатает `UAsdaPartySubsystem::OpenInviteWindow` (гейты 5/6/7,
	//   `AsdaPartySubsystem.cpp:695/706/713`), а маршрут в чат делает её же `Notice`
	//   (`AsdaPartySubsystem.cpp:2242-2266` → `UAsdaMessageSubsystem::PostText`).
	//   Держать здесь вторую копию id и второй маршрутизатор — дубль, а не поведение эталона.
}

// ═══════════════════════════ ЗАПИСЬ «КТО» ═════════════════════════════════════════════════════

FString FAsdaWhoRecord::Name()    const { return FAsdaLoginData::DecodeCp1251(B + 3, 20); }   // [3..22]
int32   FAsdaWhoRecord::PartyId() const { return AsdaBytes::RdI32(B + 23); }                  // [23..26]
int32   FAsdaWhoRecord::UserNum() const { return AsdaBytes::RdI16(B + 28); }                  // [28..29]
int32   FAsdaWhoRecord::CharId()  const { return AsdaBytes::RdI32(B + 30); }                  // [30..33]

// ═══════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════════

void UAsdaCommunityWhoSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HResult = Net->OnWhoResult.AddUObject(this, &UAsdaCommunityWhoSubsystem::OnWhoResult);
			HNotice = Net->OnWhoNotice.AddUObject(this, &UAsdaCommunityWhoSubsystem::OnWhoNotice);
		}
	}
	UE_LOG(LogAsdaWho, Log, TEXT("[кто] подсистема поднята (вкладка 14600; опкоды 5480/5481/5482)"));
}

void UAsdaCommunityWhoSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnWhoResult.Remove(HResult);
			Net->OnWhoNotice.Remove(HNotice);
		}
	}
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: список «Кто?» принадлежит СЕССИИ —
//   он собран из ответов 5481 на наш запрос 5480. После перезахода новый персонаж видел бы
//   список, который сервер прислал предыдущему.
void UAsdaCommunityWhoSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	if (Entries.Num() > 0)
	{
		UE_LOG(LogAsdaWho, Log, TEXT("[кто] сброс сессии (%s): снято записей %d"),
			AsdaSessionResetReasonName(Reason), Entries.Num());
	}
	Entries.Reset();
	Page = 0;
	TotalPages = 1;
	SelRow = -1;
	// Вкладка перерисуется сама при следующем показе; если она видна сейчас — очищаем строки.
	if (bTabVisible) { RefreshList(); }
}

// ═══════════════════════════ ВКЛАДКА ══════════════════════════════════════════════════════════

// ★★★ ПРИВЯЗАНА ЛИ ВКЛАДКА К ЭТОМУ КОНКРЕТНОМУ ВИДЖЕТУ ОКНА (разбор — в объявлении).
bool UAsdaCommunityWhoSubsystem::IsBoundTo(const UUserWidget* W) const
{
	if (!W || !bTabBound) { return false; }
	// ★ Сравнение идёт через `TWeakObjectPtr`, а не через сырой адрес, И ЭТО ВАЖНО: старый виджет
	//   мы намеренно не держим, его может собрать сборщик мусора, а индекс объекта — переиспользовать
	//   под новое окно. Слабая ссылка на такой «переиспользованный» индекс отдаёт nullptr
	//   (у записи меняется serial number), то есть привязка честно считается протухшей.
	//   ⚠ Дополнительно проверять наличие НАШИХ узлов (клик-подложек строк) НЕЛЬЗЯ: если строка в
	//   разметке не нашлась, подложки не будет НИКОГДА, и такая проверка звала бы `BindTabControls`
	//   на КАЖДЫЙ показ вкладки — реле кнопок копились бы, и одно нажатие давало бы два действия.
	return BoundWnd.Get() == W;
}

void UAsdaCommunityWhoSubsystem::OnTabActivated(bool bVisible, UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	bTabVisible = bVisible;
	if (!bVisible) { return; }   // порт `uiCommunityWhoHide`: он просто прячет окно, состояние не трогает
	// ★★★ ДЕФЕКТ ПОРТА (не оригинала), найден 02.09.2026 — разбор в объявлении `IsBoundTo`.
	//   БЫЛО: `if (!bTabBound || !CommWnd)`. `CommWnd` присваивается СТРОКОЙ ВЫШЕ, поэтому второе
	//   условие не выполнялось НИКОГДА, а `bTabBound` переживал ПЕРЕСОЗДАНИЕ окна 6400 (оно
	//   пересоздаётся после каждой смены карты). Вкладка приходила на НОВОЕ дерево виджетов без
	//   привязки — и на экран выходили заглушки дизайнера, запечённые в ассет
	//   («Distored Queen's Palace (11 Users)», `13`/`22`, `Party Leader`, `abcdefghijklmnop~`),
	//   а кнопки и выбор строки были мертвы. СТАЛО: привязка проверяется по САМОМУ виджету.
	if (!IsBoundTo(CommWnd)) { BindTabControls(CommunityWindow); }

	// ★★★ ХИТ-ТЕСТ КНОПОК ВКЛАДКИ (найдено при доводке 31.08.2026: вкладка «Кто?» этого не делала).
	//   `UAsdaCommunitySubsystem::ApplyTabVisibility` раскрывает поддерево `COMMUNITY_Who_WIN_grp`
	//   целиком, но ставит КАЖДОМУ узлу `SelfHitTestInvisible` — а `UButton` с такой видимостью
	//   мышь НЕ ЛОВИТ (закон `selfhittest-vs-hittest-invisible`). Без прохода ниже кнопки
	//   14603…14606 (пагинация), 14607 «В отряд» и 14608 «Шепот» мертвы: `SetVisByPrefix`
	//   различает узлы и выдаёт кнопке `Visible`, остальным — `SelfHitTestInvisible`.
	//   (Правильное место лечения — сам `ApplyTabVisibility`, см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА».)
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("CMTY_Who_"), true);   // IgnoreCase ⇒ и `Cmty_Who_*`
	// Узлы с `Visible = "0"` в САМОЙ разметке показывать нельзя: мёртвые строки 8 и 9
	//   (`gamemain_jui.ini:200927/201128`) и рамка выбора (`:201329`), которой правит `FSelFrame`.
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("CMTY_Who_LIST8"), false);
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("CMTY_Who_LIST9"), false);
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("CMTY_Who_SEL"),   false);

	// Порт `uiCommunityWhoShow@0xEBE500`: страница 0, всего страниц 1, выбор −1, рамка скрыта,
	//   записи затираются, затем уходит 5480.
	//   ★ Дефект оригинала D-W3: эталон затирает −1 только ПЕРВЫЕ 7 записей из 40, остальные 33
	//     держат мусор прошлого показа; у нас массив очищается целиком — дефект снят самой моделью.
	Page = 0;
	TotalPages = 1;
	SelRow = -1;
	Entries.Reset();
	Sel.Hide();
	RequestWho();
	// ★ Дефект оригинала D-W3b (★, аудит 31.08.2026): `uiCommunityWhoShow@0xEBE500` перерисовку
	//   НЕ зовёт (game.c:505174-505189 — после memset сразу `netSendTimestampPacket`), поэтому
	//   при повторном открытии вкладки на экране до прихода 5481/5482 висят строки ПРОШЛОГО
	//   поиска: счётчик `this[73]` сбрасывается только в отправителе, а нарисованное — не стёрто.
	//   Мы рисуем сразу и показываем пустой список: расхождение осознанное, отката нет, потому что
	//   «оставить чужие строки на экране» — это показ заведомо неверных данных.
	RefreshList();
}

void UAsdaCommunityWhoSubsystem::BindTabControls(UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	if (!CommWnd) { return; }
	using namespace AsdaCommunityUi;

	// ★★★ Реле ПРОШЛОГО дерева окна держать нельзя: они ведут в уничтоженные виджеты (окно 6400
	//   пересоздаётся при каждой смене карты — см. `IsBoundTo`). Чистим ровно здесь, перед новой
	//   привязкой, иначе массив рос бы на каждое пересоздание.
	Relays.Reset();

	// Подписи (эталонный `InitControls@0xEBDC10`, отчёт §3.2).
	SetButtonCaption(CommWnd, TEXT("Cmty_Who_FaddBtn"), Loc(kLocInviteBtn));    // 14607 — В ОТРЯД!
	SetButtonCaption(CommWnd, TEXT("Cmty_Who_WhisBtn"), Loc(kLocWhisperBtn));   // 14608
	SetTxt(CommWnd, TEXT("CMTY_Who_TitleTxt"), Loc(kLocTitle));                 // 14609
	// ★★★ ЗАКРЫТ ОТКРЫТЫЙ ВОПРОС АУДИТА (W-8: «назначение `CClass_SetField926(14609, 0x200000)`»).
	//   Сразу после установки текста эталон делает `CClass_SetField926((CClass *)*(this + 9),
	//   0x200000)` (game.c:505114). Прежняя пометка «читателей нет, назначение не доказано» была
	//   ошибкой метода: читатель есть, просто он не в срезе UI, а в РИСОВАЛЬЩИКЕ ШРИФТА.
	//   ДОКАЗАТЕЛЬСТВО (`RE/etalon/font_text.c:6832`): поле 926 — это `styleFlags` метки, и
	//     `if ((styleFlags & 0x200000) != 0)` рисует ТОТ ЖЕ глиф ЕЩЁ ДВАЖДЫ со сдвигом
	//     `penX + 1.0` и `penX − 1.0` (font_text.c:6834/6859) ⇒ **горизонтальное утолщение = ЖИРНЫЙ**.
	//     Соседние ветки того же места дают остальную лестницу стиля: `0x400000` — сдвиг
	//     `penX+1, penY+1` = ТЕНЬ (font_text.c:6887-6890), `0x800000` — ОБВОДКА.
	//   В ПОРТЕ ЭТО УЖЕ СДЕЛАНО и трогать здесь нечего: стиль расставляет шаг пост-импорта
	//   `Tools/asda_ui_text_style926.py` по таблице `Tools/data/text_style926.tsv`, строка
	//   `14609  CMTY_Who_TitleTxt  6400  COMMUNITY_WIN  outline=0  bold=1  shadow=0`
	//   (evidence `social.c:9128` — тот же вызов). Уверенность HIGH.
	// ★★★ 14780 = `CMTY_Who_Guide` — ДЕФЕКТ D-W6 ЗАКРЫТ (`clan_soulmate_gaps.md` §6).
	//   ПРЕЖНИЙ ВЫВОД «импортёр его никак не назовёт» НЕВЕРЕН. У контрола пустая строка `ID`, но
	//   `IDNum = 14780` ЕСТЬ (`gamemain_jui.ini:199315`), и эталон адресует его по числу:
	//     `CCommunityWhoUI_InitControls@0xEBDC10`: `*(this+11) = FindControlByID(mgr, 14780)`,
	//     при промахе — msgbox со строкой «UI CMTY_Who_Guide Error» (имя контрола у авторов игры).
	//   ИМЯ В ПОРТЕ. `IDNum` импортёр в виджет не кладёт, зато у контрола пустой `CaptionText` и
	//   НЕПУСТОЙ `EXS TextContent` ⇒ он попадает в класс S1a, и текстовый узел получает имя по
	//   ОБЩЕМУ правилу `{ЯКОРЬ}_TXT{NN}` (`Tools/asda_jui_parse.py: anchored_names`), где ЯКОРЬ —
	//   `ID` ближайшего предка. Предок 14780 в разметке — `CMTY_Who_MapBase2` (соседи-предки
	//   `CMTY_Who_MapBase1` и `CMTY_Who_LandTxt` свой `ID` имеют и в нумерацию TXT не входят),
	//   поэтому имя узла — `CMTY_Who_MapBase2_TXT00`. Ищем точно, затем по префиксу якоря, затем
	//   по префиксу скина `Box_Yellow_Op128` (409×20, `AlignYPos 8` — прижат к низу; в этом окне
	//   комбинация уникальна). Порядок «точно → якорь → скин» = три независимых признака из §6.2.
	//   ЧТО СТАВИТЬ. Эталон зовёт `CControl_SetText(*(this+11), &Src)`, и `Src` — тот же буфер, что
	//   он подставляет в 7 строк списка 14700+10i, то есть ПУСТАЯ строка: у оригинала подсказка
	//   ПУСТА. В `.ini` лежит корейская заглушка, которую импортёр перенёс в текст узла ⇒ порт
	//   показывал бы то, чего оригинал не показывает. Гасим текст — это 1:1 эталон, не выдумка.
	{
		UWidget* Guide = Find(CommWnd, TEXT("CMTY_Who_MapBase2_TXT00"));
		if (!Guide) { Guide = AsdaUi::FindNodeByPrefix(CommWnd, TEXT("CMTY_Who_MapBase2_TXT")); }
		if (!Guide) { Guide = AsdaUi::FindNodeByPrefix(CommWnd, TEXT("Box_Yellow_Op128")); }
		if (UTextBlock* T = Cast<UTextBlock>(Guide))
		{
			T->SetText(FText::GetEmpty());   // 1:1 `CControl_SetText(ctrl, &Src)` — Src пуст
			UE_LOG(LogAsdaWho, Log, TEXT("[кто] подсказка 14780 `CMTY_Who_Guide` («%s») очищена, как в эталоне"),
				*Guide->GetName());
		}
		else
		{
			UE_LOG(LogAsdaWho, Warning,
				TEXT("[кто] подсказка 14780 `CMTY_Who_Guide` не найдена (искал `CMTY_Who_MapBase2_TXT00`, ")
				TEXT("префикс якоря и скин `Box_Yellow_Op128`) — в окне может остаться корейская заглушка из .ini"));
		}
	}
	SetTxt(CommWnd, TEXT("CMTY_Who_PAGE1"), TEXT("1"));                         // 14601
	SetTxt(CommWnd, TEXT("CMTY_Who_PAGE2"), TEXT("1"));                         // 14602

	int32 nBound = 0;
	auto B = [this, &nBound](const FString& Idc, int32 Tag, TFunction<void(int32)> Fn)
	{
		if (AsdaCommunityUi::BindButton(this, CommWnd, Idc, Tag, MoveTemp(Fn), Relays)) { ++nBound; }
		else { UE_LOG(LogAsdaWho, Warning, TEXT("[кто] кнопка «%s» не найдена"), *Idc); }
	};
	B(TEXT("CMTY_Who_NEXTBTN1"), 14603, [this](int32) { PageNext();  });
	B(TEXT("CMTY_Who_ENDBTN1"),  14604, [this](int32) { PageLast();  });
	B(TEXT("CMTY_Who_RELOADBTN"),14605, [this](int32) { PagePrev();  });   // ★ имя врёт: это «назад»
	B(TEXT("CMTY_Who_STATBTN1"), 14606, [this](int32) { PageFirst(); });
	B(TEXT("Cmty_Who_FaddBtn"),  14607, [this](int32) { ActionInviteParty(); });
	B(TEXT("Cmty_Who_WhisBtn"),  14608, [this](int32) { ActionWhisper(); });

	// ★ Дефект оригинала D-W4 (★★): эталон ловит выбор строки ТОЛЬКО по подложке
	//   `CMTY_Who_LIST{n}` (жёсткий switch по 14707/14717/…), поэтому клик по имени, уровню или
	//   классу строку НЕ выбирал — в отличие от вкладки «Приятель». У нас прозрачная подложка
	//   лежит под ВСЕЙ строкой, а её дети — SelfHitTestInvisible, поэтому клик работает везде:
	//   дефект снят самой раскладкой UMG (отдельного ключа отката для него нет — вернуть «мёртвые
	//   зоны» строки означало бы намеренно ломать ввод).
	for (int32 i = 0; i < kRows; ++i)
	{
		const FString RowIdc = FString::Printf(TEXT("CMTY_Who_LIST%d"), i + 1);
		if (!EnsureRowHit(this, CommWnd, RowIdc, i, [this](int32 Row) { SelectRow(Row); }, Relays))
		{
			UE_LOG(LogAsdaWho, Warning, TEXT("[кто] строка «%s» в разметке не найдена"), *RowIdc);
		}
	}

	Sel.Collect(CommWnd, TEXT("CMTY_Who_SEL"));   // рамка 14770
	Sel.Hide();

	BoundWnd = CommWnd;   // ★ на ЭТОМ виджете стоит привязка — по нему решает `IsBoundTo`
	bTabBound = true;
	UE_LOG(LogAsdaWho, Log, TEXT("[кто] контролы вкладки 3 привязаны: кнопок %d, строк %d, планов рамки %d"),
		nBound, kRows, Sel.Parts.Num());
}

// ═══════════════════════════ ЗАПРОС 5480 ══════════════════════════════════════════════════════

void UAsdaCommunityWhoSubsystem::RequestWho()
{
	// Порт `netSendTimestampPacket@0xEBF680` (game.c:505568-505689). ПОЛЕЗНОЙ НАГРУЗКИ НЕТ:
	//   эталон собирает 24-байтовый буфер — [0..7] заглушённый QueryPerformanceCounter,
	//   [8..23] `md5Digest`, обнулённый memset'ом и больше не тронутый (game.c:505592/505685-505686),
	//   и отдаёт `CNetwork_Send(..., sendBuf, 24, OP_SEND_WHO_COMMAND, 1, 1, 1, 0, 0)`
	//   (game.c:505688). 24 = 8 + 16 + 0 — это КОНВЕРТ (тип B), а не тело.
	//   Перед отправкой обнуляется счётчик найденных записей: `*(this + 73) = 0` (game.c:505591).
	//   Ни страницы, ни фильтра клиент не передаёт — выборку целиком делает сервер
	//   (онлайн-игроки ТЕКУЩЕЙ карты — сервер обходит менеджер пользователей потока своей карты,
	//   разница уровней ±5, лимит 42 — см. шапку .h, поправка 27.09.2026).
	//   Параметры Page/Filter согласованного API заполняем нулями и помечаем это явно.
	Entries.Reset();
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendWhoSearch5480(/*Page=*/0, /*Filter=*/0);
	UE_LOG(LogAsdaWho, Log, TEXT("[кто] 5480: %s (тело пустое — фильтрует сервер)"),
		bOk ? TEXT("отправлен") : TEXT("НЕ отправлен (нет соединения)"));
}

// ═══════════════════════════ ОТРИСОВКА ════════════════════════════════════════════════════════

void UAsdaCommunityWhoSubsystem::UpdatePaging()
{
	// Порт `uiCommunityWhoUpdatePaging@0xEBFD40` (game.c:505723-505733): страниц = count/7,
	//   при остатке +1, минимум 1. Эталон после пересчёта сразу зовёт перерисовку — у нас это
	//   делают вызывающие (`OnWhoResult`/`OnWhoNotice`), чтобы не рисовать список дважды.
	TotalPages = Entries.Num() / kRows;
	if ((Entries.Num() % kRows) != 0) { ++TotalPages; }
	if (TotalPages < 1) { TotalPages = 1; }
	// ★ ДЕФЕКТ ОРИГИНАЛА D-W11 (★, найден при переразборе 02.09.2026): эталон текущую страницу
	//   `this[70]` НЕ зажимает — в `uiCommunityWhoUpdatePaging` её вообще нет. Сценарий:
	//   пролистать список до страницы 5, открыть вкладку заново (`uiCommunityWhoShow` ставит
	//   страницу 0 — ок), но повторный ответ сервера может быть КОРОЧЕ предыдущего, а страницу
	//   успели пролистать до прихода 5482. Тогда `Page > TotalPages−1`, счётчик показывает
	//   «5 / 2», все 7 строк пусты, а кнопка «предыдущая» листает по несуществующим страницам.
	//   ЛЕЧЕНИЕ: зажимаем страницу в допустимый диапазон. Ключа отката не делаем — показ
	//   номера несуществующей страницы это заведомо неверные данные на экране, а не поведение.
	const int32 PageBefore = Page;
	Page = FMath::Clamp(Page, 0, TotalPages - 1);
	if (Page != PageBefore)
	{
		UE_LOG(LogAsdaWho, Log,
			TEXT("[кто] страница %d вне диапазона (страниц %d) — зажата до %d (дефект оригинала D-W11)"),
			PageBefore + 1, TotalPages, Page + 1);
	}
}

void UAsdaCommunityWhoSubsystem::RefreshList()
{
	// ★ Рисуем ТОЛЬКО в то дерево виджетов, на котором стоит НАША привязка: `RefreshList` зовут и
	//   сетевые обработчики (5481/5482), и сброс сессии, а окно 6400 к тому моменту могло быть
	//   пересоздано. Писать в чужое/протухшее дерево бессмысленно — перерисует `OnTabActivated`.
	//   (Прежний гейт `!CommWnd || !bTabBound` пропускал рисование в НОВОЕ дерево при привязке,
	//    оставшейся от СТАРОГО, — то есть в дерево, где кнопки и рамка не работают.)
	if (!IsBoundTo(CommWnd)) { return; }
	using namespace AsdaCommunityUi;

	// ── Заголовок 14610 `CMTY_Who_LandTxt` ────────────────────────────────────────────────────
	//   1:1 эталон `uiCommunityWhoRefreshList@0xEBE630` (game.c:505223-505231):
	//     SlotByIndex = getSlotByIndex(&g_pLocalPlayer, g_nCurMapId);
	//     v7 = v12[73];                                   // ЧИСЛО НАЙДЕННЫХ (весь список, не страница)
	//     sprintf(Buffer, "%s (%d)", CClass_GetThis(SlotByIndex + 1)->gap_0, v7);
	//   ⇒ формат ровно «<имя карты> (<N>)», БЕЗ слова «Users» — «(11 Users)» в `.ini` это
	//     заглушка дизайнера (`gamemain_jui.ini:199289`, `EXS TextContent`), она же несёт его
	//     опечатку «Distored» вместо «Distorted». Ни то ни другое на экран попадать не должно.
	//   Имя карты — ИЗ ТАБЛИЦЫ (`DT_MapTable`, поле `DisplayName` = оригинальное
	//     `g_aMapDirNames + 40`), а не из кода: закон «данные из таблиц».
	//   ★ Напоминание (шапка .h): отбора списка ПО КАРТЕ на сервере нет вовсе, подпись
	//     вводит в заблуждение и в оригинале — воспроизводим как есть.
	FString MapName;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
		{
			MapName = LM->GetMapDisplayName(LM->CurrentMapId).ToString();
			if (MapName.IsEmpty())
			{
				// Не выдумываем имя: в оригинале строка всегда есть, пустота = дыра в DT_MapTable.
				UE_LOG(LogAsdaWho, Warning,
					TEXT("[кто] у карты %d нет `DisplayName` в DT_MapTable — заголовок 14610 уйдёт без имени"),
					LM->CurrentMapId);
			}
		}
	}
	SetTxt(CommWnd, TEXT("CMTY_Who_LandTxt"), FString::Printf(TEXT("%s (%d)"), *MapName, Entries.Num()));

	// Счётчики страниц: эталон печатает `this[70] + 1` и `this[71]`.
	SetTxt(CommWnd, TEXT("CMTY_Who_PAGE1"), FString::FromInt(Page + 1));
	SetTxt(CommWnd, TEXT("CMTY_Who_PAGE2"), FString::FromInt(TotalPages));
	// ★ Дефект оригинала D-W5 (★): эталон при КАЖДОЙ перерисовке зовёт `Collection_AddString` для
	//   подписей кнопок 14607/14608, то есть ДОБАВЛЯЕТ строку в коллекцию контрола вместо установки
	//   текста (в InitControls там же используется обычная установка). Мы подписи ставим один раз
	//   при привязке — повторять накопление строк смысла нет.

	const int32 Base = kRows * Page;
	for (int32 Row = 0; Row < kRows; ++Row)
	{
		const int32 Abs = Base + Row;
		// Эталонный гейт строки: `userIndex >= 0 && класс <= 0x21` — иначе строка очищается.
		if (IsRowLive(Abs)) { FillRow(Row, &Entries[Abs]); } else { ClearRow(Row); }
		SyncRowHit(CommWnd, FString::Printf(TEXT("CMTY_Who_LIST%d"), Row + 1));
	}

	// ★ ИСПРАВЛЕНО 02.09.2026: гейт рамки проверял только НАЛИЧИЕ записи в массиве, а не тот же
	//   эталонный признак «строка живая», по которому строка рисуется. После прихода нового 5481
	//   выбранная строка могла стать пустой (её очистил цикл выше), а рамка 14770 оставалась на
	//   ней — выбор «висел» на пустой строке. Теперь признак ОДИН на всю подсистему.
	//   ⚠ Порядок проверок важен: при `SelRow == −1` сумма `Base + SelRow` на странице 2+ попадает
	//     в ЗАКОННЫЙ индекс предыдущей страницы (та же арифметика, что даёт дефект D-W8), поэтому
	//     «выбора нет» проверяется ПЕРВЫМ.
	if (SelRow < 0 || !IsRowLive(Base + SelRow)) { Sel.Hide(); }
	else
	{
		// Эталон ставит рамку на подложку строки со сдвигом (−2, −2).
		Sel.MoveToRow(CommWnd, FString::Printf(TEXT("CMTY_Who_LIST%d"), SelRow + 1), FVector2D(-2.f, -2.f));
	}
}

void UAsdaCommunityWhoSubsystem::ClearRow(int32 Row)
{
	using namespace AsdaCommunityUi;
	const int32 N = Row + 1;
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_Who_NAME%d"), N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_Who_LV%d"),   N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_Who_JOB%d"),  N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("Cmty_Who_PtyStat%d"), N), FString());
	// Подложка строки INFO гасится (эталон именно по её видимости решает, «живая» ли строка),
	//   вместе с иконкой отряда и значком главы — по префиксу, т.к. иерархия расплющена.
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_Who_INFO%d"), N), false);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("Cmty_Who_InPartyIcon%d"), N), false);
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("Cmty_Who_PtyBoss%d"), N), false);
}

void UAsdaCommunityWhoSubsystem::FillRow(int32 Row, const FAsdaWhoRecord* Rec)
{
	if (!Rec) { ClearRow(Row); return; }
	using namespace AsdaCommunityUi;
	const int32 N = Row + 1;

	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_Who_NAME%d"), N), Rec->Name());
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_Who_INFO%d"), N), true);
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_Who_LV%d"), N), FString::FromInt((int32)Rec->Level()));

	// Класс: loc 12556 + код. ★ На этой вкладке база ДРУГАЯ, чем у «Приятеля»/«Клана» (12300).
	const uint8 JobCode = (CVarUseSecondJob.GetValueOnGameThread() != 0 && Rec->Job2() != 0)
		? Rec->Job2() : Rec->Job1();
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_Who_JOB%d"), N), Loc(kLocJobBase + (int32)JobCode));

	// ── Отряд. 1:1 `uiCommunityWhoRefreshList@0xEBE630` (game.c:505252-505270) ──
	//   ★ ИСПРАВЛЕНО ПО АУДИТУ 31.08.2026. Было: порт БЕЗУСЛОВНО ставил надпись 14703 и видимость
	//     значка главы 14704 из флага [27] — то есть у одиночки в строке печаталось «Член отряда».
	//     Эталон в ветке «не в отряде» гасит ТОЛЬКО иконку 14702 и больше ничего не трогает
	//     (дефект оригинала D-W9 — надпись/значок остаются от предыдущей отрисовки строки).
	const bool bInParty = Rec->PartyId() >= 0;   // [23..26] i32, −1 ⇒ не в отряде
	if (!bInParty)
	{
		SetVisByPrefix(CommWnd, FString::Printf(TEXT("Cmty_Who_InPartyIcon%d"), N), false);   // 14702
		if (CVarClearPartyStatusWhenSolo.GetValueOnGameThread() != 0)
		{
			// Ключ включён — снимаем D-W9: чистим то, что оригинал оставил от прошлой отрисовки.
			SetVisByPrefix(CommWnd, FString::Printf(TEXT("Cmty_Who_PtyBoss%d"), N), false);   // 14704
			SetTxt(CommWnd, FString::Printf(TEXT("Cmty_Who_PtyStat%d"), N), FString());       // 14703
		}
		return;
	}
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("Cmty_Who_InPartyIcon%d"), N), true);        // 14702
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("Cmty_Who_PtyBoss%d"), N), Rec->bBoss());    // 14704
	SetTxt(CommWnd, FString::Printf(TEXT("Cmty_Who_PtyStat%d"), N),                           // 14703
		Loc(Rec->bBoss() ? kLocPartyBoss : kLocPartyMember));   // 9139 глава / 9484 член
}

// ═══════════════════════════ ДЕЙСТВИЯ ═════════════════════════════════════════════════════════

// ★ Исправлено по аудиту 31.08.2026: в эталоне ветки 0x390B и 0x390D падают в общий хвост
//   `LABEL_4: uiCommunityWhoRefreshList(v9); return;` (game.c:505474/505488-505489), то есть
//   перерисовка идёт ДАЖЕ КОГДА страница не изменилась. Порт вызывал её только внутри `if`.
void UAsdaCommunityWhoSubsystem::PageNext()
{
	// Порт ветки 0x390B = 14603 (game.c:505467-505474).
	if (Page < TotalPages - 1) { ++Page; SelRow = -1; Sel.Hide(); }
	RefreshList();
}

void UAsdaCommunityWhoSubsystem::PageLast()  { Page = TotalPages - 1; SelRow = -1; Sel.Hide(); RefreshList(); }   // 0x390C game.c:505475-505480
void UAsdaCommunityWhoSubsystem::PagePrev()  { if (Page > 0) { --Page; SelRow = -1; Sel.Hide(); } RefreshList(); } // 0x390D game.c:505481-505490
void UAsdaCommunityWhoSubsystem::PageFirst() { Page = 0; SelRow = -1; Sel.Hide(); RefreshList(); }                 // 0x390E game.c:505491-505496

void UAsdaCommunityWhoSubsystem::SelectRow(int32 Row)
{
	// ★★★ ЗАКРЫТ ОТКРЫТЫЙ ВОПРОС АУДИТА (W-«семантика сообщения 0xB»: клик или наведение?).
	//   ДОКАЗАНО — это НАЖАТИЕ ЛЕВОЙ КНОПКИ по контролу, а не наведение. Имена эталонных
	//   обработчиков в этой семье ВРУТ (закон «миснеймы»), доказательство прямое:
	//     · `CControl_OnLButtonDblClick@0x12DC190` (ui_core.c:107474-107489) поднимает событие
	//       РОВНО 11 (`CControl_RaiseEvent(this, a2, 11, 0)`) и делает это только при состоянии
	//       мыши `GetStateByteAt(mgr,0) == 1` и `…(1) != 1`, `…(2) != 1` — то есть при ЗАЖАТОЙ
	//       левой кнопке; затем взводит флаг 0x400, БЕЗ которого `CControl_OnLButtonDown`
	//       (события 5/6) вообще не срабатывает, а `CControl_OnLButtonClick` (событие 7)
	//       гасит 0x400 при ОТПУСКАНИИ (`GetStateByteAt(mgr,0) == 0`);
	//     · наведение — это ДРУГИЕ коды: кадровый обход планов (ui_core.c:107764-107812) при
	//       попадании курсора зовёт «OnRButtonDown» (код 8 — вход в план), «OnRButtonUp» (код 9 —
	//       курсор над планом), а при промахе «OnRButtonClick» (код 10 — уход). Именно поэтому
	//       у окна экипировки 8 = собрать подсказку, 9 = `Tooltip_LayoutNearCursor`,
	//       0xA = погасить подсказку (ui_core.c:34246-34253), а 0xB = взять предмет.
	//   ⇒ `uiFriendListMsgProc@0xEBEB20` case 0xB → `uiSlotHoverHighlight` = ВЫБОР СТРОКИ ПО
	//     НАЖАТИЮ. Порт трактовал так и до аудита; уверенность поднята с MED до HIGH.
	//     Единственное остаточное различие: `UButton::OnClicked` в UMG приходит на ОТПУСКАНИИ,
	//     а не на нажатии — разница видна только при «нажал и увёл курсор».
	// Порт `uiSlotHoverHighlight@0xEBEBC0` (game.c:505315-505449). Эталон — жёсткий switch по
	//   IDNum подложек 14707/14717/…/14767, и в КАЖДОЙ ветке гейт один и тот же:
	//     `if ( !CControl_IsVisible((CControl *)self[8*i + 16]) ) goto LABEL_22;`  // 14701 CMTY_Who_INFO{n}
	//   то есть строка выбирается, только если у неё ВИДИМА подложка INFO, а её видимость ставит
	//   ровно гейт отрисовки `userNum >= 0 && job1 <= 0x21` (game.c:505242/505246 против :505275).
	//   ★ Исправлено по аудиту 31.08.2026: порт проверял только наличие записи в массиве и потому
	//     выбирал строку, которую эталон очистил из-за кода профессии > 0x21.
	if (Row < 0 || Row >= kRows) { return; }
	// ★ ДЕФЕКТ ОРИГИНАЛА D-W12 (★★, найден при переразборе 02.09.2026): в ветке LABEL_22
	//   (game.c:505441-505442) эталон ТОЛЬКО гасит рамку — `CControl_SetVisible(self[13], 0)` —
	//   и НЕ сбрасывает `self[74]`. То есть после клика по пустой строке рамки на экране нет,
	//   а «выбранной» остаётся ПРЕЖНЯЯ строка: кнопки 14607 «В отряд» и 14608 «Шепот» сработают
	//   по невидимому выбору. Мы снимаем выбор — иначе действие уходит не туда, куда показывает UI.
	//   Ключа отката нет: воспроизводить «действие по невидимой строке» незачем.
	SelRow = IsRowLive(kRows * Page + Row) ? Row : -1;
	RefreshList();
}

void UAsdaCommunityWhoSubsystem::ActionInviteParty()
{
	// Порт ветки 0x390F = 14607 «Пригласить в ОТРЯД» (а НЕ «в приятели»).
	if (SelRow < 0) { return; }                       // эталон: `if (this[74] == −1) return;`
	const int32 Abs = kRows * Page + SelRow;
	// ★ ИСПРАВЛЕНО 02.09.2026 (пустой выбор). Эталон дальше границ и «живости» записи не смотрит:
	//   он сразу берёт имя по `35*(SelRow + 7*Page) + 303` (social.c:9256/9268). Проверяем тем же
	//   признаком, что и отрисовка: строка на экране пуста ⇒ приглашать некого.
	if (!IsRowLive(Abs))
	{
		UE_LOG(LogAsdaWho, Warning,
			TEXT("[кто] «В отряд»: запись %d пуста или вне списка (%d) — эталон здесь прочитал бы мусор"),
			Abs, Entries.Num());
		return;
	}
	const FAsdaWhoRecord& R = Entries[Abs];
	const FString Name = R.Name();
	const int32   UserNum = R.UserNum();               // цель эталон берёт из записи (`sub_920EA4`)
	if (Name.IsEmpty())
	{
		// Сервер имя всегда заполняет (`memcpy(&Src[35*i+3], &user->gap_16B[4], 0x14)`,
		//   GameServer.c:127917), поэтому пустое имя = порча данных, а не штатный случай.
		//   Молча пропускаем: окно приглашения 2250 без имени бессмысленно.
		UE_LOG(LogAsdaWho, Warning, TEXT("[кто] «В отряд»: у записи %d пустое имя — приглашение отменено"), Abs);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	if (!Party)
	{
		UE_LOG(LogAsdaWho, Warning, TEXT("[кто] приглашение в отряд: подсистема отряда недоступна"));
		return;
	}

	// ══════ ПОРЯДОК ГЕЙТОВ. ★★★ ИСПРАВЛЕНО ПО АУДИТУ 31.08.2026 ═══════════════════════════════
	//   БЫЛО: порт печатал нотис 1089 ЗДЕСЬ, ДО вызова `OpenInviteWindow`. Это ломало порядок:
	//   эталон `friendListCmdHandler@0xEBF0E0`, ветка 0x390F (game.c:505497-505537) проверяет
	//     1) `sub_925A3F(g_CharManager)` → нотис 1056 (game.c:505503/505525-505531);
	//     2) `sub_924E28(g_CharManager)` → нотис 1045 (game.c:505505/505533 → :505507-505511);
	//     3) и ТОЛЬКО потом `Party_IsMeBoss` → иначе нотис 1089 (game.c:505513-505521).
	//   При одновременном «моя заявка в полёте» + «я в отряде не глава» эталон печатает 1056,
	//   а порт печатал 1089. Кроме того, проверка была ДУБЛЁМ: ровно эти три гейта в том же
	//   порядке уже есть в `UAsdaPartySubsystem::OpenInviteWindow` (гейты 5/6/7,
	//   AsdaPartySubsystem.cpp:633-655). Теперь просто отдаём решение ей.
	//
	// ★★★ ПРОБЕЛ ЗАКРЫТ (было «предикаты не разобраны»). `sub_925A3F` и `sub_924E28` — ТАНКИ:
	//     `sub_925A3F` = `jmp 0xB9B230` = `get_o2418E0` → `return *(this + 2365664)`  (game.c:219604-219607)
	//     `sub_924E28` = `jmp 0xB9B260` = `get_o2418E1` → `return *(this + 2365665)`  (game.c:219611-219614)
	//   то есть два байта-флага `CCharManager` +0x2418E0 / +0x2418E1 (оба обнуляются в ctor,
	//   game.c:52867-52868). Их сеттеры — `set_o2418E0@0xB98820` / `set_o2418E1@0xB98850`
	//   (game.c:218713-218726), они же `sub_923EAB` / `sub_9296DF`. Смысл снят с таймаутов:
	//     · +0x2418E1 ставится в 1 при ПРИХОДЕ входящей заявки (`Recv_NewPartyRequest`
	//       game.c:429700, `Recv_GM_INVITEPARTY_REQUEST_RES` game.c:429741) и снимается по
	//       таймауту 0x7530 = 30 000 мс (`partyInviteTimeoutTick@0xB9B070`, game.c:219562-219583)
	//       ⇒ «у меня висит ВХОДЯЩАЯ заявка» → нотис 1045;
	//     · +0x2418E0 снимается по таймауту 0x7D00 = 32 000 мс (game.c:219589-219597) и
	//       обнуляется при получении ответа (game.c:429686) ⇒ «МОЯ заявка в полёте» → нотис 1056.
	//   Ровно так они и названы в порту: `bMyRequestActive` / `bIncomingPending`
	//   (AsdaPartySubsystem.cpp:633-648). Уверенность HIGH.
	//
	// Порт `partyUi_ShowInviteRequest(g_pPartyWnd, имя, …)` — окно приглашения 2250.
	//   ★ Третий аргумент эталона — НЕ «пропустить гейт»: это `byte_2BE0540` (мой id отряда) в
	//     ветке «я в отряде» (game.c:505515) и `-1` в ветке «отряда нет» (game.c:505535), то есть
	//     «позвать в существующий отряд» против «создать новый». В порту эту развилку
	//     `OpenInviteWindow` берёт из своего же состояния (`bInParty && bIsLeader`,
	//     AsdaPartySubsystem.cpp:675) — эквивалентно.
	//   ★ Гейт присутствия цели в реестре соседей пропускаем осознанно: список «Кто?» строит
	//     СЕРВЕР, обходя таблицу пользователей своей карты на 550 слотов (`sub_523E50`,
	//     GameServer.c:110841-110847; менеджер у каждой карты свой — поправка 27.09.2026 в шапке .h):
	//     игрок на той же карте, но вне зоны видимости клиента (AoI), в реестре соседей не числится.
	const bool bOk = Party->OpenInviteWindow(UserNum, Name, /*bSkipPresenceGate=*/true);
	UE_LOG(LogAsdaWho, Log, TEXT("[кто] приглашение в отряд «%s» (userNum=%d): %s"),
		*Name, UserNum, bOk ? TEXT("окно 2250 открыто") : TEXT("отказ гейтов отряда"));
}

void UAsdaCommunityWhoSubsystem::ActionWhisper()
{
	// Порт ветки 0x3910 = 14608 (game.c:505538-505548): `Chat_BeginWhisperTo(g_pChatWnd, имя)`.
	//   ★★★ ДЕФЕКТ ОРИГИНАЛА D-W8 (см. ключ отката вверху файла): гейт эталона —
	//     `v8 = SelRow + 7*Page; if ( v8 >= 0 )`, а НЕ `SelRow != −1`. Дефолт = как в оригинале.
	const int32 Abs = (CVarFixWhisperNoSelection.GetValueOnGameThread() != 0)
		? ((SelRow < 0) ? -1 : kRows * Page + SelRow)   // исправление: без выбора — ничего
		: (SelRow + kRows * Page);                      // 1:1 эталон game.c:505539
	if (Abs < 0) { return; }                            // 1:1 `if ( v8 >= 0 )` game.c:505540
	// ★ Эталон дальше границ массива не проверяет и читает имя по `35*v8 + 303` (game.c:505545)
	//   даже за пределами заполненных записей. Мы обязаны остановиться — иначе чтение мусора.
	//   ★ ИСПРАВЛЕНО 02.09.2026: проверялось только `IsValidIndex`, поэтому шёпот уходил и по
	//     строке, которую отрисовка ОЧИСТИЛА (гейт `job1 <= 0x21`) — на экране пусто, а шепчем.
	//     Теперь признак тот же, что у отрисовки и у выбора строки.
	if (!IsRowLive(Abs))
	{
		UE_LOG(LogAsdaWho, Warning,
			TEXT("[кто] «Шепот»: запись %d пуста или вне списка (%d) — эталон здесь прочитал бы мусор (D-W8)"),
			Abs, Entries.Num());
		return;
	}
	if (SelRow < 0)
	{
		UE_LOG(LogAsdaWho, Warning,
			TEXT("[кто] «Шепот» БЕЗ ВЫБОРА строки: 1:1 дефект D-W8 — шепчем записи %d ")
			TEXT("(последняя на предыдущей странице). Отключается asda.Community.Who.FixWhisperNoSelection 1"),
			Abs);
	}
	UGameInstance* GI = GetGameInstance();
	if (UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		Chat->BeginWhisperTo(Entries[Abs].Name());
	}
}

// ═══════════════════════════ ПРИЁМ СЕТИ ═══════════════════════════════════════════════════════

// ★★★ ДЕФЕКТ ОРИГИНАЛА D-W10 — ПРОПУЩЕННЫЙ `break` У ОПКОДА 7006 (аудит закрыт 31.08.2026).
//   ВОПРОС АУДИТА: «провал ветки 7006 в `LABEL_928: communityWhoAppendListFromPacket`
//   (game.c:14997-15003) — реальный путь или артефакт декомпилятора?»
//   ОТВЕТ: ПУТЬ РЕАЛЬНЫЙ, доказано дизасмом живого бинаря `AsdaStory.exe`.
//     · диспетчер `Net_RecvPacketDispatch_Loop@0x96AD30`: `00 96 B5 12  cmp [ebp-0x1208], 0x1B5E`
//       (0x1B5E = 7006) → `je 0x96F6A5`;
//     · по адресу 0x96F6A5 лежит ПРЯМОЛИНЕЙНЫЙ код БЕЗ переходов между вызовами:
//         0096F6A5  lea eax,[ebp-0x1008]; push eax; mov ecx,0x2C2BF00; call 0x92CD35 (= jmp 0xDA8BD0
//                   `netHandlePlayerInfoPacket`)
//         0096F6B6  lea eax,[ebp-0x1008]; push eax; mov ecx,0x2C71B80 (= g_pCommunityWhoWnd);
//                   call 0x9297F7 (= jmp 0xEBFC10 `communityWhoAppendListFromPacket`)
//         0096F6C7  jmp 0x96FA0B  (продолжение цикла)
//     · это НЕ склейка хвостов декомпилятором: запись таблицы переходов для соседнего опкода
//       0x1569 (5481) указывает РОВНО на 0x96F6B6 (найдено сканом .text по литералу адреса),
//       то есть тело 5481 — вторая половина блока 7006. Значит в исходнике между `case 7006:`
//       и `case 5481:` нет `break`.
//   ПОЧЕМУ ЭТО ДЕФЕКТ, А НЕ ЗАМЫСЕЛ. 7006 к вкладке «Кто?» отношения не имеет: сервер шлёт его
//   ПАРОЙ к 5479 из `Send_TargetChar_Information@0x549D10`
//   (`sub_4C28F0(…, 5479, 121, …)` и сразу `sub_4C28F0(…, 7006, 631, …)`) — это вторая половина
//   ответа «сведения о чужом персонаже» для окна `OTHERCHARA_DATA` 4200. А приёмник списка
//   считает записи как `(size − 9) / 35` (game.c:505705), то есть на теле 631 б он допишет в
//   список «Кто?» ДО 18 мусорных строк, пока не наткнётся на −1 в полях `+28`/`+30`
//   (сторожа, которые сервер ставит только в НАСТОЯЩЕМ 5481, см. `sub_EBFE40@0xEBFE40`).
//   ⇒ в оригинале просмотр чужого персонажа засоряет список «Кто?».
//   ЛЕЧЕНИЕ (закон `fix-original-defects`): опкод 7006 в список «Кто?» НЕ ЗАВОДИМ ВОВСЕ.
//   Ключа отката не делаем: воспроизводить порчу списка нечем и незачем. Легальную половину
//   7006 (вторая часть сведений о персонаже) обязано разобрать окно 4200, когда его портируют, —
//   и оно НЕ ДОЛЖНО дёргать эту подсистему.

// 5481 — порция результата. Порт `communityWhoAppendListFromPacket@0xEBFC10`.
void UAsdaCommunityWhoSubsystem::OnWhoResult(const TArray<uint8>& Body)
{
	// ★★★ ИСПРАВЛЕНО 02.09.2026: здесь стоял ДОГАДОЧНЫЙ сдвиг «если длина не кратна 35, а
	//   (len − 9) кратна — читаю с +9, как эталон». Эталонное `+9` — это ШАПКА СЫРОГО ПАКЕТА
	//   (`a2a = (char *)a2 + 9`, social.c:9291), а до нас тело доезжает УЖЕ БЕЗ неё:
	//     · `UAsdaNetSubsystem` разбирает 5481 с нулевого смещения (`AsdaNetSubsystem.cpp:8793-8801`)
	//       и шлёт делегату ровно `Pkt.Body` (:8806);
	//     · сервер кладёт в тело ТОЛЬКО записи по 35 б и длину `v9`, кратную 35
	//       (`Send_WhoCommand@0x54DB40`, GameServer.c:127929-127943 — `memcpy(&v12[v9], …, 0x23)`,
	//       `v9 += 35`, порция сбрасывается при `v9 > 0x267`, то есть максимум 630 б = 18 записей).
	//   ⇒ ветка не могла сработать штатно и при некратной длине УВОДИЛА бы разбор на 9 байт.
	//   Некратный хвост теперь просто не читается, а факт пишется в лог.
	if ((Body.Num() % FAsdaWhoRecord::kSize) != 0)
	{
		UE_LOG(LogAsdaWho, Warning,
			TEXT("[кто] 5481: длина тела %d не кратна %d — неполный хвост отброшен"),
			Body.Num(), FAsdaWhoRecord::kSize);
	}

	int32 Added = 0;
	for (int32 Ofs = 0; Ofs + FAsdaWhoRecord::kSize <= Body.Num(); Ofs += FAsdaWhoRecord::kSize)
	{
		FAsdaWhoRecord R;
		FMemory::Memcpy(R.B, Body.GetData() + Ofs, FAsdaWhoRecord::kSize);
		// Маркер конца списка: userIndex == −1 ИЛИ charId == −1 (эталон останавливается на нём).
		if (R.UserNum() == -1 || R.CharId() == -1) { break; }

		// ★★★ ЗАЩИТА ОТ D-W1: сервер отдаёт до 42 записей, клиентский массив эталона — 40, и запись
		//   шла без проверки границ. Здесь длина зажата: лишнее отбрасываем с пометкой в лог.
		if (Entries.Num() >= kMaxEntries)
		{
			UE_LOG(LogAsdaWho, Warning,
				TEXT("[кто] 5481: получено больше %d записей — хвост отброшен (защита от D-W1)"), kMaxEntries);
			break;
		}
		Entries.Add(R);
		++Added;
	}
	UE_LOG(LogAsdaWho, Log, TEXT("[кто] 5481: принято %d, всего %d"), Added, Entries.Num());
	// Порции идут одна за другой; страницы пересчитывает 5482, но список рисуем сразу —
	//   так первая порция видна не дожидаясь маркера конца.
	UpdatePaging();
	RefreshList();
}

// 5482 — «конец списка»: тело ПУСТОЕ, эталон только пересчитывает страницы и перерисовывает.
void UAsdaCommunityWhoSubsystem::OnWhoNotice(const TArray<uint8>& /*Body*/)
{
	UpdatePaging();
	UE_LOG(LogAsdaWho, Log, TEXT("[кто] 5482: конец списка, записей %d, страниц %d"), Entries.Num(), TotalPages);
	RefreshList();
}

// ═══════════════════════════ УТИЛИТЫ ══════════════════════════════════════════════════════════

FString UAsdaCommunityWhoSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// ★★★ Эталонный гейт «строка живая» одной строкой — разбор и мотивация в объявлении (.h).
bool UAsdaCommunityWhoSubsystem::IsRowLive(int32 AbsIndex) const
{
	return Entries.IsValidIndex(AbsIndex)
		&& Entries[AbsIndex].UserNum() >= 0        // [28..29] i16, −1 = запись пустая
		&& Entries[AbsIndex].Job1() <= 0x21;       // [1] код класса, эталонный предел 0x21
}

FString UAsdaCommunityWhoSubsystem::DumpState() const
{
	return FString::Printf(TEXT("записей=%d страница=%d/%d выбор=%d вкладка=%s"),
		Entries.Num(), Page + 1, TotalPages, SelRow, bTabVisible ? TEXT("видна") : TEXT("скрыта"));
}
