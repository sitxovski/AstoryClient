// Copyright (c) 2026 @sitxovski (Discord)
// Реализация проверки новой версии на ходу. Устройство, признак сравнения, схема подписи и где выключено — в шапке
//   `Public/UI/AsdaUpdateCheckSubsystem.h`, здесь не повторяется. Формат конвертов — `Reports/updater/FORMAT_v1.md` §3,
//   эталон проверки — `Tools/asda_update_format.py` (`envelope_open`, `keyring_trusted`) и `AstoryClient/Launcher/src/Updater.cpp`.
#include "UI/AsdaUpdateCheckSubsystem.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaNetSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"     // штатный выход из мира (как «Выход из игры» ESC-меню)
#include "World/AsdaLoadManagerSubsystem.h"   // экран загрузки карты — показ окна ждёт его снятия
#include "UI/AsdaNotice.h"                    // AsdaNotice::Loc — строка таблицы на языке сессии
#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// OpenSSL движка (1.1.1t — тот же, что у лаунчера): Ed25519 и SHA-256. `UI` — тип OpenSSL, конфликтует с именем
//   UE, поэтому переименовывается на время включения (так же делает движок, BuildPatchServices/Common/Crypto.cpp).
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/evp.h>
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
#undef UI

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUpdate, Log, All);

const FName UAsdaUpdateCheckSubsystem::kMsgBoxWindowId(TEXT("ONEBUT_MSGBOX"));

// Период проверки (дев-ключ; в Shipping — константа 300 с).
static ASDA_DEV_CVAR(float, CVarUpdateIntervalSec,
	TEXT("asda.Update.IntervalSec"), UAsdaUpdateCheckSubsystem::kDefaultIntervalSec,
	TEXT("Период проверки новой версии на сервере обновлений, с (по умолчанию 300). Меньше 5 — считается 5."),
	ECVF_Default);

namespace
{
	// Адрес хранилища обновлений — тот же, что у лаунчера (`AstoryClient/Launcher/src/Config.h` kUpdateUrl).
	const TCHAR* kDefaultBaseUrl = TEXT("https://@@ASTORY_UPDATE_HOST@@");
	// Канал игры лаунчера (`Config.h` kGameChannel): им считается установка без поля `channel`.
	const TCHAR* kGameChannel = TEXT("alpha");
	// Открытый корневой ключ Ed25519, hex — `Config.h` kRootPubHex (= `Releases/keys/root.pub` вашей сборки; задаётся `ASTORY_LAUNCHER_ROOT_PUBKEY`).
	//   Меняется только вместе с лаунчером (ротация корня); самотест сверяет его с root.pub хранилища.
	const TCHAR* kRootPubHex = TEXT("@@ASTORY_LAUNCHER_ROOT_PUBKEY@@");
	// Вшитый минимум keyring seq — `Config.h` kMinKeyringSeq.
	constexpr int64 kMinKeyringSeq = 1;
	// Предел тела конверта, байт (FORMAT_v1 §6 п. 12: конверт ≤ 1 МиБ).
	constexpr int32 kMaxEnvelopeBytes = 1 << 20;
	// Предел ожидания одного запроса, с (сбой = молча, повтор через интервал).
	constexpr float kRequestTimeoutSec = 20.f;
	// Нижняя граница периода проверки, с (дев-ключ не даёт долбить сервер).
	constexpr float kMinIntervalSec = 5.f;
	// Вид записи очереди окон-сообщений: 0 = однокнопочное `ONEBUT_MSGBOX` 1500 (`ShowGameMessage`).
	constexpr int32 kMsgBoxKindOneBut = 0;

