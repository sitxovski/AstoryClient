// Copyright (c) 2026 @sitxovski (Discord)
// AsdaChatContextMenu.cpp — ЭТАП 3 окна чата CHAT_NEW (2500):
//   • контекстное меню по нику          — окно `CHAT_NEW_TABX`  IDNum **80000**
//   • контекстное меню панели вкладок    — окно `CHAT_SETUP_Win` IDNum **2670**
//   • подсветка ника в ленте 2521 при наведении
//   • игнор-лист («Блокировать чат»)
//   • лимит длины сообщения как ПАРАМЕТР
//
// ═════════════════════════════ ИСТОЧНИК ИСТИНЫ ════════════════════════════════════════════════
// ПОЛНЫЙ РЕВЕРС (адреса, строки, цифры): `Reports/chat_context_menu_and_limits.md`
// (агент `chatre-agent`, 2026-08-05, эталон = сборка IDB 13 344 768 б).
// Здесь НИЧЕГО не додумано: каждое число ниже имеет ссылку на эталон или на gamemain_jui.ini.
//
// ─────────────────────────── 1. МЕНЮ ПО НИКУ (80000) ──────────────────────────────────────────
// ОТКРЫТИЕ (`ui_core.c:12505-12509`, обработчик окна `uiChatWindowMsgProc`):
//     case 0xFu:                                        // 15 = ПКМ клик-отпускание
//       if ( ctrlId == 2501 ) uiWindowShowHideAndRegister(pThis, 1);          // вкладки → 2670
//       if ( ctrlId == 2521 && strlen(g_szParsedNameBuf) )
//           uiShowFriendNotice(pThis, g_szParsedNameBuf);                     // лента  → 80000
// ★ УСЛОВИЕ РОВНО ОДНО: под курсором строка, в которой распознан ник. Проверок «свой/чужой»,
//   «онлайн/оффлайн», «в отряде/в клане» в эталоне НЕТ — себя заблокировать разрешено.
//
// ПОКАЗ (`uiShowFriendNotice @0xBB6FD0`, ui_core.c:13559-13575):
//     copyPair79(g_pWindowMgr, v3);                              // позиция курсора
//     CControl_SetField74_75(this[425], v3[0] + 32, v3[1] - 81); // ★ окно 80000 в (кX+32, кY−81)
//     CControl_SetVisible(this[425], 1);
//     CControl_SetText(this[426], Str);                          // 80001 := ник
//     v2 = UI_FormatLocalizedNotice(ctx, strSetContains(this,Str) ? 111944 : 111943, 0, 1);
//     CWidget_SetTextAll4(this[427], v2, 0);                     // подпись 80002
// ★ Регистрации «как верхнее окно» тут НЕТ (в отличие от 2670). У нас она всё же нужна: чат и
//   системный журнал — РАЗНЫЕ корневые виджеты вьюпорта, и спор решает только z слота
//   AddToViewport (тот же вывод, что для выпадающего списка каналов, Д17/SetChannelListOpen).
//
// СОСТАВ (gamemain_jui.ini:374802-374938) — ровно 5 контролов, ни одним больше:
//   idc                  idnum  скин            Origin   Size     назначение
//   CHAT_NEW_TABX        80000  Popup_SmWin      0 0     158 96   окно
//   CHAT_NEW_TABX_T1     80001  NoImage          0 8     158 15   заголовок = ИМЯ (TextColor FFFFFFFF, align 5)
//   CHAT_NEW_TABX_B1     80002  SmWtBtn_W143     8 27    143 20   Блокировать / Разблокировать чат
//   CHAT_NEW_TABX_B2     80003  SmWtBtn_W143     8 46    143 20   Шепот
//   CHAT_NEW_TABX_B3     80004  SmGyBtn_W143     8 0     143 20   Закрыть (AlignYPos "8 0 -28" = НИЗ − 28 ⇒ y 68)
//   CHAT_NEW_TABX_move   80009  NoImage          0 0     158 25   зона перетаскивания (EVS: EVT 6 → RET 20003)
//
// ДЕЙСТВИЯ (`ui_core.c:12359-12386`, msgCode 0x10401). Общее для всех трёх пунктов —
//   `csoundPlayName2DStore(g_pSoundMgr, 5, 0)` (ЗВУК №5), затем разбор ctrlId:
//   80002: strSetContains ? mapEraseByKeyName : chatHistoryAddLine  → ТОГГЛ игнора; закрыть.
//          ★ ОПКОД НЕ ШЛЁТСЯ. Операция чисто клиентская, над списком в памяти.
//   80003: CChat+0x9E4 := ник (адресат шёпота); CChat+0x1A4 := 7 (режим ввода);
//          в поле 2609 подставляется loc 5323 «[Шепчемся с персонажем %s] »; закрыть.
//          ★ ОПКОД НЕ ШЛЁТСЯ — само сообщение уйдёт позже 5088 из ChatInput_ParseWhisper.
//   80004: только закрыть (`sub_93393C` → `sub_BB70D0` → `CControl_SetVisible(this[425], 0)`).
//   ★ ЗАКРЫТИЕ ТАКЖЕ по ЛЕВОМУ нажатию на ленту 2521 (`case 0xB`, ui_core.c:12478).
//     Больше НИЧТО меню не закрывает — клик мимо окна его НЕ гасит (проверено: `sub_93393C`
//     вызывается ровно в 4 местах).
//     ★ 07.11.2026 [ИСПРАВЛЕНИЕ] требование владельца: щелчок мимо меню гасит и 80000, и 2670 (`asda.UI.ClickAwayMenu`,
//     разбор — `AsdaWindowSubsystem_TopPress.cpp`). Ключ 0 возвращает поведение оригинала для 80000.
//
// ─────────────────────────── 2. ИГНОР-ЛИСТ ───────────────────────────────────────────────────
// Контейнер `CChat+0x738`, элемент `char[128]`. Операции: `chatHistoryAddLine @0xBB71E0`
//   (★ имя из прежнего RE ВВОДИТ В ЗАБЛУЖДЕНИЕ — это ДОБАВЛЕНИЕ В ИГНОР, а не история строк),
//   `strSetContains @0xBB7100`, `mapEraseByKeyName @0xBB7240`.
// ★ ЛИМИТА НЕТ · В ФАЙЛ НЕ СОХРАНЯЕТСЯ · НА СЕРВЕР НЕ УХОДИТ (в GameServer нет ни одного
//   опкода «заблокировать игрока»; grep ignore/blocklist/black_list — 0 совпадений).
// ★★★ ФИЛЬТРУЕТ ТОЛЬКО ДВА ОПКОДА:
//   5085 `Recv_ChatNormal @0xBA6AD0` (network.c:10404) — строка НЕ добавляется И облачко над
//        головой НЕ рисуется (весь блок лежит в ветке `else`);
//   6561 `Recv_WhisperOrSystemMessage @0xBA8560` (network.c:10876) — строка не добавляется.
//   В 5087 / 5089 / 6719 / 5106 / 4121 вызова `strSetContains` НЕТ ⇒ глобальный чат, шёпот,
//   клан и отряд «Блокировать чат» НЕ глушит. Это ОРИГИНАЛ. Улучшать запрещено.
//
// ─────────────────────────── 3. HOVER НИКА В ЛЕНТЕ 2521 ──────────────────────────────────────
// Всё живёт ВНУТРИ функции РЕНДЕРА списка `uiListWidgetRender @0x12ECC50` — то есть разбор
// разметки и запись `g_szParsedNameBuf` идут КАЖДЫЙ КАДР для строки под курсором (ui_core.c:113095):
//     if ( helper_getField91(this) == 2521 && i == *(this + 326) )   // ТОЛЬКО лента и ТОЛЬКО hover
//     {   … ник = текст между ПЕРВЫМИ '[' и ']';  plainText = всё, что было ДО '['  …
//         SetRenderState(60, -1762594576);                       // ★ 0x96F0F0F0
//         left   = item[+3112] + textFontMeasureMarkupString(plainText);
//         right  = textFontMeasureMarkupString(tagText) + 44;    // ★ 44 — ЗАШИТАЯ КОНСТАНТА
//     }
// Плюс общая подсветка строки (ui_core.c:112969): hover = тот же 0x96F0F0F0,
//   выбранная строка = `~item[+2760]` (для обычных строк поле 0 ⇒ ~0 = 0xFFFFFFFF, СПЛОШНАЯ БЕЛАЯ),
//   и если строка выбрана И под курсором — ПОБЕЖДАЕТ ВЫДЕЛЕНИЕ (второй SetRenderState идёт после).
//   Текст выбранной строки красится цветом `item[+2760] | 0xFF000000` (для обычных — ЧЁРНЫЙ).
// ★ ЧЕГО В HOVER НЕТ (проверено сплошным просмотром `uiListWidgetRender`): подчёркивания,
//   смены цвета текста, смены курсора.
// ★ ГРАБЛЯ ОРИГИНАЛА, ВОСПРОИЗВОДИМАЯ 1:1: у глобального чата с меткой (5087, hdr[1] = 1/2) и у
//   клан-чата (6719) в ПЕРВЫХ скобках стоит метка/клан-тег, а не имя ⇒ меню откроется, но
//   заголовок покажет метку и «Блокировать чат» добавит в игнор именно её. Это эталон.
//
// ─────────────────────────── 4. МЕНЮ ПАНЕЛИ ВКЛАДОК (2670) ───────────────────────────────────
// `uiWindowShowHideAndRegister @0xBA0360` (ui_core.c:11548):
//     CControl_SetVisible(this[56], 1); copyPair79(g_pWindowMgr, v3);
//     CControl_SetField74_75(this[56], v3[0], v3[1]);   // ★ РОВНО в точку курсора, БЕЗ смещения
//     return CClass_MapInsert(g_pWindowMgr, this[56]);  // ★ регистрация как ВЕРХНЕЕ окно
// Состав (gamemain_jui.ini:97652-97780), Size «158 106», скин Popup_SmWin:
//   CHAT_SETUP_T1     2671  SmWtBtn_W143     8 13   loc 5301 «Настройки вкладки чата»
//   CHAT_SETUP_T4     2674  SmGyBtn_W143_2   8 34   loc 5313 «Изменить заголовок»
//   CHAT_SETUP_T2     2672  SmWtBtn_W143     8 55   loc 5314 «Добавить вкладку чата»
//   CHAT_SETUP_T3     2673  SmGyBtn_W143_2   8 −1   loc 5315 «Удалить вкладку» (AlignYPos "8 0 -29" ⇒ y 77)
//   CHAT_SETUP_T_Move 2675  NoImage          0 0    158×14, зона перетаскивания (EVT 6 → RET 20003)
// Тексты ставит `CChat_BuildTabUI` (chat.c:864-886) — именно этими loc-id и именно в этом порядке.
// Действия (chat.c:2095-2120, chatWnd_OnCommand): см. комментарии у OnTabMenuCommand ниже.
#include "UI/AsdaChatWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaNetSubsystem.h"   // ★ 27.09.2026: префикс «(Друг)» клан-чата (GetClanChatSoulPrefix) для лимита режима 6
#include "Login/AsdaLoginData.h"   // ★ С3 08.10.2026: WireTextByteLen — лимит префикс+текст в байтах провода
#include "UI/AsdaUiHelpers.h"   // ★ D1: единая sRGB→linear конверсия (дедуп 2026-08-08)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaJuiSlot.h"
#include "UI/AsdaMarkup.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): атласы JUI — только через менеджер
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Components/ScrollBox.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChatMenu, Log, All);

