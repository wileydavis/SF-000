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
    int requests() const                    { return asked.load(); }        // sends made so far (the checks)
    void showForTests (State st, int waiting, int sent, const juce::String& note = {}) { say (st, waiting, sent, note); }

    static juce::File setupFile();
    static juce::File ledgerFile();
    // Snapshots the rig came with (the factory ones, CLEAN SLATE) have small ids and stay at home.
    static constexpr juce::int64 kFirstTaken = 1000000;

private:
    void run() override;
    struct Reply { int status = 0; juce::String body; };       // status 0: no answer at all
    static Reply http (const Setup&, const juce::String& method, const juce::String& path,
                       const juce::MemoryBlock* body, const juce::String& contentType, int timeoutMs = 15000);
    struct Entry { bool snap = false, clip = false, gone = false, refused = false, prof = false; juce::String name; };
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
    // the sender's own (one thread at a time: the thread, or a check calling pass())
    std::map<juce::String, Entry> ledger;   // what the library has said it holds, by uid
    std::set<juce::String> defsSent;        // definitions it has
    bool asksFirst = true;                  // the library has not yet been asked what it holds
    int failures = 0;
    juce::String sentTo;                    // the address the ledger was made against
};
} // namespace sf0
