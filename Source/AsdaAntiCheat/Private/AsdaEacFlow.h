// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEacFlow.h — машина состояний клиентского EAC (Э1-Г, 16.10.2026). Чистая логика: все внешние действия (HTTP AsdaId,
//   EOS Connect / AntiCheatClient, канал 15901) идут через IAsdaEacBackend. Реальная сторона — FAsdaEacEosBackend
//   (AsdaAntiCheatSubsystem.cpp), фальшивая — самотест asda.SelfTest.Eac (без сети и без EOS-бэкенда).
//
// ПОТОК ВХОДА (EAC_PLAN.md §3.1 К4, CONTRACT.md §2–§3):
//   StartLogin → POST /token {login, password_md5} → EOS_Connect_Login(OPENID_ACCESS_TOKEN) [InvalidUser → CreateUser]
//   → CopyIdToken (PUID + JWT EOS) → BeginSession(ClientServer) → канал 15901 (AsdaSecure, SubServer Eac=4) → REG
//   → REG_RESULT 0 → Active: ACMSG в обе стороны, PING каждые 10 с.
// ОБНОВЛЕНИЕ: AuthExpiration → POST /refresh → повторный Connect_Login (refresh-токен — только в памяти).
// ФАТАЛЬНОЕ: KICK сайдкара / нарушение целостности → полный сброс + событие OnFatal (сеть рвёт игровую сессию).
// ★ ред. 3: окончательный отказ (401 /token и /refresh, REG_RESULT 2 и т. п.) → OnFatal(SessionFailed): сообщение и логин.
//   BIND (§8.1) — привязка игрового соединения после каждого рукопожатия; REG_RESULT 4 — повтор REG раз в 300 с.
// ★ R3-01 (CONTRACT.md §8.6): смена персонажа и возврат к выбору персонажа НЕ закрывают 15901 и не завершают сессию EAC
//   (ни EndSession, ни Connect_Logout); повторный ввод пароля той же учёткой на том же хосте сессию тоже не трогает.
//   После нового входа в мир уходит только новый BIND. Полный сброс (EndSession, 15901, Connect_Logout, refresh стёрт) —
//   только выход из игры, настоящий разрыв (LoginManager / игровое соединение без смены персонажа), другая учётка, отказ.
// ★ R3-02 (§8.6): на логин (OnFatal) поток уводит ТОЛЬКО по 401 (/token, /refresh), REG_RESULT 2, KICK (и нарушению
//   целостности — это кик самого EAC). 503 / сеть / 400 AsdaId, ошибки EOS Connect / CreateUser / CopyIdToken /
//   BeginSession, недоступный 15901, REG_RESULT 1/3 — игра продолжается, повтор с растущей паузой до 60 с без потолка
//   числа попыток (решение о кике — за сервером, FailOpen). Что и когда повторяется — в лог и в asda.Eac.Status.
#pragma once

#include "CoreMinimal.h"
#include "AsdaEacTypes.h"
#include "AsdaEacFrame.h"

// Итог EOS_Connect_Login, упрощённый до того, что нужно потоку.
enum class EAsdaEacConnectResult : uint8
{
	Success = 0,     // EOS_Success — PUID получен
	InvalidUser,     // EOS_InvalidUser — учётки в EOS ещё нет, нужен CreateUser(ContinuanceToken)
	Error            // любая другая ошибка
};

