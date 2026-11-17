#include "Online.h"
#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

namespace sf0
{
using juce::String;
using juce::var;

juce::File Online::setupFile()  { return Session::homeFile ("online.json"); }       // (this machine's own, with its secret: never in the library folder, which may be on a drive that travels)
juce::File Online::ledgerFile() { return Session::stateFile().getSiblingFile ("online-sent.json"); }

Online::Online (Session& s) : juce::Thread ("SF-000 online"), ses (s)
{
    readSetup();
    readLedger();
    say (! now.on ? State::off : ! now.complete() ? State::notSetUp : State::working, 0, 0);
}

Online::~Online()
{
    // a send under way is called off: the thread is back in a moment, whatever the library is doing.
    // (The wait is longer than any request's own patience, so that even one that could not be called
    // off has given up by itself before JUCE would kill the thread.)
    callOff.off = true;
    {
        const juce::ScopedLock sl (callOff.lock);
        if (callOff.stream != nullptr) callOff.stream->cancel();
    }
    signalThreadShouldExit();
    notify();
    stopThread (35000);
}

bool Online::systemClientForTests = false;

void Online::startSending() { startThread (juce::Thread::Priority::low); }
void Online::poke()         { notify(); }

// ------------------------------------------------------------------------------------- settings
Online::Setup Online::setup() const
{
    const juce::ScopedLock g (lock);
    return now;
}

void Online::setSetup (const Setup& s)
{
    {
        const juce::ScopedLock g (lock);
        now = s;
        now.url = now.url.trim().trimCharactersAtEnd ("/");
        now.clientId = now.clientId.trim(); now.clientSecret = now.clientSecret.trim();
        writeSetup();
    }
    say (! s.on ? State::off : ! s.complete() ? State::notSetUp : State::working, 0, 0);
    poke();
}

void Online::setOn (bool on)
{
    auto s = setup();
    s.on = on;
    setSetup (s);
}

void Online::readSetup()
{
    const var v = juce::JSON::parse (setupFile());
    now.url = v.getProperty ("url", "").toString().trim().trimCharactersAtEnd ("/");
    now.clientId = v.getProperty ("id", "").toString().trim();
    now.clientSecret = v.getProperty ("secret", "").toString().trim();
    now.on = (bool) v.getProperty ("on", false);
}

void Online::writeSetup() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("url", now.url);
    o->setProperty ("id", now.clientId);
    o->setProperty ("secret", now.clientSecret);
    o->setProperty ("on", now.on);
    const auto f = setupFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (var (o)));
   #if ! JUCE_WINDOWS
    ::chmod (f.getFullPathName().toRawUTF8(), 0600);         // the secret is in it: this user only
   #endif
}

// --------------------------------------------------------------------------------------- status
void Online::say (State st, int waiting, int sent, const String& note)
{
    const juce::ScopedLock g (lock);
    if (stat.state == st && stat.waiting == waiting && stat.sent == sent && stat.note == note) return;
    stat.state = st; stat.waiting = waiting; stat.sent = sent; stat.note = note;
    ++changes;
}

Online::Status Online::status() const
{
    const juce::ScopedLock g (lock);
    return stat;
}

String Online::line() const
{
    const auto s = status();
    const auto n = [] (int k, const char* one, const char* many) { return String (k) + " " + (k == 1 ? one : many); };
    switch (s.state)
    {
        case State::off:      return s.waiting > 0 ? "ONLINE IS OFF. " + n (s.waiting, "SNAPSHOT WAITS", "SNAPSHOTS WAIT") + " TO GO." : String ("ONLINE IS OFF. SNAPSHOTS TAKEN NOW GO WHEN IT IS LIT.");
        case State::notSetUp: return "ONLINE NEEDS ITS ADDRESS AND THE TWO STRINGS: SET UP.";
        case State::working:  return s.waiting > 0 ? "SENDING: " + n (s.waiting, "SNAPSHOT", "SNAPSHOTS") + " TO GO." : String ("ASKING THE LIBRARY WHAT IT HOLDS.");
        case State::upToDate:
            // (its archive still going up says so: the lamp is the snapshots', and stays green)
            if (const int more = archiveLeft(); more > 0 && s.sent > 0)
                return "ONLINE. THE LIBRARY HOLDS " + (s.sent == 1 ? String ("THE ONE SNAPSHOT TAKEN") : "ALL " + String (s.sent) + " SNAPSHOTS") + ". ARCHIVE: " + String (more)
                       + (ses.engine.isRunning() ? " TO GO WHEN THE RIG IS STOPPED." : " STILL TO GO.");
            return s.sent == 0 ? String ("ONLINE. NOTHING TAKEN YET TO SEND.") : "ONLINE. THE LIBRARY HOLDS " + (s.sent == 1 ? String ("THE ONE SNAPSHOT TAKEN.") : "ALL " + String (s.sent) + " SNAPSHOTS TAKEN.");
        case State::waiting:  return "NO ANSWER FROM THE LIBRARY" + (s.note.isNotEmpty() ? " (" + s.note + ")" : String()) + ". " + n (s.waiting, "SNAPSHOT WAITS", "SNAPSHOTS WAIT") + "; IT TRIES AGAIN BY ITSELF.";
        case State::refused:  return "THE LIBRARY DID NOT LET SF-000 IN" + (s.note.isNotEmpty() ? " (" + s.note + ")" : String()) + ". CHECK THE TWO STRINGS IN SET UP.";
    }
    return {};
}

// --------------------------------------------------------------------------------------- ledger
void Online::readLedger()
{
    const var v = juce::JSON::parse (ledgerFile());
    sentTo = v.getProperty ("url", "").toString();
    ledger.clear(); defsSent.clear();
    logSent.clear(); filesSent.clear(); sessionHash = {}; filesAsked = false;
    if (sentTo != now.url) { sentTo = now.url; return; }                    // another library: it is asked afresh
    if (auto* o = v.getProperty ("sent", var()).getDynamicObject())
        for (auto& p : o->getProperties())
        {
            Entry e;
            e.snap = (bool) p.value.getProperty ("snap", false); e.clip = (bool) p.value.getProperty ("clip", false);
            e.gone = (bool) p.value.getProperty ("gone", false); e.refused = (bool) p.value.getProperty ("refused", false);
            e.name = p.value.getProperty ("name", "").toString();
            e.prof = (bool) p.value.getProperty ("prof", false);
            e.stems = (bool) p.value.getProperty ("stems", false);
            e.lossless = (bool) p.value.getProperty ("lossless", false);
            ledger[p.name.toString()] = e;
        }
    if (auto* a = v.getProperty ("defs", var()).getArray()) for (auto& d : *a) defsSent.insert (d.toString());
    // (the archive: how far the log has gone, the sound files there, the session as last sent)
    logSent.clear(); filesSent.clear(); sessionHash = {};
    const var ar = v.getProperty ("archive", var());
    if (auto* o = ar.getProperty ("log", var()).getDynamicObject()) for (auto& p : o->getProperties()) logSent[p.name.toString()] = (juce::int64) p.value;
    if (auto* a = ar.getProperty ("files", var()).getArray()) for (auto& f : *a) filesSent.insert (f.toString());
    sessionHash = ar.getProperty ("session", "").toString();
}

void Online::writeLedger() const
{
    auto* sent = new juce::DynamicObject();
    for (auto& kv : ledger)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("snap", kv.second.snap); o->setProperty ("clip", kv.second.clip);
        if (kv.second.gone) o->setProperty ("gone", true);
        if (kv.second.refused) o->setProperty ("refused", true);
        if (kv.second.prof) o->setProperty ("prof", true);
        if (kv.second.stems) o->setProperty ("stems", true);
        if (kv.second.lossless) o->setProperty ("lossless", true);
        o->setProperty ("name", kv.second.name);
        sent->setProperty (kv.first, var (o));
    }
    juce::Array<var> defs;
    for (auto& d : defsSent) defs.add (d);
    auto* root = new juce::DynamicObject();
    root->setProperty ("url", sentTo);
    root->setProperty ("sent", var (sent));
    root->setProperty ("defs", defs);
    {
        auto* ar = new juce::DynamicObject();
        auto* lg = new juce::DynamicObject();
        for (auto& kv : logSent) lg->setProperty (kv.first, kv.second);
        juce::Array<var> fs;
        for (auto& f : filesSent) fs.add (f);
        ar->setProperty ("log", var (lg)); ar->setProperty ("files", fs); ar->setProperty ("session", sessionHash);
        root->setProperty ("archive", var (ar));
    }
    const auto f = ledgerFile();
    f.getParentDirectory().createDirectory();
    const auto tmp = f.getSiblingFile ("online-sent.tmp");
    if (tmp.replaceWithText (juce::JSON::toString (var (root), true))) tmp.moveFileTo (f);
}

