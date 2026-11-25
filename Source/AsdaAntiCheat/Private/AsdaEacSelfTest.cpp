// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ клиентского EAC (Э1-Г, 16.10.2026; ★ R3 — раздел 9): `asda.SelfTest.Eac [quit]`.
//   БЕЗ сети и без EOS-бэкенда: кодек кадров CONTRACT.md §3, политика включения §6 и машина состояний FAsdaEacFlow
//   с фальшивой стороной (фальшивые итоги HTTP/EOS/канала подаются вручную, отправленное в «канал» разбирается кодеком
//   как это сделал бы сайдкар). Ничего не отправляет и не подключает — годится для `-AsdaNoNet`.
//   Итог: строка «EAC-САМОТЕСТ ИТОГ: ok=N fail=M». `quit` — через GEngine->Exec (перехватывается asda.SelfTest.Batch).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE

#include "AsdaEacFlow.h"
#include "AsdaEacFrame.h"
#include "AsdaAntiCheatLog.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AsdaConnection.h"        // ★ ред. 3: неблокирующее подключение канала 15901 (без сети)
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"

namespace AsdaEacSelfTest
{
	// Счётчик проверок.
	struct FRun
	{
		int32 Ok = 0;     // прошло
		int32 Fail = 0;   // провалено
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaAntiCheat, Log, TEXT("EAC-САМОТЕСТ ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaAntiCheat, Error, TEXT("EAC-САМОТЕСТ FAIL %s"), *What); }
		}
	};

	// Фальшивая сторона потока: записывает вызовы, итоги подаёт тест.
	class FFakeBackend : public IAsdaEacBackend
	{
	public:
		// Последний HTTP-запрос.
		struct FHttp { FString Path; FString Body; uint32 Gen = 0; };
		TArray<FHttp> Http;               // все HTTP-запросы по порядку
		TArray<FString> ConnectTokens;    // токены, ушедшие в Connect_Login
		uint32 LastConnectGen = 0;        // поколение последнего Connect_Login
		int32 CreateUserCalls = 0;        // вызовов CreateUser
		uint32 LastCreateGen = 0;         // поколение последнего CreateUser
		int32 BeginCalls = 0;             // вызовов BeginSession
		int32 EndCalls = 0;               // вызовов EndSession
		int32 LogoutCalls = 0;            // вызовов ConnectLogout
		int32 OpenCalls = 0;              // вызовов ChannelOpen
		int32 CloseCalls = 0;             // вызовов ChannelClose
		FString OpenHost;                 // хост последнего ChannelOpen
		int32 OpenPort = 0;               // порт последнего ChannelOpen
		bool bOpenOk = true;              // итог ChannelOpen
		bool bSendOk = true;              // итог ChannelSend
		bool bBeginOk = true;             // итог BeginSession
		bool bLoggedIn = false;           // есть Connect-вход (для CopyIdToken)
		FString Puid = TEXT("0002aabbccddeeff00112233445566778");   // фальшивый PUID (32 hex + 1 — длина не важна)
		int32 TokenSerial = 0;            // номер выданного IdToken
		AsdaEacFrame::FDecoder Wire;      // всё, что ушло в канал, — глазами сайдкара
		TArray<AsdaEacFrame::FFrame> Sent;   // разобранные отправленные кадры
		TArray<TArray<uint8>> Delivered;  // что ушло в ReceiveMessageFromServer

		virtual void HttpPost(const FString& Path, const FString& JsonBody, uint32 Gen) override { Http.Add({ Path, JsonBody, Gen }); }
		virtual void ConnectLogin(const FString& AccessToken, uint32 Gen) override { ConnectTokens.Add(AccessToken); LastConnectGen = Gen; }
		virtual void CreateUser(uint32 Gen) override { ++CreateUserCalls; LastCreateGen = Gen; }
		virtual bool CopyIdToken(FString& OutPuid, FString& OutIdToken) override
		{
			if (!bLoggedIn) { return false; }
			OutPuid = Puid;
			OutIdToken = FString::Printf(TEXT("eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiIxMjMifQ.sig%d"), ++TokenSerial);
			return true;
		}
		virtual bool BeginSession() override { ++BeginCalls; return bBeginOk; }
		virtual void EndSession() override { ++EndCalls; }
		virtual void ConnectLogout() override { ++LogoutCalls; bLoggedIn = false; }
		virtual bool ChannelOpen(const FString& Host, int32 Port) override { ++OpenCalls; OpenHost = Host; OpenPort = Port; Wire.Reset(); return bOpenOk; }
		virtual void ChannelClose() override { ++CloseCalls; }
		virtual bool ChannelSend(const TArray<uint8>& Bytes) override
		{
			if (!bSendOk) { return false; }
			Wire.Feed(Bytes.GetData(), Bytes.Num());
			AsdaEacFrame::FFrame F;
			while (Wire.Next(F)) { Sent.Add(F); }
			return true;
		}
		virtual void DeliverToAntiCheat(const uint8* Data, int32 Len) override { Delivered.Emplace(Data, Len); }

		// Сколько отправлено кадров данного типа.
		int32 CountSent(uint8 Type) const
		{
			int32 N = 0;
			for (const AsdaEacFrame::FFrame& F : Sent) { if (F.Type == Type) { ++N; } }
			return N;
		}
		// ★ ред. 3: последний отправленный кадр данного типа (nullptr — не было).
		const AsdaEacFrame::FFrame* LastSent(uint8 Type) const
		{
			for (int32 i = Sent.Num() - 1; i >= 0; --i) { if (Sent[i].Type == Type) { return &Sent[i]; } }
			return nullptr;
		}
	};

	// ★ ред. 3: проверить, что кадр — BIND с данным видом и привязкой (u8 kind · u8[32] bind).
	static bool IsBind(const AsdaEacFrame::FFrame* F, uint8 Kind, const uint8* Bind32)
	{
		uint8 K = 0, Bd[32] = {};
		return F && F->Type == AsdaEacWire::kBind && AsdaEacFrame::ParseBind(F->Payload, K, Bd) && K == Kind && FMemory::Memcmp(Bd, Bind32, 32) == 0;
	}

	// Кадр «от сайдкара».
	static TArray<uint8> SrvFrame(uint8 Type, const TArray<uint8>& Payload)
	{
		TArray<uint8> Out;
		AsdaEacFrame::Encode(Type, Payload.GetData(), Payload.Num(), Out);
		return Out;
	}

	// Ответ /token|/refresh 200.
	static FString TokenJson(const TCHAR* Access, const TCHAR* Refresh)
	{
		return FString::Printf(TEXT("{\"access_token\":\"%s\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"refresh_token\":\"%s\"}"), Access, Refresh);
	}

	// Поле строки из JSON-тела запроса.
	static FString JsonField(const FString& Body, const TCHAR* Name)
	{
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<TCHAR>> R = TJsonReaderFactory<TCHAR>::Create(Body);
		FString V;
		if (FJsonSerializer::Deserialize(R, O) && O.IsValid()) { O->TryGetStringField(Name, V); }
		return V;
	}

	static const TCHAR* kMd5 = TEXT("4297F44B13955235245B2497399D7A93");   // MD5("123123") — проверка нижнего регистра

	// ★ R3-02: ряд пауз повтора — каждая > 0 и ≤ 60 с, не убывает; Max — самая длинная.
	struct FDelays
	{
		double Prev = 0.0;   // предыдущая пауза
		double Max = 0.0;    // самая длинная пауза
		int32 N = 0;         // сколько пауз записано
		bool bOk = true;     // все паузы в (0, 60] и не убывают
		void Add(double D)
		{
			if (D <= 0.0 || D > FAsdaEacFlow::kRetryMaxSec + 1e-6 || D + 1e-9 < Prev) { bOk = false; }
			Prev = D;
			Max = FMath::Max(Max, D);
			++N;
		}
		// Строка ряда для журнала самотеста.
		FString Text() const { return FString::Printf(TEXT("%d пауз, максимум %.0f с"), N, Max); }
	};

	// Довести поток до Active (общий пролог сценариев). Возвращает текущее время.
	static double DriveToActive(FRun& R, FAsdaEacFlow& Flow, FFakeBackend& B, double T, const TCHAR* Tag)
	{
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("10.0.0.5"), 15901, T);
		const uint32 G = B.Http.Num() > 0 ? B.Http.Last().Gen : 0;
		Flow.OnHttpResult(G, 200, TokenJson(TEXT("jwt-access-1"), TEXT("refresh-1")), T);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), T);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, T);
		R.Expect(Flow.GetState() == EAsdaEacState::Active, FString::Printf(TEXT("[%s] пролог: состояние Active"), Tag));
		return T;
	}
}

