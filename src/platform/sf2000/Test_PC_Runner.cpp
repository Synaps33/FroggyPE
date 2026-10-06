#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include "client/particle/ParticleEngine.h"
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

#include "libretro.h"
#include "Input_SF2000.h"
#include "platform/input/Keyboard.h"
#include "NinecraftApp.h"
#include "client/player/LocalPlayer.h"
#include "world/entity/player/Inventory.h"
#include "client/gui/screens/crafting/WorkbenchScreen.h"
#include "client/gui/screens/crafting/PaneCraftingScreen.h"
#include "world/item/Item.h"
#include "world/item/ItemInstance.h"
#include "client/gui/screens/ArmorScreen.h"
#include "client/gui/screens/IngameBlockSelectionScreen.h"
#include "client/gui/screens/SelectWorldScreen.h"
#include "world/item/crafting/Recipe.h"
#include <typeinfo>
#include <cstdlib>
#include "world/level/LevelSettings.h"
#include "world/level/storage/LevelData.h"
#include "world/level/storage/LevelStorageSource.h"
#include "world/level/storage/ExternalFileLevelStorage.h"
#include "world/level/storage/ExternalFileLevelStorageSource.h"
#include "world/level/storage/FolderMethods.h"
#include "client/OptionsFile.h"
#include "client/gui/Gui.h"
#include "client/gui/components/Slider.h"
#include "client/gui/components/GuiElementContainer.h"
#include "client/gui/Gui.h"
#include "client/gui/screens/OptionsScreen.h"
#include "client/renderer/LevelRenderer.h"
#include "world/level/Level.h"
#include "world/level/tile/Tile.h"
#include "world/entity/MobFactory.h"
#include "world/entity/EntityTypes.h"
#include "client/gui/screens/ChestScreen.h"
#include "client/gui/screens/FurnaceScreen.h"
#include "client/gamemode/GameMode.h"
#include "world/level/tile/Tile.h"
#include "world/level/Level.h"
#include "client/gui/screens/PauseScreen.h"
#include "Rasterizer_SW.h"
#include "platform/input/Mouse.h"

extern NinecraftApp* sf2000_get_app();

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

// Forward declarations of Libretro core API
extern "C" {
    void retro_init(void);
    void retro_deinit(void);
    unsigned retro_api_version(void);
    void retro_get_system_info(struct retro_system_info *info);
    void retro_get_system_av_info(struct retro_system_av_info *info);
    void retro_set_environment(retro_environment_t);
    void retro_set_video_refresh(retro_video_refresh_t);
    void retro_set_audio_sample(retro_audio_sample_t);
    void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
    void retro_set_input_poll(retro_input_poll_t);
    void retro_set_input_state(retro_input_state_t);
    bool retro_load_game(const struct retro_game_info *game);
    bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
    void retro_unload_game(void);
    void retro_run(void);
    void retro_reset(void);
}

static uint16_t s_framebuffer[320 * 240];
static uint32_t s_input_mask = 0;
/*
 * Walk the D-Pad focus to the control with this label and press it.
 *
 * The automation script used to blind-press A and hold DOWN for two dozen frames
 * to slide a pointer onto a button. With focus navigation that lands on whatever
 * happens to be focused, so the script names the control it wants instead.
 */
static bool pressButtonByName(Minecraft* mc, const char* msg)
{
    if (mc == NULL || mc->screen == NULL) return false;

    Screen* sc = mc->screen;
    sc->refreshFocus();

    int want = -1;
    const std::vector<FocusTarget>& ft = sc->getFocusTargets();
    for (size_t i = 0; i < ft.size(); ++i)
        if (ft[i].button && ft[i].button->msg == msg) { want = (int)i; break; }
    if (want < 0)
    {
        printf("[PC TEST] no focusable control labelled '%s' on %s\n",
               msg, typeid(*sc).name());
        return false;
    }

    /*
     * Two navigation modes, and A honours whichever the screen asked for. In
     * cursor mode the focus walk is meaningless: put the pointer on the control
     * instead, which is what a player does.
     */
    if (sc->getNavMode() == GUI_NAV_CURSOR)
    {
        const FocusTarget& t = ft[want];
        sf2000_input::setCursorPos((int)(t.centreX() / Gui::InvGuiScale),
                                   (int)(t.centreY() / Gui::InvGuiScale));
        sf2000_input::setPadState(sf2000_input::BTN_A);
        sf2000_input::poll(mc);
        sf2000_input::setPadState(0);
        sf2000_input::poll(mc);
        if (mc->screen) mc->screen->updateEvents();
        return true;
    }

    for (int step = 0; step < 80 && sc->getFocusIndex() != want; ++step)
    {
        sc->refreshFocus();
        const std::vector<FocusTarget>& f = sc->getFocusTargets();
        const int fi = sc->getFocusIndex();
        if (fi < 0 || fi >= (int)f.size()) break;

        const int ddx = f[want].centreX() - f[fi].centreX();
        const int ddy = f[want].centreY() - f[fi].centreY();
        uint32_t dir;
        if (std::abs(ddx) >= std::abs(ddy))
            dir = ddx > 0 ? sf2000_input::BTN_RIGHT : sf2000_input::BTN_LEFT;
        else
            dir = ddy > 0 ? sf2000_input::BTN_DOWN : sf2000_input::BTN_UP;

        sf2000_input::setPadState(0);
        sf2000_input::poll(mc);
        sf2000_input::setPadState(dir);
        sf2000_input::poll(mc);
        sf2000_input::setPadState(0);
        sf2000_input::poll(mc);

        if (mc->screen != sc) return false;   // the walk opened something already
    }

    if (sc->getFocusIndex() != want) return false;

    sf2000_input::setPadState(sf2000_input::BTN_A);
    sf2000_input::poll(mc);
    sf2000_input::setPadState(0);
    sf2000_input::poll(mc);
    if (mc->screen) mc->screen->updateEvents();
    return true;
}

static int s_current_frame = 0;

/* Wall-clock cost of the last retro_run(), milliseconds. */
static double s_frameMs = 0.0;
static double s_lastCfgMs = 0.0;
static int    s_cfgFrames = 0;
static int    s_perfSlot = 0;
static int    s_perfRes[6]  = { 0, 0, 0, 0, 0, 0 };
static int    s_perfDist[6] = { 0, 0, 0, 0, 0, 0 };
static int    s_perfGui[6]  = { 0, 0, 0, 0, 0, 0 };
static bool s_has_new_frame = false;

static bool cb_environment(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    {
        const enum retro_pixel_format *fmt = (const enum retro_pixel_format *)data;
        return (*fmt == RETRO_PIXEL_FORMAT_RGB565);
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    {
        const char **dir = (const char **)data;
        *dir = ".";
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        return false;
    default:
        return false;
    }
}

static void cb_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    const uint8_t *src = (const uint8_t *)data;
    uint8_t *dst = (uint8_t *)s_framebuffer;
    size_t row_bytes = width * sizeof(uint16_t);
    for (unsigned y = 0; y < height && y < 240; ++y)
    {
        memcpy(dst + y * 320 * sizeof(uint16_t), src + y * pitch, row_bytes);
    }
    s_has_new_frame = true;
}

static void cb_audio_sample(int16_t left, int16_t right)
{
    (void)left; (void)right;
}

static size_t cb_audio_sample_batch(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

static void cb_input_poll(void)
{
}

static int16_t cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;

    switch (id)
    {
    case RETRO_DEVICE_ID_JOYPAD_B:      return (s_input_mask & sf2000_input::BTN_B) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_Y:      return (s_input_mask & sf2000_input::BTN_Y) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return (s_input_mask & sf2000_input::BTN_SELECT) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_START:  return (s_input_mask & sf2000_input::BTN_START) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_UP:     return (s_input_mask & sf2000_input::BTN_UP) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (s_input_mask & sf2000_input::BTN_DOWN) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (s_input_mask & sf2000_input::BTN_LEFT) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (s_input_mask & sf2000_input::BTN_RIGHT) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_A:      return (s_input_mask & sf2000_input::BTN_A) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_X:      return (s_input_mask & sf2000_input::BTN_X) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_L:      return (s_input_mask & sf2000_input::BTN_L) ? 1 : 0;
    case RETRO_DEVICE_ID_JOYPAD_R:      return (s_input_mask & sf2000_input::BTN_R) ? 1 : 0;
    default: return 0;
    }
}

#pragma pack(push, 1)
struct BMPHeader
{
    uint16_t bfType{0x4D42};
    uint32_t bfSize{0};
    uint16_t bfReserved1{0};
    uint16_t bfReserved2{0};
    uint32_t bfOffBits{54};
    uint32_t biSize{40};
    int32_t  biWidth{0};
    int32_t  biHeight{0};
    uint16_t biPlanes{1};
    uint16_t biBitCount{24};
    uint32_t biCompression{0};
    uint32_t biSizeImage{0};
    int32_t  biXPelsPerMeter{2835};
    int32_t  biYPelsPerMeter{2835};
    uint32_t biClrUsed{0};
    uint32_t biClrImportant{0};
};
#pragma pack(pop)

static bool saveScreenshotBMP(const char* path, const uint16_t* rgb565, int w, int h)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    int rowPitch = ((w * 3 + 3) & ~3);
    uint32_t dataSize = rowPitch * h;

    BMPHeader hdr;
    hdr.bfSize = sizeof(BMPHeader) + dataSize;
    hdr.biWidth = w;
    hdr.biHeight = h; // Bottom-up
    hdr.biSizeImage = dataSize;

    fwrite(&hdr, sizeof(hdr), 1, f);

    std::vector<uint8_t> row(rowPitch, 0);
    for (int y = h - 1; y >= 0; --y)
    {
        for (int x = 0; x < w; ++x)
        {
            uint16_t p = rgb565[y * w + x];
            uint8_t r = ((p >> 11) & 0x1F) << 3;
            uint8_t g = ((p >> 5)  & 0x3F) << 2;
            uint8_t b = (p         & 0x1F) << 3;
            // BMP uses BGR
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        fwrite(row.data(), 1, rowPitch, f);
    }
    fclose(f);
    printf("[PC TEST] Saved screenshot: %s\n", path);
    return true;
}

class TestPauseScreen : public PauseScreen {
public:
    TestPauseScreen() : PauseScreen(false) {}
    using PauseScreen::buttonClicked;
};

class TestOptionsScreen : public OptionsScreen {
public:
    using OptionsScreen::buttonClicked;
};

int main(int argc, char* argv[])
{
    printf("====================================================\n");
    printf("  MCPE 0.6.1 SF2000/GB300 Libretro Core PC Tester   \n");
    printf("====================================================\n");

    bool headless = false;
    int maxFrames = 350;
    bool autoWorld = true;
    bool testOptions = false;
    bool inputTest = false;
    bool storageTest = false;
    bool renderTest = false;
    bool perfTest = false;
    bool soakTest = false;
    bool explosionTest = false;
    int uiRes = -1;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--headless") == 0)
            headless = true;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            maxFrames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-world") == 0)
            autoWorld = false;
        else if (strcmp(argv[i], "--storage-test") == 0)
        {
            storageTest = true;
            autoWorld = false;
            maxFrames = 2;
        }
        else if (strcmp(argv[i], "--input-test") == 0)
        {
            inputTest = true;
            maxFrames = 260;
        }
        else if (strcmp(argv[i], "--ui-res") == 0 && i + 1 < argc)
        {
            // Diagnostic: open the OptionsScreen at a fixed 3D resolution and
            // report the GUI scale state, without touching the option itself.
            uiRes = atoi(argv[++i]);
            autoWorld = false;
            maxFrames = 60;
        }
        else if (strcmp(argv[i], "--explosion-test") == 0)
        {
            explosionTest = true;
            maxFrames = 260;
        }
        else if (strcmp(argv[i], "--soak-test") == 0)
        {
            soakTest = true;
            maxFrames = 4000;
        }
        else if (strcmp(argv[i], "--perf-test") == 0)
        {
            perfTest = true;
            maxFrames = 420;
        }
        else if (strcmp(argv[i], "--render-test") == 0)
        {
            renderTest = true;
            maxFrames = 200;
        }
        else if (strcmp(argv[i], "--test-options") == 0)
        {
            testOptions = true;
            autoWorld = false;
            maxFrames = 60;
        }
    }

