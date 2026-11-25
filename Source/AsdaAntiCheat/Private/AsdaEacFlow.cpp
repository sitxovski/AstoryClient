// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEacFlow.cpp — машина состояний клиентского EAC. Описание потока — в заголовке; отчёт — eac/CLIENT_REPORT.md §2.
#include "AsdaEacFlow.h"
#include "AsdaAntiCheatLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

const TCHAR* AsdaEacStateName(EAsdaEacState S)
{
	switch (S)
	{
	case EAsdaEacState::Disabled:       return TEXT("выключен");
	case EAsdaEacState::Unavailable:    return TEXT("EAC недоступен");
	case EAsdaEacState::Idle:           return TEXT("ожидание входа");
	case EAsdaEacState::TokenRequest:   return TEXT("запрос токена AsdaId");
	case EAsdaEacState::ConnectLogin:   return TEXT("EOS Connect: вход");
	case EAsdaEacState::CreateUser:     return TEXT("EOS Connect: создание пользователя");
	case EAsdaEacState::Reconnecting:   return TEXT("переподключение к сайдкару");
	case EAsdaEacState::Registering:    return TEXT("регистрация у сайдкара");
	case EAsdaEacState::Active:         return TEXT("сессия EAC активна");
	case EAsdaEacState::ServerDisabled: return TEXT("EAC выключен на сервере");
	case EAsdaEacState::Failed:         return TEXT("отказ");
	case EAsdaEacState::Kicked:         return TEXT("кик/нарушение — игровая сессия разорвана");
	default:                            return TEXT("?");
	}
}

// Тело JSON одной строкой (без пробелов и переводов строки).
static FString AsdaEacJson(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj, W);
	return Out;
}

void FAsdaEacFlow::Wipe(FString& S)
{
	auto& A = S.GetCharArray();
	if (A.Num() > 0) { FMemory::Memzero(A.GetData(), A.Num() * sizeof(TCHAR)); }   // затереть память, не только длину
	S.Empty();
}

void FAsdaEacFlow::SetState(EAsdaEacState S)
{
	if (State == S) { return; }
	ASDA_EAC_LOG(Log, TEXT("[EAC] состояние: %s → %s"), AsdaEacStateName(State), AsdaEacStateName(S));
	State = S;
	if (OnState) { OnState(S); }
}

void FAsdaEacFlow::SetSoft(int32 LocId)
{
	if (LocId == SoftLocId) { return; }   // то же состояние — повтор попытки, игроку второй раз не пишем
	SoftLocId = LocId;
	if (LocId <= 0) { return; }
	ASDA_EAC_LOG(Log, TEXT("[EAC] мягкое состояние → строка игроку loc %d (одна на смену состояния; повтор идёт сам)"), LocId);
	if (OnSoftNotice) { OnSoftNotice(LocId); }
}

void FAsdaEacFlow::SetPolicy(bool bEnabled, bool bAvailable, const FString& Why)
{
	Teardown(/*bKeepRefresh*/false);
	Wipe(Host);
	Wipe(SessionLogin);
	LastError = Why;
	SetState(!bEnabled ? EAsdaEacState::Disabled : (!bAvailable ? EAsdaEacState::Unavailable : EAsdaEacState::Idle));
}

void FAsdaEacFlow::Fail(const FString& Why, int32 LocId, EAsdaEacFailKind Kind)
{
	LastError = Why;
	ASDA_EAC_LOG(Warning, TEXT("[EAC] отказ — %s (loc %d) → сообщение игроку и возврат на логин"), *Why, LocId);
	Teardown(/*bKeepRefresh*/false);
	WipeBind();
	Wipe(Host);
	Wipe(SessionLogin);
	RecoverAttempt = 0;
	SetState(EAsdaEacState::Failed);
	// ★ ред. 3 (E1-11, CONTRACT.md §8.4): без молчаливого кика — сеть показывает текст и возвращает на логин (OnFatal).
	FAsdaEacFatal F;
	F.Source = EAsdaEacFatalSource::SessionFailed;
	F.LocId = LocId;
	F.FailKind = Kind;
	F.Details = Why;
	if (OnFatal) { OnFatal(F); }
}

void FAsdaEacFlow::WipeBind()
{
	FMemory::Memzero(GameBind, sizeof(GameBind));
	bHaveBind = false;
	bBindAcked = false;
	bBindInFlight = false;
	BindRetries = 0;
	BindRetryAt = 0.0;
}

void FAsdaEacFlow::OnGameChannelBound(uint8 ServerKind, const uint8* Bind32, double Now)
{
	if (IsOff() || !Bind32) { return; }
	// Новое рукопожатие игрового соединения = новое значение привязки (прежнее соединение закрыто).
	FMemory::Memcpy(GameBind, Bind32, sizeof(GameBind));
	BindKind = ServerKind;
	bHaveBind = true;
	bBindAcked = false;
	bBindInFlight = false;
	BindRetries = 0;
	BindRetryAt = 0.0;
	if (State == EAsdaEacState::Active && bChannelOpen)
	{
		ASDA_EAC_LOG(Log, TEXT("[EAC] рукопожатие игрового соединения (вид %u) — BIND сразу (регистрация уже принята)"), (uint32)ServerKind);
		SendBind(Now);
	}
	else
	{
		ASDA_EAC_LOG(Log, TEXT("[EAC] рукопожатие игрового соединения (вид %u) — BIND уйдёт после REG_RESULT 0 (состояние: %s)"),
			(uint32)ServerKind, AsdaEacStateName(State));
	}
}