const FName UAsdaChatWindowSubsystem::kMenuClaimId(TEXT("CHAT_NEW_CONTEXT_MENU"));

// ★★★ ОБВОДКА ТЕКСТА В ЛЕНТЕ 2521 — ВЫКЛЮЧЕНА ПО ТРЕБОВАНИЮ ЮЗЕРА (2026-08-06, вторая итерация:
//   «давай обводку уберем и просто сделаем цвет сообщения в цвет канала, без обводок»).
//   0 (умолчание) — префикс и текст сообщения цветом канала, ник белый, ОБВОДОК НЕТ;
//   1             — вариант Д21: ник белый + чёрная обводка, текст белый + обводка цветом канала.
//   Постоянно — `Config/DefaultEngine.ini` → [SystemSettings] → asda.Chat.LineOutline=1.
//   ⚠ В ЭТАЛОНЕ обводки у строк чата нет вообще (флаг списка 2521 — 0x400000 = ТЕНЬ, а не контур
//   0x800000, chat.c:806), так что значение 0 ближе к оригиналу, чем 1.
static ASDA_DEV_CVAR(int32, GAsdaChatLineOutline,
	TEXT("asda.Chat.LineOutline"), 0,
	TEXT("Строки чата: 0 = без обводки, текст цветом канала (умолчание); ")
	TEXT("1 = вариант Д21 (белый текст + обводка цветом канала, ник с чёрной обводкой)."),
	ECVF_Default);

// ══════════════════════ ЛИМИТ ДЛИНЫ СООБЩЕНИЯ — ПАРАМЕТР, НЕ КОНСТАНТА ════════════════════════
// ЭТАЛОН: `ctrlEditSetTextLength(chatNewEdit, 68)` (chat.c:829) — жёсткий стоп набора: лимит
//   кладётся в CEditBox+0xCAC и доводится до настоящего Win32-окна класса «edit» через
//   EM_LIMITTEXT (ui_core.c:116681). То есть 68 — это «нельзя набрать», а не «обрежется».
// ★ НАШЕ ЗНАЧЕНИЕ ПО УМОЛЧАНИЮ — 136 (решение юзера 2026-08-05: вдвое от эталона).
// ⚠⚠⚠ ЗАВИСИМОСТЬ ОТ СЕРВЕРА. В стоковом GameServer в ТРЁХ обработчиках чата `memcpy` в
//   стековый `char[80]` идёт ДО проверки длины:
//     5084 `Recv_NormalChatClient @0x547030` — memcpy(chatText[80], pkt+9, packetLen−10)      → до 690 б
//     5086 `recvGlobalChatClient  @0x547670` — memcpy(chatText[80], pkt+36, packetLen−10−27)  → до 663 б
//     4120 `sub_403F50            @0x403F50` — memcpy(v16[80],      pkt+37, v8−28)            → до 662 б
//   ⇒ сообщение длиннее 80 байт РОНЯЕТ GameServer. 136 безопасно ТОЛЬКО с патчем **P14**
//   (`chat_message_length_x2`, лимит сервера 160). Пока патча нет — вернуть 68 БЕЗ ПЕРЕСБОРКИ:
//       в игре:      asda.Chat.InputMaxChars 68
//       постоянно:   DefaultEngine.ini → [SystemSettings] → asda.Chat.InputMaxChars=68
// ★ Клиентские буферы при 136 НЕ трогаются: клампы send-путей 199, приём 257, строка ленты 253,
//   колонка CListBox 260, `chunkLen` шёпота 179 — всё выше 136 (отчёт §7.4).
static ASDA_DEV_CVAR(int32, CVarAsdaChatInputMaxChars,
	TEXT("asda.Chat.InputMaxChars"),
	UAsdaChatWindowSubsystem::kInputMaxCharsDefault,
	TEXT("Максимальная длина сообщения чата в символах (поле 2609).\n")
	TEXT("  68  = эталон AsdaStory (ctrlEditSetTextLength, chat.c:829) — безопасно на СТОКОВОМ сервере\n")
	TEXT(" 136  = значение порта по умолчанию; ТРЕБУЕТ серверного патча P14 (лимит 160),\n")
	TEXT("        иначе сообщение длиннее 80 байт роняет GameServer (опкоды 5084/5086/4120)"),
	ECVF_Default);

int32 UAsdaChatWindowSubsystem::GetInputMaxChars()
{
	const int32 V = CVarAsdaChatInputMaxChars.GetValueOnAnyThread();
	return FMath::Clamp(V, 1, kInputMaxCharsCeiling);
}

// ★★★ ИСКЛЮЧЕНИЕ ДЛЯ КАНАЛА АРМИИ (режим ввода 9, опкод 6718 → 6719).
//   Патч P14 поднимает лимит сервера до 160 в каналах 5084/5086/5088/5105/4120/6560, но
//   6718 сознательно НЕ трогает: у него ФИКСИРОВАННАЯ длина пакета (252, текстовое поле ровно
//   200 б через `memcpy 0xC8`) и MD5-ПОДПИСЬ по 0xE4 байт, а пересылка серверу→клиентам идёт
//   опкодом 6719 пакетом ФИКСИРОВАННОЙ длины 101 с записью `char[80]` и проверкой
//   `sub_523F00(pkt, 0x6C, idx, 1)` ровно на 108 байт вместе с подписью (§6.2/§7.5 п.2 реверса).
//   ⇒ для режима 9 клиент обязан держать СВОЙ лимит 80. Это не отступление, а требование протокола.
// ★ 27.09.2026 (СРЕДН-2 ревизии friend_gate): КЛАН-ЧАТ (режим 6, опкод 4120) при «Друг».
//   [ОРИГИНАЛ] `chatSendMessage@0xBCAA00` (chat.c:5671-5679) дописывает к тексту спереди loc 6937 «(Друг)»
//   (6 байт cp1251) УЖЕ ПОСЛЕ поля ввода: пользовательский текст ограничивает только поле 2609
//   (`ctrlEditSetTextLength(…, 68)`, chat.c:829), отдельной проверки длины у отправки нет. Эталонный
//   инвариант держится запасом: 68 + 6 = 74 ≤ 80 — пределу стокового сервера (`sub_403F50`, `v14 <= 80`,
//   длиннее — пакет молча выброшен). Поэтому при лимитах ≤ 154 (эталон 68, умолчание порта 136) поле
//   ведёт себя ровно как в оригинале — префикс сверху, ввод не урезается.
//   [ПОРТ] Лимит у нас параметр (до 199): чтобы префикс не вывел текст за предел сервера с патчем P14
//   (`MaxTextLen = 160`, `AstoryServer/Docs/GameServer/chat-message-length-x2.md`), в режиме 6 при
//   «Друг» поле ограничено `160 − длина префикса`. Иначе P14 обрезал бы хвост сообщения.
static constexpr int32 kClanChatServerTextMaxP14 = 160;   // предел текста 4120 на живом сервере (патч P14), байт
int32 UAsdaChatWindowSubsystem::GetActiveInputMaxChars() const
{
	const int32 Common = GetInputMaxChars();
	if (InputState == 9) { return FMath::Min(Common, kInputMaxCharsArmy); }
	if (InputState == 6)
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const int32 PrefixLen = Net ? Net->GetClanChatSoulPrefix().Len() : 0;   // cp1251: 1 символ = 1 байт
		if (PrefixLen > 0) { return FMath::Max(1, FMath::Min(Common, kClanChatServerTextMaxP14 - PrefixLen)); }
	}
	return Common;
}

bool UAsdaChatWindowSubsystem::IsWithinActiveInputLimit(const FString& S) const
{
	if (FAsdaLoginData::WireTextByteLen(S) > GetActiveInputMaxChars()) { return false; }
	if (InputState == 6)
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const FString Prefix = Net ? Net->GetClanChatSoulPrefix() : FString();
		// Склейка — как у `SendChatClan4120` (до кодирования); её длина на проводе ≤ пределу P14.
		if (!Prefix.IsEmpty() && FAsdaLoginData::WireTextByteLen(Prefix + S) > kClanChatServerTextMaxP14) { return false; }
	}
	return true;
}

FString UAsdaChatWindowSubsystem::ClampToActiveInputLimit(const FString& S) const
{
	if (IsWithinActiveInputLimit(S)) { return S; }
	// Ветка кодека может смениться на голове (без арабского — cp1251), поэтому прямой перебор по границам символов.
	FString Best;
	for (int32 i = 1; i <= S.Len(); ++i)
	{
		if (i < S.Len() && S[i - 1] >= 0xD800 && S[i - 1] <= 0xDBFF) { continue; }   // не рвать суррогатную пару
		const FString Head = S.Left(i);
		if (!IsWithinActiveInputLimit(Head)) { break; }
		Best = Head;
	}
	return Best;
}

void UAsdaChatWindowSubsystem::SetInputMaxChars(int32 NewLimit)
{
	const int32 V = FMath::Clamp(NewLimit, 1, kInputMaxCharsCeiling);
	CVarAsdaChatInputMaxChars->Set(V, ECVF_SetByConsole);
	UE_LOG(LogAsdaChatMenu, Log,
		TEXT("[чат-лимит] длина сообщения = %d символов (эталон %d, наше умолчание %d). ")
		TEXT("★ Больше 80 требует серверного патча P14 — иначе GameServer падает на 5084/5086/4120"),
		V, kInputMaxCharsEtalon, kInputMaxCharsDefault);
}

// ══════════════════════════════ ИГНОР-ЛИСТ ════════════════════════════════════════════════════
// `strSetContains @0xBB7100` — линейный обход контейнера с strcmp по 128-байтовым записям.
//   ★ strcmp ЧУВСТВИТЕЛЕН К РЕГИСТРУ — сравниваем так же (ESearchCase::CaseSensitive).
bool UAsdaChatWindowSubsystem::IsChatIgnored(const FString& Name) const
{
	if (Name.IsEmpty()) { return false; }
	for (const FString& S : IgnoreList)
	{
		if (S.Equals(Name, ESearchCase::CaseSensitive)) { return true; }
	}
	return false;
}

