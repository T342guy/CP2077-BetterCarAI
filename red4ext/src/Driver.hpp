// BCAI Autodrive+, our own driving AI: what its parts share. Plugin.hpp has
// the interface the rest of the plugin uses (DriverStart, DriverStep ...).
//
// It plans a route over the lane graph (A* over lanes and their links), then
// every frame turns the car's position, heading, velocity and what it sees
// into throttle, brake and steering, which Controls.cpp feeds into the car.
//
// Steering: pure pursuit on the planned line gives the curvature to drive.
// Speed: lane speed limits, the speed each bend can be taken at, braking
// ahead of time, and a following distance give the acceleration wanted.
// The game puts curves on the controls (a little steering barely turns the
// car, a little brake already brakes hard), so the driver learns how this car
// answers the wheel and pedals while it drives (CarResponse) and inverts
// that, with slow trims for what the learned response gets wrong.
//
//   Driver.cpp        the interface; starting, stopping, and each frame: how
//                     the car moves, steering, the speed to go, the pedals,
//                     backing up when stuck; learning the car
//   DriverRoute.cpp   the route, the line to drive along it (the room either
//                     side, the lanes alongside) and the speed plan
//   DriverSense.cpp   the sensor rays, the game's vehicle list, and what was
//                     seen turned into boxes along the path
//   DriverLanes.cpp   where to drive on the road: the lane to keep to,
//                     passing, going round, dropping back, junctions
//   DriverReport.cpp  what it is doing and why: the overlay, the log, the
//                     decisions it made (events)

#pragma once

