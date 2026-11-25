// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ПАНЕЛЬ ДРУГА `SOULMATE_POP_WIN` 6500 («SM bar») — методы `UAsdaSoulmateSubsystem` (23.10.2026).
//   Жалоба владельца: «SM bar is not showing up» — отдельное окошко под мини-картой и над значками занятости
//   городов (HP, MP, уровень, «золотое» яблоко), им же можно выбрать Друга целью.
//
// ═══════════════════════ ЧТО БЫЛО В ПОРТУ ДО ЭТОЙ ПРАВКИ ═════════════════════════════════════════
//   Окно 6500 (T7, 31.08) построено: имя 6502, уровень Дружбы 6501, уровень Друга 6505, полосы 6504 (опыт Дружбы),
//   6508 (HP, Soul_Red_Bar), 6509 (MP, Soul_Blue_Bar), стихия 6512..6517, яблоко 6689..6699 (`UAsdaSoulAppleSubsystem`).
//   НО: показ шёл только по галке 6546, а её состояние порт брал лишь из клика игрока в этой сессии — СВОЮ ЗАПИСЬ 5220
//   (где сервер хранит галку, по умолчанию 1) порт не разбирал. Итог — панель не появлялась никогда.
//
// ═══════════════════════ ЭТАЛОН (сборка IDB 13 344 768 б; адреса проверены дизасмом) ═════════════
//   • 5220 (0x1464) — диспетчер network.c:1768: записи по 101 б → `Recv_PartyMinimapMark@0xE641A0` (миснейм).
//     Своя запись (`*(DWORD*)Src == localChar[263]` = charId): memcpy(local+2215, Src+6, 95) — сервер кладёт туда
//     CUser+99001…+99095 (`Send_SoulMateInfo@0x494ED0`): @6 слот (gap_878[47]), @19 charId Друга (gap_8B4[0..3]),
//     @23 userIdx Друга (gap_8B4[4]), @25 «Друг в игре» (gap_8B4[6] = CUser+99020), @33 «показывать панель»
//     (gap_8BE[4] = CUser+99028); и CSoulmateWnd+1853..1856 ← @33/@36/@37/@38 (галки 6546/6543/6544/6545).
//     Затем: окно сообщества видно → `itemTooltip_RefreshControls` (галки вкладки); если @33 && @25: панель скрыта и
//     rec[63] (`*((char*)self+1420)`, ЗНАКОВЫЙ char) > 0 → loc 110103 в чат (−28418, TTL 0) и в плашку 221 (−11514,
//     10000); затем `sub_92CBC8` = `sub_E65450@0xE65450` — при галке +1853 показать 6500.
//     Чужая запись (ветка «SOUL OTHER CHECK») — у `UAsdaNetSubsystem` (SoulState/кольца), здесь не трогается.
//   • Сервер: при создании пары +99028 = 1 (GameServer.c:58641/58672) — панель по умолчанию ВКЛЮЧЕНА; 5220 уходит
//     только при слоте > 0 (`Send_SoulMateInfo`: `+99001 <= 0` → лишь запись в лог). Галку 6546 сервер берёт из 5221
//     (`sub_52A230`), обратного 5220 на неё нет.
//   • HP/MP Друга — 5111 (`soulmateWnd_ApplyInfoPacket`): rec[13..16]/rec[17..20] = макс/тек HP (CUser+1107/+1113),
//     rec[21..22]/rec[23..24] = макс/тек MP (+1111/+1117). Сервер шлёт 5111 при каждой смене HP/MP Друга
//     (`sub_493D80` из SetCurrentHpTimeValue/SetCurrentMpTimeValue, урона, воскрешения… → 3319 → `sub_494010` → 5111).
//   • Эффекты Друга — 5112 (`buffUiLoadAndRefreshIcons@0xE65390`) и 5110: 28 записей по 6 б в rec+83 (вид, ?, id);
//     `buffUiRefreshIcons`: вид 1 → stateinfotip, вид 3 → soulstateinfotip, значок 24×24; слот отрисовки d → контрол
//     `obj[259+2d]`, а привязка (InitControls, дизасм 0xE5E8EC…0xE5E992) кладёт 33000+k в obj[259+4k] и 33014+k в
//     obj[261+4k] ⇒ d чёт → 33000+d/2, d нечёт → 33014+d/2 (заполнение СТОЛБЦАМИ справа налево). Смена набора при
//     показанной подсказке гасит её.
//   • Мышь (`SoulmateUI_HandleEvent@0xE67EC0`): msg 8 (наведение), гейт «слот != −1 и нет перетаскивания»:
//     6501 → тема 16 (9159/9131), 6504 → 17 (9159/9132), 6505 → 23 (9160/9133) (`helpTooltipDispatchById@0xE6ABA0`);
//     33000…33013 → подсказка эффекта; 6692/6693/6695/6698 — яблоко (`UAsdaSoulAppleSubsystem`). msg 9 — ведение,
//     msg 0xA — гашение. msg 0xB (ЛКМ) по контролам панели — ничего (кроме 6693 «золотое яблоко»).
//     msg 0xD (двойной ЛКМ) — `uiPreparePartyTradeRequest(…, режим 2, …)` = «Следовать», но режим 2 функция
//     НЕ обрабатывает (дизасм 0xFBE919 `cmp [ebp+8],0` / 0xFBEA0A `cmp [ebp+8],1`) — следования нет. Не переносим.
//     msg 0xE (ПКМ) по 6502 при той же карте — `g_nPartySelStatePending = 100`; читатель `dword_2BE0554` = 100 в образе
//     НЕ читается никем (сплошной скан образа: 2 ссылки, обе записи) — мёртвая ветка, не переносим.
//
// ═══════════════════════ ЧТО ДОБАВЛЕНО (ключи; 0 = прежнее поведение) ════════════════════════════
//   [ОРИГИНАЛ]    asda.Soulmate.PopFrom5220 1 — разбор своей 5220, показ панели по ней (ключ — в AsdaSoulmateSubsystem.cpp).
//   [ОРИГИНАЛ]    asda.Soulmate.Check6546Gate 1 — галка 6546 при Друге вне игры: отказ + loc 8026 (там же).
//   [ОРИГИНАЛ]    подсказки 6501/6504/6505 и значки эффектов Друга 33000…33027 (5112/5110) — без ключа (не было вовсе).
//   [ИСПРАВЛЕНИЕ] asda.Soulmate.PopBuffTipFix 1 — подсказка значка = ЕГО эффект; у оригинала наведение на 33000+k
//                 берёт id слота k и вид записи k (а рисует там слот 2k), значки 33014…33027 подсказок не имеют.
//   [ИСПРАВЛЕНИЕ] asda.Soulmate.PopClassIcon 1 — значок класса Друга по rec[41] ? rec[41] : rec[40] (те же коды 0..29,
//                 что у loc 12556+id поля 6556 и у мини-статуса: 0 → новичок, 1..7 → меч, 12..18 → лук, 23..29 → посох);
//                 у оригинала узлы без IDNum — всегда горит «меч» из разметки.
//   [ИСПРАВЛЕНИЕ] asda.Soulmate.LoginNoticeAnyName 1 — нотис 110103 из 5220 и при кириллическом имени (у оригинала
//                 знаковое `char > 0` режет cp1251-байты ≥ 0x80 — русские имена нотиса не получали).
//   [ВЛАДЕЛЕЦ]    asda.Soulmate.PopClickTarget 1 — ЛКМ по панели делает Друга целью (как ЛКМ по строке отряда;
//                 у оригинала такого нет). Нет персонажа Друга рядом — loc 20308 «цель не найдена», как у отряда.
//   [ПОРТ]        после смены карты панель возвращается на новом виджете (оригинал окон не пересоздаёт).
//   [ИСПРАВЛЕНИЕ] ★ 26.10.2026 asda.Soulmate.SelfEchoGuard 1 — «эхо себя» (баг Player17: панель с СВОИМ именем и ур. 70): сервер
//                 по устаревшему CharPos шлёт «Друг вошёл» на мой же номер и возвращает мне мои 5110/5111/5112, а 5220 @19 = я.
//                 Эхо не применяем, Друг — вне игры (панель скрыта, вкладка «Вне игры»). Разбор — у ключа в AsdaSoulmateSubsystem.cpp.
//
// ═══════════════════════ ТАБЛИЦА НОТИСОВ «событие → loc → канал» ═════════════════════════════════
//   5220 своя, @33 && @25, панель скрыта, имя есть → 110103 «Появляется Друг [%s].» → чат −28418 + плашка 221 −11514
//   галка 6546 при Друге вне игры                   → 8026  «Друга нет в игре»     → чат −8847556, 10 с
//   наведение 6501 / 6504 / 6505                     → 9159+9131 / 9159+9132 / 9160+9133 → подсказка (SoulPop)
//   наведение на значок эффекта                      → имя+текст stateinfotip/soulstateinfotip → подсказка (SoulPop)
//   [ВЛАДЕЛЕЦ] ЛКМ, Друга рядом нет                  → 20308 «цель не найдена» → чат (Social)
//   5110 → 110103, 5226 → 110104 — прежние места (`OnCard`, `OnPartnerLeft5226`).   НУМПАД: нет (количеств нет).
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#include "UI/AsdaSoulStoneSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "AsdaDevConsole.h"
#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldKind.h"
#include "UI/AsdaMinimapSubsystem.h"   // панель занятости 79900 — под панелью Друга (RelayoutOccupationBox)
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/CheckBox.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulPop, Log, All);