// Кнопка 80002 (`ui_core.c:12359-12363`):
//     if ( strSetContains(pThis, g_szParsedNameBuf) ) mapEraseByKeyName(pThis, g_szParsedNameBuf);
//     else                                            chatHistoryAddLine(pThis, g_szParsedNameBuf);
// ★ Лимита нет; дубликат появиться не может, потому что добавление всегда идёт через этот тоггл.
void UAsdaChatWindowSubsystem::ToggleChatIgnore(const FString& Name)
{
	if (Name.IsEmpty()) { return; }
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < IgnoreList.Num(); ++i)
	{
		if (IgnoreList[i].Equals(Name, ESearchCase::CaseSensitive)) { Found = i; break; }
	}
	if (Found != INDEX_NONE)
	{
		IgnoreList.RemoveAt(Found);
		UE_LOG(LogAsdaChatMenu, Log,
			TEXT("[чат-игнор] «%s» РАЗБЛОКИРОВАН (mapEraseByKeyName); в списке %d"), *Name, IgnoreList.Num());
	}
	else
	{
		// Эталон копирует имя в буфер char[128] и вставляет в контейнер — длину режем так же.
		IgnoreList.Add(Name.Left(127));
		UE_LOG(LogAsdaChatMenu, Log,
			TEXT("[чат-игнор] «%s» ЗАБЛОКИРОВАН (chatHistoryAddLine); в списке %d. ")
			TEXT("★ Глушит ВСЕ каналы: 5085/5087/5089/5106/4121/6561/6719 + облачко над головой ")
			TEXT("(⚠ отступление Д27 п.2; эталон фильтровал только 5085 и 6561)"),
			*Name, IgnoreList.Num());
	}
	RefreshNickMenuBlockCaption();
}

// ══════════════════ СКИНЫ КНОПОК ВСПЛЫВАЮЩИХ МЕНЮ (JuiSkin.ini) ═══════════════════════════════
// Все три скина — Type = "PushButton", один план ES=0 «Rect 0 0 143 20», атлас NE_cooltime_mini.tga,
//   ТРИ состояния: SS0 обычная / SS1 наведение / SS2 нажатие. Отличаются Diffuse-тинтом,
//   цветом подписи и (у SS2) сдвинутым на 1 px вверх UV — «кнопка вдавилась».
namespace
{
	struct FAsdaBtnSkinDef
	{
		const TCHAR* Name;
		const TCHAR* Diffuse[3];    // SS0 / SS1 / SS2 — тинт плана (sRGB!)
		const TCHAR* Caption[3];    // SS0 / SS1 / SS2 — цвет подписи (sRGB!)
	};
	// UV одинаковы у всех трёх скинов: SS0/SS1 «0.000 0.922 … 0.559 1.000», SS2 «0.000 0.918 … 0.559 0.996».
	const FBox2f kBtnUvNormal(FVector2f(0.000f, 0.922f), FVector2f(0.559f, 1.000f));
	const FBox2f kBtnUvPressed(FVector2f(0.000f, 0.918f), FVector2f(0.559f, 0.996f));

	const FAsdaBtnSkinDef kBtnSkins[] =
	{
		// «белая» кнопка — пункты 80002/80003 и 2671/2672
		{ TEXT("SmWtBtn_W143"),
		  { TEXT("FFE6E6E6"), TEXT("FFFFFFFF"), TEXT("FFC0C0C0") },
		  { TEXT("FF555555"), TEXT("FF808080"), TEXT("FF323232") } },
		// «серая» кнопка — пункт 80004 «Закрыть»
		{ TEXT("SmGyBtn_W143"),
		  { TEXT("FF969696"), TEXT("FFAFAFAF"), TEXT("FFA7A7A7") },
		  { TEXT("FF3C3C3C"), TEXT("FF646464"), TEXT("FF191919") } },
		// «жёлтая» кнопка — пункты 2673/2674 меню вкладок
		{ TEXT("SmGyBtn_W143_2"),
		  { TEXT("FFFADA6B"), TEXT("FFAFAFAF"), TEXT("FFA7A7A7") },
		  { TEXT("FF3C3C3C"), TEXT("FF646464"), TEXT("FF191919") } },
	};

	const FAsdaBtnSkinDef& FindBtnSkin(const TCHAR* Name)
	{
		for (const FAsdaBtnSkinDef& S : kBtnSkins)
		{
			if (FCString::Stricmp(S.Name, Name) == 0) { return S; }
		}
		return kBtnSkins[0];
	}

	// ── Скин окна Popup_SmWin: девять планов 3×3 из атласа NE_wheeltaget3.tga ──────────────────
	//   Rect-шаблон 36×36: углы 16×16, средние полосы 4 px (16…20). Отсюда правило раскладки:
	//     колонка 0 → x 0,      ширина 16       строка 0 → y 0,      высота 16
	//     колонка 1 → x 16,     ширина W−32     строка 1 → y 16,     высота H−32
	//     колонка 2 → x W−16,   ширина 16       строка 2 → y H−16,   высота 16
	//   UV факторизуются по колонке и строке (проверено по всем девяти планам JuiSkin.ini).
	const float kPopupU[3][2] = { { 0.828f, 0.891f }, { 0.910f, 0.918f }, { 0.938f, 1.000f } };
	const float kPopupV[3][2] = { { 0.453f, 0.516f }, { 0.535f, 0.543f }, { 0.563f, 0.625f } };

	// Атлас JUI по КОРОТКОМУ имени.
	// ★ Аудит №3 (A8/B8): было — свой `LoadObject` по полному объектному пути мимо кэша и refCount
	//   менеджера (закон `resource-subsystems`). Стало — `LoadUITexture`, который сам строит путь
	//   `/Game/AsdaAssets/Textures/JUI/<имя>.<имя>`, поэтому вызывающим достаётся только имя скина.
	UTexture2D* LoadJuiAtlas(const TCHAR* AtlasName)
	{
		UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
		return TexMgr ? TexMgr->LoadUITexture(FName(AtlasName)) : nullptr;
	}
}

// Таблицы кнопок обоих меню. Вынесены в файловую область, чтобы использоваться и при постройке,
//   и при разборе клика, и при подсветке — один источник, как того требует закон «полный разбор».
static const UAsdaChatWindowSubsystem::FAsdaPopupBtn kNickBtns[] =
{
	// Node                          idnum   loc     скин               X     Y     разметка в строке
	{ TEXT("CHAT_NEW_TABX_B1"),      80002,      0,  TEXT("SmWtBtn_W143"),  8.f, 27.f, true  },
	{ TEXT("CHAT_NEW_TABX_B2"),      80003,  30184,  TEXT("SmWtBtn_W143"),  8.f, 46.f, false },
	{ TEXT("CHAT_NEW_TABX_B3"),      80004, 110669,  TEXT("SmGyBtn_W143"),  8.f, 68.f, false },
};
static const UAsdaChatWindowSubsystem::FAsdaPopupBtn kTabBtns[] =
{
	{ TEXT("CHAT_SETUP_T1"),          2671,   5301,  TEXT("SmWtBtn_W143"),    8.f, 13.f, false },
	{ TEXT("CHAT_SETUP_T4"),          2674,   5313,  TEXT("SmGyBtn_W143_2"),  8.f, 34.f, false },
	{ TEXT("CHAT_SETUP_T2"),          2672,   5314,  TEXT("SmWtBtn_W143"),    8.f, 55.f, false },
	{ TEXT("CHAT_SETUP_T3"),          2673,   5315,  TEXT("SmGyBtn_W143_2"),  8.f, 77.f, false },
};

// sRGB «AARRGGBB» → FLinearColor (закон ui-srgb-gamma-rule).
//   ★ D1 (дедуп UI 2026-08-08): единая реализация в AsdaUi::SrgbHexToLinear — тонкий адаптер.
static FLinearColor AsdaMenuSrgbToLinear(const TCHAR* Hex)
{
	return AsdaUi::SrgbHexToLinear(FString(Hex));
}

// ══════════════════════════ ПОСТРОЕНИЕ ВСПЛЫВАЮЩЕГО ОКНА ══════════════════════════════════════
// Окно = девять планов скина Popup_SmWin + прозрачная зона перетаскивания + N кнопок (план + подпись).
//   Всё живёт в дереве виджетов WBP и потому редактируется в UMG (закон ui-editable-in-ue).
UCanvasPanel* UAsdaChatWindowSubsystem::BuildPopupWindow(const TCHAR* Name, float W, float H,
                                                         const TCHAR* MoveNode, float MoveW, float MoveH,
                                                         const FAsdaPopupBtn* Btns, int32 NumBtns)
{
	if (!ChatWidget || !ChatWidget->WidgetTree) { return nullptr; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ChatWidget->WidgetTree->RootWidget);
	if (!Root) { return nullptr; }

	UCanvasPanel* Menu = ChatWidget->WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), FName(Name));
	if (!Menu) { return nullptr; }
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(Menu))
	{
		// POINT-анкор в ЛЕВЫЙ-ВЕРХНИЙ угол экрана: меню ставится в точку курсора, а курсор мы
		//   переводим в базовые px делением на DPI-масштаб — та же арифметика, что у ресайза.
		AsdaJui::SetPointSlot(CS, 0.f, 0.f, 0.f, 0.f, W, H);
		CS->SetZOrder(kMenuZ);
	}
	Menu->SetVisibility(ESlateVisibility::Collapsed);

	UTexture2D* WinTex = LoadJuiAtlas(TEXT("ne_wheeltaget3"));
	UTexture2D* BtnTex = LoadJuiAtlas(TEXT("ne_cooltime_mini"));

	// ── Девять планов рамки/подложки ────────────────────────────────────────────────────────────
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Col = 0; Col < 3; ++Col)
		{
			const int32 Idx = Row * 3 + Col;
			UImage* P = ChatWidget->WidgetTree->ConstructWidget<UImage>(
				UImage::StaticClass(), FName(*FString::Printf(TEXT("%s_bg%d"), Name, Idx)));
			if (!P) { continue; }
			FSlateBrush Br;
			if (WinTex) { Br.SetResourceObject(WinTex); }
			Br.DrawAs = ESlateBrushDrawType::Image;
			Br.ImageSize = FVector2D(16.f, 16.f);
			Br.SetUVRegion(FBox2f(FVector2f(kPopupU[Col][0], kPopupV[Row][0]),
			                      FVector2f(kPopupU[Col][1], kPopupV[Row][1])));
			P->SetBrush(Br);
			// Планы окна мышь НЕ ловят: клики разбираются по геометрии (кнопки и зона drag'а).
			P->SetVisibility(ESlateVisibility::HitTestInvisible);
			const float X  = (Col == 0) ? 0.f : ((Col == 1) ? 16.f : W - 16.f);
			const float Wd = (Col == 1) ? FMath::Max(0.f, W - 32.f) : 16.f;
			const float Y  = (Row == 0) ? 0.f : ((Row == 1) ? 16.f : H - 16.f);
			const float Ht = (Row == 1) ? FMath::Max(0.f, H - 32.f) : 16.f;
			if (UCanvasPanelSlot* CS = Menu->AddChildToCanvas(P))
			{
				CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				CS->SetAlignment(FVector2D(0.f, 0.f));
				CS->SetOffsets(FMargin(X, Y, Wd, Ht));
				CS->SetZOrder(0);
			}
		}
	}

	// ── Зона перетаскивания (80009 / 2675): NoImage, ничего не рисует, только ловит мышь ────────
	if (UBorder* Grip = ChatWidget->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(MoveNode)))
	{
		Grip->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Grip->SetPadding(FMargin(0.f));
		Grip->SetVisibility(ESlateVisibility::Visible);
		Grip->SetCursor(EMouseCursor::CardinalCross);
		if (UCanvasPanelSlot* CS = Menu->AddChildToCanvas(Grip))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetOffsets(FMargin(0.f, 0.f, MoveW, MoveH));
			CS->SetZOrder(5);
		}
	}

	// ── Кнопки: план скина + подпись ────────────────────────────────────────────────────────────
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FSlateFontInfo F = TipFont;
	F.Size = (int32)kFontPx;
	for (int32 i = 0; i < NumBtns; ++i)
	{
		const FAsdaPopupBtn& B = Btns[i];
		const FAsdaBtnSkinDef& Sk = FindBtnSkin(B.Skin);

		UImage* Plate = ChatWidget->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(B.Node));
		if (Plate)
		{
			FSlateBrush Br;
			if (BtnTex) { Br.SetResourceObject(BtnTex); }
			Br.DrawAs = ESlateBrushDrawType::Image;
			Br.ImageSize = FVector2D(kMenuBtnW, kMenuBtnH);
			Br.SetUVRegion(kBtnUvNormal);
			Br.TintColor = FSlateColor(AsdaMenuSrgbToLinear(Sk.Diffuse[0]));
			Plate->SetBrush(Br);
			Plate->SetVisibility(ESlateVisibility::Visible);   // ловит мышь (хит-тест по геометрии)
			Plate->SetCursor(EMouseCursor::Hand);
			if (UCanvasPanelSlot* CS = Menu->AddChildToCanvas(Plate))
			{
				CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				CS->SetAlignment(FVector2D(0.f, 0.f));
				CS->SetOffsets(FMargin(B.X, B.Y, kMenuBtnW, kMenuBtnH));
				CS->SetZOrder(10);
			}
		}

		UTextBlock* Cap = ChatWidget->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("%s_Text"), B.Node)));
		if (Cap)
		{
			Cap->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // подпись мышь НЕ перехватывает
			Cap->SetAutoWrapText(false);
			Cap->SetJustification(ETextJustify::Center);                  // CaptionAlign = 5
			Cap->SetFont(F);
			Cap->SetColorAndOpacity(FSlateColor(AsdaMenuSrgbToLinear(Sk.Caption[0])));
			if (UCanvasPanelSlot* CS = Menu->AddChildToCanvas(Cap))
			{
				CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				CS->SetAlignment(FVector2D(0.f, 0.f));
				// CaptionPoint «0 −1» + вертикальная центровка подписи в кнопке 20 px (кегль ≈ 14).
				CS->SetOffsets(FMargin(B.X, B.Y + 2.f, kMenuBtnW, 16.f));
				CS->SetAutoSize(false);
				CS->SetZOrder(11);
			}
			const FString Body = (B.LocId && Txt) ? Txt->GetTextById(B.LocId) : FString();
			if (B.bMarkupCaption)
			{
				// ★ ЦВЕТ ЗАДАН РАЗМЕТКОЙ В САМОЙ loc-СТРОКЕ (111943 красная / 111944 синяя) —
				//   вручную не перекрашиваем. Текст ставится позже, RefreshNickMenuBlockCaption().
			}
			else if (!Body.IsEmpty())
			{
				Cap->SetText(FText::FromString(Body));
			}
			else if (B.LocId)
			{
				Cap->SetText(FText::FromString(FString::FromInt(B.LocId)));
			}
		}
	}

	UE_LOG(LogAsdaChatMenu, Log,
		TEXT("[чат-меню] окно «%s» построено: %.0f×%.0f, скин Popup_SmWin (9 планов), кнопок %d, зона drag %.0f×%.0f, z=%d"),
		Name, W, H, NumBtns, MoveW, MoveH, kMenuZ);
	return Menu;
}

