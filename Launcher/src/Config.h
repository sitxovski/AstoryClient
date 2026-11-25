// Copyright (c) 2026 @sitxovski (Discord)
// Config.h — вшитые в лаунчер константы доверия и адреса (FORMAT_v1 §6 п. 1).
// Меняются ТОЛЬКО пересборкой лаунчера. Корневой ключ — hex из <ROOT>/Releases/keys/root.pub (python Tools/asda_release.py
// keys init), подставляется _setup/configure (ASTORY_LAUNCHER_ROOT_PUBKEY). Минимальные seq поднимать при каждой пересборке лаунчера до текущих значений хранилища
// (python Tools/asda_release.py status / keys status) — это защищает первую установку от старого keyring/канала.
#pragma once

namespace asda::config {

// Адрес хранилища обновлений
constexpr const char* kUpdateUrl = "https://@@ASTORY_UPDATE_HOST@@";
// Канал игры и канал самого лаунчера
constexpr const char* kGameChannel = "alpha";
constexpr const char* kLauncherChannel = "launcher";
// Открытый корневой ключ Ed25519 (hex)
constexpr const char* kRootPubHex = "@@ASTORY_LAUNCHER_ROOT_PUBKEY@@";
// Вшитые минимумы: keyring seq, seq канала игры, seq канала лаунчера
constexpr long long kMinKeyringSeq = 1;
constexpr long long kMinGameSeq = 1;       // 17.10.2026: первый боевой выпуск alpha-1.0.0-10.2026 = seq 1
constexpr long long kMinLauncherSeq = 1;   // 17.10.2026: первый выпуск launcher-1.0.0-10.2026 = seq 1
// Подпапка игры рядом с лаунчером, загрузчик EAC внутри неё
constexpr const wchar_t* kGameSubdir = L"Game";
constexpr const wchar_t* kBootstrapper = L"Astory.exe";
// Служба Easy Anti-Cheat (EOS)
constexpr const wchar_t* kEacService = L"EasyAntiCheat_EOS";

// ★ 17.10.2026 Пинг и состояние сервера в верхней полосе окна. Опрашивается GET /status на AsdaId (Caddy → 127.0.0.1:8444):
//   сервер отвечает из кэша (2 с) по таблице слушающих портов, к LoginManager/GameServer НИКТО не подключается.
//   Одно HTTPS-соединение держится открытым (keep-alive), поэтому пинг = время запроса без TLS-рукопожатия.
//   ★ 24.10.2026 (переезд сервера): узел — @@ASTORY_AUTH_HOST@@ (прежний @@ASTORY_DOMAIN@@ смотрит на мёртвый адрес); пины корня
//   ниже (Let's Encrypt) от имени узла не зависят. @@ASTORY_UPDATE_HOST@@ (обновления) — другой хост, не меняется.
constexpr const wchar_t* kStatusHost = L"@@ASTORY_AUTH_HOST@@";   // узел AsdaId (тот же, что выдаёт токены EAC)
constexpr const wchar_t* kStatusPath = L"/status";          // путь проверки
constexpr unsigned kStatusIntervalMs = 2000;                 // период опроса, пока окно не свёрнуто
constexpr unsigned kStatusTimeoutMs = 2500;                  // предел каждого этапа запроса (DNS, TCP, отправка, ответ)
constexpr int kPingGoodMs = 100;                             // меньше — зелёный
constexpr int kPingFairMs = 200;                             // меньше — жёлтый, иначе красный

// ★ 19.10.2026 Выгрузка запечатанных журналов прошлых игровых сессий (Reports/release_logs_2026-10-19/00_SPEC.md):
//   POST https://@@ASTORY_AUTH_HOST@@/api/v1/ingest, конверт ASLU версии 2; только этот узел, TLS 1.2+ с проверкой сертификата.
constexpr const wchar_t* kIngestHost = L"@@ASTORY_AUTH_HOST@@";    // приёмник (тот же узел AsdaId, что и /status)
constexpr const wchar_t* kIngestPath = L"/api/v1/ingest";    // путь приёмника (Caddy проксирует только POST сюда)
// ★ ревизия В3: единый срок на файл — лаунчер 50 с < служба 55 с (BodyTimeoutMs) < Caddy 60/70 с
constexpr unsigned kIngestTimeoutMs = 50000;                 // предел запроса одного файла (DNS+TCP+TLS+тело+ответ)
// ★ ревизия В5: пиннинг — корень цепочки сервера обязан быть одним из этих (SHA-256 DER SubjectPublicKeyInfo, hex):
//   ISRG Root X1, ISRG Root X2 (в хранилище Windows) и ISRG Root YE (сейчас перекрёстно подписан X2, цепочка сервера
//   19.10.2026: YE1 → Root YE → X2 → X1; когда Root YE попадёт в хранилище Windows, цепочка закончится на нём).
//   Иначе тело не отправляется (повтор при следующем запуске). Смена УЦ сервера (не Let's Encrypt) = пересборка лаунчера.
constexpr const char* kIngestRootSpki[] = {
    "0b9fa5a59eed715c26c1020c711b4f6ec42d58b0015e14337a39dad301c5afc3",   // ISRG Root X1
    "762195c225586ee6c0237456e2107dc54f1efc21f61a792ebd515913cce68332",   // ISRG Root X2
    "b0292ae545978e0fbb98abbd94c861605e5b18bb32ed523f50d5b7b5c71d47bc",   // ISRG Root YE
};
// keyId (hex, 8 б) ключей журналов, которые принимает приёмник: = Tools/data/logseal/current.txt
//   (сверяет самотест tools/log_upload_selftest.py). Файл с другим keyId — сразу в Uploaded\rejected без отправки.
//   ★ Новый ключ (asda_logseal.py keygen --force): дописать сюда и выпустить лаунчер ДО игры с новым ключом.
//   Подставляет _setup/configure (ASTORY_LOGSEAL_KEY_ID) после `python Tools/asda_logseal.py keygen`. Пока здесь
//   заглушка или пусто — лаунчер собирается, но ни один журнал не считается своим (выгрузка журналов ничего не шлёт).
constexpr const char* kLogSealKeyIds[] = {"@@ASTORY_LOGSEAL_KEY_ID@@"};
constexpr unsigned kIngestMaxPerHour = 6;                    // запросов за скользящий час (лимит приёмника на IP и install)
constexpr unsigned kIngestMaxPerRun = 4;                     // запросов (= файлов) за один запуск лаунчера
constexpr const wchar_t* kGameSavedSubdir = L"AsdaReborn\\Saved";   // папка Saved игры под %LOCALAPPDATA%
constexpr const wchar_t* kLauncherDataSubdir = L"AStoryRemake";      // данные лаунчера под %LOCALAPPDATA% (launcher.log, install.id)

}  // namespace asda::config
