#ifdef KEYPIANO_ENABLE_GUI

// ── Phase 3+ Qt GUI entry point ───────────────────────────────────────────────
#include "MainWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("keypiano");
    app.setApplicationVersion("1.0.0");
    keypiano::ui::MainWindow w;
    w.show();
    return app.exec();
}

#else

// ── Phase 1 / Phase 2 headless entry point (test / CI builds) ─────────────────
#include "config/CfgParser.h"
#include "audio/AudioEngine.h"
#include "synth/SynthFactory.h"
#include "keymap/KeyMap.h"
#include "keymap/KeyMapParser.h"
#include "platform/KeyboardHook.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char* argv[]) {
    // ── 1. Load config ────────────────────────────────────────────────────────
    std::string cfg_path = (argc >= 2) ? argv[1] : "freepiano.cfg";
    std::string cfg_text = readFile(cfg_path);
    if (cfg_text.empty()) {
        std::fprintf(stderr, "keypiano: cannot read config '%s'\n", cfg_path.c_str());
        return EXIT_FAILURE;
    }

    auto cfg_result = keypiano::CfgParser::parse(cfg_text);
    for (auto& e : cfg_result.errors)
        std::fprintf(stderr, "cfg warning: %s\n", e.c_str());

    const keypiano::AppConfig& cfg = cfg_result.config;
    std::printf("keypiano: sf2=%s  keymap=%s  rate=%u  buf=%u\n",
                cfg.sf2_path.c_str(), cfg.keymap_path.c_str(),
                cfg.sample_rate, cfg.buffer_frames);

    // ── 2. Load keymap ────────────────────────────────────────────────────────
    keypiano::KeyMap keymap;
    if (!cfg.keymap_path.empty()) {
        std::string map_text = readFile(cfg.keymap_path);
        if (map_text.empty()) {
            std::fprintf(stderr, "keypiano: cannot read keymap '%s'\n",
                         cfg.keymap_path.c_str());
        } else {
            auto map_result = keypiano::KeyMapParser::parse(map_text);
            for (auto& e : map_result.errors)
                std::fprintf(stderr, "map warning: %s\n", e.c_str());
            keymap = std::move(map_result.map);
            std::printf("keypiano: loaded %zu key bindings\n",
                        keymap.bindings().size());
        }
    }

    // ── 3. Initialise synth + audio engine ────────────────────────────────────
    auto synth = keypiano::createFluidSynth();

    keypiano::audio::AudioEngine engine;
    keypiano::audio::AudioEngine::Config audio_cfg;
    audio_cfg.sample_rate   = cfg.sample_rate;
    audio_cfg.buffer_frames = cfg.buffer_frames;
    audio_cfg.device_name   = cfg.audio_device;

    if (!engine.open(audio_cfg, synth.get())) {
        std::fprintf(stderr, "keypiano: failed to open audio device\n");
        return EXIT_FAILURE;
    }

    if (!cfg.sf2_path.empty()) {
        if (!synth->loadInstrument(cfg.sf2_path)) {
            std::fprintf(stderr, "keypiano: failed to load SF2 '%s'\n",
                         cfg.sf2_path.c_str());
        } else {
            std::printf("keypiano: loaded SF2 '%s'\n", cfg.sf2_path.c_str());
        }
    }

    // ── 4. Install keyboard hook ──────────────────────────────────────────────
    keypiano::ChannelState ch0, ch1;

    auto hook = keypiano::KeyboardHook::create();
    bool hook_ok = hook->install([&](const keypiano::KeyEvent& ev) {
        auto midi = keymap.resolve(ev.vk_code, ev.is_keydown, ch0, ch1);
        if (!midi) return;

        if (midi->type == keypiano::EventType::NoteOn)
            engine.postNoteOn(midi->chan, midi->note, midi->vel);
        else
            engine.postNoteOff(midi->chan, midi->note);
    });

    if (!hook_ok) {
        std::fprintf(stderr, "keypiano: failed to install keyboard hook\n");
        engine.close();
        return EXIT_FAILURE;
    }

    std::printf("keypiano: running — press q + Enter to quit\n");

    // ── 5. Wait for quit ──────────────────────────────────────────────────────
    for (;;) {
        int c = std::getchar();
        if (c == 'q' || c == 'Q' || c == EOF) break;
    }

    hook->uninstall();
    engine.close();
    std::printf("keypiano: shutdown\n");
    return EXIT_SUCCESS;
}

#endif // KEYPIANO_ENABLE_GUI