	// Строка hex → байты (чётная длина, только hex-цифры). false — не hex.
	bool HexToBytes(const FString& Hex, TArray<uint8>& Out)
	{
		Out.Reset();
		if (Hex.Len() % 2 != 0) { return false; }
		for (int32 i = 0; i < Hex.Len(); i += 2)
		{
			if (!FChar::IsHexDigit(Hex[i]) || !FChar::IsHexDigit(Hex[i + 1])) { return false; }
			Out.Add((uint8)((FParse::HexDigit(Hex[i]) << 4) | FParse::HexDigit(Hex[i + 1])));
		}
		return true;
	}

	// Поле-строка JSON строго строкового типа (TryGetStringField у UE превращает числа в строки — не годится).
	bool StrField(const TSharedPtr<FJsonObject>& O, const TCHAR* Name, FString& Out)
	{
		const TSharedPtr<FJsonValue> V = O.IsValid() ? O->TryGetField(Name) : nullptr;
		if (!V.IsValid() || V->Type != EJson::String) { return false; }
		Out = V->AsString();
		return true;
	}

	// Поле-целое JSON (число без дробной части в пределах int64).
	bool IntField(const TSharedPtr<FJsonObject>& O, const TCHAR* Name, int64& Out)
	{
		const TSharedPtr<FJsonValue> V = O.IsValid() ? O->TryGetField(Name) : nullptr;
		if (!V.IsValid() || V->Type != EJson::Number) { return false; }
		const double D = V->AsNumber();
		if (D != FMath::FloorToDouble(D) || D < -9.0e18 || D > 9.0e18) { return false; }
		Out = (int64)D;
		return true;
	}

	// Разобрать текст как JSON-объект.
	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(R, O)) { return nullptr; }
		return O;
	}

	// Истинность значения JSON по правилам Python (`not k.get("revoked")` эталона): нет/null/false/0/"" — ложь.
	bool JsonTruthy(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) { return false; }
		switch (V->Type)
		{
			case EJson::None:
			case EJson::Null:    return false;
			case EJson::Boolean: return V->AsBool();
			case EJson::Number:  return V->AsNumber() != 0.0;
			case EJson::String:  return !V->AsString().IsEmpty();
			case EJson::Array:   return V->AsArray().Num() > 0;
			case EJson::Object:  return V->AsObject().IsValid() && V->AsObject()->Values.Num() > 0;
			default:             return true;
		}
	}
}

// ═════════ Проверка конвертов (чистые функции) ═════════════════════════════════════════════════════════════

TArray<uint8> AsdaUpdateVerify::RootPublicKey()
{
	TArray<uint8> K;
	HexToBytes(kRootPubHex, K);
	return K;
}

FString AsdaUpdateVerify::KeyId(const TArray<uint8>& Pub)
{
	uint8 H[SHA256_DIGEST_LENGTH];
	SHA256(Pub.GetData(), (size_t)Pub.Num(), H);
	return BytesToHex(H, 8).ToLower();
}

bool AsdaUpdateVerify::VerifyEd25519(const TArray<uint8>& Pub, const TArray<uint8>& Sig, const TArray<uint8>& Msg)
{
	if (Pub.Num() != 32 || Sig.Num() != 64) { return false; }
	EVP_PKEY* Key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, Pub.GetData(), 32);
	if (!Key) { return false; }
	EVP_MD_CTX* Ctx = EVP_MD_CTX_new();
	const bool bOk = Ctx && EVP_DigestVerifyInit(Ctx, nullptr, nullptr, nullptr, Key) == 1
		&& EVP_DigestVerify(Ctx, Sig.GetData(), 64, Msg.GetData(), (size_t)Msg.Num()) == 1;
	EVP_MD_CTX_free(Ctx);
	EVP_PKEY_free(Key);
	return bOk;
}

