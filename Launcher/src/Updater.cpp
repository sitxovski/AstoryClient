// Copyright (c) 2026 @sitxovski (Discord)
// Updater.cpp — алгоритм обновления (см. Updater.h). Эталон — Tools/asda_update_client.py: порядок проверок и
// тексты отказов совпадают (их ключевые слова проверяет сквозной самотест asda_update_selftest.py --client).
#include "Updater.h"

#include <windows.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <tuple>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include <rapidjson/document.h>

#include "Crypto.h"
#include "FileSys.h"
#include "Format.h"
#include "State.h"

namespace asda {

namespace {

// Пределы служебных файлов: конверт, манифест (сжатый/распакованный), .plst
constexpr size_t kMaxEnvelope = 1u << 20, kMaxManifest = 64u << 20, kMaxPlst = 64u << 20;
constexpr uint64_t kMaxManifestRaw = 512ull << 20;
// Склейка диапазонов: зазор и предел длины
constexpr uint64_t kRangeGap = 64u << 10, kRangeMax = 16u << 20;

// Запись файла манифеста
struct FileEntry {
    std::string path, sha, pieces;
    uint64_t size = 0;
    int64_t count = 0;
};

// Нижний регистр пути (ASCII; пути манифеста — только ASCII по белому списку)
std::string Low(const std::string& s) { return LowerAscii(s); }

// Журнал
void Log(const UpdateOptions& o, const std::string& s) {
    if (o.log) o.log(s);
}

// Проверка отмены
void CheckCancel(const UpdateOptions& o) {
    if (o.progress.cancel && o.progress.cancel->load()) throw Cancelled();
}

// Просрочен ли срок ISO 8601 «YYYY-MM-DDTHH:MM:SS+00:00» (нет — не просрочен, неразборчиво — просрочен)
bool IsExpired(const std::string& iso) {
    if (iso.empty()) return false;
    int Y, M, D, h, m, s;
    if (sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &s) != 6) return true;
    std::tm t{};
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    t.tm_sec = s;
    int tzSign = 0, tzH = 0, tzM = 0;
    if (iso.size() >= 25 && (iso[19] == '+' || iso[19] == '-')) {
        tzSign = iso[19] == '+' ? 1 : -1;
        sscanf(iso.c_str() + 20, "%2d:%2d", &tzH, &tzM);
    }
    time_t utc = _mkgmtime(&t) - tzSign * (tzH * 3600 + tzM * 60);
    return utc < time(nullptr);
}

// Ключ, канал, манифест — с проверками §6 п. 1–9. Сохраняет виденные seq сразу после проверки подписей.
struct Trusted {
    rapidjson::Document chan, man;
    Bytes mblob;
    std::vector<FileEntry> files;
    std::string msha;
};

void FetchChannel(Source& src, State& st, const UpdateOptions& o, rapidjson::Document& chan, int64_t& ringSeq) {
    TrustedKeys root;
    std::array<uint8_t, 32> rp = o.rootPub;
    root[KeyId(rp.data())] = rp;
    rapidjson::Document ring = EnvelopeOpen("keyring", src.Get("keys/keyring.json", kMaxEnvelope), root);
    int64_t kfloor = std::max(st.keyringSeq, o.minKeyringSeq);
    if (!JInt(ring, "seq", ringSeq) || ringSeq < kfloor)
        throw UpdateError(Fmt("keyring seq %lld ниже допустимого %lld — откат ключей", (long long)ringSeq, (long long)kfloor));
    chan = EnvelopeOpen("channel", src.Get("channels/" + o.channel + ".json", kMaxEnvelope), KeyringTrusted(ring));
    if (JStr(chan, "channel") != o.channel)
        throw UpdateError("конверт канала " + JStr(chan, "channel") + " выдан за " + o.channel);
    int64_t seq = 0;
    if (!JInt(chan, "seq", seq) || !chan.HasMember("manifest") || !chan["manifest"].IsObject())
        throw UpdateError("канал: неверные поля");
    int64_t floor = std::max(st.seqMax.count(o.channel) ? st.seqMax[o.channel] : 0, o.minSeq);
    if (seq < floor)
        throw UpdateError(Fmt("сервер предлагает seq %lld ниже уже виденного %lld — откат версии, отказ", (long long)seq,
                              (long long)floor));
    std::string msha = JStr(chan["manifest"], "sha256");
    CheckHex64(msha, "манифест канала");
    if (st.seqMax.count(o.channel) && seq == st.seqMax[o.channel] && st.seenManifest.count(o.channel) &&
        st.seenManifest[o.channel] != msha)
        throw UpdateError(Fmt("seq %lld уже был с другим манифестом — подмена, отказ (ревизия В2, V7)", (long long)seq));
    if (IsExpired(JStr(chan, "expires")) || IsExpired(JStr(ring, "expires")))
        Log(o, "! предупреждение: подписанный указатель просрочен (сервер давно не обновлялся или заморожен)");
    st.seqMax[o.channel] = std::max(floor, seq);
    st.seenManifest[o.channel] = msha;
    st.keyringSeq = std::max(st.keyringSeq, ringSeq);
    st.Save();
}

void FetchTrusted(Source& src, State& st, const UpdateOptions& o, Trusted& t) {
    int64_t ringSeq = 0;
    FetchChannel(src, st, o, t.chan, ringSeq);
    t.msha = JStr(t.chan["manifest"], "sha256");
    t.mblob = src.Get("manifests/" + t.msha + ".json.zst", kMaxManifest);
    if (Sha256Hex(t.mblob.data(), t.mblob.size()) != t.msha) throw UpdateError("манифест: хэш не совпал с подписанным");
    Bytes raw;
    try {
        raw = SafeZstd(t.mblob.data(), t.mblob.size(), std::nullopt, kMaxManifestRaw);
    } catch (const UpdateError& e) {
        throw UpdateError(std::string("манифест: ") + e.what());
    }
    t.man.Parse<rapidjson::kParseValidateEncodingFlag>((const char*)raw.data(), raw.size());
    if (t.man.HasParseError() || !t.man.IsObject() || JStr(t.man, "format") != "asda-manifest/1")
        throw UpdateError("манифест: чужой формат");
    int64_t mseq = -1, cseq = 0;
    JInt(t.man, "seq", mseq);
    JInt(t.chan, "seq", cseq);
    if (JStr(t.man, "version") != JStr(t.chan, "version") || mseq != cseq || JStr(t.man, "channel") != o.channel)
        throw UpdateError("манифест не той версии");
    auto fit = t.man.FindMember("files");
    if (fit == t.man.MemberEnd() || !fit->value.IsArray()) throw UpdateError("манифест: нет списка файлов");
    std::set<std::string> seen, dirs;
    for (const auto& fe : fit->value.GetArray()) {
        FileEntry e;
        int64_t size = -1;
        if (!fe.IsObject() || !JInt(fe, "size", size) || size < 0 || !JInt(fe, "count", e.count) || e.count < 0)
            throw UpdateError("манифест: запись с неверными полями");
        e.size = (uint64_t)size;
        if (!fe.HasMember("path") || !fe["path"].IsString()) throw UpdateError("путь: пустой или длиннее 260");
        e.path.assign(fe["path"].GetString(), fe["path"].GetStringLength());   // с длиной: NUL внутри ловит CheckRelPath (В3)
        CheckRelPath(e.path);
        e.sha = JStr(fe, "sha256");
        e.pieces = JStr(fe, "pieces");
        CheckHex64(e.sha, e.path);
        CheckHex64(e.pieces, e.path);
        std::string low = Low(e.path);
        if (!seen.insert(low).second) throw UpdateError("манифест: повтор пути (регистр): " + e.path);
        for (size_t i = low.find('/'); i != std::string::npos; i = low.find('/', i + 1)) dirs.insert(low.substr(0, i));
        t.files.push_back(std::move(e));
    }
    for (const auto& d : dirs)
        if (seen.count(d)) throw UpdateError("манифест: путь " + d + " — и файл, и папка");
}

// Записи файлов манифеста из сохранённого (старого) манифеста — без повторной проверки подписи (хэш сверен State)
std::vector<FileEntry> OldFiles(const State& st) {
    std::vector<FileEntry> out;
    Bytes blob;
    if (!st.ManifestBlob(blob)) return out;
    Bytes raw = SafeZstd(blob.data(), blob.size(), std::nullopt, kMaxManifestRaw);
    rapidjson::Document d;
    d.Parse((const char*)raw.data(), raw.size());
    if (d.HasParseError() || !d.HasMember("files") || !d["files"].IsArray()) return out;
    for (const auto& fe : d["files"].GetArray()) {
        FileEntry e;
        e.path = JStr(fe, "path");
        e.pieces = JStr(fe, "pieces");
        e.sha = JStr(fe, "sha256");
        int64_t sz = 0;
        JInt(fe, "size", sz);
        e.size = (uint64_t)sz;
        try {
            CheckRelPath(e.path);
            CheckHex64(e.pieces, e.path);
        } catch (const UpdateError&) {
            continue;
        }
        out.push_back(e);
    }
    return out;
}

// Где лежит кусок в старой версии
struct LocalRef {
    std::string path;
    uint64_t off = 0;
    uint32_t size = 0;
};

// SHA-256 локального файла с кэшем (размер+время); пусто, если нет/не файл
std::string FileShaCached(State& st, const std::wstring& root, const std::string& rel, bool useCache) {
    std::wstring p = SafePath(root, rel);
    uint64_t size = 0, mtime = 0;
    if (!IsRegularFile(p) || !FileStat(p, size, mtime)) return {};
    auto it = st.cache.find(rel);
    if (useCache && it != st.cache.end() && it->second.size == size && it->second.mtime == mtime) return it->second.sha;
    std::string sha = Sha256FileHex(p);
    st.cache[rel] = {size, mtime, sha};
    return sha;
}

// RAII-файл
struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~File() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

// Позиционное чтение
bool ReadAt(HANDLE h, uint64_t off, void* buf, uint32_t n) {
    OVERLAPPED ov{};
    ov.Offset = (DWORD)off;
    ov.OffsetHigh = (DWORD)(off >> 32);
    DWORD got = 0;
    return ReadFile(h, buf, n, &got, &ov) && got == n;
}

// Позиционная запись
void WriteAt(HANDLE h, uint64_t off, const void* buf, uint32_t n) {
    OVERLAPPED ov{};
    ov.Offset = (DWORD)off;
    ov.OffsetHigh = (DWORD)(off >> 32);
    DWORD put = 0;
    if (!WriteFile(h, buf, n, &put, &ov) || put != n) throw UpdateError("запись файла: " + LastErrorText(GetLastError()));
}

// Недостающий кусок: смещение в собираемом файле + запись .plst
struct Missing {
    uint64_t foff;
    const Piece* p;
};

// Задание загрузки: один диапазон пачки и куски в нём
struct Job {
    std::string pack;
    uint64_t start, end;
    std::vector<Missing> items;
};

// ★ 19.10.2026: склейка недостающих кусков в диапазоны пачек — ОДНА точка для плана загрузки и для самой загрузки
//   (план обязан считать ровно те байты, что потом уйдут по сети).
std::vector<Job> BuildJobs(const std::vector<Missing>& missing) {
    std::map<std::string, std::vector<Missing>> byPack;
    for (const auto& m : missing) byPack[m.p->pack].push_back(m);
    std::vector<Job> jobs;
    for (auto& [pack, items] : byPack) {
        CheckHex64(pack, "пачка");
        std::sort(items.begin(), items.end(), [](const Missing& a, const Missing& b) { return a.p->offset < b.p->offset; });
        for (size_t i = 0; i < items.size();) {
            Job j{pack, items[i].p->offset, items[i].p->offset + items[i].p->stored, {items[i]}};
            size_t k = i + 1;
            // offset может быть МЕНЬШЕ end (повторяющийся кусок в нескольких местах файла) — без беззнакового вычитания
            while (k < items.size() && items[k].p->offset <= j.end + kRangeGap &&
                   std::max(j.end, items[k].p->offset + items[k].p->stored) - j.start <= kRangeMax) {
                j.end = std::max(j.end, items[k].p->offset + items[k].p->stored);
                j.items.push_back(items[k++]);
            }
            jobs.push_back(std::move(j));
            i = k;
        }
    }
    return jobs;
}

// Байты сети заданий (сумма длин диапазонов — с зазорами склейки, как их отдаст сервер)
uint64_t JobsBytes(const std::vector<Job>& jobs) {
    uint64_t n = 0;
    for (const auto& j : jobs) n += j.end - j.start;
    return n;
}

// ★ 19.10.2026: счётчики фазы загрузки — сеть отдельно от сборки из своих кусков.
struct DownloadCounters {
    std::atomic<uint64_t> netDone{0}, netTotal{0};       // байты диапазонов: скачано / план (план уточняется по факту)
    std::atomic<uint64_t> localDone{0}, localTotal{0};   // байты своих кусков: скопировано / план
    uint64_t localReported = 0;                          // последний отданный окну ход сборки (троттлинг)
};

// ★ 09.11.2026 Упреждающая загрузка диапазонов пачек. Прежде файлы собирались строго по одному: пока текущий файл
//   копировался из своих кусков (гигабайты чтения+SHA+записи) и перехэшировался целиком, сеть простаивала, а загрузка
//   шла только внутри одного файла. Теперь диапазоны ПЛАНА всех изменённых файлов качаются заранее, в порядке плана,
//   o.threads потоками, в памяти не больше kPrefetchCap; сборка файла забирает готовое (Take). Диапазон, которого в плане
//   нет (свой кусок не сошёлся по SHA) или который ещё не начат, сборка качает сама — тупика по памяти нет.
constexpr uint64_t kPrefetchCap = 192ull << 20;

class Prefetcher {
public:
    using Key = std::tuple<std::string, uint64_t, uint64_t>;   // пачка, начало, конец диапазона

