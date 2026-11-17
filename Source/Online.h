#pragma once
// SF-000 online: each snapshot goes to the snapshot library on the web as it is taken, in the
// background, with its clip and the definitions it names. Nothing waits on it and nothing is lost
// by it: with ONLINE off, or no network, a snapshot simply has not gone yet, and goes later.
// Where the library is and the two strings that let SF-000 in are settings (online.json beside
// the session); neither is in the code.
#include "Session.h"
#include <map>
#include <set>

namespace sf0
{
class Online : private juce::Thread
{
public:
    explicit Online (Session&);
    ~Online() override;

    // ---- settings (message thread)
    struct Setup
    {
        juce::String url;                   // https://... : where the library is
        juce::String clientId, clientSecret;    // the service token's two strings (Cloudflare Access)
        bool on = false;                    // the ONLINE key
        bool complete() const               { return url.isNotEmpty() && clientId.isNotEmpty() && clientSecret.isNotEmpty(); }
    };
    Setup setup() const;
    void setSetup (const Setup&);           // kept, and the sender begins again with it
    bool on() const                         { return setup().on; }
    void setOn (bool);
    void poke();                            // something may have changed: look now

    // ---- how it stands (any thread)
    enum class State { off, notSetUp, working, upToDate, waiting, refused };
    struct Status { State state = State::off; int waiting = 0, sent = 0; juce::String note; };
    Status status() const;
    juce::uint32 stamp() const              { return changes.load(); }     // moves when the status does
    juce::String line() const;              // the status in a few words, for the screen

    // ---- the work
    void startSending();                    // the app: a thread of its own from here on
    // One pass on the calling thread: ask the library what it holds (once per connection), then
    // send what it lacks. True: there is more to do soon (a clip still being written).
    bool pass();
    // Ask the library whether these settings are let in. Empty: yes. Else what went wrong.
    static juce::String check (const Setup&);
    // LOAD: bring what the web library's tray holds (its stacks, the macros on its knobs, a song)
    // with the snapshots this library lacks, their clips and the definitions they name. askLoad:
    // the sender's thread does it next. load: on the calling thread (a check). loadLine: how the
    // last one went, for the screen; loading: one is under way.
    void askLoad();
    juce::String load();
    // The knobs of the snapshot module, kept in the library as they are here. A knob made or changed on the instrument (a
    // macro knob taught, a stack built) goes up as a module, into its own place on the library's module if that place is
    // free; one emptied here comes off the module there (and stays in the library). Called after each pass while ONLINE is
    // on, once a knob has stood unchanged for a pass; and by LOAD first, whatever ONLINE is (sure: at once). True: it has
    // more to do soon. seated: the knobs that went onto the library's module for the first time.
    bool mirror (bool sure = false, juce::StringArray* seated = nullptr);
    // The rack, the same here and in the library, each way: a stack kept here goes there; one made, changed, unlit or
    // removed there is so here (the snapshots it names that this library lacks are brought first). After each pass
    // while ONLINE is on, and by LOAD (sure). True: it has more to do soon.
    bool rack (bool sure = false);
    // The archive: what else the library is sent so that it can be studied away from the instrument. The log of what was
    // done with the chips; the session without its chips (its modules, kept knobs, stacks, arrangements), when it has
    // changed; the sound files a snapshot needs to be played again; each snapshot's sound without loss. After each pass
    // while ONLINE is on, a little at a time, so that a snapshot just taken is never kept waiting by it. sure: all of it
    // now (the checks). True: there is more to go soon.
    bool archive (bool sure = false);
    int archiveLeft() const                 { return archiveToGo.load(); }      // how much of it has still to go (0: the library has it all)
    juce::String loadLine() const;
    bool loading() const                    { return loadBusy.load(); }
    int requests() const                    { return asked.load(); }        // sends made so far (the checks)
    static bool systemClientForTests;       // the checks: plain http through the system's client too, as https goes
    // The checks: one request to a library, made as SF-000 makes them. Its status (0: no answer).
    static int requestForTests (const Setup& s, const juce::String& method, const juce::String& path, const juce::MemoryBlock* body, const juce::String& type, juce::String* reply = nullptr)
    {
        const auto r = http (s, method, path, body, type);
        if (reply != nullptr) *reply = r.body;
        return r.status;
    }
    void showForTests (State st, int waiting, int sent, const juce::String& note = {}) { say (st, waiting, sent, note); }

