// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMessageSubsystem — реализация единого маршрутизатора сообщений. Шапка с законами
//   и таблицей «оригинал → наш маршрут» — в Public/UI/AsdaMessageSubsystem.h.
#include "UI/AsdaMessageSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»

#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaHudSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMsg, Log, All);

// Показывать диагностический плейсхолдер, если строки loc-id нет в DT_GameText.
//   1 (по умолчанию) — вместо тишины в чат уходит «[нет строки N]». Именно молчание пряталo
//   «Недоступную область»: сообщение якобы отправлено, а на экране пусто и никто не знает почему.
static ASDA_DEV_CVAR(int32, CVarMsgShowMissingLoc,
	TEXT("asda.Msg.ShowMissingLoc"), 1,
	TEXT("1 = показывать «[#N]», если loc-id отсутствует в DT_GameText; 0 = молчать"),
	ECVF_Default);

// Полный лог каждой отправки (категория, приёмники, цвет, TTL). По умолчанию включён.
static ASDA_DEV_CVAR(int32, CVarMsgVerbose,
	TEXT("asda.Msg.Verbose"), 1,
	TEXT("1 = писать в лог каждую отправку сообщения с разбором маршрута"),
	ECVF_Default);

static const TCHAR* kRouteTablePath = TEXT("/Game/AsdaAssets/Data/DT_MessageRoutes.DT_MessageRoutes");
// ★★★ 2026-08-26: CSV-ФОЛБЭК УБРАН. Раньше при отсутствии DataTable читался
//   `Content/AsdaAssets/Data/MessageRoutes.csv` — ЕДИНСТВЕННЫЙ сырой файл, который рантайм
//   тянул из `Content` напрямую. Источник переехал в `RawAssets/Data/MessageRoutes.csv`
//   (его читает импортёр `Tools/asda_import_message_routes.py`), а игра берёт только
//   `DT_MessageRoutes` и встроенные умолчания под ней.

// ═════════════════════════════════ Имена категорий ═════════════════════════════════════════════
FString UAsdaMessageSubsystem::CategoryName(EAsdaMsgCategory C)
{
	switch (C)
	{
	case EAsdaMsgCategory::War:           return TEXT("War");
	case EAsdaMsgCategory::AdminNotice:   return TEXT("AdminNotice");
	case EAsdaMsgCategory::HeroLoot:      return TEXT("HeroLoot");
	case EAsdaMsgCategory::System:        return TEXT("System");
	case EAsdaMsgCategory::MoveRefused:   return TEXT("MoveRefused");
	case EAsdaMsgCategory::CombatRefused: return TEXT("CombatRefused");
	case EAsdaMsgCategory::SkillRefused:  return TEXT("SkillRefused");
	case EAsdaMsgCategory::ItemRefused:   return TEXT("ItemRefused");
	case EAsdaMsgCategory::Loot:          return TEXT("Loot");
	case EAsdaMsgCategory::Trade:         return TEXT("Trade");
	case EAsdaMsgCategory::Quest:         return TEXT("Quest");
	case EAsdaMsgCategory::Progress:      return TEXT("Progress");
	case EAsdaMsgCategory::Social:        return TEXT("Social");
	case EAsdaMsgCategory::Connection:    return TEXT("Connection");
	case EAsdaMsgCategory::Duel:          return TEXT("Duel");
	case EAsdaMsgCategory::Tip:           return TEXT("Tip");
	default:                              return TEXT("?");
	}
}

bool UAsdaMessageSubsystem::ParseCategory(const FString& S, EAsdaMsgCategory& Out)
{
	for (int32 i = 0; i < (int32)EAsdaMsgCategory::MAX; ++i)
	{
		if (S.Equals(CategoryName((EAsdaMsgCategory)i), ESearchCase::IgnoreCase))
		{
			Out = (EAsdaMsgCategory)i;
			return true;
		}
	}
	return false;
}

FString UAsdaMessageSubsystem::TargetsToString(int32 T)
{
	if (T <= 0) { return TEXT("— (ничего)"); }
	TArray<FString> P;
	if (T & (int32)EAsdaMsgTarget::SystemChat) { P.Add(TEXT("сисчат")); }
	if (T & (int32)EAsdaMsgTarget::WarWindow)  { P.Add(TEXT("WARMESSAGE221")); }
	if (T & (int32)EAsdaMsgTarget::NoticeWin)  { P.Add(TEXT("NOTICE228")); }
	if (T & (int32)EAsdaMsgTarget::HeroNotice) { P.Add(TEXT("HERO")); }
	if (T & (int32)EAsdaMsgTarget::TrackB)     { P.Add(TEXT("буферB")); }
	if (T & (int32)EAsdaMsgTarget::ChatRibbon) { P.Add(TEXT("ЛентаЧата2521")); }
	return FString::Join(P, TEXT("+"));
}