    Prefetcher(Source& src, const UpdateOptions& o, std::function<void(uint64_t)> onFetched)
        : src_(src), o_(o), onFetched_(std::move(onFetched)) {}
    ~Prefetcher() { Stop(); }

    // Диапазоны файла fileIdx (до Start; порядок вызовов = порядок загрузки)
    void Add(size_t fileIdx, const std::vector<Job>& jobs) {
        for (const auto& j : jobs) {
            Key k{j.pack, j.start, j.end};
            if (entries_.count(k)) continue;   // тот же диапазон у другого файла — берёт первый, кто попросит
            entries_[k] = Entry{fileIdx};
            order_.push_back(k);
        }
    }

    void Start(int threads) {
        for (int i = 0; i < std::max(1, threads); ++i) workers_.emplace_back([this] { Work(); });
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
        workers_.clear();
    }

    // Забрать готовый диапазон. false — качать самому (нет в плане, ещё не начат, сбой упреждения).
    bool Take(const Key& k, Bytes& out) {
        std::unique_lock<std::mutex> lk(m_);
        auto it = entries_.find(k);
        if (it == entries_.end()) return false;
        Entry& e = it->second;
        if (e.state == kQueued) {
            e.state = kClaimed;
            return false;
        }
        cv_.wait(lk, [&] { return e.state != kInflight; });
        if (e.state != kDone) return false;
        out = std::move(e.data);
        e.data = Bytes();
        e.state = kClaimed;
        held_ -= std::get<2>(k) - std::get<1>(k);
        cv_.notify_all();
        return true;
    }

