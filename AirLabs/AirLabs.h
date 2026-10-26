#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/wrappers/GameObject/CarWrapper.h"
#include "bakkesmod/wrappers/GameObject/BallWrapper.h"
#include "bakkesmod/wrappers/GameObject/FXActorWrapper.h"
#include "bakkesmod/wrappers/GameObject/CameraWrapper.h"
#include "bakkesmod/wrappers/GameObject/PriWrapper.h"
#include "bakkesmod/wrappers/GameEvent/ServerWrapper.h"
#include "bakkesmod/wrappers/canvaswrapper.h"
#include "bakkesmod/wrappers/ImageWrapper.h"

#include <array>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "TrainerCore.h"
#include "Replay.h"

struct ImFont;
struct ImDrawList;
struct ImVec2;

// AirLabs
// Pins your car in mid-air and trains aerial car control: flick onto targets, track a
// moving ball, or flick and then track. Presets give every player the same one-minute
// run, so scores can be compared.
class AirLabs
    : public BakkesMod::Plugin::BakkesModPlugin
    , public BakkesMod::Plugin::PluginSettingsWindow
    , public BakkesMod::Plugin::PluginWindow
{
public:
    void onLoad() override;
    void onUnload() override;

    // F2 -> Plugins settings page
    void RenderSettings() override;
    std::string GetPluginName() override;
    void SetImGuiContext(uintptr_t ctx) override; // shared by the settings page and the trainer window

    // The trainer window (its own overlay; open and close it with the airlabs_menu command)
    void Render() override;
    std::string GetMenuName() override { return "airlabs"; }
    std::string GetMenuTitle() override { return "AirLabs"; }
    bool ShouldBlockInput() override;
    bool IsActiveOverlay() override { return true; }
    void OnOpen() override { windowOpen = true; replayOver = false; }
    void OnClose() override { windowOpen = false; }

#ifdef AIRLABS_UI_HARNESS
    friend struct UiHarness; // off-game screenshot test of the UI (never defined in the plugin build)
#endif

private:
    enum class RunState { Idle, Ready, Running, Results, Replay }; // Replay: playing back a leaderboard run
    enum class GuideState { Go, Brake, Hold };
    enum class Flash { None, Hit, Miss };

    // Everything that decides how a run plays, from the chosen preset or the Custom values.
    struct DrillSettings
    {
        int mode = airlabs::kFlick, preset = 0, range = airlabs::kMedium;
        int userPreset = 0; // id of the saved preset being played (Custom runs only), 0 = none
        bool custom = false;
        float target = 6.f, holdSec = 0.2f, minTurn = 30.f, maxTurn = 180.f, timeout = 5.f, delaySec = 0.3f;
        int aim = 0;
        airlabs::Band band;
        airlabs::PathParams path;
        float hybridTrack = 3.f, runLength = 60.f, distance = 800.f;
        float chaseMin = airlabs::kChaseMoveMin, chaseMax = airlabs::kChaseMoveMax; // Chase: seconds per dash
        bool settle = false;
        float settleRate = 25.f;
        bool weakSpot = false;
        unsigned dirMask = airlabs::kAllRegions;
        bool blockJump = true;
        bool noRoll = false;   // No air roll: roll input blocked (nose aim only)
    };

    // Live numbers for the run in progress.
    struct RunStats
    {
        double score = 0.0;
        int targets = 0, misses = 0, streak = 0, bestStreak = 0;
        double sumTime = 0.0, lastTime = -1.0, bestTime = -1.0;
        double sumReaction = 0.0; int reactionN = 0;
        double sumOvershoot = 0.0; int overshootN = 0;
        double trackTime = 0.0, onTime = 0.0, errInt = 0.0, nearTime = 0.0; // nearTime: within kNearDeg (yellow or green)
        double curOff = 0.0, offSum = 0.0; int offN = 0;     // spells off the target -> reacquire time
        double bandOn[airlabs::kSpeedBands] = {}, bandTotal[airlabs::kSpeedBands] = {};
        int hybReps = 0; double hybPctSum = 0.0, lastHybPct = -1.0;
        int inputTicks = 0, analogTicks = 0; // ticks with stick input / with a part-way deflection (keyboard check)
    };

    // A finished run, as shown on the results screen.
    struct RunResult
    {
        bool valid = false;
        int mode = 0, preset = 0, aim = 0, range = -1, userPreset = 0;
        bool custom = false, counted = false;   // counted = a preset run that counts toward records
        bool noRoll = false;                    // played with No air roll
        std::string flags;
        int score = 0, tier = -1, prevBest = -1;
        airlabs::RankState rank;                // your rank on this preset after this run (runs = 0: none)
        bool newBest = false;
        int targets = 0, misses = 0, hybReps = 0;
        float avgTime = -1.f, avgReaction = -1.f, avgOvershoot = -1.f;
        float onPct = -1.f, avgErr = -1.f, reacquire = -1.f, hybAvgPct = -1.f, nearPct = -1.f;
        bool hasBands = false;
        float bandPct[airlabs::kSpeedBands] = { -1.f, -1.f, -1.f, -1.f, -1.f };
        std::string share;
    };

    // One line of the saved run history (runs.csv).
    struct RunRecord
    {
        std::string when, version;
        int mode = 0, preset = 0, aim = 0, range = -1, userPreset = 0;
        bool counted = false;
        int score = 0, tier = -1, targets = 0, misses = 0, hybReps = 0;
        float avgTime = -1.f, avgReaction = -1.f, avgOvershoot = -1.f, onPct = -1.f, avgErr = -1.f, reacquire = -1.f, hybPct = -1.f, nearPct = -1.f;
        std::string flags;
        bool NoRoll() const { return flags.find("noroll") != std::string::npos; }
    };

    struct RegionStat
    {
        int samples = 0;     // hits + misses
        float avgNorm = 0.f; // recent average time scaled to a 90-degree turn
        int hits = 0;
        float avgRaw = 0.f;  // recent average real time, hits only
    };

    // ---- settings (bound to cvars) ----
    std::shared_ptr<int> mode, presetFlick, presetTrack, presetHybrid, presetChase, baseFlick, baseTrack, baseHybrid, baseChase, rangeSel;
    // Custom values (filled in from a preset the first time you change something)
    std::shared_ptr<float> minAngleDeg, maxAngleDeg, timeoutSec;
    std::shared_ptr<int> holdMs, nextDelayMs, aimMode, customDynamic;
    std::shared_ptr<float> trackSpeed, trackTurnEvery, maxTurnChange, dynMin, dynMax, hybridTrackSec, runLength, chaseMin, chaseMax;
    std::shared_ptr<float> heightMin, heightMax, targetDistance, settleRate;
    std::shared_ptr<int> dirMask;
    std::shared_ptr<bool> weakSpot, settleMode;
    // Assists
    std::shared_ptr<bool> laserEnabled, showAimMarker, guideInvert, readout, hudGrid, blockJumpBoost;
    std::shared_ptr<bool> noAirRoll;
    std::shared_ptr<int> mapPref;   // airlabs_map: where Start takes you. 0 = space map (default), 1 = standard field
    std::shared_ptr<float> laserLength, laserWidth, guideAccel;
    std::shared_ptr<int> guideMode;
    std::shared_ptr<bool> lbPost;          // airlabs_lb_post: post your preset runs to the public leaderboard
    std::shared_ptr<std::string> lbUrl;    // airlabs_lb_url: the leaderboard server

    // ---- run state ----
    RunState state = RunState::Idle;
    int runMode = 0, runPreset = 0, runAim = 0, runRange = 0, runUserPreset = 0;
    bool runNoRoll = false;
    double gameTime = 0.0, runStart = 0.0, runEnd = 0.0;
    Vector pinLocation{ 0.f, 0.f, airlabs::kArenaCentreZ };
    Vector targetLocation{ 0.f, 0.f, 0.f };
    bool hasTarget = false;
    Vector startDir{ 1.f, 0.f, 0.f };
    float readyHeld = 0.f;
    int runBest = -1;              // your best score on this preset (-1: none yet, or a Custom run), for the HUD
    bool showResults = false;      // a run just ended: results stay on screen while you line up the next start ball
    double readyLockUntil = 0.0;   // the start ball can't be locked before this (just after a run)
    bool runSkipped = false;
    RunStats run;

    // timing: count physics ticks (120 per second) when the hook fires once per tick
    double lastWall = 0.0, rateWindowStart = 0.0;
    int rateTicks = 0;
    float measuredRate = 0.f;
    bool tickTiming = false;

    // ---- leaderboard (see Replay.h and server/) ----
    // Recording: every tick of a preset run, uploaded at the end if it's a new best.
    airlabs::Replay recReplay;
    bool recording = false;
    int recClockTicks = 0;          // ticks timed by the PC clock instead of the tick count
    void RecEvent(std::uint8_t type, float value = 0.f);
    void RecordTick(const Rotator& rot);
    void UploadRun(const airlabs::Replay& r, bool post); // post now, or (posting off) keep it to post once it's turned on
    // State shared with the HTTP callbacks (which run on another thread): lbMutex guards it.
    struct LbRow { int pos = 0; std::string pid, name, platform; int score = 0; std::string date; int run = 0; };
    struct LbBoard
    {
        std::string title, error;
        int total = 0, mePos = 0, meScore = 0, meRun = 0;
        std::vector<LbRow> rows;
        bool loading = false, loaded = false;
        double fetchedWall = 0.0;
    };
    std::mutex lbMutex;
    std::shared_ptr<std::atomic<bool>> lbAlive = std::make_shared<std::atomic<bool>>(true); // false once unloaded: late callbacks do nothing
    std::map<std::string, LbBoard> lbBoards;    // by board key, "mode-preset-range-noroll"
    std::map<std::string, int> lbServerBest;    // your best on each board, as the server last said
    std::string lbKey, lbPid;                   // this install's key, and your public id (data/AirLabs/leaderboard.txt)
    std::string lbStatus;                       // what happened to the last upload
    bool lbStatusGood = true, lbUploading = false;
    double lbStatusWall = 0.0;
    std::string lbName;                         // your Rocket League name (read on the game thread)
    // Leaderboard page selection (render thread only); lbMode -1 = start from the preset you're playing.
    int lbMode = -1, lbPreset = 0, lbRange = airlabs::kMedium;
    bool lbNoRoll = false;
    double lbNameAsked = -100.0;
    void LoadLeaderboardFile();
    // Runs that couldn't be posted (no internet, server down) wait in data/AirLabs/pending and
    // are retried every minute, or straight away with Retry now.
    struct PendingRun
    {
        std::string board, platform, id, name, replayB64;
        int score = 0;
        std::filesystem::path file; // set once it's saved in pending/
    };
    int lbPendingCount = 0;                     // guarded by lbMutex
    bool lbRetryScheduled = false;
    std::filesystem::path PendingDir() const;
    void SendRun(PendingRun p);
    bool SavePendingLocked(PendingRun& p);      // caller holds lbMutex; false if a better run on that board is already waiting
    void CountPendingLocked();                  // caller holds lbMutex
    void RetryPending();                        // posts the oldest waiting run (if posting is on and nothing is in flight)
    void RequeueRejected();                     // runs turned away by checks the server no longer has: send them again
    bool lbRequeued = false;
    void SchedulePendingRetry(float seconds);
    void SaveLeaderboardFileLocked() const;     // caller holds lbMutex
    std::string LbUrl(const std::string& path) const;
    static std::string BoardKey(int mode, int preset, int range, bool noRoll);
    void FetchBoard(const std::string& key, bool force = false);
    // Playback of a leaderboard run: the car and ball follow the replay tick by tick.
    airlabs::Replay play;
    std::string playName, playTitle, playError;
    int playRun = 0, loadingRun = 0, pendingReplayRun = 0;
    std::size_t playTick = 0, playEvent = 0;
    double playScore = 0.0;
    bool playTracking = false;
    double replayOnLoadWall = 0.0;
    void WatchReplay(int runId);
    void StartReplay(CarWrapper car);
    void ReplayTick(CarWrapper car);
    void EndReplay();
    // After a replay: a card saying it's over and how to get back to the menu, until you
    // open the window or start something.
    std::atomic<bool> replayOver{ false };
    // Your keys for the prompts, read from bakkesmod/cfg/binds.cfg ("" = not bound).
    std::string menuKey, toggleKey;
    void RefreshKeys();

    // flick
    double spawnTime = 0.0, nextSpawnAt = 0.0, lockStart = 0.0, firstReach = 0.0;
    int targetRegion = -1;
    float targetAngle = 0.f;
    bool spawnFallback = false, locking = false, reached = false, settling = false, reactionDone = false;
    float holdProgress = 0.f, maxErrAfterReach = 0.f, prevErrRaw = -1.f;
    Flash lastFlash = Flash::None;

    // tracking (Track, and the second half of Hybrid)
    airlabs::PathState path;
    airlabs::ChaseState chase;
    double chaseOnSince = -1.0; // Chase: when your aim last landed on the stopped ball (-1 = not on it) // Chase mode: the one ball and whether it's dashing
    bool hybridTracking = false;
    double hybridTrackEnd = 0.0, hybOn = 0.0, hybTotal = 0.0;

    // aim measurements
    Vector lastAim{ 1.f, 0.f, 0.f }, prevAim{ 1.f, 0.f, 0.f }, aimVel{ 0.f, 0.f, 0.f };
    float aimErrorDeg = 180.f, aimSpeedDeg = 0.f, spinDeg = 0.f;
    float inPitch = 0.f, inYaw = 0.f, inRoll = 0.f;
    double calPitch = 0.0, calYaw = 0.0, calRoll = 0.0; // learns which way each input turns the car
    airlabs::Basis prevBasis{};
    bool havePrevBasis = false;

    // stick guide (Custom runs)
    float prevErr = -1.f, closingDeg = 0.f, guideX = 0.f, guideY = 0.f;
    GuideState guideState = GuideState::Go;
    bool guideOn = false;

    // records (also read by the settings page on BakkesMod's render thread)
    std::mutex recordsMutex;

    // Saved presets ("My presets"): a name plus every Custom setting. Locked once saved, so
    // their records stay comparable. Lock order: recordsMutex before presetsMutex, never the reverse.
    struct SavedPreset
    {
        int id = 0, mode = 0;
        std::string name;
        std::vector<std::pair<std::string, std::string>> values; // cvar name -> value
    };
    std::mutex presetsMutex;
    std::vector<SavedPreset> savedPresets;
    int nextPresetId = 1;
    std::atomic<int> userSel[airlabs::kModeCount] = {};   // selected saved preset per mode (0 = none)
    std::string detachedFrom[airlabs::kModeCount];         // saved preset a Custom run was edited from
    std::vector<RunRecord> records;
    std::map<int, int> bestScore, runCount;
    std::map<int, airlabs::RankState> rankState; // current rank per preset/range (and No air roll), keyed like bestScore
    airlabs::RankState RankForLocked(int mode, int preset, int range, int userPreset, bool noRoll) const; // caller holds recordsMutex
    airlabs::RankState RankFor(int mode, int preset, int range, int userPreset, bool noRoll);
    std::map<int, std::vector<float>> history;
    std::map<int, float> bestOnPct;
    RunResult lastResult;
    int sessionRuns = 0;

    // direction grid (saved to disk), per aim
    RegionStat regionStats[2][airlabs::kRegionCount];

    // settings page state (render thread only)
    int statsMode = 0, statsPreset = 0, statsRange = airlabs::kMedium, gridView = 0, statsUserSel = 0;
    bool statsNoRoll = false; // Stats > Records: show the No air roll records
    char saveName[48] = {};
    int confirmDeleteId = 0;

    // trainer window state (render thread only)
    enum UiFont { kFontSmall, kFontBody, kFontBold, kFontHeading, kFontTitle, kFontHuge, kFontCount };
    ImFont* uiFonts[kFontCount] = {};
    bool fontsReady = false;
    int fontTries = 0;
    bool windowOpen = false, closeRequested = false, showCustomise = false;
    bool haveSpaceMap = false;      // the map file is installed
    bool hideRoundSettings = false; // the window's Play page shows round length and time per target itself
    bool spaceMap = false;          // the current map is the space map (re-checked once a second)
    bool spaceStopped = false;      // you pressed Stop on the space map: hold the car, no start ball
    bool startOnLoad = false;       // Start loaded the standard field: begin the run once it's up
    double startOnLoadWall = 0.0;   // when (gives up after 30 s if the map never loads)
    double spaceCheckWall = -10.0;
    int uiPage = 0, uiStatsPage = 0;

    std::mt19937 rng;

    // ---- setup ----
    void RegisterCvars();

    // ---- settings ----
    DrillSettings CurrentSettings() const;
    int PresetIndex(int m) const;
    int BaseIndex(int m) const; // the preset a mode's Custom settings started from
    static const char* PresetCvar(int m);
    static const char* BaseCvar(int m);
    static std::string PresetLabel(int m, int preset);
    std::string CurrentPresetLabel(const DrillSettings& s) const;

    // ---- run flow ----
    void Start();
    void Stop();
    void Toggle();
    void SkipTarget();
    void EnterReady(CarWrapper car, const DrillSettings& s);
    void BeginRunning(const airlabs::Basis& b, const DrillSettings& s);
    void EndRun(CarWrapper car, const airlabs::Basis& b, const DrillSettings& s);

    // ---- per physics tick ----
    void OnPhysicsTick(CarWrapper car, void* params);
    void ReadyTick(const Vector& aim, const DrillSettings& s, float dt);
    void FlickTick(const airlabs::Basis& b, const Vector& aim, const DrillSettings& s, float dt);
    void TrackingStep(const airlabs::Basis& b, const Vector& aim, const DrillSettings& s, float dt);
    void PlayChaseSettings(const DrillSettings& s);
    void ChaseStep(const airlabs::Basis& b, const Vector& aim, const DrillSettings& s, float dt);
    static airlabs::ChaseParams ChaseParamsFor(const DrillSettings& s);
    void SpawnTarget(const airlabs::Basis& b, const DrillSettings& s);
    void FinishTarget(bool success, const airlabs::Basis& b, const DrillSettings& s);
    void StartHybridTrack(const airlabs::Basis& b, const DrillSettings& s);
    void EndHybridRep(const DrillSettings& s);
    float ArenaLimit(const Vector& from, const Vector& dir) const;
    Vector BallSpot(const Vector& dir, const DrillSettings& s) const;

    // ---- direction grid / weak spot ----
    std::array<double, airlabs::kRegionCount> RegionWeights(const DrillSettings& s) const;
    void RecordRegion(bool hit, double seconds, int aim);
    std::filesystem::path DataDir() const;
    // The AirLabs space map (data/AirLabs/AirLabs_Space.udk). On it the car is held in the air
    // from the moment you spawn, and a run's start ball is waiting.
    std::filesystem::path SpaceMapPath() const;
    std::string SpaceMapUrlPath(const std::filesystem::path& map); // a space-free path the game's "start" command accepts, or ""
    void LoadSpaceMap();
    void LoadStandardMap();         // Freeplay on the standard field (DFH Stadium)
    bool WantSpaceMap() const;      // Start goes to the space map (the default, when it's installed)
    void MapSwitch(float w, float h); // the Space | Standard switch next to START
    float mapKnob = -1.f;           // its knob, sliding between 0 (Space) and 1 (Standard); -1 = not shown yet
    bool OnSpaceMap();
    void MigrateOldData();
    void LoadRegionStats();
    void SaveRegionStats() const;
    void ResetDirectionStats();

    // ---- records ----
    // One record per mode, preset and range (Flip Reset has a single record: it ignores range).
    static int RecordKey(int m, int preset, int range)
    {
        int slot = airlabs::PresetUsesRange(m, preset) ? std::clamp(range, 0, airlabs::kRangeCount - 1) : airlabs::kRangeCount;
        return (m * 16 + preset) * 4 + slot;
    }
    static std::string RangeLabel(int m, int preset, int range);       // "Short", "350 uu", or "" (Custom)
    static std::string PresetRangeLabel(int m, int preset, int range);  // e.g. "Control (Medium)"
    std::string RunLabel(int m, int preset, int range, int userPreset);  // as above, or a saved preset's name

    // ---- saved presets ----
    static int UserKey(int id) { return 1000000 + id; }
    // No air roll runs keep their own records, next to the normal ones.
    static constexpr int kNoRollKey = 500000;
    static int RunKey(int m, int preset, int range, int userPreset, bool noRoll)
    {
        return (userPreset > 0 ? UserKey(userPreset) : RecordKey(m, preset, range)) + (noRoll ? kNoRollKey : 0);
    }
    void LoadSavedPresets();
    void WriteSavedPresets();                    // caller holds presetsMutex
    int SaveCurrentAsPreset(const std::string& name);
    void SelectSavedPreset(int mode, int id);
    void DeleteSavedPreset(int id);
    std::string SavedPresetName(int id);          // "" if it doesn't exist
    void LoadRecords();
    void AddRecordLocked(const RunRecord& r);
    void AppendRecordToFile(const RunRecord& r) const;
    std::string BuildShareLine(const RunResult& r) const;
    void ResetSession();

    // ---- stick guide ----
    void UpdateGuide(const airlabs::Basis& car, const Vector& aim, const Vector& toTarget, float dt, const DrillSettings& s, bool tracking);

    // ---- HUD ----
    void Render(CanvasWrapper canvas);
    void DrawLaser(CanvasWrapper& canvas);
    void DrawGuide(CanvasWrapper& canvas, const Vector2& screen);
    void DrawRegionGrid(CanvasWrapper& canvas, int x, int y);
    void DrawReadout(CanvasWrapper& canvas, const Vector2& screen);
    void DrawReplayOver(CanvasWrapper& canvas, const Vector2& screen);

    // ---- settings page ----
    void TabPlay();
    void TabAssists();
    void TabStats();
    void TabHelp();
    void PlaySettingsBody(const DrillSettings& s, bool withHeading);

    // ---- trainer window (TrainerWindow.cpp) ----
    void LoadFonts();
    void EnsureFonts();
    // Rank icons: optional PNGs in data/AirLabs/ranks (see README). Without them a coloured
    // badge with the short name (GC1) is drawn instead.
    std::shared_ptr<ImageWrapper> rankIcon[airlabs::kTierCount]; // one per division (or shared per rank)
    bool rankIconShared[airlabs::kTierCount] = {};                // shared per rank: the division is shown as pips
    std::shared_ptr<ImageWrapper> unrankedIcon;                   // unranked.png: shown while placing
    bool rankIconsTried = false;
    void EnsureRankIcons();
    // Draws your rank's badge at p (dl == nullptr: just measure). Returns its width (0 with no runs).
    float RankBadge(const airlabs::RankState& rs, float height, ImFont* font, ImDrawList* dl, ImVec2 p);
    bool RankChip(const char* id, const char* label, bool selected, ImFont* font, ImFont* badgeFont, const airlabs::RankState& rs, float height);
    float RankChipWidth(const char* label, ImFont* font, ImFont* badgeFont, const airlabs::RankState& rs, float height);
    void ToggleWindow();
    void DrawTrainerWindow();
    void WinSidebar(float height);
    void WinPlay();
    void WinStats();
    void WinAssists();
    void WinHelp();
    void WinLeaderboard();
    void WinSettings(const DrillSettings& s, float width); // every setting, on the Play page
    void PlayFlickSettings(const DrillSettings& s, bool hybridPart);
    void PlayTrackSettings(const DrillSettings& s, bool hybridPart);
    void PlayDirections(const DrillSettings& s);
    void PlayHeight(const DrillSettings& s);
    void StatsOverview();
    void StatsRecords();
    void StatsDirections();
    void StatsHistory();
    void SettingsDirectionGrid();

    // Settings widgets show the value in use (from the preset, or Custom). Changing one
    // turns the run into "Custom (based on <preset>)".
    void MakeCustom();
    void CopySettingsToCustom(const DrillSettings& s);
    void SetCvarF(const char* cvar, float v);
    void SetCvarI(const char* cvar, int v);
    bool EditF(const char* label, const char* desc, const char* cvar, float shown, float lo, float hi, const char* fmt);
    bool EditI(const char* label, const char* desc, const char* cvar, int shown, int lo, int hi, const char* fmt);
    bool EditCheck(const char* label, const char* desc, const char* cvar, bool shown);
    bool EditRadio(const char* label, const char* desc, const char* cvar, int shown, std::initializer_list<const char*> options);
    // Plain cvar widgets for assists (these never affect whether a run counts).
    bool UiSliderF(const char* label, const char* desc, const char* cvar, float lo, float hi, const char* fmt);
    bool UiCheck(const char* label, const char* desc, const char* cvar);
};