// ═══════════════════════════════ Жизненный цикл ════════════════════════════════════════════════
void UAsdaMessageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostCount.SetNumZeroed((int32)EAsdaMsgCategory::MAX);
	ReloadRoutes();
}

void UAsdaMessageSubsystem::Deinitialize()
{
	Routes.Empty();
	Super::Deinitialize();
}

void UAsdaMessageSubsystem::ReloadRoutes()
{
	FillDefaults();
	RoutesSource = TEXT("встроенные умолчания");
	if (ApplyDataTable())  { RoutesSource = TEXT("DataTable DT_MessageRoutes"); }
	UE_LOG(LogAsdaMsg, Log, TEXT("[msg] таблица маршрутизации: %s, категорий %d"),
		*RoutesSource, Routes.Num());
	WarnRibbonConflicts();
}

// ★★★ «Журнал ИЛИ лента, но не оба» — см. объявление в заголовке.
int32 UAsdaMessageSubsystem::WarnRibbonConflicts() const
{
	const int32 kChat = (int32)EAsdaMsgTarget::SystemChat;
	const int32 kRib  = (int32)EAsdaMsgTarget::ChatRibbon;
	TArray<FString> Bad;
	for (const FAsdaMsgRoute& R : Routes)
	{
		if ((R.Targets & kChat) && (R.Targets & kRib))
		{
			Bad.Add(CategoryName(R.Category));
		}
	}
	if (Bad.Num() > 0)
	{
		UE_LOG(LogAsdaMsg, Warning,
			TEXT("[msg] ★ ДВОЙНИК: у категорий %s взведены СРАЗУ «сисчат» (журнал 2570) и "
			     "«ЛентаЧата2521» — одна и та же строка появится в журнале И в ленте. Сними один "
			     "из приёмников в DT_MessageRoutes и выполни asda.Msg.Reload."),
			*FString::Join(Bad, TEXT(", ")));
	}
	return Bad.Num();
}