    // Файл собран: его невзятые диапазоны больше не нужны (освободить память, не качать). Возвращает байты, которые
    //   уже были скачаны впустую (их надо добавить к плану сети, чтобы прогресс не ушёл за 100 %).
    uint64_t DropFile(size_t fileIdx) {
        std::lock_guard<std::mutex> lk(m_);
        uint64_t wasted = 0;
        for (auto& [k, e] : entries_) {
            if (e.file != fileIdx) continue;
            uint64_t n = std::get<2>(k) - std::get<1>(k);
            if (e.state == kQueued) e.state = kClaimed;
            else if (e.state == kDone) {
                e.data = Bytes();
                e.state = kClaimed;
                held_ -= n;
                wasted += n;
            } else if (e.state == kInflight) {
                e.dropAfter = true;
                wasted += n;
            }
        }
        cv_.notify_all();
        return wasted;
    }

private:
    enum State { kQueued, kInflight, kDone, kFailed, kClaimed };
    struct Entry {
        size_t file = 0;
        State state = kQueued;
        bool dropAfter = false;
        Bytes data;
    };

    void Work() {
        for (;;) {
            Key k;
            uint64_t n = 0;
            {
                std::unique_lock<std::mutex> lk(m_);
                for (;;) {
                    if (stop_ || (o_.progress.cancel && o_.progress.cancel->load())) return;
                    while (next_ < order_.size() && entries_[order_[next_]].state != kQueued) ++next_;
                    if (next_ >= order_.size()) return;
                    k = order_[next_];
                    n = std::get<2>(k) - std::get<1>(k);
                    if (held_ == 0 || held_ + n <= kPrefetchCap) break;   // память: не больше kPrefetchCap (кроме первого)
                    cv_.wait(lk);
                }
                entries_[k].state = kInflight;
                held_ += n;
                ++next_;
            }
            Bytes b;
            bool ok = true;
            try {
                b = src_.GetRange("packs/" + std::get<0>(k) + ".blob", std::get<1>(k), (uint32_t)n);
            } catch (const std::exception&) {
                ok = false;   // ошибку покажет сборка, скачав диапазон сама
            }
            if (ok && onFetched_) onFetched_(n);
            {
                std::lock_guard<std::mutex> lk(m_);
                Entry& e = entries_[k];
                if (!ok || e.dropAfter) {
                    held_ -= n;
                    e.state = e.dropAfter ? kClaimed : kFailed;
                } else {
                    e.data = std::move(b);
                    e.state = kDone;
                }
            }
            cv_.notify_all();
        }
    }