bool AsdaUpdateVerify::OpenEnvelope(const FString& EnvelopeJson, const FString& Kind, const TMap<FString, TArray<uint8>>& Trusted,
	TSharedPtr<FJsonObject>& OutPayload, FString& OutWhy)
{
	OutPayload.Reset();
	const TSharedPtr<FJsonObject> Env = ParseObject(EnvelopeJson);
	FString PayloadB64;
	TArray<uint8> Payload;
	const TArray<TSharedPtr<FJsonValue>>* Sigs = nullptr;
	if (!Env.IsValid() || !StrField(Env, TEXT("payload"), PayloadB64) || !FBase64::Decode(PayloadB64, Payload)
		|| !Env->TryGetArrayField(TEXT("sigs"), Sigs) || !Sigs)
	{
		OutWhy = FString::Printf(TEXT("конверт %s: повреждён"), *Kind);
		return false;
	}
	// Подписываемое сообщение: "ASDA-UPD-v1\0" + вид + "\0" + payload (FORMAT_v1 §3).
	TArray<uint8> Msg;
	const char Prefix[] = "ASDA-UPD-v1";
	Msg.Append((const uint8*)Prefix, sizeof(Prefix));   // вместе с завершающим нулём
	const FTCHARToUTF8 KindUtf8(*Kind);
	Msg.Append((const uint8*)KindUtf8.Get(), KindUtf8.Length());
	Msg.Add(0);
	Msg.Append(Payload);

	for (const TSharedPtr<FJsonValue>& SV : *Sigs)
	{
		const TSharedPtr<FJsonObject> S = (SV.IsValid() && SV->Type == EJson::Object) ? SV->AsObject() : nullptr;
		FString KeyIdStr, SigB64;
		if (!S.IsValid() || !StrField(S, TEXT("key"), KeyIdStr) || !StrField(S, TEXT("sig"), SigB64)) { continue; }   // мусорные типы — «нет подписи»
		const TArray<uint8>* Pub = Trusted.Find(KeyIdStr);
		TArray<uint8> Sig;
		if (!Pub || !FBase64::Decode(SigB64, Sig) || Sig.Num() != 64) { continue; }
		if (!VerifyEd25519(*Pub, Sig, Msg)) { continue; }
		// Подпись верна — только теперь разбор нагрузки.
		const FUTF8ToTCHAR Conv((const ANSICHAR*)Payload.GetData(), Payload.Num());
		const TSharedPtr<FJsonObject> Obj = ParseObject(FString(Conv.Length(), Conv.Get()));
		FString Format;
		if (!Obj.IsValid())
		{
			OutWhy = FString::Printf(TEXT("конверт %s: нагрузка не JSON"), *Kind);
			return false;
		}
		if (!StrField(Obj, TEXT("format"), Format) || Format != FString::Printf(TEXT("asda-%s/1"), *Kind))
		{
			OutWhy = FString::Printf(TEXT("конверт %s: чужой формат нагрузки"), *Kind);
			return false;
		}
		OutPayload = Obj;
		return true;
	}
	OutWhy = FString::Printf(TEXT("конверт %s: нет действительной подписи доверенным ключом"), *Kind);
	return false;
}

bool AsdaUpdateVerify::ParseInstalledState(const FString& StateJson, FAsdaInstalledVersion& Out)
{
	Out = FAsdaInstalledVersion();
	const TSharedPtr<FJsonObject> O = ParseObject(StateJson);
	if (!O.IsValid()) { return false; }
	StrField(O, TEXT("channel"), Out.Channel);
	StrField(O, TEXT("version"), Out.Version);
	IntField(O, TEXT("keyring_seq"), Out.KeyringSeq);
	if (Out.Channel.IsEmpty()) { Out.Channel = kGameChannel; }
	// Без версии и seq установки нет (лаунчер пишет их вместе после успешного обновления).
	Out.bValid = !Out.Version.IsEmpty() && IntField(O, TEXT("seq"), Out.Seq);
	return Out.bValid;
}

