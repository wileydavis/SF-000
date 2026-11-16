#include "CueRig.h"
#include "Engine.h"
#include "Session.h"
#include "core/Bus.h"
#include <cstdlib>
#include <cstring>
#include <limits>

#if JUCE_MAC || JUCE_LINUX
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
 #include <signal.h>
 #define SF0_CUE_SHM 1
#endif

namespace sf0
{
using juce::String;
using juce::var;

// ----------------------------------------------------------------------------------- the shared block
CueMemory::~CueMemory()
{
   #if SF0_CUE_SHM
    if (named) shm_unlink (shmName.toRawUTF8());
    if (data != nullptr) munmap ((void*) data, sizeof (CueShared));
   #endif
}

bool CueMemory::create()
{
   #if SF0_CUE_SHM
    // (a short name: macOS takes 31 characters at most)
    static std::atomic<int> made { 0 };
    for (int tries = 0; tries < 8 && data == nullptr; ++tries)
    {
        const String n = "/sf0cue_" + String ((int) getpid()) + "_" + String (++made);
        const int fd = shm_open (n.toRawUTF8(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) continue;
        if (ftruncate (fd, (off_t) sizeof (CueShared)) == 0)
        {
            void* m = mmap (nullptr, sizeof (CueShared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (m != MAP_FAILED) { data = static_cast<CueShared*> (m); shmName = n; named = true; }
        }
        close (fd);
        if (data == nullptr) shm_unlink (n.toRawUTF8());
    }
    if (data == nullptr) return false;
    std::memset ((void*) data, 0, sizeof (CueShared));      // (every count at nought, no cue, an empty ring)
    data->validFrom.store (std::numeric_limits<int64_t>::max());
    data->version.store (CueShared::kVersion);
    data->magic.store (CueShared::kMagic);
    return true;
   #else
    return false;
   #endif
}

bool CueMemory::open (const String& name)
{
   #if SF0_CUE_SHM
    if (data != nullptr || name.isEmpty()) return false;
    const int fd = shm_open (name.toRawUTF8(), O_RDWR, 0);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat (fd, &st) == 0 && st.st_size >= (off_t) sizeof (CueShared))
    {
        void* m = mmap (nullptr, sizeof (CueShared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m != MAP_FAILED)
        {
            auto* c = static_cast<CueShared*> (m);
            // (this program's own block, of this build's shape: another's is left alone)
            if (c->magic.load() == CueShared::kMagic && c->version.load() == CueShared::kVersion) { data = c; shmName = name; }
            else munmap (m, sizeof (CueShared));
        }
    }
    close (fd);
    return data != nullptr;
   #else
    juce::ignoreUnused (name);
    return false;
   #endif
}

void CueMemory::forgetName()
{
   #if SF0_CUE_SHM
    if (named) { shm_unlink (shmName.toRawUTF8()); named = false; }
   #endif
}

static juce::MemoryBlock blockOf (const var& v)
{
    const String text = juce::JSON::toString (v, true);
    return juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

static var message (const char* type)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("t", type);
    return var (o);
}

// --------------------------------------------------------------------------------- in the instrument
CueRig::CueRig (Engine& e, Session& s) : engine (e), ses (s) {}

CueRig::~CueRig()
{
    stop();
    // (the audio thread may be in the buffer that still had the block: two more of them, or a moment if none are being played)
    const auto b0 = engine.blocksDone();
    for (int i = 0; i < 40 && engine.blocksDone() < b0 + 2; ++i) juce::Thread::sleep (2);
    retired.clear();
}

void CueRig::send (const var& v)
{
    sendMessageToWorker (blockOf (v));
}

// (asked to go, it goes when its message thread next looks; one that is busy or stuck is ended outright. It holds
// nothing that needs keeping: it writes no file.)
void CueRig::endWorker()
{
    killWorkerProcess();
   #if SF0_CUE_SHM
    if (workerPid > 1) ::kill ((pid_t) workerPid, SIGKILL);
   #endif
    workerPid = 0;
}

void CueRig::start()
{
    if (state == starting || state == ready) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (! engine.isPrepared()) { state = gone; goneAt = now; why = "THE AUDIO HAS NOT STARTED"; startTimer (500); return; }     // (looked at again: not counted as a try)
    auto m = std::make_unique<CueMemory>();
    if (! m->create()) { state = gone; goneAt = now; starts = 99; why = "NO MEMORY COULD BE SHARED WITH IT"; if (onChange) onChange(); return; }
    engine.cueRigWant (0);
    engine.setCueFeed (nullptr);
    if (mem != nullptr) retired.push_back (std::move (mem));
    mem = std::move (m);
    ++starts;
    serial = awaiting = 0; lastId = 0;
    if (! launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile), uid(), 10000, 0))
    {
        state = gone; goneAt = now; why = "IT COULD NOT BE STARTED";
        if (onChange) onChange();
        startTimer (100);
        return;
    }
    state = starting; startedAt = now; openedRate = engine.sampleRate();
    var o = message ("open");
    o.getDynamicObject()->setProperty ("shm", mem->name());
    o.getDynamicObject()->setProperty ("rate", openedRate);
    o.getDynamicObject()->setProperty ("library", Session::libraryDir().getFullPathName());
    send (o);
    startTimer (100);
    if (onChange) onChange();
}

void CueRig::stop()
{
    stopTimer();
    engine.cueRigWant (0);
    engine.setCueFeed (nullptr);
    if (state == starting || state == ready) endWorker();
    state = off;
    awaiting = 0; lastId = 0;
    if (mem != nullptr) retired.push_back (std::move (mem));
}

void CueRig::killForTests()
{
   #if SF0_CUE_SHM
    if (workerPid > 1) ::kill ((pid_t) workerPid, SIGKILL);        // (as a crash would end it: nothing is said first)
   #endif
}

void CueRig::handleMessageFromWorker (const juce::MemoryBlock& mb)
{
    // (the connection's own thread: over to the message thread)
    const String text = mb.toString();
    juce::MessageManager::callAsync ([w = juce::WeakReference<CueRig> (this), text] { if (w != nullptr) w->took (text); });
}

void CueRig::handleConnectionLost()
{
    juce::MessageManager::callAsync ([w = juce::WeakReference<CueRig> (this)] { if (w != nullptr && (w->state == starting || w->state == ready)) w->lost ("IT STOPPED ANSWERING"); });
}

void CueRig::took (const String& text)
{
    const var m = juce::JSON::parse (text);
    const String t = m.getProperty ("t", var()).toString();
    if (t == "ready" && state == starting && mem != nullptr)
    {
        // (a second rig on the first one's bus would duck under its kick and be counted among its lanes: not used)
        workerPid = (int) m.getProperty ("pid", 0);
        if (! (bool) m.getProperty ("ownBus", false)) { starts = 99; lost ("ITS MODULES WOULD SHARE THIS RIG'S BUS"); return; }
        state = ready; why.clear();
        engine.setCueFeed (mem->get());
        mem->forgetName();                              // (both sides have it open: nothing is left behind under its name)
        if (onChange) onChange();
    }
    else if (t == "playing")
    {
        if ((int) m.getProperty ("serial", 0) == awaiting) { awaiting = 0; readyMs = (int) m.getProperty ("ms", 0); ++played; if (onChange) onChange(); }
    }
    else if (t == "failed") lost (m.getProperty ("why", "IT FAILED").toString());
}

// The second rig is not there: a cue that was its is the snapshot's clip from here, and it is started again in a
// while (a few times: one that keeps going is left off, and SET UP says why).
void CueRig::lost (const String& reason)
{
    const bool was = state == starting || state == ready;
    const auto id = ses.cueing();
    const bool wasRig = ses.cueIsRig();
    state = gone; goneAt = juce::Time::getMillisecondCounter(); why = reason;
    awaiting = 0; lastId = 0; skipOnce = false;
    engine.cueRigWant (0);
    engine.setCueFeed (nullptr);
    if (mem != nullptr) retired.push_back (std::move (mem));
    if (was) endWorker();
    if (wasRig && id != 0) { ++fellBack; ses.cue (id); }
    if (onChange) onChange();
}

bool CueRig::play (juce::int64 id, juce::uint32 lanes, bool notes)
{
    if (skipOnce) { skipOnce = false; return false; }
    if (state != ready || mem == nullptr) return false;
    const auto stamp = engine.stateStamp();
    // the same cue of the same rig as the last: the second rig still holds it, and goes on from where the song is
    if (serial != 0 && id == lastId && lanes == lastLanes && notes == lastNotes && ses.changes == lastChanges && stamp == lastStamp)
    {
        engine.cueRigWant (serial);
        return true;
    }
    const var st = ses.cueState (id);
    if (++serial <= 0) serial = 1;
    var o = message ("cue");
    o.getDynamicObject()->setProperty ("serial", serial);
    o.getDynamicObject()->setProperty ("id", String (id));
    o.getDynamicObject()->setProperty ("lanes", (juce::int64) lanes);
    o.getDynamicObject()->setProperty ("notes", notes);
    o.getDynamicObject()->setProperty ("state", st);
    send (o);
    engine.cueRigWant (serial);
    awaiting = serial; sentAt = juce::Time::getMillisecondCounter();
    lastId = id; lastLanes = lanes; lastNotes = notes; lastChanges = ses.changes; lastStamp = engine.stateStamp();
    return true;
}

void CueRig::quiet()
{
    engine.cueRigWant (0);
    awaiting = 0;
}

void CueRig::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (state == starting && now - startedAt > 12000) lost ("IT DID NOT START");
    else if (state == ready)
    {
        if (awaiting != 0 && now - sentAt > 4000)
        {
            // the cue did not come: its clip, this time
            const auto id = lastId;
            awaiting = 0; lastId = 0; ++fellBack;
            engine.cueRigWant (0);
            why = "ITS LAST CUE WAS NOT READY IN 4 S: THE CLIP WAS PLAYED";
            if (ses.cueIsRig() && ses.cueing() == id && id != 0) { skipOnce = true; ses.cue (id); skipOnce = false; }
            if (onChange) onChange();
        }
        // (the interface's rate changed: the second rig is made again at the new one)
        if (std::abs (engine.sampleRate() - openedRate) > 0.5 && engine.isPrepared()) { lost ("THE RATE CHANGED"); starts = 0; goneAt = now - 3000; }
    }
    else if (state == gone && starts < 4 && now - goneAt >= 3000) start();
}

String CueRig::says() const
{
    if (state == off) return "NO SECOND RIG IS RUNNING";
    if (state == starting) return "A SECOND RIG IS STARTING. UNTIL IT IS UP A CUE IS THE SNAPSHOT'S CLIP";
    if (state == gone) return "NO SECOND RIG: " + why + (starts < 4 ? ". IT IS TRIED AGAIN IN A MOMENT" : ". LEFT OFF") + ". A CUE IS THE SNAPSHOT'S CLIP";
    String s = "A SECOND RIG IS UP: A CUE IS THE RIG AS ITS RECALL WOULD LEAVE IT";
    if (played > 0) s << ". THE LAST ONE WAS READY IN " << readyMs << " MS";
    if (auto* f = mem != nullptr ? mem->get() : nullptr)
        if (const float l = f->load.load(); l > 0.005f && engine.cueRigWanted() != 0) s << ", AND TAKES " << juce::roundToInt (100.0f * l) << " % OF A CORE OF ITS OWN";
    if (const int g = engine.cueRigGaps(); g > 0) s << ". GAPS IN IT: " << g;
    if (fellBack > 0) s << ". FELL BACK TO THE CLIP: " << fellBack;
    return s;
}

// ------------------------------------------------------------------------------ in the second process
CueWorker::CueWorker() : juce::Thread ("SF-000 cue rig") {}

CueWorker::~CueWorker()
{
    play = false;
    signalThreadShouldExit();
    notify();
    stopThread (3000);
    ses = nullptr;
    engine = nullptr;
}

bool CueWorker::wanted (const String& commandLine)
{
    return commandLine.contains ("--" + CueRig::uid() + ":");
}

bool CueWorker::begin (const String& commandLine)
{
    return initialiseFromCommandLine (commandLine, CueRig::uid(), 10000);
}

void CueWorker::say (const var& v)
{
    sendMessageToCoordinator (blockOf (v));
}

void CueWorker::handleMessageFromCoordinator (const juce::MemoryBlock& mb)
{
    // (the connection's own thread. A cue asked for while another is being made ready takes its place: only the last is made)
    const String text = mb.toString();
    const int n = text.startsWith ("{\"t\": \"cue\"") || text.startsWith ("{\"t\":\"cue\"") ? arrived.fetch_add (1) + 1 : 0;
    juce::MessageManager::callAsync ([w = juce::WeakReference<CueWorker> (this), text, n]
    {
        if (w == nullptr) return;
        if (n != 0 && n != w->arrived.load()) return;
        w->took (text);
    });
}

void CueWorker::handleConnectionLost()
{
    // the instrument has gone (or stopped answering): so does this
    juce::MessageManager::callAsync ([] { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->quit(); });
}

void CueWorker::rest()
{
    play = false;
    notify();
    for (int i = 0; i < 2000 && isThreadRunning() && ! resting.load(); ++i) juce::Thread::sleep (1);
}

void CueWorker::took (const String& text)
{
    const var m = juce::JSON::parse (text);
    const String t = m.getProperty ("t", var()).toString();
    if (t == "open")
    {
        if (engine != nullptr) return;                  // (once: a rig at another rate is another process)
        if (! mem.open (m.getProperty ("shm", var()).toString()))
        {
            var o = message ("failed"); o.getDynamicObject()->setProperty ("why", "IT COULD NOT OPEN THE MEMORY SHARED WITH IT"); say (o);
            return;
        }
        rate = juce::jlimit (8000.0, 384000.0, (double) m.getProperty ("rate", 48000.0));
        Session::placeForRig (juce::File (m.getProperty ("library", var()).toString()));
        engine = std::make_unique<Engine>();
        engine->prepare (rate, kBlock);
        ses = std::make_unique<Session> (*engine, false);
        // its rendering is the one thing here that has to be on time: a realtime thread where the system gives one
        juce::Thread::RealtimeOptions rt;
        if (! startRealtimeThread (rt.withPriority (8).withApproximateAudioProcessingTime (kBlock, rate))) startThread (juce::Thread::Priority::highest);
        var o = message ("ready");
        o.getDynamicObject()->setProperty ("ownBus", ! sf::Bus::get().shared());
       #if SF0_CUE_SHM
        o.getDynamicObject()->setProperty ("pid", (int) getpid());
       #endif
        say (o);
    }
    else if (t == "cue" && engine != nullptr)
    {
        const int serial = (int) m.getProperty ("serial", 0);
        const auto id = m.getProperty ("id", var()).toString().getLargeIntValue();
        const auto lanes = (juce::uint32) (juce::int64) m.getProperty ("lanes", (juce::int64) Session::kAllLanes);
        const bool notes = (bool) m.getProperty ("notes", true);
        if (play.load()) juce::Thread::sleep (25);      // (the one playing goes out over 10 ms, on frames that are still coming)
        rest();
        engine->setRunning (false);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        ses->cueRigTake (m.getProperty ("state", var()), id, lanes, notes);
        const int ms = juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - t0);
        serialNow = serial;
        play = true;
        notify();
        var o = message ("playing");
        o.getDynamicObject()->setProperty ("serial", serial);
        o.getDynamicObject()->setProperty ("ms", ms);
        say (o);
    }
    else if (t == "stop") play = false;
}