    Source& src_;
    const UpdateOptions& o_;
    std::function<void(uint64_t)> onFetched_;
    std::map<Key, Entry> entries_;
    std::vector<Key> order_;
    size_t next_ = 0;        // первый, возможно, ещё не начатый диапазон в order_
    uint64_t held_ = 0;      // байты в памяти и в полёте
    bool stop_ = false;
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
};

// Отдать окну ход сети и сборки
void ReportNet(const UpdateOptions& o, DownloadCounters& c) {
    if (o.progress.bytes) o.progress.bytes(c.netDone.load(), c.netTotal.load());
}
void ReportLocal(const UpdateOptions& o, DownloadCounters& c, bool force) {
    uint64_t d = c.localDone.load();
    if (!force && d - c.localReported < (8u << 20)) return;   // не чаще, чем раз в 8 МиБ
    c.localReported = d;
    if (o.progress.assemble) o.progress.assemble(d, std::max(d, c.localTotal.load()));
}

// Собрать файл во временный, проверить, заменить.
//   replaced — пути (нижний регистр) файлов, уже заменённых новой версией в ЭТОМ обновлении: их старые куски на диске
//   больше не лежат по смещениям старого манифеста — читать их бессмысленно (тот же отбор, что у плана).
//   plannedNet — байты сети, которые план отвёл этому файлу: если по факту больше/меньше (свой кусок не сошёлся по SHA и
//   т. п.), план загрузки поправляется на разницу, чтобы прогресс не ушёл за 100 %.
void Assemble(Source& src, State& st, const std::wstring& root, const FileEntry& fe, const Bytes& plst,
              const std::map<Digest, std::vector<LocalRef>>& localIdx, const std::set<std::string>& replaced,
              const UpdateOptions& o, UpdateStats& stats, DownloadCounters& cnt, uint64_t plannedNet,
              Prefetcher* pf) {
    std::vector<Piece> pieces = PlstUnpack(plst);
    uint64_t sum = 0;
    for (const auto& p : pieces) sum += p.size;
    if (sum != fe.size || (int64_t)pieces.size() != fe.count) throw UpdateError(fe.path + ": куски не покрывают файл");
    std::wstring dst = SafePath(root, fe.path);
    if (IsDir(dst) && !RemoveDirectoryW(dst.c_str())) throw UpdateError(fe.path + ": на месте файла непустая папка");
    MakeParentDirs(root, dst);
    std::wstring tmp = dst + Widen(kTmpSuffix);
    File out;
    out.h = CreateTmpChecked(root, tmp);                 // новый файл, ссылки не разыменовываются (ревизия В1)
    struct TmpGuard {                                    // временный файл удаляется при любом выходе
        File& f;
        std::wstring p;
        bool keep = false;
        ~TmpGuard() {
            if (!keep) {
                if (f.h != INVALID_HANDLE_VALUE) CloseHandle(f.h), f.h = INVALID_HANDLE_VALUE;
                DeleteFileW(p.c_str());
            }
        }
    } guard{out, tmp};
    LARGE_INTEGER sz;
    sz.QuadPart = (LONGLONG)fe.size;
    SetFilePointerEx(out.h, sz, nullptr, FILE_BEGIN);
    SetEndOfFile(out.h);
    // 1) свои куски из старых файлов (каждый перехэшируется)
    std::map<std::wstring, File> handles;
    std::vector<Missing> missing;
    Bytes buf;
    uint64_t off = 0;
    for (const auto& p : pieces) {
        CheckCancel(o);
        bool ok = false;
        auto it = localIdx.find(p.hash);
        if (it != localIdx.end()) {
            for (const auto& ref : it->second) {
                if (replaced.count(Low(ref.path))) continue;   // ★ 19.10.2026: файл уже заменён новой версией
                std::wstring lp;
                try {
                    lp = SafePath(root, ref.path);
                } catch (const UpdateError&) {
                    continue;
                }
                File& f = handles[lp];
                if (f.h == INVALID_HANDLE_VALUE) f.h = OpenReadChecked(root, lp);   // не ссылка, внутри корня
                if (f.h == INVALID_HANDLE_VALUE || ref.size != p.size) continue;
                buf.resize(p.size);
                if (!ReadAt(f.h, ref.off, buf.data(), p.size) || Sha256Of(buf.data(), p.size) != p.hash) continue;
                WriteAt(out.h, off, buf.data(), p.size);
                stats.local += p.size;
                cnt.localDone += p.size;
                ok = true;
                break;
            }
        }
        if (!ok) missing.push_back({off, &p});
        off += p.size;
        ReportLocal(o, cnt, false);
    }
    handles.clear();
    ReportLocal(o, cnt, true);
    // 2) недостающие — диапазонами по пачкам, параллельно
    std::vector<Job> jobs = BuildJobs(missing);
    // ★ 19.10.2026: факт против плана — разницу в план загрузки (прогресс не уходит за 100 % и не «недобегает»)
    const uint64_t actualNet = JobsBytes(jobs);
    if (actualNet > plannedNet) cnt.netTotal += actualNet - plannedNet;
    else if (actualNet < plannedNet) cnt.netTotal -= plannedNet - actualNet;
    if (actualNet != plannedNet) ReportNet(o, cnt);
    std::mutex wm;
    std::atomic<size_t> next{0};
    std::string firstErr;
    std::atomic<bool> failed{false}, cancelled{false};
    auto worker = [&] {
        while (!failed) {
            size_t ji = next++;
            if (ji >= jobs.size()) return;
            const Job& j = jobs[ji];
            try {
                if (o.progress.cancel && o.progress.cancel->load()) throw Cancelled();
                Bytes blob;
                // ★ 09.11.2026: диапазон, скачанный заранее (Prefetcher), — без повторной загрузки и без повторного счёта
                const bool pre = pf && pf->Take(Prefetcher::Key{j.pack, j.start, j.end}, blob);
                if (!pre) blob = src.GetRange("packs/" + j.pack + ".blob", j.start, (uint32_t)(j.end - j.start));
                for (const auto& m : j.items) {
                    const Piece& p = *m.p;
                    if (p.offset < j.start || p.offset - j.start + p.stored > blob.size())
                        throw UpdateError(fe.path + ": кусок вне скачанного диапазона");
                    const uint8_t* raw = blob.data() + (p.offset - j.start);
                    Bytes data;
                    if (p.codec == kCodecZstd) {
                        try {
                            data = SafeZstd(raw, p.stored, p.size, std::nullopt);
                        } catch (const UpdateError& e) {
                            throw UpdateError(fe.path + ": кусок " + ToHex(p.hash.data(), 8) + " испорчен: " + e.what());
                        }
                    } else {
                        data.assign(raw, raw + p.stored);
                    }
                    if (data.size() != p.size || Sha256Of(data.data(), data.size()) != p.hash)
                        throw UpdateError(fe.path + ": скачанный кусок " + ToHex(p.hash.data(), 8) + " испорчен");
                    std::lock_guard<std::mutex> lk(wm);
                    WriteAt(out.h, m.foff, data.data(), p.size);
                    stats.netPayload += p.size;
                }
                if (!pre) {   // скачанное заранее учтено в момент загрузки (onFetched)
                    cnt.netDone += j.end - j.start;   // ★ 19.10.2026: прогресс — байты сети (длина скачанного диапазона)
                    ReportNet(o, cnt);
                }
            } catch (const Cancelled&) {
                cancelled = true;
                failed = true;
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lk(wm);
                if (firstErr.empty()) firstErr = e.what();
                failed = true;
            }
        }
    };
    int n = std::max(1, std::min<int>(o.threads, (int)jobs.size()));
    std::vector<std::thread> ts;
    for (int i = 0; i < n; ++i) ts.emplace_back(worker);
    for (auto& t : ts) t.join();
    if (cancelled) throw Cancelled();
    if (failed) throw UpdateError(firstErr);
    FlushFileBuffers(out.h);
    CloseHandle(out.h);
    out.h = INVALID_HANDLE_VALUE;
    if (Sha256FileHex(tmp) != fe.sha) throw UpdateError(fe.path + ": собранный файл не совпал с манифестом");
    ReplaceFile(root, tmp, dst);
    guard.keep = true;
    uint64_t s = 0, m = 0;
    FileStat(dst, s, m);
    st.cache[fe.path] = {s, m, fe.sha};
}

}  // namespace

