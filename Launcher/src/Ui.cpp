// Copyright (c) 2026 @sitxovski (Discord)
// Ui.cpp — окно лаунчера (см. Ui.h).
//
// Поток работы (фоновый поток, окно только рисует снимок состояния):
//   1) самообновление лаунчера (канал launcher; ошибка не фатальна — работаем старой версией);
//   2) папка игры доступна на запись (иначе — предложение перезапуститься с правами администратора);
//   3) обновление игры (Update: проверка по кэшу хэшей, докачка только изменившихся кусков) → «Играть»;
//   сервер недоступен или отказ проверки — «Играть» недоступна (решение владельца 16.10: без сервера не пускать).
// «Играть»: служба Easy Anti-Cheat (при отсутствии — установка через UAC) → Astory.exe → лаунчер закрывается.
// Стиль — игра: размытый город и логотип AStory Remake (RawAssets/Textures/map/load/Start_logo.png, JUI/n_03.png),
// палитра золото/тёмно-коричневый/кремовый.
#include "Ui.h"

#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "Config.h"
#include "FileSys.h"
#include "Format.h"
#include "Http.h"
#include "Launch.h"
#include "LogUpload.h"
#include "Ping.h"
#include "SelfUpdate.h"
#include "State.h"
#include "Updater.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

namespace G = Gdiplus;

namespace asda {

namespace {

// Ресурсы (res/launcher.rc)
constexpr int kResBg = 101, kResLogo = 102, kResIcon = 1;
// Логический размер окна (px при 96 dpi)
constexpr float kW = 900.f, kH = 520.f;
// Сообщения окна
constexpr UINT WM_APP_REFRESH = WM_APP + 1, WM_APP_QUIT = WM_APP + 2;
constexpr UINT_PTR kTimerAnim = 1;

// Палитра игры
const G::Color kGold(255, 246, 201, 92), kGoldDark(255, 201, 150, 43), kBrown(255, 58, 36, 14), kCream(255, 255, 241, 208),
    kMuted(255, 214, 199, 168), kPanel(205, 14, 10, 6), kTrack(190, 0, 0, 0), kTrackEdge(255, 96, 72, 40),
    kErr(255, 255, 140, 110);
// ★ 17.10.2026 Индикатор сервера: зелёный / жёлтый / красный (пинг < kPingGoodMs / < kPingFairMs / больше; «Офлайн» — красный)
const G::Color kNetGood(255, 104, 214, 122), kNetFair(255, 242, 196, 72), kNetBad(255, 236, 92, 76),
    kNetDim(255, 120, 108, 88);

// ── строки RU/EN ──────────────────────────────────────────────────────────────────────────────────────────────
enum class Lang { Ru, En };
enum Str {
    sTitle, sPlay, sUpdating, sRetry, sCheckFiles, sCancel, sStarting, sSelfUpd, sConnect, sChecking, sDownloading,
    sFinishing, sReady, sCancelled, sErrPrefix, sNoServer, sInstalled, sNotInstalled, sAvailable, sEac, sLaunching,
    sNeedAdmin, sAdminBtn, sGameRunning, sNoRelease, sSrvOnline, sSrvOffline, sSrvChecking, sMs, sCount
};
const char* kStrings[sCount][2] = {
    {"AStory Remake", "AStory Remake"},
    {"ИГРАТЬ", "PLAY"},
    {"ОБНОВЛЕНИЕ", "UPDATING"},
    {"ПОВТОРИТЬ", "RETRY"},
    {"Проверить файлы", "Verify files"},
    {"Отмена", "Cancel"},
    {"Запуск…", "Starting…"},
    {"Обновление лаунчера…", "Updating launcher…"},
    {"Связь с сервером обновлений…", "Contacting update server…"},
    {"Проверка файлов игры…", "Checking game files…"},
    {"Загрузка обновления…", "Downloading update…"},
    {"Завершение…", "Finishing…"},
    {"Игра готова к запуску", "Game is ready"},
    {"Обновление отменено — завершите его, чтобы играть", "Update cancelled — finish it to play"},
    {"Ошибка: ", "Error: "},
    {"Сервер обновлений недоступен. Проверьте интернет и повторите.", "Update server unavailable. Check your connection and retry."},
    {"Установлено: ", "Installed: "},
    {"Игра не установлена", "Game not installed"},
    {"Доступно: ", "Available: "},
    {"Установка Easy Anti-Cheat (нужны права администратора)…", "Installing Easy Anti-Cheat (administrator rights required)…"},
    {"Запуск игры…", "Launching game…"},
    {"Нет прав на запись в папку игры", "No write access to the game folder"},
    {"ОТ ИМЕНИ АДМИНА", "RUN AS ADMIN"},
    {"Игра запущена — закройте её, чтобы обновить", "Game is running — close it to update"},
    {"На сервере обновлений ещё нет версии игры. Повторите позже.", "No game version on the update server yet. Try again later."},
    {"Онлайн", "Online"},
    {"Офлайн", "Offline"},
    {"Проверка…", "Checking…"},
    {"мс", "ms"},
};

// Состояние окна
enum class UiState { Starting, Busy, Ready, Error, NeedAdmin, Launching };

// Снимок для отрисовки (пишет фоновый поток под мьютексом)
struct Snapshot {
    UiState state = UiState::Starting;
    Str status = sStarting;
    std::string detail;          // доп. строка (ошибка, объём)
    double progress = -1;        // 0..1, <0 — не показывать
    uint64_t done = 0, total = 0;            // ход фазы; ★ 19.10.2026 в загрузке — байты СЕТИ (total = план загрузки)
    uint64_t localDone = 0, localTotal = 0;  // ★ 19.10.2026 загрузка: сборка из своих файлов (копирование с диска)
    std::string installed, available;
    bool cancellable = false;
};

// Кнопка окна
struct Button {
    G::RectF r;
    bool hover = false, down = false, enabled = true, visible = true;
};

// Ресурс → GDI+ Bitmap
G::Bitmap* LoadPng(HINSTANCE inst, int id) {
    HRSRC rs = FindResourceW(inst, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!rs) return nullptr;
    DWORD sz = SizeofResource(inst, rs);
    const void* data = LockResource(LoadResource(inst, rs));
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sz);
    std::memcpy(GlobalLock(mem), data, sz);
    GlobalUnlock(mem);
    IStream* st = nullptr;
    CreateStreamOnHGlobal(mem, TRUE, &st);
    G::Bitmap* b = G::Bitmap::FromStream(st);
    st->Release();
    return b;
}

// Скруглённый прямоугольник
void RoundRect(G::GraphicsPath& p, const G::RectF& r, float rad) {
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

// Объём в МБ/ГБ
std::string HumanBytes(uint64_t b, Lang l) {
    if (b >= (1ull << 30)) return Fmt(l == Lang::Ru ? "%.2f ГБ" : "%.2f GB", b / 1073741824.0);
    return Fmt(l == Lang::Ru ? "%.1f МБ" : "%.1f MB", b / 1048576.0);
}

// ★ 19.10.2026 Строка подробностей загрузки (окно и превью — одна точка): «X из Y · скорость · ~время» по байтам СЕТИ
//   (Y — план загрузки: только недостающие куски, а не размер изменённых файлов), плюс ход сборки из своих файлов, пока она
//   идёт. Время — по скорости сети на оставшиеся байты сети. netRate < 0 — скорость ещё неизвестна.
std::string DownloadDetail(uint64_t done, uint64_t total, uint64_t localDone, uint64_t localTotal, double netRate, Lang l) {
    const bool ru = l == Lang::Ru;
    std::string d;
    if (total) {
        d = HumanBytes(done, l) + (ru ? " из " : " of ") + HumanBytes(total, l);   // статус над строкой уже «Загрузка обновления»
        if (netRate >= 0) d += Fmt(ru ? "  ·  %.1f МБ/с" : "  ·  %.1f MB/s", netRate / 1048576.0);
        if (netRate > 1 && done < total) {
            int left = (int)((total - done) / netRate);
            d += left >= 60 ? Fmt(ru ? "  ·  ~%d мин" : "  ·  ~%d min", (left + 59) / 60) : Fmt(ru ? "  ·  ~%d с" : "  ·  ~%d s", left);
        }
    }
    if (localTotal && localDone < localTotal) {
        if (!d.empty()) d += "  ·  ";
        d += (ru ? "сборка из своих файлов " : "reusing local files ") + HumanBytes(localDone, l) + (ru ? " из " : " of ") +
             HumanBytes(localTotal, l);
    }
    return d;
}

// Доля полосы загрузки: по сети; если качать нечего — по сборке из своих файлов
double DownloadProgress(uint64_t done, uint64_t total, uint64_t localDone, uint64_t localTotal) {
    if (total) return (double)done / total;
    if (localTotal) return (double)localDone / localTotal;
    return 1.0;
}

// Журнал лаунчера: %LOCALAPPDATA%\AStoryRemake\launcher.log (до 1 МБ, затем .1)
void LogFile(const std::string& line) {
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    PWSTR base = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base) != S_OK) return;
    std::wstring dir = std::wstring(base) + L"\\AStoryRemake";
    CoTaskMemFree(base);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring p = dir + L"\\launcher.log";
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d) && d.nFileSizeLow > (1u << 20))
        MoveFileExW(p.c_str(), (p + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
    HANDLE f = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::string s = Fmt("%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond) + line + "\r\n";
    DWORD w = 0;
    WriteFile(f, s.data(), (DWORD)s.size(), &w, nullptr);
    CloseHandle(f);
}

// Язык: сохранённый в HKCU или по языку Windows
Lang LoadLang() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\AStoryRemake\\Launcher", L"Lang", RRF_RT_REG_DWORD, nullptr, &v, &sz) ==
        ERROR_SUCCESS)
        return v ? Lang::En : Lang::Ru;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? Lang::Ru : Lang::En;
}