void FAsdaEacFlow::SendBind(double Now)
{
	if (!bHaveBind || State != EAsdaEacState::Active || !bChannelOpen) { return; }
	TArray<uint8> Frame;
	if (!AsdaEacFrame::BuildBind(BindKind, GameBind, Frame)) { return; }
	BindRetryAt = 0.0;
	bBindInFlight = true;
	const bool bOk = B.ChannelSend(Frame);
	FMemory::Memzero(Frame.GetData(), Frame.Num());
	if (!bOk) { bBindInFlight = false; LoseChannel(TEXT("отправка BIND не удалась"), Now); }
}

void FAsdaEacFlow::StartLogin(const FString& InLogin, const FString& InPasswordMd5Hex, const FString& InHost, int32 InPort, double Now)
{
	if (IsOff())
	{
		ASDA_EAC_LOG(Log, TEXT("[EAC] вход без EAC: %s"), AsdaEacStateName(State));
		return;
	}
	if (InLogin.IsEmpty() || InPasswordMd5Hex.Len() != 32 || InHost.IsEmpty())
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] вход не начат: пустой логин/хост или MD5 не 32 символа (%d)"), InPasswordMd5Hex.Len());
		return;
	}
	// ★ R3-01 (CONTRACT.md §8.6): возврат к выбору персонажа в порту ведёт через экран логина — игрок вводит пароль заново.
	//   Та же учётка на том же сайдкаре при живом Connect-входе — это смена персонажа, а не новый вход: сессию EAC, канал
	//   15901 и Connect не трогаем (новая REG здесь шла бы в лимит REG сайдкара), /token не шлём, MD5 не храним.
	if (bConnectLoggedIn && !SessionLogin.IsEmpty() && InLogin.Equals(SessionLogin, ESearchCase::CaseSensitive)
		&& InHost.Equals(Host, ESearchCase::IgnoreCase) && InPort == Port
		&& State != EAsdaEacState::Failed && State != EAsdaEacState::Kicked)
	{
		ASDA_EAC_LOG(Log, TEXT("[EAC] повторный ввод пароля той же учёткой (%s) — сессия EAC и канал 15901 сохраняются (состояние: %s)"),
			*FAsdaEacDiag::MaskLogin(SessionLogin), AsdaEacStateName(State));
		return;
	}
	Teardown(/*bKeepRefresh*/false);   // прошлая сессия (если была) закрывается целиком: другая учётка / другой сервер / не было входа
	Wipe(SessionLogin);
	WipeBind();
	RecoverAttempt = 0;
	Login = InLogin;
	PassMd5 = InPasswordMd5Hex.ToLower();   // контракт §1: 32 строчных hex
	Host = InHost;
	Port = InPort;
	LastError.Reset();
	LastRegStatus = 0xFF;
	SendHttp(EHttp::Token, Now);
}

void FAsdaEacFlow::ResumeFromRefresh(double Now)
{
	if (IsOff()) { return; }
	if (State != EAsdaEacState::Idle)
	{
		// ★ R3-01: сессия EAC идёт (или повторяется) — вход в мир её не трогает; сайдкару уйдёт только BIND нового соединения.
		ASDA_EAC_LOG(Log, TEXT("[EAC] вход в мир: сессия EAC сохраняется (%s) — ждём рукопожатия игрового соединения для BIND"),
			AsdaEacStateName(State));
		return;
	}
	if (RefreshToken.IsEmpty() || Host.IsEmpty())
	{
		ASDA_EAC_LOG(Log, TEXT("[EAC] вход в мир: возобновлять нечего (нет refresh-токена — сессия поднимется при вводе пароля)"));
		return;
	}
	ASDA_EAC_LOG(Log, TEXT("[EAC] вход в мир: сессии EAC нет, refresh есть — поднимаем через /refresh"));
	SendHttp(EHttp::Resume, Now);
}

void FAsdaEacFlow::StopForCharacterChange()
{
	if (IsOff()) { return; }
	// ★ R3-01: ни EndSession, ни закрытия 15901, ни Connect_Logout. Прежнее игровое соединение закрыто — его привязка
	//   недействительна; после входа в мир рукопожатие нового соединения даст новый BIND (OnGameChannelBound).
	ASDA_EAC_LOG(Log, TEXT("[EAC] смена персонажа: канал 15901 и сессия EAC сохраняются (%s); привязка прежнего игрового соединения забыта"),
		AsdaEacStateName(State));
	WipeBind();
}

void FAsdaEacFlow::StopFull(const TCHAR* Why)
{
	if (IsOff()) { return; }
	ASDA_EAC_LOG(Log, TEXT("[EAC] полный сброс (%s)"), Why ? Why : TEXT("?"));
	Teardown(/*bKeepRefresh*/false);
	Wipe(Host);
	Wipe(SessionLogin);   // ★ R3-01: следующий ввод пароля — новый вход
	WipeBind();   // ★ ред. 3: игрового соединения больше нет — его привязка недействительна
	RecoverAttempt = 0;
	// Кик/отказ — итог, который видит UI; его не затираем «ожиданием входа» (следующий вход начнётся с StartLogin).
	if (State != EAsdaEacState::Kicked && State != EAsdaEacState::Failed) { SetState(EAsdaEacState::Idle); }
}

void FAsdaEacFlow::Teardown(bool bKeepRefresh)
{
	CloseChannelAndSession();
	if (bConnectLoggedIn)
	{
		B.ConnectLogout();
		bConnectLoggedIn = false;
	}
	++Gen;                         // итоги запросов, ушедших до сброса, отбрасываются
	Http = EHttp::None;
	HttpAttempt = 0;
	HttpRetryAt = 0.0;
	bRefreshLogin = false;
	bRegRetried = false;
	ReconnectAt = 0.0;
	ReconnectAttempt = 0;
	ServerDisabledRetryAt = 0.0;   // ★ ред. 3
	SoftLocId = 0;                 // ★ 16.10: сброс сессии — прежнее мягкое состояние снято (без строки игроку)
	Wipe(Puid);
	Wipe(IdToken);
	Wipe(Login);
	Wipe(PassMd5);
	if (!bKeepRefresh) { Wipe(RefreshToken); }
}

