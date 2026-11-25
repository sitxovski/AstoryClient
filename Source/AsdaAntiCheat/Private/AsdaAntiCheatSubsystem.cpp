// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAntiCheatSubsystem.cpp — клиентский EAC: реальная сторона потока (EOS SDK, HTTP AsdaId, канал 15901) и подсистема.
//   Описание — в AsdaAntiCheatSubsystem.h; машина состояний — AsdaEacFlow.*; отчёт — Reports/anticheat/eac/CLIENT_REPORT.md.
#include "AsdaAntiCheatSubsystem.h"
#include "AsdaAntiCheatSettings.h"
#include "AsdaAntiCheatLog.h"
#include "AsdaEacFlow.h"
#include "AsdaEosSdk.h"
#include "AsdaConnection.h"
#include "AsdaDevConsole.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"

#if WITH_ASDA_EOSSDK
#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_anticheatclient.h"
#endif

// ★ Дев-ключ: разрешить http:// в AuthBaseUrl (локальный стенд AsdaId 127.0.0.1:8444). В Shipping — константа 0:
//   MD5 пароля и токены уходят только по https.
static ASDA_DEV_CVAR(int32, CVarAsdaEacAllowHttp,
	TEXT("asda.Eac.AllowHttp"), 0,
	TEXT("1 — разрешить http:// в AuthBaseUrl (только дев-стенд AsdaId). 0 — только https:// (умолчание; в Shipping всегда)."),
	ECVF_Default);

// Текущее время потока (монотонные секунды).
static double AsdaEacNow() { return FPlatformTime::Seconds(); }

// ★ ред. 3 (E1-08): предел неблокирующего подключения канала 15901 (резолв + connect). Игровой поток его не ждёт:
//   подключение ведёт FAsdaConnection::Tick; не установилось за 1 с — отказ, переподключение по паузе потока (2,4,8,16,30 с).
static constexpr float kAsdaEacSidecarConnectTimeoutSec = 1.f;

// ════════════════════════════════════════════════════════════════════════════════════════════════
// Реальная сторона потока: EOS SDK + HTTP + FAsdaConnection (сырой поток, AsdaSecure роль 4).
// ════════════════════════════════════════════════════════════════════════════════════════════════
class FAsdaEacEosBackend : public IAsdaEacBackend
{
public:
	FAsdaEacFlow* Flow = nullptr;   // поток, которому отдаются итоги (ставит подсистема)
	FString AuthBaseUrl;            // база AsdaId из настроек (без завершающего '/')

	FAsdaEacEosBackend() : Alive(MakeShared<bool>(true)) {}
	virtual ~FAsdaEacEosBackend() override
	{
		*Alive = false;
		for (const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& R : HttpInFlight) { R->OnProcessRequestComplete().Unbind(); R->CancelRequest(); }
		HttpInFlight.Reset();
		if (Channel) { Channel->Disconnect(); Channel.Reset(); }
		ReleasePlatform();
	}

	// Создать платформу EOS и получить интерфейсы. false — EAC недоступен (OutWhy).
	bool CreatePlatform(const UAsdaAntiCheatSettings& S, FString& OutWhy);
	// Снять подписки и освободить платформу.
	void ReleasePlatform();
	// Тик игрового потока: EOS_Platform_Tick, приём канала, отложенные события.
	void Tick(double Now);
	// Есть ли платформа (для дев-команды статуса).
	bool HasPlatform() const;

	// ── IAsdaEacBackend ──
	virtual void HttpPost(const FString& Path, const FString& JsonBody, uint32 Gen) override;
	virtual void ConnectLogin(const FString& AccessToken, uint32 Gen) override;
	virtual void CreateUser(uint32 Gen) override;
	virtual bool CopyIdToken(FString& OutPuid, FString& OutIdToken) override;
	virtual bool BeginSession() override;
	virtual void EndSession() override;
	virtual void ConnectLogout() override;
	virtual bool ChannelOpen(const FString& Host, int32 Port) override;
	virtual void ChannelClose() override;
	virtual bool ChannelSend(const TArray<uint8>& Bytes) override;
	virtual void DeliverToAntiCheat(const uint8* Data, int32 Len) override;

private:
	TSharedRef<bool> Alive;                                            // жив ли объект (для колбэков HTTP)
	TArray<TSharedRef<IHttpRequest, ESPMode::ThreadSafe>> HttpInFlight; // запросы в полёте (отмена при уничтожении)
	// Отложенный итог (HTTP без запроса / EOS без вызова) — отдаётся потоку в Tick, не изнутри его же вызова.
	struct FDeferred { int32 Kind = 0; uint32 Gen = 0; int32 Code = 0; FString Text; };
	TArray<FDeferred> Deferred;

	TUniquePtr<FAsdaConnection> Channel;   // канал 15901 к сайдкару
	bool bInChannelTick = false;           // идёт Channel->Tick (колбэки канала)
	bool bPendingClose = false;            // закрыть канал после выхода из его Tick
	bool bPendingClosedEvent = false;      // транспорт закрыл канал — сообщить потоку в Tick

#if WITH_ASDA_EOSSDK
	EOS_HPlatform Platform = nullptr;                 // платформа EOS (клиент)
	EOS_HConnect Connect = nullptr;                   // интерфейс Connect
	EOS_HAntiCheatClient AntiCheat = nullptr;         // интерфейс AntiCheatClient (nullptr — без bootstrapper)
	EOS_ProductUserId LocalUser = nullptr;            // PUID после Connect_Login/CreateUser
	EOS_ContinuanceToken Continuance = nullptr;       // токен продолжения после EOS_InvalidUser
	EOS_NotificationId NotifyMsgToServer = EOS_INVALID_NOTIFICATIONID;   // AddNotifyMessageToServer
	EOS_NotificationId NotifyIntegrity = EOS_INVALID_NOTIFICATIONID;     // AddNotifyClientIntegrityViolated
	EOS_NotificationId NotifyAuthExp = EOS_INVALID_NOTIFICATIONID;       // Connect AddNotifyAuthExpiration