// ----------------------------------------------------------------------------------------- http
// Plain http (a library on this machine, as the checks use) is asked over a socket here. JUCE's
// own client on Linux puts a stray line break in front of a request's body and cuts its end off
// to match, which a library reading JSON or a clip does not forgive. https, the real thing, goes
// through the system's client below (on the Mac, NSURLSession).
static bool plainHttp (const juce::URL& url, const String& method, const String& target, const String& headers,
                       const juce::MemoryBlock* body, int timeoutMs, int& status, String& text, const std::atomic<bool>* off, juce::MemoryBlock* bytes = nullptr)
{
    // (waits are taken a tenth of a second at a time, so that being called off is noticed)
    auto called = [off] { return off != nullptr && off->load(); };
    juce::StreamingSocket sock;
    const int port = url.getPort() > 0 ? url.getPort() : 80;
    if (! sock.connect (url.getDomain(), port, juce::jmin (timeoutMs, 5000))) return false;
    juce::MemoryOutputStream req;
    req << method << " " << target << " HTTP/1.1\r\nHost: " << url.getDomain() << ":" << port << "\r\nConnection: close\r\n" << headers;
    if (body != nullptr) req << "Content-Length: " << (juce::int64) body->getSize() << "\r\n";
    req << "\r\n";
    if (body != nullptr) req << *body;
    const auto* data = static_cast<const char*> (req.getData());
    for (size_t sent = 0; sent < req.getDataSize();)
    {
        int ready = 0;
        for (int waited = 0; ready == 0 && waited < timeoutMs && ! called(); waited += 100) ready = sock.waitUntilReady (false, 100);
        if (ready != 1) return false;
        const int n = sock.write (data + sent, (int) juce::jmin ((size_t) 65536, req.getDataSize() - sent));
        if (n <= 0) return false;
        sent += (size_t) n;
    }
    juce::MemoryBlock got;
    char buf[16384];
    for (;;)
    {
        int ready = 0;
        for (int waited = 0; ready == 0 && waited < timeoutMs && ! called(); waited += 100) ready = sock.waitUntilReady (true, 100);
        if (called()) return false;
        if (ready != 1) break;
        const int n = sock.read (buf, (int) sizeof (buf), false);
        if (n <= 0) break;
        got.append (buf, (size_t) n);
    }
    // The head is text; the body is whatever it is (a clip is not text), so it is cut by bytes.
    const auto* raw = static_cast<const char*> (got.getData());
    const size_t size = got.getSize();
    size_t cut = 0;
    while (cut + 3 < size && ! (raw[cut] == '\r' && raw[cut + 1] == '\n' && raw[cut + 2] == '\r' && raw[cut + 3] == '\n')) ++cut;
    if (cut + 3 >= size) return false;
    const String head = String::fromUTF8 (raw, (int) cut);
    if (! head.startsWith ("HTTP/")) return false;
    status = head.fromFirstOccurrenceOf (" ", false, false).getIntValue();
    juce::MemoryBlock whole;
    const char* b = raw + cut + 4;
    const size_t n = size - cut - 4;
    if (head.toLowerCase().contains ("transfer-encoding: chunked"))
    {
        // sizes in hex, each followed by that many bytes
        for (size_t at = 0; at < n;)
        {
            size_t eol = at;
            while (eol + 1 < n && ! (b[eol] == '\r' && b[eol + 1] == '\n')) ++eol;
            if (eol + 1 >= n) break;
            const int len = String::fromUTF8 (b + at, (int) (eol - at)).upToFirstOccurrenceOf (";", false, false).trim().getHexValue32();
            if (len <= 0 || eol + 2 + (size_t) len > n) break;
            whole.append (b + eol + 2, (size_t) len);
            at = eol + 2 + (size_t) len + 2;
        }
    }
    else whole.append (b, n);
    text = String::fromUTF8 (static_cast<const char*> (whole.getData()), (int) whole.getSize());
    if (bytes != nullptr) *bytes = whole;
    return true;
}

Online::Reply Online::http (const Setup& s, const String& method, const String& path, const juce::MemoryBlock* body, const String& contentType, int timeoutMs, CallOff* co, bool wantBytes)
{
    if (co != nullptr && co->off.load()) return {};
    if (s.url.startsWithIgnoreCase ("http://") && ! systemClientForTests)
    {
        Reply r;
        String h = "CF-Access-Client-Id: " + s.clientId + "\r\nCF-Access-Client-Secret: " + s.clientSecret + "\r\n";
        if (contentType.isNotEmpty()) h << "Content-Type: " << contentType << "\r\n";
        const juce::URL base (s.url);
        const auto under = base.getSubPath();
        if (! plainHttp (base, method, (under.isNotEmpty() ? "/" + under : String()) + path, h, body, timeoutMs, r.status, r.body, co != nullptr ? &co->off : nullptr, wantBytes ? &r.bytes : nullptr)) r.status = 0;
        return r;
    }
    juce::URL url (s.url + path);
    if (body != nullptr) url = url.withPOSTData (*body);
    String headers = "CF-Access-Client-Id: " + s.clientId + "\r\nCF-Access-Client-Secret: " + s.clientSecret + "\r\n";
    if (contentType.isNotEmpty()) headers << "Content-Type: " << contentType << "\r\n";
    Reply r;
    // (the stream is made here and not by URL::createInputStream, which does just this, so that it
    // can be reached while it waits: cancel() ends a connect or a read under way)
    juce::WebInputStream in (url, body != nullptr);
    in.withExtraHeaders (headers).withConnectionTimeout (timeoutMs).withCustomRequestCommand (method).withNumRedirectsToFollow (0);
    if (co != nullptr)
    {
        const juce::ScopedLock sl (co->lock);
        if (co->off.load()) return {};
        co->stream = &in;
    }
    const bool ok = in.connect (nullptr);
    r.status = in.getStatusCode();
    if (ok && ! in.isError())
    {
        if (wantBytes) in.readIntoMemoryBlock (r.bytes);        // (a clip: not text)
        else r.body = in.readEntireStreamAsString();
    }
    if (co != nullptr)
    {
        const juce::ScopedLock sl (co->lock);
        co->stream = nullptr;
        if (co->off.load()) return {};
    }
    return r;
}

// Access turns away what it does not know with a 401 or 403, or by sending it to a sign-in page.
static bool turnedAway (int status) { return status == 401 || status == 403 || (status >= 300 && status < 400); }

String Online::check (const Setup& given)
{
    Setup s = given;
    s.url = s.url.trim().trimCharactersAtEnd ("/");
    if (! s.complete()) return "THE ADDRESS AND BOTH STRINGS ARE NEEDED.";
    if (! (s.url.startsWithIgnoreCase ("https://") || s.url.startsWithIgnoreCase ("http://"))) return "THE ADDRESS BEGINS https://";
    const auto r = http (s, "GET", "/api/health", nullptr, {}, 10000);
    if (r.status == 200 && (bool) juce::JSON::parse (r.body).getProperty ("ok", false)) return {};
    if (r.status == 0) return "NO ANSWER FROM THAT ADDRESS.";
    if (turnedAway (r.status)) return "IT ANSWERED, AND DID NOT LET SF-000 IN: CHECK THE TWO STRINGS, AND THAT THE TOKEN HAS A SERVICE AUTH POLICY.";
    return "IT ANSWERED " + String (r.status) + ": NOT THE SNAPSHOT LIBRARY?";
}

// -------------------------------------------------------------------------------- what to send
// A snapshot as the session file holds it, and the definitions it names that the library has not
// got (a lane's, effect's or return's sound engine with it). With the message lock.
String Online::body (const String& uid, const std::set<String>& forced, juce::StringArray& sending) const
{
    const Snapshot* s = nullptr;
    for (auto& x : ses.library) if (x.uid == uid) { s = &x; break; }
    if (s == nullptr) return {};
    const var snap = ses.written (*s);
    auto* defs = new juce::DynamicObject();
    auto want = [&] (const String& print)
    {
        if (print.length() != 16 || sending.contains (print) || (defsSent.count (print) != 0 && forced.count (print) == 0)) return var();
        const var d = ses.definition (print);
        if (d.isObject()) { defs->setProperty (print, d); sending.add (print); }
        return d;
    };
    const var made = snap.getProperty ("made", var());
    for (auto* group : { "lanes", "fx", "returns" })
        if (auto* o = made.getProperty (group, var()).getDynamicObject())
            for (auto& p : o->getProperties())
            {
                const var d = want (p.value.toString());
                if (d.isObject()) want (d.getProperty ("engine", "").toString());
                else if (const var known = ses.definition (p.value.toString()); known.isObject()) want (known.getProperty ("engine", "").toString());
            }
    want (made.getProperty ("chassis", "").toString());
    auto* root = new juce::DynamicObject();
    root->setProperty ("snapshot", snap);
    root->setProperty ("defs", var (defs));
    return juce::JSON::toString (var (root), true);
}