// Внешние действия потока. Все асинхронные итоги возвращаются в поток с тем же Gen (устаревшие отбрасываются).
class IAsdaEacBackend
{
public:
	virtual ~IAsdaEacBackend() = default;
	// POST AuthBaseUrl+Path (тело JSON). Итог → FAsdaEacFlow::OnHttpResult(Gen, код, тело); код 0 — нет ответа (сеть).
	virtual void HttpPost(const FString& Path, const FString& JsonBody, uint32 Gen) = 0;
	// EOS_Connect_Login(EOS_ECT_OPENID_ACCESS_TOKEN). Итог → OnConnectLoginResult(Gen, …).
	virtual void ConnectLogin(const FString& AccessToken, uint32 Gen) = 0;
	// EOS_Connect_CreateUser(ContinuanceToken последнего InvalidUser). Итог → OnCreateUserResult(Gen, …).
	virtual void CreateUser(uint32 Gen) = 0;
	// EOS_Connect_CopyIdToken: PUID строкой и JWT EOS. false — нет входа в Connect.
	virtual bool CopyIdToken(FString& OutPuid, FString& OutIdToken) = 0;
	// EOS_AntiCheatClient_BeginSession(LocalUserId=PUID, ClientServer). false — отказ SDK.
	virtual bool BeginSession() = 0;
	// EOS_AntiCheatClient_EndSession.
	virtual void EndSession() = 0;
	// EOS_Connect_Logout + забыть PUID и ContinuanceToken.
	virtual void ConnectLogout() = 0;
	// Открыть канал 15901 (FAsdaConnection, сырой поток, AsdaSecure роль 4). true — TCP установлен (рукопожатие идёт).
	virtual bool ChannelOpen(const FString& Host, int32 Port) = 0;
	// Закрыть канал (из колбэка канала — отложенно, после выхода из его Tick).
	virtual void ChannelClose() = 0;
	// Отправить байты в канал. false — канал мёртв.
	virtual bool ChannelSend(const TArray<uint8>& Bytes) = 0;
	// EOS_AntiCheatClient_ReceiveMessageFromServer.
	virtual void DeliverToAntiCheat(const uint8* Data, int32 Len) = 0;
};

// Машина состояний (один экземпляр на подсистему).
class FAsdaEacFlow
{
public:
	// Сроки контракта и политики повторов (секунды).
	static constexpr double kPingSec        = 10.0;   // PING каждые 10 с (CONTRACT.md §3)
	static constexpr double kLivenessSec    = 30.0;   // без данных от сайдкара 30 с — канал считается потерянным
	static constexpr double kRegTimeoutSec  = 30.0;   // REG_RESULT не пришёл за 30 с — переподключение
	static constexpr double kRetryMaxSec     = 60.0;  // ★ R3-02: потолок любой паузы повтора (HTTP, EOS, 15901) — 60 с, число попыток не ограничено
	static constexpr double kReconnectBaseSec = 2.0;  // первая пауза переподключения 15901 (2, 4, 8, 16, 32, 60, 60 …)
	static constexpr double kHttpBaseSec     = 5.0;   // первая пауза повтора AsdaId (5, 10, 20, 40, 60, 60 …)
	static constexpr int32  kQueueMax        = 256;   // предел очереди ACMSG до REG_RESULT 0
	static constexpr double kServerDisabledRetrySec = 300.0;   // ★ ред. 3 (E1-21, §8.4): после REG_RESULT 4 — повтор REG раз в 300 с
	static constexpr double kBindRetrySec    = 5.0;    // ★ ред. 3 (§8.1): BIND_RESULT 2 — повтор BIND через 5 с
	static constexpr int32  kBindRetryMax    = 12;     // ★ ред. 3: не больше 12 повторов на одно значение привязки (~60 с)

	explicit FAsdaEacFlow(IAsdaEacBackend& InBackend) : B(InBackend) {}

	// Политика включения: false — Disabled; true + bAvailable=false — Unavailable; иначе Idle. Why — причина (для UI/журнала).
	void SetPolicy(bool bEnabled, bool bAvailable, const FString& Why = FString());