	// Контекст асинхронного вызова EOS (Connect_Login / CreateUser): владелец + поколение потока.
	struct FEosCtx { FAsdaEacEosBackend* Self; uint32 Gen; };
	static void EOS_CALL OnLoginCb(const EOS_Connect_LoginCallbackInfo* D);
	static void EOS_CALL OnCreateUserCb(const EOS_Connect_CreateUserCallbackInfo* D);
	static void EOS_CALL OnLogoutCb(const EOS_Connect_LogoutCallbackInfo* D);
	static void EOS_CALL OnAuthExpirationCb(const EOS_Connect_AuthExpirationCallbackInfo* D);
	static void EOS_CALL OnMessageToServerCb(const EOS_AntiCheatClient_OnMessageToServerCallbackInfo* D);
	static void EOS_CALL OnIntegrityViolatedCb(const EOS_AntiCheatClient_OnClientIntegrityViolatedCallbackInfo* D);
#endif
};

// Отложенные итоги (Kind).
namespace AsdaEacDeferred
{
	constexpr int32 kHttp = 1;         // HTTP-запрос не отправлен (адрес/схема) — итог с кодом Code
	constexpr int32 kConnect = 2;      // Connect_Login не вызван — итог Error
	constexpr int32 kCreateUser = 3;   // CreateUser не вызван — итог «нет»
}

#if WITH_ASDA_EOSSDK
// Создать платформу EOS по настройкам (общая для подсистемы и дев-пробы). nullptr — причина в OutWhy.
//   ProbeSecret — только дев-проба (asda.Eac.Probe dummysecret): заглушка вместо пустого ClientSecret, чтобы EOS создал
//   платформу локально и можно было спросить интерфейс AntiCheatClient. Платформа пробы не тикается — запросов к Epic нет.
static EOS_HPlatform AsdaEacCreatePlatform(const UAsdaAntiCheatSettings& S, FString& OutWhy, const TCHAR* ProbeSecret = nullptr)
{
	if (S.ProductId.IsEmpty() || S.SandboxId.IsEmpty() || S.DeploymentId.IsEmpty() || S.ClientId.IsEmpty())
	{
		OutWhy = TEXT("в DefaultGame.ini не заданы ProductId/SandboxId/DeploymentId/ClientId");
		return nullptr;
	}
	const FString CacheDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EOS")));
	IFileManager::Get().MakeDirectory(*CacheDir, /*Tree*/true);
	const FTCHARToUTF8 Product(*S.ProductId);     // строки живут до конца вызова EOS_Platform_Create (SDK копирует)
	const FTCHARToUTF8 Sandbox(*S.SandboxId);
	const FTCHARToUTF8 Deployment(*S.DeploymentId);
	const FTCHARToUTF8 Client(*S.ClientId);
	const bool bProbeSecret = S.ClientSecret.IsEmpty() && ProbeSecret != nullptr;
	const FTCHARToUTF8 Secret(bProbeSecret ? ProbeSecret : *S.ClientSecret);
	const FTCHARToUTF8 Cache(*CacheDir);

	EOS_Platform_Options O = {};
	O.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	O.ProductId = Product.Get();
	O.SandboxId = Sandbox.Get();
	O.DeploymentId = Deployment.Get();
	O.ClientCredentials.ClientId = Client.Get();
	O.ClientCredentials.ClientSecret = (S.ClientSecret.IsEmpty() && !bProbeSecret) ? nullptr : Secret.Get();
	O.bIsServer = EOS_FALSE;
	O.Flags = EOS_PF_DISABLE_OVERLAY;
	O.CacheDirectory = Cache.Get();
	O.TickBudgetInMilliseconds = 0;
	// ★ Живая проверка 16.10: параметры платформы в журнал — без значения секрета (только «задан/пуст»).
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Platform_Create: ProductId=%s SandboxId=%s DeploymentId=%s ClientId=%s ClientSecret=%s%s, кэш «%s», оверлей выкл"),
		*S.ProductId, *S.SandboxId, *S.DeploymentId, *FAsdaEacDiag::MaskId(S.ClientId),
		S.ClientSecret.IsEmpty() ? TEXT("ПУСТ") : TEXT("задан (длина > 0)"), bProbeSecret ? TEXT(" (заглушка пробы)") : TEXT(""), *CacheDir);
	EOS_HPlatform P = EOS_Platform_Create(&O);
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Platform_Create → %s"), P ? TEXT("платформа создана") : TEXT("nullptr (причина — строки [EOS …] выше)"));
	if (!P)
	{
		// EOS 1.19.2.1 без секрета платформу не создаёт: «ClientCredentials.ClientSecret cannot be null» (лог SDK).
		OutWhy = S.ClientSecret.IsEmpty() && !bProbeSecret
			? TEXT("ClientSecret в DefaultGame.ini пуст — EOS не создаёт платформу без секрета (его вписывает координатор)")
			: TEXT("EOS_Platform_Create вернул nullptr (см. строки [EOS …] выше)");
		return nullptr;
	}
	return P;
}
#endif

bool FAsdaEacEosBackend::CreatePlatform(const UAsdaAntiCheatSettings& S, FString& OutWhy)
{
	AuthBaseUrl = S.AuthBaseUrl.TrimStartAndEnd().TrimQuotes();
	while (AuthBaseUrl.EndsWith(TEXT("/"))) { AuthBaseUrl.LeftChopInline(1); }
	// ★ Живая проверка 16.10: ini-парсер UE режет значение на «//» (AuthBaseUrl=https://<домен> → «https:»), запрос
	//   /token не уходил ни разу. В ini значение теперь в кавычках; здесь — страховка: адрес без хоста → адрес по умолчанию.
	if (AuthBaseUrl.Len() <= 8 || !AuthBaseUrl.Contains(TEXT("://")))
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] AuthBaseUrl из настроек «%s» без хоста — беру адрес по умолчанию https://@@ASTORY_AUTH_HOST@@"), *AuthBaseUrl);
		AuthBaseUrl = TEXT("https://@@ASTORY_AUTH_HOST@@");
	}