#include "Navigator.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace BCAI::Autodrive
{
constexpr int kBendWindow = 4;         // metres either side for curvature
constexpr float kCruiseLength = 800.0f;
constexpr float kHalfWidth = 1.0f; // half a car's width, with a little to spare
constexpr float kBehind = 45.0f;   // how far back the sensor keeps what it saw
constexpr float kCarWidth = 1.9f, kCarLength = 4.6f;
// The game's cars corner hardest with the body 8-15 degrees off the way they
// go (driving tests: about 2 g there, falling off past 20-30 degrees), the
// slip growing with the cornering (about 7 degrees at 1 g). Sliding is slip
// well past what the cornering asks for (CarResponse::slipStiff), or past
// this slip angle (radians) whatever it corners at.
constexpr float kSlideSlip = 0.45f;
// The grip a car new to Autodrive+ corners at (m/s^2), until cornering
// cleanly shows it holds more (up to the style's): a car with less grip than
// the style asks for would otherwise slide off at its first hard corner.
constexpr float kStartGrip = 10.0f;
// Sideways acceleration moves across lanes are planned with, at most (m/s^2):
// a lane change stays a lane change however hard the car can corner.
constexpr float kMoveLat = 9.3f;

// y = k·x^p, fitted while driving: least squares on ln y = ln k + p·ln x,
// older samples fading out and pulled towards a prior, so a few odd samples
// can't run away with it.
struct PowerFit
{
    float k0, p0, pMin, pMax;
    float k, p;
    double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;

    PowerFit(float aK, float aP, float aPMin, float aPMax) : k0(aK), p0(aP), pMin(aPMin), pMax(aPMax), k(aK), p(aP) {}

    // A sample worth `w` (seconds of driving); the old ones keep `keep` of their weight.
    void Add(float x, float y, float w, float keep)
    {
        double lx = std::log(x), ly = std::log(y);
        sw = sw * keep + w;
        sx = sx * keep + w * lx;
        sy = sy * keep + w * ly;
        sxx = sxx * keep + w * lx * lx;
        sxy = sxy * keep + w * lx * ly;
        const double prior = 1.5; // as strong as 1.5 s of samples
        double a0 = std::log(k0);
        double m11 = sw + prior, m12 = sx, m22 = sxx + prior;
        double r1 = sy + prior * a0, r2 = sxy + prior * p0;
        double det = m11 * m22 - m12 * m12;
        double pp = det > 1e-9 ? (m11 * r2 - m12 * r1) / det : p0;
        pp = std::clamp(pp, double(pMin), double(pMax));
        p = float(pp);
        k = float(std::exp((r1 - m12 * pp) / m11));
    }

    float X(float y) const { return y <= 0.0f ? 0.0f : std::min(std::pow(y / k, 1.0f / p), 1.0f); }
};

// How this car answers its controls, learned while it drives. The game puts
// curves on the inputs (a little steering barely turns the car, a little
// brake already brakes hard), so the driver measures them and inverts them.
struct CarResponse
{
    static constexpr int kBuckets = 12; // speeds 0, 5, 10 ... 55 m/s
    static constexpr float kBucket = 5.0f;
    uint64_t car = 0;
    // Path curvature (1/m) for a steering input, times 1 + (v/25)^2 (cars
    // turn less at speed). Measured in game: about 0.17·steer^2.
    PowerFit steer{0.17f, 2.0f, 0.7f, 3.5f};
    // Slowing (m/s^2) from the brake on top of coasting. In game a 0.2 brake
    // already slows about 6 m/s^2.
    PowerFit brake{8.0f, 0.3f, 0.1f, 2.0f};
    // Speeding up at full throttle, and coasting (both m/s^2), by speed.
    float full[kBuckets], coast[kBuckets];
    // The sideways acceleration (m/s^2) it corners up to: lowered where it
    // slid or ran wide, raised while it corners near that cleanly; 0 until
    // it has cornered (kStartGrip).
    float grip = 0.0f;
    // Where it last slid (m/s^2; 0: it hasn't): grip rises past that only slowly.
    float slidAt = 0.0f;
    // Sideways acceleration per radian of slip while it grips (the game's
    // sports cars about 86: 1 g at 6.5 degrees).
    float slipStiff = 86.0f;
    // The turn at full lock (curvature x SpeedTurn) as seen: the steering
    // fit, learned with the wheel turned a little, says far less for the
    // game's cars (they turned on 4 m across junctions; the fit said 7 m).
    float turnSeen = 0.0f;
    float Reach() const { return std::max(steer.k, turnSeen); }

    CarResponse() { Reset(0); }

    void Reset(uint64_t aCar)
    {
        car = aCar;
        steer = PowerFit{0.17f, 2.0f, 0.7f, 3.5f};
        brake = PowerFit{8.0f, 0.3f, 0.1f, 2.0f};
        grip = 0.0f;
        slipStiff = 86.0f;
        turnSeen = 0.0f;
        slidAt = 0.0f;
        for (int i = 0; i < kBuckets; ++i)
        {
            float v = i * kBucket;
            full[i] = std::max(1.0f, 3.8f - 0.11f * v);
            coast[i] = -0.4f - 0.0006f * v * v;
        }
    }

    static float Lookup(const float* aTable, float v)
    {
        float f = std::clamp(v / kBucket, 0.0f, kBuckets - 1.001f);
        int i = int(f);
        return aTable[i] + (aTable[i + 1] - aTable[i]) * (f - i);
    }

    // Moves the table towards `value` at speed v, by `rate` (0..1).
    static void Learn(float* aTable, float v, float value, float rate)
    {
        float f = std::clamp(v / kBucket, 0.0f, kBuckets - 1.001f);
        int i = int(f);
        float t = f - i;
        aTable[i] += (value - aTable[i]) * rate * (1.0f - t);
        aTable[i + 1] += (value - aTable[i + 1]) * rate * t;
    }

    static float SpeedTurn(float v) { return 1.0f + (v / 25.0f) * (v / 25.0f); }
};

struct PathPoint
{
    Point3 p;
    uint32_t lane;
    float laneS;
    // Room for the car's centre either side of the line (left positive):
    // anywhere on the road, and on this side of the oncoming lanes.
    float lo = -0.8f, hi = 0.8f, loSame = -0.8f, hiSame = 0.8f;
    bool junction = false;
    bool crossing = false; // in a junction where roads cross (not a merge or a fork)
    bool crosswalk = false; // a crosswalk crosses the line here
    float flow = 0.0f; // how far the line runs beside the lane (left positive)
    // The middles of the lanes going the same way alongside, from the lane
    // (left positive; its own about 0). The flowed line keeps them: a lane
    // here at +3.5 is driven 3.5 m left of the flowed line.
    float lanes[6] = {};
    int laneN = 0;
    // A long vehicle's front and rear ends with its centre here, heading
    // along the line: how far they are off the line (left positive). Out
    // past a bend's outside by about a^2/2R (Driver::Swing); 0 for a car.
    float swingF = 0.0f, swingR = 0.0f;
};

// Something a sensor ray hit: where, and where that is along the path.
struct Seen
{
    Point3 p;
    float s, l, time;
    int kind;   // 0 vehicle, 1 wall
    float born; // when first seen there (a moving car keeps leaving its spot)
    Point3 first; // where it was then
};

// A sideways move from the line: from `from` at path distance `start` to
// `to`, over `len` metres (a smooth S).
struct Shift
{
    float from = 0.0f, to = 0.0f, start = 0.0f, len = 1.0f;

    float At(float s) const
    {
        float u = std::clamp((s - start) / len, 0.0f, 1.0f);
        return from + (to - from) * u * u * (3.0f - 2.0f * u);
    }
};

// Something in the way, as a box along the path: a car (its sensor points
// grouped, grown to at least a car's size while only part of it has been
// seen), a single point of a wall, or a person (from the game's list).
struct Body
{
    float s0, s1, l0, l1;
    int kind;  // 0 vehicle, 1 wall, 2 person
    int point; // its nearest sensor point
    int track = -1;
    bool measured = false; // how it moves (along the path, sideways) is known
    float u = 0.0f, w = 0.0f;
    float seenL0 = 0.0f, seenL1 = 0.0f; // sideways, as far as its points go
    bool parked = false; // watched for a while without it going anywhere
    int known = -1;      // from the game's vehicle list (mKnown), not rays alone
    float stillFor = 0.0f; // how long it has stood still (as far as it was watched)
    bool crosswise = false; // pointing across the path (listed vehicles only)
};

// A vehicle from the game's list: where it is and how it moves, exactly. Its
// size starts as an average car (or bike) and grows to cover the ray hits on it.
struct Known
{
    Point3 p, fwd, vel;
    float front = 2.3f, rear = 2.3f, half = 0.95f;
    float listed = 0.0f; // when last listed
    bool parked = false, bike = false;
    float stillSince = -1.0f; // when it stopped (< 0: moving)
    // A person on foot; where they were listed a moment ago, and when (their
    // velocity comes from that: the game gives none).
    bool person = false;
    Point3 anchor{};
    float anchorAt = -1.0f;
};

// A car followed from frame to frame, for how fast it moves along the path
// (measured over half a second, as its seen outline jitters).
struct Track
{
    float s0, l0, l1;
    float speed = 0.0f, lspeed = 0.0f; // along the path, and sideways (left +)
    float pending = 0.0f;              // a speed seen once, waiting to be seen again
    float firstS0 = 0.0f, stillSince = 0.0f; // where its near end was when last seen moving, and when
    int windows = 0;                   // half seconds measured
    float since = 0.0f;                // when first seen
    float lastSeen = 0.0f;
    float anchorS = 0.0f, anchorS1 = 0.0f, anchorL0 = 0.0f, anchorL1 = 0.0f, anchorTime = 0.0f;
    // All of it seen so far, while it stands still (the rays see a few
    // points of it at a time).
    float extS0 = 0.0f, extS1 = 0.0f, extL0 = 0.0f, extL1 = 0.0f;
    bool measured = false;
};

// A sideways move judged against what the sensor saw.
struct Way
{
    float clear = 0.0f;    // how far it can be driven before running into something (up to the horizon)
    float block = -1.0f;   // gap from the bumper to the nearest thing ahead in the way, < 0: none
    float oncoming = 0.0f; // metres driven in the oncoming lanes
    int kind = -1, point = -1, track = -1;
    bool contact = false;  // it runs into something within the horizon
    bool off = false;      // it leaves the road (by more than the car is off it already)
    float contactU = 0.0f;   // how fast what it runs into goes along the path
    float contactS0 = 0.0f;  // where its rear end is along the path now
    bool contactAlongside = false; // that is beside the car already (dropping back avoids it)
    float squeeze = 0.0f;          // how much closer than now it comes to a car alongside (m)
    bool endsOncoming = false;     // where the move ends is in the oncoming lanes
    float offAt = -1.0f;           // how far ahead it leaves the road (when off)
    // What is in the way: how it moves along the path and sideways, if known.
    float blockU = 0.0f, blockW = 0.0f;
    bool blockMeasured = false;
    int blockKnown = -1;
};

// What each driving style does differently (DriverSettings.style: 0 city,
// 1 aggressive, 2 madman).
struct StyleRules
{
    const char* name;
    // Going round a car stopped in its lane once it has stood this long (s),
    // unless it waits in a queue (a car stopped just in front of it, or a
    // junction where roads cross just past it); a madman ignores queues.
    float standingAfter;
    bool queues;
    // Overtaking a moving car through the oncoming lanes: slow means under
    // this share of the speed it means to go (city), or this much slower
    // (m/s, the others). Careful: not at crossings, only where the road is
    // straight enough to see along, and only if a car coming from beyond
    // what the game lists couldn't get there first.
    float slowShare, slowBy;
    bool carefulPass;
    // Lanes: ending a move between two lanes counts this much against it;
    // a new move has to score this much better; after a lane change the
    // next waits this long (s); a lane has to look clear this long first.
    float laneGapWeight, switchMargin, holdAfterMove, clearFor;
    // Keeps right where there are at least this many lanes going its way.
    int keepRightFrom;
    // Following: the gap (m) left to a moving car when it stops.
    float followGap;
    // A junction with a car across that may go: able to stop before it
    // braking at this (m/s^2), creeping at this much more (m/s); a careful
    // style also counts cars it hasn't watched long enough to know.
    float cautionDecel, cautionCreep;
    bool cautious;
    // Going round something standing: past it at about sqrt(this) m/s.
    float roundBase;
    // Slowing for a crosswalk ahead with someone by it.
    bool crosswalkCare;
};

constexpr StyleRules kStyles[3] = {
    {"city", 20.0f, true, 0.33f, 0.0f, true, 6.0f, 8.0f, 3.0f, 1.5f, 1, 5.0f, 5.0f, 2.0f, true, 64.0f, true},
    {"aggressive", 5.0f, true, 0.0f, 4.0f, true, 3.0f, 5.0f, 2.0f, 1.0f, 3, 4.0f, 7.0f, 3.0f, true, 64.0f, false},
    {"madman", 1.5f, false, 0.0f, 2.0f, false, 1.5f, 3.0f, 1.0f, 0.5f, 99, 2.5f, 7.0f, 3.0f, false, 121.0f, false},
};

// How the car moves this frame, as Step measured it.
struct Motion
{
    float dt;
    Point3 f, left;  // forward and left, level
    float v, vLat;   // speed along and across (left +)
    float gravity;   // how much the slope slows the car (m/s^2)
    float slip;      // slip angle (radians)
};

// What Avoid works from this frame and what it found on the way.
struct Situation
{
    float v = 0.0f, dt = 0.0f, horizon = 0.0f, len = 0.0f, now = 0.0f;
    const StyleRules* rules = nullptr;
    float laneNeed = 0.0f, laneHome = 0.0f;
    const char* homeWhy = "";     // why that lane (for the log)
    bool laneEnds = false;        // the lane the car is in runs out (it has to move over)
    float laneEndsAt = -1.0f;
    Shift home;                   // the move back to the lane to keep to
    Way homeWay;
    const Body* homeBody = nullptr; // what is in the way there
    float passNeed = 0.0f;        // how far past a moving car takes
    float roadUntil = 1e9f;       // the road has to be wide enough until this far (going round)
    bool standing = false;        // that stands still (a way round may use the oncoming lanes)
    bool ownSide = true;          // the car is on its own side of the road
    bool inOncoming = false;      // not, and not off the road either: in the oncoming lanes
    bool headOn = false;          // out passing, with something coming the other way
    bool mayPass = false;         // may use the oncoming lanes now
    bool junction = false;        // roads cross just ahead
    Way keptNow;                  // the move under way
    Way keptWay;                  // the same, as far as going round or past needs the road
    float offSoon = 0.0f;         // leaving the road closer than this is the worst
    float dropTo = -1.0f;         // the speed to drop back behind a car alongside (< 0: none)
    bool canDrop = false;
    bool urgent = false, emergency = false, trouble = false;
};

// Something it decided, or that happened to it, for the log: when (seconds
// into the drive), at what speed (m/s), where along the route (m), what.
struct Event
{
    float time, v, s;
    std::string what;
};

// What the log last said about each thing it follows, so it notes changes
// rather than every frame. Nothing here changes how the car drives.
struct Noted
{
    int style = -1;
    DriverSettings settings{}, seen{};
    float seenSince = 0.0f;
    bool arriving = false;
    bool passing = false, passMoving = false, waitingToPass = false;
    bool laneEnds = false;
    const char* why = "";     // what held the speed down, when last noted
    const char* whyNow = "";  // and now
    float whySince = 0.0f;
    bool noGrip = false;
    float grip = 0.0f;
    float hardBrakeAt = -10.0f;
    bool offRoad = false, offNoted = false; // off the road now; that noted (after 3 m)
    float offFrom = 0.0f;     // mOffRoad when it went off
    float crosswalkAt = -100.0f; // the crosswalk last slowed for (along the path)
};

class Driver
{
public:
    bool active = false;
    std::string status = "idle";

    // Driver.cpp
    std::string Start(const LaneGraph& aGraph, const Point3& aPos, const Point3& aForward, const Point3* aGoal);
    void Stop(const std::string& aWhy);
    // One frame. Returns false when the driver has stopped (arrived, lost).
    bool Step(const LaneGraph& aGraph, const Point3& aPos, const Point3& aFwd, const Point3& aVel, float aDt,
              const DriverSettings& aSet, DriverOutput& aOut);

    // DriverSense.cpp
    std::vector<DriverRay> Rays(float aFront);
    size_t Sense(const std::vector<Point3>& aEnds, const std::vector<bool>& aHit);
    void SetBody(float aFront, float aRear, float aHalfWidth);
    size_t SetVehicles(const std::vector<DriverVehicle>& aList);

    // DriverReport.cpp
    float OffRoad() const { return mOffRoad; }
    float RoadOut(float aHalf) const;
    size_t KnownCount() const { return mKnown.size(); }
    int BigMoves() const { return mBigMoves; }
    int Dithers() const { return mDithers; }
    std::vector<DriverSeenPoint> SeenPoints() const;
    DriverInfo State() const;
    std::vector<Point3> Ahead(float aDistance, float aStep) const;
    std::vector<float> AheadSpeeds(float aDistance, float aStep) const;
    std::vector<Point3> Room(float aDistance, float aStep) const;
    std::string Describe() const;
    std::string Telemetry() const;
    std::string Hud() const;
    std::string Dump(const std::filesystem::path& aPath) const;
    // The events noted since last asked, a line each, oldest first.
    std::vector<std::string> TakeEvents();

private:
    std::vector<PathPoint> mPath; // every metre
    std::vector<float> mS, mLimit, mSpeed;
    float mProgress = 0.0f;
    bool mHasGoal = false;
    Point3 mGoal{};
    DriverSettings mSettings{};
    GeoCache mGeo;
    Point3 mPos{}, mFwd{1, 0, 0};
    float mFront = 2.4f, mRear = 2.4f; // the car's front and rear from its centre
    float mHalfWidth = kHalfWidth;      // half its width, with a little to spare
    std::vector<Known> mKnown;
    float mHeading = 0.0f, mYawRate = 0.0f, mSteer = 0.0f, mLastV = 0.0f, mAccel = 0.0f;
    float mSteerFelt = 0.0f, mThrottleFelt = 0.0f, mBrakeFelt = 0.0f;
    float mCurvTrim = 0.0f, mAccelWant = 0.0f, mAccelLagged = 0.0f, mAccelTrim = 0.0f, mGripSpeed = 100.0f;
    float mSlideTime = 0.0f; // how long it has been sliding (s)
    bool mBraking = false; // on the brake (Pedals: a little slowing is left to coasting)
    int mPedals = 0;
    float mPedalTime = 0.0f;
    CarResponse mCar;
    bool mHaveHeading = false;
    // Sensing and avoiding.
    std::vector<DriverRay> mRays;
    std::vector<Seen> mSeen;
    float mClock = 0.0f;
    int mCarCursor = 0, mWallCursor = 0, mBackCursor = 0, mAcrossCursor = 0;
    Shift mShift;
    float mShiftAge = 0.0f;
    bool mPassing = false; // getting past something standing still, may use the oncoming lanes
    bool mPassMoving = false; // that is a slow car (not something standing)
    float mNoPassUntil = 0.0f;
    // Not changing its mind: the next lane change waits until then; the
    // move that started the wait; how long the way back has been clear.
    float mHoldUntil = 0.0f, mHeldTo = 0.0f;
    // Per sideways target (every half metre from -13 to +13 m): since when
    // a move there has looked clear (nothing it would run into soon).
    float mClearSince[53] = {};
    // The last sideways move of 1 m or more: to where, by how much, when; how
    // often one was undone soon after.
    float mMindTo = 0.0f, mMindBy = 0.0f, mMindAt = -10.0f;
    int mDithers = 0;
    std::vector<Body> mBodies;
    std::vector<Track> mTracks;
    float mBlock = -1.0f;
    int mBlockKind = -1, mBlockPoint = -1, mBlockTrack = -1, mBlockKnown = -1;
    float mBlockU = 0.0f, mBlockW = 0.0f; // how what is in the way moves (when measured)
    bool mBlockMeasured = false;
    float mLeadSpeed = 0.0f, mStuck = 0.0f, mLastLateral = 0.0f;
    float mLeadGap = -1.0f; // the gap mLeadSpeed was last measured at
    float mProfileLat = 0.0f, mProfileDecel = 0.0f, mProfileTurn = 0.0f; // the limits the speed plan was made with
    int mStartHits = 0;     // rays that hit something where they start
    float mOffRoad = 0.0f;  // metres driven off the mapped road
    float mLastTo = 0.0f;   // the last move 2 m or more sideways, and how many
    int mBigMoves = 0;
    // Backing up: time left, the gap ahead when it started, how often, how
    // much room there is behind (rear rays, cast while slow).
    float mBoxed = 0.0f, mReverse = 0.0f, mReverseGap = 0.0f, mRearClear = 1e9f;
    int mReversals = 0;
    size_t mRearFrom = SIZE_MAX;
    bool mNoWay = false; // no sideways move gets past what is ahead
    bool mDoor = false;  // but there is a gap beside it, if the car had more room
    float mRoundGap = -1.0f; // going round something standing this far ahead
    bool mRoundPerson = false; // that is a person
    float mCrosswalkGap = -1.0f; // a crosswalk this far ahead with someone by it (crosswalk care)
    float mCautionGap = -1.0f; // a junction this far ahead, with a car across it that may go
    float mDropBack = -1.0f;   // dropping back behind a car alongside, to this speed
    float mWideGap = -1.0f;    // a long vehicle waiting this far before a turn its ends swing wide in
    bool mReturning = false;   // moving from the oncoming lanes back to its own side
    float mLateral = 0.0f, mWant = 0.0f; // the car's offset from the line; the speed it means to go
    float mSlope = 0.0f;                 // rise per metre the car points (uphill +)
    float mSteerSign = 1.0f, mSignEvidence = 0.0f;
    bool mSignKnown = false;
    int mReplans = 0;
    DriverOutput mLast{};
    float mLastWantRate = 0.0f, mLastPlanSpeed = 0.0f, mLastSlip = 0.0f;
    const char* mSpeedWhy = "";
    // The decision log: events not yet taken (the last 200), and what it
    // last said about each thing.
    std::deque<Event> mEvents;
    Noted mNoted;

    template<typename... Args>
    void Note(const char* aFormat, Args... aArgs)
    {
        if (mEvents.size() >= 200)
            mEvents.pop_front();
        mEvents.push_back({mClock, mLastV, mProgress, Format(aFormat, aArgs...)});
    }

    // Driver.cpp: the parts of a frame.
    Motion Feel(const Point3& aPos, const Point3& aFwd, const Point3& aVel, float aDt, const DriverSettings& aSet);
    void UseSettings(const DriverSettings& aSet);
    int KeepOnRoute(const LaneGraph& aGraph, const Point3& aPos, const Motion& m, DriverOutput& aOut, float& aLateral);
    void ForgetOldSightings();
    float Steer(const Point3& aPos, const Motion& m, float& aMaxSteer, float& aWantRate);
    float TargetSpeed(const Motion& m, float aMaxSteer, float aWantRate, float& aFollowNeed);
    void Pedals(const Motion& m, float aTargetSpeed, float aFollowNeed, DriverOutput& aOut);
    bool Unstick(const Motion& m, float aMaxRate, DriverOutput& aOut);
    void LearnResponse(float v, float aSlip, float aGravity, float dt);
    void LearnSteerSign(float v, float dt);

    // DriverRoute.cpp
    bool Plan(const LaneGraph& aGraph, const Point3& aPos, const Point3* aFwd, const Spot* aFrom, std::string& aMsg);
    void Build(const LaneGraph& aGraph, const std::vector<Leg>& aLegs, const Point3& aPos);
    Point3 NormalAt(size_t i) const;
    void MeasureRoom(const LaneGraph& aGraph);
    void Flow(float aStartLateral);
    void Profile();
    void Swing();
    bool Project(const Point3& aPos, float aFrom, float aTo, float& aS, float& aLateral) const;
    bool Locate(const Point3& aPos, float& aLateral);

    size_t Index(float aS) const
    {
        return std::min<size_t>(size_t(std::max(aS, 0.0f)), mPath.size() - 1);
    }

    Point3 At(float aS) const
    {
        aS = std::clamp(aS, 0.0f, mS.back());
        size_t i = std::min<size_t>(size_t(aS), mPath.size() - 2);
        return Lerp(mPath[i].p, mPath[i + 1].p, aS - float(i));
    }

    // Left of the path's direction at aS.
    Point3 Normal(float aS) const
    {
        Point3 a = At(aS - 1.0f), b = At(aS + 1.0f);
        float dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
        return l > 1e-4f ? Point3{-dy / l, dx / l, 0} : Point3{0, 1, 0};
    }

    // Where the car means to be at aS: the line plus the sideways move.
    Point3 Driven(float aS) const
    {
        Point3 c = At(aS), n = Normal(aS);
        float d = mShift.At(aS);
        return {c.x + n.x * d, c.y + n.y * d, c.z};
    }

    float SpeedAt(float aS) const
    {
        aS = std::clamp(aS, 0.0f, mS.back());
        size_t i = std::min<size_t>(size_t(aS), mPath.size() - 2);
        float t = aS - float(i);
        return mSpeed[i] + (mSpeed[i + 1] - mSpeed[i]) * t;
    }

    // DriverSense.cpp
    // Where a listed vehicle is by now (it was listed a moment ago).
    Point3 KnownAt(const Known& k) const
    {
        float dt = std::clamp(mClock - k.listed, 0.0f, 0.5f);
        return {k.p.x + k.vel.x * dt, k.p.y + k.vel.y * dt, k.p.z + k.vel.z * dt};
    }
    int Absorb(const Point3& aHit);
    void Gather(float aHorizon);
    bool OnKnown(const Point3& p) const;
    void Follow(Body& b);

    // DriverLanes.cpp
    void Avoid(float v, float dt);
    Situation Survey(float v, float dt);
    void JudgePassing(Situation& c);
    float PassLength(const Body& aBody, float v) const;
    bool Standing(const Situation& c) const;
    float RowEnd(const Situation& c, const Body& aFirst) const;
    bool SlowPassClear(const Situation& c) const;
    bool OncomingClear(const Situation& c) const;
    float DropBackSpeed(const Situation& c) const;
    float Score(const Situation& c, const Shift& aShift, const Way& aWay, bool aKept) const;
    std::vector<std::pair<Shift, Way>> Options(const Situation& c) const;
    Way Choose(Situation& c, const std::vector<std::pair<Shift, Way>>& aOptions, Way& aBestWay);
    void CountMoves(const Situation& c);
    void Commit(const Situation& c, const Way& aChosen);
    void WatchJunction(const Situation& c);
    void WatchWideTurn(const Situation& c);
    void WatchCrosswalks(const Situation& c);
    float WideTurnWait(const Situation& c) const;
    void FindDoor(const Situation& c, const Way& aBestWay);
    static int ClearBin(float aTo) { return std::clamp(int(std::lround(aTo * 2.0f)) + 26, 0, 52); }
    void Oncoming(float aRange, float& aGap, float& aSpeed) const;
    float LaneGap(float aS, float aOffset) const;
    float NearestLane(const PathPoint& aPt, float aOffset, float& aGap) const;
    float KeptLane(float aNeed) const;
    bool LaneGoesOn(float aOffset, float aNeed, float& aFirst, bool aStrict = false, float* aEndsAt = nullptr) const;
    float HomeLane(float aNeed, const char** aWhy = nullptr) const;
    bool OnOwnSide() const;
    float MinShiftLength(float aBy) const;
    Way Clearance(const Shift& aShift, float aHorizon, float aRoadUntil = 1e9f, float aRoadAtLeast = 0.0f) const;
    const StyleRules& Rules() const { return kStyles[std::clamp(mSettings.style, 0, 2)]; }

    // DriverReport.cpp: the decision log (Note, above, adds to it).
    // Which lane aOffset is in, counted from the right (1 = rightmost; 0:
    // none), of aCount going this way here.
    int LaneNumber(float aOffset, int& aCount) const;
    // What a move runs into, in words ("car 23 m ahead at 40 km/h").
    std::string WhatIs(const Way& aWay) const;
    void NoteMove(const Situation& c, const Shift& aWas, const Way& aChosen, bool aHome, float aBest, float aKept);
    void NotePassing(const Situation& c, bool aWasPassing, bool aWasMoving);
    void NoteSettings(const DriverSettings& aSet);
    void NoteSpeed(float aTarget, bool aNoGrip, float aSlip);
    void NoteFrame(const Motion& m, const DriverOutput& aOut, bool aOffRoad);

    // Over 7 m long (a truck, a bus): its ends swing wide in tight turns.
    bool Long() const { return mFront + mRear > 7.0f; }
    // How far off the line the body is at aAt with the centre at aCentre,
    // more than the line there puts it (Swing; 0 for a car), less aSpare
    // (a margin kept anyway covers that much).
    float SwingAt(float aCentre, float aAt, float aSpare = 0.0f) const
    {
        if (!Long())
            return 0.0f;
        const PathPoint& pt = mPath[Index(aCentre)];
        float a = aAt - aCentre;
        float sw = a >= 0.0f ? pt.swingF * std::min(a * a / (mFront * mFront), 1.0f)
                             : pt.swingR * std::min(a * a / (mRear * mRear), 1.0f);
        return std::copysign(std::max(std::fabs(sw) - aSpare, 0.0f), sw);
    }

    // What this car can do: as learned, never more than the settings allow.
    float LatAccel() const { return std::min(mSettings.latAccel, mCar.grip > 0.0f ? mCar.grip : kStartGrip); }
    float MoveLat() const { return std::min(LatAccel(), kMoveLat); }
    float BrakeMax() const { return std::clamp(mCar.brake.k, 4.0f, 10.0f); }
    float Decel() const { return std::min(mSettings.decel, 0.55f * BrakeMax()); }
    float AccelAt(float v) const { return std::clamp(0.8f * CarResponse::Lookup(mCar.full, v), 0.5f, 2.0f); }
};
} // namespace BCAI::Autodrive
