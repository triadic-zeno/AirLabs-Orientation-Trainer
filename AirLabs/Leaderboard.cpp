// The public leaderboard: recording runs, posting them, reading boards, and playing replays.
//
// A preset run is recorded tick by tick (Replay.h). When it ends, if you've turned on "Post my
// runs" and it might be a new best, it goes to the leaderboard server with your Rocket League
// name and account id. The server scores the replay again itself before it accepts it, and keeps
// your best run's replay so anyone can watch it: Watch downloads it and the car and ball follow
// it tick by tick.
//
// HTTP callbacks run on another thread. Everything they share with the game and the window is
// behind lbMutex, and anything that touches the game goes through gameWrapper->Execute.
#include "AirLabs.h"
#include "bakkesmod/wrappers/http/HttpWrapper.h"
#include "bakkesmod/wrappers/UniqueIDWrapper.h"
#include "bakkesmod/wrappers/MMRWrapper.h"
#include "bakkesmod/wrappers/Engine/UnrealStringWrapper.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

using namespace airlabs;

namespace
{
    double Wall() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    const char* PlatformName(OnlinePlatform p)
    {
        switch (p)
        {
        case OnlinePlatform_Steam: return "Steam";
        case OnlinePlatform_Epic:  return "Epic";
        case OnlinePlatform_PS4:   return "PlayStation";
        case OnlinePlatform_Dingo: return "Xbox";
        case OnlinePlatform_NNX:
        case OnlinePlatform_OldNNX: return "Switch";
        default: return "Unknown";
        }
    }

    // "2.8.0" newer than "2.7.2"?
    bool NewerVersion(const std::string& a, const std::string& b)
    {
        int x[3] = {}, y[3] = {};
        std::sscanf(a.c_str(), "%d.%d.%d", &x[0], &x[1], &x[2]);
        std::sscanf(b.c_str(), "%d.%d.%d", &y[0], &y[1], &y[2]);
        for (int i = 0; i < 3; ++i) if (x[i] != y[i]) return x[i] > y[i];
        return false;
    }
    // A saved run's "YYYY-MM-DD HH:MM" (your local time) as unix seconds, 0 if unreadable.
    long long StampSeconds(const std::string& when)
    {
        std::tm tm{};
        if (std::sscanf(when.c_str(), "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min) < 3) return 0;
        tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1;
        const std::time_t t = std::mktime(&tm);
        return t < 0 ? 0 : static_cast<long long>(t);
    }

    std::string Thousands(long long v)
    {
        std::string d = std::to_string(v < 0 ? -v : v), out;
        for (std::size_t i = 0; i < d.size(); ++i) { if (i && (d.size() - i) % 3 == 0) out += ','; out += d[i]; }
        return (v < 0 ? "-" : "") + out;
    }
}

std::string AirLabs::BoardKey(int mode, int preset, int range, bool noRoll)
{
    return std::to_string(mode) + "-" + std::to_string(preset) + "-" + std::to_string(PresetUsesRange(mode, preset) ? range : 0) + "-" + (noRoll ? "1" : "0");
}

std::string AirLabs::LbUrl(const std::string& path) const
{
    std::string base = lbUrl ? *lbUrl : std::string();
    while (!base.empty() && (base.back() == '/' || base.back() == ' ')) base.pop_back();
    return base + path;
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

void AirLabs::RecEvent(std::uint8_t type, float value)
{
    if (!recording) return;
    ReplayEventRec e;
    e.tick = static_cast<std::uint32_t>(recReplay.samples.size()); // the sample this tick will record
    e.type = type;
    e.value = value;
    recReplay.events.push_back(e);
}

void AirLabs::RecordTick(const Rotator& rot)
{
    if (recReplay.samples.size() >= kReplayMaxSamples) return;
    ReplaySample sm;
    sm.ms = static_cast<std::uint16_t>(std::clamp<long>(std::lround((gameTime - runStart) * 1000.0), 0L, 65535L));
    sm.pitch = static_cast<std::int16_t>(static_cast<std::uint16_t>(rot.Pitch & 0xFFFF));
    sm.yaw = static_cast<std::int16_t>(static_cast<std::uint16_t>(rot.Yaw & 0xFFFF));
    sm.roll = static_cast<std::int16_t>(static_cast<std::uint16_t>(rot.Roll & 0xFFFF));
    const Vector d = Norm(Sub(targetLocation, pinLocation)); // what this tick was scored against
    sm.dx = PackUnit(d.X); sm.dy = PackUnit(d.Y); sm.dz = PackUnit(d.Z);
    recReplay.samples.push_back(sm);
}

// ---------------------------------------------------------------------------------------------
// Posting a run
// ---------------------------------------------------------------------------------------------

void AirLabs::LoadLeaderboardFile()
{
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        CountPendingLocked();
    }
    std::ifstream f(DataDir() / "leaderboard.txt");
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    TextReply t = ParseTextReply(ss.str());
    std::lock_guard<std::mutex> lock(lbMutex);
    lbKey = t.Get("key");
    lbPid = t.Get("pid");
    const std::vector<std::string> rk = SplitTabs(t.Get("ranks"));
    for (int i = 0; i < 3 && i < static_cast<int>(rk.size()); ++i) lbRealRank[i] = std::clamp(std::atoi(rk[i].c_str()), -1, 22 * 4 + 3);
}

void AirLabs::SaveLeaderboardFileLocked() const
{
    // This install's key: the server ties your Rocket League account to it on your first post.
    std::error_code ec;
    std::filesystem::create_directories(DataDir(), ec);
    std::ofstream f(DataDir() / "leaderboard.txt", std::ios::trunc);
    f << "key " << lbKey << "\npid " << lbPid << "\nranks " << lbRealRank[0] << "\t" << lbRealRank[1] << "\t" << lbRealRank[2] << "\n";
}

void AirLabs::UploadRun(const Replay& r, bool post)
{
    const std::string board = BoardKey(r.mode, r.preset, r.range, (r.flags & kRfNoRoll) != 0);
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        auto it = lbServerBest.find(board);
        if (it != lbServerBest.end() && r.score <= it->second)
        {
            if (post)
            {
                lbStatus = "Leaderboard: your best on this preset is " + Thousands(it->second) + ", so this run wasn't posted.";
                lbStatusGood = true;
                lbStatusWall = Wall();
            }
            return;
        }
    }

    PendingRun p;
    if (!PlayerIdentity(p.platform, p.id, p.name))
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        lbStatus = "Not posted: couldn't read your Rocket League account.";
        lbStatusGood = false;
        lbStatusWall = Wall();
        return;
    }
    p.board = board;
    p.score = r.score;
    p.replayB64 = Base64Encode(SerializeReplay(r));
    if (!post)
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        if (SavePendingLocked(p))
        {
            lbStatus = "Kept for the leaderboard: your best runs post when you turn on Post my runs.";
            lbStatusGood = true;
            lbStatusWall = Wall();
        }
        return;
    }
    cvarManager->log("[AirLabs] posting run to the leaderboard (" + board + ", " + std::to_string(r.score) + ")");
    SendRun(std::move(p));
}