#if WITH_ASDA_EOSSDK
	Platform = AsdaEacCreatePlatform(S, OutWhy);
	if (!Platform) { return false; }
	Connect = EOS_Platform_GetConnectInterface(Platform);
	AntiCheat = EOS_Platform_GetAntiCheatClientInterface(Platform);
	ASDA_EAC_LOG(Log, TEXT("[EAC] интерфейсы EOS: Connect=%s, AntiCheatClient=%s; AuthBaseUrl=%s"),
		Connect ? TEXT("есть") : TEXT("НЕТ"), AntiCheat ? TEXT("есть") : TEXT("НЕТ (не через Astory.exe — бутстрапер EAC?)"), *AuthBaseUrl);
	if (!AntiCheat)
	{
		OutWhy = TEXT("интерфейс AntiCheatClient = nullptr — игра запущена не через Astory.exe (бутстрапер EAC)");
		ReleasePlatform();
		return false;
	}
	if (!Connect)
	{
		OutWhy = TEXT("интерфейс Connect = nullptr");
		ReleasePlatform();
		return false;
	}
	{
		EOS_AntiCheatClient_AddNotifyMessageToServerOptions O = {};
		O.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYMESSAGETOSERVER_API_LATEST;
		NotifyMsgToServer = EOS_AntiCheatClient_AddNotifyMessageToServer(AntiCheat, &O, this, &OnMessageToServerCb);
	}
	{
		EOS_AntiCheatClient_AddNotifyClientIntegrityViolatedOptions O = {};
		O.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYCLIENTINTEGRITYVIOLATED_API_LATEST;
		NotifyIntegrity = EOS_AntiCheatClient_AddNotifyClientIntegrityViolated(AntiCheat, &O, this, &OnIntegrityViolatedCb);
	}
	{
		EOS_Connect_AddNotifyAuthExpirationOptions O = {};
		O.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
		NotifyAuthExp = EOS_Connect_AddNotifyAuthExpiration(Connect, &O, this, &OnAuthExpirationCb);
	}
	if (NotifyMsgToServer == EOS_INVALID_NOTIFICATIONID || NotifyIntegrity == EOS_INVALID_NOTIFICATIONID || NotifyAuthExp == EOS_INVALID_NOTIFICATIONID)
	{
		OutWhy = TEXT("подписка на события EOS (MessageToServer/IntegrityViolated/AuthExpiration) не удалась");
		ReleasePlatform();
		return false;
	}
	ASDA_EAC_LOG(Log, TEXT("[EAC] платформа EOS создана (оверлей выключен), интерфейс AntiCheatClient получен"));
	return true;
#else
	OutWhy = TEXT("сборка без EOS SDK");
	return false;
#endif
}

void FAsdaEacEosBackend::ReleasePlatform()
{
#if WITH_ASDA_EOSSDK
	if (AntiCheat && NotifyMsgToServer != EOS_INVALID_NOTIFICATIONID) { EOS_AntiCheatClient_RemoveNotifyMessageToServer(AntiCheat, NotifyMsgToServer); }
	if (AntiCheat && NotifyIntegrity != EOS_INVALID_NOTIFICATIONID) { EOS_AntiCheatClient_RemoveNotifyClientIntegrityViolated(AntiCheat, NotifyIntegrity); }
	if (Connect && NotifyAuthExp != EOS_INVALID_NOTIFICATIONID) { EOS_Connect_RemoveNotifyAuthExpiration(Connect, NotifyAuthExp); }
	NotifyMsgToServer = NotifyIntegrity = NotifyAuthExp = EOS_INVALID_NOTIFICATIONID;
	if (Platform) { EOS_Platform_Release(Platform); }
	Platform = nullptr;
	Connect = nullptr;
	AntiCheat = nullptr;
	LocalUser = nullptr;
	Continuance = nullptr;
#endif
}

bool FAsdaEacEosBackend::HasPlatform() const
{
#if WITH_ASDA_EOSSDK
	return Platform != nullptr;
#else
	return false;
#endif
}

void FAsdaEacEosBackend::Tick(double Now)
{
#if WITH_ASDA_EOSSDK
	if (Platform) { EOS_Platform_Tick(Platform); }   // колбэки EOS — только отсюда (игровой поток)
#endif
	if (Channel)
	{
		bInChannelTick = true;
		Channel->Tick();                               // приём → Flow->OnChannelData (сырой поток)
		bInChannelTick = false;
		if (bPendingClose)
		{
			bPendingClose = false;
			bPendingClosedEvent = false;
			Channel->Disconnect();                     // закрытие, запрошенное из колбэка канала, — уже вне его Tick
		}
	}
	if (bPendingClosedEvent)
	{
		bPendingClosedEvent = false;
		if (Flow) { Flow->OnChannelClosed(Now); }
	}
	if (Deferred.Num() > 0 && Flow)
	{
		TArray<FDeferred> Q = MoveTemp(Deferred);
		Deferred.Reset();
		for (const FDeferred& D : Q)
		{
			if (D.Kind == AsdaEacDeferred::kHttp)            { Flow->OnHttpResult(D.Gen, D.Code, FString(), Now); }
			else if (D.Kind == AsdaEacDeferred::kConnect)    { Flow->OnConnectLoginResult(D.Gen, EAsdaEacConnectResult::Error, D.Text, Now); }
			else if (D.Kind == AsdaEacDeferred::kCreateUser) { Flow->OnCreateUserResult(D.Gen, false, D.Text, Now); }
		}
	}
}

// ── HTTP AsdaId ──────────────────────────────────────────────────────────────────────────────────

