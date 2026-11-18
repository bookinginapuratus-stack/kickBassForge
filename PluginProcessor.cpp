#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static constexpr double twoPi = 6.283185307179586;

static float polyBlep(double t, double dt)
{
    if (t < dt)       { t /= dt;               return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return (float)(t * t + t + t + 1.0); }
    return 0.f;
}

juce::AudioProcessorValueTreeState::ParameterLayout KickBassProcessor::makeLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto add = [&](const char* id, const char* name, float lo, float hi, float def, float skew = 1.f)
    { l.add(std::make_unique<P>(juce::ParameterID{ id, 1 }, name, R(lo, hi, 0.f, skew), def)); };

    add("bpm",     "BPM (sem host)", 60.f, 200.f, 128.f);
    add("kStart",  "Kick Start Hz",  80.f, 500.f, 180.f, 0.5f);
    add("kEnd",    "Kick End Hz",    30.f, 90.f,  48.f);
    add("kPDecay", "Pitch Decay ms", 5.f,  120.f, 35.f);
    add("kDecay",  "Kick Decay ms",  80.f, 900.f, 320.f);
    add("kClick",  "Click",          0.f,  1.f,   0.3f);
    add("kDrive",  "Kick Drive",     0.f,  1.f,   0.3f);
    add("kLevel",  "Kick Level",     0.f,  1.2f,  0.9f);
    add("bCut",    "Bass Cutoff",    40.f, 6000.f, 400.f, 0.3f);
    add("bRes",    "Bass Reso",      0.f,  1.f,   0.3f);
    add("bSub",    "Sub Mix",        0.f,  1.f,   0.6f);
    add("bDrive",  "Bass Drive",     0.f,  1.f,   0.3f);
    add("bLevel",  "Bass Level",     0.f,  1.2f,  0.8f);
    add("dDepth",  "Duck Depth",     0.f,  1.f,   0.85f);
    add("dRel",    "Duck Release ms", 40.f, 500.f, 180.f);
    add("varAmt",  "Variacao",       0.f,  1.f,   0.4f);
    add("master",  "Master",         0.f,  1.f,   0.8f);
    return l;
}

KickBassProcessor::KickBassProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", makeLayout())
{
    initDefaultPattern();
}

void KickBassProcessor::initDefaultPattern()
{
    for (int s = 0; s < 16; ++s)
    {
        pattern.kick[s] = (s % 4 == 0) ? 1.f : 0.f;
        pattern.bassNote[s] = (s % 4 == 2) ? ((s == 14) ? 39 : 36) : 0;
        pattern.bassVel[s] = pattern.bassNote[s] > 0 ? 1.f : 0.f;
        pattern.kickProb[s] = (s % 4 == 0) ? 0.95f : 0.1f;
        pattern.bassProb[s] = (s % 4 == 2) ? 0.8f : 0.25f;
    }
    pattern.pool = { { 36, 0.5f }, { 39, 0.2f }, { 43, 0.15f }, { 34, 0.15f } };
}

void KickBassProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 1 };
    bFilter.prepare(spec);
    bFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    bFilter.reset();
    kActive = false; bEnv = 0.f; duckEnv = duckSm = 0.f; lastStep = -1000000;
}

bool KickBassProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo();
}

void KickBassProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();
    const int n = buffer.getNumSamples();

    // --- tempo / posicao
    double bpm = getParam("bpm"), ppq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) { ppq = *p; hostPlaying = pos->getIsPlaying(); }
        }
    const bool standalone = (wrapperType == wrapperType_Standalone);
    const bool running = hostPlaying || standalone;
    if (!hostPlaying) ppq = internalPpq;
    if (!running) lastStep = -1000000;
    const double ppqPerSample = bpm / 60.0 / sr;
    const int stepSamples = (int)(sr * 60.0 / bpm / 4.0);

    // --- parametros
    const double kStart = getParam("kStart"), kEnd = getParam("kEnd");
    const double pTau = getParam("kPDecay") * 0.001, dTau = getParam("kDecay") * 0.001 / 3.0;
    const float kClick = getParam("kClick"), kGain = 1.f + getParam("kDrive") * 6.f;
    const float kLevel = getParam("kLevel"), bLevel = getParam("bLevel");
    const float bSub = getParam("bSub"), bGain = 1.f + getParam("bDrive") * 5.f;
    const float depth = getParam("dDepth"), master = getParam("master");
    const float duckCoef = std::exp(-1.f / (float)(sr * getParam("dRel") * 0.001));
    const float duckAtk = 1.f - std::exp(-1.f / (float)(sr * 0.001));
    const float envAtk = 1.f - std::exp(-1.f / (float)(sr * 0.003));
    const float envRel = 1.f - std::exp(-1.f / (float)(sr * 0.02));
    bFilter.setCutoffFrequency(getParam("bCut"));
    bFilter.setResonance(0.7f + getParam("bRes") * 7.f);

    auto* L = buffer.getWritePointer(0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        // --- sequenciador (grade de 16avos)
        if (running)
        {
            const int step = (int)std::floor((ppq + i * ppqPerSample) * 4.0);
            if (step != lastStep)
            {
                lastStep = step;
                const int s = ((step % 16) + 16) % 16;
                juce::SpinLock::ScopedTryLockType tl(lock);
                if (tl.isLocked())
                {
                    if (pattern.kick[s] > 0.f)
                    { kActive = true; kT = 0.0; kPhase = 0.0; kVel = pattern.kick[s]; duckEnv = 1.f; }
                    if (pattern.bassNote[s] > 0)
                    {
                        bFreq = 440.f * std::pow(2.f, (pattern.bassNote[s] - 69) / 12.f);
                        bGate = true; bGateSamples = (int)(stepSamples * 0.85f);
                    }
                }
            }
        }

        // --- kick
        float k = 0.f;
        if (kActive)
        {
            const double f = kEnd + (kStart - kEnd) * std::exp(-kT / pTau);
            kPhase += f / sr; if (kPhase >= 1.0) kPhase -= 1.0;
            const float aEnv = (float)((1.0 - std::exp(-kT / 0.0008)) * std::exp(-kT / dTau));
            const float noise = rng.nextFloat() * 2.f - 1.f;
            clickLp += 0.25f * (noise - clickLp);
            const float click = (noise - clickLp) * (float)std::exp(-kT / 0.003) * kClick;
            const float body = (float)std::sin(twoPi * kPhase) * aEnv;
            k = (std::tanh(body * kGain) / std::tanh(kGain) + click * 0.5f) * kVel;
            kT += 1.0 / sr;
            if (kT > 0.02 && aEnv < 1e-4f) kActive = false;
        }

        // --- sidechain interno (disparado pelo kick)
        duckEnv *= duckCoef;
        duckSm += (duckEnv - duckSm) * duckAtk;
        const float duck = 1.f - depth * duckSm;

        // --- baixo
        if (bGateSamples > 0 && --bGateSamples == 0) bGate = false;
        bEnv += ((bGate ? 1.f : 0.f) - bEnv) * (bGate ? envAtk : envRel);
        float b = 0.f;
        if (bEnv > 1e-5f)
        {
            const double dt = bFreq / sr;
            bPhase += dt; if (bPhase >= 1.0) bPhase -= 1.0;
            const float saw = (float)(2.0 * bPhase - 1.0) - polyBlep(bPhase, dt);
            const float sub = (float)std::sin(twoPi * bPhase);
            float x = sub * bSub + saw * (1.f - bSub) * 0.7f;
            x = bFilter.processSample(0, x);
            x = std::tanh(x * bGain) / std::tanh(bGain);
            b = x * bEnv;
        }

        const float out = std::tanh((k * kLevel + b * bLevel * duck) * master);
        L[i] = out; if (R) R[i] = out;
    }
    if (!hostPlaying) internalPpq += n * ppqPerSample;
}

// ---------- padroes / analise / variacoes ----------
void KickBassProcessor::setParam(const char* id, double v)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1((float)v));
}

juce::var KickBassProcessor::patternToVar(const Pattern& p)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> kp, bp, kpr, bpr, notes;
    for (int i = 0; i < 16; ++i)
    { kp.add(p.kick[i]); bp.add(p.bassNote[i]); kpr.add(p.kickProb[i]); bpr.add(p.bassProb[i]); }
    for (auto& e : p.pool)
    { auto* x = new juce::DynamicObject(); x->setProperty("note", e.first); x->setProperty("weight", e.second); notes.add(juce::var(x)); }
    o->setProperty("kickPattern", kp); o->setProperty("bassPattern", bp);
    o->setProperty("kickProb", kpr);   o->setProperty("bassProb", bpr);
    o->setProperty("bassNotes", notes);
    return juce::var(o);
}

