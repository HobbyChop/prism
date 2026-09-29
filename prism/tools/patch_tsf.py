#!/usr/bin/env python3
"""Apply the local changes to TinySoundFont (tsf.h, MIT) in place: 16-bit
sample storage, per-channel tone control and a per-part render entry point.
Idempotent; the guard string is PRISM. Normalise tsf.h to LF first.
"""
import io, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
P = os.path.join(HERE, '..', 'tsf', 'tsf.h')
s = io.open(P, encoding='utf-8', newline='').read()
if 'PRISM' in s:
    print('tsf.h already patched')
    sys.exit(0)

def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (s.count(a), a[:70])
    s = s.replace(a, b)

# ---- 1. 16-bit samples ---------------------------------------------------
rep("\tfloat* fontSamples;\n", "\tshort* fontSamples; /* PRISM: 16-bit, not float */\n")
rep("static int tsf_load_samples(void** pRawBuffer, float** pFloatBuffer, unsigned int* pSmplCount, struct tsf_riffchunk *chunkSmpl, struct tsf_stream* stream)",
    "static int tsf_load_samples(void** pRawBuffer, short** pFloatBuffer, unsigned int* pSmplCount, struct tsf_riffchunk *chunkSmpl, struct tsf_stream* stream)")
rep("""	// Inline convert the samples from short to float
	float *res, *out; const short *in;
	(void)pRawBuffer;
	*pSmplCount = chunkSmpl->size / (unsigned int)sizeof(short);
	*pFloatBuffer = (float*)TSF_MALLOC(*pSmplCount * sizeof(float));
	if (!*pFloatBuffer || !stream->read(stream->data, *pFloatBuffer, chunkSmpl->size)) return 0;
	for (res = *pFloatBuffer, out = res + *pSmplCount, in = (short*)res + *pSmplCount; out != res;)
		*(--out) = (float)(*(--in) / 32767.0);
	return 1;
""",
"""	/* PRISM: the samples stay 16-bit; the render loop scales them */
	(void)pRawBuffer;
	*pSmplCount = chunkSmpl->size / (unsigned int)sizeof(short);
	*pFloatBuffer = (short*)TSF_MALLOC(*pSmplCount * sizeof(short));
	if (!*pFloatBuffer || !stream->read(stream->data, *pFloatBuffer, chunkSmpl->size)) return 0;
	return 1;
""")
# the loader's local that receives the buffer
s2, n = re.subn(r"float\s*\*\s*floatBuffer\b", "short* floatBuffer", s)
assert n >= 1, "floatBuffer local"
s = s2
rep("\tfloat* input = f->fontSamples;\n", "\tconst short* input = f->fontSamples; /* PRISM */\n")
rep("float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha);",
    "float alpha = (float)(tmpSourceSamplePosition - pos), val = (input[pos] * (1.0f - alpha) + input[nextPos] * alpha) * (1.0f / 32767.0f); /* PRISM */", 3)

# ---- 2. per-channel tone ---------------------------------------------------
rep("""struct tsf_channel
{
	unsigned short presetIndex, bank, pitchWheel, midiPan, midiVolume, midiExpression, midiRPN, midiData : 14, sustain : 1;
	float panOffset, gainDB, pitchRange, tuning;
};
""",
"""struct tsf_channel
{
	unsigned short presetIndex, bank, pitchWheel, midiPan, midiVolume, midiExpression, midiRPN, midiData : 14, sustain : 1;
	float panOffset, gainDB, pitchRange, tuning;
	float toneFc, toneQ, toneAtk, toneRel; /* PRISM: cutoff cents, resonance dB, envelope multipliers */
};
""")
rep("TSFDEF int tsf_channel_set_tuning(tsf* f, int channel, float tuning);\n",
    "TSFDEF int tsf_channel_set_tuning(tsf* f, int channel, float tuning);\n"
    "/* PRISM: the part's own filter and envelope offsets, on every voice it starts */\n"
    "TSFDEF int tsf_channel_set_tone(tsf* f, int channel, float fc_cents, float q_db, float attack_mul, float release_mul);\n")
rep("""		// Setup envelopes.
		tsf_voice_envelope_setup(&voice->ampenv, &region->ampenv, key, midiVelocity, TSF_TRUE, f->outSampleRate);
		tsf_voice_envelope_setup(&voice->modenv, &region->modenv, key, midiVelocity, TSF_FALSE, f->outSampleRate);

		// Setup lowpass filter.
		lowpassFc = (region->initialFilterFc <= 13500 ? tsf_cents2Hertz((float)region->initialFilterFc) / f->outSampleRate : 1.0f);
		lowpassFilterQDB = region->initialFilterQ / 10.0f;
""",
"""		// Setup envelopes. PRISM: the channel's tone rides on the region's values.
		{
			struct tsf_envelope ae = region->ampenv;
			float fcCents = (float)region->initialFilterFc, qAdd = 0.0f;
			if (f->channels)
			{
				struct tsf_channel* tc = &f->channels->channels[f->channels->activeChannel];
				if (tc->toneAtk > 0.0f) ae.attack *= tc->toneAtk;
				if (tc->toneRel > 0.0f) ae.release *= tc->toneRel;
				if (fcCents > 13500.0f) fcCents = 13500.0f;
				fcCents += tc->toneFc;
				if (fcCents < 1500.0f) fcCents = 1500.0f;
				if (fcCents > 13500.0f) fcCents = 13500.0f;
				qAdd = tc->toneQ;
			}
			tsf_voice_envelope_setup(&voice->ampenv, &ae, key, midiVelocity, TSF_TRUE, f->outSampleRate);
			tsf_voice_envelope_setup(&voice->modenv, &region->modenv, key, midiVelocity, TSF_FALSE, f->outSampleRate);

			// Setup lowpass filter.
			lowpassFc = (fcCents < 13500.0f ? tsf_cents2Hertz(fcCents) / f->outSampleRate : 1.0f);
			lowpassFilterQDB = region->initialFilterQ / 10.0f + qAdd;
			if (lowpassFilterQDB < 0.0f) lowpassFilterQDB = 0.0f;
		}
""")
# the API, after set_tuning's definition
m = re.search(r"TSFDEF int tsf_channel_set_tuning\(tsf\* f, int channel, float tuning\)\n\{.*?\n\}\n", s, re.S)
assert m, "set_tuning body"
s = s[:m.end()] + """
/* PRISM */
TSFDEF int tsf_channel_set_tone(tsf* f, int channel, float fc_cents, float q_db, float attack_mul, float release_mul)
{
	struct tsf_channel* c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->toneFc = fc_cents; c->toneQ = q_db; c->toneAtk = attack_mul; c->toneRel = release_mul;
	return 1;
}
""" + s[m.end():]
# new channels start neutral: find where a fresh channel's defaults are set
m = re.search(r"(\t\tc->pitchRange = 2\.0f;)", s)
assert m, "channel defaults"
s = s[:m.end()] + " c->toneFc = 0.0f; c->toneQ = 0.0f; c->toneAtk = 1.0f; c->toneRel = 1.0f; /* PRISM */" + s[m.end():]

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('tsf.h patched')
