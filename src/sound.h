#pragma once

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Audio — tiny procedural synth through the classic SDL audio callback
// ---------------------------------------------------------------------------
enum Sfx { SFX_WAKA_A, SFX_WAKA_B, SFX_POWER, SFX_EAT_GHOST, SFX_DEATH,
           SFX_START, SFX_LEVEL_CLEAR, SFX_COUNT };

class SoundEngine {
public:
    std::vector<int16_t> queue;      // samples to play (concatenated)
    bool    enabled = true;
    bool    opened = false;
    int     rate = 22050;
    SDL_mutex *mtx = nullptr;

    static void audioCB(void *ud, Uint8 *buf, int len) {
        SoundEngine *s = (SoundEngine *)ud;
        int n = len / 2;
        SDL_LockMutex(s->mtx);
        if (s->enabled && !s->queue.empty()) {
            int take = (int)std::min<size_t>(n, s->queue.size());
            std::memcpy(buf, s->queue.data(), (size_t)take * 2);
            s->queue.erase(s->queue.begin(), s->queue.begin() + take);
            if (take < n) std::memset(buf + (size_t)take * 2, 0, (size_t)(n - take) * 2);
        } else {
            std::memset(buf, 0, (size_t)len);
        }
        SDL_UnlockMutex(s->mtx);
    }

    void init() {
        mtx = SDL_CreateMutex();
        SDL_AudioSpec want{};
        want.freq = rate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = audioCB;
        want.userdata = this;
        if (SDL_OpenAudio(&want, nullptr) < 0) {
            enabled = false;
            return;
        }
        opened = true;
    }
    void destroy() {
        if (opened) SDL_CloseAudio();
        if (mtx) { SDL_DestroyMutex(mtx); mtx = nullptr; }
    }
    void toggle() {
        SDL_LockMutex(mtx);
        enabled = !enabled;
        if (!enabled) queue.clear();
        SDL_UnlockMutex(mtx);
    }

    // Render a whole effect into the queue (called on the game thread)
    void play(Sfx s) {
        if (!enabled || !opened) return;
        std::vector<int16_t> tmp;
        render(s, tmp);
        SDL_LockMutex(mtx);
        if (queue.size() > (size_t)rate * 2) queue.clear();  // safety cap
        queue.insert(queue.end(), tmp.begin(), tmp.end());
        SDL_UnlockMutex(mtx);
    }

private:
    static double freqAt(Sfx s, double t) {
        switch (s) {
            case SFX_WAKA_A: return 520;
            case SFX_WAKA_B: return 380;
            case SFX_POWER:  return 200 + 700 * (t / 0.35);
            case SFX_EAT_GHOST:
                if (t < 0.10) return 660;
                if (t < 0.20) return 880;
                return 1320;
            case SFX_DEATH:  return 700 - 620 * std::min(1.0, t / 1.0);
            case SFX_START:  return t < 0.12 ? 440 : t < 0.24 ? 554 : t < 0.36 ? 659 : 880;
            case SFX_LEVEL_CLEAR:
                if (t < 0.08) return 523;
                if (t < 0.16) return 659;
                if (t < 0.24) return 784;
                if (t < 0.32) return 1046;
                if (t < 0.40) return 784;
                return 1046;
            case SFX_COUNT: break;
        }
        return 440;
    }
    static double durAt(Sfx s) {
        switch (s) {
            case SFX_WAKA_A: case SFX_WAKA_B: return 0.07;
            case SFX_POWER:  return 0.35;
            case SFX_EAT_GHOST: return 0.30;
            case SFX_DEATH:  return 1.1;
            case SFX_START:  return 0.48;
            case SFX_LEVEL_CLEAR: return 0.48;
            case SFX_COUNT: break;
        }
        return 0.1;
    }
    static void render(Sfx s, std::vector<int16_t> &out) {
        double dur = durAt(s);
        int n = (int)(dur * 22050);
        out.reserve(n);
        for (int i = 0; i < n; i++) {
            double t = (double)i / 22050.0;
            double f = freqAt(s, t);
            double v = std::sin(2.0 * 3.14159265358979 * f * t);
            double env = 1.0 - (t / dur) * 0.6;
            int16_t smp = (int16_t)(v * 9000 * env);
            out.push_back(smp);
        }
    }
};

extern SoundEngine SFX;