void SaveLang(Lang l) {
    DWORD v = l == Lang::En;
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\AStoryRemake\\Launcher", L"Lang", REG_DWORD, &v, sizeof(v));
}

// ── окно ──────────────────────────────────────────────────────────────────────────────────────────────────────
class Window {
public:
    explicit Window(HINSTANCE inst) : inst_(inst) {
        bg_.reset(LoadPng(inst, kResBg));
        logo_.reset(LoadPng(inst, kResLogo));
        lang_ = LoadLang();
    }

    // Отрисовка в контекст GDI+ (логические единицы; масштаб — снаружи)
    void Paint(G::Graphics& g) {
        Snapshot s;
        ServerStatus net;
        {
            std::lock_guard<std::mutex> lk(m_);
            s = snap_;
            net = net_;
        }
        g.SetSmoothingMode(G::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);
        g.SetInterpolationMode(G::InterpolationModeHighQualityBicubic);
        if (bg_) g.DrawImage(bg_.get(), G::RectF(0, 0, kW, kH));
        // верхняя полоса
        G::LinearGradientBrush top(G::PointF(0, 0), G::PointF(0, 48), G::Color(150, 0, 0, 0), G::Color(0, 0, 0, 0));
        g.FillRectangle(&top, 0.f, 0.f, kW, 48.f);
        G::FontFamily ff(L"Arial");
        G::Font fTitle(&ff, 14, G::FontStyleBold, G::UnitPixel), fStatus(&ff, 16, G::FontStyleBold, G::UnitPixel),
            fSmall(&ff, 12.5f, G::FontStyleRegular, G::UnitPixel), fBtn(&ff, 22, G::FontStyleBold, G::UnitPixel),
            fLink(&ff, 12.5f, G::FontStyleBold, G::UnitPixel);
        G::SolidBrush cream(kCream), muted(kMuted), gold(kGold), err(kErr);
        g.DrawString(Widen(T(sTitle)).c_str(), -1, &fTitle, G::PointF(18, 14), &cream);
        // RU · EN
        langRu_.r = G::RectF(kW - 160, 10, 30, 24);
        langEn_.r = G::RectF(kW - 124, 10, 30, 24);
        G::StringFormat center;
        center.SetAlignment(G::StringAlignmentCenter);
        center.SetLineAlignment(G::StringAlignmentCenter);
        G::SolidBrush dim(G::Color(170, 214, 199, 168));
        g.DrawString(L"RU", -1, &fLink, langRu_.r, &center, lang_ == Lang::Ru ? &gold : (langRu_.hover ? &cream : &dim));
        g.DrawString(L"EN", -1, &fLink, langEn_.r, &center, lang_ == Lang::En ? &gold : (langEn_.hover ? &cream : &dim));
        PaintServer(g, net, langRu_.r.X - 12);
        // свернуть / закрыть
        minBtn_.r = G::RectF(kW - 84, 6, 36, 30);
        closeBtn_.r = G::RectF(kW - 44, 6, 36, 30);
        for (Button* b : {&minBtn_, &closeBtn_}) {
            if (b->hover) {
                G::SolidBrush hb(b == &closeBtn_ ? G::Color(200, 170, 40, 30) : G::Color(60, 255, 255, 255));
                g.FillRectangle(&hb, b->r);
            }
        }
        G::Pen pw(kCream, 1.6f);
        float cx = minBtn_.r.X + 18, cy = minBtn_.r.Y + 15;
        g.DrawLine(&pw, cx - 6, cy + 1, cx + 6, cy + 1);
        cx = closeBtn_.r.X + 18;
        g.DrawLine(&pw, cx - 5, cy - 5, cx + 5, cy + 5);
        g.DrawLine(&pw, cx - 5, cy + 5, cx + 5, cy - 5);
        // логотип
        if (logo_) {
            float lw = 320, lh = lw * logo_->GetHeight() / logo_->GetWidth();
            g.DrawImage(logo_.get(), G::RectF((kW - lw) / 2, 58, lw, lh));
        }
        // нижняя панель
        float py = kH - 152;
        G::SolidBrush panel(kPanel);
        g.FillRectangle(&panel, 0.f, py, kW, kH - py);
        G::LinearGradientBrush line(G::PointF(0, 0), G::PointF(kW, 0), G::Color(0, 201, 150, 43), kGoldDark);
        G::Color stops[3] = {G::Color(0, 201, 150, 43), kGold, G::Color(0, 201, 150, 43)};
        float pos[3] = {0.f, 0.5f, 1.f};
        line.SetInterpolationColors(stops, pos, 3);
        g.FillRectangle(&line, 0.f, py, kW, 2.f);
        // статус и подробности
        float lx = 36, rw = kW - lx - 36 - 250;
        std::string status = T(s.status);
        if (s.state == UiState::Busy && s.status == sDownloading && (s.total || s.localTotal)) {
            status += Fmt("  %d%%", (int)(s.progress * 100 + 0.5));
        }
        g.DrawString(Widen(status).c_str(), -1, &fStatus, G::RectF(lx, py + 20, rw, 22), nullptr, &cream);
        G::RectF detailR(lx, py + 44, rw, 34);
        g.DrawString(Widen(s.detail).c_str(), -1, &fSmall, detailR, nullptr,
                     s.state == UiState::Error || s.state == UiState::NeedAdmin ? &err : &muted);
        // полоса прогресса
        G::RectF track(lx, py + 80, rw, 16);
        G::GraphicsPath tp;
        RoundRect(tp, track, 8);
        G::SolidBrush tb(kTrack);
        g.FillPath(&tb, &tp);
        if (s.progress >= 0) {
            float fw = std::max(16.f, track.Width * (float)std::min(1.0, s.progress));
            G::RectF fill(track.X, track.Y, fw, track.Height);
            G::GraphicsPath fp;
            RoundRect(fp, fill, 8);
            G::LinearGradientBrush fb(G::PointF(0, track.Y), G::PointF(0, track.Y + track.Height), kGold, kGoldDark);
            g.FillPath(&fb, &fp);
        }
        G::Pen te(kTrackEdge, 1.f);
        g.DrawPath(&te, &tp);
        // нижняя строка: версия + «Проверить файлы»
        std::string ver = s.installed.empty() ? T(sNotInstalled) : T(sInstalled) + s.installed;
        if (!s.available.empty() && s.available != s.installed) ver += "   ·   " + T(sAvailable) + s.available;
        g.DrawString(Widen(ver).c_str(), -1, &fSmall, G::PointF(lx, py + 112), &muted);
        G::RectF vb;
        g.MeasureString(Widen(T(sCheckFiles)).c_str(), -1, &fLink, G::PointF(0, 0), &vb);
        verifyBtn_.r = G::RectF(lx + rw - vb.Width, py + 110, vb.Width, 18);
        verifyBtn_.visible = s.state == UiState::Ready || s.state == UiState::Error;
        if (verifyBtn_.visible) {
            g.DrawString(Widen(T(sCheckFiles)).c_str(), -1, &fLink, G::PointF(verifyBtn_.r.X, verifyBtn_.r.Y),
                         verifyBtn_.hover ? &cream : &gold);
            if (verifyBtn_.hover) {
                G::Pen ul(kCream, 1.f);
                g.DrawLine(&ul, verifyBtn_.r.X, verifyBtn_.r.Y + 16, verifyBtn_.r.X + verifyBtn_.r.Width, verifyBtn_.r.Y + 16);
            }
        }
        // большая кнопка
        playBtn_.r = G::RectF(kW - 36 - 230, py + 26, 230, 70);
        Str label = sPlay;
        playBtn_.enabled = s.state == UiState::Ready || s.state == UiState::Error || s.state == UiState::NeedAdmin;
        if (s.state == UiState::Error) label = sRetry;
        else if (s.state == UiState::NeedAdmin) label = sAdminBtn;
        else if (s.state == UiState::Busy || s.state == UiState::Starting) label = sUpdating;
        else if (s.state == UiState::Launching) label = sLaunching;
        G::GraphicsPath bp;
        RoundRect(bp, playBtn_.r, 10);
        if (playBtn_.enabled) {
            G::Color c1 = playBtn_.down ? kGoldDark : (playBtn_.hover ? G::Color(255, 255, 222, 128) : kGold);
            G::Color c2 = playBtn_.down ? G::Color(255, 150, 105, 25) : kGoldDark;
            G::LinearGradientBrush b(G::PointF(0, playBtn_.r.Y), G::PointF(0, playBtn_.r.Y + playBtn_.r.Height), c1, c2);
            g.FillPath(&b, &bp);
            G::Pen edge(kBrown, 2.f);
            g.DrawPath(&edge, &bp);
            G::SolidBrush tbr(kBrown);
            g.DrawString(Widen(T(label)).c_str(), -1, label == sAdminBtn ? &fStatus : &fBtn, playBtn_.r, &center, &tbr);
        } else {
            G::SolidBrush b(G::Color(200, 70, 58, 40));
            g.FillPath(&b, &bp);
            G::Pen edge(G::Color(255, 110, 92, 62), 1.5f);
            g.DrawPath(&edge, &bp);
            G::SolidBrush tbr(G::Color(255, 170, 150, 120));
            g.DrawString(Widen(T(label)).c_str(), -1, &fStatus, playBtn_.r, &center, &tbr);
        }
        // «Отмена» под кнопкой во время загрузки
        cancelBtn_.visible = s.cancellable;
        cancelBtn_.r = G::RectF(playBtn_.r.X, py + 104, playBtn_.r.Width, 22);
        if (cancelBtn_.visible)
            g.DrawString(Widen(T(sCancel)).c_str(), -1, &fLink, cancelBtn_.r, &center, cancelBtn_.hover ? &cream : &muted);
    }