void UAsdaChatWindowSubsystem::BuildContextMenus()
{
	if (!ChatWidget || !ChatWidget->WidgetTree) { return; }
	if (!NickMenu)
	{
		NickMenu = BuildPopupWindow(TEXT("CHAT_NEW_TABX"), kNickMenuW, kNickMenuH,
			TEXT("CHAT_NEW_TABX_move"), 158.f, 25.f, kNickBtns, UE_ARRAY_COUNT(kNickBtns));
		// Заголовок 80001 — имя игрока. Скин NoImage, Origin «0 8», Size «158 15»,
		//   TextColor FFFFFFFF, TextAlign 5 (центр). CaptionInfo «ff400040» — служебный цвет
		//   импортёра, брать его НЕЛЬЗЯ (тот же «фиолетовый текст», что чинили в ApplyEtalonTextColors).
		if (NickMenu)
		{
			if (UTextBlock* T = ChatWidget->WidgetTree->ConstructWidget<UTextBlock>(
					UTextBlock::StaticClass(), FName(TEXT("CHAT_NEW_TABX_T1"))))
			{
				FSlateFontInfo F = TipFont;
				F.Size = (int32)kFontPx;
				T->SetFont(F);
				T->SetAutoWrapText(false);
				T->SetJustification(ETextJustify::Center);
				T->SetColorAndOpacity(FSlateColor(AsdaMenuSrgbToLinear(TEXT("FFFFFFFF"))));
				T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				if (UCanvasPanelSlot* CS = NickMenu->AddChildToCanvas(T))
				{
					CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
					CS->SetAlignment(FVector2D(0.f, 0.f));
					CS->SetOffsets(FMargin(0.f, 8.f, kNickMenuW, 15.f));
					CS->SetAutoSize(false);
					CS->SetZOrder(11);
				}
			}
		}
	}
	if (!TabMenu)
	{
		TabMenu = BuildPopupWindow(TEXT("CHAT_SETUP_Win"), kTabMenuW, kTabMenuH,
			TEXT("CHAT_SETUP_T_Move"), 158.f, 14.f, kTabBtns, UE_ARRAY_COUNT(kTabBtns));
	}
}

// ══════════════════════════ ПОКАЗ / СКРЫТИЕ / ПОЗИЦИЯ ═════════════════════════════════════════

void UAsdaChatWindowSubsystem::PlaceMenu(UCanvasPanel* Menu, const FVector2D& BasePos, float W, float H)
{
	if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(Menu))
	{
		AsdaJui::SetPointSlot(CS, 0.f, 0.f, BasePos.X, BasePos.Y, W, H);
	}
}

bool UAsdaChatWindowSubsystem::GetMenuScreenRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	const UCanvasPanel* M = NickMenu && NickMenu->GetVisibility() != ESlateVisibility::Collapsed
		? NickMenu.Get()
		: (TabMenu && TabMenu->GetVisibility() != ESlateVisibility::Collapsed ? TabMenu.Get() : nullptr);
	if (!M) { return false; }
	// ★ Аудит №4 (A9/B1): тело «живой абсолютный прямоугольник виджета» — общая точка
	//   `AsdaUi::WidgetAbsRect` (она же отсеивает нулевую геометрию «ни разу не отрисован»).
	return AsdaUi::WidgetAbsRect(M, OutMin, OutMax);
}

// ★ Подпись 80002 — 1:1 `uiShowFriendNotice` (ui_core.c:13569-13574):
//     strSetContains ? loc 111944 «<FC=FF0072BC>Разблокировать чат</F>»
//                    : loc 111943 «<FC=FFFF0000>Блокировать чат</F>»
//   В ЭТАЛОНЕ ЦВЕТ ЖИВЁТ В САМОЙ LOC-СТРОКЕ (красный/синий) и рисуется разметкой.
// ⚠⚠⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (требование юзера 2026-08-06, Д27 п.6): подпись этих ДВУХ пунктов
//   делается ЧЁРНОЙ. Разметка цвета, пришедшая из данных, снимается и заменяется на FF000000.
//   Остальные пункты меню 80000/2670 не тронуты — их цвет по-прежнему из скина кнопки.
void UAsdaChatWindowSubsystem::RefreshNickMenuBlockCaption()
{
	if (!ChatWidget) { return; }
	UWidget* Cap = ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_TABX_B1_Text"));
	if (!Cap) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const int32 LocId = IsChatIgnored(RibbonHoverNick) ? 111944 : 111943;
	FString Body = Txt ? Txt->GetTextById(LocId) : FString();
	if (Body.IsEmpty()) { Body = FString::FromInt(LocId); }
	// ── Снять цветовую разметку из loc-строки ────────────────────────────────────────────────
	//   loc 111943/111944 приходят как «<FC=AARRGGBB>текст</F>». Вырезаем ВСЕ теги <FC=…> и
	//   закрывающие </F>: иначе они перебили бы наш чёрный прямо внутри строки.
	{
		FString Clean;
		Clean.Reserve(Body.Len());
		for (int32 i = 0; i < Body.Len(); )
		{
			if (Body[i] == TEXT('<'))
			{
				const int32 Close = Body.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i);
				if (Close != INDEX_NONE)
				{
					const FString Tag = Body.Mid(i, Close - i + 1);
					if (Tag.StartsWith(TEXT("<FC="), ESearchCase::IgnoreCase) || Tag.Equals(TEXT("</F>"), ESearchCase::IgnoreCase))
					{
						i = Close + 1;
						continue;
					}
				}
			}
			Clean.AppendChar(Body[i]);
			++i;
		}
		Body = Clean;
	}
	if (UWidget* New = UAsdaRichText::ApplyFormatted(Cap,
			FString::Printf(TEXT("<FS%02d><FC=FF000000>%s"), (int32)kFontPx, *Body)))
	{
		New->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UTextLayoutWidget* Tw = Cast<UTextLayoutWidget>(New))
		{
			Tw->SetJustification(ETextJustify::Center);   // CaptionAlign = 5
		}
	}
}

void UAsdaChatWindowSubsystem::ShowNickMenu(const FString& Name, const FVector2D& BasePos)
{
	if (!NickMenu || Name.IsEmpty()) { return; }
	// Одно всплывающее окно за раз: раскрытый список каналов и второе меню закрываем.
	//   (В эталоне это разные контролы одного менеджера окон; у нас — один слой «поверх всего».)
	SetChannelListOpen(false);
	HideTabMenu();

	RibbonHoverNick = Name;
	NickMenuPos = BasePos;
	PlaceMenu(NickMenu, NickMenuPos, kNickMenuW, kNickMenuH);
	if (UTextBlock* T = Cast<UTextBlock>(ChatWidget ? ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_TABX_T1")) : nullptr))
	{
		T->SetText(FText::FromString(Name));   // CControl_SetText(this[426], Str)
	}
	RefreshNickMenuBlockCaption();
	NickMenu->SetVisibility(ESlateVisibility::Visible);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			FAsdaUiZoneRect D;
			D.BindWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx) { return GetMenuScreenRect(Mn, Mx); });
			WM->PushTopmost(kMenuClaimId, kZoneId, MoveTemp(D));
		}
	}
	UE_LOG(LogAsdaChatMenu, Log,
		TEXT("[чат-меню] 80000 открыто по нику «%s» в (%.0f,%.0f) базовых px (курсор +%.0f/%.0f); в игноре: %s"),
		*Name, NickMenuPos.X, NickMenuPos.Y, kNickMenuDX, kNickMenuDY,
		IsChatIgnored(Name) ? TEXT("ДА") : TEXT("нет"));
}

void UAsdaChatWindowSubsystem::HideNickMenu()
{
	if (!NickMenu || NickMenu->GetVisibility() == ESlateVisibility::Collapsed) { return; }
	NickMenu->SetVisibility(ESlateVisibility::Collapsed);
	MenuPressedBtn = -1;
	if (MenuDragTarget == NickMenu) { MenuDragTarget = nullptr; }
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			if (!TabMenu || TabMenu->GetVisibility() == ESlateVisibility::Collapsed) { WM->PopTopmost(kMenuClaimId); }
		}
	}
	UE_LOG(LogAsdaChatMenu, Log, TEXT("[чат-меню] 80000 закрыто (sub_93393C → CControl_SetVisible(this[425], 0))"));
}