void FAsdaEacFlow::CloseChannelAndSession()
{
	if (bChannelOpen)
	{
		B.ChannelClose();
		bChannelOpen = false;
	}
	if (bSessionBegun)
	{
		B.EndSession();
		bSessionBegun = false;
	}
	OutQueue.Reset();   // сообщения закрытой сессии в новую не идут
	Dec.Reset();
	// ★ ред. 3: новая регистрация — новая EAC-сессия у сайдкара; привязку она получит заново (значение не забываем).
	bBindAcked = false;
	bBindInFlight = false;
	BindRetries = 0;
	BindRetryAt = 0.0;
}

// ── HTTP AsdaId ────────────────────────────────────────────────────────────────────────────────

void FAsdaEacFlow::SendHttp(EHttp What, double Now)
{
	if (Http != What) { HttpAttempt = 0; }
	Http = What;
	++HttpAttempt;
	HttpRetryAt = 0.0;
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	FString Path;
	if (What == EHttp::Token)
	{
		Path = TEXT("/token");
		Obj->SetStringField(TEXT("login"), Login);
		Obj->SetStringField(TEXT("password_md5"), PassMd5);
	}
	else
	{
		Path = TEXT("/refresh");
		Obj->SetStringField(TEXT("refresh_token"), RefreshToken);
	}
	FString Body = AsdaEacJson(Obj);
	if (What != EHttp::Refresh) { SetState(EAsdaEacState::TokenRequest); }
	ASDA_EAC_LOG(Log, TEXT("[EAC] POST %s (попытка %d)"), *Path, HttpAttempt);
	B.HttpPost(Path, Body, Gen);
	Wipe(Body);
}

// ★ R3-02: имя HTTP-запроса для журнала и статуса.
static const TCHAR* AsdaEacHttpName(int32 What)
{
	switch (What)
	{
	case 1:  return TEXT("POST /token");
	case 2:  return TEXT("POST /refresh (подъём входа EOS)");
	case 3:  return TEXT("POST /refresh (продление живой сессии)");
	default: return TEXT("—");
	}
}

void FAsdaEacFlow::ScheduleHttpRetry(int32 Code, const TCHAR* Why, double Now)
{
	const double Delay = RetryDelay(kHttpBaseSec, HttpAttempt);   // 5, 10, 20, 40, 60, 60 … с
	HttpRetryAt = Now + Delay;
	const FString CodeText = Code >= 0 ? FString::Printf(TEXT(" (HTTP %d)"), Code) : FString();   // −1 — сбой не HTTP (EOS)
	LastError = FString::Printf(TEXT("%s%s — повтор %s через %.0f с, попытка %d"),
		Why, *CodeText, AsdaEacHttpName((int32)Http), Delay, HttpAttempt + 1);
	ASDA_EAC_LOG(Warning, TEXT("[EAC] %s; игра продолжается (решение — за сервером)"), *LastError);
	// ★ 16.10: Code −1 — сбой продления входа EOS (не AsdaId) → 991039; иначе сервис входа AsdaId недоступен → 991038.
	SetSoft(Code < 0 ? AsdaEacLoc::kEosFailed : AsdaEacLoc::kAuthUnreachable);
}

void FAsdaEacFlow::HandleHttpFailure(int32 Code, double Now)
{
	const EHttp What = Http;
	// ★ R3-02 (CONTRACT.md §8.6): на логин — ТОЛЬКО 401. Остальное (сеть 0, 429, 5xx/503, 400, прочие коды) — временный
	//   сбой: повтор того же запроса с растущей паузой до 60 с, число попыток не ограничено, игра продолжается.
	if (Code == 401)
	{
		Http = EHttp::None;
		HttpRetryAt = 0.0;
		if (What == EHttp::Token)
		{
			Fail(TEXT("AsdaId отверг вход (HTTP 401, /token)"), AsdaEacLoc::kAuthRejected, EAsdaEacFailKind::Auth);
			return;
		}
		// ★ ред. 3 (§8.4, E1-10/E1-11): 401 на /refresh (продление живой сессии или подъём входа) — refresh недействителен,
		//   продлить вход нечем: игроку «войдите заново» и возврат на логин.
		Wipe(RefreshToken);
		Fail(FString::Printf(TEXT("AsdaId отверг обновление входа (HTTP 401, /refresh, %s)"),
			What == EHttp::Refresh ? TEXT("живая сессия") : TEXT("подъём входа")), AsdaEacLoc::kRefreshRejected, EAsdaEacFailKind::Auth);
		return;
	}
	const TCHAR* Why = Code == 0 ? TEXT("AsdaId: нет ответа (сеть)")
		: Code == 429 ? TEXT("AsdaId: превышен лимит запросов")
		: Code == 400 ? TEXT("AsdaId: запрос отвергнут (400) или адрес сервиса входа не https")
		: Code >= 500 ? TEXT("AsdaId: сервис временно недоступен")
		: TEXT("AsdaId: неожиданный ответ");
	ScheduleHttpRetry(Code, Why, Now);
}

