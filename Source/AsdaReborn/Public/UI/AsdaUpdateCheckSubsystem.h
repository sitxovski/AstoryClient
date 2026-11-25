// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaUpdateCheckSubsystem — ПРОВЕРКА НОВОЙ ВЕРСИИ ИГРЫ НА ХОДУ (18.10.2026, по просьбе владельца). [ПОРТ]
//
// ═════════ ЧТО ДЕЛАЕТ ════════════════════════════════════════════════════════════════════════════════
//   Пока игра запущена, раз в 5 минут спрашивает у сервера обновлений (`https://@@ASTORY_UPDATE_HOST@@`), не вышла ли
//   новая версия. Если вышла — один раз показывает модальное окно «Доступно обновление! Закройте игру и обновитесь»
//   с ОДНОЙ кнопкой «Закрыть игру»; по кнопке — штатный выход (в мире — тот же путь, что «Выход из игры» ESC-меню:
//   раскладки хотбара + выход к серверу, затем закрытие; вне мира — закрытие, как кнопка «Выход» экрана входа).
//   У оригинала такой функции нет — это система обновлений порта (формат v1, `Reports/updater/FORMAT_v1.md` §9).
//
// ═════════ С ЧЕМ СРАВНИВАЕТ (тот же признак, что у лаунчера) ═══════════════════════════════════════════
//   Установленная версия — состояние лаунчера `<папка игры>/.asdaupd/state.json` (`channel`, `version`, `seq`,
//   `keyring_seq`; пишет `AstoryClient/Launcher/src/State.cpp` после КАЖДОГО успешного обновления). Папка игры = корень
//   упакованной игры (`FPaths::RootDir()`: рядом лежат `AsdaReborn/`, `Engine/`, `Astory.exe`). `ProjectVersion` из
//   DefaultGame.ini НЕ годится — он не меняется между выпусками.
//   Версия на сервере — подписанный конверт `version.json` (`asda-version/1`: channel, version, seq, min_version).
//   Новая версия = `seq` из version.json СТРОГО больше установленного `seq` того же канала. seq канала монотонен
//   (`asda_release.py`), version.json выкладывается ПОСЛЕ канала (`publish-set`) — лаунчер к моменту показа окна уже
//   видит ту же версию, ложного «обновитесь» при обрыве выкладки нет. Равный или меньший seq — окна нет.
//
// ═════════ ПОДПИСЬ (та же схема и тот же корневой ключ, что у лаунчера) ═══════════════════════════════════
//   `keys/keyring.json` проверяется вшитым корневым ключом Ed25519 (`kRootPubHex` = `AstoryClient/Launcher/src/Config.h`,
//   он же `Releases/keys/root.pub`), его `seq` ≥ max(запомненного лаунчером `keyring_seq`, вшитого минимума 1);
//   доверенные ключи выпуска — неотозванные, `id == SHA-256(pub)[0:8]`. `version.json` проверяется ключом выпуска.
//   Сообщение подписи: `"ASDA-UPD-v1\0" + вид + "\0" + payload` (FORMAT_v1 §3). Любой сбой проверки = «ответ
//   недействителен»: окна НЕТ, повтор через интервал (подделать «обновитесь» и закрыть игру игроку нельзя).
//   Ed25519 — OpenSSL 1.1.1t движка (тот же, что у лаунчера).
//
// ═════════ КОГДА ═════════════════════════════════════════════════════════════════════════════════════
//   Первая проверка — через 5 минут после ЗАПУСКА игры, не сразу: лаунчер сверил версию перед самым запуском
//   (без актуальной версии он игру не запускает), так что немедленный запрос лишь продублировал бы его ответ.
//   Дальше — каждые 5 минут от конца прошлой проверки. Запрос асинхронный (FHttpModule), игровой поток не ждёт.
//   Сбой сети / не-200 / таймаут — молча, следующая попытка через тот же интервал.
//   Окно показывается один раз и больше не плодится; найденная новая версия защёлкивается — если окно снесла смена
//   карты, оно возвращается тем же экземпляром (кнопка одна — «Закрыть игру», другого выхода из окна нет, как
//   просил владелец). Пока поднят экран загрузки карты — показ ждёт его снятия.
//
// ═════════ ГДЕ ВЫКЛЮЧЕНО ═══════════════════════════════════════════════════════════════════════════════
//   Редактор/PIE (`GIsEditor`), коммандлеты, нет `state.json` (игра не из лаунчера: dev-сборки, `-game` редактора).
//   Dev-ключи (только вне Shipping, `ASDA_DEV_CVAR`/`ASDA_DEV_CONSOLE`): `asda.Update.IntervalSec` (период, с),
//   `asda.Update.ShowPopup` (принудительный показ окна), `asda.Update.CheckNow` (проверить сейчас).
//   Самотест: `asda.SelfTest.UpdateCheck` (`Private/AsdaUpdateCheckSelfTest.cpp`).
//
// ═════════ ОКНО ══════════════════════════════════════════════════════════════════════════════════════
//   ★ Требование владельца 18.10: окно в стиле ОБЫЧНЫХ модальных окон игры, одна кнопка «Закрыть игру», ввод игрока
//   заблокирован. Поэтому это штатное игровое однокнопочное окно-сообщение `ONEBUT_MSGBOX` 1500 через общую точку
//   `UAsdaWindowSubsystem::ShowGameMessage` (очередь окон-сообщений, звук показа, раскладка текста по ширине 270,
//   подпись кнопки по loc-id). Новых виджетов нет (закон no-ui-regeneration-without-permission).
//   Модальность: окно `bModal` → ввод мира снят (`FInputModeUIOnly` + замок клавиш `AAsdaPlayerController`,
//   `IsAnyModalOpen`); ESC у 1500 съедается и окно не закрывает (таблица ESC оконной подсистемы) — выйти из окна можно
//   только кнопкой. Запись очереди помечена парой (вид 0, loc 991100): второй показ той же пары очередь отбрасывает
//   сама (не плодить), а её исчезновение (сброс очереди сменой карты, чужое программное закрытие) секундный тик видит
//   и ставит окно заново. Новое окно-сообщение поверх нашего — штатная очередь: после ответа на него наше вернётся.
//   Текст — loc 991100, подпись кнопки — loc 991101 (RU: `Tools/data/gametext_overrides.json`,
//   EN: `Tools/data/loc/en/GameText.json`).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "AsdaUpdateCheckSubsystem.generated.h"