static void AsdaEacSelfTest_Run(const TArray<FString>& Args)
{
	using namespace AsdaEacSelfTest;
	FRun R;
	const bool bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });

	// ── 1. Кодек кадров §3 ───────────────────────────────────────────────────────────────────────
	{
		TArray<uint8> Buf;
		const uint8 P[3] = { 0xAA, 0xBB, 0xCC };
		R.Expect(AsdaEacFrame::Encode(0x02, P, 3, Buf) && Buf.Num() == 6 && Buf[0] == 4 && Buf[1] == 0 && Buf[2] == 0x02 && Buf[5] == 0xCC,
			TEXT("кодек: u16 len(LE)=type+payload · u8 type · payload"));
		AsdaEacFrame::BuildPing(Buf);
		TArray<uint8> Ack = SrvFrame(AsdaEacWire::kPong, {});
		Buf.Append(Ack);
		AsdaEacFrame::FDecoder D;
		for (uint8 Byte : Buf) { D.Feed(&Byte, 1); }   // поток побайтно
		AsdaEacFrame::FFrame F1, F2, F3, F4;
		R.Expect(D.Next(F1) && F1.Type == 0x02 && F1.Payload.Num() == 3 && D.Next(F2) && F2.Type == AsdaEacWire::kPing && F2.Payload.Num() == 0
			&& D.Next(F3) && F3.Type == AsdaEacWire::kPong && !D.Next(F4) && !D.IsError(),
			TEXT("декодер: три кадра, поданные по байту, разобраны по порядку"));
		TArray<uint8> Big; Big.SetNumZeroed(4095);
		TArray<uint8> Out;
		R.Expect(AsdaEacFrame::Encode(0x02, Big.GetData(), 4095, Out) && Out.Num() == 4098, TEXT("кодек: len = 4096 (предел) собирается"));
		Big.SetNumZeroed(4096);
		TArray<uint8> Out2;
		R.Expect(!AsdaEacFrame::Encode(0x02, Big.GetData(), 4096, Out2) && Out2.Num() == 0, TEXT("кодек: len = 4097 отвергнут"));
		AsdaEacFrame::FDecoder Bad0; const uint8 Z[3] = { 0, 0, 1 }; Bad0.Feed(Z, 3); AsdaEacFrame::FFrame X;
		R.Expect(!Bad0.Next(X) && Bad0.IsError(), TEXT("декодер: len = 0 → поток испорчен"));
		AsdaEacFrame::FDecoder Bad1; const uint8 Y[3] = { 0x01, 0x10, 1 }; Bad1.Feed(Y, 3);   // 0x1001 = 4097
		R.Expect(!Bad1.Next(X) && Bad1.IsError(), TEXT("декодер: len = 4097 → поток испорчен"));

		TArray<uint8> Reg;
		R.Expect(AsdaEacFrame::BuildReg(TEXT("0002abc"), TEXT("eyJ.a.b"), 1, Reg), TEXT("REG собран"));
		AsdaEacFrame::FDecoder DR; DR.Feed(Reg.GetData(), Reg.Num()); AsdaEacFrame::FFrame FR;
		uint8 Ver = 0, Plat = 0; FString Pu, Tk;
		R.Expect(DR.Next(FR) && FR.Type == AsdaEacWire::kReg && AsdaEacFrame::ParseReg(FR.Payload, Ver, Plat, Pu, Tk)
			&& Ver == 1 && Plat == 1 && Pu == TEXT("0002abc") && Tk == TEXT("eyJ.a.b")
			&& FR.Payload.Num() == 2 + 2 + 7 + 2 + 7,
			TEXT("REG: ver=1 · platform=1 · u16 n+PUID · u16 n+IdToken — байт-в-байт"));
		TArray<uint8> RegBad;
		R.Expect(!AsdaEacFrame::BuildReg(TEXT("0002abЖ"), TEXT("t"), 1, RegBad), TEXT("REG: не-ASCII в PUID отвергнут"));
		TArray<uint8> A0, A513, A512;
		uint8 M[513] = {};
		R.Expect(!AsdaEacFrame::BuildAcMsg(M, 0, A0) && !AsdaEacFrame::BuildAcMsg(M, 513, A513) && AsdaEacFrame::BuildAcMsg(M, 512, A512),
			TEXT("ACMSG: 0 и 513 б отвергнуты, 512 б собран"));
		// KICK с кириллицей в деталях (UTF-8).
		FTCHARToUTF8 U8(TEXT("Нарушение: тест"));
		TArray<uint8> KP; KP.Add(2); KP.Add(7); KP.Add((uint8)(U8.Length() & 0xFF)); KP.Add((uint8)(U8.Length() >> 8)); KP.Append((const uint8*)U8.Get(), U8.Length());
		uint8 Our = 0, Eos = 0; FString Det;
		R.Expect(AsdaEacFrame::ParseKick(KP, Our, Eos, Det) && Our == 2 && Eos == 7 && Det == TEXT("Нарушение: тест"), TEXT("KICK: ourReason · eosReason · u16 n + UTF-8"));
		KP.Add(0);
		R.Expect(!AsdaEacFrame::ParseKick(KP, Our, Eos, Det), TEXT("KICK: лишний байт — формат нарушен"));
		uint8 St = 0;
		R.Expect(AsdaEacFrame::ParseRegResult({ 4 }, St) && St == 4 && !AsdaEacFrame::ParseRegResult({ 0, 0 }, St), TEXT("REG_RESULT: ровно 1 байт"));
		// ★ ред. 3 (§8.1): BIND и BIND_RESULT.
		uint8 Bv[32]; for (int32 i = 0; i < 32; ++i) { Bv[i] = (uint8)(0xA0 + i); }
		TArray<uint8> Bf;
		R.Expect(AsdaEacFrame::BuildBind(AsdaEacWire::kServerKindIndun, Bv, Bf) && Bf.Num() == 2 + 1 + 1 + 32 && Bf[0] == 34 && Bf[1] == 0
			&& Bf[2] == 0x05 && Bf[3] == 3 && Bf[4] == 0xA0 && Bf[35] == 0xBF,
			TEXT("BIND: u16 len=34 · type 0x05 · u8 serverKind · u8[32] bind — байт-в-байт"));
		uint8 Bb = 0xFF;
		R.Expect(AsdaEacFrame::ParseBindResult({ 2 }, Bb) && Bb == 2 && !AsdaEacFrame::ParseBindResult({}, Bb) && !AsdaEacFrame::ParseBindResult({ 0, 0 }, Bb),
			TEXT("BIND_RESULT (0x85): ровно 1 байт"));
		R.Expect(AsdaEacWire::kBind == 0x05 && AsdaEacWire::kBindResult == 0x85 && AsdaEacLoc::ForKick(6) == 991026 && AsdaEacLoc::ForKick(7) == 991020,
			TEXT("коды ред. 3: BIND 0x05, BIND_RESULT 0x85, причина 6 → loc 991026"));
	}

	// ── 2. Политика включения §6 ───────────────────────────────────────────────────────────────────
	R.Expect(!FAsdaEacFlow::ComputeEnabled(true, true, true) && !FAsdaEacFlow::ComputeEnabled(false, true, true),
		TEXT("политика: -AsdaNoNet — выключен всегда (и в Shipping, и с -AsdaEac)"));
	R.Expect(FAsdaEacFlow::ComputeEnabled(true, false, false), TEXT("политика: Shipping — включён без ключей"));
	R.Expect(!FAsdaEacFlow::ComputeEnabled(false, false, false) && FAsdaEacFlow::ComputeEnabled(false, false, true),
		TEXT("политика: Development — только с -AsdaEac"));

	// ── 3. Машина состояний: основной путь ─────────────────────────────────────────────────────────
	{
		FFakeBackend B; FAsdaEacFlow Flow(B);
		TArray<EAsdaEacState> States; Flow.OnState = [&States](EAsdaEacState S) { States.Add(S); };
		Flow.SetPolicy(true, true);
		double T = 1000.0;
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("10.0.0.5"), 15901, T);
		R.Expect(B.Http.Num() == 1 && B.Http[0].Path == TEXT("/token") && JsonField(B.Http[0].Body, TEXT("login")) == TEXT("tester")
			&& JsonField(B.Http[0].Body, TEXT("password_md5")) == FString(kMd5).ToLower() && Flow.GetState() == EAsdaEacState::TokenRequest,
			TEXT("вход: POST /token {login, password_md5 строчными}"));
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("jwt-access-1"), TEXT("refresh-1")), T);
		R.Expect(B.ConnectTokens.Num() == 1 && B.ConnectTokens[0] == TEXT("jwt-access-1") && Flow.GetState() == EAsdaEacState::ConnectLogin
			&& !Flow.HasPasswordMd5() && Flow.HasRefreshToken(),
			TEXT("200 /token → Connect_Login(JWT); MD5 пароля стёрт, refresh в памяти"));
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), T);
		R.Expect(B.BeginCalls == 1 && B.OpenCalls == 1 && B.OpenHost == TEXT("10.0.0.5") && B.OpenPort == 15901 && Flow.GetState() == EAsdaEacState::Registering,
			TEXT("Connect ok → CopyIdToken → BeginSession → канал к хосту LoginManager:15901"));
		uint8 Ver = 0, Plat = 0; FString Pu, Tk;
		R.Expect(B.Sent.Num() == 1 && B.Sent[0].Type == AsdaEacWire::kReg && AsdaEacFrame::ParseReg(B.Sent[0].Payload, Ver, Plat, Pu, Tk)
			&& Plat == AsdaEacWire::kPlatformWindows && Pu == B.Puid && Tk.EndsWith(TEXT("sig1")),
			TEXT("первый кадр канала — REG (Windows, PUID, IdToken из CopyIdToken)"));
		const uint8 Msg1[5] = { 1, 2, 3, 4, 5 };
		Flow.OnMessageToServer(Msg1, 5);
		R.Expect(Flow.GetQueuedCount() == 1 && B.CountSent(AsdaEacWire::kAcMsg) == 0, TEXT("ACMSG до REG_RESULT — в очереди, в канал не ушёл"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, T);
		R.Expect(Flow.GetState() == EAsdaEacState::Active && B.CountSent(AsdaEacWire::kAcMsg) == 1 && B.Sent.Last().Payload == TArray<uint8>(Msg1, 5),
			TEXT("REG_RESULT 0 → Active, очередь ACMSG вытолкнута байт-в-байт"));
		Flow.OnMessageToServer(Msg1, 3);
		R.Expect(B.CountSent(AsdaEacWire::kAcMsg) == 2, TEXT("Active: ACMSG уходит сразу"));
		const TArray<uint8> In = { 9, 8, 7 };
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kAcMsg, In).GetData(), 6, T);
		R.Expect(B.Delivered.Num() == 1 && B.Delivered[0] == In, TEXT("ACMSG от сайдкара → ReceiveMessageFromServer"));
		Flow.Tick(T + 5.0);
		R.Expect(B.CountSent(AsdaEacWire::kPing) == 0, TEXT("PING не раньше 10 с"));
		Flow.Tick(T + 10.0);
		R.Expect(B.CountSent(AsdaEacWire::kPing) == 1, TEXT("PING через 10 с"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kPong, {}).GetData(), 3, T + 10.1);
		Flow.Tick(T + 20.1);
		R.Expect(B.CountSent(AsdaEacWire::kPing) == 2 && Flow.GetState() == EAsdaEacState::Active, TEXT("PONG держит канал живым, PING идёт каждые 10 с"));
		R.Expect(States.Contains(EAsdaEacState::TokenRequest) && States.Contains(EAsdaEacState::ConnectLogin)
			&& States.Contains(EAsdaEacState::Registering) && States.Last() == EAsdaEacState::Active, TEXT("наблюдатель видел все переходы"));

		// Обновление токена: AuthExpiration → /refresh → Connect_Login, сессия не прерывается.
		Flow.OnAuthExpiration(T + 21.0);
		R.Expect(B.Http.Num() == 2 && B.Http[1].Path == TEXT("/refresh") && JsonField(B.Http[1].Body, TEXT("refresh_token")) == TEXT("refresh-1"),
			TEXT("AuthExpiration → POST /refresh {refresh_token}"));
		Flow.OnHttpResult(B.Http[1].Gen, 200, TokenJson(TEXT("jwt-access-2"), TEXT("refresh-2")), T + 21.5);
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), T + 22.0);
		R.Expect(B.ConnectTokens.Last() == TEXT("jwt-access-2") && Flow.GetState() == EAsdaEacState::Active && B.EndCalls == 0,
			TEXT("refresh → повторный Connect_Login, сессия EAC не прерывалась"));
		Flow.OnAuthExpiration(T + 23.0);
		R.Expect(JsonField(B.Http.Last().Body, TEXT("refresh_token")) == TEXT("refresh-2"), TEXT("следующий refresh — новым refresh-токеном (старый погашен)"));
		// ★ ред. 3: 401 на /refresh теперь — окончательный отказ с сообщением (отдельный блок 8); здесь — обычный успех.
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("jwt-access-3"), TEXT("refresh-3")), T + 23.5);
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), T + 23.6);
		R.Expect(Flow.GetState() == EAsdaEacState::Active && Flow.HasRefreshToken(), TEXT("второй refresh → сессия живёт, refresh-3 в памяти"));

		// Потеря канала → EndSession → переподключение с новой сессией и свежим токеном.
		const int32 Opens = B.OpenCalls, Begins = B.BeginCalls;
		Flow.OnChannelClosed(T + 30.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && B.EndCalls == 1, TEXT("обрыв 15901 → EndSession, Reconnecting"));
		Flow.Tick(T + 31.0);
		R.Expect(B.OpenCalls == Opens, TEXT("переподключение не раньше паузы (2 с)"));
		Flow.Tick(T + 32.1);
		R.Expect(B.OpenCalls == Opens + 1 && B.BeginCalls == Begins + 1 && Flow.GetState() == EAsdaEacState::Registering
			&& B.Sent.Last().Type == AsdaEacWire::kReg, TEXT("через паузу: BeginSession заново и REG по новому каналу"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, T + 32.2);
		// Живость: 30 с без данных → переподключение.
		Flow.Tick(T + 42.2); Flow.Tick(T + 52.2);   // PING уходят, PONG нет
		Flow.Tick(T + 62.3);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting, TEXT("30 с без данных от сайдкара → канал считается потерянным"));

		// KICK: фатальное событие, полный сброс.
		Flow.Tick(T + 70.0);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, T + 70.1);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		const int32 Ends = B.EndCalls, Closes = B.CloseCalls;
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kKick, { 5, 0, 0, 0 }).GetData(), 7, T + 71.0);
		R.Expect(Fatals.Num() == 1 && Fatals[0].Source == EAsdaEacFatalSource::SidecarKick && Fatals[0].OurReason == 5
			&& Fatals[0].LocId == AsdaEacLoc::kKickBase + 5, TEXT("KICK reason 5 → событие OnFatal (loc 991025)"));
		R.Expect(Flow.GetState() == EAsdaEacState::Kicked && B.EndCalls == Ends + 1 && B.CloseCalls == Closes + 1 && B.LogoutCalls >= 1
			&& !Flow.HasRefreshToken(), TEXT("KICK → EndSession, канал закрыт, Connect_Logout, всё стёрто"));
		Flow.StopFull(TEXT("разрыв после кика"));
		R.Expect(Flow.GetState() == EAsdaEacState::Kicked, TEXT("состояние «кик» не затирается последующим сбросом"));
	}

	// ── 4. InvalidUser → CreateUser; 401; сетевые повторы; устаревшие итоги ─────────────────────────
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::InvalidUser, FString(), 0.0);
		R.Expect(B.CreateUserCalls == 1 && Flow.GetState() == EAsdaEacState::CreateUser, TEXT("EOS_InvalidUser → CreateUser(ContinuanceToken)"));
		B.bLoggedIn = true;
		Flow.OnCreateUserResult(B.LastCreateGen, true, FString(), 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Registering && B.BeginCalls == 1, TEXT("CreateUser ok → BeginSession → REG"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 401, TEXT("{\"error\":\"invalid_grant\"}"), 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Failed && B.ConnectTokens.Num() == 0 && !Flow.HasPasswordMd5(),
			TEXT("401 /token → отказ без Connect_Login, MD5 стёрт"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 0, FString(), 0.0);
		R.Expect(B.Http.Num() == 1 && Flow.GetState() == EAsdaEacState::TokenRequest && Flow.HasPasswordMd5(), TEXT("сеть недоступна → повтор запланирован, MD5 ещё нужен"));
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 1.0);
		R.Expect(B.ConnectTokens.Num() == 0, TEXT("ответ в паузе до повтора — отброшен"));
		Flow.Tick(4.9);
		R.Expect(B.Http.Num() == 1, TEXT("повтор не раньше 5 с"));
		Flow.Tick(5.0);
		R.Expect(B.Http.Num() == 2 && B.Http[1].Path == TEXT("/token"), TEXT("повтор /token через 5 с"));
		for (int32 i = 2; i <= 7; ++i)
		{
			Flow.OnHttpResult(B.Http.Last().Gen, 503, FString(), 100.0 * i);
			Flow.Tick(100.0 * i + 100.0);
		}
		// ★ R3-02: потолка числа попыток нет — 8 запросов, отказа нет, MD5 ещё нужен для следующего повтора.
		R.Expect(B.Http.Num() == 8 && Flow.GetState() == EAsdaEacState::TokenRequest && Flow.HasPasswordMd5(),
			TEXT("R3-02: 503 шесть раз подряд — отказа нет, /token повторяется дальше (8-й запрос ушёл)"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		const uint32 Old = B.Http[0].Gen;
		Flow.StopFull(TEXT("тест"));
		Flow.OnHttpResult(Old, 200, TokenJson(TEXT("a"), TEXT("r")), 0.1);
		R.Expect(B.ConnectTokens.Num() == 0 && Flow.GetState() == EAsdaEacState::Idle, TEXT("итог запроса, ушедшего до сброса, отброшен (поколение)"));
	}

	// ── 5. REG_RESULT 4 / 1 / 2 ─────────────────────────────────────────────────────────────────
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 4 }).GetData(), 4, 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::ServerDisabled && B.EndCalls == 1 && B.CloseCalls == 1, TEXT("REG_RESULT 4 → EAC выключен на сервере: сессия и канал закрыты"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 1 }).GetData(), 4, 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting, TEXT("REG_RESULT 1 (первый) → повтор со свежим IdToken"));
		Flow.Tick(2.1);
		uint8 V = 0, P = 0; FString Pu, Tk;
		R.Expect(B.Sent.Last().Type == AsdaEacWire::kReg && AsdaEacFrame::ParseReg(B.Sent.Last().Payload, V, P, Pu, Tk) && Tk.EndsWith(TEXT("sig2")),
			TEXT("повторная REG несёт новый IdToken"));
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		const int32 HttpBefore = B.Http.Num();
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 1 }).GetData(), 4, 2.2);
		// ★ R3-02: на логин — только 401 / REG_RESULT 2 / KICK; REG_RESULT 1 повторно — повтор с паузой и продление входа.
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && Flow.GetLastRegStatus() == 1 && Fatals.Num() == 0
			&& B.Http.Num() == HttpBefore + 1 && B.Http.Last().Path == TEXT("/refresh"),
			TEXT("R3-02: REG_RESULT 1 повторно → без отказа: переподключение по паузе и /refresh для свежего IdToken"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 2 }).GetData(), 4, 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Failed && B.LogoutCalls == 1, TEXT("REG_RESULT 2 → отказ, Connect сброшен"));
	}

	// ── 6. Нарушение целостности, смена персонажа, порча потока, переполнение очереди ────────────
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		DriveToActive(R, Flow, B, 0.0, TEXT("целостность"));
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		Flow.OnIntegrityViolated(5, TEXT("GameFileMismatch"));
		R.Expect(Fatals.Num() == 1 && Fatals[0].Source == EAsdaEacFatalSource::IntegrityViolation && Fatals[0].ViolationType == 5
			&& Fatals[0].LocId == 991005 && Flow.GetState() == EAsdaEacState::Kicked && B.EndCalls == 1,
			TEXT("IntegrityViolated(5) → OnFatal (loc 991005), EndSession, Kicked"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		DriveToActive(R, Flow, B, 0.0, TEXT("смена персонажа"));
		Flow.StopForCharacterChange();
		// ★ R3-01: смена персонажа не закрывает 15901 и не завершает сессию EAC.
		R.Expect(Flow.GetState() == EAsdaEacState::Active && B.EndCalls == 0 && B.CloseCalls == 0 && B.LogoutCalls == 0 && Flow.HasRefreshToken(),
			TEXT("R3-01: смена персонажа — ни EndSession, ни закрытия 15901, ни Connect_Logout; сессия Active"));
		const int32 HttpN = B.Http.Num();
		Flow.ResumeFromRefresh(10.0);
		R.Expect(B.Http.Num() == HttpN && Flow.GetState() == EAsdaEacState::Active && B.BeginCalls == 1,
			TEXT("R3-01: вход в мир при живой сессии — ни /refresh, ни BeginSession, ни новой REG"));
		Flow.StopFull(TEXT("разрыв"));
		R.Expect(Flow.GetState() == EAsdaEacState::Idle && !Flow.HasRefreshToken() && B.EndCalls == 1 && B.CloseCalls == 1 && B.LogoutCalls == 1,
			TEXT("настоящий разрыв: полный сброс — EndSession, 15901 закрыт, Connect_Logout, refresh стёрт"));
		const int32 N = B.Http.Num();
		Flow.ResumeFromRefresh(20.0);
		R.Expect(B.Http.Num() == N, TEXT("после разрыва возобновлять нечем — запроса нет"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		DriveToActive(R, Flow, B, 0.0, TEXT("порча потока"));
		const uint8 Garbage[3] = { 0, 0, 0x81 };   // len = 0
		Flow.OnChannelData(Garbage, 3, 1.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && B.EndCalls == 1, TEXT("len = 0 от сайдкара → канал рвётся, EndSession"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		const uint8 Msg[2] = { 1, 2 };
		for (int32 i = 0; i < FAsdaEacFlow::kQueueMax; ++i) { Flow.OnMessageToServer(Msg, 2); }
		R.Expect(Flow.GetQueuedCount() == FAsdaEacFlow::kQueueMax && Flow.GetState() == EAsdaEacState::Registering, TEXT("очередь до регистрации держит 256"));
		Flow.OnMessageToServer(Msg, 2);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && Flow.GetQueuedCount() == 0, TEXT("257-е сообщение → сессия заново (сообщения EAC не теряем молча)"));
		Flow.Tick(1.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting, TEXT("до паузы переподключения — ждём"));
		Flow.Tick(40.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Registering, TEXT("REG_RESULT нет 30 с → … (сначала новый REG)"));
		Flow.Tick(70.1);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting, TEXT("REG_RESULT не пришёл за 30 с → переподключение"));
	}
	{
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		B.bOpenOk = false;
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && B.EndCalls == 1, TEXT("сайдкар недоступен → EndSession, переподключение по таймеру"));
	}

	// ── 7. Выключен / недоступен: вход без EAC ───────────────────────────────────────────────────
	{
		FFakeBackend B; FAsdaEacFlow Flow(B);
		Flow.SetPolicy(false, false, TEXT("тест"));
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		R.Expect(B.Http.Num() == 0 && Flow.GetState() == EAsdaEacState::Disabled, TEXT("Disabled: вход не трогает AsdaId/EOS"));
		Flow.SetPolicy(true, false, TEXT("нет интерфейса"));
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		R.Expect(B.Http.Num() == 0 && Flow.GetState() == EAsdaEacState::Unavailable && Flow.GetLastError() == TEXT("нет интерфейса"),
			TEXT("Unavailable: «EAC недоступен», игра идёт без сессии"));
	}

	// ── 8. ★ ред. 3 (CONTRACT.md §8): BIND, окончательные отказы с сообщением, повтор REG после 4, неблокирующий connect ──
	uint8 Bind1[32], Bind2[32];
	for (int32 i = 0; i < 32; ++i) { Bind1[i] = (uint8)(i + 1); Bind2[i] = (uint8)(0xF0 ^ i); }
	{
		// 8a. Рукопожатие игрового соединения РАНЬШЕ REG_RESULT 0 → BIND сразу после REG_RESULT 0.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		Flow.OnGameChannelBound(AsdaEacWire::kServerKindGame, Bind1, 0.5);
		R.Expect(Flow.HasGameBind() && B.CountSent(AsdaEacWire::kBind) == 0, TEXT("BIND: рукопожатие до REG_RESULT 0 — привязка запомнена, кадр ещё не ушёл"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, 1.0);
		R.Expect(B.CountSent(AsdaEacWire::kBind) == 1 && IsBind(B.LastSent(AsdaEacWire::kBind), 2, Bind1) && B.Sent.Last().Type == AsdaEacWire::kBind,
			TEXT("BIND: сразу после REG_RESULT 0 — serverKind 2 и bind игрового соединения байт-в-байт"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kBindResult, { 0 }).GetData(), 4, 1.1);
		R.Expect(Flow.IsBindAcked(), TEXT("BIND_RESULT 0 → привязка принята"));
		// 8b. Новое рукопожатие (портал / переподключение) при принятой регистрации → BIND сразу, новое значение.
		Flow.OnGameChannelBound(AsdaEacWire::kServerKindGame, Bind2, 2.0);
		R.Expect(B.CountSent(AsdaEacWire::kBind) == 2 && IsBind(B.LastSent(AsdaEacWire::kBind), 2, Bind2) && !Flow.IsBindAcked(),
			TEXT("BIND: новое рукопожатие в Active — кадр сразу, новая привязка"));
		// 8c. BIND_RESULT 2 → повтор через 5 с.
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kBindResult, { 2 }).GetData(), 4, 2.1);
		Flow.Tick(7.0);
		R.Expect(B.CountSent(AsdaEacWire::kBind) == 2, TEXT("BIND_RESULT 2: повтор не раньше 5 с"));
		Flow.Tick(7.2);
		R.Expect(B.CountSent(AsdaEacWire::kBind) == 3 && IsBind(B.LastSent(AsdaEacWire::kBind), 2, Bind2), TEXT("BIND_RESULT 2 → повтор BIND через 5 с"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kBindResult, { 0 }).GetData(), 4, 7.3);
		// 8d. Потеря канала → новая регистрация → BIND заново (новая EAC-сессия у сайдкара).
		Flow.OnChannelClosed(8.0);
		Flow.Tick(10.1);
		R.Expect(Flow.GetState() == EAsdaEacState::Registering && B.CountSent(AsdaEacWire::kBind) == 3, TEXT("переподключение: до REG_RESULT 0 BIND не шлётся"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, 10.2);
		R.Expect(B.CountSent(AsdaEacWire::kBind) == 4 && IsBind(B.LastSent(AsdaEacWire::kBind), 2, Bind2),
			TEXT("переподключение: после REG_RESULT 0 BIND с привязкой текущего игрового соединения"));
		Flow.StopFull(TEXT("разрыв"));
		R.Expect(!Flow.HasGameBind(), TEXT("разрыв игрового соединения: привязка забыта"));
	}
	{
		// 8e. 401 на /refresh живой сессии → окончательный отказ с сообщением (LocId 991040) и возврат на логин (OnFatal).
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		DriveToActive(R, Flow, B, 0.0, TEXT("401 refresh"));
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		Flow.OnAuthExpiration(100.0);
		Flow.OnHttpResult(B.Http.Last().Gen, 401, TEXT("{\"error\":\"invalid_grant\"}"), 100.5);
		R.Expect(Fatals.Num() == 1 && Fatals[0].Source == EAsdaEacFatalSource::SessionFailed && Fatals[0].LocId == AsdaEacLoc::kRefreshRejected
			&& Fatals[0].FailKind == EAsdaEacFailKind::Auth && AsdaEacLoc::kRefreshRejected == 991040,
			TEXT("401 /refresh → OnFatal(SessionFailed, loc 991040 «войдите заново», Auth)"));
		R.Expect(Flow.GetState() == EAsdaEacState::Failed && B.EndCalls == 1 && B.CloseCalls >= 1 && !Flow.HasRefreshToken(),
			TEXT("401 /refresh → EndSession, канал закрыт, refresh стёрт, состояние «отказ»"));
		Flow.StopFull(TEXT("возврат на логин"));
		R.Expect(Flow.GetState() == EAsdaEacState::Failed, TEXT("отказ не затирается сбросом сети (UI видит итог)"));
	}
	{
		// 8f. ★ R3-02: 401 на /refresh при подъёме входа EOS (после временной ошибки Connect) → то же сообщение 991040.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Error, TEXT("EOS_TimedOut"), 0.1);
		R.Expect(Fatals.Num() == 0 && Flow.GetState() == EAsdaEacState::TokenRequest && Flow.GetNextRetryAt() > 0.1,
			TEXT("R3-02: ошибка EOS Connect → без отказа, подъём входа по паузе"));
		Flow.Tick(Flow.GetNextRetryAt());
		Flow.OnHttpResult(B.Http.Last().Gen, 401, TEXT("{\"error\":\"invalid_grant\"}"), 10.1);
		R.Expect(B.Http.Last().Path == TEXT("/refresh") && Fatals.Num() == 1 && Fatals[0].LocId == AsdaEacLoc::kRefreshRejected
			&& Flow.GetState() == EAsdaEacState::Failed, TEXT("подъём входа: 401 /refresh → сообщение 991040, отказ"));
	}
	{
		// 8g. Прочие окончательные отказы — тоже с сообщением (без молчаливого кика): 401 /token, REG_RESULT 2.
		//   ★ R3-02: недоступный AsdaId (503 подряд) — уже НЕ отказ.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http.Last().Gen, 401, TEXT("{}"), 0.0);
		R.Expect(Fatals.Num() == 1 && Fatals[0].LocId == AsdaEacLoc::kAuthRejected && Fatals[0].FailKind == EAsdaEacFailKind::Auth,
			TEXT("401 /token → сообщение 991037 (Auth)"));
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		for (int32 i = 1; i <= 5; ++i)
		{
			Flow.OnHttpResult(B.Http.Last().Gen, 503, FString(), 100.0 * i);
			Flow.Tick(100.0 * i + 70.0);
		}
		R.Expect(Fatals.Num() == 1 && Flow.GetState() == EAsdaEacState::TokenRequest && B.Http.Num() == 7,
			TEXT("R3-02: AsdaId 503 пять раз подряд → сообщения нет, повтор идёт дальше"));
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 1000.0);
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 1000.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 1000.0);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 2 }).GetData(), 4, 1000.1);
		R.Expect(Fatals.Num() == 2 && Fatals[1].LocId == 991032 && Fatals[1].FailKind == EAsdaEacFailKind::Auth, TEXT("REG_RESULT 2 → сообщение 991032 (Auth)"));
	}
	{
		// 8h. REG_RESULT 4 (Mode=0) → повтор REG раз в 300 с (E1-21): сервер мог перейти 0 → 1/2 без перезахода.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		Flow.OnGameChannelBound(AsdaEacWire::kServerKindGame, Bind1, 0.0);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 4 }).GetData(), 4, 10.0);
		R.Expect(Flow.GetState() == EAsdaEacState::ServerDisabled && FMath::IsNearlyEqual(Flow.GetServerDisabledRetryAt(), 310.0)
			&& B.CountSent(AsdaEacWire::kBind) == 0, TEXT("REG_RESULT 4 → ServerDisabled, повтор назначен через 300 с, BIND не шлётся"));
		const int32 Opens = B.OpenCalls, Begins = B.BeginCalls, Regs = B.CountSent(AsdaEacWire::kReg);
		Flow.Tick(309.9);
		R.Expect(B.OpenCalls == Opens && Flow.GetState() == EAsdaEacState::ServerDisabled, TEXT("REG_RESULT 4: до 300 с — тишина"));
		Flow.Tick(310.0);
		R.Expect(B.OpenCalls == Opens + 1 && B.BeginCalls == Begins + 1 && B.CountSent(AsdaEacWire::kReg) == Regs + 1
			&& Flow.GetState() == EAsdaEacState::Registering, TEXT("через 300 с: BeginSession, канал и новая REG"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 4 }).GetData(), 4, 310.1);
		R.Expect(Flow.GetState() == EAsdaEacState::ServerDisabled && FMath::IsNearlyEqual(Flow.GetServerDisabledRetryAt(), 610.1),
			TEXT("снова 4 → следующий повтор ещё через 300 с"));
		Flow.Tick(610.1);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, 610.2);
		R.Expect(Flow.GetState() == EAsdaEacState::Active && IsBind(B.LastSent(AsdaEacWire::kBind), 2, Bind1),
			TEXT("сервер включил EAC (REG_RESULT 0) → Active и BIND игрового соединения"));
	}
	{
		// 8i. Канал «подключение начато» (неблокирующий connect): REG копится, отказ подключения → переподключение по паузе.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Registering && B.CountSent(AsdaEacWire::kReg) == 1, TEXT("connect начат: REG поставлен в канал без ожидания"));
		Flow.OnChannelClosed(1.0);   // транспорт: не подключилось за 1 с
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && B.EndCalls == 1, TEXT("connect не состоялся → EndSession, переподключение по паузе"));
	}
	{
		// 8j. FAsdaConnection::ConnectAsync — игровой поток не ждёт; предел 1 с; без сети.
		FAsdaConnection C;
		int32 RawBytes = 0; int32 Closed = 0;
		C.SetRawStreamMode([&RawBytes](const uint8*, int32 Len) { RawBytes += Len; });
		C.SetOnClosed([&Closed]() { ++Closed; });
		const double T0 = FPlatformTime::Seconds();
		// Настоящий ConnectAsync зовём ТОЛЬКО в прогоне без сети (там он обязан отказать сразу, не создав сокета).
		const bool bStarted = FAsdaConnection::IsNoNetRun() ? C.ConnectAsync(TEXT("127.0.0.1"), 15901, 4, 1.f) : false;
		const double Dt0 = FPlatformTime::Seconds() - T0;
		R.Expect(FAsdaConnection::IsNoNetRun() ? (!bStarted && !C.IsConnectPending() && Dt0 < 0.05) : true,
			FString::Printf(TEXT("ConnectAsync при -AsdaNoNet: отказ сразу (%.1f мс), сокета нет"), Dt0 * 1000.0));
		// «Подключение, на которое никто не отвечает» — без сокета и сети (самотестовый вход того же состояния).
		C.DebugBeginStalledConnect(1.f);
		const uint8 Reg[3] = { 1, 0, 4 };
		R.Expect(C.IsConnectPending() && !C.IsConnected() && C.SendRaw(Reg, 3), TEXT("подключение идёт: SendRaw копит кадр (true), соединения ещё нет"));
		double MaxTick = 0.0, ClosedAt = 0.0;
		const double S0 = FPlatformTime::Seconds();
		while (FPlatformTime::Seconds() - S0 < 1.6 && Closed == 0)
		{
			const double A = FPlatformTime::Seconds();
			C.Tick();
			const double D = FPlatformTime::Seconds() - A;
			MaxTick = FMath::Max(MaxTick, D);
			if (Closed > 0) { ClosedAt = FPlatformTime::Seconds() - S0; }
			FPlatformProcess::SleepNoStats(0.01f);
		}
		R.Expect(MaxTick < 0.005, FString::Printf(TEXT("Tick во время подключения не ждёт: максимум %.2f мс"), MaxTick * 1000.0));
		R.Expect(Closed == 1 && ClosedAt >= 0.95 && ClosedAt <= 1.3,
			FString::Printf(TEXT("не подключилось за 1 с → OnClosed через %.0f мс (предел 1 с)"), ClosedAt * 1000.0));
		R.Expect(!C.IsConnectPending() && !C.SendRaw(Reg, 3) && RawBytes == 0, TEXT("после отказа: состояние снято, накопленное выброшено, SendRaw = false"));
		C.DebugBeginStalledConnect(1.f);
		C.Disconnect();
		C.Tick();
		R.Expect(!C.IsConnectPending() && Closed == 1, TEXT("Disconnect во время подключения: снято без OnClosed"));
	}

	// ── 9. ★ R3 (CONTRACT.md §8.6): смена персонажа без закрытия 15901 (R3-01); без выхода на логин при сбоях доступности (R3-02) ──
	{
		// 9a. R3-01: смена персонажа ×6 — канал 15901 не закрывается, сессия EAC не завершается, на каждый вход — только BIND.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		DriveToActive(R, Flow, B, 0.0, TEXT("R3-01 смена ×6"));
		uint8 Bw[32]; for (int32 i = 0; i < 32; ++i) { Bw[i] = (uint8)(0x11 + i); }
		Flow.OnGameChannelBound(AsdaEacWire::kServerKindGame, Bw, 0.5);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kBindResult, { 0 }).GetData(), 4, 0.6);
		const int32 Http0 = B.Http.Num(), Open0 = B.OpenCalls, Begin0 = B.BeginCalls, Reg0 = B.CountSent(AsdaEacWire::kReg);
		const int32 Bind0 = B.CountSent(AsdaEacWire::kBind);
		bool bBindEach = true, bNoBindBetween = true, bActive = true, bClassified = true;
		for (int32 k = 1; k <= 6; ++k)
		{
			const double Tk = 20.0 * k;
			// Сервер закрыл игровое соединение после нашего 1028 без 4101 — так это видит UAsdaNetSubsystem.
			bClassified &= FAsdaEacFlow::IsCharacterChangeClose(/*bWasGame*/true, /*1028*/true, /*4101 нет*/0xFF);
			Flow.StopForCharacterChange();
			bNoBindBetween &= !Flow.HasGameBind();
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kPong, {}).GetData(), 3, Tk + 1.0);
			Flow.Tick(Tk + 1.0);
			Flow.StartLogin(TEXT("tester"), kMd5, TEXT("10.0.0.5"), 15901, Tk + 2.0);   // экран логина порта: пароль заново
			Flow.ResumeFromRefresh(Tk + 3.0);                                         // 1021 — вход в мир
			uint8 Bk[32]; for (int32 i = 0; i < 32; ++i) { Bk[i] = (uint8)(k * 16 + i); }
			Flow.OnGameChannelBound(AsdaEacWire::kServerKindGame, Bk, Tk + 4.0);      // рукопожатие нового игрового соединения
			bBindEach &= B.CountSent(AsdaEacWire::kBind) == Bind0 + k && IsBind(B.LastSent(AsdaEacWire::kBind), 2, Bk);
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kBindResult, { 0 }).GetData(), 4, Tk + 4.1);
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kPong, {}).GetData(), 3, Tk + 10.0);
			Flow.Tick(Tk + 10.0);
			bActive &= Flow.GetState() == EAsdaEacState::Active && Flow.IsBindAcked();
		}
		R.Expect(bClassified && B.CloseCalls == 0 && B.EndCalls == 0 && B.LogoutCalls == 0,
			TEXT("R3-01 смена персонажа ×6: канал 15901 ни разу не закрыт, EndSession и Connect_Logout не звались"));
		R.Expect(B.OpenCalls == Open0 && B.BeginCalls == Begin0 && B.CountSent(AsdaEacWire::kReg) == Reg0 && B.Http.Num() == Http0,
			TEXT("R3-01 ×6: ни нового канала, ни BeginSession, ни REG, ни /token, ни /refresh"));
		R.Expect(bBindEach && B.CountSent(AsdaEacWire::kBind) == Bind0 + 6,
			TEXT("R3-01 ×6: на каждый вход в мир — ровно один BIND с привязкой нового игрового соединения"));
		R.Expect(bNoBindBetween && bActive && Fatals.Num() == 0 && !Flow.HasPasswordMd5(),
			TEXT("R3-01 ×6: между выходом и входом привязки нет, сессия Active (BIND принят), отказов нет, MD5 не хранится"));
		Flow.StopFull(TEXT("выход из игры"));
		R.Expect(B.CloseCalls == 1 && B.EndCalls == 1 && B.LogoutCalls == 1 && !Flow.HasRefreshToken(),
			TEXT("R3-01: выход из игры / настоящий разрыв — полный сброс (15901 закрыт, EndSession, Connect_Logout)"));
	}
	R.Expect(FAsdaEacFlow::IsCharacterChangeClose(true, true, 0xFF) && !FAsdaEacFlow::IsCharacterChangeClose(true, false, 0xFF)
		&& !FAsdaEacFlow::IsCharacterChangeClose(false, true, 0xFF) && !FAsdaEacFlow::IsCharacterChangeClose(true, true, 6),
		TEXT("R3-01: закрытие игрового соединения после 1028 без 4101 — смена персонажа; без 1028, разрыв с LoginManager или 4101 — полный сброс"));
	{
		// 9b. R3-01: другая учётка или другой сервер после возврата к выбору персонажа — полный перезапуск.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		DriveToActive(R, Flow, B, 0.0, TEXT("R3-01 другая учётка"));
		Flow.StopForCharacterChange();
		Flow.StartLogin(TEXT("other"), kMd5, TEXT("10.0.0.5"), 15901, 5.0);
		R.Expect(B.EndCalls == 1 && B.CloseCalls == 1 && B.LogoutCalls == 1 && B.Http.Last().Path == TEXT("/token")
			&& JsonField(B.Http.Last().Body, TEXT("login")) == TEXT("other") && Flow.GetState() == EAsdaEacState::TokenRequest,
			TEXT("R3-01: другая учётка — прежняя сессия закрыта целиком, новый /token"));
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a2"), TEXT("r2")), 5.1);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 5.2);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, 5.3);
		Flow.StartLogin(TEXT("other"), kMd5, TEXT("10.0.0.9"), 15901, 6.0);
		R.Expect(B.EndCalls == 2 && B.CloseCalls == 2 && B.LogoutCalls == 2 && Flow.GetState() == EAsdaEacState::TokenRequest,
			TEXT("R3-01: та же учётка на другом сервере — полный перезапуск"));
	}
	{
		// 9c. R3-02: /token — 503, сеть, 429, 400 ×12: на логин не уходим, повтор каждый раз, пауза растёт до 60 с.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		const int32 Codes[] = { 503, 0, 429, 400, 503, 0, 502, 503, 0, 503, 503, 0 };
		FDelays D; double T = 0.0; bool bEach = true;
		for (int32 C : Codes)
		{
			Flow.OnHttpResult(B.Http.Last().Gen, C, FString(), T);
			const double At = Flow.GetNextRetryAt();
			D.Add(At - T);
			bEach &= Flow.GetState() == EAsdaEacState::TokenRequest && Flow.DescribeRetry(T).Contains(TEXT("/token"));
			const int32 N = B.Http.Num();
			Flow.Tick(At - 0.01);
			bEach &= B.Http.Num() == N;   // не раньше паузы
			Flow.Tick(At);
			bEach &= B.Http.Num() == N + 1 && B.Http.Last().Path == TEXT("/token");
			T = At + 0.5;
		}
		R.Expect(D.bOk && bEach && D.N == 12 && FMath::IsNearlyEqual(D.Max, 60.0) && Fatals.Num() == 0 && B.Http.Num() == 13,
			FString::Printf(TEXT("R3-02: /token 503/сеть/429/400 ×12 — без выхода на логин, каждый раз повтор, пауза растёт и ≤ 60 с (%s)"), *D.Text()));
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("acc-0"), TEXT("ref-0")), T);
		R.Expect(Flow.GetState() == EAsdaEacState::ConnectLogin && B.ConnectTokens.Last() == TEXT("acc-0") && !Flow.HasPasswordMd5(),
			TEXT("R3-02: AsdaId ожил — вход идёт дальше (Connect_Login), MD5 стёрт"));

		// 9d. Временные ошибки EOS Connect ×8 → подъём входа (/refresh → Connect_Login) с растущей паузой, без логина.
		FDelays D2; bool bEach2 = true;
		for (int32 k = 1; k <= 8; ++k)
		{
			Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Error, TEXT("EOS_TimedOut"), T);
			bEach2 &= Flow.GetState() == EAsdaEacState::TokenRequest;
			const double At = Flow.GetNextRetryAt();
			D2.Add(At - T);
			Flow.Tick(At);
			bEach2 &= B.Http.Last().Path == TEXT("/refresh");
			const FString Acc = FString::Printf(TEXT("acc-%d"), k);
			Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(*Acc, *FString::Printf(TEXT("ref-%d"), k)), At + 0.1);
			bEach2 &= Flow.GetState() == EAsdaEacState::ConnectLogin && B.ConnectTokens.Last() == Acc;
			T = At + 0.2;
		}
		R.Expect(D2.bOk && bEach2 && FMath::IsNearlyEqual(D2.Max, 60.0) && Fatals.Num() == 0,
			FString::Printf(TEXT("R3-02: EOS_Connect_Login временно ×8 — без логина, /refresh → Connect с растущей паузой ≤ 60 с (%s)"), *D2.Text()));
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), T);
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, T + 0.1);
		R.Expect(Flow.GetState() == EAsdaEacState::Active && Flow.DescribeRetry(T + 0.2) == TEXT("нет") && Flow.GetLastError().IsEmpty(),
			TEXT("R3-02: EOS ожил — Active, повтора нет, причина сброшена"));
	}
	{
		// 9e. R3-02: BeginSession временно ×8, затем 15901 недоступен ×4 — Connect жив, повтор с паузой до 60 с, без логина.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		B.bBeginOk = false;
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && Fatals.Num() == 0, TEXT("R3-02: BeginSession отказал → повтор по паузе, отказа нет"));
		FDelays D; double T = 0.0;
		for (int32 k = 1; k <= 8; ++k)
		{
			const double At = Flow.GetNextRetryAt();
			D.Add(At - T);
			Flow.Tick(At);
			T = At;
		}
		R.Expect(D.bOk && FMath::IsNearlyEqual(D.Max, 60.0) && B.BeginCalls == 9 && B.LogoutCalls == 0 && Fatals.Num() == 0
			&& Flow.GetState() == EAsdaEacState::Reconnecting && Flow.DescribeRetry(T).Contains(TEXT("15901")),
			FString::Printf(TEXT("R3-02: BeginSession ×9 отказов — Connect не сброшен, паузы 2…60 с (%s)"), *D.Text()));
		B.bBeginOk = true;
		B.bOpenOk = false;
		FDelays D2; D2.Prev = D.Prev;
		for (int32 k = 1; k <= 4; ++k)
		{
			const double At = Flow.GetNextRetryAt();
			D2.Add(At - T);
			Flow.Tick(At);
			T = At;
		}
		R.Expect(D2.bOk && FMath::IsNearlyEqual(D2.Max, 60.0) && B.OpenCalls == 4 && B.EndCalls == 4 && Fatals.Num() == 0
			&& Flow.GetState() == EAsdaEacState::Reconnecting,
			FString::Printf(TEXT("R3-02: сайдкар 15901 недоступен ×4 — EndSession, повтор каждые ≤ 60 с, без логина (%s)"), *D2.Text()));
		B.bOpenOk = true;
		Flow.Tick(Flow.GetNextRetryAt());
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, T + 61.0);
		Flow.OnChannelClosed(T + 62.0);
		R.Expect(Flow.GetState() == EAsdaEacState::Reconnecting && FMath::IsNearlyEqual(Flow.GetNextRetryAt() - (T + 62.0), 2.0),
			TEXT("R3-02: после REG_RESULT 0 пауза переподключения снова с 2 с"));
	}
	{
		// 9f. R3-02: продление живой сессии (/refresh) 503 ×8 — сессия идёт, повтор ≤ 60 с; отказ Connect при продлении — тоже повтор.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		DriveToActive(R, Flow, B, 0.0, TEXT("R3-02 refresh 503"));
		double T = 1.0;
		Flow.OnAuthExpiration(T);
		FDelays D; bool bAlive = true;
		for (int32 k = 1; k <= 8; ++k)
		{
			Flow.OnHttpResult(B.Http.Last().Gen, k % 2 ? 503 : 0, FString(), T);
			const double At = Flow.GetNextRetryAt();
			D.Add(At - T);
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kPong, {}).GetData(), 3, At);   // сайдкар жив всё это время
			Flow.Tick(At);
			bAlive &= Flow.GetState() == EAsdaEacState::Active && B.Http.Last().Path == TEXT("/refresh");
			T = At + 0.1;
		}
		R.Expect(D.bOk && bAlive && FMath::IsNearlyEqual(D.Max, 60.0) && B.EndCalls == 0 && B.CloseCalls == 0 && Fatals.Num() == 0,
			FString::Printf(TEXT("R3-02: /refresh живой сессии 503/сеть ×8 — сессия Active, повтор ≤ 60 с, без логина (%s)"), *D.Text()));
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("acc-r"), TEXT("ref-r")), T);
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Error, TEXT("EOS_ServiceFailure"), T + 0.1);
		const double At = Flow.GetNextRetryAt();
		R.Expect(At > T && At - T <= 60.0 && Flow.GetState() == EAsdaEacState::Active && Fatals.Num() == 0 && B.LogoutCalls == 0,
			TEXT("R3-02: продление Connect не удалось — сессия идёт, /refresh повторится по паузе"));
		Flow.OnChannelData(SrvFrame(AsdaEacWire::kPong, {}).GetData(), 3, At);
		Flow.Tick(At);
		R.Expect(B.Http.Last().Path == TEXT("/refresh") && JsonField(B.Http.Last().Body, TEXT("refresh_token")) == TEXT("ref-r"),
			TEXT("R3-02: повтор продления — /refresh новым refresh-токеном"));
	}
	{
		// 9g. R3-02: CreateUser / CopyIdToken временно, REG_RESULT 3 ×8 — без логина, повтор с паузой.
		FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
		TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
		Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::InvalidUser, FString(), 0.0);
		Flow.OnCreateUserResult(B.LastCreateGen, false, TEXT("EOS_TimedOut"), 0.1);
		R.Expect(Flow.GetState() == EAsdaEacState::TokenRequest && Flow.GetNextRetryAt() > 0.1 && Fatals.Num() == 0,
			TEXT("R3-02: CreateUser временно — подъём входа по паузе, без логина"));
		Flow.Tick(Flow.GetNextRetryAt());
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a1"), TEXT("r1")), 6.0);
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 6.1);   // B.bLoggedIn = false → CopyIdToken откажет
		R.Expect(Flow.GetState() == EAsdaEacState::TokenRequest && B.LogoutCalls == 1 && Fatals.Num() == 0,
			TEXT("R3-02: CopyIdToken не выдал токен — Connect сброшен, подъём входа по паузе, без логина"));
		Flow.Tick(Flow.GetNextRetryAt());
		Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a2"), TEXT("r2")), 30.0);
		B.bLoggedIn = true;
		Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 30.1);
		FDelays D; double T = 30.1;
		for (int32 k = 1; k <= 8; ++k)
		{
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 3 }).GetData(), 4, T);
			const double At = Flow.GetNextRetryAt();
			D.Add(At - T);
			Flow.Tick(At);
			T = At;
		}
		R.Expect(D.bOk && FMath::IsNearlyEqual(D.Max, 60.0) && Fatals.Num() == 0 && Flow.GetState() == EAsdaEacState::Registering,
			FString::Printf(TEXT("R3-02: REG_RESULT 3 ×8 — новая REG по паузе ≤ 60 с, без логина (%s)"), *D.Text()));
	}

	// ── 10. ★ 16.10.2026 (тексты владельца): ключ показа по причине KICK / нарушения / отказа; мягкое → чат ОДИН раз ──
	{
		// 10a. KICK по причинам 1..6 (через настоящий поток — кадр KICK сайдкара) → заголовок 991020 + текст 991021..991026.
		for (uint8 Our = 1; Our <= 6; ++Our)
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
			const double T = DriveToActive(R, Flow, B, 0.0, TEXT("10a"));
			const TArray<uint8> Kick = { Our, 1, 3, 0, 'a', 'b', 'c' };   // ourReason · eosReason · u16 3 · «abc»
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kKick, Kick).GetData(), 3 + Kick.Num(), T + 1.0);
			const AsdaEacLoc::FFatalDisplay D = Fatals.Num() == 1 ? AsdaEacLoc::ForFatal(Fatals[0]) : AsdaEacLoc::FFatalDisplay();
			R.Expect(Fatals.Num() == 1 && D.TitleId == 991020 && D.BodyId == 991020 + Our && D.bDetails == (Our == 2)
				&& D.FallbackReason == (Our == 5 ? 2 : 6) && Fatals[0].Details == TEXT("abc"),
				FString::Printf(TEXT("10a: KICK причина %u → заголовок 991020 + текст %d, деталь EAC %s, запасной код %u"),
					(uint32)Our, 991020 + Our, Our == 2 ? TEXT("да") : TEXT("нет"), (uint32)D.FallbackReason));
		}
		{
			FAsdaEacFatal F; F.Source = EAsdaEacFatalSource::SidecarKick; F.OurReason = 9; F.LocId = AsdaEacLoc::ForKick(9);
			const AsdaEacLoc::FFatalDisplay D = AsdaEacLoc::ForFatal(F);
			R.Expect(D.TitleId == 991020 && D.BodyId == 0 && D.bDetails && D.FallbackReason == 6,
				TEXT("10a: KICK причина вне 1..6 → только заголовок 991020 + деталь EAC, запасной 1106"));
		}
		// 10b. Нарушение целостности: тип 1..15 → 991000 + 99100N; тип вне диапазона → заголовок + текст EAC.
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
			DriveToActive(R, Flow, B, 0.0, TEXT("10b"));
			Flow.OnIntegrityViolated(9, TEXT("ForbiddenModule"));
			const AsdaEacLoc::FFatalDisplay D = Fatals.Num() == 1 ? AsdaEacLoc::ForFatal(Fatals[0]) : AsdaEacLoc::FFatalDisplay();
			R.Expect(Fatals.Num() == 1 && D.TitleId == 991000 && D.BodyId == 991009 && !D.bDetails && D.FallbackReason == 6,
				TEXT("10b: нарушение тип 9 → заголовок 991000 + текст 991009, без детали"));
			FAsdaEacFatal U; U.Source = EAsdaEacFatalSource::IntegrityViolation; U.ViolationType = 77; U.LocId = AsdaEacLoc::ForViolation(77);
			const AsdaEacLoc::FFatalDisplay DU = AsdaEacLoc::ForFatal(U);
			R.Expect(DU.TitleId == 991000 && DU.BodyId == 0 && DU.bDetails, TEXT("10b: нарушение неизвестного типа → заголовок 991000 + текст EAC"));
		}
		// 10c. Отказ входа (на логин): REG_RESULT 2 → 991032; 401 /token → 991037; 401 /refresh (991040) → показ 991037.
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
			Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
			Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
			B.bLoggedIn = true;
			Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 2 }).GetData(), 4, 0.0);
			const AsdaEacLoc::FFatalDisplay D = Fatals.Num() == 1 ? AsdaEacLoc::ForFatal(Fatals[0]) : AsdaEacLoc::FFatalDisplay();
			R.Expect(Fatals.Num() == 1 && D.TitleId == 0 && D.BodyId == 991032 && !D.bDetails && D.FallbackReason == 11,
				TEXT("10c: REG_RESULT 2 → текст 991032 без заголовка, запасной 12941"));
		}
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<FAsdaEacFatal> Fatals; Flow.OnFatal = [&Fatals](const FAsdaEacFatal& F) { Fatals.Add(F); };
			Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
			Flow.OnHttpResult(B.Http[0].Gen, 401, FString(), 0.0);
			const AsdaEacLoc::FFatalDisplay D = Fatals.Num() == 1 ? AsdaEacLoc::ForFatal(Fatals[0]) : AsdaEacLoc::FFatalDisplay();
			R.Expect(Fatals.Num() == 1 && D.BodyId == 991037 && D.FallbackReason == 11, TEXT("10c: 401 /token → текст 991037, запасной 12941"));
			FAsdaEacFatal Rf; Rf.Source = EAsdaEacFatalSource::SessionFailed; Rf.LocId = AsdaEacLoc::kRefreshRejected; Rf.FailKind = EAsdaEacFailKind::Auth;
			R.Expect(AsdaEacLoc::ForFatal(Rf).BodyId == 991037, TEXT("10c: 401 /refresh (991040) → показ 991037 «сессия входа истекла»"));
		}
		// 10d. Мягкое → чат один раз: REG_RESULT 3 ×5 — одна строка 991033; успех снимает; новый сбой — снова строка.
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<int32> Soft; Flow.OnSoftNotice = [&Soft](int32 Id) { Soft.Add(Id); };
			Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
			Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
			B.bLoggedIn = true;
			Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
			double T = 0.0;
			for (int32 k = 0; k < 5; ++k)
			{
				Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 3 }).GetData(), 4, T);
				T = Flow.GetNextRetryAt();
				Flow.Tick(T);
			}
			R.Expect(Soft.Num() == 1 && Soft[0] == 991033 && Flow.GetSoftLocId() == 991033,
				FString::Printf(TEXT("10d: REG_RESULT 3 ×5 → в чат одна строка 991033 (строк: %d)"), Soft.Num()));
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 0 }).GetData(), 4, T);
			R.Expect(Flow.GetState() == EAsdaEacState::Active && Flow.GetSoftLocId() == 0 && Soft.Num() == 1,
				TEXT("10d: REG_RESULT 0 → мягкое состояние снято без новой строки"));
			Flow.OnChannelClosed(T + 1.0);
			Flow.Tick(Flow.GetNextRetryAt());
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 3 }).GetData(), 4, Flow.GetNextRetryAt() + 0.1);
			R.Expect(Soft.Num() == 2 && Soft[1] == 991033, TEXT("10d: после успеха новый REG_RESULT 3 → снова одна строка"));
		}
		// 10e. REG_RESULT 1 (первый и повторный) → одна строка 991031; REG_RESULT 4 → строки нет (991034 — только журнал).
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<int32> Soft; Flow.OnSoftNotice = [&Soft](int32 Id) { Soft.Add(Id); };
			Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
			Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
			B.bLoggedIn = true;
			Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 1 }).GetData(), 4, 0.0);
			Flow.Tick(Flow.GetNextRetryAt());
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 1 }).GetData(), 4, 2.2);
			R.Expect(Soft.Num() == 1 && Soft[0] == 991031, FString::Printf(TEXT("10e: REG_RESULT 1 ×2 → одна строка 991031 (строк: %d)"), Soft.Num()));
		}
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<int32> Soft; Flow.OnSoftNotice = [&Soft](int32 Id) { Soft.Add(Id); };
			Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
			Flow.OnHttpResult(B.Http[0].Gen, 200, TokenJson(TEXT("a"), TEXT("r")), 0.0);
			B.bLoggedIn = true;
			Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Success, FString(), 0.0);
			Flow.OnChannelData(SrvFrame(AsdaEacWire::kRegResult, { 4 }).GetData(), 4, 0.0);
			R.Expect(Soft.Num() == 0 && Flow.GetState() == EAsdaEacState::ServerDisabled, TEXT("10e: REG_RESULT 4 → игроку ничего (991034 только в журнал)"));
		}
		// 10f. AsdaId 503 ×4 → одна строка 991038; ответ 200 снимает; EOS Connect ×3 → одна строка 991039.
		{
			FFakeBackend B; FAsdaEacFlow Flow(B); Flow.SetPolicy(true, true);
			TArray<int32> Soft; Flow.OnSoftNotice = [&Soft](int32 Id) { Soft.Add(Id); };
			Flow.StartLogin(TEXT("tester"), kMd5, TEXT("h"), 15901, 0.0);
			double T = 0.0;
			for (int32 k = 0; k < 4; ++k)
			{
				Flow.OnHttpResult(B.Http.Last().Gen, 503, FString(), T);
				T = Flow.GetNextRetryAt();
				Flow.Tick(T);
			}
			R.Expect(Soft.Num() == 1 && Soft[0] == 991038, FString::Printf(TEXT("10f: /token 503 ×4 → одна строка 991038 (строк: %d)"), Soft.Num()));
			Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a"), TEXT("r")), T);
			R.Expect(Flow.GetSoftLocId() == 0 && Soft.Num() == 1, TEXT("10f: /token 200 → «сервис входа недоступен» снято"));
			for (int32 k = 0; k < 3; ++k)
			{
				Flow.OnConnectLoginResult(B.LastConnectGen, EAsdaEacConnectResult::Error, TEXT("EOS_TimedOut"), T);
				T = Flow.GetNextRetryAt();
				Flow.Tick(T);
				Flow.OnHttpResult(B.Http.Last().Gen, 200, TokenJson(TEXT("a"), TEXT("r")), T);
			}
			R.Expect(Soft.Num() == 2 && Soft[1] == 991039, FString::Printf(TEXT("10f: EOS Connect ×3 → одна строка 991039 (строк: %d)"), Soft.Num()));
		}
		// 10g. Таблица мягких и запасные нотисы.
		R.Expect(AsdaEacLoc::IsSoft(991031) && AsdaEacLoc::IsSoft(991033) && AsdaEacLoc::IsSoft(991038) && AsdaEacLoc::IsSoft(991039)
			&& !AsdaEacLoc::IsSoft(991034) && !AsdaEacLoc::IsSoft(991032) && AsdaEacLoc::SoftFallbackNotice(991031) == 12941
			&& AsdaEacLoc::SoftFallbackNotice(991038) == 1098 && AsdaEacLoc::SoftFallbackNotice(991039) == 1098 && AsdaEacLoc::SoftFallbackNotice(991033) == 0,
			TEXT("10g: мягкие 991031/991033/991038/991039; 991034 не показывается; запасные 12941/1098"));
	}

	UE_LOG(LogAsdaAntiCheat, Log, TEXT("EAC-САМОТЕСТ ИТОГ: ok=%d fail=%d — %s"), R.Ok, R.Fail, R.Fail == 0 ? TEXT("ВСЁ СОШЛОСЬ") : TEXT("ЕСТЬ ПРОВАЛЫ"));
	if (bQuit && GEngine) { GEngine->Exec(nullptr, TEXT("quit")); }
}

static FAutoConsoleCommand GAsdaSelfTestEac(
	TEXT("asda.SelfTest.Eac"),
	TEXT("asda.SelfTest.Eac [quit] — офлайн: кодек кадров канала 15901, политика включения и машина состояний клиентского EAC (фальшивая сторона, без сети и EOS)."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaEacSelfTest_Run));

#endif // ASDA_DEV_CONSOLE