void FAsdaEacFlow::ScheduleRelogin(const FString& Why, double Now)
{
	// Сессия и канал (если были) закрыты; Connect-вход сбрасывается — его поднимет /refresh → Connect_Login.
	CloseChannelAndSession();
	if (bConnectLoggedIn)
	{
		B.ConnectLogout();
		bConnectLoggedIn = false;
	}
	// Gen не меняем: незавершённых EOS-вызовов здесь нет (зовётся из итога Connect/CreateUser, из CopyIdToken и из Tick),
	//   а уже ушедший /refresh отбрасывать нельзя — сервер гасит старый refresh при выдаче нового (ротация).
	bRefreshLogin = false;
	ReconnectAt = 0.0;
	ServerDisabledRetryAt = 0.0;
	if (Http != EHttp::None && HttpRetryAt <= 0.0)
	{
		// /refresh уже в полёте (продление живой сессии) — его итог и поднимет вход: Connect_Login → BeginSession → REG.
		Http = EHttp::Resume;
		LastError = Why + TEXT(" — подъём входа EOS по ответу уже отправленного /refresh");
		ASDA_EAC_LOG(Warning, TEXT("[EAC] %s; игра продолжается"), *LastError);
		SetState(EAsdaEacState::TokenRequest);
		SetSoft(AsdaEacLoc::kEosFailed);   // ★ 16.10: сессия EAC поднимается заново — «повторная попытка…»
		return;
	}
	if (RefreshToken.IsEmpty() || Host.IsEmpty())
	{
		// Поднять вход без пароля нечем (refresh нет). Это не отказ: игра идёт, сессия EAC поднимется при следующем вводе пароля.
		Http = EHttp::None;
		HttpRetryAt = 0.0;
		LastError = Why + TEXT(" — refresh-токена нет: сессия EAC поднимется при следующем вводе пароля");
		ASDA_EAC_LOG(Warning, TEXT("[EAC] %s; игра продолжается"), *LastError);
		Wipe(SessionLogin);
		SetState(EAsdaEacState::Idle);
		SetSoft(0);   // ★ 16.10: повтора нет (ждём ввода пароля) — «повторная попытка…» была бы неправдой
		return;
	}
	++RecoverAttempt;
	const double Delay = RetryDelay(kHttpBaseSec, RecoverAttempt);   // 5, 10, 20, 40, 60, 60 … с
	Http = EHttp::Resume;
	HttpAttempt = 0;
	HttpRetryAt = Now + Delay;
	LastError = FString::Printf(TEXT("%s — подъём входа EOS (/refresh → Connect) через %.0f с, попытка %d"), *Why, Delay, RecoverAttempt);
	ASDA_EAC_LOG(Warning, TEXT("[EAC] %s; игра продолжается (решение — за сервером)"), *LastError);
	SetState(EAsdaEacState::TokenRequest);
	SetSoft(AsdaEacLoc::kEosFailed);   // ★ 16.10: ошибка EOS (Connect / CreateUser / CopyIdToken / REG не собран) → 991039
}

void FAsdaEacFlow::OnHttpResult(uint32 InGen, int32 Code, const FString& Body, double Now)
{
	if (InGen != Gen || Http == EHttp::None || HttpRetryAt > 0.0)
	{
		ASDA_EAC_LOG(Verbose, TEXT("[EAC] устаревший ответ AsdaId (поколение %u, текущее %u) отброшен"), InGen, Gen);
		return;
	}
	if (Code != 200) { HandleHttpFailure(Code, Now); return; }

	FString Access, Refresh;
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<TCHAR>> R = TJsonReaderFactory<TCHAR>::Create(Body);
		if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
		{
			Obj->TryGetStringField(TEXT("access_token"), Access);
			Obj->TryGetStringField(TEXT("refresh_token"), Refresh);
		}
	}
	const EHttp What = Http;
	if (Access.IsEmpty() || Refresh.IsEmpty())
	{
		// ★ R3-02: кривой ответ сервиса входа — не повод выкидывать игрока: повтор того же запроса с паузой.
		Wipe(Access); Wipe(Refresh);
		ScheduleHttpRetry(Code, TEXT("AsdaId: ответ без access_token/refresh_token"), Now);
		return;
	}
	Http = EHttp::None;
	HttpAttempt = 0;
	if (SoftLocId == AsdaEacLoc::kAuthUnreachable) { SetSoft(0); }   // ★ 16.10: сервис входа ответил — «недоступен» снято
	// ★ R3-01: учётка сессии — для сверки повторного ввода пароля после возврата к выбору персонажа.
	if (What == EHttp::Token) { SessionLogin = Login; }
	// Пароль больше не нужен ни в каком виде; старый refresh погашен сервером — держим новый.
	Wipe(PassMd5);
	Wipe(Login);
	Wipe(RefreshToken);
	RefreshToken = MoveTemp(Refresh);
	ASDA_EAC_LOG(Log, TEXT("[EAC] AsdaId: токен получен (JWT %d симв.)"), Access.Len());
	if (What == EHttp::Refresh)
	{
		bRefreshLogin = true;   // сессия идёт своим чередом, Connect_Login лишь продлевает вход
	}
	else
	{
		SetState(EAsdaEacState::ConnectLogin);
	}
	B.ConnectLogin(Access, Gen);
	Wipe(Access);
}

// ── EOS Connect ────────────────────────────────────────────────────────────────────────────────