// ------------------------------------------------------------------------------------ one pass
bool Online::pass()
{
    const Setup s = setup();
    struct Item { String uid, name; juce::int64 id = 0; bool clipDue = false; juce::File clip; bool prof = false; juce::File stems; };
    std::vector<Item> items;
    {
        // the library is the message thread's: read with its lock (given up at once if this thread is asked to stop)
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        const auto nowMs = juce::Time::currentTimeMillis();
        for (auto& x : ses.library)
            if (x.id >= kFirstTaken && x.uid.isNotEmpty())
                items.push_back ({ x.uid, x.name, x.id, x.profile.has && nowMs - x.time.toMilliseconds() < 120000, ses.clipFile (x.id), x.profile.has, ses.stemsFile (x.id) });
    }
    auto left = [&]
    {
        int n = 0;
        for (auto& it : items)
        {
            const auto e = ledger.find (it.uid);
            if (e != ledger.end() && (e->second.gone || e->second.refused)) continue;
            if (e == ledger.end() || ! e->second.snap || e->second.name != it.name || (it.prof && ! e->second.prof) || (! e->second.clip && it.clip.existsAsFile())
                || (! e->second.stems && ! noStems && it.stems.existsAsFile())) ++n;
        }
        return n;
    };
    auto held = [&] { int n = 0; for (auto& it : items) { const auto e = ledger.find (it.uid); if (e != ledger.end() && e->second.snap) ++n; } return n; };

    if (! s.on)          { say (State::off, left(), held()); return false; }
    if (! s.complete())  { say (State::notSetUp, left(), held()); return false; }
    if (sentTo != s.url)
    {
        ledger.clear(); defsSent.clear(); sentTo = s.url; asksFirst = true;
        logSent.clear(); filesSent.clear(); sessionHash = {}; filesAsked = false;          // (another library: its archive is its own)
        noLossless = noFiles = noLog = noSession = false;
    }

    bool changed = false;
    auto fail = [&] (const Reply& r)
    {
        ++failures;
        asksFirst = true;                                   // on the way back, ask again what it holds
        if (changed) writeLedger();
        if (turnedAway (r.status)) say (State::refused, left(), held(), String (r.status));
        else say (State::waiting, left(), held(), r.status == 0 ? String() : String (r.status));
        return false;
    };

    // 1. once per connection: what does the library hold of ours? Its word replaces the ledger's.
    if (asksFirst)
    {
        say (State::working, left(), held());
        juce::Array<var> uids;
        for (auto& it : items) uids.add (it.uid);
        auto* q = new juce::DynamicObject();
        q->setProperty ("uids", uids);
        const auto text = juce::JSON::toString (var (q), true);
        const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
        ++asked;
        const auto r = http (s, "POST", "/api/have", &mb, "application/json", 8000, &callOff);
        const var have = juce::JSON::parse (r.body).getProperty ("have", var());
        if (r.status != 200 || ! have.isObject()) return fail (r);
        for (auto& it : items)
        {
            const var h = have.getProperty (juce::Identifier (it.uid), var());
            auto& e = ledger[it.uid];
            const Entry was = e;
            if (! h.isObject()) { e.snap = false; e.clip = false; e.gone = false; }             // not there: it goes (again)
            else if ((bool) h.getProperty ("gone", false)) { e.gone = true; }                 // removed there: not sent again
            else
            {
                e.snap = true; e.clip = (bool) h.getProperty ("clip", false); e.gone = false;
                e.stems = (bool) h.getProperty ("stems", false);
                e.lossless = (bool) h.getProperty ("lossless", false);
                if (was.name.isEmpty()) e.name = it.name;
                e.prof = e.prof || e.clip;                      // (a clip and a profile arrive together)
            }
            changed = changed || was.snap != e.snap || was.clip != e.clip || was.gone != e.gone || was.name != e.name || was.stems != e.stems || was.lossless != e.lossless;
        }
        // (a library that says nothing of lossless sound for a snapshot it holds is one from before it kept any: none is offered)
        {
            bool holds = false, says = false;
            for (auto& it : items)
                if (const var h = have.getProperty (juce::Identifier (it.uid), var()); h.isObject() && ! (bool) h.getProperty ("gone", false)) { holds = true; says = says || h.hasProperty ("lossless"); }
            if (holds) noLossless = ! says;
        }
        asksFirst = false;
    }

    // 2. send what it lacks, oldest first
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.id < b.id; });
    bool soon = false;
    for (auto& it : items)
    {
        if (threadShouldExit()) break;
        auto& e = ledger[it.uid];
        if (e.gone || e.refused) continue;
        if (! e.snap || e.name != it.name || (it.prof && ! e.prof))    // new, renamed, or its sound has arrived since it went
        {
            say (State::working, left(), held());
            std::set<String> forced;
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                juce::StringArray sending;
                String text;
                {
                    const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
                    if (! mm.lockWasGained()) { if (changed) writeLedger(); return false; }
                    text = body (it.uid, forced, sending);
                }
                if (text.isEmpty()) break;                                  // deleted here meanwhile
                const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
                ++asked;
                const auto r = http (s, "POST", "/api/snapshots", &mb, "application/json", 15000, &callOff);
                if (r.status == 400 || r.status == 413) { e.refused = true; changed = true; break; }       // this one alone will not go: the rest are not held up
                const var a = juce::JSON::parse (r.body);
                if (r.status != 200 || ! (bool) a.getProperty ("ok", false)) return fail (r);
                failures = 0; changed = true;
                for (auto& p : sending) defsSent.insert (p);
                if ((bool) a.getProperty ("gone", false)) { e.gone = true; break; }
                e.snap = true; e.name = it.name; e.prof = it.prof; e.clip = e.clip || (bool) a.getProperty ("clip", false);
                forced.clear();
                if (auto* miss = a.getProperty ("missingDefs", var()).getArray()) for (auto& m : *miss) forced.insert (m.toString());
                if (forced.empty()) break;                                  // (else once more, with the definitions it asked for)
            }
            if (e.gone || e.refused || ! e.snap) continue;
        }
        if (! e.clip)
        {
            if (! it.clip.existsAsFile()) { soon = soon || it.clipDue; continue; }          // still being written: back in a moment
            juce::MemoryBlock mb;
            if (! it.clip.loadFileAsData (mb) || mb.getSize() == 0) continue;
            ++asked;
            const auto r = http (s, "PUT", "/api/clips/" + juce::URL::addEscapeChars (it.uid, true), &mb, "audio/ogg", 30000, &callOff);
            if (r.status == 404) { e.snap = false; changed = true; soon = true; continue; }                 // it lost the snapshot: again next pass
            if (r.status == 400 || r.status == 413) { e.refused = true; changed = true; continue; }
            if (r.status != 200) return fail (r);
            failures = 0; changed = true;
            if (! (bool) juce::JSON::parse (r.body).getProperty ("gone", false)) e.clip = true; else e.gone = true;
        }
        // its stems (each lane, the returns and the drum bus on their own): what the library plays a chip from when keys are put out on it
        if (! e.stems && ! noStems && ! e.gone && e.snap && it.stems.existsAsFile())
        {
            juce::MemoryBlock mb;
            if (! it.stems.loadFileAsData (mb) || mb.getSize() == 0) continue;
            say (State::working, left(), held());
            ++asked;
            const auto r = http (s, "PUT", "/api/stems/" + juce::URL::addEscapeChars (it.uid, true), &mb, "application/octet-stream", 60000, &callOff);
            if (r.status == 404 || r.status == 501)
            {
                // (no such snapshot there: it goes again next pass. No such thing as stems there: a library from before it kept them.)
                if (juce::JSON::parse (r.body).getProperty ("error", "").toString().contains ("snapshot")) { e.snap = false; changed = true; soon = true; }
                else noStems = true;
                continue;
            }
            if (r.status == 400 || r.status == 413) { e.stems = true; changed = true; continue; }       // (turned down: not offered again)
            if (r.status != 200) return fail (r);
            failures = 0; changed = true;
            if (! (bool) juce::JSON::parse (r.body).getProperty ("gone", false)) e.stems = true; else e.gone = true;
        }
    }
    if (changed) writeLedger();
    const int n = left();
    say (n > 0 ? State::working : State::upToDate, n, held());
    return soon || n > 0;
}