void KickBassProcessor::varToPattern(const juce::var& v, Pattern& p)
{
    auto read16 = [&](const char* key, auto setter)
    {
        if (auto* a = v[key].getArray())
            if (a->size() == 16) for (int i = 0; i < 16; ++i) setter(i, (*a)[i]);
    };
    read16("kickPattern", [&](int i, const juce::var& x) { p.kick[i] = (float)x > 0.f ? 1.f : 0.f; });
    read16("bassPattern", [&](int i, const juce::var& x) { p.bassNote[i] = (int)x; p.bassVel[i] = (int)x > 0 ? 1.f : 0.f; });
    read16("kickProb",    [&](int i, const juce::var& x) { p.kickProb[i] = juce::jlimit(0.f, 1.f, (float)x); });
    read16("bassProb",    [&](int i, const juce::var& x) { p.bassProb[i] = juce::jlimit(0.f, 1.f, (float)x); });
    if (auto* a = v["bassNotes"].getArray())
    {
        p.pool.clear();
        for (auto& e : *a) p.pool.push_back({ (int)e["note"], (float)e["weight"] });
    }
}

bool KickBassProcessor::loadAnalysis(const juce::File& f)
{
    auto v = juce::JSON::parse(f);
    if (!v.isObject()) return false;
    auto num = [&](const juce::var& o, const char* k, const char* id)
    { if (o.hasProperty(k)) setParam(id, (double)o[k]); };
    if (v.hasProperty("bpm")) setParam("bpm", (double)v["bpm"]);
    num(v["kick"], "startHz", "kStart");   num(v["kick"], "endHz", "kEnd");
    num(v["kick"], "pitchDecayMs", "kPDecay"); num(v["kick"], "decayMs", "kDecay");
    num(v["kick"], "click", "kClick");     num(v["kick"], "drive", "kDrive");
    num(v["bass"], "cutoff", "bCut");      num(v["bass"], "sub", "bSub");
    num(v["bass"], "drive", "bDrive");
    Pattern p; { juce::SpinLock::ScopedLockType l(lock); p = pattern; }
    varToPattern(v, p);
    { juce::SpinLock::ScopedLockType l(lock); pattern = p; }
    return true;
}

void KickBassProcessor::generateVariation()
{
    const float amount = getParam("varAmt");
    Pattern cur; { juce::SpinLock::ScopedLockType l(lock); cur = pattern; }
    Pattern nw = cur;
    juce::Random r;
    float total = 0.f; for (auto& e : cur.pool) total += e.second;
    auto pick = [&]() -> int
    {
        if (cur.pool.empty() || total <= 0.f) return 0;
        float x = r.nextFloat() * total;
        for (auto& e : cur.pool) if ((x -= e.second) <= 0.f) return e.first;
        return cur.pool.back().first;
    };
    for (int s = 0; s < 16; ++s)
    {
        const float kBase = cur.kick[s] > 0.f ? 1.f : 0.f;
        const float kChance = kBase * (1.f - amount) + cur.kickProb[s] * amount;
        nw.kick[s] = (s == 0 || r.nextFloat() < kChance) ? 1.f : 0.f;

        const float bBase = cur.bassNote[s] > 0 ? 1.f : 0.f;
        const float bChance = bBase * (1.f - amount) + cur.bassProb[s] * amount;
        if (r.nextFloat() < bChance)
        {
            int note = cur.bassNote[s] > 0 ? cur.bassNote[s] : pick();
            if (r.nextFloat() < amount * 0.5f) note = pick();
            if (note > 0 && r.nextFloat() < amount * 0.1f) note += (note <= 40 ? 12 : -12);
            nw.bassNote[s] = note;
            nw.bassVel[s] = note > 0 ? 0.75f + 0.25f * r.nextFloat() : 0.f;
        }
        else { nw.bassNote[s] = 0; nw.bassVel[s] = 0.f; }
    }
    juce::SpinLock::ScopedLockType l(lock);
    pattern = nw;
}

// ---------- estado ----------
void KickBassProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    { juce::SpinLock::ScopedLockType l(lock);
      state.setProperty("pattern", juce::JSON::toString(patternToVar(pattern), true), nullptr); }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void KickBassProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (!state.isValid()) return;
        auto pat = state.getProperty("pattern").toString();
        apvts.replaceState(state);
        if (pat.isNotEmpty())
        {
            Pattern p; { juce::SpinLock::ScopedLockType l(lock); p = pattern; }
            varToPattern(juce::JSON::parse(pat), p);
            juce::SpinLock::ScopedLockType l(lock); pattern = p;
        }
    }
}

juce::AudioProcessorEditor* KickBassProcessor::createEditor() { return new KickBassEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KickBassProcessor(); }