// Значение `asda.Soulmate.PopFrom5220` (ключ объявлен в AsdaSoulmateSubsystem.cpp).
bool AsdaSoulmatePopFrom5220On();
// ★ 26.10.2026: значение `asda.Soulmate.SelfEchoGuard` (ключ и разбор «эха себя» — в AsdaSoulmateSubsystem.cpp).
bool AsdaSoulmateSelfEchoGuardOn();

// ═══════════ ★★★ 26.10.2026 [ИСПРАВЛЕНИЕ] ВТОРАЯ ЛИНИЯ ЗАЩИТЫ ОТ ЗАВИСАНИЯ НА ПОРТАЛЕ (хотфикс; первая — `asda.Window.ReopenOrphan`) ═══
//   Заморозка у игроков с Другом: на L_Loading реестр окон отдавал мёртвый виджет 6500, и `TryShowPop` → `RefreshPopCard` →
//   `ApplyPopVisibility` → `TryShowPop` … уходили в бесконечную рекурсию — игровой поток стоял. Защита (ключ asda.Soulmate.PopGuard):
//   (1) глубина вложенности этих трёх функций ограничена (законная цепочка — 4 уровня: показ → данные → видимость → показ живого
//       окна); сверх лимита вызов отклоняется, одна строка в журнал на заход; (2) вне мира (L_Loading, сцена входа, мир ещё не начал
//       игру) панель не поднимается и тик панели не работает — желание «панель на экране» сохраняется, тик вернёт её на карте.
static ASDA_DEV_CVAR(int32, CVarSoulPopGuard,
	TEXT("asda.Soulmate.PopGuard"), 1,
	TEXT("Панель Друга 6500: 1 (по умолчанию) [ИСПРАВЛЕНИЕ] — лимит повторного входа TryShowPop/RefreshPopCard/ApplyPopVisibility\n")
	TEXT("и запрет показа/тика панели вне мира (L_Loading, сцена входа). 0 = прежнее поведение (без защиты)."),
	ECVF_Default);