void FAsdaEacFlow::OnConnectLoginResult(uint32 InGen, EAsdaEacConnectResult Result, const FString& ErrText, double Now)
{
	if (InGen != Gen) { return; }
	if (bRefreshLogin)
	{
		bRefreshLogin = false;
		if (Result != EAsdaEacConnectResult::Success)
		{
			// ★ R3-02: сессия идёт на прежнем Connect-токене; продление повторяем (/refresh → Connect_Login) с паузой.
			++RecoverAttempt;
			Http = EHttp::Refresh;
			HttpAttempt = RecoverAttempt;
			ScheduleHttpRetry(-1, *FString::Printf(TEXT("EOS Connect: продление входа не удалось (%s)"), *ErrText), Now);
			return;
		}
		RecoverAttempt = 0;
		bConnectLoggedIn = true;
		FString P, T;
		if (B.CopyIdToken(P, T))
		{
			if (!Puid.IsEmpty() && P != Puid)
			{
				ASDA_EAC_LOG(Warning, TEXT("[EAC] продление входа выдало другой PUID (%s ≠ %s)"), *FAsdaEacDiag::MaskId(P), *FAsdaEacDiag::MaskId(Puid));
			}
			Puid = P;
			Wipe(IdToken);
			IdToken = MoveTemp(T);   // свежий токен — для следующей REG (переподключение канала)
		}
		ASDA_EAC_LOG(Log, TEXT("[EAC] EOS Connect: вход продлён (refresh)"));
		return;
	}
	if (State != EAsdaEacState::ConnectLogin) { return; }
	switch (Result)
	{
	case EAsdaEacConnectResult::Success:
		bConnectLoggedIn = true;
		StartSessionAndChannel(Now);
		break;
	case EAsdaEacConnectResult::InvalidUser:
		ASDA_EAC_LOG(Log, TEXT("[EAC] EOS Connect: учётки нет (InvalidUser) → CreateUser"));
		SetState(EAsdaEacState::CreateUser);
		B.CreateUser(Gen);
		break;
	default:
		// ★ R3-02: временная ошибка EOS Connect — не выход на логин: через паузу /refresh → Connect_Login заново.
		ScheduleRelogin(FString::Printf(TEXT("EOS_Connect_Login: %s"), *ErrText), Now);
		break;
	}
}

void FAsdaEacFlow::OnCreateUserResult(uint32 InGen, bool bOk, const FString& ErrText, double Now)
{
	if (InGen != Gen || State != EAsdaEacState::CreateUser) { return; }
	if (!bOk) { ScheduleRelogin(FString::Printf(TEXT("EOS_Connect_CreateUser: %s"), *ErrText), Now); return; }   // ★ R3-02
	bConnectLoggedIn = true;
	StartSessionAndChannel(Now);
}

// ── Сессия EAC и канал 15901 ───────────────────────────────────────────────────────────────────

void FAsdaEacFlow::StartSessionAndChannel(double Now)
{
	CloseChannelAndSession();
	FString P, T;
	if (!B.CopyIdToken(P, T))
	{
		// ★ R3-02: Connect-вход негоден (истёк/сброшен) — поднять заново через /refresh, без выхода на логин.
		ScheduleRelogin(TEXT("EOS_Connect_CopyIdToken не выдал токен"), Now);
		return;
	}
	Puid = P;
	Wipe(IdToken);
	IdToken = MoveTemp(T);
	if (!B.BeginSession())
	{
		// ★ R3-02: временный отказ BeginSession — повтор BeginSession + канал + REG с растущей паузой (Connect жив).
		LoseChannel(TEXT("EOS_AntiCheatClient_BeginSession отказал"), Now);
		SetSoft(AsdaEacLoc::kEosFailed);   // ★ 16.10: «не удалось запустить сессию античита» → 991039
		return;
	}
	bSessionBegun = true;
	ASDA_EAC_LOG(Log, TEXT("[EAC] BeginSession(ClientServer) PUID=%s; канал к сайдкару %s:%d"), *FAsdaEacDiag::MaskId(Puid), *Host, Port);
	if (!B.ChannelOpen(Host, Port))
	{
		LoseChannel(TEXT("сайдкар недоступен"), Now);
		return;
	}
	bChannelOpen = true;
	bChannelGotData = false;   // ★ живая проверка 16.10: первая порция данных сайдкара — строкой в журнал
	Dec.Reset();
	TArray<uint8> Reg;
	if (!AsdaEacFrame::BuildReg(Puid, IdToken, AsdaEacWire::kPlatformWindows, Reg))
	{
		// ★ R3-02: негодный IdToken/PUID от EOS — новый Connect-вход (свежий токен) через паузу, без выхода на логин.
		ScheduleRelogin(TEXT("REG не собрался (PUID/IdToken не ASCII или длиннее 4096)"), Now);
		return;
	}
	if (!B.ChannelSend(Reg))
	{
		LoseChannel(TEXT("отправка REG не удалась"), Now);
		return;
	}
	RegSentAt = LastRecvAt = LastPingAt = Now;
	SetState(EAsdaEacState::Registering);
}

void FAsdaEacFlow::LoseChannel(const TCHAR* Why, double Now)
{
	CloseChannelAndSession();
	++ReconnectAttempt;
	const double Delay = RetryDelay(kReconnectBaseSec, ReconnectAttempt);   // ★ R3-02: 2, 4, 8, 16, 32, 60, 60 … с, без потолка числа
	ReconnectAt = Now + Delay;
	LastError = FString::Printf(TEXT("канал 15901: %s — переподключение через %.0f с, попытка %d"), Why, Delay, ReconnectAttempt);
	ASDA_EAC_LOG(Warning, TEXT("[EAC] %s (EndSession; игра продолжается, решение — за сервером)"), *LastError);
	SetState(EAsdaEacState::Reconnecting);
}

void FAsdaEacFlow::OnChannelClosed(double Now)
{
	if (!bChannelOpen) { return; }   // уже закрыт нами
	bChannelOpen = false;            // закрыл сокет сам транспорт — повторно не закрываем
	LoseChannel(TEXT("соединение закрыто"), Now);
}