	// Игрок ввёл логин/пароль (тот же MD5-hex, что уходит LoginManager в 1003). Host — хост LoginManager.
	//   ★ R3-01: та же учётка на том же хосте:порту при живом Connect-входе (повторный ввод пароля после возврата к выбору
	//   персонажа) — сессия EAC и канал 15901 сохраняются, /token не шлётся, MD5 не запоминается. Иначе — полный перезапуск.
	void StartLogin(const FString& InLogin, const FString& InPasswordMd5Hex, const FString& InHost, int32 InPort, double Now);
	// Вход в мир: если сессия EAC не идёт (Idle), а refresh-токен есть — поднять через /refresh. Идущую сессию не трогает:
	//   после входа в мир сайдкару уходит только BIND нового игрового соединения (OnGameChannelBound).
	void ResumeFromRefresh(double Now);
	// ★ R3-01: смена персонажа / возврат к выбору персонажа. Канал 15901, сессия EAC и Connect-вход СОХРАНЯЮТСЯ; забывается
	//   только привязка прежнего игрового соединения (его больше нет) — после входа в мир придёт новая.
	void StopForCharacterChange();
	// Разрыв / выход: полный сброс (включая refresh-токен и хост).
	void StopFull(const TCHAR* Why);

	// Итоги внешних действий.
	void OnHttpResult(uint32 InGen, int32 Code, const FString& Body, double Now);
	void OnConnectLoginResult(uint32 InGen, EAsdaEacConnectResult Result, const FString& ErrText, double Now);
	void OnCreateUserResult(uint32 InGen, bool bOk, const FString& ErrText, double Now);
	void OnChannelData(const uint8* Data, int32 Len, double Now);
	void OnChannelClosed(double Now);
	void OnMessageToServer(const uint8* Data, int32 Len);
	void OnIntegrityViolated(int32 ViolationType, const FString& Message);
	void OnAuthExpiration(double Now);
	// ★ ред. 3 (CONTRACT.md §8.1): рукопожатие AsdaSecure игрового соединения завершено. Привязка запоминается и уходит
	//   кадром BIND сразу, если REG уже принят (Active), иначе — сразу после REG_RESULT 0 (и после каждой новой регистрации).
	void OnGameChannelBound(uint8 ServerKind, const uint8* Bind32, double Now);
	// Таймеры: PING, живость канала, срок REG, повторы HTTP, переподключение.
	void Tick(double Now);

	// Наблюдатели (ставит подсистема).
	TFunction<void(EAsdaEacState)> OnState;              // смена состояния
	TFunction<void(const FAsdaEacFatal&)> OnFatal;       // фатальное событие (разрыв игровой сессии)
	// ★ 16.10.2026: мягкое состояние (игра идёт, повтор сам; loc 991031/991033/991038/991039) — ОДИН раз на смену состояния,
	//   не на каждую попытку. Сеть показывает строку в системном чате.
	TFunction<void(int32)> OnSoftNotice;

	// Состояние для UI/журнала и самотеста.
	EAsdaEacState GetState() const { return State; }
	const FString& GetPuid() const { return Puid; }
	const FString& GetLastError() const { return LastError; }
	uint8 GetLastRegStatus() const { return LastRegStatus; }
	int32 GetSoftLocId() const { return SoftLocId; }                 // ★ 16.10 самотест: текущее мягкое состояние (0 — нет)
	bool HasPasswordMd5() const { return !PassMd5.IsEmpty(); }       // самотест: MD5 стирается сразу после /token
	bool HasRefreshToken() const { return !RefreshToken.IsEmpty(); } // самотест: refresh держится только в памяти
	int32 GetQueuedCount() const { return OutQueue.Num(); }
	bool HasGameBind() const { return bHaveBind; }                   // ★ ред. 3 самотест: привязка игрового соединения известна
	bool IsBindAcked() const { return bBindAcked; }                  // ★ ред. 3 самотест: сайдкар принял BIND (BIND_RESULT 0)
	double GetServerDisabledRetryAt() const { return ServerDisabledRetryAt; }   // ★ ред. 3 самотест: момент повтора REG после 4
	uint32 GetGen() const { return Gen; }
	// ★ R3-02: ближайший назначенный повтор (HTTP AsdaId / переподключение 15901 / REG после 4); 0 — повтора нет.
	double GetNextRetryAt() const;
	// ★ R3-02: что и когда повторяется — строка для журнала и asda.Eac.Status («нет» — повтора нет).
	FString DescribeRetry(double Now) const;
	// ★ R3-02: пауза повтора номер Attempt (1, 2, …): Base·2^(Attempt−1), не больше kRetryMaxSec.
	static double RetryDelay(double Base, int32 Attempt)
	{
		return FMath::Min(kRetryMaxSec, Base * (double)(1 << FMath::Clamp(Attempt - 1, 0, 10)));
	}
	// ★ R3-01: закрытие игрового соединения (UAsdaNetSubsystem::HandleConnectionClosed) — это смена персонажа, а не разрыв,
	//   только если соединение было игровым, клиент сам отправил возврат к выбору персонажа (1028) и кика 4101 перед
	//   закрытием не было (KickCode 0xFF). Во всех остальных случаях — настоящий разрыв: полный сброс EAC.
	static bool IsCharacterChangeClose(bool bWasGame, bool bReturnToCharSelectSent, uint8 KickCode)
	{
		return bWasGame && bReturnToCharSelectSent && KickCode == 0xFF;
	}