    // ★ 17.10.2026 Плашка сервера в верхней полосе (правым краем к right): точка состояния · «Онлайн/Офлайн» │ шкала · пинг.
    void PaintServer(G::Graphics& g, const ServerStatus& net, float right) {
        G::FontFamily ff(L"Arial");
        G::Font fState(&ff, 12.5f, G::FontStyleBold, G::UnitPixel), fPing(&ff, 12.5f, G::FontStyleRegular, G::UnitPixel);
        bool online = net.state == ServerState::Online, offline = net.state == ServerState::Offline;
        bool hasPing = online && net.pingMs >= 0;
        G::Color stateC = online ? kNetGood : offline ? kNetBad : kNetDim;
        G::Color pingC = !hasPing ? kNetDim
                         : net.pingMs < config::kPingGoodMs ? kNetGood
                         : net.pingMs < config::kPingFairMs ? kNetFair
                                                             : kNetBad;
        int bars = !hasPing ? 0 : net.pingMs < config::kPingGoodMs ? 3 : net.pingMs < config::kPingFairMs ? 2 : 1;
        std::wstring stateW = Widen(T(online ? sSrvOnline : offline ? sSrvOffline : sSrvChecking));
        std::wstring pingW = hasPing ? Widen(Fmt("%d ", std::min(net.pingMs, 9999)) + T(sMs)) : Widen("— " + T(sMs));
        G::RectF ms1, ms2, wide;
        g.MeasureString(stateW.c_str(), -1, &fState, G::PointF(0, 0), &ms1);
        g.MeasureString(pingW.c_str(), -1, &fPing, G::PointF(0, 0), &ms2);
        // ширина под пинг — не меньше «999 мс», чтобы плашка не прыгала при смене чисел
        g.MeasureString(Widen("999 " + T(sMs)).c_str(), -1, &fPing, G::PointF(0, 0), &wide);
        const float h = 24, pad = 10, dot = 8, gap = 6, barsW = 13, sepW = 13;
        float pingSlot = std::max(ms2.Width, wide.Width);
        float w = pad + dot + gap + ms1.Width + sepW + barsW + gap + pingSlot + pad - 4;
        G::RectF pill(right - w, 10, w, h);
        G::GraphicsPath pp;
        RoundRect(pp, pill, h / 2);
        G::SolidBrush pb(G::Color(140, 10, 7, 4));
        g.FillPath(&pb, &pp);
        G::Pen pe(G::Color(150, 201, 150, 43), 1.f);
        g.DrawPath(&pe, &pp);
        float x = pill.X + pad, cy = pill.Y + h / 2;
        // точка состояния с мягким ореолом
        G::SolidBrush halo(G::Color(70, stateC.GetR(), stateC.GetG(), stateC.GetB()));
        g.FillEllipse(&halo, x - 3, cy - dot / 2 - 3, dot + 6, dot + 6);
        G::SolidBrush db(stateC);
        g.FillEllipse(&db, x, cy - dot / 2, dot, dot);
        x += dot + gap;
        G::SolidBrush sb(online || offline ? stateC : kMuted);
        g.DrawString(stateW.c_str(), -1, &fState, G::PointF(x - 2, cy - ms1.Height / 2), &sb);
        x += ms1.Width;
        // разделитель
        G::Pen sp(G::Color(110, 214, 199, 168), 1.f);
        g.DrawLine(&sp, x + sepW / 2 - 2, cy - 7, x + sepW / 2 - 2, cy + 7);
        x += sepW;
        // шкала из трёх столбиков: горит столько, сколько «делений» у пинга
        for (int i = 0; i < 3; ++i) {
            float bh = 5.f + 3.f * i;
            G::SolidBrush bb(i < bars ? pingC : G::Color(110, 120, 108, 88));
            g.FillRectangle(&bb, x + i * 5.f, cy + 6 - bh, 3.f, bh);
        }
        x += barsW + gap;
        G::SolidBrush pbr(hasPing ? pingC : kMuted);
        g.DrawString(pingW.c_str(), -1, &fPing, G::PointF(x - 2, cy - ms2.Height / 2), &pbr);
    }