void UAsdaChatWindowSubsystem::ShowTabMenu(const FVector2D& BasePos, int32 TargetTab)
{
	if (!TabMenu) { return; }
	SetChannelListOpen(false);
	HideNickMenu();
	// ★ Цель пунктов меню — вкладка, ПО КОТОРОЙ кликнули (отступление, см. RenameTab в .h).
	//   Невалидный индекс (меню открыто по стрелке прокрутки или из консоли) ⇒ активная вкладка.
	TabMenuTargetIdx = ChatTabs.IsValidIndex(TargetTab) ? TargetTab : INDEX_NONE;
	TabMenuPos = BasePos;   // ★ БЕЗ смещения — uiWindowShowHideAndRegister ставит РОВНО в курсор
	PlaceMenu(TabMenu, TabMenuPos, kTabMenuW, kTabMenuH);
	TabMenu->SetVisibility(ESlateVisibility::Visible);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			FAsdaUiZoneRect D;
			D.BindWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx) { return GetMenuScreenRect(Mn, Mx); });
			WM->PushTopmost(kMenuClaimId, kZoneId, MoveTemp(D));
		}
	}
	UE_LOG(LogAsdaChatMenu, Log,
		TEXT("[чат-меню] 2670 открыто в (%.0f,%.0f) базовых px (эталон: точка курсора без смещения + ")
		TEXT("регистрация верхним окном); ЦЕЛЬ пунктов — вкладка %d «%s»%s"),
		TabMenuPos.X, TabMenuPos.Y, MenuTargetTab(), *TabName(MenuTargetTab()),
		TabMenuTargetIdx == INDEX_NONE ? TEXT(" (по вкладке не попали — цель активная, как в эталоне)")
		                               : TEXT(" (★ отступление: эталон всегда берёт активную)"));
}

void UAsdaChatWindowSubsystem::HideTabMenu()
{
	if (!TabMenu || TabMenu->GetVisibility() == ESlateVisibility::Collapsed) { return; }
	TabMenu->SetVisibility(ESlateVisibility::Collapsed);
	MenuPressedBtn = -1;
	if (MenuDragTarget == TabMenu) { MenuDragTarget = nullptr; }
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			if (!NickMenu || NickMenu->GetVisibility() == ESlateVisibility::Collapsed) { WM->PopTopmost(kMenuClaimId); }
		}
	}
	UE_LOG(LogAsdaChatMenu, Log, TEXT("[чат-меню] 2670 закрыто"));
}

// ══════════════════════════ СОСТОЯНИЯ СКИНА КНОПОК (SS0/SS1/SS2) ══════════════════════════════
void UAsdaChatWindowSubsystem::RefreshPopupBtnStates(UCanvasPanel* Menu, const FAsdaPopupBtn* Btns, int32 Num,
                                                     const FVector2D& Cur, bool bDown)
{
	if (!Menu || Menu->GetVisibility() == ESlateVisibility::Collapsed || !ChatWidget) { return; }
	for (int32 i = 0; i < Num; ++i)
	{
		const FAsdaPopupBtn& B = Btns[i];
		UImage* Plate = Cast<UImage>(ChatWidget->GetWidgetFromName(FName(B.Node)));
		if (!Plate) { continue; }
		const bool bHov = IsUnderCursor(Plate, Cur);
		const bool bPre = bHov && bDown && (MenuPressedBtn == B.IdNum);
		const int32 St = bPre ? 2 : (bHov ? 1 : 0);
		const FAsdaBtnSkinDef& Sk = FindBtnSkin(B.Skin);
		FSlateBrush Br = Plate->GetBrush();
		Br.SetUVRegion(St == 2 ? kBtnUvPressed : kBtnUvNormal);
		Br.TintColor = FSlateColor(AsdaMenuSrgbToLinear(Sk.Diffuse[St]));
		Plate->SetBrush(Br);
		// ★ Подпись 80002 ставится РАЗМЕТКОЙ (RefreshNickMenuBlockCaption) — её цвет НЕ трогаем ни
		//   в одном состоянии. ⚠ Д27.6: цвет из loc-строки снят, подпись принудительно ЧЁРНАЯ.
		if (!B.bMarkupCaption)
		{
			if (UTextBlock* Cap = Cast<UTextBlock>(ChatWidget->GetWidgetFromName(
					FName(*FString::Printf(TEXT("%s_Text"), B.Node)))))
			{
				Cap->SetColorAndOpacity(FSlateColor(AsdaMenuSrgbToLinear(Sk.Caption[St])));
			}
		}
	}
}

void UAsdaChatWindowSubsystem::RefreshMenuHover(const FVector2D& Cur, bool bDown)
{
	RefreshPopupBtnStates(NickMenu, kNickBtns, UE_ARRAY_COUNT(kNickBtns), Cur, bDown);
	RefreshPopupBtnStates(TabMenu,  kTabBtns,  UE_ARRAY_COUNT(kTabBtns),  Cur, bDown);
}

