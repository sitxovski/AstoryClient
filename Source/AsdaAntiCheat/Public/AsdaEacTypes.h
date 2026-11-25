// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEacTypes.h — общие типы клиентского античита EAC (Э1-Г, 16.10.2026): состояния, причины, коды контракта, loc-ключи.
//   Контракт (кадры, статусы, причины): Reports/anticheat/eac/CONTRACT.md §3, §5. Отчёт: eac/CLIENT_REPORT.md.
#pragma once

#include "CoreMinimal.h"

// Состояние клиентского античита (машина состояний FAsdaEacFlow; см. CLIENT_REPORT.md §2).
enum class EAsdaEacState : uint8
{
	Disabled = 0,     // выключен политикой: -AsdaNoNet, Development без -AsdaEac, неподдерживаемая платформа
	Unavailable,      // EAC недоступен: SDK/платформа не поднялись или интерфейса AC нет (запуск без bootstrapper)
	Idle,             // готов, ждёт ввода логина/пароля
	TokenRequest,     // POST AuthBaseUrl/token (или /refresh при возобновлении после смены персонажа)
	ConnectLogin,     // EOS_Connect_Login(EOS_ECT_OPENID_ACCESS_TOKEN)
	CreateUser,       // EOS_Connect_CreateUser(ContinuanceToken) после EOS_InvalidUser
	Reconnecting,     // канал 15901 потерян (или не поднялся) — ждём повтор (сессия AC закрыта, Connect жив)
	Registering,      // канал 15901 открыт, REG отправлен — ждём REG_RESULT
	Active,           // REG_RESULT 0: ACMSG в обе стороны, PING каждые 10 с
	ServerDisabled,   // REG_RESULT 4: EAC выключен на сервере (Mode=0) — сессия закрыта, игра идёт
	Failed,           // окончательный отказ: ★ R3-02 — только 401 (/token, /refresh) и REG_RESULT 2 → сообщение и возврат на логин
	Kicked            // KICK сайдкара или нарушение целостности — игровая сессия разорвана
};

// Имя состояния для журнала.
ASDAANTICHEAT_API const TCHAR* AsdaEacStateName(EAsdaEacState S);

// Источник фатального события (разрыв игровой сессии).
enum class EAsdaEacFatalSource : uint8
{
	SidecarKick = 0,      // кадр KICK (0x03) от сайдкара
	IntegrityViolation,   // EOS_AntiCheatClient_AddNotifyClientIntegrityViolated
	// ★ ред. 3 (E1-11, §8.4): окончательный отказ сессии EAC (401 /token и /refresh, REG_RESULT 2, повторный REG_RESULT 1,
	//   AsdaId недоступен после всех попыток, ошибка EOS) — игроку сообщение и возврат на логин, без молчаливого кика.
	SessionFailed
};

// ★ ред. 3: вид окончательного отказа (для выбора текста попапа; см. UAsdaNetSubsystem — разбор OnFatal).
enum class EAsdaEacFailKind : uint8
{
	None = 0,      // не отказ (KICK / нарушение)
	Auth,          // учётные данные/токен отвергнуты: войдите заново
	Unreachable    // сервис входа / EOS недоступен. ★ R3-02 (CONTRACT.md §8.6): поток его больше НЕ выдаёт — при недоступности
	               //   AsdaId/EOS/15901 игра идёт, повторы до 60 с; значение оставлено для разбора OnFatal в UAsdaNetSubsystem
};

// Фатальное событие античита: UI показывает причину, сеть рвёт игровую сессию штатным путём сброса.
struct FAsdaEacFatal
{
	EAsdaEacFatalSource Source = EAsdaEacFatalSource::SidecarKick;   // откуда пришло
	uint8   OurReason = 0;        // KICK: ourReason (CONTRACT.md §5: 1..5, ред. 3 §8.1: 6); для нарушения целостности — 0
	uint8   EosReason = 0;        // KICK: EOS_EAntiCheatCommonClientActionReason
	int32   ViolationType = 0;    // нарушение: EOS_EAntiCheatClientViolationType (1..15); для KICK — 0
	FString Details;              // KICK: UTF-8 детали сайдкара; нарушение: ViolationMessage от EAC (локализован EAC)
	int32   LocId = 0;            // id строки DT_GameText для UI (диапазон 991000+; показ — AsdaEacLoc::ForFatal)
	EAsdaEacFailKind FailKind = EAsdaEacFailKind::None;   // ★ ред. 3: для SessionFailed — вид отказа
};