// --------------------------------------------------------------------------------- the archive
// What else goes, for the library to be studied from: the log, the session without its chips, the sound files, each
// snapshot's sound without loss. A few requests a call, the small things first, and the large ones (megabytes each)
// only while the rig is stopped; a library from before it had an address for one of them says "not found", and that
// one is not offered again this run.
bool Online::archive (bool sure)
{
    const Setup s = setup();
    if (! s.complete() || ! s.on || sentTo != s.url) return false;
    if (! sure && (asksFirst || failures > 0 || loadBusy.load())) return false;       // (the snapshots first; nothing while the library is not answering)
    // (it got no answer the last time: not again at once, and longer each time, ten minutes at most)
    if (! sure && archiveFails > 0 && (juce::int32) (juce::Time::getMillisecondCounter() - archiveNext) < 0) return false;
    int budget = sure ? 1000000 : 4;
    // (a sound file or a snapshot's lossless sound: megabytes each. One a call, so a snapshot just taken never waits behind
    // many; and none while the rig is running, so that nothing large is read or sent under a set being played)
    int heavy = sure ? 1000000 : ses.engine.isRunning() ? 0 : 1;
    const bool held = heavy == 0;
    bool changed = false, more = false, down = false;
    auto notFound = [] (const Reply& r) { return r.status == 404 && juce::JSON::parse (r.body).getProperty ("error", "").toString() == "not found"; };
    auto ask = [&] (const String& method, const String& path, const juce::MemoryBlock* mb, const String& type, int ms)
    {
        --budget; ++asked;
        const auto r = http (s, method, path, mb, type, ms, &callOff);
        if (r.status == 0 || r.status >= 500 || turnedAway (r.status)) down = true;       // (no answer, or not let in: the next pass says so)
        return r;
    };

    // what the session holds of all this: read with the message thread's lock, and let go of at once
    struct Lossless { String uid; juce::File file; };
    std::vector<Lossless> lossless;
    std::vector<Session::Sample> samples;
    String libraryId, sessionText;
    juce::Array<var> chipIds;
    juce::File libDir, logDir;
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        libraryId = ses.libraryId;
        libDir = Session::libraryDir(); logDir = ses.logDir();
        samples = ses.samples;
        for (auto& x : ses.library)
            if (x.id >= kFirstTaken && x.uid.isNotEmpty())
                if (const auto e = ledger.find (x.uid); e != ledger.end() && e->second.snap && ! e->second.gone && ! e->second.lossless)
                    lossless.push_back ({ x.uid, ses.losslessFile (x.id) });
        // the session without its chips, if it has changed since it was last looked at (no oftener than once a minute)
        const auto nowMs = juce::Time::getMillisecondCounter();
        if (! noSession && (! sessionLooked || ses.changes != sessionSeen) && (sure || ! sessionLooked || nowMs - sessionAt > 60000))
        {
            sessionLooked = true; sessionSeen = ses.changes; sessionAt = nowMs;
            sessionText = juce::JSON::toString (ses.stateVar (false), true);       // (as text: what it is made of is the session's own, and is not touched)
            // (its knobs, stacks and arrangements name chips by this library's numbers: which chip each number is)
            for (auto& x : ses.library)
                if (x.uid.isNotEmpty()) { auto* c = new juce::DynamicObject(); c->setProperty ("id", x.id); c->setProperty ("uid", x.uid); chipIds.add (var (c)); }
        }
    }
    if (libraryId.length() != 16) return false;
    if (sessionText.isNotEmpty())
    {
        // What the rig is doing this minute is not the session's own things, and would make another session of every
        // recall and every turn of a knob: the rig as it stands (the chips hold states, and the log what was done),
        // the files it has loaded now, and where each stack knob stands.
        var st = juce::JSON::parse (sessionText);
        sessionText = {};
        if (auto* o = st.getDynamicObject())
        {
            for (auto* gone : { "wrote", "base", "baseId", "controls", "activeLane", "files", "space", "insertFiles" }) o->removeProperty (gone);
            auto still = [] (const var& knobs) { if (auto* ks = knobs.getArray()) for (auto& k : *ks) if (auto* ko = k.getDynamicObject()) ko->removeProperty ("at"); };
            still (o->getProperty ("knobs"));
            if (auto* ms = o->getProperty ("modules").getArray()) for (auto& m : *ms) still (m.getProperty ("knobs", var()));
            if (auto* ks = o->getProperty ("keptKnobs").getArray()) for (auto& k : *ks) if (auto* ko = k.getProperty ("knob", var()).getDynamicObject()) ko->removeProperty ("at");
            o->setProperty ("chips", chipIds);
            sessionText = juce::JSON::toString (st, true);
        }
    }

    // 1. the log: each file from where the library has it to, whole lines, a few hundred at a time
    if (! noLog)
    {
        juce::Array<juce::File> files;
        logDir.findChildFiles (files, juce::File::findFiles, false, "*.jsonl");
        files.sort();
        for (auto& f : files)
        {
            const auto name = f.getFileName();
            juce::int64 at = logSent.count (name) ? logSent[name] : 0;
            if (f.getSize() < at) at = 0;               // (shorter than what went: not the file that was sent from. All of it again; the library keeps a line once)
            while (! down && ! noLog && budget > 0 && f.getSize() > at)
            {
                juce::FileInputStream in (f);
                if (in.failedToOpen() || ! in.setPosition (at)) break;
                juce::MemoryBlock chunk;
                in.readIntoMemoryBlock (chunk, 192 * 1024);
                const char* d = (const char*) chunk.getData();
                juce::Array<var> lines;
                size_t used = 0, from = 0;
                for (size_t i = 0; i < chunk.getSize() && lines.size() < 500; ++i)
                    if (d[i] == '\n')
                    {
                        const var line = juce::JSON::parse (String::fromUTF8 (d + from, (int) (i - from)));
                        if (line.isObject()) lines.add (line);
                        from = used = i + 1;
                    }
                if (used == 0) break;                       // (no whole line yet: it is still being written)
                if (! lines.isEmpty())
                {
                    auto* q = new juce::DynamicObject();
                    q->setProperty ("library", libraryId); q->setProperty ("lines", lines);
                    const auto text = juce::JSON::toString (var (q), true);
                    const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
                    const auto r = ask ("POST", "/api/actions", &mb, "application/json", 20000);
                    if (notFound (r)) { noLog = true; break; }
                    if (r.status != 200 && r.status != 400 && r.status != 413) break;      // (turned down as it stands: gone past, so the rest is not held up)
                }
                at += (juce::int64) used; logSent[name] = at; changed = true;
            }
            if (f.getSize() > at && ! noLog) more = true;
        }
    }

    // 2. the session without its chips, if it is not what the library was last sent
    if (! down && ! noSession && sessionText.isNotEmpty() && budget > 0)
    {
        const auto hash = Session::hashOf (sessionText).substring (0, 32);
        if (hash != sessionHash)
        {
            const String text = "{\"library\":\"" + libraryId + "\",\"at\":" + String (juce::Time::currentTimeMillis())
                              + ",\"app\":\"" + Session::madeBy() + "\",\"session\":" + sessionText + "}";
            const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
            const auto r = ask ("POST", "/api/sessions", &mb, "application/json", 30000);
            if (notFound (r)) noSession = true;
            else if (r.status == 200 || r.status == 400 || r.status == 413) { sessionHash = hash; changed = true; }
            else sessionLooked = false;                     // (no answer: looked at again next time)
        }
    }

    // 3. the sound files a snapshot needs to be played again: the library is asked once which it holds, then sent the rest
    if (! down && ! noFiles && ! samples.empty())
    {
        if (! filesAsked && budget > 0)
        {
            juce::Array<var> hashes;
            for (auto& sm : samples) if (sm.hash.length() == 64) hashes.add (sm.hash);
            auto* q = new juce::DynamicObject();
            q->setProperty ("hashes", hashes);
            const auto text = juce::JSON::toString (var (q), true);
            const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
            const auto r = ask ("POST", "/api/files/have", &mb, "application/json", 15000);
            if (notFound (r)) noFiles = true;
            else if (r.status == 200)
            {
                filesAsked = true;
                const auto before = filesSent;
                filesSent.clear();
                const var have = juce::JSON::parse (r.body).getProperty ("have", var());
                if (auto* o = have.getDynamicObject()) for (auto& p : o->getProperties()) filesSent.insert (p.name.toString());
                changed = changed || before != filesSent;
            }
        }
        if (filesAsked && ! noFiles)
            for (auto& sm : samples)
            {
                if (sm.hash.length() != 64 || filesSent.count (sm.hash) || archiveSkip.count (sm.hash)) continue;
                if (down || held) break;
                if (budget <= 0 || heavy <= 0) { more = true; break; }
                const auto f = libDir.getChildFile (sm.file);
                juce::MemoryBlock mb;
                // (not here, too large to go, or no longer the file its name says: left for another run, and nothing said of it to the library)
                if (! f.existsAsFile() || f.getSize() > 96 * 1024 * 1024 || ! f.loadFileAsData (mb) || mb.getSize() == 0 || Session::hashOf (mb.getData(), mb.getSize()) != sm.hash) { archiveSkip.insert (sm.hash); continue; }
                --heavy;
                const auto r = ask ("PUT", "/api/files/" + sm.hash + "?name=" + juce::URL::addEscapeChars (f.getFileName(), true), &mb, "application/octet-stream", 300000);
                if (notFound (r)) { noFiles = true; break; }
                if (r.status == 200 || r.status == 400 || r.status == 413) { filesSent.insert (sm.hash); changed = true; }     // (there now, or turned down as it is: not offered again)
                else if (r.status >= 500 && ++archiveTries[sm.hash] >= 3) archiveSkip.insert (sm.hash);                       // (the library's own trouble with this one, three times: the rest are not held up by it)
            }
    }

    // 4. each snapshot's sound without loss: the large things, last
    if (! down && ! noLossless)
        for (auto& it : lossless)
        {
            if (! it.file.existsAsFile() || archiveSkip.count (it.uid)) continue;       // (taken before they were kept, or still being written; or left for another run)
            if (down || held) break;
            if (budget <= 0 || heavy <= 0) { more = true; break; }
            if (it.file.getSize() > 64 * 1024 * 1024) { archiveSkip.insert (it.uid); continue; }     // (more than the library takes)
            juce::MemoryBlock mb;
            if (! it.file.loadFileAsData (mb) || mb.getSize() == 0) continue;
            --heavy;
            const auto r = ask ("PUT", "/api/lossless/" + juce::URL::addEscapeChars (it.uid, true), &mb, "application/octet-stream", 600000);
            auto& e = ledger[it.uid];
            if (r.status == 404 || r.status == 501)
            {
                // (no such snapshot there: the pass sends it again. No such thing there: a library from before it kept them.)
                if (juce::JSON::parse (r.body).getProperty ("error", "").toString().contains ("snapshot")) { e.snap = false; changed = true; }
                else { noLossless = true; break; }
                continue;
            }
            if (r.status == 400 || r.status == 413) { e.lossless = true; changed = true; continue; }       // (turned down: not offered again)
            if (r.status >= 500 && ++archiveTries[it.uid] >= 3) archiveSkip.insert (it.uid);
            if (r.status != 200) continue;
            changed = true;
            if (! (bool) juce::JSON::parse (r.body).getProperty ("gone", false)) e.lossless = true; else e.gone = true;
        }

    if (changed) writeLedger();
    if (down) { ++archiveFails; archiveNext = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmin (600000, 30000 * (1 << juce::jmin (5, archiveFails - 1))); }
    else archiveFails = 0;
    // how much of it has still to go, for the screen
    int left = 0;
    if (! noLossless) for (auto& it : lossless) { const auto e = ledger.find (it.uid); if (it.file.existsAsFile() && ! archiveSkip.count (it.uid) && e != ledger.end() && ! e->second.lossless && ! e->second.gone) ++left; }
    if (! noFiles && filesAsked) for (auto& sm : samples) if (sm.hash.length() == 64 && ! filesSent.count (sm.hash) && ! archiveSkip.count (sm.hash)) ++left;
    if (! noLog)
    {
        juce::Array<juce::File> files;
        logDir.findChildFiles (files, juce::File::findFiles, false, "*.jsonl");
        for (auto& f : files) if (f.getSize() != (logSent.count (f.getFileName()) ? logSent[f.getFileName()] : 0)) ++left;
    }
    if (archiveToGo.exchange (left) != left) ++changes;
    return more && ! down;
}