// Who you are: your platform account id (the leaderboard's key for you) and your name.
bool AirLabs::PlayerIdentity(std::string& platform, std::string& id, std::string& name)
{
    UniqueIDWrapper uid = gameWrapper->GetUniqueID();
    const OnlinePlatform plat = uid.GetPlatform();
    id = plat == OnlinePlatform_Epic ? uid.GetEpicAccountID() : std::to_string(uid.GetUID());
    if (id.empty() || id == "0") id = uid.GetEpicAccountID();
    if (id.empty() || id == "0") return false;
    platform = PlatformName(plat);
    name = Utf8FromWide(gameWrapper->GetPlayerName().ToWideString());
    return true;
}

std::filesystem::path AirLabs::PendingDir() const { return DataDir() / "pending"; }

void AirLabs::CountPendingLocked()
{
    int n = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(PendingDir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".run") ++n;
    lbPendingCount = n;
}

namespace
{
    // pending/<time>_<board>_<score>.run: the board and score from a waiting run's file name
    // (older files are named <time>_<board>.run: their score is read from inside).
    bool PendingInfo(const std::filesystem::path& f, std::string& board, int& score)
    {
        const std::string stem = f.stem().string();
        const std::size_t a = stem.find('_');
        if (a == std::string::npos) return false;
        const std::size_t b = stem.find('_', a + 1);
        board = stem.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        if (b != std::string::npos) { score = std::atoi(stem.c_str() + b + 1); return true; }
        std::ifstream in(f);
        std::string line;
        score = 0;
        while (std::getline(in, line)) if (line.rfind("score ", 0) == 0) { score = std::atoi(line.c_str() + 6); break; }
        return true;
    }
}