void FAsdaEacEosBackend::HttpPost(const FString& Path, const FString& JsonBody, uint32 Gen)
{
	const bool bHttps = AuthBaseUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	const bool bHttp = AuthBaseUrl.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase);
	if (!bHttps && !(bHttp && CVarAsdaEacAllowHttp.GetValueOnGameThread() != 0))
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] AuthBaseUrl «%s» не https:// — запрос не отправлен (http — только дев-сборка с дев-ключом; имя ключа в Shipping не попадает)"), *AuthBaseUrl);
		FDeferred D; D.Kind = AsdaEacDeferred::kHttp; D.Gen = Gen; D.Code = 400;   // как «кривой запрос» — без повторов
		Deferred.Add(MoveTemp(D));
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	// ★ Живая проверка 16.10: URL и прокси, который возьмёт libcurl UE (ключ -httpproxy=, [HTTP] HttpProxyAddress или
	//   системный прокси WinHTTP/IE — FWindowsPlatformHttp::GetOperatingSystemProxyAddress). Тело (MD5/refresh) не пишем.
	{
		const FString Proxy = FHttpModule::Get().GetProxyAddress();
		ASDA_EAC_LOG(Log, TEXT("[EAC] HTTP → POST %s%s (тело %d симв., тайм-аут 15 с, прокси: %s)"),
			*AuthBaseUrl, *Path, JsonBody.Len(), Proxy.IsEmpty() ? TEXT("нет — напрямую") : *Proxy);
	}
	Req->SetURL(AuthBaseUrl + Path);
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->SetContentAsString(JsonBody);
	Req->SetTimeout(15.f);
	TWeakPtr<bool> AliveWeak = Alive;
	Req->OnProcessRequestComplete().BindLambda([this, AliveWeak, Gen](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected)
	{
		const TSharedPtr<bool> A = AliveWeak.Pin();
		if (!A.IsValid() || !*A) { return; }
		HttpInFlight.RemoveAll([&Request](const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& R) { return Request.Get() == &R.Get(); });
		const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
		const FString Body = Response.IsValid() ? Response->GetContentAsString() : FString();
		// ★ Живая проверка 16.10: при сбое — статус и причина UE (ConnectionError / TimedOut / Cancelled / Other), время запроса.
		ASDA_EAC_LOG(Log, TEXT("[EAC] HTTP ← %s → код %d (%d симв.); соединение=%s, статус=%s, причина сбоя=%s, %.2f с"),
			Request.IsValid() ? *Request->GetURL() : TEXT("?"), Code, Body.Len(), bConnected ? TEXT("да") : TEXT("НЕТ"),
			Request.IsValid() ? EHttpRequestStatus::ToString(Request->GetStatus()) : TEXT("?"),
			Request.IsValid() ? LexToString(Request->GetFailureReason()) : TEXT("?"),
			Request.IsValid() ? Request->GetElapsedTime() : -1.f);
		if (Flow) { Flow->OnHttpResult(Gen, Code, Body, AsdaEacNow()); }
	});
	HttpInFlight.Add(Req);
	Req->ProcessRequest();
}

// ── EOS Connect / AntiCheatClient ────────────────────────────────────────────────────────────────

void FAsdaEacEosBackend::ConnectLogin(const FString& AccessToken, uint32 Gen)
{
#if WITH_ASDA_EOSSDK
	if (Connect)
	{
		const FTCHARToUTF8 Tok(*AccessToken);
		EOS_Connect_Credentials C = {};
		C.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		C.Token = Tok.Get();
		C.Type = EOS_EExternalCredentialType::EOS_ECT_OPENID_ACCESS_TOKEN;
		EOS_Connect_LoginOptions O = {};
		O.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		O.Credentials = &C;
		O.UserLoginInfo = nullptr;   // для OpenID не нужен (eos_connect_types.h: только Amazon/Apple/Google/…/DeviceID)
		ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Connect_Login(OPENID_ACCESS_TOKEN, JWT %d симв.) — вызван"), AccessToken.Len());
		EOS_Connect_Login(Connect, &O, new FEosCtx{ this, Gen }, &OnLoginCb);
		return;
	}
#endif
	FDeferred D; D.Kind = AsdaEacDeferred::kConnect; D.Gen = Gen; D.Text = TEXT("нет интерфейса Connect");
	Deferred.Add(MoveTemp(D));
}

void FAsdaEacEosBackend::CreateUser(uint32 Gen)
{
#if WITH_ASDA_EOSSDK
	if (Connect && Continuance)
	{
		EOS_Connect_CreateUserOptions O = {};
		O.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
		O.ContinuanceToken = Continuance;
		ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Connect_CreateUser — вызван"));
		EOS_Connect_CreateUser(Connect, &O, new FEosCtx{ this, Gen }, &OnCreateUserCb);
		return;
	}
#endif
	FDeferred D; D.Kind = AsdaEacDeferred::kCreateUser; D.Gen = Gen; D.Text = TEXT("нет ContinuanceToken");
	Deferred.Add(MoveTemp(D));
}

bool FAsdaEacEosBackend::CopyIdToken(FString& OutPuid, FString& OutIdToken)
{
#if WITH_ASDA_EOSSDK
	if (!Connect || !LocalUser) { return false; }
	EOS_Connect_CopyIdTokenOptions O = {};
	O.ApiVersion = EOS_CONNECT_COPYIDTOKEN_API_LATEST;
	O.LocalUserId = LocalUser;
	EOS_Connect_IdToken* Tok = nullptr;
	const EOS_EResult R = EOS_Connect_CopyIdToken(Connect, &O, &Tok);
	if (R != EOS_EResult::EOS_Success || !Tok || !Tok->JsonWebToken)
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] EOS_Connect_CopyIdToken: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(R)));
		if (Tok) { EOS_Connect_IdToken_Release(Tok); }
		return false;
	}
	OutIdToken = UTF8_TO_TCHAR(Tok->JsonWebToken);
	EOS_Connect_IdToken_Release(Tok);
	char Buf[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
	int32_t Len = (int32_t)sizeof(Buf);
	if (EOS_ProductUserId_ToString(LocalUser, Buf, &Len) != EOS_EResult::EOS_Success) { return false; }
	OutPuid = UTF8_TO_TCHAR(Buf);
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Connect_CopyIdToken: EOS_Success (PUID %s, IdToken %d симв.)"), *FAsdaEacDiag::MaskId(OutPuid), OutIdToken.Len());
	return true;
#else
	return false;
#endif
}

