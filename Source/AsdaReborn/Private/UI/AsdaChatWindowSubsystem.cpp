// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы UAsdaChatWindowSubsystem
//   разнесены по группам в AsdaChatWindowSubsystem_{Mouse,Input,Tabs}.cpp; общее (LogAsdaChatWin, чистые константы и хелперы) —
//   в AsdaChatWindowSubsystemInternal.h. Строки перенесены БАЙТ В БАЙТ:
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaChatWindowSubsystem.plan.json`.
//   Здесь осталось ядро: таблица каналов, жизненный цикл, построение и раскладка окна по JUI, вывод строк.
// Реализация UAsdaChatWindowSubsystem — основное окно чата CHAT_NEW (2500).
// Всё обоснование по эталону, геометрия, таблица каналов и план этапов — в шапке заголовка.
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaTipPanel.h"   // ★ 30.10.2026: EAsdaTipOwner::ChatWin — гашение подсказок окна чата
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: WireTextByteLen/TruncateToWireBytes (лимит ввода в байтах провода)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "UI/AsdaUiHelpers.h"   // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)
#include "AsdaNetSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ эмоции слэш-командой (порт ChatInput_ParseCommand)
#include "UI/AsdaJuiSlot.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): атласы JUI — только через менеджер
#include "UI/AsdaMarkup.h"   // UAsdaRichText::ApplyFormatted — теги <FS>/<FD>/<FC=..>
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"   // UTextLayoutWidget — общий предок UTextBlock и URichTextBlock
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/RichTextBlock.h"
#include "Components/EditableText.h"
#include "Components/CheckBox.h"   // 2607 CHAT_NEW_Hbtn — CheckButton скина Chat_new_ck3
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"   // замер высоты строки для точной центровки подписи 2608
#include "Styling/CoreStyle.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNotice.h"                 // ★ Дедуп №2, В1-B (U10c): `AsdaNotice::Loc` вместо прямого `FAsdaLoginData::Loc`
#include "UI/AsdaPartySubsystem.h"         // ★ 12.10.2026 [ОРИГИНАЛ]: чат отряда — `Party_IsInParty` и `g_nPartyInviteTargetIdx` (= PartyIndex)
#include "UI/AsdaSoulmateSubsystem.h"      // ★ 12.10.2026 [ОРИГИНАЛ]: канал «Друг» — слот/пара/имя партнёра (`g_pSoulmateWnd`)
#include "AsdaChatWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaChatWindowSubsystem*.cpp (LogAsdaChatWin, чистые хелперы)

DEFINE_LOG_CATEGORY(LogAsdaChatWin);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaChatWindowSubsystemInternal.h (один экземпляр на все части)

// ★★★ [ОРИГИНАЛ] 25.10.2026 — РЕЖИМ ЧАТА И АДРЕСАТ ШЁПОТА НЕ ПЕРЕЖИВАЮТ ВЫХОД НА ЛОГИН (ревизия
//   `Reports/inventory_relog_leak/REVIEW.md`, Н1). Было: `InputState`, `WhisperTarget` и `LastWhisperFrom` не сбрасывались ни
//   при одной причине — учётка Б после перезахода жала Enter, и строка уходила 5088 ШЁПОТОМ собеседнику учётки А (а в поле
//   ввода стояло «Шепчемся с …» с его именем).
//   ЭТАЛОН. Выход в выбор персонажа `gameLeaveToCharSelect@0xED2E10` (game_main.c:12125) зовёт `playerStateReset@0xBB6470`
//   (entity_char.c:44448) — там `memset(chat+2532, 0, 0x14)` (адресат шёпота) и `memset(chat+2552, 0, 0x14)` (последний
//   шептавший, entity_char.c:44525-44526), и снимает `g_bUIDataLoaded`; следующий вход в мир перегружает интерфейс
//   (`LoadAllInterfaces2@0xD5F270` стадия 2 → `CChat_Init` → `chatInitColorPalette@0xB9E110`: `tabSelMode = 4`, chat.c:679) —
//   режим ввода снова «[Общий]». То есть сброс при выходе на логин — поведение ОРИГИНАЛА.
//   Точки порта — те же, что у очистки вещей (`asda.Session.RelogClearItems`): `LeftWorld` (травел на сцену входа) и
//   `EnteringWorld` (1021). `MapChanged` и `Disconnected` режим и адресата НЕ трогают (у оригинала при разрыве клиент закрывается —
//   [[original-quits-client-on-disconnect]]; порт оставляет персонажа в мире под попапом, следующий шаг — сцена входа → `LeftWorld`).
//   Ключ: 1 — сбрасывать (по умолчанию); 0 — прежнее поведение порта (режим и адресат переживали перезаход).
static ASDA_DEV_CVAR(int32, CVarChatRelogResetWhisper,
	TEXT("asda.Chat.RelogResetWhisper"),
	1,
	TEXT("Сброс режима чата и адресата шёпота при выходе на логин / входе в мир: 1 = да, как оригинал (playerStateReset + CChat_Init, "
	     "25.10.2026); 0 = как было — режим, адресат и последний шептавший переживали перезаход."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 25.10.2026 — «R» (CHAT ANSWER, команда 69) ОТВЕЧАЕТ ПОСЛЕДНЕМУ ШЕПТАВШЕМУ. Было: `NoteWhisperFrom` никто не
//   вызывал, `LastWhisperFrom` всегда пуст ⇒ клавиша не работала никогда (аудит `Reports/audit_2026-09-02/10_chat.md` №4/№5).
//   ЭТАЛОН. Запоминает ПРИЁМ: `Recv_ChatWhisper@0xBA7530`, ветка «входящий шёпот от другого игрока» (msgType 1, flag 0) —
//   `memcpy(chat+2552, &pkt[2], 0x14)` (network.c:10646) ДО развилки «одержимый отправитель» (`LOBYTE(pkt[7]) == 2`), поэтому
//   запоминаются обе ветки. Эхо своего шёпота (flag != 0) и служебные msgType 0/2/3 поле не трогают. Читает клавиша 69:
//   `ChatInput_ParseWhisper(chat, 1)` (input.c:4407-4409 → chat.c:2873-2900) — см. `AnswerLastWhisper` (_Input.cpp).
//   Порт: признак входящего шёпота — `SrcOpcode == 5089 && bSenderAuthored` (ровно две ветки эталона). [ПОРТ]: строка, отброшенная
//   игнор-листом (наше расширение фильтра на 5089, Д27 п.2), адресатом ответа не становится — иначе «R» отвечал бы невидимому.
//   Ключ: 1 — как оригинал (по умолчанию); 0 — прежнее поведение порта (шептавший не запоминается, «R» открывает ввод в текущем режиме).
static ASDA_DEV_CVAR(int32, CVarChatWhisperAnswer,
	TEXT("asda.Chat.WhisperAnswer"),
	1,
	TEXT("Клавиша «R» (CHAT ANSWER): 1 = как оригинал — приём 5089 запоминает шептавшего, «R» ставит шёпот ему, без шёпота — loc 6942 "
	     "(25.10.2026); 0 = как было — шептавший не запоминается, «R» открывает ввод в текущем режиме."),
	ECVF_Default);

// Аксессор ключа «R» для `AsdaChatWindowSubsystem_Input.cpp` (имя ключа в рабочем коде другого файла не повторяется — правило Shipping).
bool AsdaChatWhisperAnswerOn()
{
	return CVarChatWhisperAnswer.GetValueOnGameThread() != 0;
}

// Импортированный WBP основного чата (генератор Tools/asda_jui_generic_umg.py).
static const TCHAR* kChatWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW.WBP_CHAT_NEW_C");

// Id зоны интерфейса в общем реестре = имя окна оригинала.
const FName UAsdaChatWindowSubsystem::kZoneId(TEXT("CHAT_NEW"));
const FName UAsdaChatWindowSubsystem::kComboClaimId(TEXT("CHAT_NEW_CHANNEL_LIST"));

int32 UAsdaChatWindowSubsystem::CategoryToSlot(int32 Category)
{
	// 1:1 switch из Chat_AddChatLine @0xBB14C0 (chat.c:5027-5075).
	switch (Category)
	{
	case 0: case 100: return 4;   // colorType 4  (chanColorDefault)
	case 1:           return 3;   // colorType 3  (chanColorNormal)
	case 2:           return 5;   // colorType 5 (или 41 = вариант с префиксом loc 12093)
	case 3: case 101: return 8;   // colorType 8
	case 4:           return 6;   // colorType 6
	case 5:           return 7;   // colorType 7
	case 6:           return -1;  // colorType 40 — служебный, цвета канала не берёт
	case 7: case 8: case 9: return Category - 7;   // слоты 0/1/2 — три канала «[Все] …»
	default:          return -1;
	}
}

FColor UAsdaChatWindowSubsystem::SlotColor(int32 Slot)
{
	if (Slot < 0 || Slot > 9) { return FColor(255, 255, 255, 255); }
	const FAsdaChanDef& C = kChannels[Slot];
	return FColor(C.R, C.G, C.B, 255);
}

// ★★★ ДЕЙСТВУЮЩИЙ ЦВЕТ КАНАЛА — ИЗ ЖИВОЙ ЗАПИСИ `tabFilterOpt[slot]` (наш `ChanOpts`).
//   Полное доказательство того, что цвет ТЕКСТА ленты в оригинале читается именно отсюда
//   (шесть «отдельных полей» chanColor* физически лежат внутри массива tabFilterOpt) —
//   в объявлении этой функции в заголовке.
//   Пока таблица не заполнена (самый ранний старт до Initialize) — заводской цвет слота.
FColor UAsdaChatWindowSubsystem::ChanColor(int32 Slot) const
{
	if (ChanOpts.IsValidIndex(Slot))
	{
		const FColor C = ChanOpts[Slot].Color;
		return FColor(C.R, C.G, C.B, 255);   // альфа в записи не используется (эталон рисует непрозрачно)
	}
	return SlotColor(Slot);
}

int32 UAsdaChatWindowSubsystem::SlotNameLocId(int32 Slot)
{
	return (Slot >= 0 && Slot <= 9) ? kChannels[Slot].LocId : 0;
}

FString UAsdaChatWindowSubsystem::SlotShortName(int32 Slot)
{
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): строка таблицы на языке сессии (пробелы по краям срезаются).
	return (Slot >= 0 && Slot <= 9) ? FAsdaLoginData::Loc(kChanShortLoc[Slot]).TrimStartAndEnd() : FString();
}

// ★★★ ПОДПИСЬ КАНАЛА ДЛЯ ИНТЕРФЕЙСА (требование юзера 2026-08-06): «По всем каналам» → «Все каналы».
//   ⚠ ОТСТУПЛЕНИЕ ОТ ДАННЫХ ОРИГИНАЛА. Строка приходит из ТАБЛИЦЫ ТЕКСТОВ игры (loc 5316 —
//   приглашение режима 0, loc 5417 — вкладка-фильтр № 6 с маской 903). Саму таблицу НЕ правим:
//   она — данные оригинала, и её правка разошлась бы с сервером и с остальными подсистемами.
//   Переименование делается ТОЛЬКО на выводе, одной точкой, чтобы новая подпись появилась ВЕЗДЕ
//   одинаково: и на вкладке-фильтре, и в выпадающем списке, и в приглашении, и в логах.
//   ★ Скобки у названий каналов здесь же не трогаем — их снимает вызывающий (решение Д6).
FString UAsdaChatWindowSubsystem::UiChannelText(int32 LocId) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FString S = Txt ? Txt->GetTextById(LocId) : FString();
	if (S.IsEmpty()) { S = FString::FromInt(LocId); }
	return RenameChannelText(S);
}

// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): переименование больше НЕ ищет кусок русской строки (на английском молча перестало
//   бы работать). Пары «строка игры → своя строка порта» по id; сравнение — со строкой ТОГО ЖЕ языка:
//   5417 «По всем каналам» → 990458 «Все каналы», 5316 «[По всем каналам] » → 990500 «[Все каналы] ».
//   Своей строки нет в таблице — остаётся строка игры (поведение не ломается).
static const int32 kChannelRenameLoc[][2] = { { 5417, 990458 }, { 5316, 990500 } };

int32 UAsdaChatWindowSubsystem::RenamedChannelLoc(int32 LocId)
{
	for (const int32 (&Pair)[2] : kChannelRenameLoc) { if (Pair[0] == LocId) { return Pair[1]; } }
	return 0;
}

FString UAsdaChatWindowSubsystem::RenameChannelText(const FString& In)
{
	for (const int32 (&Pair)[2] : kChannelRenameLoc)
	{
		if (In != FAsdaLoginData::Loc(Pair[0])) { continue; }
		const FString Ours = FAsdaLoginData::Loc(Pair[1]);
		return Ours.IsEmpty() ? In : Ours;
	}
	return In;
}

// ══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════

void UAsdaChatWindowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★ Набор вкладок и опции каналов — ДАННЫЕ (эталон: `uiChatOpt_InitItems` заводит дефолты,
	//   затем `CChat_LoadChatOpt` перечитывает их из `ChatOpt.bin`; файла нет → сразу пишется дефолт).
	//   Порядок тот же: сперва дефолты, потом попытка загрузки.
	InitDefaultTabs();
	InitDefaultChanOpts();
	LoadChatOpt();
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());

	// ★★★ 10.10.2026 [ОРИГИНАЛ]: ESC ЗАКРЫВАЕТ МЕНЮ ВКЛАДОК 2670 ПЕРВЫМ. Эталон показывает меню и регистрирует его
	//   ВЕРХНИМ корневым окном (`uiWindowShowHideAndRegister@0xBA0360` → `CClass_MapInsert`), признак `CControl+0x228`
	//   ставит `CChat_BuildTabUI` (chat.c:863), событие 21 → `uiChatWindowMsgProc` (ui_core.c:12430-12433):
	//   `if (ctrlId == 2670) CControl_SetVisible(pThis[56], 0)`. В порту меню — слой внутри виджета чата, поэтому
	//   проход ESC видит его через `RegisterEscPopup` (z = z чата, поднятого `PushTopmost`). Меню по нику 80000
	//   признака не имеет (его в таблице нет) — ESC его не закрывает, как в оригинале.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		static const FName kTabMenuId(TEXT("CHAT_SETUP_Win"));   // IDNum 2670
		TWeakObjectPtr<UAsdaChatWindowSubsystem> WeakThis(this);
		WM->RegisterEscPopup(kTabMenuId, kZoneId, [WeakThis]()
		{
			const UAsdaChatWindowSubsystem* S = WeakThis.Get();
			return S && S->IsTabMenuShown();
		});
		WM->RegisterEscCloser(kTabMenuId, FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-меню] ESC: 2670 закрыто (событие 21 → CControl_SetVisible(pThis[56], 0))"));
			HideTabMenu();
		}));
		// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: щелчок мимо гасит оба меню чата; разбор — `AsdaWindowSubsystem_TopPress.cpp`.
		WM->RegisterClickAwayMenu(kMenuClaimId, FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			HideNickMenu();
			HideTabMenu();
		}));
	}

	// Приём строк чата — 6 опкодов разбирает сетевой слой, сюда приходит готовая строка.
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HChatLine = Net->OnChatLine.AddLambda([this](const UAsdaNetSubsystem::FAsdaChatLineIn& L)
		{
			// ★★★ ИГНОР-ЛИСТ. Эталон зовёт `strSetContains` РОВНО В ДВУХ приёмниках —
			//   `Recv_ChatNormal` (5085, network.c:10404) и `Recv_WhisperOrSystemMessage`
			//   (6561, network.c:10876) — и при попадании строка просто НЕ добавляется.
			//   ⚠⚠⚠ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (требование юзера 2026-08-06, Д27 п.2): фильтр
			//   распространён на ВСЕ входящие чат-опкоды (5085/5087/5089/5106/4121/6561/6719),
			//   то есть заблокированный игрок не пробивается НИ ОДНИМ каналом. НЕ «чинить»
			//   обратно к двум опкодам — список в `IgnoreFiltersOpcode`.
			//   ★ Второе условие — авторство строки (`bSenderAuthored`): служебные уведомления
			//   шёпота и эхо собственного исходящего шёпота не глушатся, см. FAsdaChatLineIn.
			if (L.bSenderAuthored && IgnoreFiltersOpcode(L.SrcOpcode) && IsChatIgnored(L.SenderName))
			{
				UE_LOG(LogAsdaChatWin, Log,
					TEXT("[чат-игнор] опкод %d от «%s» отброшен (игнор-лист; ★ отступление: в эталоне ")
					TEXT("фильтруются только 5085 и 6561)"), L.SrcOpcode, *L.SenderName);
				return;
			}
			// ★ [ОРИГИНАЛ] 25.10.2026: входящий шёпот запоминает отправителя для «R» — `memcpy(chat+2552, &pkt[2], 0x14)`
			//   (network.c:10646); шапка ключа `asda.Chat.WhisperAnswer` выше.
			if (L.SrcOpcode == 5089 && L.bSenderAuthored && !L.SenderName.IsEmpty() && CVarChatWhisperAnswer.GetValueOnGameThread() != 0)
			{
				NoteWhisperFrom(L.SenderName);
			}
			// TODO-ЭТАП 2: мат-фильтр `Chat_MatchBannedWord` (CurseTable.BIN) → замена на loc 9005
			//   и анти-спам «.com»/«.c0m» (эталон отбрасывает строку целиком).
				// ★ Имя отправителя передаём ОТДЕЛЬНЫМ полем (Д21 п.6): по нему красится ник в
				//   строке и по нему же работают hover/ПКМ-меню — из текста его больше не вынимаем,
				//   иначе добавленный префикс канала «[Общ.]» подменил бы ник.
				// ★ Клан-чат 6719 приходит категорией 6 (colorType 40) — слота канала у неё нет,
				//   поэтому «[Гильдия]» ставим по опкоду. Цвет строки при этом НЕ трогаем: у этой
				//   категории эталон единственный раз оставляет переданный a4 (онлайн/офлайн).
				const int32 PrefixSlot = (L.SrcOpcode == 6719) ? 6 : INDEX_NONE;
				AddChatLineRaw(L.Category, L.Text, L.Fallback, L.RowBg, L.bRowBg, L.SenderName, PrefixSlot);
		});
	}
}

bool UAsdaChatWindowSubsystem::IsTabMenuShown() const
{
	return TabMenu && TabMenu->GetVisibility() != ESlateVisibility::Collapsed;
}

void UAsdaChatWindowSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnChatLine.Remove(HChatLine);
	}
	HChatLine.Reset();
	// Роспись ESC меню вкладок 2670 снимается вместе с подсистемой.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscPopup(FName(TEXT("CHAT_SETUP_Win")));
		WM->UnregisterEscCloser(FName(TEXT("CHAT_SETUP_Win")));
	}
	UnregisterZone();
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaChatWindowSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A п.0; КРИТ К-1 ревизии В1, `wave1_REVIEW.md` §3): ОКНА ЧАТА ЗАКРЫВАЮТСЯ В РЕЕСТРЕ.
	//   Было: сброс только забывал указатели (`ResetWidgets`), а окно 2500 оставалось живым в реестре окон. При сбросе БЕЗ смены
	//   мира (разрыв связи: три неудачные отправки подряд, `asda.Session.Reset`) следующий `EnsureWidgets` получал от
	//   `OpenWindowByPath` ТОТ ЖЕ виджет, и `BuildChannelCombo` строил узлы второй раз в старом дереве под теми же именами:
	//   строки `CHAT_NEW_SSbtn_Item*` к тому времени уже `URichTextBlock` (`UAsdaRichText::ApplyFormatted`) ⇒ фатал
	//   «Cannot replace existing object of a different class»; узлы того же класса молча пересоздавались поверх живых.
	//   Стало: окно чата, редактор вкладок 2590 и палитра 2700 снимаются с реестра и вьюпорта (тихо, без звука менеджера
	//   сообщений) — `EnsureWidgets` создаёт НОВЫЙ экземпляр, все узлы кода строятся в чистом дереве (как после травела).
	//   История ленты: правило прежнее — полный сброс чистит `LogHistory` ниже (оригинал при разрыве закрывает клиент,
	//   истории у него не остаётся вовсе — память `original-quits-client-on-disconnect`), смена карты её хранит, окно её
	//   перерисовывает из данных. Ключа отката нет: прежнее поведение — падение.
	HideTabEditor();   // 2590 + 2700: `CloseWindow` + снятие колеса + производные узлы
	UnregisterZone();  // слои «поверх всего» меню/списка каналов и колесо окна 2500 — как при разборке
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindowSilently(kZoneId);
	}
	ResetWidgets();
	VisibleLines.Reset();
	bInputActive = false;
	InputFocusGraceFrames = 0;
	bComboOpen = false;
	bScrollFrozen = false;
	bResizingTop = false; bResizingRight = false;
	bTipDragging = false;
	RibbonHoverNick.Empty();
	TabMenuTargetIdx = INDEX_NONE;
	// Антифлуд и «остывание» — счётчики ОТПРАВКИ, они относятся к сессии соединения.
	FloodFastCount = 0; FloodRepeatCount = 0;
	FloodLastSend = 0.0; FloodRepeatWindow = 0.0;
	FloodLastText.Empty();
	bFloodBlocked = false; FloodBlockLeft = 0.f; FloodLeft = 0.f;
	CooldownLeft = 0.f; CooldownTotal = 0.f; CooldownReason.Empty();
	Mouse.Reset();
	if (AsdaIsFullSessionReset(Reason))
	{
		LogHistory.Reset();   // лента прошлого персонажа
		IgnoreList.Reset();   // «не слышать» — список сессии, у нового персонажа он свой
	}
	// ★ [ОРИГИНАЛ] 25.10.2026 (Н1 ревизии перезахода): режим ввода, адресат шёпота и последний шептавший — только на НАЧАЛЕ
	//   новой сессии (`LeftWorld` / `EnteringWorld`), как `playerStateReset` + `CChat_Init` эталона; шапка ключа
	//   `asda.Chat.RelogResetWhisper` в начале файла. Окно здесь уже закрыто — приглашение пересоберёт `EnsureWidgets`.
	if (AsdaIsFullSessionReset(Reason) && !AsdaSessionResetKeepsWorldAlive(Reason)
		&& CVarChatRelogResetWhisper.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaChatWin, Log, TEXT("[чат] сброс сессии (%s): режим ввода %d → 4, адресат шёпота «%s» и последний шептавший «%s» сняты"),
			AsdaSessionResetReasonName(Reason), InputState, *WhisperTarget, *LastWhisperFrom);
		InputState = 4;            // `tabSelMode = 4` — «[Общий]» (chat.c:679)
		WhisperTarget.Empty();     // chat+2532
		LastWhisperFrom.Empty();   // chat+2552
	}
}

void UAsdaChatWindowSubsystem::ResetWidgets()
{
	HelpTip.Hide(GetGameInstance(), EAsdaTipOwner::ChatWin);   // ★ 30.10.2026: подсказка принадлежала старому дереву
	ChatWidget = nullptr;
	LinesBox = nullptr;
	HomeBtnSrc = nullptr;
	ResizeTopSrc = nullptr;
	UDbtnSrc = nullptr;
	HbtnSrc = nullptr;
	QueBtnSrc = nullptr;
	QueTipSrc = nullptr;
	QueTipXSrc = nullptr;
	QueTipMoveSrc = nullptr;
	PressedNode = nullptr;
	bTipDragging = false;
	bChannelComboBuilt = false;
	ComboLabel = nullptr;
	ComboList = nullptr;
	ComboRows.Reset();
	bComboOpen = false;
	TabQuads.Reset(); TabCaps.Reset();
	TabArrowL = nullptr; TabArrowR = nullptr; TabArrowLCap = nullptr; TabArrowRCap = nullptr;
	bTabsBuilt = false;
	bResizingTop = false;
	bResizingRight = false;
	ResizeRightSrc = nullptr;
	// ★ Аудит №3 (B3): `Mouse.Reset()` = прежние `bPrevLmbDown = false` (здесь) и
	//   `bPrevRmbDown = false` (ниже по этой же функции) — оба латча жили в одном месте.
	Mouse.Reset();
	// ★ Перенесено в ассет (15.09, разрешение владельца): флаг bWarnedSpanSlot удалён вместе с NormalizeControlSlot.
	bScrollFrozen = false;
	bQueTipTextsBuilt = false;
	// ЭТАП 4: редактор вкладок 2590 и палитра 2700 — отдельные окна, умирают вместе с миром.
	//   ★ САМИ ВКЛАДКИ (ChatTabs/ChatTabsEdit/ChanOpts) — чистые данные, травел их не трогает.
	TabEditorWidget = nullptr;
	ColorWidget = nullptr;
	TabEditorRelays.Reset();
	bTabEditorBuilt = false;
	bColorBuilt = false;
	// Столбец клон-ячеек списка вкладок 2597 живёт внутри окна 2590 — умирает вместе с ним.
	EditTabCells.Reset(); EditTabCellCaps.Reset();
	EditTabArrowUp = nullptr; EditTabArrowDn = nullptr;
	EditTabArrowUpCap = nullptr; EditTabArrowDnCap = nullptr;
	EditTabCellHost = nullptr;
	bTabEditorListBuilt = false;
	EditTabHoverIdx = -1; EditArrowHoverIdx = -1;
	// ЭТАП 3: всплывающие меню и плиты подсветки ленты — виджеты, умирают вместе с миром.
	NickMenu = nullptr;
	TabMenu = nullptr;
	RibbonRowBg = nullptr;
	RibbonSelBg = nullptr;
	RibbonNameBg = nullptr;
	VisibleLines.Reset();
	RibbonHoverIdx = -1;
	RibbonSelIdx = -1;
	RibbonHoverNick.Reset();
	RibbonHoverPlain.Reset();
	MenuDragTarget = nullptr;
	MenuPressedBtn = -1;
	// (латч ПКМ сброшен общим `Mouse.Reset()` выше — ★ Аудит №3 (B3))
	// LogHistory / CurH / bExpanded / bQueTipVisible / IgnoreList — чистые данные, виджетам не
	//   принадлежат: травел их не рвёт, окно восстанавливается в том же состоянии.
	//   ★ Игнор-лист живёт до выхода из игры (эталон: в файл не пишется) — травел его не чистит.
}

// ★★★ 2026-08-06: РУЧНАЯ РЕГИСТРАЦИЯ ЗОНЫ УБРАНА — дубль (см. тот же комментарий в
//   AsdaChatSubsystem::RegisterZone). Окно 2500 открыто через реестр, поэтому его хит-тест
//   ведёт общая система по данным .JUI. `UnregisterZone` ОСТАЁТСЯ рабочей: она ещё и снимает
//   слои «поверх всего» (PopTopmost) — без неё окно после травела осталось бы на z=9000+.
void UAsdaChatWindowSubsystem::RegisterZone()
{
}

void UAsdaChatWindowSubsystem::UnregisterZone()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ Слой «поверх всего» снимаем ВСЕГДА: иначе после травела окно осталось бы на z=9000+
		//   и перекрывало бы весь интерфейс (z хранится в реестре и сам не откатывается).
		WM->PopTopmost(kMenuClaimId);
		WM->PopTopmost(kComboClaimId);
		WM->UnregisterUiZone(kZoneId);
		WM->UnregisterWheelHandler(kZoneId);
		WM->UnregisterWheelHandler(kTabEditorId);
	}
}

// Тик: самовосстановление окна после травела + мышь по узлам управления.
void UAsdaChatWindowSubsystem::Tick(float DeltaTime)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	// ★★★ СВЕРКА ФЛАГА НАБОРА — ДО ВСЕХ ГЕЙТОВ ТИКА. Именно там, где окна чата нет (травел, смерть
	//   виджетов, окно ещё не построено), залипший латч и глушил ВЕСЬ интерфейс: гейт
	//   `IsTextInputFocused` стоит в системном меню, хотбаре и диспетчере клавиш, а сюда тик
	//   не доходил и снять латч было некому.
	ReconcileInputFocus();
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// Чат живёт ТОЛЬКО в мире (эталон: CChat строится при входе, g_GameState == 1).
	if (!LM || LM->CurrentMapId < 0 || !WS || !IsValid(WS->LocalPlayerActor)) { HelpTip.Hide(GI, EAsdaTipOwner::ChatWin); return; }
	if (!EnsureWidgets()) { HelpTip.Hide(GI, EAsdaTipOwner::ChatWin); return; }
	TickPointer(DeltaTime);
	TickAntiFlood(DeltaTime);
}

// ══════════════════════════════ ПОСТРОЕНИЕ ОКНА ═══════════════════════════════════════════════

// ★ Перенесено в ассет (15.09, разрешение владельца). Здесь была таблица `ChatNodeState` — исходная
//   видимость узлов WBP_CHAT_NEW после `CChat_BuildTabUI` (JUI `Visible` + CControl_SetVisible, chat.c),
//   которую код выставлял при КАЖДОМ построении окна. Теперь эта видимость лежит в самом ассете;
//   узлы, значения и ссылки на эталон — шаг `Tools/asda_ui_chat_markup_visibility.py`
//   (зарегистрирован в `asda_ui_post_import.STEPS`, переживает перегенерацию). Справочная таблица
//   эталона — в шапке AsdaChatWindowSubsystem.h. Сверка до/после путём игрока —
//   `Reports/dedup_audit/chat_markup_to_asset.md`.

bool UAsdaChatWindowSubsystem::EnsureWidgets()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(ChatWidget))
	{
		return true;
	}
	ResetWidgets();

	// z = 689: на единицу НИЖЕ журнала 2570 (690). Окна встык по вертикали (580…758 и 508…578),
	//   поэтому спорить им не за что, но порядок фиксируем явно, чтобы не зависеть от случайностей.
	ChatWidget = WM->OpenWindowByPath(kZoneId, kChatWbp, kChatZ, ESlateVisibility::SelfHitTestInvisible);
	if (!ChatWidget)
	{
		UE_LOG(LogAsdaChatWin, Error, TEXT("[чат-окно] WBP_CHAT_NEW не загрузился (%s)"), kChatWbp);
		return false;
	}
	RegisterZone();
	// ★★★ Колесо мыши над лентой 2521 и рядом вкладок 2501 — через ЕДИНЫЙ реестр окон.
	WM->RegisterWheelHandler(kZoneId,
		FAsdaUiWheelHandler::CreateUObject(this, &UAsdaChatWindowSubsystem::OnWheelChatWindow));

	// ★★★ ПОРЯДОК КРИТИЧЕН (грабля №5, стоила круга на окне 2570): указатели на ИНТЕРАКТИВ
	//   разрешаем ДО декорационного прохода. Проход ниже гасит «все прочие» узлы в
	//   HitTestInvisible; если указатели ещё nullptr, под гашение попадает и интерактив,
	//   и потом «ни одна кнопка не работает».
	//   ★ ГРАБЛЯ №1: тип узла НЕ предполагаем — ищем среди ВСЕХ UWidget и храним как UWidget*.
	//   Импортёр даёт интерактиву точное имя idc, планам — имя с idx-суффиксом, поэтому ищем
	//   «точное имя, затем вхождение подстроки».
	HomeBtnSrc  = FindNodeByName(TEXT("CHAT_NEW_HomeBtn"),  TEXT("CHAT_NEW_HomeBtn"));
	UDbtnSrc    = FindNodeByName(TEXT("CHAT_NEW_UDbtn"),    TEXT("CHAT_NEW_UDbtn"));
	HbtnSrc     = FindNodeByName(TEXT("CHAT_NEW_Hbtn"),     TEXT("CHAT_NEW_Hbtn"));
	QueBtnSrc   = FindNodeByName(TEXT("CHAT_NEW_Que_btn"),  TEXT("CHAT_NEW_Que_btn"));
	QueTipSrc   = FindNodeByName(TEXT("CHAT_NEW_Que_btn_tooltip_grp"), TEXT("CHAT_NEW_Que_btn_tooltip_grp"));
	QueTipXSrc  = FindNodeByName(TEXT("CHAT_NEW_Que_btn_toolti_X"),    TEXT("CHAT_NEW_Que_btn_toolti_X"));
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] узлы: HomeBtn=%s UDbtn=%s Hbtn=%s Que_btn=%s справка=%s крестик=%s"),
		HomeBtnSrc  ? *HomeBtnSrc->GetClass()->GetName()  : TEXT("НЕТ"),
		UDbtnSrc    ? *UDbtnSrc->GetClass()->GetName()    : TEXT("НЕТ"),
		HbtnSrc     ? *HbtnSrc->GetClass()->GetName()     : TEXT("НЕТ"),
		QueBtnSrc   ? *QueBtnSrc->GetClass()->GetName()   : TEXT("НЕТ"),
		QueTipSrc   ? *QueTipSrc->GetClass()->GetName()   : TEXT("НЕТ"),
		QueTipXSrc  ? *QueTipXSrc->GetClass()->GetName()  : TEXT("НЕТ"));

	// ★ Перенесено в ассет (15.09, разрешение владельца): здесь стоял NormalizeControlSlot(HomeBtn) —
	//   перевод растягивающего анкора в POINT. В WBP_CHAT_NEW у CHAT_NEW_HomeBtn (и у всех узлов окна)
	//   анкор уже POINT (дамп ассета 15.09) — подпорка ничего не делала.

	// Зона захвата верхней кромки — создаётся ДО декорационного прохода, чтобы он её не погасил.
	EnsureResizeHandle();
	EnsureResizeRight();

	// ── Декорационный проход по ВСЕМ узлам окна ──────────────────────────────────────────────
	if (UWidgetTree* Tree = ChatWidget->WidgetTree)
	{
		TArray<UWidget*> All;
		Tree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();

			// ★ Перенесено в ассет (15.09, разрешение владельца): ПОКАЗАН ли узел — решает WBP
			//   (исходная видимость по эталону, шаг Tools/asda_ui_chat_markup_visibility.py). Свёрнутое
			//   в ассете остаётся свёрнутым; ниже код задаёт только политику хит-теста, шрифт и курсор
			//   показанных узлов. По событиям (справка 2580, кулдаун 2618, ввод) видимость меняет код.
			if (W->GetVisibility() == ESlateVisibility::Collapsed)
			{
				continue;
			}

			// ★★★ 2607 — ЕДИНСТВЕННЫЙ ЧЕКБОКС ЭТОГО ОКНА, И ЕМУ НУЖЕН ОСОБЫЙ РЕЖИМ.
			//   Скин `Chat_new_ck3` имеет Type="CheckButton" ⇒ импортёр создал `UCheckBox`.
			//   Если оставить его хит-тестируемым (Visible), `SCheckBox` переключит СВОЙ кадр по
			//   ОТПУСКАНИЮ ЛКМ, а наш диспетчер уже переключил флаг по НАЖАТИЮ — кадр вернётся к
			//   старому («галочка моргает», ровно грабля Д26.1 из окна 2590).
			//   Делаем кнопку ЧИСТЫМ ИНДИКАТОРОМ: клик разбирает TickPointer по геометрии (это и
			//   есть механизм оригинала — `uiControlHitTest @0x12DB470` по прямоугольникам планов),
			//   а кадр ставит `SetIsChecked` в RefreshScrollLockVisual().
			//   ★ HitTestInvisible НЕ ломает хит-тест окна: `HitTestWidgetTree` отбрасывает только
			//   Collapsed/Hidden, а `IsUnderCursor` работает по `GetCachedGeometry()`.
			if (W == HbtnSrc)
			{
				W->SetVisibility(ESlateVisibility::HitTestInvisible);
				continue;
			}

			// Узлы управления мышь ЛОВЯТ (Visible), остальное — click-through.
			if (W == HomeBtnSrc || W == UDbtnSrc || W == QueBtnSrc || W == QueTipXSrc)
			{
				W->SetVisibility(ESlateVisibility::Visible);
				W->SetCursor(EMouseCursor::Hand);
				continue;
			}
			if (W == ResizeRightSrc)
			{
				W->SetCursor(EMouseCursor::ResizeLeftRight);
			}
			else if (W == ResizeTopSrc)
			{
				W->SetVisibility(ESlateVisibility::Visible);
				W->SetCursor(EMouseCursor::ResizeUpDown);
				continue;
			}

			// ★ ПОДЛОЖКА окна перехватывает мышь — «клик в виджете работает только в виджете»
			//   (решение юзера 2026-08-04; эталон, наоборот, прокидывает клик по 2521 в мир
			//   через whitelist game_main.c:5748 — это осознанное отступление, НЕ «чинить»).
			if (N.StartsWith(TEXT("CHAT_NEW_back")) || N.StartsWith(TEXT("back2")))
			{
				W->SetVisibility(ESlateVisibility::Visible);
				continue;
			}

			// ★ ГРАБЛЯ №7: autowrap выключаем всем однострочным текстам, кегль приводим к 8.
			if (UTextBlock* Tb = Cast<UTextBlock>(W))
			{
				Tb->SetAutoWrapText(false);
				FSlateFontInfo F = Tb->GetFont();
				F.Size = (int32)kFontPx;
				Tb->SetFont(F);
				Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				continue;
			}

			// Вся прочая декорация (планы импортёра — UImage с Visibility=Visible) мышь НЕ ловит:
			//   Visible-картинка в Slate перехватывает наведение и накрывает узлы управления.
			if (Cast<UImage>(W) != nullptr)
			{
				W->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
	}

	// Панель строк + восстановление истории после травела.
	LinesBox = MakeLinesPanel();
	RebuildLines();

	// ★ Кнопка 2528 лежит в ТОЙ ЖЕ группе, что и лента, и в дереве стоит РАНЬШЕ неё: при
	//   одинаковом z панель строк накрывала бы её. Эталон рисует кнопку поверх списка.
	if (UCanvasPanelSlot* HS = AsdaUi::SlotOf(HomeBtnSrc))
	{
		HS->SetZOrder(10);
	}

	// Подписи окна справки (loc 5400-5406) и список каналов 2608 — один раз на экземпляр окна.
	BuildQueTipTexts();
	BuildChannelCombo();
	BuildInputWidgets();
	BuildChatTabs();
	// ЭТАП 3: всплывающие меню 80000 / 2670 и три плиты подсветки ленты 2521.
	//   ★ Строго ПОСЛЕ декорационного прохода (он гасит мышь у всех UImage) и ПОСЛЕ BuildQueTipTexts
	//   (там читается игровой шрифт TipFont, которым подписываются кнопки меню).
	BuildContextMenus();
	EnsureRibbonHighlights();
	SetQueTipVisible(bQueTipVisible);
	// Кадр тумблера 2607 переживает пересоздание окна (травел): флаг живёт в подсистеме.
	RefreshScrollLockVisual();

	// ★★★ РАСКЛАДКА ПО ПРАВИЛАМ JUI. Обязательна ДАЖЕ на свежем WBP: импортёр ставит
	//   CHAT_NEW_TextMove как ТЕКСТОВЫЙ узел (NoImage + вертикальное центрирование подписи),
	//   поэтому его слот уезжает на +9 px вниз относительно контрола 2534 (582 → 591).
	//   Здесь всё возвращается к эталонным rect'ам, и заодно восстанавливается состояние
	//   свёрнуто/развёрнуто и перетаскивание после травела.
	// ★★★ РАЗМЕР ОКНА ИЗ ini ИГРЫ (решение юзера 2026-08-07: «все окна пусть сохраняют … и оттуда
	//   каждый раз достают»). Позиция чата не сохраняется — окно не таскается (прямое требование
	//   юзера), а вот РАЗМЕР по обеим осям сохраняется. Читается ДО `RefreshLayout`, чтобы окно
	//   сразу построилось нужного размера, а не прыгнуло в первом кадре.
	//   ⚠ `bLayoutToggled` едет вместе с размером отдельным признаком: он решает, к чему прибит НИЗ
	//   окна (758 из .JUI до первого касания против 750 после) — без него восстановленная высота
	//   встала бы на 8 px мимо. Память кнопки 2605 (`SavedW/SavedH`) ЖИВЁТ ТОЛЬКО В СЕССИИ, как и
	//   требовал юзер: сюда пишется ТЕКУЩИЙ размер окна, а не «запомненный кнопкой».
	if (WM)   // реестр окон получен выше по функции — им же читаем сохранённую раскладку
	{
		// Позиция окна справки 2580 (её двигает наш код, поэтому и читаем её мы сами).
		//   Клампится собственным `ApplyQueTipOffset` в базовый экран — за край не уедет.
		FVector2D TipOff;
		if (WM->GetSavedWindowOffset(FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_QueTip")), TipOff))
		{
			ApplyQueTipOffset(TipOff);   // именно через него: он и клампит, и раскладывает
		}
		FVector2D Sz; bool bToggled = false;
		if (WM->GetSavedWindowSize(FName(TEXT("CHAT_NEW")), Sz, &bToggled))
		{
			CurW = FMath::Clamp((float)Sz.X, kWinMinW, kWinMaxW);
			CurH = FMath::Clamp((float)Sz.Y, kHCollapsed, kHExpanded);
			bLayoutToggled = bToggled;
			bExpanded = (CurH > (kHCollapsed + kHExpanded) * 0.5f);
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-окно] размер восстановлен из ini: %.0fx%.0f (низ закреплён=%d)"),
				CurW, CurH, bLayoutToggled ? 1 : 0);
		}
	}
	RefreshLayout();
	// ★ Ширина переноса строк ленты считается СРАЗУ при постройке, а не только при растягивании.
	//   Иначе LinesWrapW = 0, и каждая новая строка получала бы дефолт `UAsdaRichText::ApplyFormatted`
	//   (228/200 px — авторская ширина пузыря ANI_TXT), то есть рвалась бы посреди ленты шириной 439.
	ApplyLinesWidth();

	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] CHAT_NEW (2500) построено: z=%d, окно (%.0f,%.0f,%.0fx%.0f), лента 2521 rect(%.0f,%.0f,%.0fx%.0f), строк в истории %d"),
		kChatZ, kWinX, BaseTop(), kWinW, BaseHeight(),
		kListX, kListY, kListW, kListH, LogHistory.Num());
	return true;
}

// Невидимая зона захвата верхней кромки окна. В WBP такого узла нет — в оригинале у окна 2500
// вообще нет растягивания мышью (есть только тогл 2605), поэтому зона добавлена ПО ПРЯМОМУ
// ТРЕБОВАНИЮ ЮЗЕРА. Ничего не рисует (кисть прозрачная) — только ловит мышь, как ручка RESIZE
// у системного журнала. Лежит в дереве виджетов, поэтому видна и редактируема в UMG.
void UAsdaChatWindowSubsystem::EnsureResizeHandle()
{
	if (ResizeTopSrc || !ChatWidget || !ChatWidget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ChatWidget->WidgetTree->RootWidget);
	if (!Root) { return; }
	UBorder* B = ChatWidget->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), FName(TEXT("CHAT_NEW_ResizeTop")));
	if (!B) { return; }
	B->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));   // прозрачная — только хит-тест
	B->SetPadding(FMargin(0.f));
	B->SetVisibility(ESlateVisibility::Visible);
	B->SetCursor(EMouseCursor::ResizeUpDown);
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(B))
	{
		AsdaJui::SetChatSlot(CS, kWinX, BaseTop() - kResizeBandOut, kWinW, kResizeBandH);
		CS->SetZOrder(60);   // выше плит окна, чтобы кромку было за что взять
	}
	ResizeTopSrc = B;
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] создана зона растягивания верхней кромки CHAT_NEW_ResizeTop (%.0f×%.0f, пределы высоты %.0f…%.0f)"),
		kWinW, kResizeBandH, kHCollapsed, kHExpanded);
}

// ★★★ ЗОНА ЗАХВАТА ПРАВОЙ КРОМКИ — ГОРИЗОНТАЛЬНОЕ РАСТЯГИВАНИЕ (отступление).
//   РЕВЕРС ПОДТВЕРЖДАЕТ: в оригинале окно 2500 не меняет размер НИ ПО ОДНОЙ ОСИ
//   (разбор целиком — у поля CurW в заголовке). Поэтому делаем ТОЧНО ТАК ЖЕ,
//   как уже сделано с вертикалью: отдельная прозрачная полоса-кромка, без выдуманных кнопок.
//   Почему НЕ сделали верхнюю кромку «угловой» (обе оси сразу): вертикальный
//   ресайз уже отлажен и проверен юзером, а совмещённый хват менял бы вторую ось
//   при любом движении мыши — то есть ровно тот регресс, который запрещено допускать.
void UAsdaChatWindowSubsystem::EnsureResizeRight()
{
	if (ResizeRightSrc || !ChatWidget || !ChatWidget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ChatWidget->WidgetTree->RootWidget);
	if (!Root) { return; }
	UBorder* B = ChatWidget->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), FName(TEXT("CHAT_NEW_ResizeRight")));
	if (!B) { return; }
	B->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	B->SetPadding(FMargin(0.f));
	B->SetVisibility(ESlateVisibility::Visible);
	B->SetCursor(EMouseCursor::ResizeLeftRight);
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(B))
	{
		AsdaJui::SetChatSlot(CS, kWinX + BaseWidth() - kResizeBandOut, BaseTop(), kResizeBandW, BaseHeight());
		CS->SetZOrder(60);
	}
	ResizeRightSrc = B;
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] создана зона растягивания правой кромки CHAT_NEW_ResizeRight (%.0f px, пределы ширины %.0f…%.0f)"),
		kResizeBandW, kWinMinW, kWinMaxW);
}

// ★★★ ПЕРЕНОС СТРОК ЛЕНТЫ ПО ТЕКУЩЕЙ ШИРИНЕ — главное место, где ломается
//   горизонтальный ресайз: ширина окна изменилась, а строки остались порезаны по старой.
//   В оригинале ширина переноса хранится в CChat+0x5D4 (chatLineMaxWidth) — это ПАРАМЕТР, а не
//   константа; у нас его роль играет фактическая ширина ленты (439 + надбавка).
//   ★ ГРАБЛЯ №7 (autowrap в CanvasPanel ломает раскладку) ЗДЕСЬ НЕ ДЕЙСТВУЕТ: строки лежат
//   в UScrollBox (вертикальный бокс), а не в канвасе — там перенос штатен и безопасен.
// ★ ГРАБЛЯ (найдена 2026-08-05 при переносе горизонтали на окно 2570): строка ленты — это
//   **URichTextBlock**, а не UTextBlock. `UAsdaRichText::ApplyFormatted` ПОДМЕНЯЕТ исходный
//   UTextBlock на rich-виджет (старый узел уходит из дерева и переименовывается в транзиент), а
//   URichTextBlock НЕ наследник UTextBlock — оба идут от `UTextLayoutWidget` (TextWidgetTypes.h).
//   ⇒ прежний `Cast<UTextBlock>` не находил НИ ОДНОЙ строки, и пересчёт переноса молча ничего не
//   делал. Приводим к общему предку — у него есть и SetAutoWrapText, и SetWrapTextAt.
void UAsdaChatWindowSubsystem::ApplyLinesWidth()
{
	if (!LinesBox) { return; }
	const float WrapW = FMath::Max(40.f, kListW + (BaseWidth() - kWinW) - 6.f);
	auto Fix = [WrapW](UWidget* W)
	{
		if (UTextLayoutWidget* Tw = Cast<UTextLayoutWidget>(W))
		{
			Tw->SetWrapTextAt(WrapW);
			Tw->SetAutoWrapText(true);
		}
	};
	for (int32 i = 0; i < LinesBox->GetChildrenCount(); ++i)
	{
		UWidget* Ch = LinesBox->GetChildAt(i);
		Fix(Ch);
		if (UBorder* Bd = Cast<UBorder>(Ch)) { Fix(Bd->GetContent()); }
	}
	LinesWrapW = WrapW;
}

// ══════════════════ РАСКЛАДКА ОКНА ПО ПРАВИЛАМ JUI (единственный источник геометрии) ══════════
//
// Правила сняты с gamemain_jui.ini (режимы AlignXPos/AlignYPos/AlignXSize/AlignYSize) и сверены
// с фактическими слотами импортированного WBP при L=1, T=580, H=178 — совпадают до десятых:
//
//   узел (имя в WBP)                правило                                при 1,580,178
//   CHAT_NEW_back_grp               (L+1, T+1, 472, H·0.98)                (2, 581, 472×174.4)
//     CHAT_NEW_back_1_0   локально  (0, 0, 472, H·0.98)
//     back2_2_0           локально  (7, 8, 460, H·0.98·0.88)               (9, 589, 460×153.5)
//   CHAT_NEW_TextMove               (L+1, T+2, 474, 32)                    (2, 582, 474×32)
//   CHAT_NEW_Radio_B_*              (L+9, T+11, 79, 25)                    (10, 591, 79×25)
//   CHAT_NEW_C_List_grp             (L+27, T+45, 439, H−80)                (28, 625, 439×98)
//     CHAT_NEW_C_NOR_11_0 локально  (0, 0, 439, H−80)
//     CHAT_NEW_HomeBtn    локально  (439−16, 2, 13, 13)                    (451, 627, 13×13)
//   CHAT_NEW_UDbtn                  (L+474, T+2, 22, 22)                   (475, 582, 22×22)
//   CHAT_NEW_Quw_btn                (L+474, T+27, 22, 22)                  (475, 607, 22×22)
//   CHAT_NEW_Hbtn                   (L+474, T+52, 22, 22)                  (475, 632, 22×22)
//   SizeWin_grp                     (L+0, T+H−170, 465, 158)               (1, 588, 465×158)
//   CHAT_NEW_Que_btn_tooltip_grp    (L+445, T−209, 276, 310)               (446, 371, 276×310)
//   CHAT_NEW_scr_9_*                (L+444, T+26, …)  — эталон его прячет, двигаем для порядка
//
// ★ Высоты 0.98 и 0.88 — это AlignYSize mode 0x20 («процент от родителя»): back = 98 % высоты
//   окна, back2 = 88 % высоты back. Лента: AlignYSize mode 0x08 («высота родителя + sb»),
//   sb = −80 ⇒ H−80. HomeBtn: AlignXPos mode 0x02 («правый край родителя + a»), a = −15,
//   Origin.x = −1 ⇒ правый край ленты − 16.
void UAsdaChatWindowSubsystem::BuildLayoutTable(float L, float T, float H, float W, FVector2D TipOff, TArray<FChatLayoutEntry>& Out)
{
	// ★★★ ГОРИЗОНТАЛЬНОЕ РАСТЯГИВАНИЕ (отступление, см. шапку EnsureResizeRight).
	//   DW — НАДБАВКА к авторской ширине 474. Всё, что в JUI шло на всю ширину окна, получает +DW;
	//   всё, что прижато к ПРАВОМУ краю, сдвигается на +DW. Левый край (якорь) не двигается.
	const float DW = FMath::Max(0.f, W - kWinW);
	const float BackH  = H * 0.98f;          // AlignYSize "32 0 98"  — 98 % высоты окна
	const float Back2H = BackH * 0.88f;      // AlignYSize "32 0 88"  — 88 % высоты back
	const float ListH  = H - 80.f;           // AlignYSize "8 0 -80"  — высота окна − 80

	auto Add = [&Out](const TCHAR* N, bool bLocal, float X, float Y, float Wd, float Ht)
	{
		FChatLayoutEntry E; E.Name = N; E.bLocal = bLocal;
		E.X = X; E.Y = Y; E.W = Wd; E.H = Ht;
		Out.Add(E);
	};

	Add(TEXT("CHAT_NEW_back_grp"),     false, L + 1.f,   T + 1.f,  472.f + DW, BackH);
	Add(TEXT("CHAT_NEW_back_1_0"),     true,  0.f,       0.f,      472.f + DW, BackH);
	Add(TEXT("back2_2_0"),             true,  7.f,       8.f,      460.f + DW, Back2H);

	Add(TEXT("CHAT_NEW_TextMove"),     false, L + 1.f,   T + 2.f,  474.f + DW, 32.f);

	Add(TEXT("CHAT_NEW_Radio_B_4_0"),  false, L + 9.f,   T + 11.f,  79.f, 25.f);
	Add(TEXT("CHAT_NEW_Radio_B_OPT0"), false, L + 9.f,   T + 11.f,  79.f, 25.f);
	Add(TEXT("CHAT_NEW_Radio_B_Text"), false, L + 9.f,   T + 16.5f, 79.f, 25.f);  // подпись по центру бокса

	Add(TEXT("CHAT_NEW_C_List_grp"),   false, L + 27.f,  T + 45.f, 439.f + DW, ListH);
	Add(TEXT("CHAT_NEW_C_NOR_11_0"),   true,  0.f,       0.f,      439.f + DW, ListH);
	// ★ 07.10.2026 (отчёт `Reports/owner_1007/label_band_and_8500.md`): узел-подпись ленты 2520 стоит верхом на
	//   42 (запечённая импортёром центровка (98 − 14)/2), а низ — по низу ленты: высота ListH − 42 (не меньше строки
	//   14), как в ассете после шага `asda_ui_label_band`. БЫЛО ListH — узел вылезал на 42 − 35 = 7 px ниже рамки
	//   чата, и зона окна (`GetWindowScreenRect`, фильтр `CHAT_NEW_C_List*`) съедала щелчок в мир.
	Add(TEXT("CHAT_NEW_C_List"),       true,  0.f,      42.f,      439.f + DW, FMath::Max(ListH - 42.f, 14.f));
	Add(TEXT("CHAT_NEW_HomeBtn"),      true,  423.f + DW, 2.f,      13.f, 13.f);

	Add(TEXT("CHAT_NEW_UDbtn"),        false, L + 474.f + DW, T + 2.f,  22.f, 22.f);
	Add(TEXT("CHAT_NEW_Quw_btn"),      false, L + 474.f + DW, T + 27.f, 22.f, 22.f);
	Add(TEXT("CHAT_NEW_Hbtn"),         false, L + 474.f + DW, T + 52.f, 22.f, 22.f);

	Add(TEXT("SizeWin_grp"),           false, L,         T + H - kSizeWinDY, 465.f + DW, 158.f);
	// ★ Планы скина комбобокса 2608 внутри группы SizeWin (origin 1,588 при исходной раскладке):
	//   тело растягивается на остаток ширины, СТРЕЛКА кладётся СВОИМ размером 20×20 к правому краю
	//   (импортёр масштабировал её пропорционально до 18.2×20 — отсюда «сплюснутая иконка»).
	//   ★ Комбобокс прижат к ЛЕВОМУ краю окна (JUI AlignXPos "0 0 0") — при растягивании ВПРАВО
	//   он стоит на месте. Это не недоделка: тянуть его некуда, свободное место уходит полю ввода.
	Add(TEXT("CHAT_NEW_SSbtn_29_0"),   true,  kComboX - 1.f,                    kSizeWinComboLocalY, kComboW - kComboArrowW, kComboH);
	// Поле ввода 2609 тянется на всю добавку ширины (полоса кулдауна 2618 идёт за ним, ApplyCooldownBar).
	Add(TEXT("CHAT_NEW_Edit2"),        true,  kEditX - 1.f, 138.f + 3.f, kEditW + DW, 14.f);
	// ★★★ ЖИВОЙ ТЕСТ 2026-08-06 («строка ввода и всё рядом не тянутся»): ПЛИТА поля ввода —
	//   ОТДЕЛЬНЫЙ узел `CHAT_NEW_Edit2_34_0` (план скина Black_EditBox), и в таблице его НЕ БЫЛО.
	//   Тянулся только сам UEditableText (невидимый прямоугольник), а чёрная подложка оставалась
	//   на авторских 336 px — именно это и видно на скриншоте юзера.
	Add(TEXT("CHAT_NEW_Edit2_34_0"),   true,  kEditX - 1.f, 138.f,       kEditW + DW, kEditH);
	// ★ Кнопка справки «?» 2537 (JUI local «446 139», 16×16) прижата к ПРАВОМУ краю строки ввода —
	//   едет вместе с ним на +DW. Её тоже не было в таблице (юзер: «и вопросик»).
	Add(TEXT("CHAT_NEW_Que_btn"),      true,  446.f + DW,   139.f,       16.f, 16.f);
	// ── ЛЕГАСИ-УЗЛЫ НИЖНЕЙ СТРОКИ (эталон гасит их при построении, но раскладку держим честной:
	//    закон полного структурного разбора — ни одного контрола с намертво зашитым X/W) ────────
	Add(TEXT("CHAT_NEW_Bar_25_0"),     true,  31.f,         140.f,       432.f + DW, 17.f);   // 2532
	Add(TEXT("CHAT_NEW_Bar_25_1"),     true,  31.f,         140.f,       432.f + DW, 17.f);   // 2532
	Add(TEXT("CHAT_NEW_Bar_TEXT"),     true,  31.f,         140.f,       432.f + DW, 17.f);   // 2535
	Add(TEXT("CHAT_NEW_Edit_27_0"),    true,  27.f,         138.f,       438.f + DW, 20.f);   // 2533
	Add(TEXT("CHAT_NEW_Edit"),         true,  27.f,         138.f,       438.f + DW, 20.f);   // 2533
	Add(TEXT("CHAT_NEW_Enter_28_0"),   true,  446.f + DW,   140.f,       16.f, 16.f);         // 2529
	// Индикаторы раскладки ввода 2512/2511 (JUI local «11 142», 12×12) прижаты ВЛЕВО — стоят.
	Add(TEXT("CHAT_NEW_ENG_30_0"),     true,  11.f,         142.f,       12.f, 12.f);
	Add(TEXT("CHAT_NEW_HAN_31_0"),     true,  11.f,         142.f,       12.f, 12.f);
	// Наше приглашение режима (скрыто решением юзера, Д6) — держим на ширине поля, чтобы дамп
	//   и возможное включение обратно не расходились с реальностью.
	Add(TEXT("CHAT_NEW_InputPrefix"),  false, L + kEditX + 2.f, kEditY + 4.f, kEditW + DW - 6.f, 14.f);
	// ★ Подпись полосы кулдауна 2619 `CHAT_NEW_Bar_TEXT2` — В JUI её rect БАЙТ В БАЙТ совпадает с
	//   2618 и с полем 2609 («127 138 336×20», gamemain_jui.ini:93655-93662), а текст центрируется
	//   ПО ЭТОМУ ПРЯМОУГОЛЬНИКУ ⇒ без надбавки подпись оставалась бы центром на авторских 336 px,
	//   то есть уезжала бы влево от середины растянутого поля.
	Add(TEXT("CHAT_NEW_Bar_TEXT2"),    true,  kEditX - 1.f, 138.f, kEditW + DW, kEditH);
	Add(TEXT("CHAT_NEW_SSbtn_29_1"),   true,  kComboX - 1.f + kComboW - kComboArrowW, kSizeWinComboLocalY, kComboArrowW, kComboH);
	// Окно справки 2580 стоит на СВОЁМ месте из JUI и не зависит от высоты чата; TipOff — ручное
	//   перетаскивание (отступление по требованию юзера).
	Add(TEXT("CHAT_NEW_Que_btn_tooltip_grp"), false,
	    kTipX + AsdaJui::kBaseW * kTipStartShiftFrac + TipOff.X, kTipY + TipOff.Y, kTipW, kTipH);

	// Скрытые эталоном узлы тоже держим на местах — чтобы дамп показывал правду, а не мусор.
	// ★ 19.09.2026 (ревизия правки полос, СРЕДН): числа полос приведены к ИСПРАВЛЕННОЙ геометрии ассета
	//   (эталон `uiScrollbarLayoutPlanes@0x12FE500`: у горизонтальной полосы тянется только жёлоб, стрелки и
	//   бегунок — собственного размера скина). Прежде здесь стояла старая растянутая раскладка импортёра, и
	//   эта таблица в игре ОТМЕНЯЛА правку ассета. Связь с ассетом: X_кода = X_ассета − 1, Y_кода = Y_ассета + 188.
	Add(TEXT("CHAT_NEW_scr_9_0"),      false, L + 444.f + DW, T + 26.f, 20.f, 10.f);
	Add(TEXT("CHAT_NEW_scr_9_1"),      false, L + 444.f + DW, T + 26.f, 10.f, 10.f);
	Add(TEXT("CHAT_NEW_scr_9_2"),      false, L + 454.f + DW, T + 26.f, 10.f, 10.f);
	Add(TEXT("CHAT_NEW_scr_9_3"),      false, L + 454.f + DW, T + 26.f,  5.f, 10.f);
	// ★ В дереве ДВА скроллбара: 2536 (`_9_*`) и безымянный (`_8_*`, JUI Visible=0). Оба скрыты
	//   эталоном, но X у них был зашит намертво — при растягивании они «выныривали» бы слева.
	//   ★ Полоса `_8` своя, а не копия `_9`: прежде она получала геометрию `_9`, хотя в ассете у неё другой
	//   скин (`Nomal_Scr_hori`), ширина 42 и высота 11.
	Add(TEXT("CHAT_NEW_scr_8_0"),      false, L + 432.f + DW, T + 24.f, 20.f, 11.f);
	Add(TEXT("CHAT_NEW_scr_8_1"),      false, L + 421.f + DW, T + 24.f, 11.f, 11.f);
	Add(TEXT("CHAT_NEW_scr_8_2"),      false, L + 452.f + DW, T + 24.f, 11.f, 11.f);
	Add(TEXT("CHAT_NEW_scr_8_3"),      false, L + 441.f + DW, T + 24.f, 11.f, 11.f);
	Add(TEXT("lCHAT_NEW_ine0_grp"),    false, L,         T,       474.f + DW, 170.f);

	// Зона захвата верхней кромки (не из JUI — добавка по требованию юзера, см. EnsureResizeHandle).
	Add(TEXT("CHAT_NEW_ResizeTop"),    false, L,         T - kResizeBandOut, 474.f + DW, kResizeBandH);
	// Зона захвата ПРАВОЙ кромки — горизонтальное растягивание (добавка порта, EnsureResizeRight).
	Add(TEXT("CHAT_NEW_ResizeRight"),  false, L + W - kResizeBandOut, T, kResizeBandW, H);
}

void UAsdaChatWindowSubsystem::ApplyWindowLayout(float L, float T, float H, float W)
{
	if (!ChatWidget) { return; }
	TArray<FChatLayoutEntry> Table;
	BuildLayoutTable(L, T, H, W, QueTipOffset, Table);
	for (const FChatLayoutEntry& E : Table)
	{
		// ★ Имя `Node`, а не `W`: параметр функции W — это ШИРИНА окна, и локальная переменная
		//   его перекрывала (warning C4457). Ошибки не было, но читалось двусмысленно.
		UWidget* Node = ChatWidget->GetWidgetFromName(FName(E.Name));
		UCanvasPanelSlot* CS = AsdaUi::SlotOf(Node);
		if (!CS) { continue; }
		if (E.bLocal)
		{
			// Внутри группы координаты ЛОКАЛЬНЫЕ (импортёр кладёт детей от origin группы).
			CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetOffsets(FMargin(E.X, E.Y, E.W, E.H));
		}
		else
		{
			// Верхний уровень — экранный POINT-слот схемы импортёра (левый-нижний угол экрана).
			AsdaJui::SetChatSlot(CS, E.X, E.Y, E.W, E.H);
		}
	}
	WinL = L; WinT = T; WinH = H; WinW = W;
	RefreshChatTabs();   // ряд вкладок живёт по тем же L/T
	// ★★★ КОМБОБОКС 2608 ПОСЛЕ КАЖДОЙ СМЕНЫ РАСКЛАДКИ. Подпись лежит в группе `SizeWin_grp` и едет
	//   вместе с телом сама, но ПРОВЕРЯЕМ и восстанавливаем её слот здесь тоже — это единственная
	//   точка, через которую проходят ВСЕ пути изменения геометрии (кнопка 2605, растягивание обеих
	//   кромок, восстановление размера, загрузка настроек). Заодно пересчитывается позиция
	//   раскрытого списка: он висит на корневом канвасе и сам за группой не следует.
	//   ★ Тут зовём ТОЛЬКО геометрию (`ApplyComboGeometry`), а не полный `RefreshChannelCombo`:
	//   последний пересобирает строки через `ApplyFormatted`, который может ПОДМЕНИТЬ виджет, —
	//   делать это каждый кадр перетаскивания кромки нельзя.
	ApplyComboGeometry();

	// ★★★ СЦЕПКА: публикуем верхнюю кромку окна в общий реестр. Системный журнал 2570 читает её
	//   в своём тике и приклеивает свой НИЗ к ней (эталонный зазор 2 px: журнал 508…578, чат 580).
	//   Никаких перекрёстных зависимостей между подсистемами — только реестр окон.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowAnchorTop(kZoneId, T);
		}
	}
}

float UAsdaChatWindowSubsystem::BaseHeight() const
{
	// До первого касания (ни тогла 2605, ни растягивания) — высота 178 из JUI, как в оригинале.
	return bLayoutToggled ? FMath::Clamp(CurH, kHCollapsed, kHExpanded) : kWinH;
}

// ★ Ширина окна. Пока правую кромку не тянули — авторские 474 из JUI (они же минимум).
float UAsdaChatWindowSubsystem::BaseWidth() const
{
	return FMath::Clamp(CurW, kWinMinW, kWinMaxW);
}

float UAsdaChatWindowSubsystem::BaseTop() const
{
	// ★ НИЗ ЗАКРЕПЛЁН: после первого касания низ = 750 (эталон uiToggleExpandLayout),
	//   до него — 758 (JUI 580 + 178). Верх всегда = низ − высота.
	return bLayoutToggled ? (kWinBottomFixed - BaseHeight()) : kWinY;
}

void UAsdaChatWindowSubsystem::RefreshLayout()
{
	// Позиция окна больше не смещается ничем: только высота и вычисленный из неё верх.
	ApplyWindowLayout(kWinX, BaseTop(), BaseHeight(), BaseWidth());
}

// Панель строк на rect ленты 2521. Кладём ВНУТРЬ родной группы контейнера 2520, если импортёр её
//   создал (тогда панель едет вместе с группой и кнопка 2528 может встать над ней по z);
//   иначе — на корневой канвас по POINT-схеме JUI.
UScrollBox* UAsdaChatWindowSubsystem::MakeLinesPanel()
{
	if (!ChatWidget || !ChatWidget->WidgetTree) { return nullptr; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ChatWidget->WidgetTree->RootWidget);
	if (!Root) { return nullptr; }

	UScrollBox* Box = ChatWidget->WidgetTree->ConstructWidget<UScrollBox>(
		UScrollBox::StaticClass(), FName(TEXT("CHAT_NEW_Lines")));
	if (!Box) { return nullptr; }
	Box->SetOrientation(Orient_Vertical);
	// ★★★ КОЛЕСО НЕ ОТДАЁМ SLATE (2026-08-06). Раньше стояло `WhenScrollingPossible`, и лента
	//   листалась «сама», МИМО нашего диспетчера. Это опасно двумя способами: (1) когда Slate
	//   событие съедает, аксис `MouseWheelAxis` контроллера не срабатывает вовсе — и остальные
	//   списки (2501/2597/2639), которые ловят мышь опросом геометрии, колеса не видят;
	//   (2) когда не съедает — прокрутка происходит ДВАЖДЫ. Теперь колесо разбирает ОДНА точка —
	//   `UAsdaWindowSubsystem::HandleMouseWheel` → `OnWheelChatWindow`, как в оригинале, где
	//   колесо кладётся в очередь ОДНОГО менеджера окон (game_main.c:5871).
	Box->SetConsumeMouseWheel(EConsumeMouseWheel::Never);
	Box->SetVisibility(ESlateVisibility::Visible);
	// Скроллбар у ленты В ОРИГИНАЛЕ ЕСТЬ — это отдельный контрол CHAT_NEW_scr (2536,
	//   скин Nomal_Scr_Chat_hori, abs 445,606,20×10), в отличие от журнала 2570, где его нет вовсе.
	//   Родной контрол на этапе 1 не оживляем (его логика — этап 5 вместе с ресайзом), поэтому
	//   показываем штатный скроллбар UMG только при переполнении.
	Box->SetAlwaysShowScrollbar(false);

	// Родная группа контейнера 2520 у импортёра называется <idc>_grp.
	UCanvasPanel* Host = Cast<UCanvasPanel>(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_C_List_grp")));
	if (Host)
	{
		if (UCanvasPanelSlot* CS = Host->AddChildToCanvas(Box))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));   // ЛОКАЛЬНАЯ заливка группы (не экранный SPAN)
			CS->SetOffsets(FMargin(0, 0, 0, 0));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			// ★ z = 2, а НЕ 0: под лентой лежат три плиты подсветки (hover-строка, выбранная строка,
			//   плашка под ником) на z = 1 — эталон рисует квад ДО текста строки (ui_core.c:112969).
			CS->SetZOrder(2);
		}
		UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] лента вложена в родную группу CHAT_NEW_C_List_grp"));
	}
	else if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(Box))
	{
		// ★ ГРАБЛЯ №6: ТА ЖЕ схема, что у планов импортёра (иначе лента «отрывается» на 1080/1440).
		AsdaJui::SetChatSlot(CS, kListX, kListY, kListW, kListH);
		CS->SetZOrder(5);
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат-окно] группы CHAT_NEW_C_List_grp нет — лента на корневом канвасе, POINT-слот (%.0f,%.0f,%.0fx%.0f)"),
			kListX, kListY, kListW, kListH);
	}
	return Box;
}

void UAsdaChatWindowSubsystem::RebuildLines()
{
	if (!LinesBox) { return; }
	LinesBox->ClearChildren();
	// ★ Параллельный список строк ленты (для разбора ника под курсором и выделения) очищается
	//   вместе с виджетами — иначе индексы разъедутся. Выделение при пересборке снимается,
	//   как в эталоне при перезаполнении списка (uiCtrlReleaseChildAndResize сбрасывает +327).
	VisibleLines.Reset();
	RibbonHoverIdx = -1;
	RibbonSelIdx = -1;
	// ★ ФИЛЬТР ВКЛАДКИ: строка видна, если бит её слота стоит в маске активной вкладки
	//   (эталон: `sub_92B976` раскладывает маску в 10 булей и отдаёт их листбоксу).
	//   Служебные строки без слота (categoryToSlot = −1) показываем всегда.
	const int32 Mask = TabMask(ActiveTab);
	for (const FChatLogLine& L : LogHistory)
	{
		if (L.Slot >= 0 && ((Mask >> L.Slot) & 1) == 0) { continue; }
		PushLineWidget(L);
	}
	// ★ ЗАМОК 2607 ДЕЙСТВУЕТ И ЗДЕСЬ. Пересборка ленты — это наш аналог `sub_9387FC(self[3], …)`
	//   из `chatTabSelectFilter`, а там прыжок в конец стоит ПОД проверкой флага CChat+497
	//   (chat.c:1828: `if (*((_BYTE *)this + 497) != 1) uiScrollbarSetPos(list, конец)`).
	//   Раньше здесь стоял безусловный ScrollToEnd — смена вкладки размораживала прокрутку.
	if (!bScrollFrozen) { LinesBox->ScrollToEnd(); }
}

// Одна строка ленты. Формат = кегль + тень + цвет канала; при bRowBg строка заворачивается
//   в UBorder с заливкой — это эталонная подложка строки (поле +2760, флаг +3108,
//   ui_core.c:113035-113038), в которую попадает РАНГОВЫЙ цвет.
void UAsdaChatWindowSubsystem::PushLineWidget(const FChatLogLine& L)
{
	if (!LinesBox || !ChatWidget || !ChatWidget->WidgetTree) { return; }
	const FColor Color = L.Color;
	const FColor RowBg = L.RowBg;
	const bool   bRowBg = L.bRowBg;
	UTextBlock* Tb = ChatWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	if (!Tb) { return; }
	// ★ ГРАБЛЯ №7: autowrap в CanvasPanel ломает раскладку — перенос делаем сами (этап 2,
	//   токенайзером по ширине ленты `chatLineMaxWidth` (CChat+0x5D4), как
	//   textTokenizer_SplitStringIntoRows в оригинале).
	Tb->SetAutoWrapText(false);
	Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (bRowBg && RowBg.A > 0)
	{
		UBorder* Bd = ChatWidget->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		if (Bd)
		{
			Bd->SetBrushColor(FLinearColor(FColor(RowBg.R, RowBg.G, RowBg.B, RowBg.A)));
			Bd->SetPadding(FMargin(0.f));
			Bd->SetHorizontalAlignment(HAlign_Fill);
			Bd->SetVerticalAlignment(VAlign_Fill);
			Bd->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Bd->SetContent(Tb);
			LinesBox->AddChild(Bd);
		}
		else { LinesBox->AddChild(Tb); }
	}
	else
	{
		LinesBox->AddChild(Tb);
	}

	// <FS08> кегль импортёра, <FD> тень (флаг 0x400000 списка), <FC=FFRRGGBB> цвет канала.
	//   `</F>` в исходном тексте сбрасывает ВСЕ атрибуты сразу — реставрируем префикс после каждого
	//   (важно: Recv_ChatMessage присылает ранг уже инлайн-разметкой `<FC=ffffffff>[#N]</F> `).
	//   ★ Сама разметка строится в BuildLineMarkup — ОДНОЙ функцией и для добавления, и для
	//   перекраски при выделении строки (эталон красит выбранную строку другим цветом).
	// ★ ApplyFormatted ВОЗВРАЩАЕТ новый узел (UTextBlock подменяется на URichTextBlock) — правку
	//   переноса надо класть на НЕГО, а не на осиротевший Tb, иначе она уходит в пустоту.
	UWidget* Row = UAsdaRichText::ApplyFormatted(Tb, BuildLineMarkup(L, /*bSelected*/ false));
	// Перенос новой строки — по ТЕКУЩЕЙ ширине ленты (окно могли уже растянуть).
	if (LinesWrapW > 0.f)
	{
		if (UTextLayoutWidget* Tw = Cast<UTextLayoutWidget>(Row ? Row : (UWidget*)Tb))
		{
			Tw->SetWrapTextAt(LinesWrapW);
			Tw->SetAutoWrapText(true);
		}
	}

	// Параллельный список данных строк (индекс = индекс ребёнка LinesBox).
	VisibleLines.Add(L);
	while (LinesBox->GetChildrenCount() > kMaxLines)
	{
		LinesBox->RemoveChildAt(0);
		if (VisibleLines.Num() > 0) { VisibleLines.RemoveAt(0, 1, EAllowShrinking::No); }
		// Индексы hover/выделения «едут» вместе с лентой — ровно как в эталоне
		//   (uiList_RemoveItemAt декрементирует оба поля, ui_core.c:111717-111720).
		if (RibbonHoverIdx >= 0) { --RibbonHoverIdx; }
		if (RibbonSelIdx  >= 0) { --RibbonSelIdx; }
	}
}