    // ★ 17.10.2026 Новый результат опроса сервера (поток опроса) → перерисовка
    void SetNet(const ServerStatus& st) {
        {
            std::lock_guard<std::mutex> lk(m_);
            net_ = st;
        }
        if (hwnd_) PostMessageW(hwnd_, WM_APP_REFRESH, 0, 0);
    }

    // Обновить снимок (из любого потока) и попросить перерисовку
    void Set(const std::function<void(Snapshot&)>& f) {
        {
            std::lock_guard<std::mutex> lk(m_);
            f(snap_);
        }
        if (hwnd_) PostMessageW(hwnd_, WM_APP_REFRESH, 0, 0);
    }

    // Текст на текущем языке
    std::string T(Str id) const { return kStrings[id][lang_ == Lang::En ? 1 : 0]; }
    // Язык без сохранения (превью)
    void SetLang(Lang l) { lang_ = l; }

    // Создать окно и запустить цикл
    int Run(const std::wstring& args) {
        args_ = args;
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = &Window::Proc;
        wc.hInstance = inst_;
        wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(kResIcon));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"AsdaLauncherWindow";
        RegisterClassExW(&wc);
        UINT dpi = GetDpiForSystem();
        scale_ = dpi / 96.f;
        int w = (int)(kW * scale_), h = (int)(kH * scale_);
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"AStory Remake", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                                wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h, nullptr,
                                nullptr, inst_, this);
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        ShowWindow(hwnd_, SW_SHOW);
        SetTimer(hwnd_, kTimerAnim, 500, nullptr);
        probe_.Start();   // ★ 17.10.2026 пинг и состояние сервера (Ping.h)
        // ★ 19.10.2026 журналы прошлых игровых сессий → наш сервер (LogUpload.h): свой поток, окно и «Играть» его не ждут
        uploader_.Start(DefaultLogUploadOptions([](const std::string& l) { LogFile(l); }));
        StartFlow(false);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Shutdown();
        return 0;
    }

    // Установленная версия игры из локального состояния (без сети): пусто — игры нет или установка не завершена.
    // ★ 17.10.2026 (владелец: «при повторном запуске, пока идёт проверка обновления лаунчера, пишет что игра не
    //   установлена»): раньше версия читалась только после самообновления и на экран попадала после ответа сервера.
    //   Папку игры не создаём: нет папки — значит, игры нет.
    static std::string ReadInstalledVersion() {
        std::wstring game = GameDir();
        DWORD a = GetFileAttributesW(game.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return "";
        try {
            State st(RootPath(game), nullptr);
            return st.pending ? "" : st.version;
        } catch (...) {
            return "";
        }
    }

    // Фоновая работа: самообновление → права → обновление игры
    void StartFlow(bool repair) {
        if (worker_.joinable()) worker_.join();
        cancel_ = false;
        installed_ = ReadInstalledVersion();   // ★ 17.10.2026: версия на экране сразу, до проверки самообновления
        Set([&](Snapshot& s) {
            s = Snapshot{};
            s.state = UiState::Busy;
            s.status = sConnect;
            s.installed = installed_;
        });
        workerDone_ = false;
        worker_ = std::thread([this, repair] {
            Flow(repair);
            workerDone_ = true;
        });
    }

    void Flow(bool repair) {
        UpdateOptions o;
        Bytes rp;
        // Корневой ключ не задан (заглушка configure) или повреждён — понятная ошибка вместо чтения пустого буфера.
        if (!FromHex(config::kRootPubHex, rp) || rp.size() != o.rootPub.size()) {
            LogFile("ошибка: корневой ключ обновлений в Config.h не задан или неверен (нужно 64 hex-символа) — "
                    "python Tools/asda_release.py keys init, ASTORY_LAUNCHER_ROOT_PUBKEY в _setup/configure.bat, пересборка лаунчера");
            Set([&](Snapshot& s) {
                s.state = UiState::Error;
                s.status = sErrPrefix;
                s.detail = lang_ == Lang::Ru ? "лаунчер собран без корневого ключа обновлений (Config.h kRootPubHex)"
                                             : "launcher was built without the update root key (Config.h kRootPubHex)";
                s.cancellable = false;
                s.progress = -1;
            });
            return;
        }
        std::memcpy(o.rootPub.data(), rp.data(), o.rootPub.size());
        o.channel = config::kGameChannel;
        o.minKeyringSeq = config::kMinKeyringSeq;
        o.minSeq = config::kMinGameSeq;
        o.repair = repair;
        o.log = [](const std::string& l) { LogFile(l); };
        o.progress.cancel = &cancel_;
        try {
            if (GameRunning()) {
                Set([&](Snapshot& s) {
                    s.state = UiState::Error;
                    s.status = sGameRunning;
                    s.detail.clear();
                    s.progress = -1;
                });
                return;
            }
            std::unique_ptr<Source> src = MakeHttpSource(config::kUpdateUrl);
            if (!repair && args_.find(L"--after-selfupdate") == std::wstring::npos) {
                Set([&](Snapshot& s) { s.status = sSelfUpd; });
                try {
                    if (SelfUpdate(*src, o, L"--after-selfupdate") == SelfUpdateResult::Restarting) {
                        LogFile("самообновление: новая версия запущена");
                        PostMessageW(hwnd_, WM_APP_QUIT, 0, 0);
                        return;
                    }
                } catch (const UpdateError& e) {
                    LogFile(std::string("самообновление не удалось (продолжаю): ") + e.what());
                }
            }
            std::wstring game = GameDir();
            if (!IsWritableDir(game)) {
                Set([&](Snapshot& s) {
                    s.state = UiState::NeedAdmin;
                    s.status = sNeedAdmin;
                    s.detail = Narrow(game);
                    s.progress = -1;
                });
                return;
            }
            {
                State st(RootPath(game), nullptr);
                installed_ = st.pending ? "" : st.version;
            }
            Set([&](Snapshot& s) { s.installed = installed_; });   // ★ 17.10.2026: сразу на экран, не ждать сервера
            o.progress.phase = [this](Phase p, const std::string&) {
                Set([&](Snapshot& s) {
                    s.status = p == Phase::Connect ? sConnect : p == Phase::Check ? sChecking : p == Phase::Download ? sDownloading : sFinishing;
                    s.cancellable = p == Phase::Check || p == Phase::Download;
                    s.progress = p == Phase::Connect ? -1 : 0;
                    s.done = s.total = 0;
                    s.localDone = s.localTotal = 0;
                });
                phaseStart_ = GetTickCount64();
                Source* sp = srcPtr_.load();
                dlStart_ = sp ? sp->downloaded.load() : 0;
            };
            o.progress.bytes = [this](uint64_t done, uint64_t total) {
                Set([&](Snapshot& s) {
                    s.done = done;
                    s.total = total;
                    s.progress = s.status == sDownloading ? DownloadProgress(done, total, s.localDone, s.localTotal)
                                                          : (total ? (double)done / total : 1.0);
                });
            };
            // ★ 19.10.2026: ход сборки из своих файлов (фаза загрузки) — отдельно от сети
            o.progress.assemble = [this](uint64_t done, uint64_t total) {
                Set([&](Snapshot& s) {
                    s.localDone = done;
                    s.localTotal = total;
                    s.progress = DownloadProgress(s.done, s.total, done, total);
                });
            };
            srcPtr_ = src.get();
            RemoteInfo ri = CheckRemote(*src, game, o);
            Set([&](Snapshot& s) { s.available = ri.version; });
            UpdateStats st = Update(*src, game, o);
            srcPtr_ = nullptr;
            installed_ = st.version;
            Set([&](Snapshot& s) {
                s.state = UiState::Ready;
                s.status = sReady;
                s.detail = st.changed ? Fmt(lang_ == Lang::Ru ? "Обновлено файлов: %llu, загружено %s" : "Files updated: %llu, downloaded %s",
                                            (unsigned long long)st.changed, HumanBytes(st.downloaded, lang_).c_str())
                                      : "";
                s.progress = 1;
                s.cancellable = false;
                s.installed = st.version;
                s.available = st.version;
            });
        } catch (const Cancelled&) {
            srcPtr_ = nullptr;
            LogFile("обновление отменено пользователем");
            Set([&](Snapshot& s) {
                s.state = UiState::Error;
                s.status = sCancelled;
                s.detail.clear();
                s.cancellable = false;
            });
        } catch (const std::exception& e) {
            srcPtr_ = nullptr;
            std::string m = e.what();
            LogFile("ошибка: " + m);
            bool noRelease = m.find("HTTP 404") != std::string::npos;          // сервер жив, выпуска нет
            bool net = !noRelease && (m.find("сеть") != std::string::npos || m.find("HTTP") != std::string::npos ||
                                      m.find("WinHTTP") != std::string::npos);
            Set([&](Snapshot& s) {
                s.state = UiState::Error;
                s.status = sErrPrefix;
                s.detail = noRelease ? T(sNoRelease) : net ? T(sNoServer) : m;
                s.cancellable = false;
                s.progress = -1;
            });
        }
    }

    // «Играть»: EAC → игра → выход
    void Play() {
        if (worker_.joinable()) worker_.join();
        Set([&](Snapshot& s) {
            s.state = UiState::Launching;
            s.status = sLaunching;
            s.detail.clear();
        });
        workerDone_ = false;
        worker_ = std::thread([this] {
            struct Done {
                std::atomic<bool>& d;
                ~Done() { d = true; }
            } done{workerDone_};
            try {
                std::wstring game = GameDir();
                if (!IsReady(game)) throw UpdateError(T(sCancelled));
                if (!EacServiceInstalled()) {
                    Set([&](Snapshot& s) { s.status = sEac; });
                    InstallEacService(game);
                }
                StartGame(game);
                LogFile("игра запущена");
                PostMessageW(hwnd_, WM_APP_QUIT, 0, 0);
            } catch (const std::exception& e) {
                LogFile(std::string("запуск: ") + e.what());
                Set([&](Snapshot& s) {
                    s.state = UiState::Ready;
                    s.status = sErrPrefix;
                    s.detail = e.what();
                });
            }
        });
    }

    // Объём, скорость и оставшееся время для строки подробностей (раз в 0,5 с)
    void UpdateRate() {
        Source* src = srcPtr_.load();
        std::lock_guard<std::mutex> lk(m_);
        if (snap_.state != UiState::Busy || (!snap_.total && !snap_.localTotal)) return;
        double sec = std::max(0.5, (GetTickCount64() - phaseStart_) / 1000.0);
        std::string d;
        if (snap_.status == sDownloading) {
            // ★ 19.10.2026: и объём, и скорость, и оставшееся время — по байтам СЕТИ (раньше время считалось по «готовым»
            //   байтам вместе с копированием своих кусков и прыгало)
            double net = src ? (double)(src->downloaded.load() - dlStart_) / sec : -1;
            d = DownloadDetail(snap_.done, snap_.total, snap_.localDone, snap_.localTotal, net, lang_);
        } else {
            d = HumanBytes(snap_.done, lang_) + (lang_ == Lang::Ru ? " из " : " of ") + HumanBytes(snap_.total, lang_);
        }
        snap_.detail = d;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // Закрытие: отмена и ожидание фонового потока не дольше 5 с (сетевой запрос может висеть до таймаута WinHTTP),
    // дальше процесс завершается сразу: состояние на диске согласовано (pending, временные файлы убираются при старте)
    void Shutdown() {
        cancel_ = true;
        probe_.RequestStop();   // ★ 17.10.2026 идущий /status прерывается сразу
        // ★ 19.10.2026 выгрузке журналов не мешаем: окно уже закрыто (игра запущена или игрок вышел) — она получает
        //   те же ≤ 5 с, что и фоновый поток; не успела — процесс завершается, файлы остаются до следующего запуска
        for (int i = 0; i < 50 && (!workerDone_ || !probe_.Done() || !uploader_.Done()); ++i) Sleep(100);
        probe_.JoinIfDone();
        uploader_.JoinIfDone();
        if (!probe_.Done() || !uploader_.Done()) ExitProcess(0);
        if (!worker_.joinable()) return;
        if (workerDone_) worker_.join();
        else ExitProcess(0);
    }

    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        Window* self = (Window*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            self = (Window*)((CREATESTRUCTW*)lp)->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd_ = h;
        }
        return self ? self->Handle(m, wp, lp) : DefWindowProcW(h, m, wp, lp);
    }

    // Кнопка под точкой (логические координаты) или null
    Button* HitButton(float x, float y) {
        for (Button* b : {&playBtn_, &verifyBtn_, &cancelBtn_, &minBtn_, &closeBtn_, &langRu_, &langEn_})
            if (b->visible && b->r.Contains(x, y)) return b;
        return nullptr;
    }

    LRESULT Handle(UINT m, WPARAM wp, LPARAM lp) {
        switch (m) {
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM_(lp), GET_Y_LPARAM_(lp)};
            ScreenToClient(hwnd_, &pt);
            return HitButton(pt.x / scale_, pt.y / scale_) ? HTCLIENT : HTCAPTION;   // окно таскается за любое место
        }
        case WM_MOUSEMOVE: {
            float x = GET_X_LPARAM_(lp) / scale_, y = GET_Y_LPARAM_(lp) / scale_;
            bool changed = false;
            for (Button* b : {&playBtn_, &verifyBtn_, &cancelBtn_, &minBtn_, &closeBtn_, &langRu_, &langEn_}) {
                bool hv = b->visible && b->r.Contains(x, y);
                changed |= hv != b->hover;
                b->hover = hv;
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            Button* b = HitButton(x, y);
            SetCursor(LoadCursorW(nullptr, b && b->enabled ? IDC_HAND : IDC_ARROW));
            if (changed) InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE:
            for (Button* b : {&playBtn_, &verifyBtn_, &cancelBtn_, &minBtn_, &closeBtn_, &langRu_, &langEn_}) b->hover = b->down = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            Button* b = HitButton(GET_X_LPARAM_(lp) / scale_, GET_Y_LPARAM_(lp) / scale_);
            if (b) {
                b->down = true;
                SetCapture(hwnd_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            Button* b = HitButton(GET_X_LPARAM_(lp) / scale_, GET_Y_LPARAM_(lp) / scale_);
            bool wasDown = b && b->down;
            for (Button* x : {&playBtn_, &verifyBtn_, &cancelBtn_, &minBtn_, &closeBtn_, &langRu_, &langEn_}) x->down = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            if (!wasDown || !b->enabled) return 0;
            if (b == &closeBtn_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            else if (b == &minBtn_) ShowWindow(hwnd_, SW_MINIMIZE);
            else if (b == &langRu_ || b == &langEn_) {
                lang_ = b == &langRu_ ? Lang::Ru : Lang::En;
                SaveLang(lang_);
            } else if (b == &cancelBtn_) cancel_ = true;
            else if (b == &verifyBtn_) StartFlow(true);
            else if (b == &playBtn_) {
                UiState st;
                {
                    std::lock_guard<std::mutex> lk(m_);
                    st = snap_.state;
                }
                if (st == UiState::Ready) Play();
                else if (st == UiState::Error) StartFlow(false);
                else if (st == UiState::NeedAdmin && RelaunchElevated(L"--elevated-restart")) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        case WM_TIMER:
            UpdateRate();
            return 0;
        case WM_SIZE:   // ★ 17.10.2026 свёрнуто — опрос сервера на паузе (нагрузки ноль), развернули — сразу опрос
            probe_.SetPaused(wp == SIZE_MINIMIZED);
            break;
        case WM_APP_REFRESH:
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_APP_QUIT:
            DestroyWindow(hwnd_);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT rc;
            GetClientRect(hwnd_, &rc);
            G::Bitmap buf(rc.right, rc.bottom, PixelFormat32bppPARGB);
            {
                G::Graphics g(&buf);
                g.ScaleTransform(scale_, scale_);
                Paint(g);
            }
            G::Graphics screen(dc);
            screen.DrawImage(&buf, 0, 0);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_CLOSE:
            cancel_ = true;                              // идущее обновление отменяется (pending остаётся)
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd_, m, wp, lp);
    }

    // Координаты из LPARAM (без windowsx.h)
    static int GET_X_LPARAM_(LPARAM lp) { return (int)(short)LOWORD(lp); }
    static int GET_Y_LPARAM_(LPARAM lp) { return (int)(short)HIWORD(lp); }

    HINSTANCE inst_;
    HWND hwnd_ = nullptr;
    float scale_ = 1.f;
    std::wstring args_;
    std::unique_ptr<G::Bitmap> bg_, logo_;
    std::atomic<Lang> lang_{Lang::Ru};       // читают оба потока (ревизия В4, мелочь)
    std::mutex m_;
    Snapshot snap_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> workerDone_{true};      // фоновый поток завершил работу (join не заблокирует)
    std::string installed_;
    std::atomic<Source*> srcPtr_{nullptr};
    std::atomic<uint64_t> phaseStart_{0}, dlStart_{0};   // начало фазы и счётчик скачанного на её старте
    Button playBtn_, verifyBtn_, cancelBtn_, minBtn_, closeBtn_, langRu_, langEn_;
    ServerStatus net_;   // ★ 17.10.2026 состояние сервера и пинг (под m_)
    LogUploader uploader_;   // ★ 19.10.2026 выгрузка журналов прошлых сессий (поток; разрушается до окна — прерывает запрос)
    // опрос /status; объявлен последним — разрушается первым, пока окно ещё живо
    ServerProbe probe_{[this](const ServerStatus& st) { SetNet(st); }, [](const std::string& l) { LogFile(l); }};
};

// CLSID кодировщика PNG
bool PngClsid(CLSID& out) {
    UINT n = 0, sz = 0;
    G::GetImageEncodersSize(&n, &sz);
    std::vector<uint8_t> buf(sz);
    auto* enc = (G::ImageCodecInfo*)buf.data();
    G::GetImageEncoders(n, sz, enc);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(enc[i].MimeType, L"image/png") == 0) {
            out = enc[i].Clsid;
            return true;
        }
    return false;
}

}  // namespace

int RunLauncherWindow(HINSTANCE inst, const std::wstring& args) {
    G::GdiplusStartupInput in;
    ULONG_PTR tok = 0;
    G::GdiplusStartup(&tok, &in, nullptr);
    int rc;
    {
        Window w(inst);
        rc = w.Run(args);
    }
    G::GdiplusShutdown(tok);
    return rc;
}

int RenderPreview(HINSTANCE inst, const std::wstring& outPng, const std::wstring& state, const std::wstring& lang,
                  const std::wstring& net) {
    G::GdiplusStartupInput in;
    ULONG_PTR tok = 0;
    G::GdiplusStartup(&tok, &in, nullptr);
    int rc = 1;
    {
        Window w(inst);
        w.SetLang(lang == L"en" ? Lang::En : Lang::Ru);
        // ★ 17.10.2026 сеть: online:<мс> | offline | checking (по умолчанию online:42)
        ServerStatus ns{ServerState::Online, 42};
        if (net == L"offline") ns = {ServerState::Offline, -1};
        else if (net == L"checking") ns = {ServerState::Checking, -1};
        else if (net.rfind(L"online:", 0) == 0) ns.pingMs = _wtoi(net.c_str() + 7);
        w.SetNet(ns);
        w.Set([&](Snapshot& s) {
            s.installed = "alpha-1.0.0-10.2026";
            if (state == L"updating" || state == L"updating-local") {
                // ★ 19.10.2026: образец микропатча — загрузка 90 МБ, сборка из своих файлов 2.6 ГБ (updating-local — сборка
                //   идёт, updating — сборка закончена)
                s.state = UiState::Busy;
                s.status = sDownloading;
                s.done = 47185920;
                s.total = 94055219;
                s.localTotal = 2793893888;
                s.localDone = state == L"updating" ? s.localTotal : 1181116006;
                s.progress = DownloadProgress(s.done, s.total, s.localDone, s.localTotal);
                s.detail = DownloadDetail(s.done, s.total, s.localDone, s.localTotal, 5.4 * 1048576, lang == L"en" ? Lang::En : Lang::Ru);
                s.available = "alpha-1.0.1-10.2026";
                s.cancellable = true;
            } else if (state == L"error") {
                s.state = UiState::Error;
                s.status = sErrPrefix;
                s.detail = "Сервер обновлений недоступен. Проверьте интернет и повторите.";
            } else if (state == L"ready") {
                s.state = UiState::Ready;
                s.status = sReady;
                s.progress = 1;
                s.available = s.installed;
            } else {
                s.state = UiState::Busy;
                s.status = sConnect;
            }
        });
        G::Bitmap bmp((INT)(kW * 1.5f), (INT)(kH * 1.5f), PixelFormat32bppARGB);
        {
            G::Graphics g(&bmp);
            g.ScaleTransform(1.5f, 1.5f);
            w.Paint(g);
        }
        CLSID png;
        if (PngClsid(png) && bmp.Save(outPng.c_str(), &png, nullptr) == G::Ok) rc = 0;
    }
    G::GdiplusShutdown(tok);
    return rc;
}

}  // namespace asda