namespace AsdaSoulPopGuardLocal
{
	static int32 GDepth = 0;              // текущая глубина вложенности трёх функций панели (игровой поток)
	static bool  GTripLogged = false;     // строка о срабатывании уже написана в этом заходе (сброс — при выходе на глубину 0)
	static int32 GTrips = 0;              // срабатываний за процесс (самотест)
	static bool  GLostLogged = false;     // строка «окно 6500 потеряно с миром» уже написана (сброс — когда окно снова живо)
	static constexpr int32 kMaxDepth = 4; // законная цепочка: TryShowPop → RefreshPopCard → ApplyPopVisibility → TryShowPop
}
// Вход в функцию панели: false — лимит глубины превышен, вызов отклонить (парный `AsdaSoulPopLeave` НЕ звать).
bool AsdaSoulPopEnter(const TCHAR* Where)
{
	using namespace AsdaSoulPopGuardLocal;
	if (CVarSoulPopGuard.GetValueOnGameThread() != 0 && GDepth >= kMaxDepth)
	{
		++GTrips;
		if (!GTripLogged)
		{
			GTripLogged = true;
			UE_LOG(LogAsdaSoulPop, Warning, TEXT("[панель Друга] повторный вход в %s на глубине %d — вызов отклонён (защита от зависания)"), Where, GDepth);
		}
		return false;
	}
	++GDepth;
	return true;
}
// Выход из функции панели (только после успешного `AsdaSoulPopEnter`).
void AsdaSoulPopLeave()
{
	using namespace AsdaSoulPopGuardLocal;
	if (GDepth > 0) { --GDepth; }
	if (GDepth == 0) { GTripLogged = false; }
}
// Срабатываний лимита за процесс (самотест).
int32 AsdaSoulPopGuardTrips() { return AsdaSoulPopGuardLocal::GTrips; }
// Мир годен для панели: игра идёт и это не L_Loading и не сцена входа. При ключе 0 — всегда да (прежнее поведение).
bool AsdaSoulPopWorldReady(const UGameInstance* GI)
{
	if (CVarSoulPopGuard.GetValueOnGameThread() == 0) { return true; }
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	return W && W->HasBegunPlay() && !AsdaWorldKind::IsLoadingMap(W) && !AsdaWorldKind::IsLoginScene(W);
}
// RAII-обёртка входа/выхода.
struct FAsdaSoulPopScope
{
	bool bOk;                                                              // вход разрешён
	explicit FAsdaSoulPopScope(const TCHAR* Where) : bOk(AsdaSoulPopEnter(Where)) {}
	~FAsdaSoulPopScope() { if (bOk) { AsdaSoulPopLeave(); } }
};