// ★ ВСТРОЕННЫЕ УМОЛЧАНИЯ = решение юзера 2026-08-04. Данные их перекрывают, но никогда не обнуляют:
//   если DataTable/CSV сломан или неполон, клиент всё равно показывает сообщения.
void UAsdaMessageSubsystem::FillDefaults()
{
	const int32 kChat  = (int32)EAsdaMsgTarget::SystemChat;
	const int32 kWar   = (int32)EAsdaMsgTarget::WarWindow;
	const int32 kNote  = (int32)EAsdaMsgTarget::NoticeWin;
	const int32 kHero  = (int32)EAsdaMsgTarget::HeroNotice;
	const int32 kTrkB  = (int32)EAsdaMsgTarget::TrackB;
	// ★★★ ДУБЛЬ В ЛЕНТУ 2521 — ПРАВИЛО ВЗАИМНОГО ИСКЛЮЧЕНИЯ (регресс 2026-08-06, жалоба юзера
	//   «текст из системного чата попадает в основной чат»).
	//   Первая редакция ставила `kRib` двенадцати категориям, включая те, что УЖЕ пишут в
	//   СИСТЕМНЫЙ ЖУРНАЛ 2570 (`kChat`). Юзер видел одну и ту же строку («Ты получаешь предмет:
	//   Сияющая руда») дважды: в журнале и в ленте. Он просил дублировать ВСПЛЫВАШКИ И БАННЕРЫ,
	//   а не содержимое журнала.
	//   ⇒ ЗАКОН: `kChat` и `kRib` в одной строке маршрута НЕСОВМЕСТИМЫ. Лента получает дубль
	//     ТОЛЬКО у категорий, которые показываются баннером (NOTICE_WIN 228 / HERO_NOTICE 83400 /
	//     WARMESSAGE_WIN 221) и в журнал НЕ идут: War, AdminNotice, HeroLoot.
	//     Нарушение ловится `WarnRibbonConflicts()` после каждой загрузки данных.
	//   Включить ленту любой другой категории по-прежнему можно галочкой ChatRibbon в
	//   DT_MessageRoutes + `asda.Msg.Reload` (без пересборки) — но тогда сними у неё «сисчат»,
	//   иначе получишь ту же двойную строку.
	const int32 kRib   = (int32)EAsdaMsgTarget::ChatRibbon;

	const FColor Gold (0xFF, 0xD3, 0x06, 0xFF);   // −11514  основной системный (229/316 сайтов)
	const FColor Red  (0xFF, 0x00, 0x00, 0xFF);   // −65536  критично
	const FColor White(0xFF, 0xFF, 0xFF, 0xFF);   // −1
	const FColor Amber(0xFF, 0xAD, 0x00, 0xFF);   // −21248  сбой операции
	const FColor Green(0x86, 0xFD, 0x51, 0xFF);   // −7930543 свой подбор
	const FColor Lime (0x00, 0xFF, 0x00, 0xFF);   // −16711936 редкий дроп
	const FColor Pink (0xFE, 0xD9, 0xF1, 0xFF);   // −75279  дуэль/особые

	Routes.Reset();
	Routes.SetNum((int32)EAsdaMsgCategory::MAX);

	// ChatSlot: −1 = строка ленты видна на всех вкладках (эталонный слот 255 объявлений 3113);
	//   4 = эталонный слот анонса редкой добычи (канал 10 → colorType 4) — только вкладка 0.
	// ChatCol: A = 0 — «цвет ленты = цвет события» (см. FAsdaMsgRoute::ChatColor).
	auto Set = [this](EAsdaMsgCategory C, int32 T, FColor Col, float Ttl, const TCHAR* Snd, const TCHAR* Note,
	                  int32 ChatSlot = -1, FColor ChatCol = FColor(0, 0, 0, 0))
	{
		FAsdaMsgRoute& R = Routes[(int32)C];
		R.Category = C; R.Targets = T; R.Color = Col; R.TtlMs = Ttl;
		R.Sound = (Snd && *Snd) ? FName(Snd) : NAME_None;
		R.Note = Note;
		R.ChatSlot = ChatSlot;
		R.ChatColor = ChatCol;
	};

	// ★★★ ПОПРАВКА ЮЗЕРА 2026-08-04: военные события идут в ОКНО С РАМКОЙ — NOTICE_WIN 228,
	//   БЕЗ дубля в журнал и БЕЗ бесфонового верхнего списка 221 (скрин: «Битва началась» висела
	//   голым текстом вверху и повторялась внизу — юзер этого не хотел).
	Set(EAsdaMsgCategory::War,           kNote | kRib,        Gold,  10000.f, nullptr,
		TEXT("оригинал: 221 соло (бесфоновый список, зоны TTL 2000), в ленту НЕ дублировался | наш маршрут (решение юзера): NOTICE_WIN 228 + дубль строкой в ленту, без журнала и без 221"));
	// ★ ЭТАЛОН ДУБЛИРОВАЛ 3113 САМ: uiChatLogAddLine(listBoxSys 226, текст, −65536 КРАСНЫЙ, TTL 0,
	//   слот 255) — ui_core.c:12656, дубль получают все режимы 0/1/100.
	//   ★ ИСПРАВЛЕНИЕ 2026-08-06: раньше здесь было написано «ДО разбора режима» — это НЕВЕРНО.
	//   В uiNoticeUpdate3Line_BC90@0xBABC90 блок `if (mode == 100) { sprintf(loc 10221, atoi(тело)) }`
	//   стоит СТРОКАМИ ВЫШЕ вызова uiChatLogAddLine (ui_core.c:12645-12656) ⇒ в чат эталона уходит
	//   УЖЕ РАЗВЁРНУТАЯ строка «Битва начнется через N мин.», а не аргумент. См. ResolveNoticeText.
	//   Красный фиксируем в ChatColor: в бар уходит RGB из пакета, в ленту — эталонный красный.
	Set(EAsdaMsgCategory::AdminNotice,   kNote | kRib,        White, 15000.f, nullptr,
		TEXT("оригинал: NOTICE_WIN 228 (push 3113) + дубль строкой в чат (красный, слот 255, скрытый листбокс 226) | наш: NOTICE_WIN + дубль в ЖИВУЮ ленту 2521 тем же красным"),
		/*ChatSlot*/ -1, /*ChatColor*/ Red);
	// ★ ЭТАЛОН ДУБЛИРОВАЛ РЕДКУЮ ДОБЫЧУ САМ: Chat_AddChatLine(g_pChatWnd, 10, msg, −16711936)
	//   (item_inventory.c:39570 свой, network.c:3750 чужой) ⇒ слот 4, префикса нет, цвет из вызова.
	//   ChatColor не задан (A=0): цвет ленты = цвет события ⇒ золотой успех заточки и красный
	//   провал (перекрытие OnItemAcquired) доходят до ленты как есть, вместе с цветом грейда внутри.
	Set(EAsdaMsgCategory::HeroLoot,      kHero | kRib,        Lime,  10000.f, nullptr,
		TEXT("оригинал: HERO_NOTICE 83400 (6131, грейд>=3, не свой) + дубль в ленту каналом 10 (слот 4, вкладка «Объединение») | наш: то же 1:1"),
		/*ChatSlot*/ 4);
	Set(EAsdaMsgCategory::System,        kChat,               White, 10000.f, nullptr,
		TEXT("оригинал: журнал (+221 у части сайтов), в ленту не дублировался | наш: ТОЛЬКО журнал 2570 (дубль в ленту снят 2026-08-06: строка журнала не должна повторяться в ленте)"));
	// ── Четыре спамных отказа: ChatRibbon НЕ ставим (решение юзера). Строки ленты вечные, а эти
	//    категории летят пачками на каждый клик/удар/нажатие. Включаются галочкой в таблице.
	Set(EAsdaMsgCategory::MoveRefused,   kChat,        Gold,  10000.f, TEXT("error"),
		TEXT("оригинал: input.c пара журнал+221+error.wav, карта/миникарта — журнал+error.wav; 4007 соло в 221 | наш: журнал+error.wav. Дубль в ленту ВЫКЛЮЧЕН (спам на каждый клик) — включается галочкой ChatRibbon"));
	Set(EAsdaMsgCategory::CombatRefused, kChat,        Red,   10000.f, nullptr,
		TEXT("оригинал: 4027/4029 соло в 221 | наш: журнал. Дубль в ленту ВЫКЛЮЧЕН (спам на каждый удар) — включается галочкой ChatRibbon"));
	Set(EAsdaMsgCategory::SkillRefused,  kChat,        Gold,  10000.f, nullptr,
		TEXT("оригинал: соло в 221 (network.c/skill_buff.c) | наш: журнал. Дубль в ленту ВЫКЛЮЧЕН (кулдаун/мана бьют на каждое нажатие) — включается галочкой ChatRibbon"));
	Set(EAsdaMsgCategory::ItemRefused,   kChat,        Gold,  10000.f, TEXT("error"),
		TEXT("оригинал: соло в 221 (item_inventory.c/ui_core.c), перегруз TTL 30000 | наш: журнал+error.wav. Дубль в ленту ВЫКЛЮЧЕН (прочность 0/перегруз повторяются каждым тиком боя) — включается галочкой ChatRibbon"));
	Set(EAsdaMsgCategory::Loot,          kChat | kTrkB,        Green, 10000.f, nullptr,
		TEXT("оригинал: журнал + буфер (свой подбор буфер B), в ленту не дублировался | наш: журнал 2570 + буфер B. ★ ДУБЛЬ В ЛЕНТУ СНЯТ 2026-08-06 — именно он давал жалобу «Ты получаешь предмет: …» дважды"));
	Set(EAsdaMsgCategory::Trade,         kChat,               Amber, 10000.f, nullptr,
		TEXT("оригинал: пара журнал+221 (chat.c транзакции) | наш: ТОЛЬКО журнал 2570 (дубль в ленту снят 2026-08-06)"));
	Set(EAsdaMsgCategory::Quest,         kChat,               Gold,  10000.f, nullptr,
		TEXT("оригинал: пара журнал+221 | наш: ТОЛЬКО журнал 2570 (дубль в ленту снят 2026-08-06)"));
	Set(EAsdaMsgCategory::Progress,      kChat | kTrkB,        Lime, 10000.f, nullptr,
		TEXT("оригинал: журнал + буфер B (уровень/опыт/награды) | наш: журнал 2570 + буфер B (дубль в ленту снят 2026-08-06)"));
	Set(EAsdaMsgCategory::Social,        kChat,               White, 10000.f, nullptr,
		TEXT("оригинал: журнал (+221 у части соулмейт-состояний) | наш: ТОЛЬКО журнал 2570 (дубль в ленту снят 2026-08-06)"));
	Set(EAsdaMsgCategory::Connection,    kChat,               Red,   10000.f, nullptr,
		TEXT("оригинал: 221 соло (loc 1001 при разрыве) | наш: ТОЛЬКО журнал 2570 (дубль в ленту снят 2026-08-06)"));
	Set(EAsdaMsgCategory::Duel,          kChat | kTrkB,        Pink, 10000.f, nullptr,
		TEXT("оригинал: пара журнал+221 (PvpDuel_ApplyMatchResult) | наш: журнал 2570 + буфер B (дубль в ленту снят 2026-08-06)"));
	Set(EAsdaMsgCategory::Tip,           kChat,               White, 10000.f, nullptr,
		TEXT("оригинал: журнал (скриншот loc 1102/1103 белым) | наш: ТОЛЬКО журнал 2570 (дубль в ленту снят 2026-08-06)"));
}