// ══════════════════════════════ ВЫВОД СТРОК ═══════════════════════════════════════════════════

// Ранговый цвет — 1:1 эталон Recv_ChatNormal (network.c:10442-10462).
FColor UAsdaChatWindowSubsystem::RankColor(int32 RankLevel)
{
	if (RankLevel == 1) { return FColor(0x00, 0x00, 0x00, 0xFF); }             // 0xFF000000
	if (RankLevel == 2 || RankLevel == 3) { return FColor(0x72, 0x6C, 0xFF, 0xFF); }  // 0xFF726CFF
	// Линейный лерп по rankLevel/100: R 0→109, G 54→207, B 99→246.
	//   Константы эталона: max {109, 207, 246}, min {0, 54, 99}; c = (max−min)/100·rank + min.
	const double R = (109.0 - 0.0)  / 100.0 * (double)RankLevel + 0.0;
	const double G = (207.0 - 54.0) / 100.0 * (double)RankLevel + 54.0;
	const double B = (246.0 - 99.0) / 100.0 * (double)RankLevel + 99.0;
	return FColor((uint8)R, (uint8)G, (uint8)B, 0xFF);
}

void UAsdaChatWindowSubsystem::SetChannelPrefixMode(bool bEnabled)
{
	if (bChannelPrefixMode == bEnabled) { return; }
	bChannelPrefixMode = bEnabled;
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] режим префикса канала (флаг осады) = %s"),
		bEnabled ? TEXT("ВКЛ") : TEXT("выкл"));
	// TODO-ЭТАП 4: в оригинале вместе с флагом набор вкладок переключается на индекс 9
	//   (toggleField105Swap @0xBB4CA0, chat.c:5607, с сохранением предыдущего в gap_1A8).
	//   Вкладок в порте пока нет — включим на этапе 4.
}

