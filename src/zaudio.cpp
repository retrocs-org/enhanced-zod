#include "zprefs.h"
#include <algorithm>
#include "zaudio.h"
#include <vector>
#include <string>
#include <stdlib.h>
#include <string.h>

// One mixer device; SFX play on a pool of tracks (emulating SDL2_mixer
// "channels"); music gets its own dedicated track. The engine's volumes are
// 0..ZAUDIO_MAX_VOLUME ints; MIX_ gains are 0.0..1.0 floats.
static MIX_Mixer              *g_mixer = NULL;
static std::vector<MIX_Track*> g_channels;
static std::vector<float> g_effect_base;
static float g_music_base=80.0f/128;
static MIX_Track              *g_music = NULL;

// ---- music track ownership ---------------------------------------------------
// The music track can be driven from two threads: the game thread (recorded
// music, stop, seek) and the MIDI worker below. g_music_lock serialises them,
// and every request bumps g_music_generation so the worker can tell that the
// MIDI it was preparing has been superseded.
static SDL_Mutex     *g_music_lock = NULL;
static SDL_Condition *g_midi_wake = NULL;
static SDL_Thread    *g_midi_thread = NULL;
static bool           g_midi_quit = false;
static Uint64         g_music_generation = 0;

static MIX_Audio     *g_midi_audio = NULL;      // MIDI currently on the music track
static void          *g_midi_audio_data = NULL; // its file bytes (must outlive the audio)

struct MidiRequest
{
    bool pending;
    Uint64 generation;
    std::vector<unsigned char> data;
    int loops;
    int fade_in_ms;
};
static MidiRequest g_midi_request;

static std::string g_soundfont;

static float env_float(const char *name, float fallback)
{
    const char *v = getenv(name);
    return (v && *v) ? (float)atof(v) : fallback;
}
static int         g_midi_state = -1;   // -1 unknown, 0 unavailable, 1 available

static void music_lock()   { if (g_music_lock) SDL_LockMutex(g_music_lock); }
static void music_unlock() { if (g_music_lock) SDL_UnlockMutex(g_music_lock); }

// call with the music lock held
static void release_midi_audio()
{
    if (g_midi_audio) { MIX_DestroyAudio(g_midi_audio); g_midi_audio = NULL; }
    if (g_midi_audio_data) { SDL_free(g_midi_audio_data); g_midi_audio_data = NULL; }
}

bool ZAudio_Init(void)
{
    return MIX_Init();
}

bool ZAudio_Open(int /*frequency*/, Uint16 /*format*/, int /*channels*/, int /*chunksize*/)
{
    if (!MIX_Init()) return false;
    g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!g_mixer) return false;
    g_music = MIX_CreateTrack(g_mixer);
    if (!g_music_lock) g_music_lock = SDL_CreateMutex();
    if (!g_midi_wake) g_midi_wake = SDL_CreateCondition();
    return true;
}

void ZAudio_Close(void)
{
    // stop the MIDI worker before the mixer goes away
    if (g_midi_thread)
    {
        SDL_LockMutex(g_music_lock);
        g_midi_quit = true;
        g_midi_request.pending = false;
        SDL_SignalCondition(g_midi_wake);
        SDL_UnlockMutex(g_music_lock);
        SDL_WaitThread(g_midi_thread, NULL);
        g_midi_thread = NULL;
    }
    music_lock();
    if (g_music) MIX_StopTrack(g_music, 0);
    release_midi_audio();
    music_unlock();
    g_midi_state = -1;

    for (MIX_Track *t : g_channels) if (t) MIX_DestroyTrack(t);
    g_channels.clear();
    if (g_music) { MIX_DestroyTrack(g_music); g_music = NULL; }
    if (g_mixer) { MIX_DestroyMixer(g_mixer); g_mixer = NULL; }
    MIX_Quit();
}

bool ZAudio_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
    // The engine only uses these loosely; report sane defaults.
    if (frequency) *frequency = 44100;
    if (format)    *format = SDL_AUDIO_S16LE;
    if (channels)  *channels = 2;
    return true;
}