// ══════════════════════════ КЛИК ПО МЕНЮ ══════════════════════════════════════════════════════
// ★ НАЖАТИЕ только ЗАПОМИНАЕТ кнопку (состояние SS2 скина «кнопка вдавилась»); ДЕЙСТВИЕ
//   выполняется на ОТПУСКАНИИ над той же кнопкой — так работает PushButton в оригинале:
//   пункты меню разбираются в `case 0x10401` (WM_COMMAND), то есть по ЗАВЕРШЁННОМУ клику.
// Возвращает true, если нажатие поглощено меню — TickPointer на этом останавливается.
bool UAsdaChatWindowSubsystem::HandleMenuMouseDown(const FVector2D& Cur)
{
	if (!ChatWidget) { return false; }
	auto HitBtn = [this, &Cur](const FAsdaPopupBtn* Tab, int32 Num) -> int32
	{
		for (int32 i = 0; i < Num; ++i)
		{
			UImage* Plate = Cast<UImage>(ChatWidget->GetWidgetFromName(FName(Tab[i].Node)));
			if (IsUnderCursor(Plate, Cur)) { return Tab[i].IdNum; }
		}
		return -1;
	};

	if (NickMenu && NickMenu->GetVisibility() != ESlateVisibility::Collapsed && IsUnderCursor(NickMenu, Cur))
	{
		MenuPressedBtn = HitBtn(kNickBtns, UE_ARRAY_COUNT(kNickBtns));
		// Перетаскивание меню за зону 80009 (JUI EVS: EVT 6 → RET 20003).
		if (MenuPressedBtn < 0 && IsUnderCursor(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_TABX_move")), Cur))
		{
			MenuDragTarget = NickMenu;
			MenuDragStartMouse = Cur;
			MenuDragStartPos = NickMenuPos;
		}
		return true;   // клик внутри меню наружу не проваливается
	}
	if (TabMenu && TabMenu->GetVisibility() != ESlateVisibility::Collapsed && IsUnderCursor(TabMenu, Cur))
	{
		MenuPressedBtn = HitBtn(kTabBtns, UE_ARRAY_COUNT(kTabBtns));
		if (MenuPressedBtn < 0 && IsUnderCursor(ChatWidget->GetWidgetFromName(TEXT("CHAT_SETUP_T_Move")), Cur))
		{
			MenuDragTarget = TabMenu;
			MenuDragStartMouse = Cur;
			MenuDragStartPos = TabMenuPos;
		}
		return true;
	}
	return false;
}

// Отпускание ЛКМ: выполняем действие, если отпустили над ТОЙ ЖЕ кнопкой, где нажали.
void UAsdaChatWindowSubsystem::HandleMenuMouseUp(const FVector2D& Cur)
{
	MenuDragTarget = nullptr;
	const int32 Pressed = MenuPressedBtn;
	MenuPressedBtn = -1;
	if (Pressed < 0 || !ChatWidget) { return; }

	UGameInstance* GI = GetGameInstance();
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;

	// ── Меню по нику 80000 ──────────────────────────────────────────────────────────────────────
	if (NickMenu && NickMenu->GetVisibility() != ESlateVisibility::Collapsed)
	{
		for (const FAsdaPopupBtn& B : kNickBtns)
		{
			if (B.IdNum != Pressed) { continue; }
			UImage* Plate = Cast<UImage>(ChatWidget->GetWidgetFromName(FName(B.Node)));
			if (!IsUnderCursor(Plate, Cur)) { return; }   // увели курсор с кнопки — команды нет
			// ★ ЗВУК №5 — общий для ВСЕХ трёх пунктов (`csoundPlayName2DStore(g_pSoundMgr, 5, 0)`,
			//   ui_core.c:12358, выполняется ДО разбора ctrlId).
			if (Snd) { Snd->PlaySound2DById(5); }
			if (B.IdNum == 80002)
			{
				// ★ ТОГГЛ ИГНОРА. Опкод НЕ шлётся, окон не открывается.
				ToggleChatIgnore(RibbonHoverNick);
				HideNickMenu();
			}
			else if (B.IdNum == 80003)
			{
				// ★ ШЕПОТ. Эталон: CChat+0x9E4 := ник (адресат), CChat+0x1A4 := 7 (режим ввода),
				//   в поле 2609 подставляется префикс loc 5323, CChat+0x1CC := тот же префикс
				//   (по его длине `ChatInput_ParseWhisper(mode=2)` потом срезает начало строки).
				//   ОПКОД НЕ ШЛЁТСЯ.
				// ★ ОТСТУПЛЕНИЕ ПОРТА (действующее решение юзера, Д6): ПРИГЛАШЕНИЕ loc 5323 В ПОЛЕ
				//   НЕ ПОКАЗЫВАЕТСЯ — оно дублировало бы подпись комбобокса 2608, а адресат шёпота
				//   хранится отдельным полем `WhisperTarget` (наш аналог CChat+0x9E4) и уходит в
				//   пакет 5088 из SendCurrentChannel.
				// ★★★ УТОЧНЕНО 2026-08-30: это отступление касается ТОЛЬКО приглашения. Сам эталон
				//   в мышиных путях шёпота кладёт в поле ДРУГОЕ — литерал «/w имя »
				//   (`CChatList_OnSelectWhisper@0xDBB7C0`), и он в отправляемый текст НЕ уезжает:
				//   `ParseSlashCommand` его вырезает ровно как эталонный разбор по пробелу.
				const FString Target = RibbonHoverNick;
				HideNickMenu();
				// ★★★ Общая точка `BeginWhisperTo` (порт `CChatList_OnSelectWhisper`): в поле
				//   встаёт литерал «/w имя », поэтому выбор шёпота ВИДЕН. Отправку это не портит:
				//   `ParseSlashCommand` вырезает «/w имя » и ставит режим 7 с этим адресатом.
				BeginWhisperTo(Target);
				UE_LOG(LogAsdaChatMenu, Log,
					TEXT("[чат-меню] 80003 «Шепот»: режим ввода 7, адресат «%s» (опкод НЕ шлётся — уйдёт 5088 при Enter)"),
					*Target);
			}
			else if (B.IdNum == 80004)
			{
				// 80004 «Закрыть» — только скрыть окно (`sub_93393C`, ui_core.c:12385).
				HideNickMenu();
			}
			return;
		}
	}

	// ── Меню панели вкладок 2670 ────────────────────────────────────────────────────────────────
	if (TabMenu && TabMenu->GetVisibility() != ESlateVisibility::Collapsed)
	{
		for (const FAsdaPopupBtn& B : kTabBtns)
		{
			if (B.IdNum != Pressed) { continue; }
			UImage* Plate = Cast<UImage>(ChatWidget->GetWidgetFromName(FName(B.Node)));
			if (!IsUnderCursor(Plate, Cur)) { return; }
			if (Snd) { Snd->PlaySound2DById(5); }
			// ★★★ ЧТО ДЕЛАЕТ КАЖДЫЙ ПУНКТ В ЭТАЛОНЕ (chat.c:2095-2120, chatWnd_OnCommand).
			//   ОБЩЕЕ ДЛЯ ВСЕХ ЧЕТЫРЁХ: `CControl_SetVisible(self[56], 0)` — окно 2670 гаснет.
			//   Эта часть воспроизведена 1:1. Дальше каждый пункт открывает СВОЁ окно:
			//     2671 (0xA6F) → CControl_SetVisibleState(self[30], 0); uiRefreshSkillSlotList(self, 1)
			//                    = РЕДАКТОР ВКЛАДОК `CHAT_TAB_Win` 2590 (снимок вкладок в зеркала
			//                      tabFilterOptBackup/tabRecordListMirror + показ окна);
			//     2674 (0xA72) → uiConfirmDialog_Setup(g_pItemTooltipUI, 1, loc 5313, loc 5338, имя вкладки)
			//                    = диалог ввода «Изменить заголовок» (≤ 5 символов);
			//     2672 (0xA70) → uiConfirmDialog_Setup(g_pItemTooltipUI, 2, loc 5314, loc 5338, Src)
			//                    = диалог ввода «Добавить вкладку чата»;
			//     2673 (0xA71) → CItem_BuildTooltipText(g_pItemTooltipUI, 1, 5309, «Вкладка %s будет
			//                      удалена. Продолжить?», 1) = подтверждение удаления.
			//   ★★★ ЭТАП 4 ВЫПОЛНЕН (2026-08-06): все четыре пункта делают РЕАЛЬНОЕ действие.
			//   Диалоги — ЭТАЛОННЫЕ окна игры, ничего не выдумано: ввод текста = `SHOUTING_WIN`
			//   1580 (эталон переиспользует окно «крика», реверс §5.1), подтверждение = `TWOBUT_MSGBOX`
			//   1520 (§5.2). Реализация операций — `Private/UI/AsdaChatTabEditor.cpp`.
			HideTabMenu();
			HandleTabMenuCommand(B.IdNum);
			return;
		}
	}
}

// ══════════════════════════ ПЕРЕТАСКИВАНИЕ МЕНЮ ═══════════════════════════════════════════════
// JUI-привязка зоны 80009 / 2675: EVS { EVT = "6" (LButtonUp); RET = "20003"; CtrlID = окно }.
//   То есть перетаскивание меню задекларировано ДАННЫМИ, обработчика в прикладном коде нет.
void UAsdaChatWindowSubsystem::TickMenuDrag(const FVector2D& Cur, float UiScale)
{
	if (!MenuDragTarget) { return; }
	const FVector2D Delta = (Cur - MenuDragStartMouse) / FMath::Max(0.01f, UiScale);
	if (MenuDragTarget == NickMenu)
	{
		NickMenuPos = MenuDragStartPos + Delta;
		PlaceMenu(NickMenu, NickMenuPos, kNickMenuW, kNickMenuH);
	}
	else if (MenuDragTarget == TabMenu)
	{
		TabMenuPos = MenuDragStartPos + Delta;
		PlaceMenu(TabMenu, TabMenuPos, kTabMenuW, kTabMenuH);
	}
}

// ══════════════════════════ HOVER НИКА В ЛЕНТЕ 2521 ═══════════════════════════════════════════

// Ник = текст между ПЕРВЫМИ '[' и ']' (сами скобки в результат не входят); OutPlain = всё, что
//   было ДО '['. Эталонный цикл (ui_core.c:113099-113115) идёт по СЫРОЙ строке С РАЗМЕТКОЙ —
//   у нас так же: разметка снимается только при ИЗМЕРЕНИИ ширины, как `textFontMeasureMarkupString`.
FString UAsdaChatWindowSubsystem::ParseNickFromLine(const FString& Raw, FString& OutPlain)
{
	OutPlain.Reset();
	int32 Open = INDEX_NONE;
	if (!Raw.FindChar(TEXT('['), Open)) { return FString(); }
	int32 Close = INDEX_NONE;
	for (int32 i = Open + 1; i < Raw.Len(); ++i)
	{
		if (Raw[i] == TEXT(']')) { Close = i; break; }
	}
	if (Close == INDEX_NONE) { return FString(); }   // пары нет ⇒ g_szParsedNameBuf остаётся пустым
	OutPlain = Raw.Left(Open);
	return Raw.Mid(Open + 1, Close - Open - 1);
}

float UAsdaChatWindowSubsystem::MeasureMarkupWidth(const FString& S) const
{
	if (S.IsEmpty() || !FSlateApplication::IsInitialized()) { return 0.f; }
	const FString Plain = FAsdaMarkup::StripTags(S);
	if (Plain.IsEmpty()) { return 0.f; }
	FSlateFontInfo F = TipFont;
	if (!F.HasValidFont()) { F = FCoreStyle::GetDefaultFontStyle("Regular", (int32)kFontPx); }
	F.Size = (int32)kFontPx;
	// ★ СВЕДЕНО (`fix_helpers_dedup.md`): общий замер `AsdaUi::MeasureTextWidth` (Slate уже поднят — гвард выше).
	return AsdaUi::MeasureTextWidth(Plain, F, 0.f);
}

bool UAsdaChatWindowSubsystem::GetRibbonLineRect(int32 Idx, FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!LinesBox) { return false; }
	// ★ Аудит №4 (A9/B1): через общую точку `AsdaUi::WidgetAbsRect` (nullptr-узел и нулевая
	//   геометрия отсекаются внутри неё — отдельная проверка `GetChildAt` больше не нужна).
	return AsdaUi::WidgetAbsRect(LinesBox->GetChildAt(Idx), OutMin, OutMax);
}

// Три плиты подсветки ленты. Лежат В ТОЙ ЖЕ панели, что и лента (обычно группа
//   CHAT_NEW_C_List_grp), с z НИЖЕ ленты — эталон рисует квад ДО текста строки.
void UAsdaChatWindowSubsystem::EnsureRibbonHighlights()
{
	if (!LinesBox || !ChatWidget || !ChatWidget->WidgetTree) { return; }
	if (RibbonRowBg && RibbonSelBg && RibbonNameBg) { return; }
	UCanvasPanel* Host = Cast<UCanvasPanel>(LinesBox->GetParent());
	if (!Host) { return; }
	// ★ Аудит №3 (A8): белый пиксель — обычный ассет движка, поэтому идёт тем же менеджером
	//   (`LoadTextureCached` принимает любой объектный путь UE): один кэш и один refCount на порт.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	UTexture2D* White = TexMgr ? TexMgr->LoadTextureCached(
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")) : nullptr;
	auto Make = [&](const TCHAR* Name) -> UImage*
	{
		UImage* Img = ChatWidget->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(Name));
		if (!Img) { return nullptr; }
		FSlateBrush Br;
		if (White) { Br.SetResourceObject(White); }
		Br.DrawAs = ESlateBrushDrawType::Image;
		Br.ImageSize = FVector2D(8.f, 8.f);
		Img->SetBrush(Br);
		Img->SetVisibility(ESlateVisibility::Collapsed);   // мышь ловит лента, не плита
		if (UCanvasPanelSlot* CS = Host->AddChildToCanvas(Img))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
			CS->SetZOrder(1);            // лента поднята на z = 2 (MakeLinesPanel)
		}
		return Img;
	};
	if (!RibbonRowBg)  { RibbonRowBg  = Make(TEXT("CHAT_NEW_C_NOR_HoverBg")); }
	if (!RibbonSelBg)  { RibbonSelBg  = Make(TEXT("CHAT_NEW_C_NOR_SelBg")); }
	if (!RibbonNameBg) { RibbonNameBg = Make(TEXT("CHAT_NEW_C_NOR_NameBg")); }
	UE_LOG(LogAsdaChatMenu, Log,
		TEXT("[чат-лента] плиты подсветки созданы: hover 0x%08X, выделение = ~цвет строки, плашка ника 0x%08X (формула right = ширина(ника) + %.0f)"),
		kRibbonHoverColor.ToPackedARGB(), kRibbonHoverColor.ToPackedARGB(), kRibbonNamePadRight);
}