#ifdef HAVE_SDL2
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    if (!headless)
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) == 0)
        {
            window = SDL_CreateWindow("MCPE 0.6.1 - SF2000 Core PC Test",
                                      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      960, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
            if (window)
            {
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
                if (renderer)
                {
                    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, 320, 240);
                }
            }
        }
        if (!window || !renderer || !texture)
        {
            printf("[PC TEST] SDL2 window creation failed or no display. Falling back to headless mode.\n");
            headless = true;
        }
    }
#else
    headless = true;
#endif

    printf("[PC TEST] Initializing Libretro callbacks...\n");
    retro_set_environment(cb_environment);
    retro_set_video_refresh(cb_video_refresh);
    retro_set_audio_sample(cb_audio_sample);
    retro_set_audio_sample_batch(cb_audio_sample_batch);
    retro_set_input_poll(cb_input_poll);
    retro_set_input_state(cb_input_state);

    printf("[PC TEST] Calling retro_init()...\n");
    retro_init();

    struct retro_game_info gameInfo = {};
    gameInfo.path = "mcpe";
    printf("[PC TEST] Calling retro_load_game()...\n");
    if (!retro_load_game(&gameInfo))
    {
        fprintf(stderr, "[PC TEST ERROR] retro_load_game failed!\n");
        return 1;
    }
    printf("[PC TEST] retro_load_game succeeded!\n");

    bool running = true;
    if (storageTest)
    {
        /*
         * Regression guard for the empty world list on GB300.
         *
         * The stock firmware exposes no rename()/unlink(), so the usual
         * "write level.dat_new then rename it to level.dat" commit silently
         * failed and the save stayed in level.dat_new. getLevelList() only looked
         * for level.dat and level.dat_old, so the list stayed empty and only
         * "Create" was offered.
         */
        printf("[STORAGE TEST] --- world list / level.dat commit ---\n");
        int storageFailures = 0;

        system("rm -rf /tmp/opencode/stor /tmp/opencode/stor_cache");

        // the storage source appends /games/com.mojang/minecraftWorlds itself
        ExternalFileLevelStorageSource src("/tmp/opencode/stor",
                                           "/tmp/opencode/stor_cache");

        const std::string worldDir =
            "/tmp/opencode/stor/games/com.mojang/minecraftWorlds/TestWorld";
        std::string mk = "mkdir -p '" + worldDir + "'";
        system(mk.c_str());

        LevelSettings settings(12345, 0);
        LevelData data(settings, "TestWorld");

        // 1) the real save path must commit level.dat
        ExternalFileLevelStorage storage(worldDir, worldDir);
        storage.saveLevelData(data, NULL);

        const std::string datFile = worldDir + "/level.dat";
        printf("[STORAGE TEST] level.dat committed by save: %s\n",
               exists(datFile.c_str()) ? "yes" : "no");
        if (!exists(datFile.c_str()))
        { fprintf(stderr, "[PC TEST ERROR] level.dat was not committed!\n"); storageFailures++; }

        LevelSummaryList found;
        src.getLevelList(found);
        printf("[STORAGE TEST] worlds listed after save: %d", (int)found.size());
        if (!found.empty()) printf(" ('%s')", found[0].name.c_str());
        printf("\n");
        if (found.size() != 1)
        { fprintf(stderr, "[PC TEST ERROR] expected 1 world, got %d!\n", (int)found.size()); storageFailures++; }
        else if (found[0].name != "TestWorld")
        { fprintf(stderr, "[PC TEST ERROR] wrong world name '%s'!\n", found[0].name.c_str()); storageFailures++; }

        /*
         * 2) Simulate the GB300 state: rename() and remove() both fail there, so
         * the freshly written level.dat_new is left behind and level.dat never
         * appears. Reproduce that here by moving level.dat to level.dat_new.
         */
        std::string sim = "cp '" + datFile + "' '" + worldDir + "/level.dat_new' && rm -f '" + datFile + "'";
        if (system(sim.c_str()) != 0)
        { fprintf(stderr, "[PC TEST ERROR] could not simulate the device state!\n"); storageFailures++; }
        printf("[STORAGE TEST] simulated device state: level.dat=%s level.dat_new=%s\n",
               exists(datFile.c_str()) ? "present" : "absent",
               exists((worldDir + "/level.dat_new").c_str()) ? "present" : "absent");

        LevelSummaryList recovered;
        src.getLevelList(recovered);
        printf("[STORAGE TEST] worlds recovered from level.dat_new only: %d\n",
               (int)recovered.size());
        if (recovered.size() != 1)
        {
            fprintf(stderr, "[PC TEST ERROR] a world saved as level.dat_new must stay visible!\n");
            storageFailures++;
        }
        else
        {
            printf("[STORAGE TEST] recovered world name: '%s'\n", recovered[0].name.c_str());
            if (recovered[0].name != "TestWorld")
            { fprintf(stderr, "[PC TEST ERROR] wrong recovered name '%s'!\n",
                        recovered[0].name.c_str()); storageFailures++; }
        }

        /*
         * 3) Options must land in an explicit directory. They used to be written
         * to a bare relative "options.txt", which on GB300 (getcwd() stubbed to
         * NULL) meant nothing was ever persisted.
         */
        {
            system("rm -rf /tmp/opencode/optdir && mkdir -p /tmp/opencode/optdir");

            StringVector out;
            out.push_back("gfx_renderdistance:1");
            out.push_back("gfx_blockresolution:3");
            out.push_back("game_autojump:0");

            OptionsFile f("/tmp/opencode/optdir");
            printf("[STORAGE TEST] options path: %s\n", f.getPath().c_str());
            if (f.getPath() != "/tmp/opencode/optdir/options.txt")
            { fprintf(stderr, "[PC TEST ERROR] wrong options path '%s'!\n", f.getPath().c_str()); storageFailures++; }

            f.save(out);
            if (!exists("/tmp/opencode/optdir/options.txt"))
            { fprintf(stderr, "[PC TEST ERROR] options.txt was not created in the given directory!\n"); storageFailures++; }

            // odczyt przez świeży obiekt, tak jak po restarcie
            OptionsFile f2("/tmp/opencode/optdir");
            StringVector in = f2.getOptionStrings();
            printf("[STORAGE TEST] entries read back: %d\n", (int)in.size());
            bool sawRenderDist = false;
            for (size_t i = 0; i + 1 < in.size(); i += 2)
            {
                if (in[i] == "gfx_renderdistance" && in[i + 1] == "1")
                    sawRenderDist = true;
            }
            printf("[STORAGE TEST] gfx_renderdistance round-trip: %s\n", sawRenderDist ? "ok" : "FAILED");
            if (!sawRenderDist)
            { fprintf(stderr, "[PC TEST ERROR] render distance did not round-trip!\n"); storageFailures++; }
        }

        /*
         * 4) Creating several worlds must produce several distinct directories.
         * With an empty level id every world resolved to minecraftWorlds/ itself,
         * so "Create" just reloaded the world made first.
         */
        {
            LevelSummaryList before;
            src.getLevelList(before);
            const int beforeCount = (int)before.size();
            const char* kNames[] = { "World", "World-", "World--", "World" };
            for (int i = 0; i < 4; ++i)
            {
                const std::string id = src.makeUniqueLevelId(kNames[i]);
                printf("[STORAGE TEST] create '%s' -> levelId '%s'\n", kNames[i], id.c_str());
                if (id.empty())
                { fprintf(stderr, "[PC TEST ERROR] levelId must never be empty!\n"); storageFailures++; }
                if (i > 0 && id == kNames[i - 1])
                { fprintf(stderr, "[PC TEST ERROR] levelId '%s' collides with the previous one!\n",
                          id.c_str()); storageFailures++; }

                const std::string dir =
                    "/tmp/opencode/stor/games/com.mojang/minecraftWorlds/" + id;
                std::string mk = "mkdir -p '" + dir + "'";
                system(mk.c_str());
                LevelData d(settings, id);
                ExternalFileLevelStorage st(dir, dir);
                st.saveLevelData(d, NULL);
            }

            LevelSummaryList all;
            src.getLevelList(all);
            printf("[STORAGE TEST] worlds listed after creating 4: %d (was %d)\n",
                   (int)all.size(), beforeCount);
            for (size_t i = 0; i < all.size(); ++i)
                printf("[STORAGE TEST]   - '%s'\n", all[i].id.c_str());
            if ((int)all.size() != beforeCount + 4)
            { fprintf(stderr, "[PC TEST ERROR] expected %d distinct worlds, got %d!\n",
                      beforeCount + 4, (int)all.size()); storageFailures++; }
        }

        if (storageFailures)
        { fprintf(stderr, "[PC TEST ERROR] %d storage test failure(s)!\n", storageFailures); return 1; }
        printf("[STORAGE TEST] OK: level.dat committed, level.dat_new visible, options persist, worlds unique\n");
        fflush(stdout);
        printf("[PC TEST SUCCESS] Storage test finished.\n");
        return 0;
    }

    for (s_current_frame = 0; s_current_frame < maxFrames && running; ++s_current_frame)
    {
        // ------------------------------------------------------------------
        // Input handling / Automation Script
        // ------------------------------------------------------------------
        uint32_t mask = 0;

#ifdef HAVE_SDL2
        if (!headless)
        {
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_QUIT)
                    running = false;
            }

            const uint8_t *keystate = SDL_GetKeyboardState(NULL);
            if (keystate[SDL_SCANCODE_ESCAPE]) running = false;
            if (keystate[SDL_SCANCODE_W])      mask |= sf2000_input::BTN_UP;
            if (keystate[SDL_SCANCODE_S])      mask |= sf2000_input::BTN_DOWN;
            if (keystate[SDL_SCANCODE_A])      mask |= sf2000_input::BTN_LEFT;
            if (keystate[SDL_SCANCODE_D])      mask |= sf2000_input::BTN_RIGHT;
            if (keystate[SDL_SCANCODE_UP])     mask |= sf2000_input::BTN_X;
            if (keystate[SDL_SCANCODE_DOWN])   mask |= sf2000_input::BTN_B;
            if (keystate[SDL_SCANCODE_LEFT])   mask |= sf2000_input::BTN_Y;
            if (keystate[SDL_SCANCODE_RIGHT])  mask |= sf2000_input::BTN_A;
            if (keystate[SDL_SCANCODE_SPACE])  mask |= sf2000_input::BTN_SELECT; // Jump
            if (keystate[SDL_SCANCODE_RETURN]) mask |= sf2000_input::BTN_START;
            if (keystate[SDL_SCANCODE_J])      mask |= sf2000_input::BTN_L;
            if (keystate[SDL_SCANCODE_K])      mask |= sf2000_input::BTN_R;
        }