EAsdaUpdateVerdict AsdaUpdateVerify::Evaluate(const FString& KeyringJson, const FString& VersionJson, const TArray<uint8>& RootPub,
	const FAsdaInstalledVersion& Inst, FString& OutServerVersion, int64& OutServerSeq, FString& OutWhy)
{
	OutServerVersion.Reset();
	OutServerSeq = -1;
	if (RootPub.Num() != 32) { OutWhy = TEXT("корневой ключ не 32 байта"); return EAsdaUpdateVerdict::Invalid; }

	// keyring: подпись корнем, seq ≥ max(запомненный лаунчером, вшитый минимум).
	TMap<FString, TArray<uint8>> RootTrust;
	RootTrust.Add(KeyId(RootPub), RootPub);
	TSharedPtr<FJsonObject> Ring;
	if (!OpenEnvelope(KeyringJson, TEXT("keyring"), RootTrust, Ring, OutWhy)) { return EAsdaUpdateVerdict::Invalid; }
	int64 RingSeq = 0;
	const int64 RingFloor = FMath::Max<int64>(Inst.KeyringSeq, kMinKeyringSeq);
	if (!IntField(Ring, TEXT("seq"), RingSeq) || RingSeq < RingFloor)
	{
		OutWhy = FString::Printf(TEXT("keyring seq %lld ниже допустимого %lld — откат ключей"), (long long)RingSeq, (long long)RingFloor);
		return EAsdaUpdateVerdict::Invalid;
	}

	// Доверенные ключи выпуска: неотозванные, pub — 32 байта, id == key_id(pub) (`keyring_trusted` эталона).
	TMap<FString, TArray<uint8>> ReleaseTrust;
	const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
	if (Ring->TryGetArrayField(TEXT("release_keys"), Keys) && Keys)
	{
		for (const TSharedPtr<FJsonValue>& KV : *Keys)
		{
			const TSharedPtr<FJsonObject> K = (KV.IsValid() && KV->Type == EJson::Object) ? KV->AsObject() : nullptr;
			FString Id, PubHex;
			TArray<uint8> Pub;
			if (!K.IsValid() || !StrField(K, TEXT("id"), Id) || !StrField(K, TEXT("pub"), PubHex) || !HexToBytes(PubHex, Pub)) { continue; }
			if (Pub.Num() != 32 || JsonTruthy(K->TryGetField(TEXT("revoked"))) || KeyId(Pub) != Id) { continue; }
			ReleaseTrust.Add(Id, Pub);
		}
	}

	// version: подпись ключом выпуска, тот же канал, seq — целое ≥ 0.
	TSharedPtr<FJsonObject> Ver;
	if (!OpenEnvelope(VersionJson, TEXT("version"), ReleaseTrust, Ver, OutWhy)) { return EAsdaUpdateVerdict::Invalid; }
	FString Channel, Version;
	int64 Seq = -1;
	if (!StrField(Ver, TEXT("channel"), Channel) || Channel != Inst.Channel)
	{
		OutWhy = FString::Printf(TEXT("version.json канала «%s», установлен «%s»"), *Channel, *Inst.Channel);
		return EAsdaUpdateVerdict::Invalid;
	}
	if (!StrField(Ver, TEXT("version"), Version) || Version.IsEmpty() || !IntField(Ver, TEXT("seq"), Seq) || Seq < 0)
	{
		OutWhy = TEXT("version.json: нет версии или seq");
		return EAsdaUpdateVerdict::Invalid;
	}
	OutServerVersion = Version;
	OutServerSeq = Seq;
	OutWhy.Reset();
	if (Seq > Inst.Seq) { return EAsdaUpdateVerdict::Newer; }
	return (Seq == Inst.Seq) ? EAsdaUpdateVerdict::Same : EAsdaUpdateVerdict::Older;
}

// ═════════ Подсистема ═══════════════════════════════════════════════════════════════════════════════════

void UAsdaUpdateCheckSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BaseUrl = kDefaultBaseUrl;

	// Редактор/PIE и коммандлеты — выключено. Без state.json (игра не из лаунчера) — выключено.
	if (!GIsEditor && !IsRunningCommandlet() && !IsRunningDedicatedServer())
	{
		bHasStateFile = LoadInstalledState();
		bEnabled = bHasStateFile;
	}
	if (bEnabled)
	{
		NextCheckAt = FPlatformTime::Seconds() + IntervalSec();   // первая — через интервал после запуска (см. шапку)
		UE_LOG(LogAsdaUpdate, Log, TEXT("[обновления] установлена %s (канал %s, seq %lld); первая проверка через %.0f с"),
			*Installed.Version, *Installed.Channel, (long long)Installed.Seq, IntervalSec());
	}
	else
	{
		UE_LOG(LogAsdaUpdate, Log, TEXT("[обновления] проверка выключена (%s)"),
			GIsEditor ? TEXT("редактор") : TEXT("нет .asdaupd/state.json — игра не из лаунчера"));
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAsdaUpdateCheckSubsystem::OnTick), 1.0f);

#if ASDA_DEV_CONSOLE   // дев-команды; в Shipping не регистрируются
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.Update.ShowPopup"),
		TEXT("Показать окно «Доступно обновление» (тот же показ, что при найденной новой версии). Кнопка закрывает игру."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { ShowUpdatePopup(/*bForce*/true); }),
		ECVF_Default);
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.Update.CheckNow"),
		TEXT("Проверить новую версию на сервере обновлений сейчас (только когда проверка включена — игра из лаунчера)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (!CheckNow())
			{
				UE_LOG(LogAsdaUpdate, Display, TEXT("[обновления] CheckNow: %s"),
					bEnabled ? TEXT("запрос уже идёт") : TEXT("проверка выключена (нет state.json)"));
			}
		}),
		ECVF_Default);
#endif // ASDA_DEV_CONSOLE
}

void UAsdaUpdateCheckSubsystem::Deinitialize()
{
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); }
	TickHandle.Reset();
	const TArray<FHttpRequestPtr> Pending = MoveTemp(Requests);   // CancelRequest может звать колбэк — список уже пуст
	Requests.Reset();
	for (const FHttpRequestPtr& R : Pending)
	{
		if (R.IsValid()) { R->OnProcessRequestComplete().Unbind(); R->CancelRequest(); }
	}
	InFlight = 0;
#if ASDA_DEV_CONSOLE
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("asda.Update.ShowPopup"), false);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("asda.Update.CheckNow"), false);
#endif
	Super::Deinitialize();
}

bool UAsdaUpdateCheckSubsystem::LoadInstalledState()
{
	// Корень упакованной игры = папка игры лаунчера (`<лаунчер>\Game`, там же `.asdaupd`).
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::RootDir() / TEXT(".asdaupd/state.json"));
	FString Text;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		// Основной файл повреждён/занят — лаунчер держит рядом копию прошлой записи.
		if (!FFileHelper::LoadFileToString(Text, *(Path + TEXT(".bak")))) { return false; }
	}
	FAsdaInstalledVersion I;
	if (!AsdaUpdateVerify::ParseInstalledState(Text, I))
	{
		FFileHelper::LoadFileToString(Text, *(Path + TEXT(".bak")));
		if (!AsdaUpdateVerify::ParseInstalledState(Text, I))
		{
			UE_LOG(LogAsdaUpdate, Warning, TEXT("[обновления] %s без версии — проверка выключена"), *Path);
			return false;
		}
	}
	Installed = I;
	return true;
}

float UAsdaUpdateCheckSubsystem::IntervalSec() const
{
	return FMath::Max(kMinIntervalSec, CVarUpdateIntervalSec.GetValueOnGameThread());
}

double UAsdaUpdateCheckSubsystem::GetSecondsToNextCheck() const
{
	return (NextCheckAt < 0.0) ? -1.0 : (NextCheckAt - FPlatformTime::Seconds());
}

void UAsdaUpdateCheckSubsystem::ScheduleNext()
{
	NextCheckAt = FPlatformTime::Seconds() + IntervalSec();
}