// ----------------------------------------------------------------------- the knobs, kept there
bool Online::mirror (bool sure, juce::StringArray* seated)
{
    const Setup s = setup();
    if (! s.complete() || (! sure && (! s.on || loadBusy.load()))) return false;
    struct Now { bool holds = false, whole = false, reseat = false, due = false; String lib, mark, print; var body; };
    std::array<Now, 16> now;
    struct Aside { String text; var body; bool whole = false; };
    std::vector<Aside> aside;
    juce::Array<var> unseated;
    auto wholeIn = [this] (const var& body, bool whole)
    {
        // (and the library holds every snapshot of it: a module is never sent with a snapshot the library has not got)
        if (auto* items = body.getProperty ("items", var()).getArray())
            for (auto& it : *items) { const auto e = ledger.find (it.getProperty ("u", "").toString()); if (e == ledger.end() || ! e->second.snap) whole = false; }
        return whole;
    };
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        for (int k = 0; k < 16; ++k)
        {
            auto& n = now[(size_t) k];
            const auto& more = ses.knobs[(size_t) k].more;
            n.holds = ses.knobHolds (k);
            n.lib = more.getWithDefault ("lib", "").toString();
            n.mark = more.getWithDefault ("libmark", "").toString();
            n.reseat = (bool) more.getWithDefault ("reseat", false) && n.lib.isNotEmpty();
            n.print = ses.knobPrint (k);
            n.body = ses.knobBody (k, n.whole);
            n.whole = wholeIn (n.body, n.whole);
        }
        for (int i = 0; i < ses.knobsAside.size(); ++i)
        {
            Aside a;
            a.text = juce::JSON::toString (ses.knobsAside[i], true);
            a.body = ses.asideBody (i, a.whole);
            a.whole = wholeIn (a.body, a.whole);
            aside.push_back (a);
        }
        unseated = ses.knobsUnseated;
    }
    bool any = unseated.size() > 0, soon = false;
    for (auto& a : aside) any = any || a.whole;
    for (int k = 0; k < 16; ++k)
    {
        auto& n = now[(size_t) k];
        any = any || n.reseat;
        if (! n.holds || n.print == n.mark) { mirrorSeen[(size_t) k] = n.print; continue; }
        if (! n.whole) continue;                                                // (not all its snapshots are in the library: when they are, a pass later. The rig's own never go, and a knob of those stays here.)
        if (sure || mirrorSeen[(size_t) k] == n.print) { n.due = true; any = true; }
        else { mirrorSeen[(size_t) k] = n.print; soon = true; }                 // (just changed: once it has stood a pass)
    }
    if (! any) return soon;

    auto put = [&] (const String& method, const String& path, const var& body)
    {
        const auto text = juce::JSON::toString (body, true);
        const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
        ++asked;
        return http (s, method, path, &mb, "application/json", 20000, &callOff).status;
    };
    auto slotOnly = [] (const var& slot) { auto* o = new juce::DynamicObject(); o->setProperty ("slot", slot); return var (o); };
    auto newId = []
    {
        String id ("sf");
        juce::Random r;
        for (int i = 0; i < 14; ++i) id << String::charToString ((juce::juce_wchar) "abcdefghijklmnopqrstuvwxyz0123456789"[r.nextInt (36)]);
        return id;
    };
    // the knobs brought over to another place by the two kinds: the library's module is told where each is now
    for (int k = 0; k < 16 && ! threadShouldExit(); ++k)
        if (auto& n = now[(size_t) k]; n.reseat)
        {
            const int st = put ("PATCH", "/api/stacks/" + n.lib, slotOnly (k + 1));
            if (st != 200 && st != 404) return true;                            // (no answer: later)
            const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
            if (! mm.lockWasGained()) return false;
            auto& more = ses.knobs[(size_t) k].more;
            if (more.getWithDefault ("lib", "").toString() == n.lib) { more.remove ("reseat"); if (st == 404) { more.remove ("lib"); more.remove ("libmark"); n.lib = {}; n.mark = {}; } }
            ses.touch();
        }
    // what is on the library's module now
    ++asked;
    const auto t = http (s, "GET", "/api/load", nullptr, {}, 20000, &callOff);
    const var tray = juce::JSON::parse (t.body);
    if (t.status != 200 || ! tray.isObject()) return true;
    std::array<String, 16> pocket;
    if (auto* kn = tray.getProperty ("knobs", var()).getArray())
        for (auto& v : *kn) { const int k = (int) v.getProperty ("slot", 0) - 1; if (k >= 0 && k < 16) pocket[(size_t) k] = v.getProperty ("id", "").toString(); }
    // a knob emptied here: its module comes off the library's module (if it is still where it was)
    for (auto& u : unseated)
    {
        if (threadShouldExit()) return false;
        const String id = u.getProperty ("id", "").toString();
        const int k = (int) u.getProperty ("slot", 0) - 1;
        if (k >= 0 && k < 16 && pocket[(size_t) k] == id && id.isNotEmpty())
        {
            const int st = put ("PATCH", "/api/stacks/" + id, slotOnly (var()));
            if (st != 200 && st != 404) return true;
            pocket[(size_t) k] = {};
        }
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        for (int i = ses.knobsUnseated.size(); --i >= 0;) if (ses.knobsUnseated[i].getProperty ("id", "").toString() == id) ses.knobsUnseated.remove (i);
        ses.touch();
    }
    // the knobs that found no place when the two kinds came: into the library, off its module
    for (auto& a : aside)
    {
        if (threadShouldExit()) return false;
        if (! a.whole) continue;
        a.body.getDynamicObject()->setProperty ("id", newId());
        const int st = put ("POST", "/api/stacks", a.body);
        if (st != 200 && st != 201) return true;
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        for (int i = ses.knobsAside.size(); --i >= 0;) if (juce::JSON::toString (ses.knobsAside[i], true) == a.text) { ses.knobsAside.remove (i); break; }
        ses.touch();
    }
    // each knob made or changed here
    for (int k = 0; k < 16 && ! threadShouldExit(); ++k)
    {
        auto& n = now[(size_t) k];
        if (! n.due) continue;
        String id = n.lib;
        bool isNew = false;
        int st = id.isNotEmpty() ? put ("PATCH", "/api/stacks/" + id, n.body) : 404;
        if (st == 404)
        {
            // (the library has never seen it, or no longer has it: a module of its own, on the library's module if its place there is free)
            id = newId(); isNew = true;
            n.body.getDynamicObject()->setProperty ("id", id);
            if (pocket[(size_t) k].isEmpty() || pocket[(size_t) k] == n.lib) n.body.getDynamicObject()->setProperty ("slot", k + 1);
            st = put ("POST", "/api/stacks", n.body);
        }
        if (st != 200 && st != 201) return true;
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        if (ses.knobPrint (k) != n.print) { soon = true; continue; }            // (changed again meanwhile: next time)
        auto& more = ses.knobs[(size_t) k].more;
        more.set ("lib", id); more.set ("libmark", n.print);
        if (isNew && seated != nullptr && n.body.hasProperty ("slot")) seated->add (String (k + 1));
        ses.touch();
    }
    return soon;
}