int ZAudio_AllocateChannels(int numchans)
{
    if (!g_mixer || numchans < 0) return 0;
    for (MIX_Track *t : g_channels) if (t) MIX_DestroyTrack(t);
    g_channels.assign(numchans, NULL);g_effect_base.assign(numchans,1.0f);
    for (int i = 0; i < numchans; i++) g_channels[i] = MIX_CreateTrack(g_mixer);
    return numchans;
}

ZAudio_Sound *ZAudio_LoadSound(const char *file)
{
    if (!g_mixer) return NULL;
    return MIX_LoadAudio(g_mixer, file, true);   // predecode short SFX
}

ZAudio_Music *ZAudio_LoadMusic(const char *file)
{
    if (!g_mixer) return NULL;
    return MIX_LoadAudio(g_mixer, file, false);  // stream music
}

static bool play_track(MIX_Track *t, MIX_Audio *audio, int loops)
{
    if (!t || !audio) return false;
    if (!MIX_SetTrackAudio(t, audio)) return false;
    SDL_PropertiesID props = 0;
    if (loops != 0)   // legacy: 0 = once, -1 = forever, N = repeat N times
    {
        props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
    }
    if(t==g_music)MIX_SetTrackGain(t,g_music_base*zod_music_volume/100.0f);
    bool ok = MIX_PlayTrack(t, props);
    if (props) SDL_DestroyProperties(props);
    return ok;
}

int ZAudio_PlayChannel(int channel, ZAudio_Sound *sound, int loops)
{
    if (!g_mixer) return -1;
    if (channel < 0)   // find a free channel
    {
        for (size_t i = 0; i < g_channels.size(); i++)
            if (g_channels[i] && !MIX_TrackPlaying(g_channels[i])) { channel = (int)i; break; }
        if (channel < 0) return -1;
    }
    if (channel >= (int)g_channels.size() || !g_channels[channel]) return -1;
    g_effect_base[channel]=1.0f;MIX_SetTrackGain(g_channels[channel],zod_effects_volume/100.0f);
    return play_track(g_channels[channel], sound, loops) ? channel : -1;
}

void ZAudio_HaltChannel(int channel)
{
    if (channel < 0) { for (MIX_Track *t : g_channels) if (t) MIX_StopTrack(t, 0); return; }
    if (channel < (int)g_channels.size() && g_channels[channel]) MIX_StopTrack(g_channels[channel], 0);
}

bool ZAudio_ChannelPlaying(int channel)
{
    if (channel < 0)
    {
        for (MIX_Track *t : g_channels) if (t && MIX_TrackPlaying(t)) return true;
        return false;
    }
    if (channel < (int)g_channels.size() && g_channels[channel])
        return MIX_TrackPlaying(g_channels[channel]);
    return false;
}

void ZAudio_ApplyVolumes() {
    for(size_t i=0;i<g_channels.size();++i)if(g_channels[i])MIX_SetTrackGain(g_channels[i],g_effect_base[i]*zod_effects_volume/100.0f);
    if(g_music)MIX_SetTrackGain(g_music,g_music_base*zod_music_volume/100.0f);
}
void ZAudio_SetChannelVolume(int channel,int volume) {
    if(channel>=0 && channel<(int)g_channels.size())g_effect_base[channel]=volume<0?1.0f:volume/(float)ZAUDIO_MAX_VOLUME;
    ZAudio_ApplyVolumes();
}

bool ZAudio_PlayMusic(ZAudio_Music *music, int loops)
{
    music_lock();
    g_music_generation++;
    g_midi_request.pending = false;
    bool ok = play_track(g_music, music, loops);
    release_midi_audio();
    music_unlock();
    return ok;
}

void ZAudio_StopMusic(void)
{
    music_lock();
    g_music_generation++;
    g_midi_request.pending = false;
    if (g_music) MIX_StopTrack(g_music, 0);
    music_unlock();
}

static bool file_readable(const std::string &path)
{
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) return false;
    SDL_CloseIO(io);
    return true;
}

static bool has_soundfont_ext(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && (!SDL_strcasecmp(name + n - 4, ".sf2") || !SDL_strcasecmp(name + n - 4, ".sf3"));
}