bool UAsdaMessageSubsystem::ApplyDataTable()
{
	UDataTable* DT = LoadObject<UDataTable>(nullptr, kRouteTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!DT) { return false; }
	int32 Applied = 0;
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		const FAsdaMsgRoute* Row = reinterpret_cast<const FAsdaMsgRoute*>(It.Value);
		if (!Row) { continue; }
		EAsdaMsgCategory C;
		// Приоритет — имя строки (редактируемо), иначе поле Category.
		if (!ParseCategory(It.Key.ToString(), C)) { C = Row->Category; }
		if ((int32)C >= Routes.Num()) { continue; }
		FAsdaMsgRoute R = *Row; R.Category = C;
		Routes[(int32)C] = R;
		++Applied;
	}
	UE_LOG(LogAsdaMsg, Log, TEXT("[msg] DT_MessageRoutes: применено строк %d"), Applied);
	return Applied > 0;
}

const FAsdaMsgRoute& UAsdaMessageSubsystem::GetRoute(EAsdaMsgCategory C) const
{
	static const FAsdaMsgRoute Empty;
	return Routes.IsValidIndex((int32)C) ? Routes[(int32)C] : Empty;
}

// ═════════════════════════════════ Резолв текста ═══════════════════════════════════════════════
FString UAsdaMessageSubsystem::ResolveText(const FAsdaMsgParams& P, bool& bOutMissingLoc) const
{
	bOutMissingLoc = false;
	if (!P.Text.IsEmpty()) { return P.Text; }
	if (P.LocId <= 0) { return FString(); }

	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FString S;
	if (Txt)
	{
		// ★★★ ФОРМАТИРУЕМ ВСЕГДА (правка 2026-08-28). Раньше строка шла через `FormatById` только
		//   при наличии аргументов, и нотис с маркерами, посланный без них, уходил в чат СЫРЫМ
		//   шаблоном: владелец увидел «Всего выставлено предметов: [sd1]. Общая цена: [ss1]…».
		//   Оригинал (`UI_FormatLocalizedNotice`) подставляет ВСЕ маркеры всегда — незаполненный
		//   слот даёт пустую строку/ноль. Строк с маркерами в таблицах порта 195 из 15487, так что
		//   для остальных вызов — тождественная операция (маркеров нет ⇒ строка возвращается как есть).
		S = Txt->FormatById(P.LocId, P.StringArgs, P.NumberArgs);
	}
	if (!S.IsEmpty()) { return S; }

	bOutMissingLoc = true;
	if (!P.Fallback.IsEmpty()) { return P.Fallback; }
	// Строки нет и запасной нет: молча пропадать нельзя — это ровно тот случай, что прятал
	//   «Недоступную область». Плейсхолдер видно в игре, warning — в логе.
	return (CVarMsgShowMissingLoc.GetValueOnAnyThread() != 0)
		? FAsdaLang::MissingTag(P.LocId)   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): нейтральная метка вместо русского «[нет строки N]»
		: FString();
}