// ★★★ КАЖДЫЙ КАДР, как в эталоне (разбор живёт внутри `uiListWidgetRender`, а не в обработчике мыши).
void UAsdaChatWindowSubsystem::TickRibbonHover(const FVector2D& Cur)
{
	EnsureRibbonHighlights();
	if (!LinesBox || !RibbonRowBg || !RibbonSelBg || !RibbonNameBg) { return; }
	UCanvasPanel* Host = Cast<UCanvasPanel>(LinesBox->GetParent());
	if (!Host) { return; }

	// Строка под курсором. Эталон: `*(this + 326) = uiListHitTestItemY(this, x, y)` и ТОЛЬКО когда
	//   ЛКМ не нажата (`uiList_OnMouseDownHover`, ui_core.c:112081).
	const bool bLmb = FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	int32 NewIdx = -1;
	if (!bLmb && IsUnderCursor(LinesBox, Cur))
	{
		for (int32 i = 0; i < LinesBox->GetChildrenCount(); ++i)
		{
			if (IsUnderCursor(LinesBox->GetChildAt(i), Cur)) { NewIdx = i; break; }
		}
	}
	else if (bLmb)
	{
		NewIdx = RibbonHoverIdx;   // при нажатой ЛКМ индекс не переписывается (условие эталона)
	}
	RibbonHoverIdx = NewIdx;

	// Разбор ника — ТОЛЬКО для строки под курсором и ТОЛЬКО в ленте 2521.
	// ★★★ ПОСЛЕ ВВЕДЕНИЯ ПРЕФИКСА КАНАЛА (Д21 п.6) ник берётся ИЗ ПАКЕТА (`FChatLogLine::Nick`),
	//   а не «из первых скобок»: первыми скобками теперь идёт «[Общ.]». Эталонный разбор
	//   `ParseNickFromLine` остался ЗАПАСНЫМ путём — для строк без имени отправителя
	//   (служебные уведомления), и работает он по-прежнему по ЧИСТОМУ `Raw`, БЕЗ префикса,
	//   поэтому воспроизведённые в Д20.2 грабли оригинала (клан-тег/метка вместо имени) целы.
	// ★ `Plain` — то, что нарисовано ЛЕВЕЕ ника: префикс канала плюс хвост строки до ника.
	//   Он и задаёт левую границу второй hover-плашки (эталонная формула rect.left + ширина(текста)).
	FString Nick, Plain;
	if (VisibleLines.IsValidIndex(RibbonHoverIdx))
	{
		const FChatLogLine& HL = VisibleLines[RibbonHoverIdx];
		int32 NickPos = INDEX_NONE;
		if (!HL.Nick.IsEmpty())
		{
			NickPos = HL.Raw.Find(FString::Printf(TEXT("[%s]"), *HL.Nick), ESearchCase::CaseSensitive, ESearchDir::FromStart);
			if (NickPos == INDEX_NONE)
			{
				NickPos = HL.Raw.Find(HL.Nick, ESearchCase::CaseSensitive, ESearchDir::FromStart);
			}
			if (NickPos != INDEX_NONE) { Nick = HL.Nick; Plain = HL.Raw.Left(NickPos); }
		}
		// ★★★ ГЕЙТ bNoNick (2026-08-06, дубль оповещений в ленту). У СЛУЖЕБНЫХ строк-анонсов
		//   первые скобки — это НАЗВАНИЕ ПРЕДМЕТА («… [ Меч Дьявола ] +45%»), а не ник. Без гейта
		//   эталонный фолбэк отдал бы название предмета как ник: hover-плашка встала бы под ним,
		//   ПКМ открыло бы меню 80000 с заголовком-предметом, а «Блокировать чат» отправило бы
		//   название предмета в игнор-лист. Для строк ИГРОКОВ (bNoNick = false) фолбэк цел.
		if (Nick.IsEmpty() && !HL.bNoNick)
		{
			Nick = ParseNickFromLine(HL.Raw, Plain);
		}
		// Префикс канала рисуется ПЕРЕД строкой, значит входит в ширину «текста до ника».
		Plain = HL.ChanPrefix + Plain;
	}
	// ★ Пока меню 80000 открыто, ник-адресат НЕ переписываем: эталон читает g_szParsedNameBuf
	//   в момент нажатия пункта, а курсор к тому времени уже ушёл со строки на кнопку меню.
	const bool bMenuOpen = NickMenu && NickMenu->GetVisibility() != ESlateVisibility::Collapsed;
	if (!bMenuOpen)
	{
		RibbonHoverNick = Nick;
		RibbonHoverPlain = Plain;
	}

	const FGeometry& HG = Host->GetCachedGeometry();
	const float S = FMath::Max(0.01f, (float)HG.Scale);
	const FVector2D HP = HG.GetAbsolutePosition();
	const FVector2D HSz = HG.GetLocalSize() * HG.Scale;

	auto PlaceAbs = [&](UImage* Img, const FVector2D& Mn, const FVector2D& Mx, const FColor& C)
	{
		if (!Img) { return; }
		// Клампим прямоугольник по корпусу ленты: эталон рисует внутри списка, у нас плита лежит
		//   в канвасе группы и без клампа могла бы вылезти за кромку при частично видимой строке.
		const FVector2D A(FMath::Max(Mn.X, HP.X), FMath::Max(Mn.Y, HP.Y));
		const FVector2D B(FMath::Min(Mx.X, HP.X + HSz.X), FMath::Min(Mx.Y, HP.Y + HSz.Y));
		if (B.X - A.X <= 0.5 || B.Y - A.Y <= 0.5) { Img->SetVisibility(ESlateVisibility::Collapsed); return; }
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(Img))
		{
			CS->SetOffsets(FMargin((float)((A.X - HP.X) / S), (float)((A.Y - HP.Y) / S),
			                       (float)((B.X - A.X) / S), (float)((B.Y - A.Y) / S)));
		}
		// ★ ГАММУ НЕ ПРИМЕНЯЕМ: это D3DRS_TEXTUREFACTOR — цвет-МНОЖИТЕЛЬ, а не тексель sRGB.
		Img->SetColorAndOpacity(FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f));
		Img->SetVisibility(ESlateVisibility::HitTestInvisible);
	};

	// ── (а) общая подсветка строки: hover и выделение (ui_core.c:112969-113035) ─────────────────
	FVector2D Mn, Mx;
	// Выделение рисуем первым (оно ниже по z не важно — это разные плиты), цвет = ~цвет строки.
	if (VisibleLines.IsValidIndex(RibbonSelIdx) && GetRibbonLineRect(RibbonSelIdx, Mn, Mx))
	{
		const FColor& Bg = VisibleLines[RibbonSelIdx].RowBg;
		const uint32 Packed = ((uint32)Bg.A << 24) | ((uint32)Bg.R << 16) | ((uint32)Bg.G << 8) | (uint32)Bg.B;
		const uint32 Inv = ~Packed;
		PlaceAbs(RibbonSelBg, Mn, Mx,
			FColor((uint8)((Inv >> 16) & 0xFF), (uint8)((Inv >> 8) & 0xFF), (uint8)(Inv & 0xFF), (uint8)((Inv >> 24) & 0xFF)));
	}
	else if (RibbonSelBg) { RibbonSelBg->SetVisibility(ESlateVisibility::Collapsed); }

	// ★ ВЫДЕЛЕНИЕ ПЕРЕБИВАЕТ HOVER: в эталоне два `if` идут подряд и второй (выделение)
	//   перезаписывает TEXTUREFACTOR, выставленный первым.
	const bool bHoverBeaten = (RibbonHoverIdx >= 0 && RibbonHoverIdx == RibbonSelIdx);
	if (!bHoverBeaten && VisibleLines.IsValidIndex(RibbonHoverIdx) && GetRibbonLineRect(RibbonHoverIdx, Mn, Mx))
	{
		PlaceAbs(RibbonRowBg, Mn, Mx, kRibbonHoverColor);
	}
	else if (RibbonRowBg) { RibbonRowBg->SetVisibility(ESlateVisibility::Collapsed); }

	// ── (б) вторая плашка РОВНО ПОД НИКОМ (ui_core.c:113126-113182) ────────────────────────────
	//   left  = rect.left + ширина(текста до '[')
	//   right = ширина(ника) + 44                    ← ★ АБСОЛЮТНАЯ формула, воспроизводим как есть
	//   ★ 44 — зашитая константа в координатах ЭКРАНА оригинала, где лента 2521 стоит на x = 28.
	//     У нас лента тоже на базовом x = kListX (28) и её левый край при растягивании окна не
	//     двигается, поэтому в локальных координатах группы правая граница = (ширина ника + 44) − 28.
	if (!bHoverBeaten && !Nick.IsEmpty() && GetRibbonLineRect(RibbonHoverIdx, Mn, Mx))
	{
		const float PlainW = MeasureMarkupWidth(Plain);
		const float NickW  = MeasureMarkupWidth(Nick);
		const float LeftLocal  = (float)((Mn.X - HP.X) / S) + PlainW;
		// ★★★ ПОПРАВКА 2026-08-06 (Д21 п.6). Эталонная формула ПРАВОЙ границы АБСОЛЮТНА
		//   (`right = ширина(ника) + 44`) и молча предполагает, что ник стоит В НАЧАЛЕ строки:
		//   тогда 44 = левый инсет ленты (28) + ширина '[' и хвостик. С добавленным префиксом
		//   канала «[Общ.]» ник начинается на ~30 px правее, и абсолютная граница оказывалась
		//   ЛЕВЕЕ левой ⇒ плашка под ником просто пропадала.
		//   Сохраняем СМЫСЛ эталона (ширина ника + та же добавка kRibbonNamePadRight − kListX = 16),
		//   но отсчитываем от НАЧАЛА НИКА. При пустом префиксе результат совпадает с эталонным
		//   числом до пикселя — регресса для строк без префикса нет.
		const float RightLocal = LeftLocal + NickW + (kRibbonNamePadRight - kListX);
		if (RightLocal > LeftLocal)
		{
			PlaceAbs(RibbonNameBg,
				FVector2D(HP.X + (double)LeftLocal  * (double)S, Mn.Y),
				FVector2D(HP.X + (double)RightLocal * (double)S, Mx.Y),
				kRibbonHoverColor);
		}
		else if (RibbonNameBg) { RibbonNameBg->SetVisibility(ESlateVisibility::Collapsed); }
	}
	else if (RibbonNameBg) { RibbonNameBg->SetVisibility(ESlateVisibility::Collapsed); }
}