// Коды контракта §3 (тип кадра канала 15901).
namespace AsdaEacWire
{
	constexpr uint8  kReg       = 0x01;   // К→С регистрация
	constexpr uint8  kRegResult = 0x81;   // С→К итог регистрации
	constexpr uint8  kAcMsg     = 0x02;   // оба: сырые байты EAC 1..512
	constexpr uint8  kKick      = 0x03;   // С→К кик
	constexpr uint8  kPing      = 0x04;   // К→С пинг (каждые 10 с)
	constexpr uint8  kPong      = 0x84;   // С→К понг
	constexpr uint8  kBind      = 0x05;   // ★ ред. 3 (§8.1) К→С привязка игрового соединения: u8 serverKind · u8[32] bind
	constexpr uint8  kBindResult = 0x85;  // ★ ред. 3 (§8.1) С→К итог привязки: u8 status
	constexpr uint8  kBindAccepted = 0;   // BIND_RESULT: принято
	constexpr uint8  kBindRejected = 2;   // BIND_RESULT: отклонено (нет аутентифицированной сессии)
	constexpr uint8  kServerKindGame  = 2;   // serverKind BIND: GameServer (как HELLO §4)
	constexpr uint8  kServerKindIndun = 3;   // serverKind BIND: Indun_GameServer
	constexpr int32  kMaxLen    = 4096;   // предел поля len (type + payload)
	constexpr int32  kMaxAcMsg  = 512;    // предел полезной нагрузки ACMSG (EOS_ANTICHEATCLIENT_ONMESSAGETOSERVERCALLBACK_MAX_MESSAGE_SIZE)
	constexpr uint8  kRegVer    = 1;      // версия REG
	constexpr uint8  kPlatformWindows = 1;   // EOS_ACCCP_Windows

	// REG_RESULT status (CONTRACT.md §3).
	constexpr uint8 kRegOk          = 0;   // зарегистрирован
	constexpr uint8 kRegBadToken    = 1;   // токен не прошёл
	constexpr uint8 kRegBadAccount  = 2;   // учётка не совпала / не найдена
	constexpr uint8 kRegInternal    = 3;   // внутренняя ошибка сайдкара
	constexpr uint8 kRegEacDisabled = 4;   // EAC выключен на сервере (Mode=0)
}

// ★ loc-ключи UI античита (DT_GameText, диапазон 991000–991040 — свободен: в GameText.csv и ведомостях max 990994).
//   ★ 16.10.2026 (agent-eac-texts): тексты ВНЕСЕНЫ — RU ведомость `Tools/data/gametext_overrides.json` (DT_GameText),
//   EN `Tools/data/loc/en/GameText.json` (оверлей DT_Loc_GameText_en). Утверждены владельцем 16.10
//   (`Reports/anticheat/eac/PLAYER_TEXTS.md`); 991001–991015 — локализация тулкита EAC 2.3.0 (game_error), как есть.
//   991034 (REG_RESULT 4) игроку не показывается — только журнал; 991040 (401 /refresh) своего текста не имеет —
//   показывается 991037 «Сессия входа истекла» (у владельца 991037 = «отклонил логин/пароль ИЛИ сессия истекла»).
//   ПОКАЗ: жёсткие (KICK, нарушение, отказ входа) — попап разрыва / окно кика логина (ForFatal); мягкие (повтор идёт сам) —
//   одна строка в системном чате на смену состояния (FAsdaEacFlow::OnSoftNotice). Строки нет в таблице — нотис оригинала.
namespace AsdaEacLoc
{
	constexpr int32 kBase              = 991000;   // начало диапазона
	constexpr int32 kViolationBase     = 991000;   // + EOS_EAntiCheatClientViolationType (1..15) → 991001..991015
	constexpr int32 kKickBase          = 991020;   // + ourReason (1..6) → 991021..991026
	constexpr int32 kRegRejectedBase   = 991030;   // + REG_RESULT status (1..4) → 991031..991034
	constexpr int32 kUnavailable       = 991036;   // «EAC недоступен» (нет bootstrapper / SDK)
	constexpr int32 kAuthRejected      = 991037;   // AsdaId отверг вход (401)
	constexpr int32 kAuthUnreachable   = 991038;   // AsdaId недоступен (★ R3-02: отказа нет — повторы; ★ 16.10: мягкая строка в чат)
	constexpr int32 kEosFailed         = 991039;   // ошибка EOS Connect / BeginSession (★ R3-02: повторы; ★ 16.10: мягкая строка в чат)
	constexpr int32 kRefreshRejected   = 991040;   // ★ ред. 3 (§8.4): /refresh → 401 — «войдите заново»
	constexpr int32 kKickMaxReason     = 6;        // ★ ред. 3 (§8.1): причина 6 «не привязано» → 991026

	// loc-id для нарушения целостности по типу EAC (вне 1..15 — общий 991000).
	inline int32 ForViolation(int32 Type) { return (Type >= 1 && Type <= 15) ? kViolationBase + Type : kViolationBase; }
	// loc-id для KICK по ourReason (вне 1..6 — общий 991020).
	inline int32 ForKick(int32 OurReason) { return (OurReason >= 1 && OurReason <= kKickMaxReason) ? kKickBase + OurReason : kKickBase; }