bool InstalledFileSha(const std::wstring& gameDir, const std::string& rel, std::string& sha) {
    State st(RootPath(gameDir), nullptr);
    if (st.pending) return false;
    for (const auto& fe : OldFiles(st))
        if (Low(fe.path) == Low(rel)) {
            sha = fe.sha;
            return !sha.empty();
        }
    return false;
}

RemoteInfo CheckRemote(Source& src, const std::wstring& gameDir, const UpdateOptions& o) {
    State st(RootPath(gameDir), nullptr);
    rapidjson::Document chan;
    int64_t ringSeq = 0;
    FetchChannel(src, st, o, chan, ringSeq);
    RemoteInfo r;
    r.version = JStr(chan, "version");
    r.minVersion = JStr(chan, "min_version");
    JInt(chan, "seq", r.seq);
    r.upToDate = !st.pending && st.version == r.version && st.seq == r.seq && st.manifest == JStr(chan["manifest"], "sha256");
    return r;
}

UpdateStats Update(Source& src, const std::wstring& gameDir, const UpdateOptions& o) {
    std::wstring root = RootPath(gameDir);
    State st(root, o.log);
    if (o.progress.phase) o.progress.phase(Phase::Connect, "");
    Trusted t;
    FetchTrusted(src, st, o, t);
    std::vector<FileEntry> old = OldFiles(st);
    UpdateStats stats;
    stats.version = JStr(t.chan, "version");
    JInt(t.chan, "seq", stats.seq);
    stats.files = t.files.size();
    CleanupTmp(root);
    st.pending = Pending{stats.version, t.msha, stats.seq};              // до первого изменения на диске (V8)
    st.Save();
    // индекс своих кусков из старой версии
    std::map<Digest, std::vector<LocalRef>> localIdx;
    for (const auto& fe : old) {
        Bytes plst;
        if (!st.Plst(fe.pieces, plst)) continue;
        std::vector<Piece> ps;
        try {
            ps = PlstUnpack(plst);
        } catch (const UpdateError&) {
            continue;
        }
        uint64_t off = 0;
        for (const auto& p : ps) {
            localIdx[p.hash].push_back({fe.path, off, p.size});
            off += p.size;
        }
    }
    std::set<std::string> newPaths, newDirs;
    for (const auto& fe : t.files) {
        std::string low = Low(fe.path);
        newPaths.insert(low);
        for (size_t i = low.find('/'); i != std::string::npos; i = low.find('/', i + 1)) newDirs.insert(low.substr(0, i));
    }
    std::vector<std::string> gone;
    for (const auto& fe : old)
        if (!newPaths.count(Low(fe.path))) gone.push_back(fe.path);
    // 1) конфликтующие старые файлы — сразу: файл на месте будущей папки и файлы внутри будущего файла (V3)
    for (const auto& rel : gone) {
        std::string low = Low(rel);
        bool conflict = newDirs.count(low) > 0;
        for (size_t i = low.find('/'); !conflict && i != std::string::npos; i = low.find('/', i + 1))
            conflict = newPaths.count(low.substr(0, i)) > 0;
        if (conflict) {
            std::wstring p = SafePath(root, rel);
            if (IsRegularFile(p)) {
                RemoveFileChecked(root, p);
                ++stats.deleted;
            }
            st.cache.erase(rel);
            RemoveEmptyParents(root, p);
        }
    }
    // 2) проверка файлов (хэши; кэш — кроме «Проверить файлы»)
    if (o.progress.phase) o.progress.phase(Phase::Check, "");
    uint64_t checkTotal = 0, checkDone = 0;
    for (const auto& fe : t.files) checkTotal += fe.size;
    std::vector<const FileEntry*> todo;
    for (const auto& fe : t.files) {
        CheckCancel(o);
        if (FileShaCached(st, root, fe.path, !o.repair) != fe.sha) todo.push_back(&fe);
        checkDone += fe.size;
        if (o.progress.bytes) o.progress.bytes(checkDone, checkTotal);
    }
    st.Save();
    // 3) ★ 19.10.2026 ПЛАН ЗАГРУЗКИ — до первого скачанного куска: какие куски возьмутся из своих файлов, какие — из сети.
    //   Свой кусок годен, если файл старой версии цел (SHA совпадает со старым манифестом) и ещё не заменён новой версией
    //   в этом обновлении (файлы собираются по порядку `todo`). Недостающие склеиваются в диапазоны той же `BuildJobs`, что
    //   и при загрузке, — план равен байтам сети. Прежде окно показывало сумму размеров изменённых файлов целиком.
    if (o.progress.phase) o.progress.phase(Phase::Download, "");
    std::map<std::string, std::string> oldSha;           // путь старой версии (нижний регистр) → SHA по старому манифесту
    for (const auto& fe : old) oldSha[Low(fe.path)] = fe.sha;
    std::map<std::string, bool> intact;                  // путь (нижний регистр) → файл старой версии цел
    auto Intact = [&](const std::string& rel) -> bool {
        std::string low = Low(rel);
        auto it = intact.find(low);
        if (it != intact.end()) return it->second;
        auto os = oldSha.find(low);
        bool ok = false;
        try {
            ok = os != oldSha.end() && !os->second.empty() && FileShaCached(st, root, rel, !o.repair) == os->second;
        } catch (const UpdateError&) {
            ok = false;
        }
        intact[low] = ok;
        return ok;
    };
    struct Planned {
        const FileEntry* fe;
        Bytes plst;
        uint64_t net = 0, local = 0;
        std::vector<Job> jobs;   // ★ 09.11.2026: диапазоны плана — для упреждающей загрузки
        bool exact = true;       // ★ 09.11.2026: план точен (нет кусков, отвергнутых лишь из-за «нецелого» старого файла)
    };
    std::vector<Planned> plan;
    DownloadCounters cnt;
    {
        std::set<std::string> willReplace;
        for (auto* fe : todo) {
            CheckCancel(o);
            Planned pl{fe, {}, 0, 0, {}, true};
            if (!st.Plst(fe->pieces, pl.plst)) {
                pl.plst = src.Get("objects/" + fe->pieces.substr(0, 2) + "/" + fe->pieces + ".plst", kMaxPlst);
                if (Sha256Hex(pl.plst.data(), pl.plst.size()) != fe->pieces)
                    throw UpdateError(fe->path + ": список кусков не совпал с манифестом");
            }
            std::vector<Piece> ps = PlstUnpack(pl.plst);
            std::vector<Missing> miss;
            uint64_t off = 0;
            for (const auto& p : ps) {
                bool local = false;
                auto it = localIdx.find(p.hash);
                if (it != localIdx.end())
                    for (const auto& ref : it->second) {
                        if (ref.size != p.size || willReplace.count(Low(ref.path))) continue;
                        if (Intact(ref.path)) {
                            local = true;
                            break;
                        }
                        pl.exact = false;   // кусок, возможно, цел в повреждённом файле — сборка проверит сама
                    }
                if (local) pl.local += p.size;
                else miss.push_back({off, &p});
                off += p.size;
            }
            pl.jobs = BuildJobs(miss);
            pl.net = JobsBytes(pl.jobs);
            for (auto& j : pl.jobs) j.items.clear();   // куски указывают на локальный ps — упреждению нужны только диапазоны
            cnt.netTotal += pl.net;
            cnt.localTotal += pl.local;
            stats.changedBytes += fe->size;
            willReplace.insert(Low(fe->path));
            plan.push_back(std::move(pl));
        }
    }
    stats.planNet = cnt.netTotal.load();
    Log(o, Fmt("план обновления: файлов %llu (%.1f МБ на диске), загрузка %.1f МБ, из своих файлов %.1f МБ",
               (unsigned long long)plan.size(), stats.changedBytes / 1e6, stats.planNet / 1e6, cnt.localTotal.load() / 1e6));
    ReportNet(o, cnt);
    ReportLocal(o, cnt, true);
    // 4) сборка изменившихся по плану; ★ 09.11.2026 сеть качает диапазоны плана заранее, параллельно сборке
    Prefetcher pf(src, o, [&](uint64_t n) {
        cnt.netDone += n;
        ReportNet(o, cnt);
    });
    // Неточный план (ремонт частично испорченного файла) не качается заранее: сборка сама найдёт целые куски в нём
    //   и скачает только недостающее — иначе упреждение тянуло бы лишнее (до kPrefetchCap).
    for (size_t i = 0; i < plan.size(); ++i)
        if (plan[i].exact) pf.Add(i, plan[i].jobs);
    pf.Start(o.threads);
    std::set<std::string> replaced;
    for (size_t i = 0; i < plan.size(); ++i) {
        auto& pl = plan[i];
        CheckCancel(o);
        ++stats.changed;
        Assemble(src, st, root, *pl.fe, pl.plst, localIdx, replaced, o, stats, cnt, pl.net, &pf);
        if (uint64_t wasted = pf.DropFile(i)) {   // скачанное заранее, но не понадобившееся, — в план (прогресс ≤ 100 %)
            cnt.netTotal += wasted;
            ReportNet(o, cnt);
        }
        replaced.insert(Low(pl.fe->path));
        WriteAtomic(root, st.objects + L"\\" + Widen(pl.fe->pieces) + L".plst", pl.plst.data(), pl.plst.size());
        st.Save();                                      // прогресс не теряется при обрыве
    }
    pf.Stop();
    // 4) остальные исчезнувшие файлы и опустевшие папки
    if (o.progress.phase) o.progress.phase(Phase::Finish, "");
    for (const auto& rel : gone) {
        std::wstring p = SafePath(root, rel);
        if (IsRegularFile(p)) {
            RemoveFileChecked(root, p);
            ++stats.deleted;
        }
        st.cache.erase(rel);
        RemoveEmptyParents(root, p);
    }
    // 5) .plst текущей версии — сохранить, лишние — удалить; манифест — как скачан
    std::set<std::string> keep;
    for (const auto& fe : t.files) {
        keep.insert(fe.pieces);
        Bytes plst;
        if (!st.Plst(fe.pieces, plst)) {
            plst = src.Get("objects/" + fe.pieces.substr(0, 2) + "/" + fe.pieces + ".plst", kMaxPlst);
            if (Sha256Hex(plst.data(), plst.size()) != fe.pieces) throw UpdateError("список кусков " + fe.pieces + " испорчен");
            WriteAtomic(root, st.objects + L"\\" + Widen(fe.pieces) + L".plst", plst.data(), plst.size());
        }
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((st.objects + L"\\*.plst").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string name = Narrow(fd.cFileName);
            if (name.size() > 5 && !keep.count(name.substr(0, name.size() - 5)))
                DeleteFileW((st.objects + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    WriteAtomic(root, st.dir + L"\\manifest.json.zst", t.mblob.data(), t.mblob.size());
    st.channel = o.channel;
    st.version = stats.version;
    st.seq = stats.seq;
    st.manifest = t.msha;
    st.minVersion = JStr(t.chan, "min_version");
    st.pending.reset();
    st.Save();
    stats.downloaded = src.downloaded;
    Log(o, Fmt("версия %s seq %lld: изменено файлов %llu, из локальных кусков %.1f МБ, скачано %.1f МБ (полезных %.1f), "
               "удалено %llu, план загрузки %.1f МБ",
               stats.version.c_str(), (long long)stats.seq, (unsigned long long)stats.changed, stats.local / 1e6,
               stats.downloaded / 1e6, stats.netPayload / 1e6, (unsigned long long)stats.deleted, stats.planNet / 1e6));
    return stats;
}

}  // namespace asda