	// Политика включения клиента (CONTRACT.md §6): -AsdaNoNet — никогда; Shipping — всегда; Development — только с -AsdaEac.
	static bool ComputeEnabled(bool bShippingBuild, bool bNoNet, bool bEacFlag)
	{
		if (bNoNet) { return false; }
		if (bShippingBuild) { return true; }
		return bEacFlag;
	}

private:
	// Назначение текущего HTTP-запроса.
	enum class EHttp : uint8 { None, Token, Resume, Refresh };

	IAsdaEacBackend& B;                    // внешние действия
	EAsdaEacState State = EAsdaEacState::Disabled;
	uint32 Gen = 1;                        // поколение: растёт при сбросе — итоги прошлых запросов отбрасываются

	FString Login;                         // логин игрока (только до ответа /token)
	FString PassMd5;                       // MD5-hex пароля (только до ответа /token, затем стирается)
	FString Host;                          // хост LoginManager = хост сайдкара
	int32   Port = 15901;                  // порт сайдкара (SidecarPort)
	FString RefreshToken;                  // refresh AsdaId — только в памяти процесса
	FString SessionLogin;                  // ★ R3-01: логин учётки текущей сессии EAC (сверка повторного ввода пароля; не секрет)
	int32   RecoverAttempt = 0;            // ★ R3-02: номер подряд идущего восстановления входа EOS (/refresh → Connect), для паузы

	EHttp   Http = EHttp::None;            // какой HTTP-запрос в полёте (или ждёт повтора)
	int32   HttpAttempt = 0;               // номер попытки текущего запроса
	double  HttpRetryAt = 0.0;             // момент повтора (0 — повтора нет)
	bool    bRefreshLogin = false;         // идущий Connect_Login — обновление токена при живой сессии

	bool    bConnectLoggedIn = false;      // Connect-вход есть (PUID выдан)
	bool    bSessionBegun = false;         // BeginSession выполнен
	bool    bChannelOpen = false;          // канал 15901 открыт
	bool    bChannelGotData = false;       // ★ живая проверка 16.10: после открытия канала уже приходили данные (строка в журнал — один раз)
	bool    bRegRetried = false;           // REG_RESULT 1 уже повторяли (вторая — отказ)
	FString Puid;                          // PUID строкой (для REG и журнала)
	FString IdToken;                       // JWT EOS Connect (для REG)

	double  RegSentAt = 0.0;               // момент отправки REG
	double  LastRecvAt = 0.0;              // момент последнего кадра от сайдкара
	double  LastPingAt = 0.0;              // момент последнего PING
	double  ReconnectAt = 0.0;             // момент переподключения (состояние Reconnecting)
	int32   ReconnectAttempt = 0;          // номер попытки переподключения (пауза 2,4,8,16,32,60,60 … с)
	double  NowCache = 0.0;                // время последнего Tick/приёма — для событий без своего времени (колбэк EAC)