namespace AsdaSoulPopLocal
{
	static ASDA_DEV_CVAR(int32, CVarPopBuffTipFix,
		TEXT("asda.Soulmate.PopBuffTipFix"), 1,
		TEXT("Панель Друга 6500: 1 (по умолчанию) [ИСПРАВЛЕНИЕ] — подсказка значка эффекта показывает ЕГО эффект (все 28 значков).\n")
		TEXT("0 = как оригинал: наведение на 33000+k берёт id слота k и вид записи k, у 33014…33027 подсказок нет."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarPopClassIcon,
		TEXT("asda.Soulmate.PopClassIcon"), 1,
		TEXT("Панель Друга 6500: 1 (по умолчанию) [ИСПРАВЛЕНИЕ] — значок класса Друга по его классу (rec[41]?rec[40]).\n")
		TEXT("0 = как оригинал: значки без IDNum, всегда горит «меч» разметки."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarLoginNoticeAnyName,
		TEXT("asda.Soulmate.LoginNoticeAnyName"), 1,
		TEXT("5220: 1 (по умолчанию) [ИСПРАВЛЕНИЕ] — нотис 110103 «Появляется Друг» при любом непустом имени.\n")
		TEXT("0 = как оригинал: знаковое `char > 0` — имена с первым байтом cp1251 ≥ 0x80 (кириллица) нотиса не дают."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarPopClickTarget,
		TEXT("asda.Soulmate.PopClickTarget"), 1,
		TEXT("Панель Друга 6500: 1 (по умолчанию) [ВЛАДЕЛЕЦ] — ЛКМ по панели делает Друга целью.\n")
		TEXT("0 = как оригинал: ЛКМ по панели ничего не делает."),
		ECVF_Default);

	// help-темы `UI_BuildHelpTooltipText@0xEB11A0` для контролов панели (`helpTooltipDispatchById@0xE6ABA0`).
	struct FTopic { int32 Title; int32 Body; };
	static FTopic TopicFor(int32 ControlId)
	{
		switch (ControlId)
		{
		case 6501: return { 9159, 9131 };   // тема 16 «Полная информация о Друге» / «Общий уровень Друга»
		case 6504: return { 9159, 9132 };   // тема 17 … / «Общий опыт Друга»
		case 6505: return { 9160, 9133 };   // тема 23 «Информация о выбранном Друге» / «Уровень выбранного Друга»
		default:   return { 0, 0 };
		}
	}

	// Слот отрисовки d → номер контрола SICON (00…27): d чёт → d/2, нечёт → 14 + d/2 (дизасм InitControls 0xE5E8EC).
	static int32 SiconForSlot(int32 D) { return (D % 2 == 0) ? D / 2 : 14 + D / 2; }
}

// ═══════════════════════════════ 5220 — СВОЯ ЗАПИСЬ ═════════════════════════════════════════════
void UAsdaSoulmateSubsystem::OnSelfSoulInfo5220(const TArray<uint8>& Rec101)
{
	if (Rec101.Num() < 101) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 CharId = AsdaBytes::RdI32(&Rec101[0]);
	// Своя ли запись: эталон сравнивает @0 с charId своего персонажа (`localChar[263]` = +1052).
	if (!Net || Net->SelectedCharId == 0 || CharId != Net->SelectedCharId) { return; }
	// ★ 23.10.2026 (ревизия, п.3): ДАННЫЕ своей записи храним ВСЕГДА — ключи управляют только поведением. Запись целиком
	//   (её читает вкладка: имена строк share, гейт 5221) и поля своего блока, которые пишет и 5110/5111: уровень (+12 @18) и
	//   опыт (+4..11 @10..17) Дружбы, карта (+22 @28) и канал (+23 @29) Друга, свой connecttype (+26 @32, 2 = я одержим).
	SelfInfo5220 = Rec101;
	const uint8* S = Rec101.GetData();
	FriendLevelBlk    = S[18];
	FriendExpBlk      = (int64)(((uint64)AsdaBytes::RdU32(&S[14]) << 32) | (uint64)AsdaBytes::RdU32(&S[10]));   // QWORD LE
	PartnerMapBlk     = S[28];
	PartnerChannelBlk = S[29];
	SelfConnectType   = S[32];
	bBlkLevelKnown    = true;
	if (!AsdaSoulmatePopFrom5220On())
	{
		// Прежний порт: слот, «Друг в игре», галки и панель по 5220 НЕ меняются. Вкладка и отметка — своими ключами (волна sm-info).
		UE_LOG(LogAsdaSoulPop, Log, TEXT("[панель Друга] ←5220 своя запись — сохранена, панель/галки не трогаем (ключ разбора 5220 выключен)"));
		RefreshSelfBlockControls(TEXT("5220 своя запись"));
		PushCoupleMark(false);
		return;
	}
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ сервера] «эхо себя» (ключ asda.Soulmate.SelfEchoGuard, разбор — в AsdaSoulmateSubsystem.cpp):
	//   @19 == свой charId — сервер после эха 3313 (`recv_1@0x4945B0` пишет +99014 = charId отправителя) считает Другом МЕНЯ;
	//   и пока эхо замечено в этой сессии, @25 «в игре» (из устаревшего CharPos) не верен — Друг на деле вне игры.
	const bool bEchoGuard = AsdaSoulmateSelfEchoGuardOn();
	const bool bEchoRec   = bEchoGuard && AsdaBytes::RdI32(&S[19]) != 0 && AsdaBytes::RdI32(&S[19]) == CharId;
	const bool bEchoHold  = bEchoGuard && !bEchoRec && IsSelfEchoSeen();
	// memcpy(local+2215, Src+6, 95): слот, Друг, «Друг в игре», «показывать панель».
	Card.SlotIndex   = AsdaBytes::RdI32(&S[6]);
	if (!bEchoRec) { MateCharIdLive = AsdaBytes::RdI32(&S[19]); }
	MateUserIdxLive  = (bEchoRec || bEchoHold) ? -1 : (int32)AsdaBytes::RdI16(&S[23]);
	Card.bHasSoulmate = (S[25] != 0) && !bEchoRec && !bEchoHold;
	bPopSrvOpt       = (S[33] != 0);
	// CSoulmateWnd+1853..1856 ← @33/@36/@37/@38: галка 6546 и три галки «поделиться» (Modes[0..2] = 6543..6545).
	Card.Modes[3] = S[33];
	Card.Modes[0] = S[36];
	Card.Modes[1] = S[37];
	Card.Modes[2] = S[38];
	// `communityUI_IsVisible` → `itemTooltip_RefreshControls`: галки вкладки (привязка живая — сама проверяет).
	SyncShareChecksFromModes();
	if (bEchoRec || bEchoHold)
	{
		// Своё (слот, галки, уровень/опыт Дружбы) применено выше; Друг — вне игры: вкладка «Вне игры», панель скрыта, без нотиса.
		if (bEchoRec) { ++SelfEchoDrops; }
		UE_LOG(LogAsdaSoulPop, Warning, TEXT("[панель Друга] ←5220 своя: %s — панель не показываю (слот=%d, @25=%u)"),
			bEchoRec ? TEXT("Друг @19 = Я (эхо сервера)") : TEXT("эхо уже замечено, «в игре» не верю"), Card.SlotIndex, (uint32)S[25]);
		EnterSelfEchoOffline(bEchoRec ? TEXT("5220 — Друг = я") : TEXT("5220 при замеченном эхе"));
		return;
	}
	// ★ 23.10.2026 (sm-info): вся перерисовка вкладки по своему блоку («Вне игры», уровень/опыт Дружбы, строки share, 6546).
	RefreshSelfBlockControls(TEXT("5220 своя запись"));
	PushCoupleMark(false);
	UE_LOG(LogAsdaSoulPop, Log,
		TEXT("[панель Друга] ←5220 своя: слот=%d Друг charId=%d userIdx=%d в игре=%u панель(6546)=%u галки=%u/%u/%u"),
		Card.SlotIndex, MateCharIdLive, MateUserIdxLive, (uint32)S[25], (uint32)S[33], (uint32)S[36], (uint32)S[37], (uint32)S[38]);

	if (S[33] == 0 || S[25] == 0) { return; }
	// Нотис 110103 — только если панель есть и СКРЫТА, а имя в записи непустое (`*((char*)self+1420) > 0`).
	const uint8 First = Rec.B[63];
	const bool bNameOk = (AsdaSoulPopLocal::CVarLoginNoticeAnyName.GetValueOnGameThread() != 0)
		? (First != 0)
		: ((int8)First > 0);
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	// ★ 23.10.2026 (ревизия, п.1): у эталона окно 6500 живёт всегда — «скрыта» и панель, которой у порта ещё нет / которая умерла
	//   со сменой карты (иначе первый вход в игру нотиса не давал: виджет поднимается только `TryShowPop` ниже).
	const bool bPopHidden = !PopWnd || !WM || !WM->IsWindowLive(PopWnd) || !WM->IsWindowVisible(kPopWindowId);
	if (bPopHidden && bNameOk)
	{
		// sprintf(loc 110103, −28418, имя) — первый аргумент это цвет тега <FC=%08x>.
		FString Line = AsdaNotice::LocTagged(GI, 110103);
		Line.ReplaceInline(TEXT("%08x"), TEXT("ffff90fe"), ESearchCase::IgnoreCase);
		Line.ReplaceInline(TEXT("%s"), *RecName(), ESearchCase::CaseSensitive);
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Social, Line, FColor(0xFF, 0x90, 0xFE, 0xFF), 0.f);
		AsdaNotice::Plank(GI, EAsdaMsgCategory::Social, Line, AsdaNoticeColor::Gold, 10000.f, 5220);
		++PopNoticeSeq; PopLastNoticeLoc = 110103;
	}
	TryShowPop(TEXT("5220 своя запись"));
}

// ═══════════════════════════════ 5112 — ЭФФЕКТЫ ДРУГА ═══════════════════════════════════════════
void UAsdaSoulmateSubsystem::OnPartnerBuffs5112(const TArray<uint8>& Body168)
{
	// `buffUiLoadAndRefreshIcons`: 28 × (вид u8, u8, id i32) → gap_54D[83 + 6i …].
	const int32 N = FMath::Min(Body168.Num(), kPopBuffs * 6);
	FMemory::Memcpy(&Rec.B[83], Body168.GetData(), N);
	RefreshPopBuffs();
	UE_LOG(LogAsdaSoulPop, Log, TEXT("[панель Друга] ←5112 эффекты Друга: значков %d"), GetPopBuffShownCount());
}

// ═══════════════════════════════ ПОКАЗ ═══════════════════════════════════════════════════════════
void UAsdaSoulmateSubsystem::TryShowPop(const TCHAR* Why)
{
	// `sub_E65450`: `if (+1853) { SetVisible(m_pPopWin, 1); SetVisible(m_pGhostWin, 0); … }`. Окно-призрак 9125 показывает
	//   `uiProgressShowZero@0xE6A680` при активации Камня души (`this+208`), гасит — эта функция. Группа яблока 6691 при раскрытой
	//   панели (+181 == 1) возвращается `UAsdaSoulAppleSubsystem` (ключ asda.Soul.Apple.FixCloseAllHide).
	if (Card.Modes[3] == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (хотфикс зависания на портале): вне мира панель не поднимаем — желание сохраняем, тик вернёт её на карте.
	if (!AsdaSoulPopWorldReady(GI)) { bPopWanted = true; return; }
	const FAsdaSoulPopScope Guard(TEXT("TryShowPop"));
	if (!Guard.bOk) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const bool bFresh = !PopWnd || !WM->IsWindowLive(PopWnd);
	EnsurePopWindow();
	if (!PopWnd) { return; }
	// Окно и после подъёма мёртвое (реестр отдал виджет прежнего мира) — не рисовать и не показывать: иначе круг показ ↔ данные.
	if (!WM->IsWindowLive(PopWnd) && CVarSoulPopGuard.GetValueOnGameThread() != 0)
	{
		bPopWanted = true;
		UE_LOG(LogAsdaSoulPop, Warning, TEXT("[панель Друга] окно 6500 после подъёма не живое (%s) — показ отложен до тика"), Why);
		return;
	}
	if (bFresh)
	{
		// Только что поднятое окно — данные записи (иначе на экране заглушки разметки «소울메이트이름~»).
		//   `RefreshPopCard` внутри снова позовёт показ — окно уже живо, повторного «свежего» прохода не будет.
		RefreshPopCard();
		RefreshPopBuffs();
	}
	const bool bWas = WM->IsWindowVisible(kPopWindowId);
	WM->SetWindowVisible(kPopWindowId, true);
	// `sub_E65450`: `SetVisible(m_pGhostWin, 0)`. Окно 9125 показывает Камень души (`uiProgressShowZero`).
	if (AsdaSoulStoneOn()) { if (UAsdaSoulStoneSubsystem* Stone = GI->GetSubsystem<UAsdaSoulStoneSubsystem>()) { Stone->HideGhostWindow(); } }
	bPopWanted = true;
	if (!bWas)
	{
		UE_LOG(LogAsdaSoulPop, Log, TEXT("[панель Друга] окно 6500 показано (%s)"), Why);
		// Панель занятости городов — ПОД панелью Друга (раскладка .JUI), иначе ложится поверх значков эффектов.
		if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>()) { Mm->RelayoutOccupationBox(); }
	}
}

// ═══════════════════════════════ ЗНАЧКИ ══════════════════════════════════════════════════════════
void UAsdaSoulmateSubsystem::BindPopExtraNodes()
{
	if (!PopWnd) { return; }
	if (PopExtraBoundWnd.Get() == PopWnd && PopBuffIcons.Num() == kPopBuffs) { return; }
	PopExtraBoundWnd = PopWnd;
	PopBuffIcons.Reset();
	for (int32 D = 0; D < kPopBuffs; ++D)
	{
		const FString Pref = FString::Printf(TEXT("SOULMATE_SICON%02d_"), AsdaSoulPopLocal::SiconForSlot(D));
		PopBuffIcons.Add(Cast<UImage>(AsdaUi::FindNodeByPrefix(PopWnd, *Pref)));
	}
	PopClassIcons.Reset();
	static const TCHAR* const kCls[4] = { TEXT("SoulPop_Tender_"), TEXT("SoulPop_Sword_"), TEXT("SoulPop_Bow_"), TEXT("SoulPop_Staff_") };
	for (const TCHAR* P : kCls) { PopClassIcons.Add(Cast<UImage>(AsdaUi::FindNodeByPrefix(PopWnd, P))); }
	int32 NB = 0, NC = 0;
	for (const TObjectPtr<UImage>& I : PopBuffIcons) { NB += I ? 1 : 0; }
	for (const TObjectPtr<UImage>& I : PopClassIcons) { NC += I ? 1 : 0; }
	UE_LOG(LogAsdaSoulPop, Log, TEXT("[панель Друга] узлы окна 6500: значков эффектов %d/28, значков класса %d/4"), NB, NC);
}

void UAsdaSoulmateSubsystem::RefreshPopBuffs()
{
	BindPopExtraNodes();
	UGameInstance* GI = GetGameInstance();
	UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	int32 Dst = 0;
	bool bDirty = false;
	for (int32 Src = 0; Src < kPopBuffs; ++Src)
	{
		const uint8 Kind = Rec.B[83 + 6 * Src];
		const int32 Id = Rec.Rd32(85 + 6 * Src);
		if ((Kind != 1 && Kind != 3) || !Fx || Dst >= kPopBuffs) { continue; }
		FString Name, Atlas, Text; int32 Cell = 0;
		const bool bOk = (Kind == 1) ? Fx->GetStateInfo(Id, Name, Atlas, Cell, Text) : Fx->GetSoulStateInfo(Id, Name, Atlas, Cell, Text);
		if (!bOk) { continue; }   // эталон: записи в таблице нет — значка нет
		// Смена id в слоте при уже занятом первом слоте → подсказку погасить (`tooltipDirty`, `pBuffWnd[258] > 0`).
		if (PopBuffId[Dst] != Id && PopBuffId[0] > 0) { bDirty = true; }
		PopBuffId[Dst] = Id;
		PopBuffSrc[Dst] = Src;
		UImage* Img = PopBuffIcons.IsValidIndex(Dst) ? PopBuffIcons[Dst].Get() : nullptr;
		if (Img && AsdaUi::ApplyAtlasCellIcon(Img, Atlas, Cell, 24, 10)) { Img->SetVisibility(ESlateVisibility::HitTestInvisible); }
		++Dst;
	}
	for (int32 I = Dst; I < kPopBuffs; ++I)
	{
		PopBuffId[I] = -1;
		PopBuffSrc[I] = -1;
		if (UImage* Img = PopBuffIcons.IsValidIndex(I) ? PopBuffIcons[I].Get() : nullptr) { Img->SetVisibility(ESlateVisibility::Collapsed); }
	}
	if (bDirty && bPopTipShown && PopTipId >= 33000) { HidePopTip(); }
}

int32 UAsdaSoulmateSubsystem::GetPopBuffShownCount() const
{
	int32 N = 0;
	for (int32 D = 0; D < kPopBuffs; ++D) { N += (PopBuffId[D] >= 0) ? 1 : 0; }
	return N;
}

void UAsdaSoulmateSubsystem::RefreshPopClassIcon()
{
	BindPopExtraNodes();
	if (PopClassIcons.Num() != 4) { return; }
	if (AsdaSoulPopLocal::CVarPopClassIcon.GetValueOnGameThread() == 0)
	{
		// Как оригинал: разметка — «меч» виден, прочие скрыты (`Visible` в .JUI), код их не трогает.
		for (int32 i = 0; i < 4; ++i)
		{
			if (PopClassIcons[i]) { PopClassIcons[i]->SetVisibility(i == 1 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
		}
		PopClassIdx = 1;
		return;
	}
	const int32 ClassId = (int32)Rec.Grade();   // rec[41] ? rec[41] : rec[40] — то же, что поле 6556 (loc 12556+id)
	int32 Which = -1;
	if (ClassId == 0)                         { Which = 0; }   // новичок
	else if (ClassId >= 1 && ClassId <= 7)    { Which = 1; }   // воины
	else if (ClassId >= 12 && ClassId <= 18)  { Which = 2; }   // лучники
	else if (ClassId >= 23 && ClassId <= 29)  { Which = 3; }   // маги
	for (int32 i = 0; i < 4; ++i)
	{
		if (PopClassIcons[i]) { PopClassIcons[i]->SetVisibility(i == Which ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	}
	PopClassIdx = Which;
}

void UAsdaSoulmateSubsystem::SyncShareChecksFromModes()
{
	if (!IsBoundTo(CommWnd)) { return; }
	for (int32 i = 0; i < 4; ++i)
	{
		// SetIsChecked события не шлёт — 5221 не уйдёт.
		if (UCheckBox* Cb = FindCheck(*FString::Printf(TEXT("SOUL_CHECK%02d"), i))) { Cb->SetIsChecked(Card.Modes[i] != 0); }
	}
}

// ═══════════════════════════════ ЦЕЛЬ (ВЛАДЕЛЕЦ) ═════════════════════════════════════════════════
bool UAsdaSoulmateSubsystem::SelectPartnerAsTarget(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	int32 U = MateUserIdxLive;
	if (U < 0 && WS && MateCharIdLive > 0) { U = WS->GetUserNumByCharId(MateCharIdLive); }
	AActor* Actor = (WS && U >= 0) ? WS->GetPlayerActorByUserNum(U) : nullptr;
	if (!Actor)
	{
		// Как ЛКМ по строке отряда (`UAsdaPartySubsystem::ClickRow`): персонажа нет рядом — 20308, цель не меняем.
		AsdaNotice::ChatText(GI, EAsdaMsgCategory::Social, Loc(20308));
		++PopNoticeSeq; PopLastNoticeLoc = 20308;
		UE_LOG(LogAsdaSoulPop, Log, TEXT("[панель Друга] %s: Друг (userIdx=%d charId=%d) не рядом — цель не меняем (20308)"),
			Why, U, MateCharIdLive);
		return false;
	}
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetWorld() ? GI->GetWorld()->GetFirstPlayerController() : nullptr);
	if (!PC) { return false; }
	// ОБЩАЯ точка выбора цели (её же зовёт клик в мире и строка отряда; запрос 6538 уходит внутри).
	PC->SetTarget(Actor);
	UE_LOG(LogAsdaSoulPop, Log, TEXT("[панель Друга] %s: Друг userIdx=%d — цель"), Why, U);
	return true;
}

// ═══════════════════════════════ ПОДСКАЗКИ ═══════════════════════════════════════════════════════
void UAsdaSoulmateSubsystem::ShowPopTip(int32 ControlId, int32 Dst)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	if (!Mini) { return; }
	FString Title, Body;
	if (Dst < 0)
	{
		const AsdaSoulPopLocal::FTopic T = AsdaSoulPopLocal::TopicFor(ControlId);
		if (T.Title == 0) { return; }
		Title = Loc(T.Title);
		Body = Loc(T.Body);
	}
	else
	{
		UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
		int32 Id = -1, Src = -1;
		if (AsdaSoulPopLocal::CVarPopBuffTipFix.GetValueOnGameThread() != 0)
		{
			Id = PopBuffId[Dst]; Src = PopBuffSrc[Dst];
		}
		else
		{
			// Оригинал: контрол 33000+k (k < 14) → id слота k, вид записи k (social.c:8012-8024).
			const int32 K = ControlId - 33000;
			if (K < 0 || K >= 14) { return; }
			Id = PopBuffId[K]; Src = K;
		}
		if (Id < 0 || Src < 0 || !Fx) { return; }
		const uint8 Kind = Rec.B[83 + 6 * Src];
		FString Atlas; int32 Cell = 0;
		bool bOk = false;
		if (Kind == 1)      { bOk = Fx->GetStateInfo(Id, Title, Atlas, Cell, Body); }       // uiHelpEntryLayoutAndShow
		else if (Kind == 3) { bOk = Fx->GetSoulStateInfo(Id, Title, Atlas, Cell, Body); }   // …AndShowAlt
		if (!bOk) { return; }
	}
	Mini->ShowSharedTip(Title, Body, EAsdaTipOwner::SoulPop);
	bPopTipShown = true;
	PopTipId = ControlId;
}

void UAsdaSoulmateSubsystem::HidePopTip()
{
	if (!bPopTipShown) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
	{
		Mini->HideSharedTip(EAsdaTipOwner::SoulPop);   // гасит ТОЛЬКО свою (закон ui-shared-tip-ownership)
	}
	bPopTipShown = false;
	PopTipId = -1;
}

// ═══════════════════════════════ ТИК ═════════════════════════════════════════════════════════════
void UAsdaSoulmateSubsystem::TickPopCard()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!WM || !World) { return; }
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (хотфикс зависания на портале): на L_Loading / сцене входа тик панели не работает.
	if (!AsdaSoulPopWorldReady(GI))
	{
		HidePopTip();
		PopMouse.Reset();
		PopHoverId = -1;
		return;
	}

	// [ПОРТ] Возврат после смены карты: виджет умер вместе с миром, а панель должна стоять (оригинал её не пересоздаёт).
	if (bPopWanted && (!PopWnd || !WM->IsWindowLive(PopWnd)) && World->HasBegunPlay() && !AsdaWorldKind::IsLoginScene(World))
	{
		// ★ 26.10.2026: строка — один раз на эпизод потери (реестр может отдавать мёртвый виджет кадр за кадром — без спама журнала).
		if (!AsdaSoulPopGuardLocal::GLostLogged || CVarSoulPopGuard.GetValueOnGameThread() == 0)
		{
			AsdaSoulPopGuardLocal::GLostLogged = true;
			UE_LOG(LogAsdaSoulPop, Log, TEXT("[панель Друга] окно 6500 потеряно с миром — поднимаю заново"));
		}
		EnsurePopWindow();
		// ★ 26.10.2026: окно после подъёма всё ещё не живое (реестр отдал виджет прежнего мира) — не рисуем, попробуем на следующем тике.
		const bool bLiveNow = PopWnd && (WM->IsWindowLive(PopWnd) || CVarSoulPopGuard.GetValueOnGameThread() == 0);
		if (bLiveNow)
		{
			RefreshPopCard();
			RefreshPopBuffs();
			WM->SetWindowVisible(kPopWindowId, true);
			if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>()) { Mm->RelayoutOccupationBox(); }
		}
	}