// ═════════════════════════════ ★ ЕДИНСТВЕННАЯ ТОЧКА ВЫВОДА ═════════════════════════════════════
void UAsdaMessageSubsystem::Post(const FAsdaMsgParams& P)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	if (Routes.Num() == 0) { ReloadRoutes(); }

	const FAsdaMsgRoute& R = GetRoute(P.Category);

	bool bMissingLoc = false;
	const FString Text = ResolveText(P, bMissingLoc);
	if (bMissingLoc)
	{
		UE_LOG(LogAsdaMsg, Warning,
			TEXT("[msg] loc %d ОТСУТСТВУЕТ в DT_GameText (категория %s, опкод %d) — показываю '%s'"),
			P.LocId, *CategoryName(P.Category), P.SrcOpcode, Text.IsEmpty() ? TEXT("<ничего>") : *Text);
	}
	if (Text.IsEmpty())
	{
		UE_LOG(LogAsdaMsg, Warning, TEXT("[msg] пустое сообщение (категория %s, loc %d) — пропуск"),
			*CategoryName(P.Category), P.LocId);
		return;
	}

	const int32   Targets = (P.Targets >= 0) ? P.Targets : R.Targets;
	const FColor  Color   = P.bUseColor ? P.Color : R.Color;
	const float   TtlMs   = (P.TtlMs >= 0.f) ? P.TtlMs : R.TtlMs;
	const FName   Sound   = (P.Sound != NAME_None) ? P.Sound : R.Sound;

	if (PostCount.IsValidIndex((int32)P.Category)) { ++PostCount[(int32)P.Category]; }

	if (CVarMsgVerbose.GetValueOnAnyThread() != 0)
	{
		UE_LOG(LogAsdaMsg, Log, TEXT("[msg] %s → %s | цвет=#%02X%02X%02X TTL=%.0fмс%s%s | «%s»"),
			*CategoryName(P.Category), *TargetsToString(Targets),
			Color.R, Color.G, Color.B, TtlMs,
			(Sound != NAME_None) ? *FString::Printf(TEXT(" звук=%s"), *Sound.ToString()) : TEXT(""),
			(P.SrcOpcode != 0) ? *FString::Printf(TEXT(" опкод=%d"), P.SrcOpcode) : TEXT(""),
			*Text);
	}

	if (Targets == (int32)EAsdaMsgTarget::None)
	{
		UE_LOG(LogAsdaMsg, Warning, TEXT("[msg] у категории %s НЕТ приёмников — сообщение никуда не пойдёт"),
			*CategoryName(P.Category));
		return;
	}

	// ── Приёмник 1: системный чат (журнал 2572 + невидимый буфер A/B) ──
	if (Targets & (int32)EAsdaMsgTarget::SystemChat)
	{
		if (UAsdaChatSubsystem* Chat = GI->GetSubsystem<UAsdaChatSubsystem>())
		{
			Chat->AddSystemMessage(Text, Color,
				(Targets & (int32)EAsdaMsgTarget::TrackB) ? EAsdaSysTrack::B : EAsdaSysTrack::A, TtlMs);
		}
		else { UE_LOG(LogAsdaMsg, Warning, TEXT("[msg] нет UAsdaChatSubsystem — сисчат недоступен")); }
	}

	// ── Приёмник 2: WARMESSAGE_WIN (221) ──
	if (Targets & (int32)EAsdaMsgTarget::WarWindow)
	{
		if (UAsdaChatSubsystem* Chat = GI->GetSubsystem<UAsdaChatSubsystem>())
		{
			// bAlsoSystemChat=false ВСЕГДА: парность решает маска приёмников, а не флаг стока.
			// ★ StyleTag сообщения (`<FO>` обводка / `<FB>` жирный) — по тому же правилу, что у
			//   NOTICE_WIN и HERO_NOTICE: стиль задаёт вызывающий, канал остаётся эталонным.
			Chat->AddWarMessage(Text, Color, TtlMs, /*bAlsoSystemChat=*/false, P.StyleTag);
		}
	}

	// ── Приёмник 3: NOTICE_WIN (228, окно с рамкой) ──
	//   Сюда идут и серверный push 3113 (AdminNotice, mode/цвет из пакета), и ЛОКАЛЬНЫЕ военные
	//   строки (War, mode 0, цвет из таблицы). Правила окна принадлежат ему самому:
	//     • буфер 3 строк, переполнение вытесняет самую старую (эталон uiNoticeUpdate3Line);
	//     • раскладка 1 строка → центр, 2 → верх+низ, 3 → все три;
	//     • ЖИЗНЬ СТРОКИ ФИКСИРОВАНА окном — 10 000 мс (kNoticeLineLifeS), поле TtlMs маршрута
	//       на этот приёмник НЕ влияет (действует только для сисчата и 221);
	//     • RGB(0,0,0) в цвете = «не задан» → белый (сентинел кампвар-пакетов нашего сервера);
	//     • inline-теги <FC=..>/<FO> строка проносит как есть — обводку/цвет ставит ApplyNoticeText.
	//   ⇒ локальные военные строки и админские нотисы уживаются в одном окне без конфликта правил.
	if (Targets & (int32)EAsdaMsgTarget::NoticeWin)
	{
		if (UAsdaHudSubsystem* Hud = GI->GetSubsystem<UAsdaHudSubsystem>())
		{
			Hud->ShowServerNotice(P.NoticeMode, Text, Color);
		}
		else { UE_LOG(LogAsdaMsg, Warning, TEXT("[msg] нет UAsdaHudSubsystem — NOTICE_WIN недоступен")); }
	}

	// ── Приёмник 4: HERO_NOTICE (83400) ──
	if (Targets & (int32)EAsdaMsgTarget::HeroNotice)
	{
		if (UAsdaHudSubsystem* Hud = GI->GetSubsystem<UAsdaHudSubsystem>())
		{
			// Закон ui-srgb-gamma-rule: цвета эталона заданы в sRGB.
			Hud->ShowHeroNotice(Text, FLinearColor::FromSRGBColor(Color), FMath::Max(0.f, TtlMs) / 1000.f);
		}
	}

	// ── Приёмник 5: ЛЕНТА ЧАТА (CHAT_NEW 2500, список 2521) — дубль оповещения строкой ──
	//   ЭТАЛОН ДУБЛИРОВАЛ САМ (доказательства — в шапке EAsdaMsgTarget::ChatRibbon):
	//     • редкая добыча: `Chat_AddChatLine(g_pChatWnd, /*канал*/10, msg, −16711936, …)`
	//       (item_inventory.c:39570 свой подбор/гача/заточка, network.c:3750 чужой 6131) —
	//       канал 10 уходит в default-ветку: слот 4, БЕЗ префикса, цвет из вызова;
	//     • объявление 3113: `uiChatLogAddLine(listBoxSys 226, текст, −65536 КРАСНЫЙ, TTL 0,
	//       слот 255)` (ui_core.c:12656) — до разбора режима, т.е. все режимы 0/1/100.
	//   ⚠ Для остальных категорий дубля в ленту в эталоне НЕТ — это отступление по требованию юзера
	//   («да давай в принципе все Notice и прочие баннеры дублировать в чат»), обратимое снятием
	//   галочки ChatRibbon в DT_MessageRoutes + `asda.Msg.Reload`, БЕЗ ПЕРЕСБОРКИ.
	//   ★ Цвет: ChatColor маршрута, если задан (A > 0) — этим воспроизводится эталонный КРАСНЫЙ
	//   дубля 3113, не зависящий от RGB баннера; иначе действующий цвет события ⇒ строка ленты
	//   один в один повторяет цвет баннера (требование юзера «с таким же цветом текста»).
	//   ★ Гамму здесь НЕ применяем: цвет уходит тегом <FC=FFRRGGBB> в разметку, а её де-гаммит
	//   FAsdaRichDecorator (закон ui-srgb-gamma-rule; FromSRGBColor нужен только заливкам).
	if (Targets & (int32)EAsdaMsgTarget::ChatRibbon)
	{
		if (UAsdaChatWindowSubsystem* Win = GI->GetSubsystem<UAsdaChatWindowSubsystem>())
		{
			const FColor RibbonColor = (R.ChatColor.A > 0) ? R.ChatColor : Color;
			// ★★★ СТОРОЖ «СЫРОГО АРГУМЕНТА» (заведён 2026-08-06 по дефекту 3113 mode 100): в ленту
			//   уехало голое «5» вместо «Битва начнется через 5 мин.», потому что подстановка минут
			//   жила в приёмнике-баннере и выполнялась ПОСЛЕ маршрутизации. Признак такой ошибки —
			//   строка в 1-2 символа, чаще всего чистое число. Молчать нельзя: баннер при этом
			//   выглядит правильно, и дефект видит только юзер в игре.
			const FString Trimmed = Text.TrimStartAndEnd();
			if (Trimmed.Len() <= 2)
			{
				bool bDigitsOnly = !Trimmed.IsEmpty();
				for (const TCHAR Ch : Trimmed) { if (!FChar::IsDigit(Ch)) { bDigitsOnly = false; break; } }
				UE_LOG(LogAsdaMsg, Warning,
					TEXT("[msg] ПОДОЗРИТЕЛЬНАЯ строка ленты: категория %s, опкод %d, текст «%s» (%d симв.%s). ")
					TEXT("Похоже, в ленту ушёл СЫРОЙ АРГУМЕНТ, а не готовый текст — разворачивать шаблон надо ДО Post()."),
					*CategoryName(P.Category), P.SrcOpcode, *Trimmed, Trimmed.Len(),
					bDigitsOnly ? TEXT(", только цифры") : TEXT(""));
			}
			Win->AddAnnounceLine(Text, RibbonColor, R.ChatSlot);
		}
		else { UE_LOG(LogAsdaMsg, Warning, TEXT("[msg] нет UAsdaChatWindowSubsystem — лента чата недоступна")); }
	}

	// ── Звук (эталон CSoundFlag_PlaySound2D «error.wav» у клиентских отказов) ──
	if (Sound != NAME_None)
	{
		if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>())
		{
			Snd->PlaySound2D(Sound);
		}
	}
}