bool FAsdaEacEosBackend::BeginSession()
{
#if WITH_ASDA_EOSSDK
	if (!AntiCheat || !LocalUser)
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] BeginSession не вызван: AntiCheatClient=%s, PUID=%s"), AntiCheat ? TEXT("есть") : TEXT("НЕТ"), LocalUser ? TEXT("есть") : TEXT("НЕТ"));
		return false;
	}
	EOS_AntiCheatClient_BeginSessionOptions O = {};
	O.ApiVersion = EOS_ANTICHEATCLIENT_BEGINSESSION_API_LATEST;
	O.LocalUserId = LocalUser;
	O.Mode = EOS_EAntiCheatClientMode::EOS_ACCM_ClientServer;
	const EOS_EResult R = EOS_AntiCheatClient_BeginSession(AntiCheat, &O);
	if (R != EOS_EResult::EOS_Success)
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] EOS_AntiCheatClient_BeginSession: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(R)));
		return false;
	}
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_AntiCheatClient_BeginSession(ClientServer): EOS_Success"));
	return true;
#else
	return false;
#endif
}

void FAsdaEacEosBackend::EndSession()
{
#if WITH_ASDA_EOSSDK
	if (!AntiCheat) { return; }
	EOS_AntiCheatClient_EndSessionOptions O = {};
	O.ApiVersion = EOS_ANTICHEATCLIENT_ENDSESSION_API_LATEST;
	const EOS_EResult R = EOS_AntiCheatClient_EndSession(AntiCheat, &O);
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_AntiCheatClient_EndSession: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(R)));
#endif
}

void FAsdaEacEosBackend::ConnectLogout()
{
#if WITH_ASDA_EOSSDK
	if (Connect && LocalUser)
	{
		EOS_Connect_LogoutOptions O = {};
		O.ApiVersion = EOS_CONNECT_LOGOUT_API_LATEST;
		O.LocalUserId = LocalUser;
		EOS_Connect_Logout(Connect, &O, nullptr, &OnLogoutCb);
	}
	LocalUser = nullptr;
	Continuance = nullptr;
#endif
}

void FAsdaEacEosBackend::DeliverToAntiCheat(const uint8* Data, int32 Len)
{
#if WITH_ASDA_EOSSDK
	if (!AntiCheat || !Data || Len <= 0) { return; }
	EOS_AntiCheatClient_ReceiveMessageFromServerOptions O = {};
	O.ApiVersion = EOS_ANTICHEATCLIENT_RECEIVEMESSAGEFROMSERVER_API_LATEST;
	O.DataLengthBytes = (uint32_t)Len;
	O.Data = Data;
	const EOS_EResult R = EOS_AntiCheatClient_ReceiveMessageFromServer(AntiCheat, &O);
	if (R != EOS_EResult::EOS_Success)
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] ReceiveMessageFromServer (%d б): %s"), Len, UTF8_TO_TCHAR(EOS_EResult_ToString(R)));
	}
#endif
}

#if WITH_ASDA_EOSSDK
void EOS_CALL FAsdaEacEosBackend::OnLoginCb(const EOS_Connect_LoginCallbackInfo* D)
{
	if (!D) { return; }
	if (!EOS_EResult_IsOperationComplete(D->ResultCode)) { return; }   // SDK повторит сам и позовёт ещё раз
	FEosCtx* Ctx = static_cast<FEosCtx*>(D->ClientData);
	FAsdaEacEosBackend* Self = Ctx->Self;
	const uint32 Gen = Ctx->Gen;
	delete Ctx;
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Connect_Login → %s (ContinuanceToken %s)"), UTF8_TO_TCHAR(EOS_EResult_ToString(D->ResultCode)),
		D->ContinuanceToken ? TEXT("есть") : TEXT("нет"));
	if (!Self->Flow) { return; }
	if (D->ResultCode == EOS_EResult::EOS_Success)
	{
		Self->LocalUser = D->LocalUserId;
		Self->Continuance = nullptr;
		Self->Flow->OnConnectLoginResult(Gen, EAsdaEacConnectResult::Success, FString(), AsdaEacNow());
	}
	else if (D->ResultCode == EOS_EResult::EOS_InvalidUser && D->ContinuanceToken)
	{
		Self->Continuance = D->ContinuanceToken;
		Self->Flow->OnConnectLoginResult(Gen, EAsdaEacConnectResult::InvalidUser, FString(), AsdaEacNow());
	}
	else
	{
		Self->Flow->OnConnectLoginResult(Gen, EAsdaEacConnectResult::Error, UTF8_TO_TCHAR(EOS_EResult_ToString(D->ResultCode)), AsdaEacNow());
	}
}

void EOS_CALL FAsdaEacEosBackend::OnCreateUserCb(const EOS_Connect_CreateUserCallbackInfo* D)
{
	if (!D) { return; }
	if (!EOS_EResult_IsOperationComplete(D->ResultCode)) { return; }
	FEosCtx* Ctx = static_cast<FEosCtx*>(D->ClientData);
	FAsdaEacEosBackend* Self = Ctx->Self;
	const uint32 Gen = Ctx->Gen;
	delete Ctx;
	if (!Self->Flow) { return; }
	const bool bOk = D->ResultCode == EOS_EResult::EOS_Success;
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Connect_CreateUser → %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(D->ResultCode)));
	if (bOk) { Self->LocalUser = D->LocalUserId; }
	Self->Continuance = nullptr;
	Self->Flow->OnCreateUserResult(Gen, bOk, UTF8_TO_TCHAR(EOS_EResult_ToString(D->ResultCode)), AsdaEacNow());
}

void EOS_CALL FAsdaEacEosBackend::OnLogoutCb(const EOS_Connect_LogoutCallbackInfo* D)
{
	if (!D || !EOS_EResult_IsOperationComplete(D->ResultCode)) { return; }
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS_Connect_Logout: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(D->ResultCode)));
}