bool AirLabs::SavePendingLocked(PendingRun& p)
{
    std::error_code ec;
    std::filesystem::create_directories(PendingDir(), ec);
    // One waiting run per board: the best. A better one already waiting wins; worse ones go.
    std::vector<std::filesystem::path> worse;
    for (std::filesystem::directory_iterator it(PendingDir(), ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->path().extension() != ".run" || it->path() == p.file) continue;
        std::string board;
        int score = 0;
        if (!PendingInfo(it->path(), board, score) || board != p.board) continue;
        if (score >= p.score) { CountPendingLocked(); return false; }
        worse.push_back(it->path());
    }
    for (const auto& w : worse) std::filesystem::remove(w, ec);
    // Oldest first by name: when it was kept, then the board and score.
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    p.file = PendingDir() / (std::to_string(stamp) + "_" + p.board + "_" + std::to_string(p.score) + ".run");
    auto line = [](const std::string& v) { std::string o = v; for (char& c : o) if (c == '\n' || c == '\r') c = ' '; return o; };
    std::ofstream f(p.file, std::ios::trunc);
    f << "board " << p.board << "\nscore " << p.score << "\nplatform " << line(p.platform) << "\nid " << line(p.id)
      << "\nname " << line(p.name) << "\nreplay " << p.replayB64 << "\n";
    f.close();
    CountPendingLocked();
    return true;
}

// Posts one run. A run that can't get through (no internet, server down, too soon after the
// last post) waits in pending/ for the next try; one the server answers for good (posted, not a
// new best, or refused) leaves the queue.
void AirLabs::SendRun(PendingRun p)
{
    std::string key, ranks;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        key = lbKey; // the current key, so a run queued before your first post uses the key that post got
        ranks = RanksFieldLocked();
        lbUploading = true;
        if (p.file.empty()) { lbStatus = "Posting to the leaderboard..."; lbStatusGood = true; lbStatusWall = Wall(); }
    }
    CurlRequest req;
    req.url = LbUrl("/api/runs?format=text");
    req.verb = "POST";
    req.body = "{\"player\":{\"platform\":" + JsonString(p.platform) + ",\"id\":" + JsonString(p.id) + ",\"name\":" + JsonString(p.name) +
               "},\"key\":" + JsonString(key) + ",\"ranks\":" + JsonString(ranks) + ",\"replay\":\"" + p.replayB64 + "\"}";
    auto alive = lbAlive;
    HttpWrapper::SendCurlJsonRequest(req, [this, alive, p](int code, std::string result) mutable {
        if (!alive->load()) return;
        TextReply t = ParseTextReply(result);
        std::lock_guard<std::mutex> lock(lbMutex);
        lbUploading = false;
        const std::string newKey = t.Get("key"), pid = t.Get("pid");
        bool save = false;
        if (!newKey.empty() && newKey != lbKey) { lbKey = newKey; save = true; }
        if (!pid.empty() && pid != lbPid) { lbPid = pid; save = true; }
        if (save) SaveLeaderboardFileLocked();
        const bool retried = !p.file.empty();
        const bool transient = code == 418 || code == 0 || code == 429 || code >= 500;
        if (transient)
        {
            const bool wasQueued = retried;
            if (!wasQueued) SavePendingLocked(p);
            if (!wasQueued || lbStatus.rfind("Waiting to post", 0) == 0)
            {
                lbStatus = code == 429 ? "Waiting to post: the leaderboard asked to slow down. AirLabs tries again shortly."
                                       : "Waiting to post: couldn't reach the leaderboard. AirLabs keeps trying by itself.";
                lbStatusGood = true;
                lbStatusWall = Wall();
            }
            return;
        }
        if (retried)
        {
            std::error_code ec;
            std::filesystem::remove(p.file, ec);
            CountPendingLocked();
        }
        if (code == 200 && t.Get("ok") == "1")
        {
            lbServerBest[p.board] = std::max(t.GetInt("score", p.score), lbServerBest.count(p.board) ? lbServerBest[p.board] : 0);
            lbStatus = std::string(retried ? "Leaderboard (a run from earlier): " : "Leaderboard: ") + t.Get("message", "posted.");
            lbStatusGood = true;
            lbBoards[p.board].fetchedWall = 0.0; // show the new standings next time the page is open
        }
        else
        {
            std::string err = t.Get("error");
            if (err.empty()) err = "server error " + std::to_string(code);
            lbStatus = std::string(retried ? "A run from earlier wasn't posted: " : "Not posted: ") + err;
            lbStatusGood = false;
            if (code == 422)
            {
                // Turned away by the checks: keep the replay (the last 5) so it can be looked at.
                std::error_code ec;
                const std::filesystem::path dir = DataDir() / "rejected";
                std::filesystem::create_directories(dir, ec);
                std::string bytes;
                if (Base64Decode(p.replayB64, bytes))
                {
                    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    std::ofstream(dir / (std::to_string(stamp) + "_" + p.board + ".alr"), std::ios::binary | std::ios::trunc) << bytes;
                    std::ofstream(dir / (std::to_string(stamp) + "_" + p.board + ".txt"), std::ios::trunc) << err << "\n";
                }
                std::vector<std::filesystem::path> old;
                for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) old.push_back(it->path());
                std::sort(old.begin(), old.end());
                for (std::size_t i = 0; i + 10 < old.size(); ++i) std::filesystem::remove(old[i], ec);
            }
        }
        lbStatusWall = Wall();
        // Into the BakkesMod console (and bakkesmod.log) too, from the game thread.
        const std::string msg = "[AirLabs] " + lbStatus;
        gameWrapper->Execute([this, alive, msg](GameWrapper*) { if (alive->load()) cvarManager->log(msg); });
    });
}