#endif

        // Automated sequence to verify Menu and World Generation:
        if (autoWorld)
        {
            NinecraftApp* app = sf2000_get_app();
            if (app && app->screen == nullptr && app->player != nullptr)
            {
                // In 3D game world!
                if (s_current_frame % 30 == 0)
                {
                    printf("[GAMEPLAY FRAME %3d] In-game! Player pos: (%.1f, %.1f, %.1f) rotY: %.1f\n",
                           s_current_frame, app->player->x, app->player->y, app->player->z, app->player->yRot);
                }

                /*
                 * Explosion test. Reported symptom: the game freezes when a creeper
                 * explodes.
                 *
                 * Reproduced through the same call Creeper::tick() makes:
                 *     level->explode(this, x, y, z, 2.4f)
                 *
                 * There is no ClientLevel in this codebase; Minecraft creates a
                 * single ServerLevel, so isClientSide is false and the explosion is
                 * not short-circuited. Whatever it does here is what the console
                 * does.
                 *
                 * Times the blast, counts the blocks it removed, then keeps
                 * ticking to prove the game still runs afterwards.
                 */
                if (explosionTest && s_current_frame == 152)
                {
                    LocalPlayer* lp = app->player;
                    Level* lv = app->level;

                    const int bx = (int)lp->x;
                    const int by = (int)lp->y;
                    const int bz = (int)lp->z;

                    // A solid block to blow apart, so there is something to remove.
                    int placed = 0;
                    for (int dx = -3; dx <= 3; ++dx)
                        for (int dy = -2; dy <= 2; ++dy)
                            for (int dz = -3; dz <= 3; ++dz)
                            {
                                lv->setTileAndData(bx + dx, by + dy, bz + dz,
                                                    (int)Tile::rock->id, 0);
                                placed++;
                            }
                    lv->setTileAndData(bx, by - 1, bz, 0, 0);   // air at the centre

                    int solidBefore = 0;
                    for (int dx = -4; dx <= 4; ++dx)
                        for (int dy = -3; dy <= 3; ++dy)
                            for (int dz = -4; dz <= 4; ++dz)
                                if (lv->getTile(bx + dx, by + dy, bz + dz) > 0)
                                    solidBefore++;

                    printf("[EXPL] placed %d stone block(s), %d solid in range, r=2.4\n",
                           placed, solidBefore);

                    for (int attempt = 1; attempt <= 3; ++attempt)
                    {
                        // refill, then blow it up again
                        for (int dx = -3; dx <= 3; ++dx)
                            for (int dy = -2; dy <= 2; ++dy)
                                for (int dz = -3; dz <= 3; ++dz)
                                {
                                    if (dx == 0 && dy == -1 && dz == 0) continue;
                                    lv->setTileAndData(bx + dx, by + dy, bz + dz,
                                                        (int)Tile::rock->id, 0);
                                }

                        int solidNow = 0;
                        for (int dx = -4; dx <= 4; ++dx)
                            for (int dy = -3; dy <= 3; ++dy)
                                for (int dz = -4; dz <= 4; ++dz)
                                    if (lv->getTile(bx + dx, by + dy, bz + dz) > 0)
                                        solidNow++;

                        const std::chrono::steady_clock::time_point t0 =
                            std::chrono::steady_clock::now();
                        lv->explode(NULL, (float)bx + 0.5f, (float)by - 0.5f,
                                    (float)bz + 0.5f, 2.4f);
                        const std::chrono::steady_clock::time_point t1 =
                            std::chrono::steady_clock::now();
                        const double ms =
                            (double)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;

                        int solidAfter = 0;
                        for (int dx = -4; dx <= 4; ++dx)
                            for (int dy = -3; dy <= 3; ++dy)
                                for (int dz = -4; dz <= 4; ++dz)
                                    if (lv->getTile(bx + dx, by + dy, bz + dz) > 0)
                                        solidAfter++;

                        printf("[EXPL] attempt %d: explode() took %.1f ms, solid %d -> %d (%d removed)\n",
                               attempt, ms, solidNow, solidAfter, solidNow - solidAfter);
                        fflush(stdout);

                        if (ms > 2000.0)
                        { fprintf(stderr, "[PC TEST ERROR] a single explosion took %.1f ms: this is the freeze!\n", ms); break; }
                    }

                    /*
                     * The rest of what a creeper does: it hurts the player, and if
                     * that kills them LocalPlayer::die() runs, which drops the whole
                     * inventory as item entities. That path was not exercised above.
                     */
                    {
                        lv->setTileAndData(bx, by - 1, bz, 0, 0);
                        for (int dx = -3; dx <= 3; ++dx)
                            for (int dy = -2; dy <= 2; ++dy)
                                for (int dz = -3; dz <= 3; ++dz)
                                {
                                    if (dx == 0 && dy == -1 && dz == 0) continue;
                                    lv->setTileAndData(bx + dx, by + dy, bz + dz,
                                                        (int)Tile::rock->id, 0);
                                }

                        const size_t entsBefore = lv->entities.size();
                        const int hpBefore = lp->health;
                        printf("[EXPL] health=%d, entities=%d, particles=%d before the creeper\n",
                               hpBefore, (int)entsBefore, app->particleEngine->totalCount());

                        // right next to the player, exactly like a creeper that fused
                        Mob* cr = MobFactory::CreateMob(MobTypes::Creeper, lv);
                        if (cr == NULL)
                        { fprintf(stderr, "[PC TEST ERROR] could not create a creeper!\n"); }
                        else
                        {
                            cr->setPos(lp->x + 1.0f, lp->y, lp->z);
                            lv->addEntity(cr);

                            // let it run its fuse and detonate for real
                            const std::chrono::steady_clock::time_point t0 =
                                std::chrono::steady_clock::now();
                            for (int f = 0; f < 90; ++f)
                            {
                                lp->tick();
                                lv->tick();
                                cr->tick();
                            }
                            const std::chrono::steady_clock::time_point t1 =
                                std::chrono::steady_clock::now();
                            const double ms = (double)std::chrono::duration_cast<
                                std::chrono::microseconds>(t1 - t0).count() / 1000.0;

                            printf("[EXPL] 90 creeper ticks: %.2f ms/tick, health=%d, entities=%d, particles=%d\n",
                                   ms / 90.0, lp->health, (int)lv->entities.size(),
                                   app->particleEngine->totalCount());
                            fflush(stdout);

                            if (ms / 90.0 > 50.0)
                            { fprintf(stderr, "[PC TEST ERROR] a creeper tick costs %.2f ms: this is the freeze!\n",
                                      ms / 90.0); }

                            /*
                             * Particles are the one thing a blast floods the renderer
                             * with, and they are alpha-blended, so they cost fill.
                             * They must stay bounded.
                             */
                            const int pc = app->particleEngine->totalCount();
                            printf("[EXPL] particle count after the creeper: %d\n", pc);
                            if (pc > 4 * 200)
                            { fprintf(stderr, "[PC TEST ERROR] %d live particles after one creeper: the cap is not holding!\n", pc); }
                        }
                    }

                    // The game must keep running afterwards.
                    if (app->screen != NULL)
                    { fprintf(stderr, "[PC TEST ERROR] a screen opened on its own after the explosion: %s\n",
                              typeid(*app->screen).name()); }
                    printf("[EXPL] survived, particles=%d entities=%d\n",
                           app->particleEngine->totalCount(), (int)lv->entities.size());
                    fflush(stdout);
                }

                /*
                 * Soak test: the game froze after roughly ten minutes of play, so
                 * walk for thousands of frames while sampling the resident set
                 * size and the sizes of the collections that grow at runtime.
                 * A monotonic climb points at the leak; a flat line means the
                 * freeze is not a leak.
                 */
                if (soakTest && app->level && s_current_frame > 0 &&
                    (s_current_frame % 250 == 0))
                {
                    // /proc/self/statm reports pages. 4 kB is right on every
                    // platform this runs on, and unistd.h cannot be included here
                    // because it clashes with the socket layer's socklen_t.
                    const long kPageKb = 4;
                    long rssKb = 0;
                    FILE* st = fopen("/proc/self/statm", "r");
                    if (st)
                    {
                        long total = 0, resident = 0;
                        if (fscanf(st, "%ld %ld", &total, &resident) == 2)
                            rssKb = resident * kPageKb;
                        fclose(st);
                    }

                    int entities = 0;
                    for (size_t i = 0; i < app->level->entities.size(); ++i)
                        ++entities;

                    const int particles = app->particleEngine->totalCount();

                    printf("[SOAK] frame %4d rss=%ld kB entities=%d particles=%d chunks=%d dirty=%d\n",
                           s_current_frame, rssKb, entities, particles,
                           app->levelRenderer ? app->levelRenderer->debugChunkCount() : 0,
                           app->levelRenderer ? app->levelRenderer->debugDirtyChunkCount() : 0);
                    fflush(stdout);
                }

                /*
                 * Performance ablation. Measures the real cost of a frame under a
                 * few configurations instead of guessing where the time goes.
                 * Each configuration is given a few frames to settle (changing the
                 * render distance rebuilds the whole chunk grid) before sampling.
                 */
                if (perfTest && s_current_frame >= 200 && s_current_frame < 420 &&
                    s_current_frame % 20 == 0)
                {
                    NinecraftApp* a = sf2000_get_app();
                    if (a && a->screen == NULL && a->player != NULL)
                    {
                        const int slot = (s_current_frame - 200) / 20;
                        const float res[6]  = { 0.0f, 1.0f, 2.0f, 3.0f, 1.0f, 1.0f };
                        const int   dist[6] = { 6,    4,    2,    0,    6,    6    };
                        // Slots 4 and 5 differ only in GUI scale: 1.0x vs the 1.2x
                        // default. That isolates what the bigger UI actually costs.
                        const int   gui[6]  = { 0,    0,    0,    0,    1,    0    };
                        if (slot < 6)
                        {
                            if (s_cfgFrames > 0)
                            {
                                const double avg = s_lastCfgMs / (double)s_cfgFrames;
                                printf("[PERF] RESULT res=%d dist=%d gui=%d avg=%.1f ms (%.1f FPS) frames=%d\n",
                                       s_perfRes[s_perfSlot], s_perfDist[s_perfSlot],
                                       s_perfGui[s_perfSlot], avg, 1000.0 / avg, s_cfgFrames);
                                fflush(stdout);
                            }
                            const int prevRes = a->options.blockResolution;
                            const int prevDist = a->options.viewDistance;
                            a->options.blockResolution = (int)res[slot];
                            a->options.viewDistance    = dist[slot];
                            a->options.set(&Options::Option::GUI_SCALE, gui[slot]);
                            sf2000_sw::SoftwareRasterizer::instance().setBlockResolution((int)res[slot]);
                            s_lastCfgMs = 0.0;
                            s_cfgFrames = 0;
                            s_perfSlot = slot;
                            s_perfRes[slot]  = (int)res[slot];
                            s_perfDist[slot] = dist[slot];
                            s_perfGui[slot]  = gui[slot];
                            printf("[PERF] config %d: blockResolution=%d viewDistance=%d guiScaleIdx=%d\n",
                                   slot, (int)res[slot], dist[slot], gui[slot]);
                            fflush(stdout);
                            (void)prevRes; (void)prevDist;
                        }
                    }
                }

                if (perfTest && s_current_frame > 205 && s_current_frame < 419)
                {
                    s_lastCfgMs += s_frameMs;
                    ++s_cfgFrames;
                }
                if (perfTest && s_current_frame == 419)
                {
                    if (s_cfgFrames > 0)
                    {
                        const double avg = s_lastCfgMs / (double)s_cfgFrames;
                        printf("[PERF] RESULT %d res=%d dist=%d avg=%.1f ms (%.1f FPS) frames=%d\n",
                               s_perfSlot, s_perfRes[s_perfSlot], s_perfDist[s_perfSlot],
                               avg, 1000.0 / avg, s_cfgFrames);
                    }
                    s_lastCfgMs = 0.0;
                    s_cfgFrames = 0;
                    fflush(stdout);
                }

                /*
                 * Render distance: the chunk grid must follow the player. It is
                 * anchored at the world origin and re-centred by wrap-around in
                 * resortChunks(), so a grid that is too small degenerates into a
                 * single column and terrain stops updating as the player walks.
                 */
                if (renderTest && s_current_frame == 170)
                {
                    int failures = 0;
                    LocalPlayer* lp = app->player;
                    LevelRenderer* lr = app->levelRenderer;

                    for (int vd = 7; vd >= 0; --vd)
                    {
                        app->options.viewDistance = vd;
                        lr->allChanged();

                        const int home = lr->debugChunkCount();

                        // walk far away, exactly as a player would
                        const float fx = 1500.0f, fz = 1500.0f;
                        lp->x = lp->xOld = fx;
                        lp->z = lp->zOld = fz;
                        lr->render(lp, 0, 1.0f);

                        const bool covered = lr->debugChunkCovers((int)fx, (int)fz);
                        const int span = lr->debugChunkSpanXZ();
                        printf("[RENDER] vd=%d chunks=%d spanX=%d covers(1500,1500)=%s\n",
                               vd, home, span, covered ? "yes" : "NO");
                        if (!covered)
                        { fprintf(stderr, "[PC TEST ERROR] vd=%d chunk grid does not follow the player!\n", vd); failures++; }
                        /*
                         * Levels 0..6 must span at least two chunks; a single
                         * column means terrain stops updating as the player walks.
                         * Level 7 (Shortest) is deliberately one chunk: that is
                         * the whole point of it, and the grid still has to follow
                         * the player, which the covers() check above verifies.
                         */
                        const int minSpan = (vd == 7) ? 0 : 16;
                        if (span < minSpan)
                        { fprintf(stderr, "[PC TEST ERROR] vd=%d chunk span is only %d blocks, expected >= %d!\n",
                                  vd, span, minSpan); failures++; }
                    }

                    app->options.viewDistance = 3;
                    lr->allChanged();
                    if (failures)
                    { fprintf(stderr, "[PC TEST ERROR] %d render test failure(s)!\n", failures); exit(1); }
                    printf("[RENDER] OK: chunk grid follows the player at every distance\n");
                    fflush(stdout);
                }

                if (inputTest && s_current_frame == 150)
                {
                    int failures = 0;
                    LocalPlayer* lp = app->player;

                    /*
                     * Verify movement through the real chain:
                     *   pad -> poll() -> Keyboard codes -> player->setKey()
                     *       -> KeyboardInput::tick() -> xa / ya
                     * Asserting on raw key codes would miss the actual bug class:
                     * codes that never match the option bindings produce no
                     * movement at all while looking perfectly correct.
                     */
                    auto deliverKeys = [&]()
                    {
                        while (Keyboard::next()) {
                            const int key = Keyboard::getEventKey();
                            const bool down =
                                Keyboard::getEventKeyState() == KeyboardAction::KEYDOWN;
                            lp->setKey(key, down);
                        }
                        Keyboard::reset();
                        lp->input->tick(lp);
                    };

                    auto probe = [&](uint32_t mask, float* xa, float* ya, bool* jumping)
                    {
                        // clean baseline: no keys held, no pad buttons held
                        lp->input->releaseAllKeys();
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);
                        deliverKeys();

                        sf2000_input::setPadState(mask);
                        sf2000_input::poll(app);
                        deliverKeys();

                        *xa = lp->input->xa;
                        *ya = lp->input->ya;
                        *jumping = lp->input->jumping;
                    };

                    float xa, ya;
                    bool jumping;

                    printf("[INPUT TEST] --- D-Pad movement (checked via player->input xa/ya) ---\n");

                    probe(sf2000_input::BTN_UP, &xa, &ya, &jumping);
                    printf("[INPUT TEST] UP     -> xa=%.2f ya=%.2f\n", xa, ya);
                    if (ya <= 0.0f || xa != 0.0f)
                    { fprintf(stderr, "[PC TEST ERROR] UP must walk forward only (xa=%.2f ya=%.2f)!\n", xa, ya); failures++; }

                    probe(sf2000_input::BTN_DOWN, &xa, &ya, &jumping);
                    printf("[INPUT TEST] DOWN   -> xa=%.2f ya=%.2f\n", xa, ya);
                    if (ya >= 0.0f || xa != 0.0f)
                    { fprintf(stderr, "[PC TEST ERROR] DOWN must walk back only (xa=%.2f ya=%.2f)!\n", xa, ya); failures++; }

                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();

                    printf("[INPUT TEST] LEFT/RIGHT must not walk (inventory navigation)\n");
                    app->player->inventory->selectSlot(4);
                    probe(sf2000_input::BTN_LEFT, &xa, &ya, &jumping);
                    printf("[INPUT TEST] LEFT   -> xa=%.2f ya=%.2f slot=%d\n",
                           xa, ya, lp->inventory->selected);
                    if (xa != 0.0f || ya != 0.0f)
                    { fprintf(stderr, "[PC TEST ERROR] LEFT must NOT walk (xa=%.2f ya=%.2f)!\n", xa, ya); failures++; }
                    if (lp->inventory->selected != 3)
                    { fprintf(stderr, "[PC TEST ERROR] LEFT must select slot 3, got %d!\n",
                              lp->inventory->selected); failures++; }

                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();

                    app->player->inventory->selectSlot(4);
                    probe(sf2000_input::BTN_RIGHT, &xa, &ya, &jumping);
                    printf("[INPUT TEST] RIGHT  -> xa=%.2f ya=%.2f slot=%d\n",
                           xa, ya, lp->inventory->selected);
                    if (xa != 0.0f || ya != 0.0f)
                    { fprintf(stderr, "[PC TEST ERROR] RIGHT must NOT walk (xa=%.2f ya=%.2f)!\n", xa, ya); failures++; }
                    if (lp->inventory->selected != 5)
                    { fprintf(stderr, "[PC TEST ERROR] RIGHT must select slot 5, got %d!\n",
                              lp->inventory->selected); failures++; }

                    printf("[INPUT TEST] --- SELECT: inventory / manual jump ---\n");

                    // Auto Jump ON -> SELECT opens the inventory, no jump
                    app->options.autoJump = true;
                    probe(sf2000_input::BTN_SELECT, &xa, &ya, &jumping);
                    printf("[INPUT TEST] SELECT (autoJump=ON) -> screen=%s jumping=%d\n",
                           app->screen ? "opened" : "none", (int)jumping);
                    if (app->screen == nullptr)
                    { fprintf(stderr, "[PC TEST ERROR] SELECT must open the inventory when Auto Jump is on!\n"); failures++; }
                    if (jumping)
                    { fprintf(stderr, "[PC TEST ERROR] SELECT must not jump when Auto Jump is on!\n"); failures++; }
                    if (app->screen) app->setScreen(nullptr);
                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();

                    // Auto Jump OFF -> SELECT is the manual jump button
                    app->options.autoJump = false;
                    probe(sf2000_input::BTN_SELECT, &xa, &ya, &jumping);
                    printf("[INPUT TEST] SELECT (autoJump=OFF) -> jumping=%d\n", (int)jumping);
                    if (!jumping)
                    { fprintf(stderr, "[PC TEST ERROR] SELECT must jump when Auto Jump is off!\n"); failures++; }

                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();

                    /*
                     * Held-button check. At the real frame rate one poll() is one
                     * frame, so a tap shorter than a frame can never be observed;
                     * actions are level triggered so that holding for at least one
                     * frame always works. Verify the trigger fires exactly once
                     * while the button stays held.
                     */
                    app->options.autoJump = true;
                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();
                    app->setScreen(nullptr);

                    int opened = 0;
                    for (int i = 0; i < 5; ++i)
                    {
                        lp->input->releaseAllKeys();
                        sf2000_input::setPadState(sf2000_input::BTN_SELECT);
                        sf2000_input::poll(app);
                        deliverKeys();
                        if (app->screen) { opened++; app->setScreen(nullptr); }
                    }
                    printf("[INPUT TEST] SELECT held for 5 frames -> inventory opened %d time(s)\n", opened);
                    if (opened != 1)
                    { fprintf(stderr, "[PC TEST ERROR] held SELECT must open the inventory exactly once (got %d)!\n", opened); failures++; }

                    // a release must re-arm the trigger
                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();
                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(sf2000_input::BTN_SELECT);
                    sf2000_input::poll(app);
                    deliverKeys();
                    const int rearmed = (app->screen != nullptr) ? 1 : 0;
                    printf("[INPUT TEST] SELECT after release -> inventory opened: %d\n", rearmed);
                    if (rearmed != 1)
                    { fprintf(stderr, "[PC TEST ERROR] SELECT did not re-arm after release!\n"); failures++; }
                    app->setScreen(nullptr);
                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();

                    // option wiring
                    app->options.autoJump = false;
                    const bool before = app->options.getBooleanValue(&Options::Option::AUTO_JUMP);
                    app->options.toggle(&Options::Option::AUTO_JUMP, 1);
                    const bool after = app->options.getBooleanValue(&Options::Option::AUTO_JUMP);
                    printf("[INPUT TEST] AUTO_JUMP toggle: %d -> %d, player->autoJumpEnabled=%d\n",
                           (int)before, (int)after, (int)lp->autoJumpEnabled);
                    if (before == after)
                    { fprintf(stderr, "[PC TEST ERROR] Auto Jump toggle does nothing!\n"); failures++; }
                    if (lp->autoJumpEnabled != app->options.autoJump)
                    { fprintf(stderr, "[PC TEST ERROR] autoJumpEnabled not synced!\n"); failures++; }

                    // restore
                    app->options.autoJump = true;
                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();

                    /*
                     * Variant 1 inventory & screen switching tests:
                     * 1. SELECT opens IngameBlockSelectionScreen (Inventory / Blocks)
                     * 2. Inside, X switches to WorkbenchScreen (Crafting)
                     * 3. Inside, Y switches to ArmorScreen (Armor / Equipment)
                     * 4. Inside, SELECT switches back to IngameBlockSelectionScreen
                     * 5. B closes screen back to game
                     * 6. In-game chord: SELECT + X opens WorkbenchScreen directly
                     * 7. In-game chord: SELECT + Y opens ArmorScreen directly
                     */
                    app->setScreen(nullptr);
                    lp->input->releaseAllKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    deliverKeys();

                    // 1. SELECT -> IngameBlockSelectionScreen
                    sf2000_input::setPadState(sf2000_input::BTN_SELECT);
                    sf2000_input::poll(app);
                    deliverKeys();
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    printf("[INPUT TEST] SELECT opened screen: %s\n",
                           app->screen ? typeid(*app->screen).name() : "<none>");
                    IngameBlockSelectionScreen* blockScreen = dynamic_cast<IngameBlockSelectionScreen*>(app->screen);
                    if (!blockScreen)
                    { fprintf(stderr, "[PC TEST ERROR] SELECT must open IngameBlockSelectionScreen!\n"); failures++; }

                    // 2. Tab switch: X -> WorkbenchScreen
                    sf2000_input::setPadState(sf2000_input::BTN_X);
                    sf2000_input::poll(app);
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    printf("[INPUT TEST] X tab switched to: %s\n",
                           app->screen ? typeid(*app->screen).name() : "<none>");
                    WorkbenchScreen* wbScreen = dynamic_cast<WorkbenchScreen*>(app->screen);
                    if (!wbScreen)
                    { fprintf(stderr, "[PC TEST ERROR] X must switch to WorkbenchScreen!\n"); failures++; }

                    // 3. Tab switch: Y -> ArmorScreen
                    sf2000_input::setPadState(sf2000_input::BTN_Y);
                    sf2000_input::poll(app);
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    printf("[INPUT TEST] Y tab switched to: %s\n",
                           app->screen ? typeid(*app->screen).name() : "<none>");
                    ArmorScreen* armScreen = dynamic_cast<ArmorScreen*>(app->screen);
                    if (!armScreen)
                    { fprintf(stderr, "[PC TEST ERROR] Y must switch to ArmorScreen!\n"); failures++; }

                    // 4. Tab switch: SELECT -> IngameBlockSelectionScreen
                    sf2000_input::setPadState(sf2000_input::BTN_SELECT);
                    sf2000_input::poll(app);
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    printf("[INPUT TEST] SELECT tab switched to: %s\n",
                           app->screen ? typeid(*app->screen).name() : "<none>");
                    blockScreen = dynamic_cast<IngameBlockSelectionScreen*>(app->screen);
                    if (!blockScreen)
                    { fprintf(stderr, "[PC TEST ERROR] SELECT must switch back to IngameBlockSelectionScreen!\n"); failures++; }

                    /*
                     * The Crafting button on the inventory screen. It has to be
                     * reachable and it has to actually open the crafting screen.
                     * A stray click outside the slot grid used to set _pendingQuit,
                     * which skipped super::mouseClicked(), so the button never even
                     * saw the press.
                     */
                    {
                        app->setScreen(nullptr);
                        lp->input->releaseAllKeys();
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);
                        deliverKeys();

                        app->options.autoJump = true;
                        sf2000_input::setPadState(sf2000_input::BTN_SELECT);
                        sf2000_input::poll(app);
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);

                        IngameBlockSelectionScreen* inv =
                            dynamic_cast<IngameBlockSelectionScreen*>(app->screen);
                        if (!inv)
                        { fprintf(stderr, "[PC TEST ERROR] SELECT must open the inventory!\n"); failures++; }
                        else
                        {
                            // find the Crafting button and click its centre
                            const std::vector<Button*>& bs = inv->getButtons();
                            Button* craft = NULL;
                            Button* armor = NULL;
                            for (size_t i = 0; i < bs.size(); ++i)
                            {
                                if (bs[i]->msg == "Crafting") craft = bs[i];
                                if (bs[i]->msg == "Armor")    armor = bs[i];
                            }
                            printf("[INPUT TEST] inventory buttons: Crafting=%s Armor=%s"
                                   " creative=%d total=%d\n",
                                   craft ? "yes" : "NO", armor ? "yes" : "NO",
                                   (int)app->isCreativeMode(), (int)bs.size());
                            if (!craft)
                            { fprintf(stderr, "[PC TEST ERROR] Crafting button is missing!\n"); failures++; }
                            else if (craft->y >= armor->y)
                            { fprintf(stderr, "[PC TEST ERROR] Crafting must sit above Armor!\n"); failures++; }
                            else
                            {
                                // tap it through the real input path
                                const float iv = Gui::InvGuiScale;
                                const short px = (short)((craft->x + craft->width / 2) / iv);
                                const short py = (short)((craft->y + craft->height / 2) / iv);
                                Mouse::reset();
                                Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_DOWN, px, py);
                                Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_UP,   px, py);
                                if (app->screen) app->screen->updateEvents();

                                printf("[INPUT TEST] Crafting button -> %s\n",
                                       app->screen ? typeid(*app->screen).name() : "<none>");
                                WorkbenchScreen* viaButton = dynamic_cast<WorkbenchScreen*>(app->screen);
                                if (!viaButton)
                                { fprintf(stderr, "[PC TEST ERROR] the Crafting button does nothing!\n"); failures++; }
                            }
                            app->setScreen(nullptr);
                        }
                    }


                    /*
                     * Navigation mode: the inventory and the options want a
                     * pointer, because you browse a layout and want to point at a
                     * specific slot or slider. The grids and the world carousel
                     * want the arrows instead.
                     *
                     * Only screens whose destructors are safe before init() are
                     * probed here; the crafting and world-list modes are asserted
                     * from their live instances further down.
                     */
                    {
                        int failures = 0;
                        {
                            IngameBlockSelectionScreen* probe = new IngameBlockSelectionScreen();
                            const int got = probe->getNavMode();
                            printf("[NAVMODE] IngameBlockSelectionScreen = %s\n",
                                   got == GUI_NAV_CURSOR ? "cursor" : "focus");
                            if (got != GUI_NAV_CURSOR)
                            { fprintf(stderr, "[PC TEST ERROR] the inventory should use the cursor!\n"); failures++; }
                            delete probe;
                        }
                        {
                            OptionsScreen* probe = new OptionsScreen();
                            const int got = probe->getNavMode();
                            printf("[NAVMODE] OptionsScreen = %s\n",
                                   got == GUI_NAV_CURSOR ? "cursor" : "focus");
                            if (got != GUI_NAV_CURSOR)
                            { fprintf(stderr, "[PC TEST ERROR] the options should use the cursor!\n"); failures++; }
                            delete probe;
                        }
                        if (failures)
                        { fprintf(stderr, "[PC TEST ERROR] %d nav mode failure(s)!\n", failures); exit(1); }
                    }

                    /*
                     * Crafting: filtered list plus arrow navigation and scrolling.
                     *
                     * The pane must only offer recipes whose ingredients are in
                     * the inventory, and the arrows must be able to walk the whole
                     * list, scrolling the pane for the rows below the fold.
                     */
                    {
                        int failures = 0;
                        app->setScreen(nullptr);
                        lp->input->releaseAllKeys();
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);
                        deliverKeys();

                        Inventory* inv = lp->inventory;

                        // --- empty inventory: nothing should be craftable --------
                        const int savedSlots = inv->MAX_SELECTION_SIZE;
                        for (int i = savedSlots; i < inv->getContainerSize(); ++i)
                            inv->clearSlot(i);

                        lp->startCrafting((int)lp->x, (int)lp->y, (int)lp->z, Recipe::SIZE_3X3);
                        PaneCraftingScreen* cs = dynamic_cast<PaneCraftingScreen*>(app->screen);
                        if (!cs)
                        { fprintf(stderr, "[PC TEST ERROR] crafting screen did not open!\n"); failures++; }
                        else
                        {
                            const int craftable = cs->craftableRecipeCount();
                            const int offered  = cs->visibleRecipeCount();
                            printf("[CRAFT] empty inventory: %d recipe(s) craftable, %d offered\n",
                                   craftable, offered);
                            if (craftable != 0)
                            { fprintf(stderr, "[PC TEST ERROR] nothing should be craftable with an empty inventory!\n"); failures++; }
                            if (offered != 0)
                            { fprintf(stderr, "[PC TEST ERROR] no recipe may be offered with an empty inventory, got %d!\n", offered); failures++; }
                        }

                        // --- stock the inventory: the list must fill up ----------
                        /*
                         * Common ingredients rather than one specific item: this
                         * checks the filtering relationship, not a particular
                         * recipe table. Blocks are items too, via Item::tile with
                         * the tile id in the aux value.
                         */
                        {
                            struct Stock { const Item* item; const Tile* tile; };
                            const Stock stock[] = {
                                { Item::stick, NULL },  { Item::coal, NULL },
                                { Item::apple, NULL }, { Item::ironIngot, NULL },
                                { Item::string, NULL }, { Item::feather, NULL },
                                { Item::flintAndSteel, NULL },
                                { NULL, Tile::dirt },  { NULL, Tile::sand },
                                { NULL, Tile::cloth }, { NULL, Tile::grass },
                                { NULL, Tile::rock },  { NULL, Tile::wood },
                                { NULL, Tile::glass }, { NULL, Tile::bookshelf },
                                { NULL, Tile::stairs_brick },
                            };
                            const int nStock = (int)(sizeof(stock) / sizeof(stock[0]));
                            int slot = savedSlots;
                            for (int i = 0; i < nStock && slot < inv->getContainerSize() - 1; ++i)
                            {
                                ItemInstance* inst = stock[i].item
                                    ? new ItemInstance(stock[i].item, 64)
                                    : new ItemInstance(stock[i].tile, 64);
                                if (inst && inst->getItem() != NULL)
                                {
                                    inv->setItem(slot, inst);
                                    ++slot;
                                }
                                else
                                {
                                    delete inst;   // unavailable in this build
                                }
                            }
                            printf("[CRAFT] stocked %d ingredient slot(s)\n", slot - savedSlots);
                        }

                        app->setScreen(nullptr);
                        lp->startCrafting((int)lp->x, (int)lp->y, (int)lp->z, Recipe::SIZE_3X3);
                        cs = dynamic_cast<PaneCraftingScreen*>(app->screen);
                        if (cs)
                        {
                            const int craftable = cs->craftableRecipeCount();
                            const int offered  = cs->visibleRecipeCount();
                            printf("[CRAFT] stocked: %d recipe(s) craftable, %d offered\n",
                                   craftable, offered);
                            if (craftable <= 0)
                            { fprintf(stderr, "[PC TEST ERROR] a stocked inventory should unlock some recipes!\n"); failures++; }
                            if (offered <= 0)
                            { fprintf(stderr, "[PC TEST ERROR] a stocked inventory should offer recipes!\n"); failures++; }
                            if (offered > craftable)
                            { fprintf(stderr, "[PC TEST ERROR] %d recipes offered but only %d craftable: unbuildable ones leaked!\n",
                                      offered, craftable); failures++; }

                            // --- arrows walk the list and scroll the pane ---------
                            /*
                             * The property that matters: every offered recipe must be
                             * reachable. Walk down until the selection stops moving
                             * and check it ended on the last one. If the pane did not
                             * scroll, everything below the fold would be unreachable
                             * and the walk would stall early.
                             */
                            const int n = cs->visibleRecipeCount();
                            if (n > 0)
                            {
                                int outOfRange = 0, moves = 0;
                                int last = -1;
                                int prevRecipe = -2;
                                for (int i = 0; i < n * 4 + 8; ++i)
                                {
                                    sf2000_input::setPadState(0);
                                    sf2000_input::poll(app);
                                    sf2000_input::setPadState(sf2000_input::BTN_DOWN);
                                    sf2000_input::poll(app);
                                    sf2000_input::setPadState(0);
                                    sf2000_input::poll(app);
                                    cs->refreshFocus();

                                    const int fi = cs->getFocusIndex();
                                    if (fi < 0 || fi >= (int)cs->getFocusTargets().size())
                                        outOfRange++;

                                    const int now = cs->focusRecipeIndex();
                                    if (now != prevRecipe) moves++;
                                    prevRecipe = now;
                                    last = now;
                                }
                                printf("[CRAFT] %d recipes offered: walked down %d time(s), %d selection change(s), ended on %d\n",
                                       n, n * 4 + 8, moves, last);
                                if (outOfRange)
                                { fprintf(stderr, "[PC TEST ERROR] arrows threw the focus out of range!\n"); failures++; }
                                if (moves < 2)
                                { fprintf(stderr, "[PC TEST ERROR] the arrows barely move the recipe selection (%d changes)!\n", moves); failures++; }
                                if (last != n - 1)
                                { fprintf(stderr, "[PC TEST ERROR] walking down ended on recipe %d of %d: the pane does not scroll!\n",
                                          last, n); failures++; }
                            }
                        }

                        app->setScreen(nullptr);
                        if (failures)
                        { fprintf(stderr, "[PC TEST ERROR] %d crafting failure(s)!\n", failures); exit(1); }
                        printf("[CRAFT] OK: only craftable recipes are listed, and the arrows scroll it\n");
                    }

                    /*
                     * The world carousel must be reachable with the pad. It is a
                     * focus stop of its own; Left/Right steps between worlds and A
                     * loads the one in the middle. It used to be a focus target not
                     * at all, because the button that activates it was only in
                     * tabButtons, so the pad could only reach Delete/Create/Back.
                     *
                     * This runs last of the menu checks because A here starts a real
                     * level load, which leaves the app mid-transition.
                     */
                    {
                        int failures = 0;

                        LevelSettings settings(999, 0);
                        const char* ids[3] = { "padtest-a", "padtest-b", "padtest-c" };
                        for (int i = 0; i < 3; ++i)
                        {
                            const std::string dir =
                                std::string("/tmp/opencode/stor/games/com.mojang/minecraftWorlds/") + ids[i];
                            std::string mk = "mkdir -p '" + dir + "'";
                            if (system(mk.c_str()) != 0) { /* ignore */ }
                            LevelData d(settings, ids[i]);
                            ExternalFileLevelStorage st(dir, dir);
                            st.saveLevelData(d, NULL);
                        }

                        app->setScreen(new SelectWorldScreen());
                        SelectWorldScreen* sw = dynamic_cast<SelectWorldScreen*>(app->screen);
                        if (!sw)
                        { fprintf(stderr, "[PC TEST ERROR] could not open the world list!\n"); failures++; }
                        else
                        {
                            sw->refreshFocus();
                            const std::vector<FocusTarget>& ft = sw->getFocusTargets();
                            int carousel = -1;
                            for (size_t i = 0; i < ft.size(); ++i)
                                if (ft[i].id == kWorldCarouselFocus) carousel = (int)i;
                            printf("[WORLDS] navMode=%s, %d focus target(s), carousel is %d\n",
                                   sw->getNavMode() == GUI_NAV_CURSOR ? "cursor" : "focus",
                                   (int)ft.size(), carousel);
                            if (sw->getNavMode() != GUI_NAV_FOCUS)
                            { fprintf(stderr, "[PC TEST ERROR] the world list should use the arrows!\n"); failures++; }
                            if (carousel < 0)
                            { fprintf(stderr, "[PC TEST ERROR] the world carousel is not a focus target!\n"); failures++; }
                            else
                            {
                                // Walk to it from the button row; the buttons sit below
                                // the carousel, so Up is the way there.
                                int steps = 0;
                                while (sw->getFocusIndex() != carousel && steps < 12)
                                {
                                    const int before = sw->getFocusIndex();
                                    sw->setPadFocusStep(0, -1);
                                    if (sw->getFocusIndex() == before) break;
                                    ++steps;
                                }
                                printf("[WORLDS] walked to the carousel in %d press(es)\n", steps);
                                if (sw->getFocusIndex() != carousel)
                                { fprintf(stderr, "[PC TEST ERROR] cannot reach the carousel with Up!\n"); failures++; }
                                else
                                {
                                    const int first = sw->focusWorldIndex();
                                    printf("[WORLDS] first focused world: %d\n", first);

                                    sw->setPadFocusStep(1, 0);
                                    const int second = sw->focusWorldIndex();
                                    printf("[WORLDS] after Right: world %d -> %d\n", first, second);
                                    if (second == first)
                                    { fprintf(stderr, "[PC TEST ERROR] Right does not step to the next world!\n"); failures++; }

                                    sw->setPadFocusStep(1, 0);
                                    const int third = sw->focusWorldIndex();
                                    printf("[WORLDS] after Right: world %d -> %d\n", second, third);
                                    if (third != second + 1)
                                    { fprintf(stderr, "[PC TEST ERROR] worlds do not advance one at a time!\n"); failures++; }

                                    sw->setPadFocusStep(-1, 0);
                                    printf("[WORLDS] after Left: world %d\n", sw->focusWorldIndex());
                                    if (sw->focusWorldIndex() != second)
                                    { fprintf(stderr, "[PC TEST ERROR] Left does not step back!\n"); failures++; }

                                    // A on the carousel loads: it opens the ProgressScreen
                                    sf2000_input::setPadState(sf2000_input::BTN_A);
                                    sf2000_input::poll(app);
                                    sf2000_input::setPadState(0);
                                    sf2000_input::poll(app);
                                    if (app->screen) app->screen->updateEvents();
                                    // The click only sets hasPickedLevel; the load is
                                    // started from tick() on the following frame.
                                    sw->tick();
                                    printf("[WORLDS] after A: screen=%s\n",
                                           app->screen ? typeid(*app->screen).name() : "<none>");
                                    if (app->screen == sw)
                                    { fprintf(stderr, "[PC TEST ERROR] A on the carousel does not load the world!\n"); failures++; }
                                }
                            }
                        }
                        if (failures)
                        { fprintf(stderr, "[PC TEST ERROR] %d world list failure(s)!\n", failures); exit(1); }
                        printf("[WORLDS] OK: worlds are reachable with the pad\n");
                        fflush(stdout);
                    }

                    // 5. B -> Close back to game
                    sf2000_input::setPadState(sf2000_input::BTN_B);
                    sf2000_input::poll(app);
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    printf("[INPUT TEST] B closed screen: %s\n",
                           app->screen ? typeid(*app->screen).name() : "<none>");
                    if (app->screen != nullptr)
                    { fprintf(stderr, "[PC TEST ERROR] B must close the screen!\n"); failures++; }

                    // 6. In-game chord: SELECT + X -> WorkbenchScreen directly
                    sf2000_input::setPadState(sf2000_input::BTN_SELECT | sf2000_input::BTN_X);
                    sf2000_input::poll(app);
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    printf("[INPUT TEST] SELECT+X chord opened: %s (creative=%d)\n",
                           app->screen ? typeid(*app->screen).name() : "<none>",
                           (int)app->isCreativeMode());
                    wbScreen = dynamic_cast<WorkbenchScreen*>(app->screen);
                    if (!wbScreen)
                    {
                        /*
                         * LocalPlayer::startCrafting refuses in creative mode, and
                         * that is correct: with an infinite inventory there is
                         * nothing to craft. The chord then falls back to the
                         * inventory. This test world is created in creative, so
                         * either result is correct here.
                         */
                        const bool fellBackToInventory =
                            dynamic_cast<IngameBlockSelectionScreen*>(app->screen) != NULL;
                        if (!app->isCreativeMode() || !fellBackToInventory)
                        { fprintf(stderr, "[PC TEST ERROR] SELECT+X must open WorkbenchScreen (or the inventory in creative)!\n"); failures++; }
                    }
                    app->setScreen(nullptr);

                    // 7. In-game chord: SELECT + Y -> ArmorScreen directly
                    sf2000_input::setPadState(sf2000_input::BTN_SELECT | sf2000_input::BTN_Y);
                    sf2000_input::poll(app);
                    sf2000_input::setPadState(0);
                    sf2000_input::poll(app);
                    printf("[INPUT TEST] SELECT+Y chord opened: %s\n",
                           app->screen ? typeid(*app->screen).name() : "<none>");
                    armScreen = dynamic_cast<ArmorScreen*>(app->screen);
                    if (!armScreen)
                    { fprintf(stderr, "[PC TEST ERROR] SELECT+Y must open ArmorScreen directly!\n", 0); failures++; }

                    /*
                     * Leave the WorkbenchScreen open so the remaining gameplay frames
                     * exercise rendering the in-game screen without crashing.
                     */
                    app->setScreen(nullptr);
                    lp->startCrafting((int)lp->x, (int)lp->y, (int)lp->z, Recipe::SIZE_2X2);

                    if (failures)
                    { fprintf(stderr, "[PC TEST ERROR] %d input test failure(s)!\n", failures); exit(1); }
                    printf("[INPUT TEST] OK: movement, inventory nav and auto jump verified\n");
                    fflush(stdout);
                }

                // Test Pause Menu: Press START at frame 220
                if (s_current_frame == 220 || s_current_frame == 221)
                {
                    mask |= sf2000_input::BTN_START;
                }
                else
                {
                    // Walk forward
                    mask |= sf2000_input::BTN_UP;

                    // Rotate camera right for a bit, then left
                    if ((s_current_frame / 40) % 2 == 0)
                        mask |= sf2000_input::BTN_A; // Camera right
                    else
                        mask |= sf2000_input::BTN_Y; // Camera left

                    // Swing arm / attack occasionally
                    if (s_current_frame % 50 < 10)
                        mask |= sf2000_input::BTN_L;
                }
            }
            else
            {
                // In menu or loading screen or pause menu:
                if (s_current_frame == 224)
                {
                    PauseScreen* pause = dynamic_cast<PauseScreen*>(app->screen);
                    if (pause)
                    {
                        printf("[PC TEST] In-game PauseScreen verified! Clicking Options button...\n");
                        Button optBtn(5, 0, 0, 100, 20, "Options");
                        static_cast<TestPauseScreen*>(pause)->buttonClicked(&optBtn);
                    }
                }
                else if (s_current_frame == 228)
                {
                    OptionsScreen* inGameOpt = dynamic_cast<OptionsScreen*>(app->screen);
                    if (inGameOpt)
                    {
                        printf("[PC TEST] OptionsScreen opened from PauseScreen while in-game! Closing options...\n");
                        inGameOpt->closeOptions();
                        PauseScreen* backPause = dynamic_cast<PauseScreen*>(app->screen);
                        if (backPause)
                        {
                            printf("[PC TEST] Successfully returned to PauseScreen from OptionsScreen!\n");
                            Button contBtn(1, 0, 0, 100, 20, "Back to game");
                            static_cast<TestPauseScreen*>(backPause)->buttonClicked(&contBtn);
                            printf("[PC TEST] Resumed game from PauseScreen!\n");
                        }
                    }
                }
                // Frame 25: walk to "Start Game" and press it
                else if (s_current_frame == 25)
                {
                    if (!pressButtonByName(app, "Start Game"))
                        printf("[PC TEST] could not press 'Start Game'\n");
                }
                // Frame 55: pick the world / create a new one
                else if (s_current_frame == 55)
                {
                    if (!pressButtonByName(app, "Create new"))
                        printf("[PC TEST] could not press 'Create new'\n");
                }
                // Frame 75: confirm the new world
                else if (s_current_frame == 75)
                {
                    if (!pressButtonByName(app, "Create"))
                        printf("[PC TEST] could not press 'Create'\n");
                }
                // Keep moving: chunks stream, entities tick, particles spawn.
                else if (s_current_frame > 120 && soakTest)
                {
                    // change direction every 40 frames so the player crosses chunk
                    // borders in several directions instead of running off the map
                    if ((s_current_frame / 40) % 2 == 0) mask |= sf2000_input::BTN_UP;
                    else                               mask |= sf2000_input::BTN_LEFT;
                    if (s_current_frame % 137 == 0)   mask |= sf2000_input::BTN_Y;
                }
            }
        }

        if (testOptions || uiRes >= 0)
        {
            NinecraftApp* app = sf2000_get_app();
            if (app)
            {
                if (s_current_frame == 20)
                {
                    printf("[PC TEST] Navigating to OptionsScreen...\n");
                    app->setScreen(new OptionsScreen());
                }
                else if (uiRes >= 0 && s_current_frame == 22)
                {
                    // Pin the 3D resolution, then verify the GUI scale invariants.
                    app->options.blockResolution = uiRes;
                    sf2000_sw::SoftwareRasterizer::instance().setBlockResolution(uiRes);

                    OptionsScreen* os = (OptionsScreen*)app->screen;
                    if (os)
                    {
                        // 4 categories: Game, Controls, Graphics, Sound
                        for (int c = 0; c < 4; ++c)
                        {
                            printf("[UI DUMP] ===== category %d =====\n", c);
                            fflush(stdout);
                            os->debugDumpOptionWidgets(c);
                        }
                    }
                }
                else if (uiRes >= 0 && s_current_frame == 24)
                {
                    /*
                     * Regression guard, driven through the real input path
                     * (Mouse event queue + Screen::updateEvents) exactly like
                     * Input_SF2000 does for the D-Pad cursor and the A button.
                     *
                     * Bug being guarded: every left-button release anywhere on the
                     * screen rewrote every step slider from the cursor position,
                     * so tapping anything in the options screen could fling the UI
                     * to 400% (guiScale 3 -> InvGuiScale 0.25). No tap outside an
                     * actual control may change the GUI scale.
                     */
                    int offenders = 0;
                    const int kStep = 4;
                    for (int c = 0; c < 3; ++c)
                    {
                        int hits = 0;
                        for (int y = 2; y < 238; y += kStep)
                        {
                            for (int x = 2; x < 318; x += kStep)
                            {
                                OptionsScreen* cur = dynamic_cast<OptionsScreen*>(app->screen);
                                if (!cur)
                                {
                                    app->setScreen(new OptionsScreen());
                                    cur = (OptionsScreen*)app->screen;
                                    if (!cur) break;
                                }
                                cur->selectCategory(c);

                                /*
                                 * GUI_SCALE is a real control in the Graphics pane
                                 * now, so taps inside it are expected to change the
                                 * value. Everything outside it must not.
                                 */
                                const float invS = Gui::InvGuiScale;
                                const int lx = (int)(x * invS), ly = (int)(y * invS);
                                if (cur->debugIsPointOnOption(c, &Options::Option::GUI_SCALE, lx, ly))
                                    continue;

                                app->options.set(&Options::Option::GUI_SCALE, 0);
                                const int before = app->options.guiScale;

                                Mouse::reset();
                                Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_DOWN, (short)x, (short)y);
                                Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_UP,   (short)x, (short)y);

                                Screen* victim = app->screen;
                                victim->updateEvents();

                                if (app->options.guiScale != before)
                                {
                                    if (hits < 8)
                                        printf("[UI GUARD] category %d: tap (%3d,%3d) changed guiScale %d -> %d\n",
                                               c, x, y, before, app->options.guiScale);
                                    hits++;
                                }
                                if (app->screen != victim)
                                    app->setScreen(new OptionsScreen());

                                if (app->options.guiScale != 0)
                                    app->options.set(&Options::Option::GUI_SCALE, 0);
                            }
                        }
                        printf("[UI GUARD] category %d: %d taps changed the GUI scale\n", c, hits);
                        offenders += hits;
                    }

                    if (offenders != 0)
                    {
                        fprintf(stderr, "[PC TEST ERROR] %d stray taps changed the GUI scale!\n", offenders);
                        exit(1);
                    }
                    printf("[UI GUARD] OK: no stray tap changes the GUI scale\n");
                    fflush(stdout);

                    app->options.set(&Options::Option::GUI_SCALE, 0);
                    app->setScreen(new OptionsScreen());
                }
                else if (uiRes >= 0 && s_current_frame == 26)
                {
                    /*
                     * Positive check: sliders that belong to a control must still
                     * respond when tapped directly. Without this, the guard above
                     * could pass simply because every slider was dead.
                     */
                    /*
                     * Tap points come from the live control rectangles, not from
                     * hard-coded pixels: the logical space shrinks as the GUI scale
                     * grows, so baked coordinates would drift off the controls.
                     */
                    const float invPos = Gui::InvGuiScale;
                    int failures = 0;

                    // tap a point given in logical coords, converted to physical
                    auto tapLogical = [&](int lx, int ly) {
                        const short px = (short)(lx / invPos);
                        const short py = (short)(ly / invPos);
                        Mouse::reset();
                        Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_DOWN, px, py);
                        Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_UP,   px, py);
                    };

                    /*
                     * Tap a quarter of the way along an option's track.
                     *
                     * Not the right-hand end: RENDER_DISTANCE lists its steps
                     * descending, {6..0} = Nearest..Far, so the far right of that
                     * track is value 0. Tapping it while the value is already 0 is
                     * a no-op and would look like a dead control.
                     */
                    auto tapOption = [&](OptionsScreen* os, int cat, const Options::Option* opt) -> bool
                    {
                        int rx = 0, ry = 0, rw = 0, rh = 0;
                        if (!os->debugGetOptionRect(cat, opt, &rx, &ry, &rw, &rh)) return false;
                        const int lx = rx + rw / 4, ly = ry + rh / 2;
                        printf("[UI POS] cat%d %s rect=(%d,%d) %dx%d logical, tap logical (%d,%d) -> physical (%d,%d)\n",
                               cat, opt->getCaptionId().c_str(), rx, ry, rw, rh, lx, ly,
                               (int)(lx / invPos), (int)(ly / invPos));
                        tapLogical(lx, ly);
                        return true;
                    };

                    // tap the middle of an option's control (toggles, not tracks)
                    auto tapOptionCentre = [&](OptionsScreen* os, int cat, const Options::Option* opt) -> bool
                    {
                        int rx = 0, ry = 0, rw = 0, rh = 0;
                        if (!os->debugGetOptionRect(cat, opt, &rx, &ry, &rw, &rh)) return false;
                        tapLogical(rx + rw / 2, ry + rh / 2);
                        return true;
                    };

                    // DIFFICULTY slider, Game pane
                    {
                        OptionsScreen* os = dynamic_cast<OptionsScreen*>(app->screen);
                        if (!os) { app->setScreen(new OptionsScreen()); os = (OptionsScreen*)app->screen; }
                        os->selectCategory(0);
                        app->options.difficulty = 0;
                        if (!tapOption(os, 0, &Options::Option::DIFFICULTY))
                        { fprintf(stderr, "[PC TEST ERROR] difficulty control not found!\n"); failures++; }
                        Screen* v = app->screen; v->updateEvents();
                        printf("[UI POS] difficulty slider: 0 -> %d\n", app->options.difficulty);
                        if (app->options.difficulty == 0) { fprintf(stderr, "[PC TEST ERROR] difficulty slider is dead!\n"); failures++; }
                        if (app->screen != v) app->setScreen(new OptionsScreen());
                    }

                    // RENDER_DISTANCE + BLOCK_RESOLUTION sliders, Graphics pane
                    {
                        OptionsScreen* os = dynamic_cast<OptionsScreen*>(app->screen);
                        if (!os) { app->setScreen(new OptionsScreen()); os = (OptionsScreen*)app->screen; }
                        os->selectCategory(2);

                        app->options.viewDistance = 0;
                        if (!tapOption(os, 2, &Options::Option::RENDER_DISTANCE))
                        { fprintf(stderr, "[PC TEST ERROR] render distance control not found!\n"); failures++; }
                        Screen* v = app->screen; v->updateEvents();
                        printf("[UI POS] render distance slider: 0 -> %d\n", app->options.viewDistance);
                        if (app->options.viewDistance == 0) { fprintf(stderr, "[PC TEST ERROR] render distance slider is dead!\n"); failures++; }
                        if (app->screen != v) app->setScreen(new OptionsScreen());

                        os = dynamic_cast<OptionsScreen*>(app->screen);
                        if (os)
                        {
                            os->selectCategory(2);
                            app->options.blockResolution = 0;
                            if (!tapOption(os, 2, &Options::Option::BLOCK_RESOLUTION))
                            { fprintf(stderr, "[PC TEST ERROR] block resolution control not found!\n"); failures++; }
                            Screen* v2 = app->screen; v2->updateEvents();
                            printf("[UI POS] block resolution slider: 0 -> %d\n", app->options.blockResolution);
                            if (app->options.blockResolution == 0) { fprintf(stderr, "[PC TEST ERROR] block resolution slider is dead!\n"); failures++; }
                            if (app->screen != v2) app->setScreen(new OptionsScreen());
                        }
                    }

                    /*
                     * Sound options must live in their own category. They used to
                     * sit at the bottom of the Graphics pane, which is not where
                     * anyone looks for volume.
                     */
                    {
                        int failures = 0;
                        app->setScreen(new OptionsScreen());
                        OptionsScreen* os = (OptionsScreen*)app->screen;
                        if (!os)
                        { fprintf(stderr, "[PC TEST ERROR] could not open the options!\n"); failures++; }
                        else
                        {
                            int musicCat = -1, soundCat = -1;
                            int musicX = 0, musicY = 0, musicW = 0, musicH = 0;
                            int soundX = 0, soundY = 0, soundW = 0, soundH = 0;
                            for (int c = 0; c < 4; ++c)
                            {
                                if (os->debugGetOptionRect(c, &Options::Option::MUSIC,
                                                            &musicX, &musicY, &musicW, &musicH))
                                    musicCat = c;
                                if (os->debugGetOptionRect(c, &Options::Option::SOUND,
                                                            &soundX, &soundY, &soundW, &soundH))
                                    soundCat = c;
                            }
                            printf("[UI SOUND] music is in category %d, sound is in category %d\n",
                                   musicCat, soundCat);
                            if (musicCat != 3)
                            { fprintf(stderr, "[PC TEST ERROR] Music must be in the Sound category (3), got %d!\n", musicCat); failures++; }
                            if (soundCat != 3)
                            { fprintf(stderr, "[PC TEST ERROR] Sound must be in the Sound category (3), got %d!\n", soundCat); failures++; }

                            // and the dedicated category must actually respond
                            os->selectCategory(3);
                            os->refreshFocus();
                            printf("[UI SOUND] category 3 focus targets: %d\n",
                                   (int)os->getFocusTargets().size());
                            if (os->getFocusTargets().size() < 4)
                            { fprintf(stderr, "[PC TEST ERROR] the Sound category has too few controls!\n"); failures++; }

                            // the volume sliders must respond to a tap
                            os->selectCategory(3);
                            app->options.music = 0.0f;
                            if (!tapOption(os, 3, &Options::Option::MUSIC))
                            { fprintf(stderr, "[PC TEST ERROR] Music control not found in the Sound category!\n"); failures++; }
                            Screen* v = app->screen; v->updateEvents();
                            printf("[UI SOUND] music after tap: %.2f\n", app->options.music);
                            if (app->options.music <= 0.0f)
                            { fprintf(stderr, "[PC TEST ERROR] the Music slider does nothing!\n"); failures++; }
                            if (app->screen != v) app->setScreen(new OptionsScreen());
                        }
                        if (failures)
                        { fprintf(stderr, "[PC TEST ERROR] %d sound category failure(s)!\n", failures); exit(1); }
                        printf("[UI SOUND] OK: sound options have their own category\n");
                    }

                    // Auto Jump must be a reachable, working toggle in the Game pane
                    {
                        OptionsScreen* os = dynamic_cast<OptionsScreen*>(app->screen);
                        if (!os) { app->setScreen(new OptionsScreen()); os = (OptionsScreen*)app->screen; }
                        if (os)
                        {
                            os->selectCategory(0);
                            app->options.autoJump = false;
                            if (!tapOptionCentre(os, 0, &Options::Option::AUTO_JUMP))
                            { fprintf(stderr, "[PC TEST ERROR] Auto Jump control not found!\n"); failures++; }
                            Screen* v = app->screen; v->updateEvents();
                            printf("[UI POS] auto jump toggle tapped: false -> %d\n", (int)app->options.autoJump);
                            if (!app->options.autoJump)
                            { fprintf(stderr, "[PC TEST ERROR] Auto Jump toggle in Game pane is dead!\n"); failures++; }
                            if (app->screen != v) app->setScreen(new OptionsScreen());
                        }
                    }

                    // GRAPHICS must be a working toggle (it used to be built as a
                    // step slider with duplicate steps {0,0}, i.e. a dead control)
                    {
                        OptionsScreen* os = dynamic_cast<OptionsScreen*>(app->screen);
                        if (!os) { app->setScreen(new OptionsScreen()); os = (OptionsScreen*)app->screen; }
                        if (os)
                        {
                            os->selectCategory(2);
                            app->options.fancyGraphics = false;
                            app->options.toggle(&Options::Option::GRAPHICS, 1);
                            printf("[UI POS] graphics toggle: false -> %s\n",
                                   app->options.fancyGraphics ? "true" : "false");
                            if (!app->options.fancyGraphics)
                            { fprintf(stderr, "[PC TEST ERROR] graphics toggle does nothing!\n"); failures++; }
                        }
                    }

                    printf("[UI POS] OK: direct taps on options still work\n");
                    fflush(stdout);

                    app->options.set(&Options::Option::BLOCK_RESOLUTION, 1);

                    /*
                     * GUI scale. The panel is a fixed 320x240 and the GUI is drawn
                     * through an orthographic projection in logical units, so the
                     * scale is a straight element-size multiplier. Auto must give
                     * 1.2x, and the explicit steps must stay in a usable range.
                     */
                    {
                        app->options.guiScale = 0;
                        app->setSize(app->width, app->height);
                        printf("[UI SCALE] auto -> GuiScale=%.3f InvGuiScale=%.4f logical=%dx%d\n",
                               Gui::GuiScale, Gui::InvGuiScale,
                               (int)(app->width * Gui::InvGuiScale),
                               (int)(app->height * Gui::InvGuiScale));
                        if (std::abs(Gui::GuiScale - 1.2f) > 0.001f)
                        { fprintf(stderr, "[PC TEST ERROR] auto GUI scale must be 1.2x, got %.3f!\n",
                                  Gui::GuiScale); failures++; }
                        const int lw = (int)(app->width * Gui::InvGuiScale);
                        if (lw <= 0 || lw >= app->width)
                        { fprintf(stderr, "[PC TEST ERROR] 1.2x must shrink the logical viewport, got %dx%d!\n",
                                  lw, app->height); failures++; }

                        static const float kExpect[5] = { 1.20f, 1.00f, 1.15f, 1.30f, 1.50f };
                        for (int v = 0; v < 5; ++v)
                        {
                            app->options.set(&Options::Option::GUI_SCALE, v);
                            app->setSize(app->width, app->height);
                            printf("[UI SCALE] guiScale=%d -> %.2fx (logical %dx%d)\n",
                                   v, Gui::GuiScale,
                                   (int)(app->width * Gui::InvGuiScale),
                                   (int)(app->height * Gui::InvGuiScale));
                            if (std::abs(Gui::GuiScale - kExpect[v]) > 0.001f)
                            { fprintf(stderr, "[PC TEST ERROR] guiScale %d must be %.2fx, got %.3f!\n",
                                      v, kExpect[v], Gui::GuiScale); failures++; }
                            if (Gui::GuiScale < 1.0f || Gui::GuiScale > 1.5f)
                            { fprintf(stderr, "[PC TEST ERROR] guiScale %d out of safe range (%.3f)!\n",
                                      v, Gui::GuiScale); failures++; }
                        }

                        // back to the shipping default
                        app->options.set(&Options::Option::GUI_SCALE, 0);
                        app->setSize(app->width, app->height);
                        app->setScreen(new OptionsScreen());
                    }

                    /*
                     * Cursor navigation, used by the inventory and the options.
                     *
                     * The pad steps a free pointer 12 px per press; A presses the
                     * nearest control. Checked here:
                     *   - one press moves exactly one step on one axis
                     *   - releasing does not move it
                     *   - holding does not drift it per frame
                     *   - a control can be walked to and pressed
                     * Screen-to-screen focus stepping is asserted by the input
                     * test, which covers the grids and the world carousel.
                     */
                    {
                        int failures = 0;
                        const int kStep = 12;

                        app->setScreen(new OptionsScreen());
                        OptionsScreen* os = (OptionsScreen*)app->screen;
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);
                        sf2000_input::setCursorPos(160, 120);

                        int cx = 0, cy = 0;
                        sf2000_input::getCursorPos(&cx, &cy);
                        printf("[UI CUR] cursor starts at %d,%d\n", cx, cy);

                        // one press == one step, on one axis only
                        sf2000_input::setPadState(sf2000_input::BTN_RIGHT);
                        sf2000_input::poll(app);
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);
                        sf2000_input::getCursorPos(&cx, &cy);
                        printf("[UI CUR] RIGHT -> %d,%d (dx=%d dy=%d)\n", cx, cy, cx - 160, cy - 120);
                        if (cx - 160 != kStep || cy != 120)
                        { fprintf(stderr, "[PC TEST ERROR] RIGHT must be one %d px step (dx=%d dy=%d)!\n",
                                  kStep, cx - 160, cy - 120); failures++; }

                        // releasing must not move it
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);
                        sf2000_input::getCursorPos(&cx, &cy);
                        if (cx != 172 || cy != 120)
                        { fprintf(stderr, "[PC TEST ERROR] the cursor moved after release (%d,%d)!\n", cx, cy); failures++; }

                        // vertical step
                        sf2000_input::setPadState(sf2000_input::BTN_DOWN);
                        sf2000_input::poll(app);
                        sf2000_input::setPadState(0);
                        sf2000_input::poll(app);
                        sf2000_input::getCursorPos(&cx, &cy);
                        printf("[UI CUR] DOWN  -> %d,%d (dy=%d)\n", cx, cy, cy - 120);
                        if (cy - 120 != kStep || cx != 172)
                        { fprintf(stderr, "[PC TEST ERROR] DOWN must be one %d px step (dx=%d dy=%d)!\n",
                                  kStep, cx - 172, cy - 120); failures++; }

                        // holding must not drift it per frame: the repeat delay is
                        // 320 ms and a frame here is far longer, so exactly one
                        // extra step is expected, not one per poll
                        {
                            const int heldX = cx;
                            int polls = 0, moved = 0;
                            sf2000_input::setPadState(sf2000_input::BTN_RIGHT);
                            for (; polls < 6; ++polls)
                            {
                                sf2000_input::poll(app);
                                int nx = 0, ny = 0;
                                sf2000_input::getCursorPos(&nx, &ny);
                                if (nx != cx) moved++;
                                cx = nx; cy = ny;
                            }
                            sf2000_input::setPadState(0);
                            sf2000_input::poll(app);
                            printf("[UI CUR] 6 polls holding Right: %d move(s), now %d,%d\n", moved, cx, cy);
                            if (moved > 3)
                            { fprintf(stderr, "[PC TEST ERROR] holding drifted the cursor %d times in 6 polls!\n", moved); failures++; }
                            if (cx < heldX)
                            { fprintf(stderr, "[PC TEST ERROR] holding Right moved the cursor backwards!\n"); failures++; }
                        }

                        // A snaps to the nearest control and presses it
                        app->setScreen(new OptionsScreen());
                        os = (OptionsScreen*)app->screen;
                        if (os)
                        {
                            os->selectCategory(0);
                            const std::vector<Button*>& bs = os->getButtons();
                            int best = -1, bestD = 1 << 30;
                            for (size_t i = 0; i < bs.size(); ++i)
                            {
                                const int bx = bs[i]->x + bs[i]->width / 2;
                                const int by = bs[i]->y + bs[i]->height / 2;
                                const int d2 = (bx - 300) * (bx - 300) + (by - 12) * (by - 12);
                                if (d2 < bestD) { bestD = d2; best = (int)i; }
                            }
                            if (best < 0)
                            { fprintf(stderr, "[PC TEST ERROR] no buttons on the pane!\n"); failures++; }
                            else
                            {
                                const int tx = bs[best]->x + bs[best]->width / 2;
                                const int ty = bs[best]->y + bs[best]->height / 2;
                                const int lax = tx - 22, lay = ty + 12;   // logical, outside
                                const float iv = Gui::InvGuiScale;
                                const int pax = (int)(lax / iv), pay = (int)(lay / iv);
                                printf("[UI CUR] target logical (%d,%d), pointing at physical (%d,%d), outside=%s\n",
                                       tx, ty, pax, pay,
                                       bs[best]->pointInside(lax, lay) ? "no" : "yes");
                                if (bs[best]->pointInside(lax, lay))
                                { fprintf(stderr, "[PC TEST ERROR] the aim point should be outside the button!\n"); failures++; }

                                sf2000_input::setCursorPos(pax, pay);
                                sf2000_input::setPadState(sf2000_input::BTN_A);
                                sf2000_input::poll(app);
                                sf2000_input::setPadState(0);
                                sf2000_input::poll(app);
                                if (app->screen) app->screen->updateEvents();

                                printf("[UI CUR] after A: screen=%s\n",
                                       app->screen ? typeid(*app->screen).name() : "<closed>");
                                if (app->screen == os)
                                { fprintf(stderr, "[PC TEST ERROR] A did not press the nearest control!\n"); failures++; }
                                app->setScreen(new OptionsScreen());
                            }
                        }

                        app->setScreen(new OptionsScreen());
                        if (failures)
                        { fprintf(stderr, "[PC TEST ERROR] %d cursor navigation failure(s)!\n", failures); exit(1); }
                        printf("[UI CUR] OK: 12 px steps, no drift, A snaps and presses\n");
                        fflush(stdout);
                    }

                    fflush(stdout);
                }

                // Test Pause Menu: Press START at frame 220
                if (s_current_frame == 220 || s_current_frame == 221)
                {
                    mask |= sf2000_input::BTN_START;
                }
                else
                {
                    // Walk forward
                    mask |= sf2000_input::BTN_UP;

                    // Rotate camera right for a bit, then left
                    if ((s_current_frame / 40) % 2 == 0)
                        mask |= sf2000_input::BTN_A; // Camera right
                    else
                        mask |= sf2000_input::BTN_Y; // Camera left

                    // Swing arm / attack occasionally
                    if (s_current_frame % 50 < 10)
                        mask |= sf2000_input::BTN_L;
                }
            }
            else
            {
                // In menu or loading screen or pause menu:
                if (s_current_frame == 224)
                {
                    PauseScreen* pause = dynamic_cast<PauseScreen*>(app->screen);
                    if (pause)
                    {
                        printf("[PC TEST] In-game PauseScreen verified! Clicking Options button...\n");
                        Button optBtn(5, 0, 0, 100, 20, "Options");
                        static_cast<TestPauseScreen*>(pause)->buttonClicked(&optBtn);
                    }
                }
                else if (s_current_frame == 228)
                {
                    OptionsScreen* inGameOpt = dynamic_cast<OptionsScreen*>(app->screen);
                    if (inGameOpt)
                    {
                        printf("[PC TEST] OptionsScreen opened from PauseScreen while in-game! Closing options...\n");
                        inGameOpt->closeOptions();
                        PauseScreen* backPause = dynamic_cast<PauseScreen*>(app->screen);
                        if (backPause)
                        {
                            printf("[PC TEST] Successfully returned to PauseScreen from OptionsScreen!\n");
                            Button contBtn(1, 0, 0, 100, 20, "Back to game");
                            static_cast<TestPauseScreen*>(backPause)->buttonClicked(&contBtn);
                            printf("[PC TEST] Resumed game from PauseScreen!\n");
                        }
                    }
                }
                // Frame 25: walk to "Start Game" and press it
                else if (s_current_frame == 25)
                {
                    if (!pressButtonByName(app, "Start Game"))
                        printf("[PC TEST] could not press 'Start Game'\n");
                }
                // Frame 55: pick the world / create a new one
                else if (s_current_frame == 55)
                {
                    if (!pressButtonByName(app, "Create new"))
                        printf("[PC TEST] could not press 'Create new'\n");
                }
                // Frame 75: confirm the new world
                else if (s_current_frame == 75)
                {
                    if (!pressButtonByName(app, "Create"))
                        printf("[PC TEST] could not press 'Create'\n");
                }
            }
        }

        s_input_mask = mask;

        // Run one frame of the game core
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        retro_run();
        {
            const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
            s_frameMs = (double)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
        }

        // Save screenshots at key milestones:
        if (inputTest && s_current_frame == 152)
        {
            NinecraftApp* app = sf2000_get_app();
            printf("[INPUT TEST] screenshot frame, screen=%s\n",
                   app && app->screen ? typeid(*app->screen).name() : "<none>");
            saveScreenshotBMP("test_frame_152_inventory.bmp", s_framebuffer, 320, 240);
            fflush(stdout);
        }
        else if (s_current_frame == 15)
        {
            saveScreenshotBMP("test_frame_015_title.bmp", s_framebuffer, 320, 240);
        }
        else if (s_current_frame == 50)
        {
            saveScreenshotBMP("test_frame_050_select_world.bmp", s_framebuffer, 320, 240);
        }
        else if (s_current_frame == 70)
        {
            saveScreenshotBMP("test_frame_070_choose_level.bmp", s_framebuffer, 320, 240);
        }
        else if (s_current_frame == 110)
        {
            saveScreenshotBMP("test_frame_110_loading.bmp", s_framebuffer, 320, 240);
        }
        else if (s_current_frame == 200)
        {
            saveScreenshotBMP("test_frame_200_ingame.bmp", s_framebuffer, 320, 240);
        }
        else if (s_current_frame == 230)
        {
            saveScreenshotBMP("test_frame_230_pause.bmp", s_framebuffer, 320, 240);
        }
        else if (s_current_frame == 320)
        {
            saveScreenshotBMP("test_frame_320_gameplay.bmp", s_framebuffer, 320, 240);
        }