void EOS_CALL FAsdaEacEosBackend::OnAuthExpirationCb(const EOS_Connect_AuthExpirationCallbackInfo* D)
{
	FAsdaEacEosBackend* Self = D ? static_cast<FAsdaEacEosBackend*>(D->ClientData) : nullptr;
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS Connect: уведомление AuthExpiration (свой пользователь: %s)"),
		(Self && D->LocalUserId == Self->LocalUser) ? TEXT("да") : TEXT("нет"));
	if (Self && Self->Flow && D->LocalUserId == Self->LocalUser) { Self->Flow->OnAuthExpiration(AsdaEacNow()); }
}

void EOS_CALL FAsdaEacEosBackend::OnMessageToServerCb(const EOS_AntiCheatClient_OnMessageToServerCallbackInfo* D)
{
	FAsdaEacEosBackend* Self = D ? static_cast<FAsdaEacEosBackend*>(D->ClientData) : nullptr;
	if (Self && Self->Flow && D->MessageData && D->MessageDataSizeBytes > 0)
	{
		Self->Flow->OnMessageToServer(static_cast<const uint8*>(D->MessageData), (int32)D->MessageDataSizeBytes);
	}
}

void EOS_CALL FAsdaEacEosBackend::OnIntegrityViolatedCb(const EOS_AntiCheatClient_OnClientIntegrityViolatedCallbackInfo* D)
{
	FAsdaEacEosBackend* Self = D ? static_cast<FAsdaEacEosBackend*>(D->ClientData) : nullptr;
	if (Self && Self->Flow)
	{
		Self->Flow->OnIntegrityViolated((int32)D->ViolationType, UTF8_TO_TCHAR(D->ViolationMessage ? D->ViolationMessage : ""));
	}
}
#endif // WITH_ASDA_EOSSDK

// ── Канал 15901 ──────────────────────────────────────────────────────────────────────────────────

bool FAsdaEacEosBackend::ChannelOpen(const FString& Host, int32 Port)
{
	if (!Channel)
	{
		Channel = MakeUnique<FAsdaConnection>();
		Channel->SetRawStreamMode([this](const uint8* Data, int32 Len) { if (Flow) { Flow->OnChannelData(Data, Len, AsdaEacNow()); } });
		Channel->SetOnClosed([this]()
		{
			ASDA_EAC_LOG(Warning, TEXT("[EAC] канал 15901: транспорт закрыл соединение (отказ connect / тайм-аут 1 с / обрыв / рукопожатие AsdaSecure)"));
			bPendingClosedEvent = true;   // потоку — в Tick (без реэнтрантности)
		});
	}
	bPendingClose = false;
	bPendingClosedEvent = false;
	// ★ ред. 3 (E1-08): НЕблокирующее подключение — вызов не ждёт SYN; REG, отправленный до установления, уходит сразу после
	//   ClientHello; отказ/таймаут 1 с приходят через OnClosed (→ Flow->OnChannelClosed в Tick) как обрыв.
	const bool bOk = Channel->ConnectAsync(Host, Port, /*SecureRole=*/4, kAsdaEacSidecarConnectTimeoutSec);   // AsdaSecure, SubServer::Eac = 4
	ASDA_EAC_LOG(Log, TEXT("[EAC] канал к сайдкару %s:%d → %s"), *Host, Port,
		bOk ? TEXT("подключение начато (неблокирующее, предел 1 с), затем рукопожатие AsdaSecure") : TEXT("не подключён"));
	return bOk;
}

void FAsdaEacEosBackend::ChannelClose()
{
	bPendingClosedEvent = false;
	if (!Channel) { return; }
	if (bInChannelTick) { bPendingClose = true; return; }   // из колбэка канала — после выхода из его Tick
	Channel->Disconnect();
}

bool FAsdaEacEosBackend::ChannelSend(const TArray<uint8>& Bytes)
{
	const bool bOk = Channel && Channel->SendRaw(Bytes.GetData(), Bytes.Num());
	// ★ Живая проверка 16.10: тип кадра (байт 2 — после u16 len) и размер — без содержимого (в REG — IdToken).
	//   REG и BIND пишем всегда; ACMSG и PING (частые) — только при отказе отправки, чтобы ротация не вытесняла начало сессии.
	const uint8 FrameType = Bytes.Num() > 2 ? Bytes[2] : 0;   // тип кадра §3
	if (!bOk || FrameType == AsdaEacWire::kReg || FrameType == AsdaEacWire::kBind)
	{
		ASDA_EAC_LOG(Log, TEXT("[EAC] канал 15901: отправка кадра 0x%02X%s (%d б) → %s"), (uint32)FrameType,
			FrameType == AsdaEacWire::kReg ? TEXT(" REG") : FrameType == AsdaEacWire::kBind ? TEXT(" BIND") : TEXT(""),
			Bytes.Num(), bOk ? TEXT("OK (в очередь канала)") : TEXT("ОТКАЗ"));
	}
	return bOk;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
// Подсистема
// ════════════════════════════════════════════════════════════════════════════════════════════════

UAsdaAntiCheatSubsystem::UAsdaAntiCheatSubsystem() = default;
UAsdaAntiCheatSubsystem::~UAsdaAntiCheatSubsystem() = default;

#if ASDA_DEV_CONSOLE && WITH_ASDA_EOSSDK
// Дев-проба: поднять SDK и временную платформу, сообщить, есть ли интерфейс AntiCheatClient; ничего не подключает.
static void AsdaEacProbe(const TArray<FString>& Args)
{
	const bool bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
	const FString NullEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("EOS_USE_ANTICHEATCLIENTNULL"));
	FString Why;
	FString Verdict;
	if (!FAsdaEosSdk::Get().EnsureInitialized(Why))
	{
		Verdict = FString::Printf(TEXT("SDK недоступен — %s"), *Why);
	}
	else
	{
		const bool bDummy = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("dummysecret"), ESearchCase::IgnoreCase); });
		EOS_HPlatform P = AsdaEacCreatePlatform(*GetDefault<UAsdaAntiCheatSettings>(), Why, bDummy ? TEXT("asda-eac-probe-placeholder") : nullptr);
		if (!P)
		{
			Verdict = FString::Printf(TEXT("SDK %s есть, платформа не создана — %s"), *FAsdaEosSdk::Get().GetVersion(), *Why);
		}
		else
		{
			const bool bAc = EOS_Platform_GetAntiCheatClientInterface(P) != nullptr;
			Verdict = bAc
				? FString::Printf(TEXT("SDK %s, платформа создана%s, интерфейс AntiCheatClient ЕСТЬ"), *FAsdaEosSdk::Get().GetVersion(), bDummy ? TEXT(" (секрет-заглушка пробы)") : TEXT(""))
				: FString::Printf(TEXT("SDK %s, платформа создана%s, интерфейса AntiCheatClient НЕТ → статус «EAC недоступен» (запуск без bootstrapper)"),
					*FAsdaEosSdk::Get().GetVersion(), bDummy ? TEXT(" (секрет-заглушка пробы)") : TEXT(""));
			EOS_Platform_Release(P);
		}
	}
	ASDA_EAC_LOG(Log, TEXT("EAC-ПРОБА ИТОГ: %s; EOS_USE_ANTICHEATCLIENTNULL=«%s»; -AsdaNoNet=%d -AsdaEac=%d"),
		*Verdict, *NullEnv, FAsdaConnection::IsNoNetRun() ? 1 : 0, FParse::Param(FCommandLine::Get(), TEXT("AsdaEac")) ? 1 : 0);
	if (bQuit && GEngine) { GEngine->Exec(nullptr, TEXT("quit")); }
}
#endif