void UAsdaChatWindowSubsystem::AddChatLine(EAsdaChatCat Category, const FString& Text, FColor FallbackColor)
{
	AddChatLineRaw((int32)Category, Text, FallbackColor);
}

void UAsdaChatWindowSubsystem::AddChatLineRaw(int32 Category, const FString& Text,
                                              FColor FallbackColor, FColor RowBgColor, bool bRowBg,
                                              const FString& SenderName, int32 PrefixSlotOverride)
{
	if (Text.IsEmpty()) { return; }
	// Эталон Chat_AddChatLine: при a2 < 0 не делает НИЧЕГО (`if (a2 > -1)`, chat.c:5022).
	if (Category < 0) { return; }

	const int32 Slot = CategoryToSlot(Category);
	// ★★★ 1:1 (реверс 2026-08-05): цвет канала ПЕРЕТИРАЕТ переданный a4 везде, кроме
	//   a2 == 6 (colorType 40) и неизвестных категорий — там остаётся a4.
	//   CategoryToSlot возвращает −1 ровно для этих двух случаев ⇒ условие сходится 1:1.
	// ★★★ ЦВЕТ БЕРЁТСЯ ИЗ ЖИВОЙ ТАБЛИЦЫ КАНАЛОВ (ChanColor), а не из заводской (SlotColor):
	//   в эталоне это поля `chanColor*` / `tabNameColorRecs`, которые физически лежат внутри
	//   массива `tabFilterOpt` — того самого, что правит палитра 2700 и коммитит ОК окна 2590.
	const FColor Color = (Slot >= 0) ? ChanColor(Slot) : FallbackColor;

	// ★ ПРЕФИКС КАНАЛА (chat.c:5102-5121). Ветка «как есть» срабатывает, если режим выключен,
	//   ЛИБО colorType == 6, ЛИБО a10 != 0. Иначе — «[loc(noticeId + 12092)] текст», где
	//   noticeId = (colorType == 41 ? 1 : colorType). colorType 41 — это вариант категории 2
	//   (слот 5) при включённом режиме. Категория 4 (слот 6) префикс не получает НИКОГДА
	//   из-за первого условия — это и есть эталонное поведение, не «баг».
	FString Line = Text;
	if (bChannelPrefixMode && Slot >= 0 && Slot != 6)
	{
		const int32 NoticeId = (Category == 2) ? 1 : Slot;
		UGameInstance* GI = GetGameInstance();
		UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		const FString ChanName = Txt ? Txt->GetTextById(NoticeId + 12092) : FString();
		if (!ChanName.IsEmpty()) { Line = FString::Printf(TEXT("[%s] %s"), *ChanName, *Text); }
	}

	FChatLogLine L;
	L.Raw = Line; L.Color = Color; L.Slot = Slot; L.RowBg = RowBgColor; L.bRowBg = bRowBg;
	L.Nick = SenderName;
	// ★★★ ПРЕФИКС КАНАЛА ПЕРЕД НИКОМ — «[Общ.][Ник]: текст» (требование юзера 2026-08-06).
	//   ⚠ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА: в эталоне канал несёт ЦВЕТ САМОГО ТЕКСТА, а «[имя канала] »
	//   приписывается только в режиме осады (bChannelPrefixMode, chat.c:5102-5121). НЕ «чинить».
	//   ★ Префикс живёт ОТДЕЛЬНЫМ полем и в `Raw` НЕ попадает: иначе он стал бы «первыми
	//   скобками» и сломал бы эталонный разбор ника (Д20.2) вместе с hover-плашкой под ником.
	//   Служебные строки (Slot < 0 — colorType 40 и неизвестные категории) префикса не получают:
	//   канала у них нет.
	L.PrefixSlot = (PrefixSlotOverride >= 0 && PrefixSlotOverride <= 9) ? PrefixSlotOverride : Slot;
	if (L.PrefixSlot >= 0)
	{
		const FString Short = SlotShortName(L.PrefixSlot);
		if (!Short.IsEmpty()) { L.ChanPrefix = FString::Printf(TEXT("[%s]"), *Short); }
	}
	LogHistory.Add(L);
	while (LogHistory.Num() > kMaxLines) { LogHistory.RemoveAt(0, 1, EAllowShrinking::No); }

	const int32 TabM = TabMask(ActiveTab);
	if (LinesBox && (L.Slot < 0 || ((TabM >> L.Slot) & 1) != 0))
	{
		PushLineWidget(L);
		// ★★★ АВТОСКРОЛЛ 1:1 (реверс 2026-08-05). В `chatLog_AddTokenizedLinesTab3` (chat.c:3375)
		//   после добавления строк скроллбар ставится в конец БЕЗУСЛОВНО, если не удерживается
		//   ручка ресайза (контрол 2607, флаг CChat+497). Проверки «был ли скролл внизу» в рабочем
		//   пути НЕТ — она лежит в мёртвой ветке Chat_AddChatLine (chat.c:5088-5095), где
		//   `scrollWasAtBottom` обнуляется прямо перед `if`. Ручка 2607 — ЭТАП 5.
		if (!bScrollFrozen) { LinesBox->ScrollToEnd(); }
	}

	UE_LOG(LogAsdaChatWin, Verbose, TEXT("[чат-окно] строка: кат=%d слот=%d фон=%s «%s»"),
		Category, Slot, bRowBg ? TEXT("да") : TEXT("нет"), *L.Raw);
}