void FAsdaEacFlow::OnChannelData(const uint8* Data, int32 Len, double Now)
{
	NowCache = Now;
	if (!bChannelOpen || !Data || Len <= 0) { return; }
	if (!bChannelGotData)
	{
		bChannelGotData = true;
		ASDA_EAC_LOG(Log, TEXT("[EAC] канал 15901: первые данные от сайдкара (%d б) — TCP и рукопожатие AsdaSecure прошли"), Len);
	}
	Dec.Feed(Data, Len);
	AsdaEacFrame::FFrame F;
	while (bChannelOpen && Dec.Next(F))
	{
		LastRecvAt = Now;
		HandleFrame(F, Now);
	}
	if (bChannelOpen && Dec.IsError())
	{
		LoseChannel(TEXT("поток сайдкара испорчен (len вне 1..4096)"), Now);
	}
}

void FAsdaEacFlow::HandleFrame(const AsdaEacFrame::FFrame& F, double Now)
{
	switch (F.Type)
	{
	case AsdaEacWire::kRegResult:
	{
		uint8 St = 0xFF;
		if (!AsdaEacFrame::ParseRegResult(F.Payload, St)) { LoseChannel(TEXT("REG_RESULT неверной длины"), Now); return; }
		LastRegStatus = St;
		ASDA_EAC_LOG(Log, TEXT("[EAC] канал 15901: REG_RESULT %u (0 ок, 1 токен, 2 учётка, 3 сайдкар, 4 EAC выкл на сервере), состояние %s"),
			(uint32)St, AsdaEacStateName(State));
		if (State != EAsdaEacState::Registering)
		{
			ASDA_EAC_LOG(Warning, TEXT("[EAC] REG_RESULT %u вне регистрации — пропущен"), (uint32)St);
			return;
		}
		switch (St)
		{
		case AsdaEacWire::kRegOk:
			ReconnectAttempt = 0;
			RecoverAttempt = 0;   // ★ R3-02: цепочка повторов закончилась успехом — следующая начнётся с короткой паузы
			bRegRetried = false;
			LastError.Reset();
			SetSoft(0);   // ★ 16.10: сессия поднялась — мягкое состояние снято (следующий сбой снова покажется)
			ASDA_EAC_LOG(Log, TEXT("[EAC] сайдкар принял регистрацию — сессия активна (в очереди ACMSG: %d, привязка игрового соединения: %s)"),
				OutQueue.Num(), bHaveBind ? TEXT("есть — BIND сейчас") : TEXT("нет — BIND после рукопожатия"));
			SetState(EAsdaEacState::Active);
			FlushQueue(Now);
			// ★ ред. 3 (§8.1): рукопожатие игрового соединения было раньше REG — BIND сразу после REG_RESULT 0.
			if (State == EAsdaEacState::Active && bChannelOpen) { SendBind(Now); }
			break;
		case AsdaEacWire::kRegEacDisabled:
			// ★ ред. 3 (E1-21, §8.4): режим сервера может смениться 0 → 1/2 без перезахода — повтор REG раз в 300 с.
			ServerDisabledRetryAt = Now + kServerDisabledRetrySec;
			ASDA_EAC_LOG(Log, TEXT("[EAC] сайдкар: EAC выключен на сервере (Mode=0) — сессия закрыта, игра идёт; повтор REG через %.0f с"),
				kServerDisabledRetrySec);
			CloseChannelAndSession();
			SetState(EAsdaEacState::ServerDisabled);
			SetSoft(0);   // ★ 16.10: 991034 игроку не показывается (решение владельца) — только журнал выше
			break;
		case AsdaEacWire::kRegBadToken:
			if (!bRegRetried)
			{
				bRegRetried = true;   // один повтор со свежим IdToken (мог истечь между выдачей и регистрацией)
				LoseChannel(TEXT("REG_RESULT 1 (токен не прошёл) — повтор со свежим токеном"), Now);
				SetSoft(AsdaEacLoc::kSoftRegBadToken);   // ★ 16.10: 991031 «не удалось подтвердить вход… повторная попытка»
			}
			else
			{
				// ★ R3-02 (§8.6): REG_RESULT 1 — не повод для выхода на логин (на логин — только 401, REG_RESULT 2, KICK).
				//   Повтор с растущей паузой; Connect-токен продлеваем (/refresh → Connect_Login), чтобы следующая REG
				//   несла свежий IdToken (если сайдкар отверг его как истёкший).
				LoseChannel(TEXT("REG_RESULT 1 (токен не прошёл) повторно — повтор с паузой и продлением входа"), Now);
				if (Http == EHttp::None && !RefreshToken.IsEmpty() && bConnectLoggedIn) { SendHttp(EHttp::Refresh, Now); }
				SetSoft(AsdaEacLoc::kSoftRegBadToken);   // ★ 16.10: то же состояние — вторая строка не пишется
			}
			break;
		case AsdaEacWire::kRegBadAccount:
			Fail(TEXT("сайдкар: учётка не совпала / не найдена (REG_RESULT 2)"), AsdaEacLoc::kRegRejectedBase + AsdaEacWire::kRegBadAccount, EAsdaEacFailKind::Auth);
			break;
		case AsdaEacWire::kRegInternal:
			LoseChannel(TEXT("REG_RESULT 3 (внутренняя ошибка сайдкара)"), Now);
			SetSoft(AsdaEacLoc::kSoftRegInternal);   // ★ 16.10: 991033 «сервер античита временно недоступен… повторная попытка»
			break;
		default:
			LoseChannel(TEXT("REG_RESULT с неизвестным статусом"), Now);
			break;
		}
		break;
	}
	case AsdaEacWire::kAcMsg:
		if (F.Payload.Num() < 1 || F.Payload.Num() > AsdaEacWire::kMaxAcMsg)
		{
			ASDA_EAC_LOG(Warning, TEXT("[EAC] ACMSG длины %d вне 1..512 — пропущен"), F.Payload.Num());
			return;
		}
		if (bSessionBegun) { B.DeliverToAntiCheat(F.Payload.GetData(), F.Payload.Num()); }
		break;
	case AsdaEacWire::kKick:
	{
		FAsdaEacFatal Fatal;
		Fatal.Source = EAsdaEacFatalSource::SidecarKick;
		if (!AsdaEacFrame::ParseKick(F.Payload, Fatal.OurReason, Fatal.EosReason, Fatal.Details))
		{
			ASDA_EAC_LOG(Warning, TEXT("[EAC] KICK неверного формата — считаем киком без причины"));
			Fatal.OurReason = 0; Fatal.EosReason = 0; Fatal.Details.Reset();
		}
		Fatal.LocId = AsdaEacLoc::ForKick(Fatal.OurReason);
		ASDA_EAC_LOG(Warning, TEXT("[EAC] KICK от сайдкара: ourReason=%u eosReason=%u «%s»"),
			(uint32)Fatal.OurReason, (uint32)Fatal.EosReason, *Fatal.Details);
		RaiseFatal(Fatal);
		break;
	}
	case AsdaEacWire::kBindResult:
	{
		// ★ ред. 3 (CONTRACT.md §8.1): 0 — принято; 2 — отклонено (нет аутентифицированной сессии): повтор через 5 с,
		//   не больше kBindRetryMax раз на одно значение (EAC-сессия становится Authenticated не сразу после REG).
		uint8 St = 0xFF;
		if (!AsdaEacFrame::ParseBindResult(F.Payload, St)) { LoseChannel(TEXT("BIND_RESULT неверной длины"), Now); return; }
		bBindInFlight = false;
		if (St == AsdaEacWire::kBindAccepted)
		{
			bBindAcked = true;
			BindRetries = 0;
			ASDA_EAC_LOG(Log, TEXT("[EAC] сайдкар принял привязку игрового соединения (BIND_RESULT 0)"));
		}
		else if (BindRetries < kBindRetryMax)
		{
			++BindRetries;
			BindRetryAt = Now + kBindRetrySec;
			ASDA_EAC_LOG(Warning, TEXT("[EAC] BIND_RESULT %u — повтор BIND через %.0f с (%d из %d)"),
				(uint32)St, kBindRetrySec, BindRetries, kBindRetryMax);
		}
		else
		{
			ASDA_EAC_LOG(Warning, TEXT("[EAC] BIND_RESULT %u — повторы исчерпаны (%d); привязку решает сервер (причина 6)"),
				(uint32)St, kBindRetryMax);
		}
		break;
	}
	case AsdaEacWire::kPong:
		break;   // живость отмечена в OnChannelData
	default:
		ASDA_EAC_LOG(Verbose, TEXT("[EAC] кадр неизвестного типа 0x%02X (%d б) пропущен"), (uint32)F.Type, F.Payload.Num());
		break;
	}
}