// ----------------------------------------------------------------- one snapshot, brought here
// A snapshot the web library has and this one lacks: whole, with the definitions it names and its four bars.
// 1: it is here. 0: the library could not give it (left out). -1: no answer (why: the reply). -2: asked to stop.
int Online::bringOne (const Setup& s, const String& uid, bool hasClip, bool& silent, Reply& why)
{
    ++asked;
    const auto one = http (s, "GET", "/api/snapshots/" + juce::URL::addEscapeChars (uid, true), nullptr, {}, 20000, &callOff);
    const var got = juce::JSON::parse (one.body);
    const var snap = got.getProperty ("snapshot", var());
    if (one.status != 200 || ! snap.isObject()) { if (one.status == 0 || turnedAway (one.status)) { why = one; return -1; } return 0; }
    // the definitions it names that this library has not got
    juce::StringArray want;
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return -2;
        if (auto* pr = got.getProperty ("prints", var()).getArray())
            for (auto& p : *pr) if (! ses.definition (p.toString()).isObject()) want.addIfNotAlreadyThere (p.toString());
    }
    auto* defs = new juce::DynamicObject();
    const var defsVar (defs);
    for (int at = 0; at < want.size(); at += 40)
    {
        juce::StringArray part;
        for (int k = at; k < juce::jmin (want.size(), at + 40); ++k) part.add (want[k]);
        ++asked;
        const auto d = http (s, "GET", "/api/defs?prints=" + part.joinIntoString (","), nullptr, {}, 20000, &callOff);
        if (d.status != 200) { why = d; return -1; }
        const var sent = juce::JSON::parse (d.body).getProperty ("defs", var());      // (held while it is read: what it points at is its own)
        if (auto* o = sent.getDynamicObject())
            for (auto& p : o->getProperties()) defs->setProperty (p.name, p.value);
    }
    // its four bars
    juce::MemoryBlock clip;
    if (hasClip)
    {
        ++asked;
        auto c = http (s, "GET", "/api/clips/" + juce::URL::addEscapeChars (uid, true), nullptr, {}, 30000, &callOff, true);
        if (c.status == 200) clip = std::move (c.bytes);
        else if (c.status == 0) { why = c; return -1; }
    }
    juce::int64 id = 0; juce::File clipAt; String nameHere; bool prof = false;
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return -2;
        const Session::Quiet quiet (ses);                   // (what ONLINE does is not the player's doing: not in the log)
        id = ses.takeFromLibrary (snap, got.getProperty ("name", "").toString(), defsVar);
        if (id != 0)
        {
            clipAt = ses.clipFile (id);
            if (const auto* sn = ses.findSnap (id)) { nameHere = sn->name; prof = sn->profile.has; }
            if (ses.filesMissing (id) > 0) silent = true;
        }
    }
    if (id == 0) return 0;
    if (clip.getSize() > 0) { clipAt.getParentDirectory().createDirectory(); clipAt.replaceWithData (clip.getData(), clip.getSize()); }
    // (the library has it: it is not to be sent back)
    auto& e = ledger[uid];
    e.snap = true; e.clip = clip.getSize() > 0 || ! hasClip; e.prof = prof; e.name = nameHere; e.gone = false;
    return 1;
}