void AirLabs::RetryPending()
{
    if (!lbPost || !*lbPost) return; // posting is off: they wait until it's back on
    std::filesystem::path file;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        if (lbUploading) return;
        CountPendingLocked();
        if (lbPendingCount == 0) return;
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(PendingDir(), ec), end; !ec && it != end; it.increment(ec))
            if (it->path().extension() == ".run") files.push_back(it->path());
        if (files.empty()) return;
        file = *std::min_element(files.begin(), files.end());
    }
    std::ifstream f(file);
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    TextReply t = ParseTextReply(ss.str());
    PendingRun p;
    p.board = t.Get("board"); p.score = t.GetInt("score"); p.platform = t.Get("platform"); p.id = t.Get("id"); p.name = t.Get("name");
    p.replayB64 = t.Get("replay");
    p.file = file;
    if (p.board.empty() || p.id.empty() || p.replayB64.empty())
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        std::error_code ec;
        std::filesystem::remove(file, ec); // unreadable: drop it
        CountPendingLocked();
        return;
    }
    cvarManager->log("[AirLabs] posting a run from earlier (" + p.board + ", " + std::to_string(p.score) + ")");
    SendRun(std::move(p));
}

// Runs turned away by checks that have since been dropped (the server now only flags them):
// queue them to go up again, once. Ones turned away for being the wrong preset stay put.
void AirLabs::RequeueRejected()
{
    if (lbRequeued) return;
    lbRequeued = true;
    std::error_code ec;
    const std::filesystem::path dir = DataDir() / "rejected";
    std::vector<std::filesystem::path> runs;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".alr") runs.push_back(it->path());
    for (const auto& f : runs)
    {
        std::filesystem::path why = f;
        why.replace_extension(".txt");
        std::string reason;
        {
            std::ifstream in(why);
            std::getline(in, reason);
        }
        const bool wrongPreset = reason.find("distance") != std::string::npos || reason.find("target size") != std::string::npos ||
                                 reason.find("aim doesn't match") != std::string::npos || reason.find("not a preset") != std::string::npos ||
                                 reason.find("does not apply") != std::string::npos || reason.find("bad range") != std::string::npos;
        if (wrongPreset) continue;
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        in.close();
        Replay rp;
        if (ParseReplay(ss.str(), rp))
        {
            cvarManager->log("[AirLabs] sending a run the leaderboard turned away again: " + f.filename().string());
            UploadRun(rp, false); // kept in pending/: it goes up with the others
        }
        std::filesystem::remove(f, ec);
        std::filesystem::remove(why, ec);
    }
}

// On a timer (on the game thread): post the oldest waiting run, if there is one.
void AirLabs::SchedulePendingRetry(float seconds)
{
    auto alive = lbAlive;
    gameWrapper->SetTimeout([this, alive](GameWrapper*) {
        if (!alive->load()) return;
        RequeueRejected();
        RetryPending();
        int waiting;
        {
            std::lock_guard<std::mutex> lock(lbMutex);
            waiting = lbPendingCount;
        }
        // Every 25 s while runs are waiting and posting is on (the server takes one every 20 s), else every minute.
        SchedulePendingRetry(waiting > 0 && lbPost && *lbPost ? 25.f : 60.f);
    }, seconds);
}