void FAsdaEacFlow::RaiseFatal(const FAsdaEacFatal& F)
{
	LastError = F.Source == EAsdaEacFatalSource::SidecarKick
		? FString::Printf(TEXT("кик сайдкара (причина %u)"), (uint32)F.OurReason)
		: FString::Printf(TEXT("нарушение целостности (тип %d)"), F.ViolationType);
	Teardown(/*bKeepRefresh*/false);
	Wipe(Host);
	Wipe(SessionLogin);
	WipeBind();   // ★ ред. 3: игровая сессия рвётся — привязка недействительна
	RecoverAttempt = 0;
	SetState(EAsdaEacState::Kicked);
	if (OnFatal) { OnFatal(F); }
}

void FAsdaEacFlow::OnIntegrityViolated(int32 ViolationType, const FString& Message)
{
	if (IsOff()) { return; }
	FAsdaEacFatal Fatal;
	Fatal.Source = EAsdaEacFatalSource::IntegrityViolation;
	Fatal.ViolationType = ViolationType;
	Fatal.Details = Message;
	Fatal.LocId = AsdaEacLoc::ForViolation(ViolationType);
	ASDA_EAC_LOG(Warning, TEXT("[EAC] нарушение целостности клиента: тип %d «%s»"), ViolationType, *Message);
	RaiseFatal(Fatal);
}

// ── ACMSG ──────────────────────────────────────────────────────────────────────────────────────

void FAsdaEacFlow::OnMessageToServer(const uint8* Data, int32 Len)
{
	if (!bSessionBegun || !Data || Len <= 0) { return; }
	if (Len > AsdaEacWire::kMaxAcMsg)
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] сообщение EAC %d б > 512 — не по контракту, пропущено"), Len);
		return;
	}
	if (State == EAsdaEacState::Active && bChannelOpen)
	{
		SendAcMsg(Data, Len, NowCache);
		return;
	}
	if (OutQueue.Num() >= kQueueMax)
	{
		// Протокол EAC без пропусков: потерять сообщение нельзя — начинаем сессию заново.
		LoseChannel(TEXT("очередь ACMSG до регистрации переполнена"), NowCache);
		return;
	}
	OutQueue.Emplace(Data, Len);
}

void FAsdaEacFlow::SendAcMsg(const uint8* Data, int32 Len, double Now)
{
	TArray<uint8> Frame;
	if (!AsdaEacFrame::BuildAcMsg(Data, Len, Frame)) { return; }
	if (!B.ChannelSend(Frame)) { LoseChannel(TEXT("отправка ACMSG не удалась"), Now); }
}