// The rendering. It keeps the ring filled a little ahead of where the instrument is playing: each buffer here is the
// stretch of the instrument's song that those samples will be heard at, so the two rigs are on the same sample of the
// same bar. With the instrument's clock stopped it runs on its own, so that a cue is still heard.
void CueWorker::run()
{
    juce::AudioBuffer<float> buf (2, kBlock);
    auto* f = mem.get();
    juce::int64 w = 0;
    bool anchor = true;
    int serialAt = 0;
    double load = 0.0;
    const double blockMs = (double) kBlock * 1000.0 / rate;
   #if SF0_CUE_SHM
    const auto parent = getppid();
   #endif
    int looked = 0;
    while (! threadShouldExit())
    {
        f->beat.fetch_add (1, std::memory_order_relaxed);
       #if SF0_CUE_SHM
        // (the instrument has gone without a word, and this process's own message thread may be busy: it ends itself)
        if (++looked >= 400) { looked = 0; if (getppid() != parent) std::_Exit (0); }
       #endif
        if (! play.load() || f->want.load (std::memory_order_acquire) == 0)
        {
            resting = true; anchor = true;
            wait (2);
            continue;
        }
        resting = false;
        if (! play.load()) continue;                    // (asked to rest between those two lines: it does, at the top)
        // where the instrument is
        uint32_t q = 0; juce::int64 dev = 0; double ppq = 0.0, bpm = 120.0; int run = 0, block = 256;
        do
        {
            q = f->seq.load (std::memory_order_acquire);
            dev = f->devPos.load (std::memory_order_relaxed); ppq = f->ppq.load (std::memory_order_relaxed); bpm = f->bpm.load (std::memory_order_relaxed);
            run = f->running.load (std::memory_order_relaxed); block = f->block.load (std::memory_order_relaxed);
        }
        while ((q & 1u) != 0 || q != f->seq.load (std::memory_order_acquire));
        block = juce::jlimit (16, 8192, block);
        bpm = juce::jlimit (20.0, 400.0, bpm);
        const juce::int64 lead = 2 * (juce::int64) block + 256;
        const int serial = serialNow.load();
        if (serial != serialAt) { serialAt = serial; anchor = true; }
        // (fallen a whole buffer behind what is being played, or the instrument's count has gone back, its audio having started again: from here)
        if (! anchor && (w + kBlock <= dev || w > dev + lead + (juce::int64) CueShared::kFrames)) anchor = true;
        if (anchor)
        {
            w = dev + lead;
            // (in this order, so that the instrument never takes what is in the ring from before as this cue's)
            f->validFrom.store (std::numeric_limits<int64_t>::max(), std::memory_order_release);
            f->serial.store (serial, std::memory_order_release);
            f->written.store (w, std::memory_order_release);
            f->validFrom.store (w, std::memory_order_release);
            anchor = false;
            engine->setRunning (true);
        }
        const double per = bpm / 60.0 / rate;
        for (int made = 0; w < dev + lead && made < 64 && play.load() && ! threadShouldExit(); ++made)
        {
            if (run != 0) { const double from = ppq + (double) (w - dev) * per; engine->ledBy (from, from + (double) kBlock * per, bpm); }
            else engine->ledOff();
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            engine->render (buf);
            load += 0.05 * ((juce::Time::getMillisecondCounterHiRes() - t0) / blockMs - load);
            f->load.store ((float) load, std::memory_order_relaxed);
            const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
            for (int i = 0; i < kBlock; ++i)
            {
                const size_t k = (size_t) ((w + i) & (juce::int64) (CueShared::kFrames - 1)) * 2;
                f->ring[k] = L[i]; f->ring[k + 1] = R[i];
            }
            w += kBlock;
            f->written.store (w, std::memory_order_release);
        }
        wait (1);
    }
    resting = true;
}
} // namespace sf0