static bool AsdaEacNullClientAllowed(FString& OutWhy);   // проверка null-клиента EAC (определение ниже, у TickEac)

void UAsdaAntiCheatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Backend = MakeShared<FAsdaEacEosBackend>();
	Flow = MakeShared<FAsdaEacFlow>(*Backend);
	Backend->Flow = Flow.Get();
	Flow->OnState = [this](EAsdaEacState S) { OnStateChanged.Broadcast(S); };
	Flow->OnFatal = [this](const FAsdaEacFatal& F) { OnFatal.Broadcast(F); };
	Flow->OnSoftNotice = [this](int32 LocId) { OnSoftNotice.Broadcast(LocId); };   // ★ 16.10: мягкое состояние → чат

	const bool bNoNet = FAsdaConnection::IsNoNetRun();
	const bool bEacFlag = FParse::Param(FCommandLine::Get(), TEXT("AsdaEac"));
	bEnabledByPolicy = FAsdaEacFlow::ComputeEnabled(UE_BUILD_SHIPPING != 0, bNoNet, bEacFlag);
	// ★ Живая проверка 16.10: решение политики — первой строкой сессии журнала AsdaEac.log (Shipping: UE_LOG вырезан).
	ASDA_EAC_LOG(Log, TEXT("[EAC] политика: ComputeEnabled(Shipping=%d, -AsdaNoNet=%d, -AsdaEac=%d) → %s; журнал %s; ProjectSavedDir «%s»"),
		UE_BUILD_SHIPPING != 0 ? 1 : 0, bNoNet ? 1 : 0, bEacFlag ? 1 : 0, bEnabledByPolicy ? TEXT("ВКЛ") : TEXT("выкл"),
		*FAsdaEacDiag::GetFilePath(), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
	FString Why;
	bool bAvailable = false;
	if (!bEnabledByPolicy)
	{
		Why = bNoNet ? TEXT("ключ -AsdaNoNet (прогон без сети)") : TEXT("сборка Development без ключа -AsdaEac");
		ASDA_EAC_LOG(Log, TEXT("[EAC] выключен: %s"), *Why);
	}
	// ★ Аудит 16.10: переменная окружения EOS_USE_ANTICHEATCLIENTNULL подставляет «пустой» клиент EAC (интерфейс не nullptr,
	//   защиты нет). В Shipping это запрещено — уходим в ветку «EAC недоступен» (сообщение и выход), как без bootstrapper.
	else if (AsdaEacNullClientAllowed(Why) && FAsdaEosSdk::Get().EnsureInitialized(Why)
		&& Backend->CreatePlatform(*GetDefault<UAsdaAntiCheatSettings>(), Why))
	{
		bAvailable = true;
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaAntiCheatSubsystem::TickEac), 0.f);
		ASDA_EAC_LOG(Log, TEXT("[EAC] готов: EOS SDK %s, платформа создана, ждём ввода логина/пароля"), *FAsdaEosSdk::Get().GetVersion());
	}
	else
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] EAC недоступен: %s — игра продолжается без защищённой сессии (решение за сервером)"), *Why);
#if UE_BUILD_SHIPPING
		// ★ Решение владельца 16.10: без EAC клиент НЕ запускается (запуск exe напрямую, без Astory.exe — бутстрапера EAC).
		// ★ 16.10.2026 (agent-eac-texts): текст 991036 утверждён владельцем (PLAYER_TEXTS.md) и ЗАШИТ здесь RU+EN одним окном:
		//   окно идёт до загрузки игры — язык сессии и DT_GameText ещё не подняты, локализацию UE не используем.
		//   Те же строки лежат в DT_GameText / DT_Loc_GameText_en под ключом 991036 — менять синхронно.
		// ★ 16.10.2026 (решение владельца, agent-retail-branding): Astory.exe — это бутстрапер EAC start_protected_game.exe,
		//   переименованный в упаковке (Tools/eac_package.py); имя игроку — только Astory.exe.
		ASDA_EAC_LOG(Error, TEXT("[EAC] запуск без защиты EAC запрещён — сообщение игроку (loc 991036, RU+EN) и выход"));
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
			TEXT("Игра должна запускаться через Astory.exe.\nЕсли служба Easy Anti-Cheat не установлена — запустите install_eac.bat.\n\n")
			TEXT("The game must be started via Astory.exe.\nIf the Easy Anti-Cheat service is not installed, run install_eac.bat."),
			TEXT("AStory Remake"));
		FPlatformMisc::RequestExit(/*bForce*/ false, TEXT("AsdaAntiCheat: запуск без EAC"));
#endif
	}
	Flow->SetPolicy(bEnabledByPolicy, bAvailable, Why);