#ifdef HAVE_SDL2
        if (!headless && renderer && texture)
        {
            SDL_UpdateTexture(texture, NULL, s_framebuffer, 320 * sizeof(uint16_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            SDL_Delay(16); // ~60 FPS cap
        }
#endif

        if (s_current_frame % 30 == 0)
        {
            printf("[PC TEST] Frame %d / %d completed successfully.\n", s_current_frame, maxFrames);
            fflush(stdout);
        }

        // Rasteriser workload report for one representative in-game frame. The
        // counters are machine independent, so they describe the MIPS target too.
        if (s_current_frame == 300)
        {
            sf2000_sw::SoftwareRasterizer::instance().resetStats();
        }
        else if (s_current_frame == 301)
        {
            const sf2000_sw::RasterStats& st = sf2000_sw::SoftwareRasterizer::instance().getStats();
            NinecraftApp* app = sf2000_get_app();
            printf("[PC TEST] --- rasteriser workload, 1 in-game frame @ res %d/4 ---\n",
                   (app ? app->options.blockResolution : 1) + 1);
            printf("[PC TEST]   meshes          %lld\n", st.meshes);
            printf("[PC TEST]   vertices        %lld\n", st.verts);
            printf("[PC TEST]   triangles       %lld\n", st.tris);
            printf("[PC TEST]   rejected        %lld\n", st.trisRejected);
            printf("[PC TEST]   rasterized      %lld\n", st.trisRasterized);
            printf("[PC TEST]   spans           %lld\n", st.spans);
            printf("[PC TEST]   pixels tested   %lld\n", st.pixels);
            fflush(stdout);
        }
    }

    saveScreenshotBMP("test_final_frame.bmp", s_framebuffer, 320, 240);

    printf("[PC TEST] Unloading game and cleaning up...\n");
    retro_unload_game();
    retro_deinit();

#ifdef HAVE_SDL2
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
#endif

    printf("[PC TEST SUCCESS] All %d frames executed without crash!\n", maxFrames);
    return 0;
}