void UAsdaMessageSubsystem::PostText(EAsdaMsgCategory Category, const FString& Text)
{
	FAsdaMsgParams P; P.Category = Category; P.Text = Text;
	Post(P);
}

void UAsdaMessageSubsystem::PostLoc(EAsdaMsgCategory Category, int32 LocId, const FString& Fallback)
{
	FAsdaMsgParams P; P.Category = Category; P.LocId = LocId; P.Fallback = Fallback;
	Post(P);
}

void UAsdaMessageSubsystem::PostLocFmt(EAsdaMsgCategory Category, int32 LocId,
                                       const TArray<FString>& StringArgs, const TArray<int32>& NumberArgs)
{
	FAsdaMsgParams P; P.Category = Category; P.LocId = LocId;
	P.StringArgs = StringArgs; P.NumberArgs = NumberArgs;
	Post(P);
}

// ═════════════════════════════════ Диагностика ═════════════════════════════════════════════════
FString UAsdaMessageSubsystem::DumpRouting() const
{
	FString S = FString::Printf(TEXT("═══ ТАБЛИЦА МАРШРУТИЗАЦИИ СООБЩЕНИЙ ═══ источник: %s\n"), *RoutesSource);
	S += TEXT("категория        приёмники                            цвет     TTL,мс  звук    лента(слот/цвет)    отправлено  примечание\n");
	for (int32 i = 0; i < Routes.Num(); ++i)
	{
		const FAsdaMsgRoute& R = Routes[i];
		// ★ Столбец «лента»: есть ли дубль строкой, на каких вкладках он виден и каким цветом.
		FString Ribbon;
		if (R.Targets & (int32)EAsdaMsgTarget::ChatRibbon)
		{
			Ribbon = (R.ChatSlot < 0)
				? FString(TEXT("слот −1 все вкл."))
				: FString::Printf(TEXT("слот %d"), R.ChatSlot);
			Ribbon += (R.ChatColor.A > 0)
				? FString::Printf(TEXT("/#%02X%02X%02X"), R.ChatColor.R, R.ChatColor.G, R.ChatColor.B)
				: FString(TEXT("/цвет события"));
		}
		else { Ribbon = TEXT("— выкл"); }
		// ★ Правило «журнал ИЛИ лента»: двойник виден прямо в дампе, а не только в логе.
		if ((R.Targets & (int32)EAsdaMsgTarget::SystemChat)
			&& (R.Targets & (int32)EAsdaMsgTarget::ChatRibbon))
		{
			Ribbon += TEXT(" ★ДВОЙНИК");
		}
		S += FString::Printf(TEXT("%-16s %-36s #%02X%02X%02X %7.0f  %-7s %-20s %6d      %s\n"),
			*CategoryName((EAsdaMsgCategory)i), *TargetsToString(R.Targets),
			R.Color.R, R.Color.G, R.Color.B, R.TtlMs,
			(R.Sound == NAME_None) ? TEXT("—") : *R.Sound.ToString(),
			*Ribbon,
			PostCount.IsValidIndex(i) ? PostCount[i] : 0,
			*R.Note);
	}
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaChatSubsystem* Chat = GI->GetSubsystem<UAsdaChatSubsystem>())
		{
			S += FString::Printf(TEXT("── окна: %s\n"), *Chat->DebugState());
		}
		S += FString::Printf(TEXT("── HUD-подсистема: %s\n"),
			GI->GetSubsystem<UAsdaHudSubsystem>() ? TEXT("есть (NOTICE_WIN/HERO_NOTICE)") : TEXT("НЕТ"));
	}
	return S;
}