bool UAsdaUpdateCheckSubsystem::OnTick(float DeltaTime)
{
	// Новая версия найдена — проверять больше нечего; держим окно на экране (смена карты сносит виджеты).
	if (bUpdateFound || bForcedPopup)
	{
		if (!bExitRequested && !IsPopupQueued() && CanShowNow()) { PushPopup(); }
		return true;
	}
	if (bEnabled && InFlight == 0 && NextCheckAt >= 0.0 && FPlatformTime::Seconds() >= NextCheckAt)
	{
		StartRequests();
	}
	return true;
}

bool UAsdaUpdateCheckSubsystem::CheckNow()
{
	if (!bEnabled || InFlight > 0) { return false; }
	StartRequests();
	return true;
}

void UAsdaUpdateCheckSubsystem::StartRequests()
{
	NextCheckAt = -1.0;   // пока запрос в полёте, срок не идёт
	bPendingNetOk = true;
	PendingKeyring.Reset();
	PendingVersion.Reset();
	Requests.Reset();
	const FString Base = BaseUrl.EndsWith(TEXT("/")) ? BaseUrl.LeftChop(1) : BaseUrl;
	for (const bool bKeyring : { true, false })
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
		R->SetVerb(TEXT("GET"));
		R->SetURL(Base + (bKeyring ? TEXT("/keys/keyring.json") : TEXT("/version.json")));
		R->SetHeader(TEXT("Cache-Control"), TEXT("no-cache"));   // указатели меняются при выпуске — без кэшей посредников
		R->SetTimeout(kRequestTimeoutSec);
		R->OnProcessRequestComplete().BindUObject(this, &UAsdaUpdateCheckSubsystem::OnResponse, bKeyring);
		Requests.Add(R);
	}
	InFlight = Requests.Num();
	const TArray<FHttpRequestPtr> Started = Requests;   // копия: колбэк может прийти и сразу (отказ старта)
	for (const FHttpRequestPtr& R : Started)
	{
		if (!R->ProcessRequest() && Requests.Contains(R))
		{
			// Запрос не стартовал и колбэка не дал — считаем сбоем сети этого запроса.
			R->OnProcessRequestComplete().Unbind();
			Requests.Remove(R);
			bPendingNetOk = false;
			InFlight = Requests.Num();
			if (InFlight == 0) { FinishCheck(false, FString(), FString()); return; }
		}
	}
}

void UAsdaUpdateCheckSubsystem::OnResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnectedOk, bool bKeyring)
{
	// Ответ запроса текущей проверки (проверку могли закрыть самотестом/выходом — тогда его в списке нет).
	if (!Requests.Contains(Req)) { return; }
	Requests.Remove(Req);
	InFlight = Requests.Num();
	const bool bOk = bConnectedOk && Resp.IsValid() && Resp->GetResponseCode() == 200
		&& Resp->GetContent().Num() > 0 && Resp->GetContent().Num() <= kMaxEnvelopeBytes;
	if (!bOk)
	{
		bPendingNetOk = false;
		UE_LOG(LogAsdaUpdate, Verbose, TEXT("[обновления] %s: сбой (связь %d, код %d)"),
			Req.IsValid() ? *Req->GetURL() : TEXT("?"), bConnectedOk ? 1 : 0, Resp.IsValid() ? Resp->GetResponseCode() : 0);
	}
	else
	{
		(bKeyring ? PendingKeyring : PendingVersion) = Resp->GetContentAsString();
	}
	if (InFlight == 0) { FinishCheck(bPendingNetOk, PendingKeyring, PendingVersion); }
}