    static juce::File setupFile();
    static juce::File ledgerFile();
    // Snapshots the rig came with (the factory ones, CLEAN SLATE) have small ids and stay at home.
    static constexpr juce::int64 kFirstTaken = 1000000;

private:
    void run() override;
    struct Reply { int status = 0; juce::String body; juce::MemoryBlock bytes; };       // status 0: no answer at all (bytes: the body as it came, where it was asked for)
    // A request under way can be called off from another thread: quitting must not wait on a
    // library that has stopped answering (it waited 8 s, then the thread was killed by force
    // with the request still open, which can take the app down as it leaves).
    struct CallOff
    {
        std::atomic<bool> off { false };
        juce::CriticalSection lock;
        juce::WebInputStream* stream = nullptr;         // the https request being made, under the lock
    };
    CallOff callOff;
    static Reply http (const Setup&, const juce::String& method, const juce::String& path,
                       const juce::MemoryBlock* body, const juce::String& contentType, int timeoutMs = 15000, CallOff* = nullptr, bool wantBytes = false);
    struct Entry { bool snap = false, clip = false, gone = false, refused = false, prof = false, stems = false, lossless = false; juce::String name; };       // (stems, lossless: they are there, or were turned down)
    int bringOne (const Setup&, const juce::String& uid, bool hasClip, bool& silent, Reply& why);
    void readSetup();
    void writeSetup() const;
    void readLedger();
    void writeLedger() const;
    void say (State, int waiting, int sent, const juce::String& note = {});
    juce::String body (const juce::String& uid, const std::set<juce::String>& forced, juce::StringArray& sending) const;

    Session& ses;
    mutable juce::CriticalSection lock;     // the settings and the status
    Setup now;
    Status stat;
    std::atomic<juce::uint32> changes { 0 };
    std::atomic<int> asked { 0 };
    std::atomic<bool> wantLoad { false }, loadBusy { false };
    juce::String loadSays;                  // (under the lock)
    // the sender's own (one thread at a time: the thread, or a check calling pass())
    std::map<juce::String, Entry> ledger;   // what the library has said it holds, by uid
    std::set<juce::String> defsSent;        // definitions it has
    bool asksFirst = true;                  // the library has not yet been asked what it holds
    int failures = 0;
    bool noStems = false;                           // the library said it keeps no stems (one from before it did): not offered again this run
    // the archive
    bool noLossless = false, noFiles = false, noLog = false, noSession = false;     // the library has no such address (one from before it had): not offered again this run
    std::map<juce::String, juce::int64> logSent;    // each file of the log, by its name: how many bytes of it the library has
    std::set<juce::String> filesSent;               // the sound files it has, by their SHA-256
    bool filesAsked = false;                        // it has been asked which of them it holds
    juce::String sessionHash;                       // the session as it was last sent (a hash of it)
    juce::uint32 sessionSeen = 0, sessionAt = 0;    // the session's count of changes when it was last looked at, and when that was
    bool sessionLooked = false;
    int archiveFails = 0;                           // calls on end that got no answer, and when the next may be made
    std::map<juce::String, int> archiveTries;       // a large thing the library's own trouble kept out (by its hash, or its snapshot's uid): how often
    std::set<juce::String> archiveSkip;             // ...and those left for another run: three times, or not what its name says, or too large to go
    juce::uint32 archiveNext = 0;
    std::atomic<int> archiveToGo { 0 };
    std::array<juce::String, 16> mirrorSeen;        // each knob as the last pass saw it (one that has just changed is given a pass to settle)
    juce::String sentTo;                    // the address the ledger was made against
    static constexpr int kRackEveryMs = 15000;     // the library is asked for its stacks no oftener, unless the rack here has changed
    juce::String rackSeen;                  // the rack here as the last look left it
    bool rackAsked = false;
    juce::uint32 rackAskedAt = 0;
};
} // namespace sf0