static std::string find_soundfont()
{
    // 1. explicit choice
    const char *env = getenv("ZOD_SOUNDFONT");
    if (env && env[0] && file_readable(env)) return env;

    // 2. shipped with the game: any SoundFont in assets/soundfonts (first by name)
    {
        int count = 0;
        char **list = SDL_GlobDirectory("assets/soundfonts", NULL, 0, &count);
        std::string best;
        for (int i = 0; list && i < count; i++)
            if (has_soundfont_ext(list[i]) && (best.empty() || best.compare(list[i]) > 0)) best = list[i];
        SDL_free(list);
        if (!best.empty() && file_readable("assets/soundfonts/" + best)) return "assets/soundfonts/" + best;
    }

    // 3. the conventional SDL_mixer variable, then common system locations
    env = getenv("SDL_SOUNDFONTS");
    if (env && env[0])
    {
        std::string first(env);
        size_t sep = first.find_first_of(":;");
        if (sep != std::string::npos && sep > 1) first = first.substr(0, sep);
        if (file_readable(first)) return first;
    }

    static const char *system_paths[] =
    {
        "/opt/homebrew/share/soundfonts/default.sf2",
        "/usr/local/share/soundfonts/default.sf2",
        "/usr/share/soundfonts/default.sf2",
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
    };
    for (size_t i = 0; i < SDL_arraysize(system_paths); i++)
        if (file_readable(system_paths[i])) return system_paths[i];

    return "";
}

bool ZAudio_MidiAvailable(void)
{
    if (g_midi_state >= 0) return g_midi_state == 1;
    g_midi_state = 0;
    if (!g_mixer) return false;

    bool have_decoder = false;
    for (int i = 0; i < MIX_GetNumAudioDecoders(); i++)
    {
        const char *name = MIX_GetAudioDecoder(i);
        if (name && !SDL_strcasecmp(name, "FLUIDSYNTH")) have_decoder = true;
    }
    if (!have_decoder)
    {
        SDL_Log("MIDI music: this SDL3_mixer has no FluidSynth support - using the recorded music");
        return false;
    }

    g_soundfont = find_soundfont();
    if (g_soundfont.empty())
    {
        SDL_Log("MIDI music: no SoundFont found (assets/soundfonts/*.sf2 or ZOD_SOUNDFONT) - using the recorded music");
        return false;
    }

    SDL_Log("MIDI music: FluidSynth with SoundFont %s", g_soundfont.c_str());
    g_midi_state = 1;
    return true;
}

const char *ZAudio_SoundFontPath(void)
{
    ZAudio_MidiAvailable();
    return g_soundfont.c_str();
}