#if ASDA_DEV_CONSOLE
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.Eac.Status"),
		TEXT("Состояние клиентского EAC: политика, состояние потока, PUID, назначенный повтор (что и через сколько), последняя причина."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			// ★ R3-02: что и когда повторяется (AsdaId / EOS / 15901) — без выхода на логин.
			ASDA_EAC_LOG(Log, TEXT("[EAC] статус: политика=%s, состояние=%s, PUID=%s, повтор: %s, причина=«%s», хост=%s"),
				bEnabledByPolicy ? TEXT("вкл") : TEXT("выкл"), AsdaEacStateName(GetState()),
				Flow && !Flow->GetPuid().IsEmpty() ? *Flow->GetPuid() : TEXT("—"),
				Flow ? *Flow->DescribeRetry(AsdaEacNow()) : TEXT("нет"), *GetLastError(), *LoginHost);
		}),
		ECVF_Default));
#if WITH_ASDA_EOSSDK
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.Eac.Probe"),
		TEXT("asda.Eac.Probe [quit] [dummysecret] — поднять EOS SDK и временную платформу (не тикается), проверить наличие интерфейса AntiCheatClient. dummysecret — заглушка при пустом ClientSecret. Сети игры не трогает."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaEacProbe),
		ECVF_Default));
#endif
#endif
}

void UAsdaAntiCheatSubsystem::Deinitialize()
{
	ASDA_EAC_LOG(Log, TEXT("[EAC] подсистема завершается (состояние: %s)"), AsdaEacStateName(GetState()));
	if (TickHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	for (IConsoleObject* C : ConsoleCmds) { if (C) { IConsoleManager::Get().UnregisterConsoleObject(C); } }
	ConsoleCmds.Empty();
	if (Flow) { Flow->StopFull(TEXT("выход из игры")); }   // EndSession, закрыть 15901, Connect_Logout
	if (Backend) { Backend->Flow = nullptr; Backend->ReleasePlatform(); }
	Flow.Reset();
	Backend.Reset();
	Super::Deinitialize();
}

// false (с причиной в OutWhy) — в Shipping задана EOS_USE_ANTICHEATCLIENTNULL: «пустой» клиент EAC не допускается.
// В Development null-клиент разрешён (самотесты, отладка без bootstrapper).
static bool AsdaEacNullClientAllowed(FString& OutWhy)
{
#if UE_BUILD_SHIPPING
	if (!FPlatformMisc::GetEnvironmentVariable(TEXT("EOS_USE_ANTICHEATCLIENTNULL")).IsEmpty())
	{
		OutWhy = TEXT("задана переменная окружения EOS_USE_ANTICHEATCLIENTNULL (пустой клиент EAC запрещён в Shipping)");
		return false;
	}
#endif
	return true;
}

bool UAsdaAntiCheatSubsystem::TickEac(float DeltaTime)
{
	const double Now = AsdaEacNow();
	if (Backend) { Backend->Tick(Now); }
	if (Flow) { Flow->Tick(Now); }
	return true;
}

void UAsdaAntiCheatSubsystem::NotifyLoginHost(const FString& Host)
{
	LoginHost = Host;
	ASDA_EAC_LOG(Log, TEXT("[EAC] NotifyLoginHost: хост LoginManager «%s» (сайдкар — порт %d на нём)"), *Host, GetDefault<UAsdaAntiCheatSettings>()->SidecarPort);
}

void UAsdaAntiCheatSubsystem::NotifyLoginCredentials(const FString& Login, const FString& PasswordMd5Hex)
{
	ASDA_EAC_LOG(Log, TEXT("[EAC] NotifyLoginCredentials: логин %s, длина MD5 %d, хост «%s», состояние %s"),
		*FAsdaEacDiag::MaskLogin(Login), PasswordMd5Hex.Len(), *LoginHost, AsdaEacStateName(GetState()));
	if (!Flow) { return; }
	if (LoginHost.IsEmpty())
	{
		ASDA_EAC_LOG(Warning, TEXT("[EAC] вход: хост LoginManager неизвестен — сессия EAC не начата"));
		return;
	}
	Flow->StartLogin(Login, PasswordMd5Hex, LoginHost, GetDefault<UAsdaAntiCheatSettings>()->SidecarPort, AsdaEacNow());
}

void UAsdaAntiCheatSubsystem::NotifyCharacterLeftWorld()
{
	if (Flow) { Flow->StopForCharacterChange(); }
}

void UAsdaAntiCheatSubsystem::NotifyEnterWorld()
{
	if (Flow) { Flow->ResumeFromRefresh(AsdaEacNow()); }
}

void UAsdaAntiCheatSubsystem::NotifySessionEnded(const TCHAR* Why)
{
	if (Flow) { Flow->StopFull(Why); }
}

void UAsdaAntiCheatSubsystem::NotifyConnectionClosed(bool bWasGame, bool bReturnToCharSelectSent, uint8 KickCode)
{
	if (!Flow) { return; }
	if (FAsdaEacFlow::IsCharacterChangeClose(bWasGame, bReturnToCharSelectSent, KickCode))
	{
		// ★ R3-01: сервер закрыл игровое соединение в ответ на наш возврат к выбору персонажа — это не разрыв.
		ASDA_EAC_LOG(Log, TEXT("[EAC] игровое соединение закрыто после возврата к выбору персонажа (1028) — смена персонажа, не разрыв"));
		Flow->StopForCharacterChange();
		return;
	}
	Flow->StopFull(bWasGame ? TEXT("разрыв игрового соединения") : TEXT("разрыв соединения с LoginManager"));
}

void UAsdaAntiCheatSubsystem::NotifyGameChannelBound(uint8 ServerKind, const uint8* Bind32)
{
	if (Flow) { Flow->OnGameChannelBound(ServerKind, Bind32, AsdaEacNow()); }
}

EAsdaEacState UAsdaAntiCheatSubsystem::GetState() const
{
	return Flow ? Flow->GetState() : EAsdaEacState::Disabled;
}

FString UAsdaAntiCheatSubsystem::GetLastError() const
{
	return Flow ? Flow->GetLastError() : FString();
}
