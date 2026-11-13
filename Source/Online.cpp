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
        case State::upToDate: return s.sent == 0 ? String ("ONLINE. NOTHING TAKEN YET TO SEND.") : "ONLINE. THE LIBRARY HOLDS " + (s.sent == 1 ? String ("THE ONE SNAPSHOT TAKEN.") : "ALL " + String (s.sent) + " SNAPSHOTS TAKEN.");
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
    if (sentTo != now.url) { sentTo = now.url; return; }                    // another library: it is asked afresh
    if (auto* o = v.getProperty ("sent", var()).getDynamicObject())
        for (auto& p : o->getProperties())
        {
            Entry e;
            e.snap = (bool) p.value.getProperty ("snap", false); e.clip = (bool) p.value.getProperty ("clip", false);
            e.gone = (bool) p.value.getProperty ("gone", false); e.refused = (bool) p.value.getProperty ("refused", false);
            e.name = p.value.getProperty ("name", "").toString();
            e.prof = (bool) p.value.getProperty ("prof", false);
            ledger[p.name.toString()] = e;
        }
    if (auto* a = v.getProperty ("defs", var()).getArray()) for (auto& d : *a) defsSent.insert (d.toString());
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
        o->setProperty ("name", kv.second.name);
        sent->setProperty (kv.first, var (o));
    }
    juce::Array<var> defs;
    for (auto& d : defsSent) defs.add (d);
    auto* root = new juce::DynamicObject();
    root->setProperty ("url", sentTo);
    root->setProperty ("sent", var (sent));
    root->setProperty ("defs", defs);
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
    struct Item { String uid, name; juce::int64 id = 0; bool clipDue = false; juce::File clip; bool prof = false; };
    std::vector<Item> items;
    {
        // the library is the message thread's: read with its lock (given up at once if this thread is asked to stop)
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return false;
        const auto nowMs = juce::Time::currentTimeMillis();
        for (auto& x : ses.library)
            if (x.id >= kFirstTaken && x.uid.isNotEmpty())
                items.push_back ({ x.uid, x.name, x.id, x.profile.has && nowMs - x.time.toMilliseconds() < 120000, ses.clipFile (x.id), x.profile.has });
    }
    auto left = [&]
    {
        int n = 0;
        for (auto& it : items)
        {
            const auto e = ledger.find (it.uid);
            if (e != ledger.end() && (e->second.gone || e->second.refused)) continue;
            if (e == ledger.end() || ! e->second.snap || e->second.name != it.name || (it.prof && ! e->second.prof) || (! e->second.clip && it.clip.existsAsFile())) ++n;
        }
        return n;
    };
    auto held = [&] { int n = 0; for (auto& it : items) { const auto e = ledger.find (it.uid); if (e != ledger.end() && e->second.snap) ++n; } return n; };

    if (! s.on)          { say (State::off, left(), held()); return false; }
    if (! s.complete())  { say (State::notSetUp, left(), held()); return false; }
    if (sentTo != s.url) { ledger.clear(); defsSent.clear(); sentTo = s.url; asksFirst = true; }

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
                if (was.name.isEmpty()) e.name = it.name;
                e.prof = e.prof || e.clip;                      // (a clip and a profile arrive together)
            }
            changed = changed || was.snap != e.snap || was.clip != e.clip || was.gone != e.gone || was.name != e.name;
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
    }
    if (changed) writeLedger();
    const int n = left();
    say (n > 0 ? State::working : State::upToDate, n, held());
    return soon || n > 0;
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
    says ("LOAD: ASKING THE LIBRARY WHAT ITS TRAY HOLDS");
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
        ++asked;
        const auto one = http (s, "GET", "/api/snapshots/" + juce::URL::addEscapeChars (uid, true), nullptr, {}, 20000, &callOff);
        const var got = juce::JSON::parse (one.body);
        const var snap = got.getProperty ("snapshot", var());
        if (one.status != 200 || ! snap.isObject()) { if (one.status == 0 || turnedAway (one.status)) return failed (one); ++lost; continue; }
        // the definitions it names that this library has not got
        juce::StringArray want;
        {
            const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
            if (! mm.lockWasGained()) return says ("LOAD: STOPPED");
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
            if (d.status != 200) return failed (d);
            if (auto* o = juce::JSON::parse (d.body).getProperty ("defs", var()).getDynamicObject())
                for (auto& p : o->getProperties()) defs->setProperty (p.name, p.value);
        }
        // its four bars
        juce::MemoryBlock clip;
        if (hasClip[uid])
        {
            ++asked;
            auto c = http (s, "GET", "/api/clips/" + juce::URL::addEscapeChars (uid, true), nullptr, {}, 30000, &callOff, true);
            if (c.status == 200) clip = std::move (c.bytes);
            else if (c.status == 0) return failed (c);
        }
        juce::int64 id = 0; juce::File clipAt; String nameHere; bool prof = false;
        {
            const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
            if (! mm.lockWasGained()) return says ("LOAD: STOPPED");
            id = ses.takeFromLibrary (snap, got.getProperty ("name", "").toString(), defsVar);
            if (id != 0)
            {
                clipAt = ses.clipFile (id);
                if (const auto* sn = ses.findSnap (id)) { nameHere = sn->name; prof = sn->profile.has; }
                if (ses.filesMissing (id) > 0) ++silent;
            }
        }
        if (id == 0) { ++lost; continue; }
        if (clip.getSize() > 0) { clipAt.getParentDirectory().createDirectory(); clipAt.replaceWithData (clip.getData(), clip.getSize()); }
        // (the library has it: it is not to be sent back)
        auto& e = ledger[uid];
        e.snap = true; e.clip = clip.getSize() > 0 || ! hasClip[uid]; e.prof = prof; e.name = nameHere; e.gone = false;
        ++brought;
    }
    String line;
    {
        const juce::MessageManagerLock mm (juce::Thread::getCurrentThread());
        if (! mm.lockWasGained()) return says ("LOAD: STOPPED");
        line = ses.takeTray (tray);
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
        const bool more = pass();
        const auto st = status().state;
        // a clip on its way: soon. Trouble: a little longer each time, two minutes at most. Else: when poked, or every so often.
        const int ms = more && failures == 0 ? 1500 : failures > 0 ? juce::jmin (120000, 5000 * (1 << juce::jmin (5, failures - 1))) : (st == State::upToDate ? 20000 : 60000);
        wait (ms);
    }
}
} // namespace sf0