static int SDLCALL midi_worker(void *)
{
    for (;;)
    {
        std::vector<unsigned char> data;
        Uint64 generation;
        int loops, fade_in_ms;

        SDL_LockMutex(g_music_lock);
        while (!g_midi_quit && !g_midi_request.pending) SDL_WaitCondition(g_midi_wake, g_music_lock);
        if (g_midi_quit) { SDL_UnlockMutex(g_music_lock); return 0; }
        data.swap(g_midi_request.data);
        generation = g_midi_request.generation;
        loops = g_midi_request.loops;
        fade_in_ms = g_midi_request.fade_in_ms;
        g_midi_request.pending = false;
        SDL_UnlockMutex(g_music_lock);

        // The slow part, outside the lock: SDL_mixer reads the SoundFont here.
        void *bytes = SDL_malloc(data.size());
        if (!bytes) continue;
        memcpy(bytes, &data[0], data.size());

        MIX_Audio *audio = NULL;
        SDL_IOStream *io = SDL_IOFromConstMem(bytes, data.size());
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_PropertiesID fluid_props = 0;
        if (io && props)
        {
            SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, io);
            SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, true);
            SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_PREFERRED_MIXER_POINTER, g_mixer);
            SDL_SetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, "FLUIDSYNTH");
            // (property name from SDL_mixer's decoder_fluidsynth.c; it has no public macro in 3.2)
            SDL_SetStringProperty(props, "SDL_mixer.decoder.fluidsynth.soundfont_path", g_soundfont.c_str());
            // SDL_mixer runs FluidSynth at gain 1.0, which clips the loudest tracks (the
            // planet themes peak at full scale). Use a safer default and let the effects be
            // tuned with ZOD_MIDI_GAIN / ZOD_MIDI_REVERB / ZOD_MIDI_CHORUS (0.0 .. 1.0+).
            fluid_props = SDL_CreateProperties();
            if (fluid_props)
            {
                SDL_SetFloatProperty(fluid_props, "synth.gain", env_float("ZOD_MIDI_GAIN", 0.6f));
                const char *rv = getenv("ZOD_MIDI_REVERB");
                if (rv) SDL_SetFloatProperty(fluid_props, "synth.reverb.level", (float)atof(rv));
                const char *ch = getenv("ZOD_MIDI_CHORUS");
                if (ch) SDL_SetFloatProperty(fluid_props, "synth.chorus.level", (float)atof(ch));
                // (no public macro in 3.2 either)
                SDL_SetNumberProperty(props, "SDL_mixer.decoder.fluidsynth.props", (Sint64)fluid_props);
            }
            audio = MIX_LoadAudioWithProperties(props);
        }
        else if (io) SDL_CloseIO(io);
        if (props) SDL_DestroyProperties(props);
        if (fluid_props) SDL_DestroyProperties(fluid_props);   // the decoder keeps its own copy

        if (!audio)
        {
            SDL_Log("MIDI music: could not load: %s", SDL_GetError());
            SDL_free(bytes);
            continue;
        }

        SDL_LockMutex(g_music_lock);
        bool current = (generation == g_music_generation) && !g_midi_quit && g_music;
        bool ok = false;
        if (current && MIX_SetTrackAudio(g_music, audio))
        {
            SDL_PropertiesID play = SDL_CreateProperties();
            if (loops != 0) SDL_SetNumberProperty(play, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
            if (fade_in_ms > 0) SDL_SetNumberProperty(play, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, fade_in_ms);
            ok = MIX_PlayTrack(g_music, play);
            SDL_DestroyProperties(play);
        }
        if (ok)
        {
            release_midi_audio();          // the previous MIDI, now off the track
            g_midi_audio = audio;
            g_midi_audio_data = bytes;
        }
        else
        {
            if (current) SDL_Log("MIDI music: could not start: %s", SDL_GetError());
            MIX_DestroyAudio(audio);
            SDL_free(bytes);
        }
        SDL_UnlockMutex(g_music_lock);
    }
}

void ZAudio_PlayMidi(const unsigned char *data, size_t len, int loops, int fade_in_ms)
{
    if (!data || !len || !ZAudio_MidiAvailable() || !g_music_lock) return;

    SDL_LockMutex(g_music_lock);
    if (!g_midi_thread)
    {
        g_midi_quit = false;
        g_midi_thread = SDL_CreateThread(midi_worker, "zod-midi", NULL);
    }
    g_midi_request.data.assign(data, data + len);
    g_midi_request.loops = loops;
    g_midi_request.fade_in_ms = fade_in_ms;
    g_midi_request.generation = ++g_music_generation;
    g_midi_request.pending = true;
    SDL_SignalCondition(g_midi_wake);
    SDL_UnlockMutex(g_music_lock);
}

void ZAudio_SetMusicVolume(int volume)
{
    g_music_base=volume<0?1.0f:volume/(float)ZAUDIO_MAX_VOLUME;ZAudio_ApplyVolumes();
}

void ZAudio_PauseMusic(bool pause)
{
    if (!g_music) return;
    if (pause) MIX_PauseTrack(g_music); else MIX_ResumeTrack(g_music);
}

bool ZAudio_SetMusicPosition(double position)
{
    if (!g_music) return false;
    // never seek a MIDI: the synthesizer would have to render everything up to
    // the target (seconds of work). The music engine plays a slice instead.
    music_lock();
    bool is_midi = g_midi_audio != NULL || g_midi_request.pending;
    music_unlock();
    if (is_midi) return false;
    Sint64 frames = MIX_TrackMSToFrames(g_music, (Sint64)(position * 1000.0));
    return MIX_SetTrackPlaybackPosition(g_music, frames);
}