// ★★★ ДУБЛЬ ОПОВЕЩЕНИЯ СТРОКОЙ В ЛЕНТУ. Полное доказательство эталона и разбор отличий от
//   AddChatLineRaw — в шапке объявления (Public/UI/AsdaChatWindowSubsystem.h).
//   Коротко: эталон дублировал анонс редкой добычи `Chat_AddChatLine(…, канал 10, msg, цвет)`
//   (item_inventory.c:39570, network.c:3750) → слот 4, без префикса, цвет из вызова; а объявление
//   администрации 3113 — `uiChatLogAddLine(…, −65536, слот 255)` (ui_core.c:12656) → «канала нет»,
//   видно на любой вкладке (у нас это Slot = −1).
void UAsdaChatWindowSubsystem::AddAnnounceLine(const FString& MarkupText, FColor BaseColor, int32 Slot)
{
	if (MarkupText.IsEmpty()) { return; }

	FChatLogLine L;
	L.Raw   = MarkupText;          // текст уже с inline-разметкой (<FC=грейд>…</F>) — не трогаем
	L.Color = BaseColor;           // базовый цвет служебной ветки BuildLineMarkup
	// Слот вне 0..9 приводим к −1 («канала нет» = видно на всех вкладках, эталонный 255).
	L.Slot  = (Slot >= 0 && Slot <= 9) ? Slot : -1;
	// ★ ОБА поля — защита ника (см. шапку объявления): служебная ветка разметки + запрет фолбэка
	//   ParseNickFromLine, иначе «[ Меч Дьявола ]» из анонса заточки станет «ником».
	L.PrefixSlot = -1;
	L.bNoNick    = true;

	LogHistory.Add(L);
	while (LogHistory.Num() > kMaxLines) { LogHistory.RemoveAt(0, 1, EAllowShrinking::No); }

	// Показ — по той же маске вкладки, что и у обычных строк (RebuildLines/AddChatLineRaw):
	//   Slot < 0 проходит всегда, Slot 4 — только там, где стоит бит 4 (вкладка 0 «Объединение»).
	const int32 TabM = TabMask(ActiveTab);
	if (LinesBox && (L.Slot < 0 || ((TabM >> L.Slot) & 1) != 0))
	{
		PushLineWidget(L);
		// Замок автопрокрутки 2607 действует и здесь (флаг CChat+497, chat.c:1828).
		if (!bScrollFrozen) { LinesBox->ScrollToEnd(); }
	}

	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] АНОНС в ленту: слот=%d цвет=#%02X%02X%02X «%s»"),
		L.Slot, BaseColor.R, BaseColor.G, BaseColor.B, *L.Raw);
}

// ★★★ ДУБЛЬ КОНСОЛЬНОГО КЛЮЧА `asda.UI.DumpZones` СНЯТ (аудит 2026-09-02).
//   Было: здесь регистрировалась ВТОРАЯ команда с тем же именем, что и в `AsdaWindowSubsystem.cpp`
//   (`GAsdaUiDumpZones`). `FConsoleManager::AddConsoleObject` для КОМАНД (в отличие от переменных)
//   делает `ConsoleObjects.Add(Name, Cmd); ExistingCmd->Release();` — то есть побеждает
//   ЗАРЕГИСТРИРОВАННАЯ ПОСЛЕДНЕЙ, а первая молча уничтожается. Порядок статической инициализации
//   между единицами трансляции задаётся порядком .obj в rsp линкера и НЕ фиксирован, так что
//   какая из двух реализаций жива — не определено от сборки к сборке.
//   Стало: ключ остаётся только в `AsdaWindowSubsystem.cpp` — та реализация СТРОГО ШИРЕ: печатает
//   `DebugUiZones()` (ровно то, что печатала эта) плюс `DebugTopmost()`, `DebugWheelHandlers()`
//   и «курсор над UI: зона». Ни одной диагностики не потеряно, дублирующее имя убрано.