// ------------------------------------------------------------------- the rack, the same each way
// The stacks of the rack here and the web library's stacks for the instrument, made the same. Session says what is to be
// done (rackPlan) and does its own part of each step once the library has said yes (rackDone); this asks the library.
// A stack the library has that names a snapshot this library lacks: the snapshot is brought first, and until it is
// here the stack is left exactly as it is on both sides. Run after each pass while ONLINE is lit (the library is
// asked at once when the rack here has changed, and otherwise every so often), and by LOAD (sure). True: more to do soon.
bool Online::rack (bool sure)
{
    const Setup s = setup();
    if (! s.complete() || (! sure && (! s.on || loadBusy.load()))) return false;
    // (the pass before this one got no answer, or was turned away: the library is not asked again on top of that)
    if (! sure) if (const auto was = status().state; was == State::waiting || was == State::refused) return false;
    auto lineNow = [this] (String& into)
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        into = {};
        for (auto& st : ses.stacks) into << st.id << "=" << ses.stackPrint (st) << (st.mark.isEmpty() ? "?" : "") << ";";
        into << "|" << ses.stacksGone.size();
        return true;
    };
    String sig;
    if (! lineNow (sig)) return false;
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (! sure && rackAsked && sig == rackSeen && nowMs - rackAskedAt < (juce::uint32) kRackEveryMs) return false;
    rackAsked = true; rackAskedAt = nowMs;
    ++asked;
    const auto r = http (s, "GET", "/api/stacks", nullptr, {}, 20000, &callOff);
    const var all = juce::JSON::parse (r.body).getProperty ("stacks", var());
    if (r.status != 200 || ! all.isArray()) return false;                      // (no answer: at the next look)

    std::set<String> none;
    juce::StringArray lacking;
    std::vector<Session::RackStep> plan;
    auto planNow = [&]
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        lacking.clear();
        plan = ses.rackPlan (all, none, lacking);
        return true;
    };
    if (! planNow()) return false;
    if (lacking.size() > 0)
    {
        // snapshots its stacks name that are not here: which of them the library still has, and those brought
        juce::Array<var> uids;
        for (auto& u : lacking) uids.add (u);
        auto* q = new juce::DynamicObject();
        q->setProperty ("uids", uids);
        const auto text = juce::JSON::toString (var (q), true);
        const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
        ++asked;
        const auto h = http (s, "POST", "/api/have", &mb, "application/json", 20000, &callOff);
        const var have = juce::JSON::parse (h.body).getProperty ("have", var());
        if (h.status != 200 || ! have.isObject()) return false;
        const auto want = lacking;
        bool broughtAny = false;
        for (auto& uid : want)
        {
            if (threadShouldExit() || callOff.off.load()) return false;
            const var one = have.getProperty (juce::Identifier (uid), var());
            if (! one.isObject() || (bool) one.getProperty ("gone", false)) { none.insert (uid); continue; }    // (no library has it any more: the stack is what is left of it)
            Reply why; bool quiet = false;
            const int got = bringOne (s, uid, (bool) one.getProperty ("clip", false), quiet, why);
            if (got < 0) { if (broughtAny) writeLedger(); return false; }
            broughtAny = broughtAny || got == 1;
        }
        if (broughtAny) writeLedger();
        if (! planNow()) return false;
    }

    auto put = [&] (const String& method, const String& path, const var& body)
    {
        const auto text = juce::JSON::toString (body, true);
        const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
        ++asked;
        return http (s, method, path, &mb, "application/json", 20000, &callOff).status;
    };
    auto only = [] (const char* key, const var& value) { auto* o = new juce::DynamicObject(); o->setProperty (key, value); return var (o); };
    // (a stack is never sent with a snapshot the library has not got: when it has, at a later look)
    auto libraryHasAll = [this] (const var& body)
    {
        if (auto* items = body.getProperty ("items", var()).getArray())
            for (auto& it : *items) { const auto e = ledger.find (it.getProperty ("u", "").toString()); if (e == ledger.end() || ! e->second.snap) return false; }
        return true;
    };
    bool again = false;
    for (auto& step : plan)
    {
        if (threadShouldExit() || callOff.off.load()) return false;
        const String at = "/api/stacks/" + step.id;
        int st = 200;
        switch (step.net)
        {
            case Session::RackStep::none: break;
            case Session::RackStep::post:
            {
                if (! libraryHasAll (step.body)) continue;
                auto* o = step.body.getDynamicObject();
                o->setProperty ("id", step.id); o->setProperty ("kind", "stack"); o->setProperty ("extra", only ("from", "sf000"));
                st = put ("POST", "/api/stacks", step.body);
                if (st == 201) st = 200;
                break;
            }
            case Session::RackStep::patch:
                if (! libraryHasAll (step.body)) continue;
                st = put ("PATCH", at, step.body);
                break;
            case Session::RackStep::restore:
                if (! libraryHasAll (step.body)) continue;
                st = put ("PATCH", at, only ("restore", true));
                if (st == 200) st = put ("PATCH", at, step.body);
                break;
            case Session::RackStep::unlight:
                st = put ("PATCH", at, only ("load", false));
                if (st == 404) st = 200;                                        // (it is gone there: so it is out)
                break;
            case Session::RackStep::removeThere:
                ++asked;
                st = http (s, "DELETE", at, nullptr, {}, 20000, &callOff).status;
                if (st == 404) st = 200;
                break;
        }
        if (st != 200) { if (st == 0 || turnedAway (st)) return false; continue; }     // (no answer: all of it later. Said no to this one: the rest go on)
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        const Session::Quiet quiet (ses);
        if (! ses.rackDone (step)) again = true;
    }
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        const Session::Quiet quiet (ses);
        ses.rackOrder (all);
    }
    if (! again) lineNow (rackSeen);
    return again;
}

// ------------------------------------------------------------------------------------ LOAD
void Online::askLoad() { wantLoad = true; notify(); }
String Online::loadLine() const { const juce::ScopedLock sl (lock); return loadSays; }