	// ── ★ 16.10.2026: показ игроку (тексты утверждены владельцем) ──
	constexpr int32 kViolationTitle    = 991000;   // заголовок окна «Easy Anti-Cheat: нарушение целостности» (над 991001–991015)
	constexpr int32 kKickTitle         = 991020;   // заголовок окна «Отключено античитом» (над 991021–991026)
	constexpr int32 kSoftRegBadToken   = kRegRejectedBase + 1;   // 991031 мягкое: REG_RESULT 1, повтор идёт сам
	constexpr int32 kSoftRegInternal   = kRegRejectedBase + 3;   // 991033 мягкое: REG_RESULT 3, повтор идёт сам
	constexpr int32 kServerDisabledLog = kRegRejectedBase + 4;   // 991034 REG_RESULT 4 — игроку НЕ показывается (только журнал)

	// Коды разрыва оригинала (таблица `g_nDisconnectReason → нотис`, UAsdaDisconnectPopupSubsystem::ReasonToNoticeId) — запасной
	//   показ, когда строки 991xxx в таблице нет.
	constexpr uint8 kReasonCheat     = 6;    // 1106 «отключён за нечестную игру»
	constexpr uint8 kReasonDuplicate = 2;    // 1099 «смена точки входа» (дубль-сессия)
	constexpr uint8 kReasonAuth      = 11;   // 12941 «ошибка авторизации»
	constexpr uint8 kReasonNetwork   = 1;    // 1098 «сбой в сети»

	// Что показать игроку при фатальном событии: заголовок + текст + (по случаю) деталь от EAC; и запасной код разрыва.
	struct FFatalDisplay
	{
		int32 TitleId = 0;            // строка-заголовок (0 — без заголовка)
		int32 BodyId = 0;             // строка-текст (0 — своего текста нет: только деталь EAC, иначе — нотис оригинала)
		bool  bDetails = false;       // добавить деталь EAC (`FAsdaEacFatal::Details`) последней строкой
		uint8 FallbackReason = kReasonCheat;   // код разрыва оригинала, если строк 991xxx нет (нотис 1106/1099/12941/1098)
	};

	// Ключи показа по фатальному событию (чистая функция — самотест asda.SelfTest.Eac, раздел 10).
	//   KICK: 991020 + 991021..991026 по ourReason; причина 2 (решение EAC) — деталь EAC второй строкой; причина вне 1..6 —
	//         заголовок + деталь. Запасной код: причина 5 (вытеснение) — 2 (1099), прочие — 6 (1106).
	//   Нарушение целостности: 991000 + 991001..991015 по типу; тип вне 1..15 — заголовок + текст EAC (ViolationMessage).
	//   Отказ входа (SessionFailed): только текст LocId (991032 / 991037; 991040 → 991037). Запасной: Auth — 11, иначе 1.
	inline FFatalDisplay ForFatal(const FAsdaEacFatal& F)
	{
		FFatalDisplay D;
		switch (F.Source)
		{
		case EAsdaEacFatalSource::SidecarKick:
			D.TitleId = kKickTitle;
			D.BodyId = (F.LocId > kKickTitle && F.LocId <= kKickBase + kKickMaxReason) ? F.LocId : 0;
			D.bDetails = (F.OurReason == 2) || (D.BodyId == 0);
			D.FallbackReason = (F.OurReason == 5) ? kReasonDuplicate : kReasonCheat;
			break;
		case EAsdaEacFatalSource::IntegrityViolation:
			D.TitleId = kViolationTitle;
			D.BodyId = (F.LocId > kViolationTitle && F.LocId <= kViolationBase + 15) ? F.LocId : 0;
			D.bDetails = (D.BodyId == 0);
			D.FallbackReason = kReasonCheat;
			break;
		default:   // SessionFailed
			D.TitleId = 0;
			D.BodyId = (F.LocId == kRefreshRejected) ? kAuthRejected : F.LocId;
			D.bDetails = false;   // в Details отказа — служебная причина для журнала, игроку не показывается
			D.FallbackReason = (F.FailKind == EAsdaEacFailKind::Auth) ? kReasonAuth : kReasonNetwork;
			break;
		}
		return D;
	}

	// Мягкое состояние (игра идёт, повтор сам) — показывается строкой в системном чате, один раз на смену состояния.
	inline bool IsSoft(int32 LocId)
	{
		return LocId == kSoftRegBadToken || LocId == kSoftRegInternal || LocId == kAuthUnreachable || LocId == kEosFailed;
	}
	// Нотис оригинала вместо мягкой строки, если её нет в таблице (колонка «Сейчас» PLAYER_TEXTS.md): 991031 → 12941,
	//   991038/991039 → 1098; у 991033 запасного нет (0 — только журнал).
	inline int32 SoftFallbackNotice(int32 LocId)
	{
		if (LocId == kSoftRegBadToken) { return 12941; }
		if (LocId == kAuthUnreachable || LocId == kEosFailed) { return 1098; }
		return 0;
	}
}