// ---------------------------------------------------------------------------------------------
// Reading a board
// ---------------------------------------------------------------------------------------------

void AirLabs::FetchBoard(const std::string& key, bool force)
{
    std::string pid;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        LbBoard& b = lbBoards[key];
        if (b.loading) return;
        if (!force && b.fetchedWall > 0.0 && Wall() - b.fetchedWall < 60.0) return;
        b.loading = true;
        pid = lbPid;
    }
    CurlRequest req;
    req.url = LbUrl("/api/boards/" + key + "?format=text&limit=100" + (pid.empty() ? std::string() : "&me=" + pid));
    req.verb = "GET";
    auto alive = lbAlive;
    HttpWrapper::SendCurlRequest(req, [this, alive, key](int code, std::string result) {
        if (!alive->load()) return;
        TextReply t = ParseTextReply(result);
        std::lock_guard<std::mutex> lock(lbMutex);
        LbBoard& b = lbBoards[key];
        b.loading = false;
        b.fetchedWall = Wall();
        if (code != 200 || t.Get("ok") != "1")
        {
            b.error = t.Get("error");
            if (b.error.empty()) b.error = (code == 418 || code == 0) ? "Couldn't reach the leaderboard." : "Leaderboard error " + std::to_string(code) + ".";
            return;
        }
        b.error.clear();
        b.loaded = true;
        b.title = t.Get("title");
        b.total = t.GetInt("total");
        b.max = t.GetInt("max");
        b.rows.clear();
        for (const auto& l : t.lines)
        {
            if (l.first != "row") continue;
            std::vector<std::string> f = SplitTabs(l.second);
            if (f.size() < 7) continue;
            LbRow row;
            row.pos = std::atoi(f[0].c_str()); row.pid = f[1]; row.name = f[2]; row.platform = f[3];
            row.score = std::atoi(f[4].c_str()); row.date = f[5]; row.run = std::atoi(f[6].c_str());
            if (f.size() >= 11) // 2.8.0 servers: replay (1/0), then 1v1/2v2/3v3 ranks as "tier.div" or "-"
            {
                row.replay = f[7] != "0";
                for (int i = 0; i < 3; ++i)
                {
                    int tier = 0, div = 0;
                    if (std::sscanf(f[8 + i].c_str(), "%d.%d", &tier, &div) == 2 && tier >= 0 && tier <= 22 && div >= 0 && div <= 3)
                        row.rank[i] = tier * 4 + div;
                }
            }
            b.rows.push_back(row);
        }
        b.mePos = b.meScore = b.meRun = 0;
        const std::string me = t.Get("me");
        if (!me.empty())
        {
            std::vector<std::string> f = SplitTabs(me);
            if (f.size() >= 3)
            {
                b.mePos = std::atoi(f[0].c_str()); b.meScore = std::atoi(f[1].c_str()); b.meRun = std::atoi(f[2].c_str());
                int& best = lbServerBest[key];
                best = std::max(best, b.meScore);
            }
        }
    });
}

// ---------------------------------------------------------------------------------------------
// Watching a replay
// ---------------------------------------------------------------------------------------------

void AirLabs::WatchReplay(int runId)
{
    if (runId <= 0) return;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        if (loadingRun != 0) return;
        loadingRun = runId;
        playError.clear();
    }
    CurlRequest req;
    req.url = LbUrl("/api/replays/" + std::to_string(runId) + "?format=text");
    req.verb = "GET";
    auto alive = lbAlive;
    HttpWrapper::SendCurlRequest(req, [this, alive, runId](int code, std::string result) {
        if (!alive->load()) return;
        TextReply t = ParseTextReply(result);
        Replay rp;
        std::string bytes, err;
        bool ok = code == 200 && t.Get("ok") == "1" && Base64Decode(t.Get("replay"), bytes) && ParseReplay(bytes, rp, &err) && !rp.samples.empty();
        {
            std::lock_guard<std::mutex> lock(lbMutex);
            loadingRun = 0;
            if (!ok)
            {
                playError = t.Get("error");
                if (playError.empty()) playError = !err.empty() ? err : (code == 418 || code == 0) ? "Couldn't reach the leaderboard." : "Couldn't load that replay.";
                return;
            }
            playName = t.Get("name", "Player");
            playTitle = t.Get("title");
            playRun = runId;
        }
        gameWrapper->Execute([this, alive, rp](GameWrapper* gw) {
            if (!alive->load()) return;
            play = rp;
            if (gw->IsInFreeplay())
            {
                ServerWrapper server = gw->GetGameEventAsServer();
                if (!server.IsNull())
                {
                    CarWrapper car = server.GetGameCar();
                    if (!car.IsNull()) { StartReplay(car); return; }
                }
            }
            // Not in Freeplay yet: load the map Start would use, and play it once it's up.
            pendingReplayRun = playRun;
            replayOnLoadWall = Wall();
            if (WantSpaceMap()) LoadSpaceMap();
            else LoadStandardMap();
        });
    });
}