class UUserWidget;

// Установленная версия из состояния лаунчера `.asdaupd/state.json`.
struct FAsdaInstalledVersion
{
	FString Channel;          // канал установки (`alpha`); пусто в файле → канал игры лаунчера
	FString Version;          // строка версии (`alpha-1.0.5-10.2026`), только для журнала
	int64 Seq = 0;            // seq канала установленной версии — признак сравнения
	int64 KeyringSeq = 0;     // максимум виденного лаунчером keyring seq — нижняя граница против отката ключей
	bool bValid = false;      // файл прочитан и в нём есть версия
};

// Итог разбора ответа сервера.
enum class EAsdaUpdateVerdict : uint8
{
	Newer,     // на сервере версия с бо́льшим seq — показать окно
	Same,      // seq равен установленному — окна нет
	Older,     // seq меньше установленного (сервер отстал / тестовая установка новее) — окна нет
	Invalid,   // подпись/формат/канал/откат ключей не прошли — окна нет, повтор через интервал
};

// Итог последней проверки (для журнала и самотеста).
enum class EAsdaUpdateCheckResult : uint8
{
	None,        // проверок ещё не было
	NetFail,     // сбой сети / не-200 / таймаут
	Invalid,     // ответ не прошёл проверку
	UpToDate,    // новой версии нет (Same / Older)
	NewVersion,  // найдена новая версия
};

// ★ Проверка конвертов и сравнение версий — чистые функции (без сети и UI), их же гоняет самотест.
namespace AsdaUpdateVerify
{
	// Вшитый корневой ключ (32 байта) — тот же, что у лаунчера (`AstoryClient/Launcher/src/Config.h` kRootPubHex).
	ASDAREBORN_API TArray<uint8> RootPublicKey();
	// id ключа: первые 8 байт SHA-256 открытого ключа, hex нижним регистром (FORMAT_v1 §3).
	ASDAREBORN_API FString KeyId(const TArray<uint8>& Pub);
	// Ed25519: подпись Sig (64 б) сообщения Msg ключом Pub (32 б).
	ASDAREBORN_API bool VerifyEd25519(const TArray<uint8>& Pub, const TArray<uint8>& Sig, const TArray<uint8>& Msg);
	// Открыть конверт вида Kind (`keyring`/`version`): первая действительная подпись доверенным ключом (id → pub),
	//   затем payload как JSON-объект с `format == "asda-<Kind>/1"`. false — OutWhy с причиной.
	ASDAREBORN_API bool OpenEnvelope(const FString& EnvelopeJson, const FString& Kind, const TMap<FString, TArray<uint8>>& Trusted,
		TSharedPtr<class FJsonObject>& OutPayload, FString& OutWhy);
	// Разобрать установленную версию из текста state.json. false — нет версии / не JSON.
	ASDAREBORN_API bool ParseInstalledState(const FString& StateJson, FAsdaInstalledVersion& Out);
	// Полная проверка ответа сервера: keyring корнем RootPub (seq ≥ max(Inst.KeyringSeq, 1)), version ключом выпуска,
	//   канал == канал установки, seq — целое ≥ 0; затем сравнение seq с установленным.
	ASDAREBORN_API EAsdaUpdateVerdict Evaluate(const FString& KeyringJson, const FString& VersionJson, const TArray<uint8>& RootPub,
		const FAsdaInstalledVersion& Inst, FString& OutServerVersion, int64& OutServerSeq, FString& OutWhy);
}