// ═══════════════════════════ Консольные команды (одна на все каналы) ═══════════════════════════
static UAsdaMessageSubsystem* AsdaMsgGet(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMsg(
	TEXT("asda.Msg"),
	TEXT("Сообщение через маршрутизатор: asda.Msg <категория> <текст> — категории: War AdminNotice HeroLoot "
	     "System MoveRefused CombatRefused SkillRefused ItemRefused Loot Trade Quest Progress Social Connection Duel Tip"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaMessageSubsystem* M = AsdaMsgGet(W);
		if (!M) { return; }
		EAsdaMsgCategory C = EAsdaMsgCategory::System;
		int32 Start = 0;
		if (Args.Num() > 0 && UAsdaMessageSubsystem::ParseCategory(Args[0], C)) { Start = 1; }
		else if (Args.Num() > 0)
		{
			UE_LOG(LogAsdaMsg, Warning, TEXT("[msg] категория '%s' не распознана — беру System"), *Args[0]);
		}
		FString Text;
		for (int32 i = Start; i < Args.Num(); ++i) { if (i > Start) { Text += TEXT(" "); } Text += Args[i]; }
		if (Text.IsEmpty()) { Text = FString::Printf(TEXT("Тест категории %s"), *UAsdaMessageSubsystem::CategoryName(C)); }
		M->PostText(C, Text);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMsgLoc(
	TEXT("asda.Msg.Loc"),
	TEXT("Сообщение по loc-id: asda.Msg.Loc <категория> <id>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaMessageSubsystem* M = AsdaMsgGet(W);
		if (!M || Args.Num() < 2) { return; }
		EAsdaMsgCategory C = EAsdaMsgCategory::System;
		UAsdaMessageSubsystem::ParseCategory(Args[0], C);
		M->PostLoc(C, FCString::Atoi(*Args[1]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMsgDump(
	TEXT("asda.Msg.Dump"),
	TEXT("Таблица маршрутизации сообщений + состояние окон"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaMessageSubsystem* M = AsdaMsgGet(W);
		if (!M) { return; }
		UE_LOG(LogAsdaMsg, Display, TEXT("\n%s"), *M->DumpRouting());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMsgReload(
	TEXT("asda.Msg.Reload"),
	TEXT("Перечитать таблицу маршрутизации из DataTable/CSV (без пересборки)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaMessageSubsystem* M = AsdaMsgGet(W);
		if (!M) { return; }
		M->ReloadRoutes();
		UE_LOG(LogAsdaMsg, Display, TEXT("\n%s"), *M->DumpRouting());
	}));
#endif // ASDA_DEV_CONSOLE

// Прогон ВСЕХ категорий одной командой — проверка «каждое сообщение реально видно».
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMsgTestAll(
	TEXT("asda.Msg.TestAll"),
	TEXT("Отправить по одному тестовому сообщению в КАЖДУЮ категорию (проверка видимости всех каналов)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaMessageSubsystem* M = AsdaMsgGet(W);
		if (!M) { return; }
		for (int32 i = 0; i < (int32)EAsdaMsgCategory::MAX; ++i)
		{
			const EAsdaMsgCategory C = (EAsdaMsgCategory)i;
			M->PostText(C, FString::Printf(TEXT("[%s] проверка канала"), *UAsdaMessageSubsystem::CategoryName(C)));
		}
		UE_LOG(LogAsdaMsg, Display, TEXT("\n%s"), *M->DumpRouting());
	}));
#endif // ASDA_DEV_CONSOLE