// ══════════════════════════ СТРОКА ЛЕНТЫ: РАЗМЕТКА И ПЕРЕКРАСКА ═══════════════════════════════
// <FS08> кегль импортёра, <FD> тень (флаг 0x400000 списка, chat.c:806), <FC=..> цвет.
//   `</F>` в исходном тексте сбрасывает ВСЕ атрибуты — восстанавливаем префикс после каждого.
// ★ ВЫДЕЛЕННАЯ СТРОКА: эталон красит её текст цветом `item[+2760] | 0xFF000000`
//   (ui_core.c:113186-113233). Поле +2760 — «ранговый» цвет строки; у обычных строк оно 0 ⇒
//   текст выделенной строки ЧЁРНЫЙ на белой плите. Это оригинал, а не баг контраста.
//
// ★★★ РАСКРАСКА СТРОКИ ЛЕНТЫ — ДЕЙСТВУЮЩИЙ ВАРИАНТ (юзер, 2026-08-06, ВТОРАЯ итерация):
//   «давай обводку уберем и просто сделаем цвет сообщения в цвет канала, без обводок».
//
//     [Общ.]   — ЦВЕТ КАНАЛА          (префикс оставлен, он нужен)
//     [Ник]    — БЕЛЫЙ, БЕЗ обводки   (см. «почему белый» ниже)
//     текст    — ЦВЕТ КАНАЛА, БЕЗ обводки   ← как в ЭТАЛОНЕ
//
//   ⚠ ЧТО ОТМЕНЕНО ОТНОСИТЕЛЬНО Д21: обводки (`<FO=…>`) в ленте больше НЕ применяются ни к нику,
//   ни к тексту, и текст сообщения снова красится ЦВЕТОМ КАНАЛА, а не белым. Механизм разметки,
//   заведённый в Д21 (биты стилей + тег `<FO=RRGGBB>`), НЕ выпилен — он общий для всего UI и
//   правильный; здесь он просто не используется. Вернуть вариант Д21 можно БЕЗ ПЕРЕСБОРКИ:
//       asda.Chat.LineOutline 1
//
//   ★ ПОЧЕМУ НИК ОСТАВЛЕН БЕЛЫМ (решение агента, юзер оставил на усмотрение). Требование «ники
//   оставляем белыми» юзер НЕ отменял — отменены были только обводки и белый цвет ТЕКСТА. Белый
//   ник при цветном сообщении сохраняет то, ради чего префикс и заводился: «кто сказал» читается
//   отдельно от «что сказал», а строка при этом остаётся эталонно-цветной по каналу. Одноцветный
//   вариант (ник тоже цветом канала = строка ровно как в оригинале + префикс) получается заменой
//   одной строки `SegNick` на `SegPrefix` — если юзер попросит, менять там.
//
//   ЭТАЛОН для сверки: вся строка красится ОДНИМ цветом — цветом слота канала
//   (`Chat_AddChatLine @0xBB14C0` перетирает переданный a4 цветом канала для всех категорий,
//   кроме colorType 40). Ни префикса, ни ника отдельным цветом, ни обводки в оригинале нет.
//   Тень `<FD>` (эталонный флаг списка 2521 = 0x400000, chat.c:806) сохранена во всех сегментах.
//
//   ⚠ ВЫДЕЛЕННАЯ СТРОКА (bSelected) идёт ПО-ПРЕЖНЕМУ ОДНИМ цветом `item[+2760] | 0xFF000000`
//   (чёрным у обычных строк) — это эталон и его не трогаем: сегментная раскраска поверх белой
//   плиты выделения сделала бы текст нечитаемым.
FString UAsdaChatWindowSubsystem::BuildLineMarkup(const FChatLogLine& L, bool bSelected) const
{
	const FString Size = FString::Printf(TEXT("<FS%02d>"), (int32)kFontPx);
	// Восстановить формат сегмента после каждого `</F>`, который пришёл В САМИХ ДАННЫХ
	//   (5087 шлёт ранг инлайн-разметкой «<FC=ffffffff>[#N]</F> »).
	auto Restore = [](const FString& In, const FString& Seg)
	{
		FString S = In;
		S.ReplaceInline(TEXT("</F>"), *(FString(TEXT("</F>")) + Seg), ESearchCase::CaseSensitive);
		return S;
	};

	if (bSelected)
	{
		const FColor C(L.RowBg.R, L.RowBg.G, L.RowBg.B, 255);
		const FString Base = Size + FString::Printf(TEXT("<FD><FC=FF%02X%02X%02X>"), C.R, C.G, C.B);
		return Base + Restore(L.Raw, Base);
	}

	// Служебная строка (канала нет вовсе — системные нотисы, наши сообщения об отказах) —
	//   1:1 эталон: весь текст ОДНИМ цветом строки, без префикса и без обводки.
	if (L.PrefixSlot < 0)
	{
		const FColor C = L.Color;
		const FString Base = Size + FString::Printf(TEXT("<FD><FC=FF%02X%02X%02X>"), C.R, C.G, C.B);
		return Base + Restore(L.Raw, Base);
	}

	// ★★★ ЦВЕТ КАНАЛА — ИЗ ЖИВОЙ ТАБЛИЦЫ (ChanColor = `tabFilterOpt[slot].color`), а не из
	//   заводской SlotColor. Этим красятся И префикс «[Общ.]», И сам текст сообщения (SegMsg),
	//   поэтому смена цвета канала в палитре 2700 меняет их вместе — как в оригинале, где
	//   `Chat_AddChatLine` берёт textColor из тех же записей (см. объявление ChanColor).
	const FColor Chan = ChanColor(L.PrefixSlot);
	// ★ Обводки в ленте выключены (см. шапку). CVar `asda.Chat.LineOutline 1` возвращает вариант
	//   Д21 целиком: ник белый + ЧЁРНАЯ обводка, текст БЕЛЫЙ + обводка цветом канала.
	const bool bOutline = (GAsdaChatLineOutline.GetValueOnGameThread() != 0);
	const FString SegPrefix = Size + FString::Printf(TEXT("<FD><FC=FF%02X%02X%02X>"), Chan.R, Chan.G, Chan.B);
	const FString SegNick = bOutline
		? (Size + TEXT("<FD><FO=000000><FC=FFFFFFFF>"))
		: (Size + TEXT("<FD><FC=FFFFFFFF>"));                       // белый, БЕЗ обводки
	const FString SegMsg = bOutline
		? (Size + FString::Printf(TEXT("<FD><FO=%02X%02X%02X><FC=FFFFFFFF>"), Chan.R, Chan.G, Chan.B))
		: SegPrefix;                                                 // ЦВЕТ КАНАЛА, БЕЗ обводки (эталон)

	FString Out;
	if (!L.ChanPrefix.IsEmpty()) { Out += SegPrefix + L.ChanPrefix; }

	// ── Найти токен ника «[Имя]» в готовой строке ────────────────────────────────────────────
	//   Имя знаем из ПАКЕТА (L.Nick) — ищем именно его вхождение в скобках. Если формат строки
	//   ника в скобках не содержит (4121 «Имя: текст») — берём голое имя. Если и его нет
	//   (служебные уведомления шёпота) — падаем на эталонный разбор «первые […]».
	int32 NickPos = INDEX_NONE;
	FString NickTok;
	if (!L.Nick.IsEmpty())
	{
		NickTok = FString::Printf(TEXT("[%s]"), *L.Nick);
		NickPos = L.Raw.Find(NickTok, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (NickPos == INDEX_NONE)
		{
			NickTok = L.Nick;
			NickPos = L.Raw.Find(NickTok, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		}
	}
	// ★ Тот же гейт bNoNick, что и в TickRibbonHover. СТРАХОВКА: служебные строки-анонсы уходят
	//   в ветку `PrefixSlot < 0` выше и сюда не доходят, но если такая строка когда-нибудь придёт
	//   с PrefixSlot >= 0, скобки названия предмета всё равно не станут ником.
	if (NickPos == INDEX_NONE && !L.bNoNick)
	{
		FString Plain;
		const FString N = ParseNickFromLine(L.Raw, Plain);
		if (!N.IsEmpty()) { NickTok = FString::Printf(TEXT("[%s]"), *N); NickPos = Plain.Len(); }
	}

	if (NickPos == INDEX_NONE)
	{
		// Ника в строке нет вовсе — весь текст идёт как сообщение (белый + обводка канала).
		Out += SegMsg + Restore(L.Raw, SegMsg);
		return Out;
	}

	const FString Head = L.Raw.Left(NickPos);
	const FString Tail = L.Raw.Mid(NickPos + NickTok.Len());
	if (!Head.IsEmpty()) { Out += SegMsg + Restore(Head, SegMsg); }
	Out += SegNick + NickTok;
	if (!Tail.IsEmpty()) { Out += SegMsg + Restore(Tail, SegMsg); }
	return Out;
}

void UAsdaChatWindowSubsystem::RepaintRibbonLine(int32 Idx)
{
	if (!LinesBox || !VisibleLines.IsValidIndex(Idx)) { return; }
	UWidget* Row = LinesBox->GetChildAt(Idx);
	if (UBorder* Bd = Cast<UBorder>(Row)) { Row = Bd->GetContent(); }
	if (!Row) { return; }
	const FString Markup = BuildLineMarkup(VisibleLines[Idx], Idx == RibbonSelIdx);
	if (UWidget* New = UAsdaRichText::ApplyFormatted(Row, Markup))
	{
		if (UTextLayoutWidget* Tw = Cast<UTextLayoutWidget>(New))
		{
			if (LinesWrapW > 0.f) { Tw->SetWrapTextAt(LinesWrapW); Tw->SetAutoWrapText(true); }
		}
	}
}

// ══════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════════
FString UAsdaChatWindowSubsystem::DebugContextMenus() const
{
	FString S;
	S += FString::Printf(TEXT("=========== КОНТЕКСТНЫЕ МЕНЮ ЧАТА ===========\n"));
	S += FString::Printf(TEXT("  меню 80000 CHAT_NEW_TABX : %s, позиция (%.0f,%.0f) базовых px, %.0fx%.0f\n"),
		(NickMenu && NickMenu->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("ОТКРЫТО") : TEXT("закрыто"),
		NickMenuPos.X, NickMenuPos.Y, kNickMenuW, kNickMenuH);
	S += FString::Printf(TEXT("  меню 2670  CHAT_SETUP_Win: %s, позиция (%.0f,%.0f) базовых px, %.0fx%.0f\n"),
		(TabMenu && TabMenu->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("ОТКРЫТО") : TEXT("закрыто"),
		TabMenuPos.X, TabMenuPos.Y, kTabMenuW, kTabMenuH);
	S += FString::Printf(TEXT("  ник под курсором (g_szParsedNameBuf): «%s»%s\n"),
		*RibbonHoverNick, RibbonHoverNick.IsEmpty() ? TEXT("  (ПКМ ничего не откроет)") : TEXT(""));
	S += FString::Printf(TEXT("  строка под курсором = %d, выбранная = %d, строк в ленте %d\n"),
		RibbonHoverIdx, RibbonSelIdx, VisibleLines.Num());
	S += FString::Printf(TEXT("  лимит поля ввода 2609: общий = %d, ДЕЙСТВУЮЩИЙ для режима %d = %d ")
	                     TEXT("(эталон %d, умолчание порта %d, потолок %d)\n"),
		GetInputMaxChars(), InputState, GetActiveInputMaxChars(),
		kInputMaxCharsEtalon, kInputMaxCharsDefault, kInputMaxCharsCeiling);
	S += FString::Printf(TEXT("     ★ канал АРМИИ (режим 9, опкод 6718) всегда %d — тело 6718 = 108 б (серверная раскладка, MD5 по 108), приём 6719 = 101 б; патч P14 его не трогает\n"),
		kInputMaxCharsArmy);
	S += FString::Printf(TEXT("     ★ >80 в прочих каналах требует серверного патча P14; вернуть эталон: asda.Chat.InputMaxChars 68\n"));
	S += FString::Printf(TEXT("  ИГНОР-ЛИСТ (%d записей; лимита нет, в файл не пишется, на сервер не уходит):\n"),
		IgnoreList.Num());
	for (const FString& N : IgnoreList) { S += FString::Printf(TEXT("     • %s\n"), *N); }
	S += FString::Printf(
		TEXT("  фильтруются ВСЕ входящие чат-опкоды: 5085 общий, 5087 глобальный, 5089 входящий шёпот,\n")
		TEXT("     5106 отряд, 4121 системный/клан, 6561 широкие каналы, 6719 клан-чат + облачко над головой.\n")
		TEXT("     НЕ глушатся (нет авторства): служебные нотисы шёпота 5089 msgType 0/2/3 и ЭХО своего шёпота.\n")
		TEXT("     ⚠ ОТСТУПЛЕНИЕ Д27 п.2: эталон фильтрует только 5085 и 6561.\n"));
	S += FString::Printf(TEXT("  ЗАМОК АВТОПРОКРУТКИ (2607, CChat+497): %s\n"),
		bScrollFrozen ? TEXT("ВКЛЮЧЁН — лента не прыгает к последней строке") : TEXT("выключен"));
	S += FString::Printf(TEXT("  ЦЕЛЬ ПУНКТОВ МЕНЮ 2670: вкладка %d «%s»%s\n"),
		MenuTargetTab(), *TabName(MenuTargetTab()),
		TabMenuTargetIdx == INDEX_NONE ? TEXT(" (по вкладке не попали — активная)") : TEXT(" (вкладка под ПКМ)"));
	return S;
}

void UAsdaChatWindowSubsystem::DebugShowNickMenu(const FString& Name)
{
	// Показ из консоли — в центре базового экрана, чтобы не зависеть от положения курсора.
	ShowNickMenu(Name, FVector2D(AsdaJui::kBaseW * 0.5f, AsdaJui::kBaseH * 0.5f));
}

void UAsdaChatWindowSubsystem::DebugShowTabMenu()
{
	ShowTabMenu(FVector2D(AsdaJui::kBaseW * 0.5f, AsdaJui::kBaseH * 0.5f));
}