UCLASS()
class ASDAREBORN_API UAsdaUpdateCheckSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Окно-сообщение игры, в котором показывается текст (`ONEBUT_MSGBOX` 1500).
	static const FName kMsgBoxWindowId;
	// loc-строки окна: текст (он же ключ записи в очереди окон-сообщений) и подпись кнопки.
	static constexpr int32 kLocText = 991100;
	static constexpr int32 kLocButton = 991101;
	// Период проверки по умолчанию, с (5 минут — решение владельца).
	static constexpr float kDefaultIntervalSec = 300.f;

	// Проверка включена (не редактор, есть state.json, либо включена самотестом).
	bool IsEnabled() const { return bEnabled; }
	// Найдена новая версия (защёлка до конца процесса).
	bool IsUpdateFound() const { return bUpdateFound; }
	// Запись окна обновления стоит в очереди окон-сообщений (показана или ждёт под другим сообщением).
	bool IsPopupQueued() const;
	// Окно обновления сейчас на экране (верхняя запись очереди).
	bool IsPopupDisplayed() const;
	// Сколько раз окно ставилось в очередь (самотест: «показано один раз»).
	int32 GetPopupShowCount() const { return PopupShowCount; }
	// Итог последней проверки.
	EAsdaUpdateCheckResult GetLastResult() const { return LastResult; }
	// Через сколько секунд следующая проверка (<0 — не запланирована / идёт запрос).
	double GetSecondsToNextCheck() const;
	// Идёт запрос к серверу.
	bool IsRequestInFlight() const { return InFlight > 0; }

	// Запустить проверку немедленно (дев-команда и самотест). false — выключено или запрос уже идёт.
	bool CheckNow();
	// Показать окно обновления (общая точка показа: защёлка + создание окна). bForce — дев-показ без найденной версии.
	void ShowUpdatePopup(bool bForce);

	// ── Только для самотеста ──
	// Включить проверку с заданной установленной версией и адресом сервера (без state.json). Сбрасывает защёлку и окно.
	// TestRoot — корневой ключ, которым подписаны образцы самотеста (пусто — вшитый корень сборки).
	void DebugBeginForTest(const FAsdaInstalledVersion& Inst, const FString& BaseUrl, const TArray<uint8>& TestRoot = TArray<uint8>());
	// Подставить ответ сервера (как будто пришли оба файла; bNetOk=false — сбой сети) — тот же разбор, что у живого ответа.
	void DebugInjectResponses(bool bNetOk, const FString& KeyringJson, const FString& VersionJson);
	// Закрыть окно и вернуть подсистему в состояние до самотеста (выключено, если не было state.json).
	void DebugEndForTest();
	// Виджет окна-сообщения, когда на экране наше окно (для проверки текста и кнопки; кнопку НЕ нажимать — закроет игру).
	UUserWidget* DebugGetPopup() const;

private:
	// Секундный тик: срок проверки, показ/возврат окна после смены карты.
	bool OnTick(float DeltaTime);
	// Прочитать установленную версию из `<RootDir>/.asdaupd/state.json`.
	bool LoadInstalledState();
	// Запустить два GET (keyring.json и version.json).
	void StartRequests();
	// Ответ одного из двух запросов.
	void OnResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnectedOk, bool bKeyring);
	// Оба ответа на руках (или сбой) — разбор и решение.
	void FinishCheck(bool bNetOk, const FString& KeyringJson, const FString& VersionJson);
	// Назначить следующую проверку через интервал.
	void ScheduleNext();
	// Период проверки (дев-ключ или 5 минут).
	float IntervalSec() const;
	// Можно ли показывать окно прямо сейчас (есть вьюпорт, не идёт загрузка карты).
	bool CanShowNow() const;
	// Поставить окно в очередь окон-сообщений (если нашей записи там нет).
	void PushPopup();
	// Клик по «Закрыть игру» (действие OnOk окна-сообщения; окно оно уже закрыло).
	void OnCloseGameClicked();

	bool bEnabled = false;            // проверка работает
	bool bHasStateFile = false;       // state.json найден при старте (для возврата после самотеста)
	bool bUpdateFound = false;        // защёлка «есть новая версия»
	bool bForcedPopup = false;        // окно показано дев-командой без проверки
	bool bExitRequested = false;      // кнопка нажата — окно больше не возвращать
	int32 PopupShowCount = 0;         // сколько раз окно создавалось
	int32 InFlight = 0;               // запросов в полёте (0..2)
	bool bPendingNetOk = true;        // оба запроса пока успешны
	FString PendingKeyring;           // тело keyring.json текущей проверки
	FString PendingVersion;           // тело version.json текущей проверки
	double NextCheckAt = -1.0;        // FPlatformTime::Seconds() следующей проверки; <0 — не назначена
	EAsdaUpdateCheckResult LastResult = EAsdaUpdateCheckResult::None;   // итог последней проверки
	FString FoundVersion;             // версия на сервере, найденная новой
	FString BaseUrl;                  // адрес хранилища обновлений
	FAsdaInstalledVersion Installed;  // установленная версия
	FAsdaInstalledVersion SavedInstalled;  // состояние до самотеста
	FString SavedBaseUrl;             // адрес до самотеста
	TArray<uint8> DebugRoot;          // корень образцов самотеста (пусто — вшитый корень)
	TArray<FHttpRequestPtr> Requests; // запросы текущей проверки (отмена при выходе)
	FTSTicker::FDelegateHandle TickHandle;   // секундный тик
};