void UAsdaUpdateCheckSubsystem::FinishCheck(bool bNetOk, const FString& KeyringJson, const FString& VersionJson)
{
	InFlight = 0;
	if (!bNetOk)
	{
		LastResult = EAsdaUpdateCheckResult::NetFail;
		ScheduleNext();
		UE_LOG(LogAsdaUpdate, Log, TEXT("[обновления] сервер обновлений недоступен — молча, повтор через %.0f с"), IntervalSec());
		return;
	}
	FString ServerVersion, Why;
	int64 ServerSeq = -1;
	const EAsdaUpdateVerdict V = AsdaUpdateVerify::Evaluate(KeyringJson, VersionJson,
		DebugRoot.Num() == 32 ? DebugRoot : AsdaUpdateVerify::RootPublicKey(),
		Installed, ServerVersion, ServerSeq, Why);
	switch (V)
	{
		case EAsdaUpdateVerdict::Newer:
			LastResult = EAsdaUpdateCheckResult::NewVersion;
			FoundVersion = ServerVersion;
			NextCheckAt = -1.0;   // защёлка: дальше проверять незачем
			UE_LOG(LogAsdaUpdate, Warning, TEXT("[обновления] на сервере %s (seq %lld), установлена %s (seq %lld) — окно «Доступно обновление»"),
				*ServerVersion, (long long)ServerSeq, *Installed.Version, (long long)Installed.Seq);
			ShowUpdatePopup(/*bForce*/false);
			return;
		case EAsdaUpdateVerdict::Same:
		case EAsdaUpdateVerdict::Older:
			LastResult = EAsdaUpdateCheckResult::UpToDate;
			UE_LOG(LogAsdaUpdate, Log, TEXT("[обновления] на сервере %s (seq %lld), установлена seq %lld — обновления нет"),
				*ServerVersion, (long long)ServerSeq, (long long)Installed.Seq);
			break;
		case EAsdaUpdateVerdict::Invalid:
		default:
			LastResult = EAsdaUpdateCheckResult::Invalid;
			UE_LOG(LogAsdaUpdate, Warning, TEXT("[обновления] ответ сервера не прошёл проверку: %s — окна нет, повтор через интервал"), *Why);
			break;
	}
	ScheduleNext();
}

bool UAsdaUpdateCheckSubsystem::IsPopupQueued() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsMsgBoxQueued(kMsgBoxKindOneBut, kLocText);
}

bool UAsdaUpdateCheckSubsystem::IsPopupDisplayed() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsMsgBoxDisplayed(kMsgBoxKindOneBut, kLocText) && WM->IsWindowVisible(kMsgBoxWindowId);
}

UUserWidget* UAsdaUpdateCheckSubsystem::DebugGetPopup() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return (WM && IsPopupDisplayed()) ? WM->FindWindow(kMsgBoxWindowId) : nullptr;
}

bool UAsdaUpdateCheckSubsystem::CanShowNow() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI || !GI->GetGameViewportClient() || !GI->GetWorld()) { return false; }
	const UAsdaLoadManagerSubsystem* Load = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	return !(Load && Load->IsLoadingScreenVisible());   // поверх экрана загрузки не показываем — ждём снятия
}

void UAsdaUpdateCheckSubsystem::ShowUpdatePopup(bool bForce)
{
	if (bForce) { bForcedPopup = true; } else { bUpdateFound = true; }
	if (bExitRequested || IsPopupQueued()) { return; }   // окно одно — не плодить
	if (!CanShowNow())
	{
		UE_LOG(LogAsdaUpdate, Log, TEXT("[обновления] показ окна отложен до снятия экрана загрузки"));
		return;   // секундный тик покажет, когда станет можно
	}
	PushPopup();
}