void AirLabs::StartReplay(CarWrapper car)
{
    if (play.samples.empty() || car.IsNull()) return;
    if (state == RunState::Running) recording = false; // the run in progress is abandoned
    showResults = false;
    replayOver = false;
    RefreshKeys();
    pinLocation = V(0.f, 0.f, kArenaCentreZ);
    car.SetLocation(pinLocation);
    car.SetVelocity(V(0.f, 0.f, 0.f));
    car.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
    const ReplaySample& s0 = play.samples[0];
    car.SetRotation(Rotator(s0.pitch, s0.yaw, s0.roll));
    targetLocation = Add(pinLocation, Scale(Norm(V(UnpackUnit(s0.dx), UnpackUnit(s0.dy), UnpackUnit(s0.dz))), play.distance));
    playTick = 0;
    playEvent = 0;
    playScore = 0.0;
    playTracking = play.mode == kTrack || play.mode == kChase;
    runAim = play.aim;
    hasTarget = true;
    locking = false;
    hybridTracking = false;
    havePrevBasis = false;
    lastFlash = Flash::None;
    spaceStopped = false;
    state = RunState::Replay;
    cvarManager->log("[AirLabs] watching replay " + std::to_string(playRun));
}

void AirLabs::ReplayTick(CarWrapper car)
{
    if (playTick >= play.samples.size()) { EndReplay(); return; }
    const ReplaySample& sm = play.samples[playTick];
    car.SetRotation(Rotator(sm.pitch, sm.yaw, sm.roll));
    car.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
    const Vector dir = Norm(V(UnpackUnit(sm.dx), UnpackUnit(sm.dy), UnpackUnit(sm.dz)));
    targetLocation = Add(pinLocation, Scale(dir, play.distance));
    const Basis b = BasisFromRotator(sm.pitch, sm.yaw, sm.roll);
    aimErrorDeg = AngleDeg(AimVec(b, play.aim), dir);
    locking = aimErrorDeg <= play.targetDeg;

    // The score as it was at this point of the run (tracking ticks, then this tick's events).
    const double dt = (play.flags & kRfTickTiming) || playTick == 0
                          ? 1.0 / 120.0 : std::max(0, static_cast<int>(sm.ms) - static_cast<int>(play.samples[playTick - 1].ms)) / 1000.0;
    if (playTracking) playScore += 1000.0 * TrackCredit(aimErrorDeg, play.targetDeg) * dt;
    while (playEvent < play.events.size() && play.events[playEvent].tick <= playTick)
    {
        const ReplayEventRec& e = play.events[playEvent++];
        switch (e.type)
        {
        case kEvHit: playScore += e.value; lastFlash = Flash::Hit; break;
        case kEvMiss: playScore = std::max(0.0, playScore - e.value); lastFlash = Flash::Miss; break;
        case kEvHybridStart: playTracking = true; hybridTracking = true; break;
        case kEvHybridEnd: playTracking = false; hybridTracking = false; break;
        case kEvChaseCatch: lastFlash = Flash::Hit; break;
        case kEvChaseMiss: lastFlash = Flash::Miss; break;
        default: break;
        }
    }
    ++playTick;
}

void AirLabs::EndReplay()
{
    state = RunState::Idle;
    hasTarget = false;
    hybridTracking = false;
    locking = false;
    spaceStopped = true; // on the space map the car stays held where it is
    RefreshKeys();       // in case you've bound a key since it started
    replayOver = true;
    std::lock_guard<std::mutex> lock(lbMutex);
    lbStatus = "Replay finished: " + playName + ", " + Thousands(play.score) + ".";
    lbStatusGood = true;
    lbStatusWall = Wall();
}