	TArray<TArray<uint8>> OutQueue;        // ACMSG, созданные EAC до REG_RESULT 0
	AsdaEacFrame::FDecoder Dec;            // разбор потока сайдкара
	FString LastError;                     // последняя причина отказа (для UI/журнала)
	uint8   LastRegStatus = 0xFF;          // последний REG_RESULT (0xFF — не было)
	int32   SoftLocId = 0;                 // ★ 16.10: loc показанного игроку мягкого состояния (0 — нет); сбрасывается успехом/сбросом
	// ── ★ ред. 3 (CONTRACT.md §8.1): привязка игрового соединения ──
	bool    bHaveBind = false;             // известна привязка текущего игрового соединения
	uint8   BindKind = AsdaEacWire::kServerKindGame;   // serverKind этой привязки
	uint8   GameBind[32] = {};             // экспортёр AsdaSecure игрового соединения (затирается при сбросе)
	bool    bBindAcked = false;            // сайдкар принял именно это значение (BIND_RESULT 0) в текущей регистрации
	bool    bBindInFlight = false;         // BIND отправлен, ждём BIND_RESULT
	int32   BindRetries = 0;               // повторов BIND этого значения после BIND_RESULT 2
	double  BindRetryAt = 0.0;             // момент повтора BIND (0 — повтора нет)
	double  ServerDisabledRetryAt = 0.0;   // ★ ред. 3 (E1-21): момент повтора REG после REG_RESULT 4 (0 — не ждём)

	void SetState(EAsdaEacState S);
	// ★ 16.10: смена мягкого состояния. Новый LocId (≠ текущему, > 0) — событие OnSoftNotice; тот же — молча (повтор
	//   попытки не спамит чат); 0 — состояние снято (успех/сброс), следующий сбой снова покажется.
	void SetSoft(int32 LocId);
	// Окончательный отказ: сброс и состояние Failed; ★ ред. 3 (E1-11): событие OnFatal(SessionFailed, LocId, Kind) —
	//   сеть показывает сообщение и возвращает на логин. ★ R3-02: зовётся только на 401 (/token, /refresh) и REG_RESULT 2.
	void Fail(const FString& Why, int32 LocId, EAsdaEacFailKind Kind);
	void SendBind(double Now);             // ★ ред. 3: кадр BIND текущей привязки (только в Active)
	void WipeBind();                       // ★ ред. 3: забыть привязку игрового соединения (затереть)
	void SendHttp(EHttp What, double Now);
	void HandleHttpFailure(int32 Code, double Now);
	// ★ R3-02: повтор того же HTTP-запроса с растущей паузой (без потолка числа попыток).
	void ScheduleHttpRetry(int32 Code, const TCHAR* Why, double Now);
	// ★ R3-02: временный сбой EOS (Connect / CreateUser / CopyIdToken / REG не собран, Reconnecting без Connect-входа) —
	//   Connect сбросить и через паузу поднять вход заново: /refresh → Connect_Login → BeginSession → REG. Без выхода на логин.
	void ScheduleRelogin(const FString& Why, double Now);
	void StartSessionAndChannel(double Now);
	void CloseChannelAndSession();
	void LoseChannel(const TCHAR* Why, double Now);
	void Teardown(bool bKeepRefresh);
	void HandleFrame(const AsdaEacFrame::FFrame& F, double Now);
	void SendAcMsg(const uint8* Data, int32 Len, double Now);
	void FlushQueue(double Now);
	void RaiseFatal(const FAsdaEacFatal& F);
	static void Wipe(FString& S);
	bool IsOff() const { return State == EAsdaEacState::Disabled || State == EAsdaEacState::Unavailable; }
};