String Online::load()
{
    const Setup s = setup();
    auto says = [this] (const String& line) { { const juce::ScopedLock sl (lock); loadSays = line; } ++changes; return line; };
    if (! s.complete()) return says ("LOAD: SET UP THE LIBRARY'S ADDRESS AND KEY FIRST");
    loadBusy = true;
    struct Done { std::atomic<bool>& b; ~Done() { b = false; } } done { loadBusy };
    // what is here goes up first (a knob made or changed here, one brought over to another place): then the library's module is asked for
    juce::StringArray seatedNow;
    says ("LOAD: SENDING THE KNOBS AS THEY ARE HERE");
    mirror (true, &seatedNow);
    if (callOff.off.load()) return says ("LOAD: STOPPED");
    says ("LOAD: ASKING THE LIBRARY WHAT ITS MODULE HOLDS");
    auto failed = [&] (const Reply& r) { return says (String ("LOAD: ") + (r.status == 0 ? "NO ANSWER FROM THE LIBRARY" : turnedAway (r.status) ? "THE LIBRARY TURNED SF-000 AWAY" : "THE LIBRARY SAID " + String (r.status)) + ". NOTHING WAS CHANGED"); };
    ++asked;
    const auto t = http (s, "GET", "/api/load", nullptr, {}, 20000, &callOff);
    const var tray = juce::JSON::parse (t.body);
    if (t.status != 200 || ! tray.isObject()) return failed (t);
    // which of its chips this library lacks
    std::set<String> here;
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return says ("LOAD: STOPPED");
        for (auto& x : ses.library) if (x.uid.isNotEmpty()) here.insert (x.uid);
    }
    juce::StringArray lacking;
    std::map<String, bool> hasClip;
    if (auto* chips = tray.getProperty ("chips", var()).getDynamicObject())
        for (auto& c : chips->getProperties())
        {
            const String uid = c.name.toString();
            hasClip[uid] = (bool) c.value.getProperty ("clip", false);
            if (here.count (uid) == 0) lacking.add (uid);
        }
    int brought = 0, lost = 0, silent = 0;
    for (int i = 0; i < lacking.size(); ++i)
    {
        const String uid = lacking[i];
        if (callOff.off.load()) return says ("LOAD: STOPPED");
        says ("LOAD: BRINGING " + String (i + 1) + " OF " + String (lacking.size()));
        Reply why;
        bool quiet = false;
        const int got = bringOne (s, uid, hasClip[uid], quiet, why);
        if (got == -2) return says ("LOAD: STOPPED");
        if (got == -1) return failed (why);
        if (quiet) ++silent;
        if (got == 1) ++brought; else ++lost;
    }
    // the rack: made the same as the library's, each way (what was kept here goes up; what the library has for the instrument comes)
    says ("LOAD: THE RACK");
    rack (true);
    if (callOff.off.load()) return says ("LOAD: STOPPED");
    // ---- the knobs. The chassis says what each of the sixteen holds: that is what a show carries. What a knob holds that the
    // library has not got is sent up first, as a module, so that nothing is ever lost by a LOAD; then the chassis's module
    // takes the knob. A knob whose module has been taken off the chassis is emptied; one the library has never seen goes
    // up into its own pocket, so the chassis shows what the instrument has.
    std::array<Session::KnobPlan, 16> plan;
    juce::StringArray sentUp, keptBack, stuck, movedSay;
    {
        struct Now { bool holds = false, whole = false, same = false, never = false; String lib, mark, print, name; var body; };
        std::array<Now, 16> now;
        std::array<var, 16> pocket;
        if (auto* kn = tray.getProperty ("knobs", var()).getArray())
            for (auto& v : *kn) { const int k = (int) v.getProperty ("slot", 0) - 1; if (k >= 0 && k < 16) pocket[(size_t) k] = v; }
        {
            const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
            if (! mm.lockWasGained()) return says ("LOAD: STOPPED");
            for (int k = 0; k < 16; ++k)
            {
                auto& n = now[(size_t) k];
                n.holds = ses.knobHolds (k);
                n.lib = ses.knobs[(size_t) k].more.getWithDefault ("lib", "").toString();
                n.mark = ses.knobs[(size_t) k].more.getWithDefault ("libmark", "").toString();
                n.print = ses.knobPrint (k);
                n.body = ses.knobBody (k, n.whole);
                n.same = pocket[(size_t) k].isObject() && ses.knobIs (k, pocket[(size_t) k]);
                n.never = ses.knobStaysHere (k);
                n.name = ses.knobs[(size_t) k].name.isNotEmpty() ? ses.knobs[(size_t) k].name : "UNNAMED";
                // (whole: and the library holds every snapshot of it. A knob that is not whole is never sent, so never replaced.)
                if (auto* items = n.body.getProperty ("items", var()).getArray())
                    for (auto& it : *items) { const auto e = ledger.find (it.getProperty ("u", "").toString()); if (e == ledger.end() || ! e->second.snap) n.whole = false; }
            }
        }
        auto put = [&] (const String& method, const String& path, const var& body)
        {
            const auto text = juce::JSON::toString (body, true);
            const juce::MemoryBlock mb (text.toRawUTF8(), text.getNumBytesAsUTF8());
            ++asked;
            const auto r = http (s, method, path, &mb, "application/json", 20000, &callOff);
            return r.status;
        };
        auto newId = []
        {
            String id ("sf");
            juce::Random r;
            for (int i = 0; i < 14; ++i) id << String::charToString ((juce::juce_wchar) "abcdefghijklmnopqrstuvwxyz0123456789"[r.nextInt (36)]);
            return id;
        };
        // LOAD makes room. A stack knob here that the library cannot be given (it holds the rig's own snapshots, which never
        // go there; or ones ONLINE has not sent yet), on a place the library's module has another knob for: it goes to the
        // first stack knob that is free here and on the module, as it is, and the module's knob takes its place. Nothing
        // is lost, and a knob that can never be sent no longer holds its place against the library for good.
        {
            std::vector<std::pair<int, int>> moves;
            for (int k = Session::kMacroKnobs; k < 16; ++k)
            {
                auto& n = now[(size_t) k];
                const var& in = pocket[(size_t) k];
                if (! Session::isStack (k) || ! in.isObject() || ! Session::moduleFits (k, in) || ! n.holds || n.same || n.whole) continue;
                if (n.lib == in.getProperty ("id", "").toString()) continue;            // (the module's own knob, changed here: it stays until it can go up)
                if (n.lib.isNotEmpty() && n.print == n.mark) continue;                  // (the library has it as it is: nothing to keep)
                int f = -1;
                for (int j = Session::kMacroKnobs; j < 16 && f < 0; ++j) if (Session::isStack (j) && ! now[(size_t) j].holds && ! pocket[(size_t) j].isObject()) f = j;
                if (f < 0) { if (n.never) stuck.add (String (k + 1)); continue; }
                moves.push_back ({ k, f });
                movedSay.add ("WHAT KNOB " + String (k + 1) + " HELD (" + n.name + ") IS ON KNOB " + String (f + 1) + " NOW");
                now[(size_t) f] = n; n = Now();
            }
            if (! moves.empty())
            {
                const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
                if (! mm.lockWasGained()) return says ("LOAD: STOPPED");
                const Session::Quiet quiet (ses);
                for (auto& m : moves) ses.knobMove (m.first, m.second);
            }
        }
        for (int k = 0; k < 16 && ! threadShouldExit(); ++k)
        {
            auto& n = now[(size_t) k]; auto& p = plan[(size_t) k];
            const var& in = pocket[(size_t) k];
            if (in.isObject() && ! Session::moduleFits (k, in)) continue;      // (the other kind in that place: the knob is left as it is, and takeTray says so)
            const String pid = in.isObject() ? in.getProperty ("id", "").toString() : String();
            const bool edited = n.holds && n.print != n.mark;
            // the knob as it is, into the library as a module of its own (in a pocket of the chassis, or off it): its id, or none
            auto asNew = [&] (int slot) -> String
            {
                if (! n.whole) return {};
                const String id = newId();
                n.body.getDynamicObject()->setProperty ("id", id);
                if (slot > 0) n.body.getDynamicObject()->setProperty ("slot", slot);
                const int st = put ("POST", "/api/stacks", n.body);
                return st == 200 || st == 201 ? id : String();
            };
            if (in.isObject())
            {
                if (! n.holds || n.same) { p.what = Session::KnobPlan::take; continue; }                // nothing to lose
                if (n.lib == pid)
                {
                    // the same module: the library's, unless it was changed here since (then what is here goes up, and stays)
                    if (! edited) p.what = Session::KnobPlan::take;
                    else if (n.whole && put ("PATCH", "/api/stacks/" + pid, n.body) == 200) p.lib = pid;
                    else keptBack.add (String (k + 1));
                    continue;
                }
                // another module has its pocket: what it holds goes up first, unless the library has it as it is
                bool safe = n.lib.isNotEmpty() && ! edited;
                if (! safe && n.lib.isNotEmpty() && n.whole)
                {
                    const int st = put ("PATCH", "/api/stacks/" + n.lib, n.body);
                    safe = st == 200 || (st == 404 && asNew (0).isNotEmpty());
                }
                else if (! safe) safe = asNew (0).isNotEmpty();
                if (safe) p.what = Session::KnobPlan::take; else if (! stuck.contains (String (k + 1))) keptBack.add (String (k + 1));
            }
            else if (n.holds)
            {
                if (n.lib.isNotEmpty())
                {
                    // it came from the chassis, and its pocket there is empty now: taken off there, so it comes off here
                    ++asked;
                    const auto g = http (s, "GET", "/api/stacks/" + n.lib, nullptr, {}, 20000, &callOff);
                    if (g.status == 200)
                    {
                        if (! edited || (n.whole && put ("PATCH", "/api/stacks/" + n.lib, n.body) == 200)) p.what = Session::KnobPlan::clear;
                        else keptBack.add (String (k + 1));
                        continue;
                    }
                    if (g.status != 404) continue;                      // (no answer about it: it is left as it is)
                    // (the library no longer has that module: the knob is all there is of it, and goes up as a new one)
                }
                if (const String id = asNew (k + 1); id.isNotEmpty()) { p.lib = id; sentUp.add (String (k + 1)); }
                else if (! n.never) keptBack.add (String (k + 1));     // (a knob of the rig's own snapshots stays here, and nothing wants its place: nothing to say)
            }
        }
    }
    String line;
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return says ("LOAD: STOPPED");
        const Session::Quiet quiet (ses);
        line = ses.takeTray (tray, &plan);
        for (auto& k : seatedNow) sentUp.addIfNotAlreadyThere (k);
        if (sentUp.size() > 0) line << ". " << (sentUp.size() == 1 ? "KNOB " : "KNOBS ") << sentUp.joinIntoString (", ") << (sentUp.size() == 1 ? " IS" : " ARE") << " ON THE CHASSIS NOW TOO";
        // (what was not as asked comes first: the line is cut short on the main screen)
        String first;
        if (stuck.size() > 0) first << (stuck.size() == 1 ? "KNOB " : "KNOBS ") << stuck.joinIntoString (", ") << (stuck.size() == 1 ? " WAS LEFT AS IT IS: IT HOLDS" : " WERE LEFT AS THEY ARE: THEY HOLD")
                                    << " THE RIG'S OWN SNAPSHOTS, WHICH STAY HERE, AND NO STACK KNOB IS FREE TO MOVE " << (stuck.size() == 1 ? "IT" : "THEM") << " TO. CLEAR ONE IN THE DESIGNER, THEN LOAD. ";
        if (keptBack.size() > 0) first << (keptBack.size() == 1 ? "KNOB " : "KNOBS ") << keptBack.joinIntoString (", ") << (keptBack.size() == 1 ? " WAS LEFT AS IT IS" : " WERE LEFT AS THEY ARE")
                                       << ": THE LIBRARY HAS NOT GOT ALL " << (keptBack.size() == 1 ? "ITS" : "THEIR") << " SNAPSHOTS YET (ONLINE SENDS THEM), OR DID NOT ANSWER. ";
        if (movedSay.size() > 0) first << movedSay.joinIntoString (". ") << ". ";
        line = first + line;
        // (a chip named there is named here now: that is not a rename to send back)
        for (auto& x : ses.library) if (const auto e = ledger.find (x.uid); e != ledger.end() && hasClip.count (x.uid) != 0) e->second.name = x.name;
    }
    writeLedger();
    String out = "LOADED: " + line + (brought > 0 ? ". " + String (brought) + (brought == 1 ? " SNAPSHOT" : " SNAPSHOTS") + " NEW HERE" : String (". NO SNAPSHOT WAS NEW HERE"));
    if (silent > 0) out << " (" << silent << (silent == 1 ? " NAMES" : " NAME") << " A SAMPLE THAT IS NOT ON THIS COMPUTER)";
    if (lost > 0) out << ". " << lost << " COULD NOT BE BROUGHT";
    return says (out);
}

void Online::run()
{
    while (! threadShouldExit())
    {
        if (wantLoad.exchange (false)) load();
        bool more = pass();
        more = mirror() || more;
        more = rack() || more;
        more = archive() || more;
        const auto st = status().state;
        // a clip on its way: soon. Trouble: a little longer each time, two minutes at most. Else: when poked, or every so often.
        const int ms = more && failures == 0 ? 1500 : failures > 0 ? juce::jmin (120000, 5000 * (1 << juce::jmin (5, failures - 1))) : (st == State::upToDate ? 20000 : 60000);
        wait (ms);
    }
}
} // namespace sf0