void FAsdaEacFlow::FlushQueue(double Now)
{
	TArray<TArray<uint8>> Q = MoveTemp(OutQueue);
	OutQueue.Reset();
	for (const TArray<uint8>& M : Q)
	{
		if (State != EAsdaEacState::Active || !bChannelOpen) { break; }
		SendAcMsg(M.GetData(), M.Num(), Now);
	}
}

// ── Обновление токена ──────────────────────────────────────────────────────────────────────────

void FAsdaEacFlow::OnAuthExpiration(double Now)
{
	if (IsOff() || !bConnectLoggedIn) { return; }
	if (RefreshToken.IsEmpty())
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] Connect-токен истекает, а refresh-токена нет — продлить нечем"));
		return;
	}
	if (Http != EHttp::None) { return; }   // запрос уже в полёте
	ASDA_EAC_LOG(Log, TEXT("[EAC] Connect-токен истекает — /refresh и повторный Connect_Login"));
	SendHttp(EHttp::Refresh, Now);
}

// ── Таймеры ────────────────────────────────────────────────────────────────────────────────────

void FAsdaEacFlow::Tick(double Now)
{
	NowCache = Now;
	if (IsOff()) { return; }
	if (Http != EHttp::None && HttpRetryAt > 0.0 && Now >= HttpRetryAt)
	{
		SendHttp(Http, Now);
	}
	if (State == EAsdaEacState::Reconnecting && Now >= ReconnectAt)
	{
		if (!bConnectLoggedIn)
		{
			ScheduleRelogin(TEXT("переподключение без Connect-входа"), Now);   // ★ R3-02: не выход на логин
			return;
		}
		ASDA_EAC_LOG(Log, TEXT("[EAC] повтор: BeginSession + канал 15901 + REG (попытка %d)"), ReconnectAttempt);
		StartSessionAndChannel(Now);
		return;
	}
	// ★ ред. 3 (E1-21): EAC был выключен на сервере (REG_RESULT 4) — раз в 300 с новая регистрация (BeginSession + REG).
	if (State == EAsdaEacState::ServerDisabled && ServerDisabledRetryAt > 0.0 && Now >= ServerDisabledRetryAt)
	{
		ServerDisabledRetryAt = 0.0;
		if (!bConnectLoggedIn)
		{
			ScheduleRelogin(TEXT("повтор REG после REG_RESULT 4 без Connect-входа"), Now);   // ★ R3-02: не выход на логин
			return;
		}
		ASDA_EAC_LOG(Log, TEXT("[EAC] прошло %.0f с после REG_RESULT 4 — повтор регистрации у сайдкара"), kServerDisabledRetrySec);
		ReconnectAttempt = 0;
		bRegRetried = false;
		StartSessionAndChannel(Now);
		return;
	}
	// ★ ред. 3 (§8.1): повтор BIND после BIND_RESULT 2.
	if (State == EAsdaEacState::Active && bChannelOpen && BindRetryAt > 0.0 && Now >= BindRetryAt && !bBindInFlight)
	{
		SendBind(Now);
		if (State != EAsdaEacState::Active) { return; }
	}
	if (bChannelOpen && (State == EAsdaEacState::Registering || State == EAsdaEacState::Active))
	{
		if (State == EAsdaEacState::Registering && Now - RegSentAt > kRegTimeoutSec)
		{
			LoseChannel(TEXT("REG_RESULT не пришёл за 30 с"), Now);
			return;
		}
		if (Now - LastRecvAt > kLivenessSec)
		{
			LoseChannel(TEXT("от сайдкара нет данных 30 с"), Now);
			return;
		}
		if (Now - LastPingAt >= kPingSec)
		{
			LastPingAt = Now;
			TArray<uint8> Ping;
			AsdaEacFrame::BuildPing(Ping);
			if (!B.ChannelSend(Ping)) { LoseChannel(TEXT("отправка PING не удалась"), Now); }
		}
	}
}

// ── ★ R3-02: статус повторов ─────────────────────────────────────────────────────────────────────

double FAsdaEacFlow::GetNextRetryAt() const
{
	if (Http != EHttp::None && HttpRetryAt > 0.0) { return HttpRetryAt; }
	if (State == EAsdaEacState::Reconnecting && ReconnectAt > 0.0) { return ReconnectAt; }
	if (State == EAsdaEacState::ServerDisabled && ServerDisabledRetryAt > 0.0) { return ServerDisabledRetryAt; }
	return 0.0;
}

FString FAsdaEacFlow::DescribeRetry(double Now) const
{
	if (Http != EHttp::None && HttpRetryAt > 0.0)
	{
		return FString::Printf(TEXT("%s через %.0f с (попытка %d)"), AsdaEacHttpName((int32)Http), FMath::Max(0.0, HttpRetryAt - Now), HttpAttempt + 1);
	}
	if (Http != EHttp::None)
	{
		return FString::Printf(TEXT("%s — ждём ответа (попытка %d)"), AsdaEacHttpName((int32)Http), HttpAttempt);
	}
	if (State == EAsdaEacState::Reconnecting && ReconnectAt > 0.0)
	{
		return FString::Printf(TEXT("переподключение 15901 через %.0f с (попытка %d)"), FMath::Max(0.0, ReconnectAt - Now), ReconnectAttempt + 1);
	}
	if (State == EAsdaEacState::ServerDisabled && ServerDisabledRetryAt > 0.0)
	{
		return FString::Printf(TEXT("повтор REG после REG_RESULT 4 через %.0f с"), FMath::Max(0.0, ServerDisabledRetryAt - Now));
	}
	return TEXT("нет");
}