// Your Rocket League ranks, as the game reports them (playlists 10 = 1v1, 11 = 2v2, 13 = 3v3;
// tier 0 = unranked, 1 = Bronze I .. 22 = Supersonic Legend; division 0-3). A playlist the game
// hasn't loaded keeps the last rank it reported. Written to the console (and bakkesmod.log) too.
void AirLabs::RefreshRealRanks(const std::string& when)
{
    auto alive = lbAlive;
    gameWrapper->Execute([this, alive, when](GameWrapper* gw) {
        if (!alive->load()) return;
        std::string line = "[AirLabs] rank check (" + when + "):";
        MMRWrapper mmr = gw->GetMMRWrapper();
        if (mmr.memory_address == 0) { cvarManager->log(line + " no MMR data at all"); return; }
        UniqueIDWrapper uid = gw->GetUniqueID();
        if (mmr.IsSyncing(uid)) line += " (syncing)";
        static const int playlists[3] = { 10, 11, 13 };
        int got[3] = { -1, -1, -1 };
        for (int i = 0; i < 3; ++i)
        {
            const int pl = playlists[i];
            const bool synced = mmr.IsSynced(uid, pl);
            const SkillRank r = mmr.GetPlayerRank(uid, pl);
            const float v = mmr.GetPlayerMMR(uid, pl);
            if (synced && r.Tier >= 0 && r.Tier <= 22 && r.Division >= 0 && r.Division <= 3) got[i] = r.Tier * 4 + r.Division;
            line += " | playlist " + std::to_string(pl) + (synced ? " synced" : " NOT synced") + ", tier " + std::to_string(r.Tier) +
                    ", div " + std::to_string(r.Division) + ", games " + std::to_string(r.MatchesPlayed) + ", mmr " + std::to_string(static_cast<int>(v));
        }
        cvarManager->log(line);
        std::lock_guard<std::mutex> lock(lbMutex);
        bool changed = false;
        for (int i = 0; i < 3; ++i)
            if (got[i] >= 0 && got[i] != lbRealRank[i]) { lbRealRank[i] = got[i]; changed = true; }
        if (changed) SaveLeaderboardFileLocked();
    });
}