void UAsdaUpdateCheckSubsystem::PushPopup()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const FString Message = AsdaNotice::Loc(GI, kLocText);
	if (Message.IsEmpty())
	{
		UE_LOG(LogAsdaUpdate, Error, TEXT("[обновления] строки %d нет в таблице текста — окно показать нечем"), kLocText);
		return;
	}
	// Штатное игровое окно-сообщение: модальное, кнопка — подпись loc 991101, её действие — выход из игры.
	//   Пара (0, 991100) — ключ записи очереди: повторный показ той же пары очередь отбрасывает (не плодить).
	UUserWidget* Box = WM->ShowGameMessage(Message, kLocButton, EAsdaMsgCategory::System,
		[this]() { OnCloseGameClicked(); }, /*ZOrder*/0, /*EtalonNoticeId*/kLocText);
	if (!Box)
	{
		UE_LOG(LogAsdaUpdate, Warning, TEXT("[обновления] окно-сообщение не показано (нет WBP или запись уже в очереди)"));
		return;
	}
	++PopupShowCount;
	UE_LOG(LogAsdaUpdate, Warning, TEXT("[обновления] окно показано (раз %d, сервер %s): %s"),
		PopupShowCount, FoundVersion.IsEmpty() ? TEXT("—") : *FoundVersion, *Message);
}

void UAsdaUpdateCheckSubsystem::OnCloseGameClicked()
{
	bExitRequested = true;
	UGameInstance* GI = GetGameInstance();
	// В мире на связи — тот же штатный выход, что «Выход из игры» ESC-меню (раскладки хотбара, выход к серверу, закрытие);
	//   иначе (логин, выбор персонажа, без связи) — закрытие, как кнопка «Выход» экрана входа.
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	UAsdaSystemOptionSubsystem* Sys = GI ? GI->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr;
	if (Sys && Net && Net->IsConnected() && PC && PC->GetPawn())
	{
		UE_LOG(LogAsdaUpdate, Warning, TEXT("[обновления] «Закрыть игру» — штатный выход из мира"));
		Sys->ExitGameNow();
		return;
	}
	UE_LOG(LogAsdaUpdate, Warning, TEXT("[обновления] «Закрыть игру» — выход"));
	UKismetSystemLibrary::QuitGame(GI ? GI->GetWorld() : nullptr, nullptr, EQuitPreference::Quit, /*bIgnorePlatformRestrictions*/ false);
}

// ── Самотест ─────────────────────────────────────────────────────────────────────────────────────────────

void UAsdaUpdateCheckSubsystem::DebugBeginForTest(const FAsdaInstalledVersion& Inst, const FString& InBaseUrl, const TArray<uint8>& TestRoot)
{
	SavedInstalled = Installed;
	SavedBaseUrl = BaseUrl;
	DebugEndForTest();   // чистое состояние: без окна, без защёлок, без запросов
	Installed = Inst;
	BaseUrl = InBaseUrl;
	DebugRoot = TestRoot;
	bEnabled = true;
	ScheduleNext();
}

void UAsdaUpdateCheckSubsystem::DebugInjectResponses(bool bNetOk, const FString& KeyringJson, const FString& VersionJson)
{
	FinishCheck(bNetOk, KeyringJson, VersionJson);
}

void UAsdaUpdateCheckSubsystem::DebugEndForTest()
{
	const TArray<FHttpRequestPtr> Pending = MoveTemp(Requests);   // CancelRequest может звать колбэк — список уже пуст
	Requests.Reset();
	for (const FHttpRequestPtr& R : Pending)
	{
		if (R.IsValid()) { R->OnProcessRequestComplete().Unbind(); R->CancelRequest(); }
	}
	InFlight = 0;
	// Снять нашу запись очереди молча (без колбэка — иначе сработал бы выход из игры).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RemoveMsgBoxEntries(kMsgBoxKindOneBut, kLocText, TEXT("самотест проверки обновлений"));
	}
	bUpdateFound = bForcedPopup = bExitRequested = false;
	PopupShowCount = 0;
	DebugRoot.Reset();
	LastResult = EAsdaUpdateCheckResult::None;
	FoundVersion.Reset();
	if (!SavedBaseUrl.IsEmpty())
	{
		Installed = SavedInstalled;
		BaseUrl = SavedBaseUrl;
	}
	bEnabled = bHasStateFile;
	NextCheckAt = bEnabled ? FPlatformTime::Seconds() + IntervalSec() : -1.0;
}