	if (PopWnd && WM->IsWindowLive(PopWnd)) { AsdaSoulPopGuardLocal::GLostLogged = false; }
	const bool bShown = PopWnd && WM->IsWindowLive(PopWnd) && WM->IsWindowVisible(kPopWindowId)
		&& UAsdaUiVisibilitySubsystem::IsUiShownFor(this) && !AsdaUi::IsModalDispatchBlocked(GI);
	if (!bShown)
	{
		HidePopTip();
		PopMouse.Reset();
		PopHoverId = -1;
		return;
	}
	if (PopPollFrame + 1 != GFrameCounter) { PopMouse.Reset(); }
	PopPollFrame = GFrameCounter;
	PopMouse.Update();

	// Мышь у панели, только если в этой точке она ВЕРХНЕЕ окно (z 40 — под окнами).
	const bool bOurs = WM->FindWindowCatchingAt(PopMouse.Cursor) == kPopWindowId;
	// ★ 23.10.2026 (ревизия, п.4) [ОРИГИНАЛ]: второй гейт msg 8 — `!helper_getField1(g_pUIManager)` (ничего не в руке,
	//   social.c:7994). Пока переносят предмет/навык («призрак» у курсора), ни подсказок, ни ЛКМ-цели.
	const bool bDragging = FAsdaDragGhost::IsAnyActive();
	int32 Over = -1, OverDst = -1;
	if (bOurs && !bDragging && Card.SlotIndex != -1)   // гейт msg 8: `gap_878[47] != -1` и ничего не в руке
	{
		auto Hit = [this](const TCHAR* Name) { return AsdaUi::IsCursorOverWidget(AsdaUi::FindNodeByPrefix(PopWnd, Name)); };
		if (Hit(TEXT("SOULMATE_POP_LV00_")))      { Over = 6501; }
		else if (Hit(TEXT("SOULMATE_POP_LV01_"))) { Over = 6505; }
		else if (Hit(TEXT("SOULMATE_GAUGE00_")))  { Over = 6504; }
		else
		{
			for (int32 D = 0; D < kPopBuffs; ++D)
			{
				UImage* Img = PopBuffIcons.IsValidIndex(D) ? PopBuffIcons[D].Get() : nullptr;
				if (Img && PopBuffId[D] >= 0 && AsdaUi::IsCursorOverWidget(Img))
				{
					Over = 33000 + AsdaSoulPopLocal::SiconForSlot(D);
					OverDst = D;
					break;
				}
			}
		}
	}
	if (Over != PopHoverId)
	{
		PopHoverId = Over;
		HidePopTip();                          // msg 0xA прежнего
		if (Over >= 0) { ShowPopTip(Over, OverDst); }   // msg 8 нового
	}

	// [ВЛАДЕЛЕЦ] ЛКМ по панели — Друг целью. Золотое яблоко 6693 — у `UAsdaSoulAppleSubsystem` (там свой клик).
	if (PopMouse.bLmbDown && bOurs && !bDragging && AsdaSoulPopLocal::CVarPopClickTarget.GetValueOnGameThread() != 0)
	{
		UWidget* Golden = AsdaUi::FindNodeByPrefix(PopWnd, TEXT("Soulmate_GoldenApple_"));
		const bool bOnGolden = Golden && Golden->GetVisibility() != ESlateVisibility::Collapsed && AsdaUi::IsCursorOverWidget(Golden);
		if (!bOnGolden) { SelectPartnerAsTarget(TEXT("ЛКМ по панели 6500")); }
	}
}