std::string AirLabs::RanksFieldLocked() const
{
    std::string out;
    for (int i = 0; i < 3; ++i)
    {
        if (i) out += ',';
        out += lbRealRank[i] < 0 ? std::string("-") : std::to_string(lbRealRank[i] / 4) + "." + std::to_string(lbRealRank[i] % 4);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Your saved bests: the best score on every preset board from your own runs.csv, including runs
// from before the leaderboard existed (those have no replay). Sent each time the plugin loads;
// the server keeps whichever is higher, so only new bests change anything.
// ---------------------------------------------------------------------------------------------
void AirLabs::SendSavedBests(int attempt)
{
    if (!lbPost || !*lbPost) return; // posting is off: not this time
    std::string platform, id, name;
    if (!PlayerIdentity(platform, id, name)) return;
    struct Best { int score = -1, targets = 0, misses = 0; float onPct = -1.f; long long date = 0; };
    std::map<std::string, Best> bests;
    {
        std::lock_guard<std::mutex> lock(recordsMutex);
        for (const RunRecord& r : records)
        {
            if (!r.counted || r.userPreset > 0 || r.preset < 0 || r.preset >= PresetCount(r.mode)) continue;
            if (NewerVersion("2.5.0", r.version)) continue; // presets and scoring are the same from 2.5.0 on
            if (PresetUsesRange(r.mode, r.preset) && (r.range < 0 || r.range >= kRangeCount)) continue;
            Best& b = bests[BoardKey(r.mode, r.preset, r.range, r.NoRoll())];
            if (r.score <= b.score) continue;
            b.score = r.score; b.targets = r.targets; b.misses = r.misses; b.onPct = r.onPct; b.date = StampSeconds(r.when);
        }
    }
    if (bests.empty()) return;
    std::string key, ranks;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        key = lbKey;
        ranks = RanksFieldLocked();
    }
    std::string list;
    for (const auto& [board, b] : bests)
    {
        if (!list.empty()) list += ',';
        list += "{\"board\":" + JsonString(board) + ",\"score\":" + std::to_string(b.score) + ",\"targets\":" + std::to_string(b.targets) +
                ",\"misses\":" + std::to_string(b.misses) + (b.onPct >= 0.f ? ",\"onPct\":" + std::to_string(b.onPct) : std::string()) +
                (b.date > 0 ? ",\"date\":" + std::to_string(b.date) : std::string()) + "}";
    }
    CurlRequest req;
    req.url = LbUrl("/api/bests?format=text");
    req.verb = "POST";
    req.body = "{\"player\":{\"platform\":" + JsonString(platform) + ",\"id\":" + JsonString(id) + ",\"name\":" + JsonString(name) +
               "},\"key\":" + JsonString(key) + ",\"ranks\":" + JsonString(ranks) + ",\"version\":" + JsonString(PluginVersion()) +
               ",\"bests\":[" + list + "]}";
    cvarManager->log("[AirLabs] sending your saved bests to the leaderboard (" + std::to_string(bests.size()) + " boards)");
    auto alive = lbAlive;
    HttpWrapper::SendCurlJsonRequest(req, [this, alive, attempt](int code, std::string result) {
        if (!alive->load()) return;
        TextReply t = ParseTextReply(result);
        std::string msg;
        bool retry = false;
        {
            std::lock_guard<std::mutex> lock(lbMutex);
            const std::string newKey = t.Get("key"), pid = t.Get("pid");
            bool save = false;
            if (!newKey.empty() && newKey != lbKey) { lbKey = newKey; save = true; }
            if (!pid.empty() && pid != lbPid) { lbPid = pid; save = true; }
            if (save) SaveLeaderboardFileLocked();
            if (code == 200 && t.Get("ok") == "1")
            {
                msg = "[AirLabs] saved bests: " + t.Get("message", "sent.");
                if (t.GetInt("posted") > 0)
                {
                    lbStatus = "Leaderboard: " + t.Get("message");
                    lbStatusGood = true;
                    lbStatusWall = Wall();
                    for (auto& [k, b] : lbBoards) b.fetchedWall = 0.0; // show the new standings
                    lbServerBest.clear();                                // and learn your bests again
                }
            }
            else
            {
                retry = (code == 0 || code == 418 || code == 429 || code >= 500) && attempt < 8;
                msg = "[AirLabs] saved bests not sent (" + std::to_string(code) + " " + t.Get("error") + ")" + (retry ? ", trying again in 30 s" : "");
            }
        }
        gameWrapper->Execute([this, alive, msg, retry, attempt](GameWrapper* gw) {
            if (!alive->load()) return;
            cvarManager->log(msg);
            if (retry) gw->SetTimeout([this, alive, attempt](GameWrapper*) { if (alive->load()) SendSavedBests(attempt + 1); }, 30.f);
        });
    });
}

// ---------------------------------------------------------------------------------------------
// Updates: the leaderboard server says which version is on bakkesplugins; a newer one shows an
// Update button in the sidebar, which has BakkesMod install it (bpm_install, as the website's
// "Install with BakkesMod" button does).
// ---------------------------------------------------------------------------------------------
void AirLabs::CheckForUpdate()
{
    CurlRequest req;
    req.url = LbUrl("/api/version?format=text");
    req.verb = "GET";
    auto alive = lbAlive;
    HttpWrapper::SendCurlRequest(req, [this, alive](int code, std::string result) {
        if (!alive->load() || code != 200) return;
        TextReply t = ParseTextReply(result);
        const std::string latest = t.Get("latest");
        std::lock_guard<std::mutex> lock(lbMutex);
        lbLatest = NewerVersion(latest, PluginVersion()) ? latest : std::string();
        if (t.GetInt("plugin") > 0) lbPluginId = t.GetInt("plugin");
    });
}

void AirLabs::StartUpdate()
{
    int id;
    std::string to;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        if (lbUpdating || lbLatest.empty()) return;
        lbUpdating = true;
        id = lbPluginId;
        to = lbLatest;
    }
    auto alive = lbAlive;
    gameWrapper->Execute([this, alive, id, to](GameWrapper*) {
        if (!alive->load()) return;
        cvarManager->log("[AirLabs] updating to " + to + " (bpm_install " + std::to_string(id) + ")");
        cvarManager->executeCommand("bpm_install " + std::to_string(id), false);
    });
}

void AirLabs::RefreshKeys()
{
    std::string cfg;
    try
    {
        std::ifstream f(std::filesystem::path(gameWrapper->GetBakkesModPathW()) / "cfg" / "binds.cfg", std::ios::binary);
        if (f)
        {
            std::stringstream ss;
            ss << f.rdbuf();
            cfg = ss.str();
        }
    }
    catch (...) {}
    menuKey = BoundKey(cfg, "airlabs_menu");
    toggleKey = BoundKey(cfg, "airlabs_toggle");
}
