#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "libretro.h"
#include "libco.h"
#include "Rasterizer_SW.h"
#include "AppPlatform_sf2000.h"
#include "SoundSystemSF2000.h"
#include "Input_SF2000.h"
#include "NinecraftApp.h"
#include "platform/log.h"

#if defined(PC_TEST)
#include <cstdarg>
extern "C" void lcd_bsod(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}
#else
extern "C" void lcd_bsod(const char *format, ...);
#endif

static void custom_terminate_handler()
{
    std::exception_ptr p = std::current_exception();
    if (p)
    {
        try {
            std::rethrow_exception(p);
        } catch (const std::exception& e) {
            lcd_bsod("Unhandled exception:\n%s", e.what());
        } catch (...) {
            lcd_bsod("Unhandled unknown C++ exception");
        }
    }
    else
    {
        lcd_bsod("terminate() called with no exception");
    }
    while (1) {}
}

static retro_environment_t        s_environ_cb;
static retro_video_refresh_t      s_video_cb;
static retro_audio_sample_t       s_audio_cb;
static retro_audio_sample_batch_t s_audio_batch_cb;
static retro_input_poll_t         s_input_poll_cb;
static retro_input_state_t        s_input_state_cb;

static cothread_t s_main_thread = nullptr;
static cothread_t s_game_thread = nullptr;

static NinecraftApp* s_app = nullptr;

NinecraftApp* sf2000_get_app()
{
    return s_app;
}

void sf2000_display_flip()
{
    if (s_video_cb)
    {
        s_video_cb(sf2000_sw::SoftwareRasterizer::instance().getFramebuffer(),
                   320, 240, 320 * sizeof(uint16_t));
    }

    if (s_audio_batch_cb && SoundSystemSF2000::instance())
    {
        // 22050 Hz / 60 fps = 368 frames (stereo = 736 samples)
        int16_t audioBuf[368 * 2];
        SoundSystemSF2000::instance()->mix(audioBuf, 368);
        s_audio_batch_cb(audioBuf, 368);
    }

    if (s_main_thread)
    {
        co_switch(s_main_thread);
    }
}

static void game_thread_entry()
{
    try
    {
        AppContext appContext;
        appContext.platform = new AppPlatform_sf2000();
        appContext.doRender = false;

        s_app = new NinecraftApp();
        std::string storage = "mcpe";
        FILE* testFp = fopen("/mnt/sda1/mcpe", "rb");
        if (testFp)
        {
            fclose(testFp);
            storage = "/mnt/sda1/mcpe";
        }
        else
        {
            testFp = fopen("/mnt/sda1/bios", "rb");
            if (testFp)
            {
                fclose(testFp);
                storage = "/mnt/sda1/mcpe";
            }
        }
        s_app->externalStoragePath = storage;
        s_app->externalCacheStoragePath = storage;
        s_app->App::init(appContext);
        s_app->setSize(320, 240);

        /*
         * Options were first loaded inside the NinecraftApp constructor, before
         * the storage path was known, so they used a relative "options.txt" and
         * were never persisted: render distance, auto jump and everything else
         * was silently lost on restart. Point them at the real directory and
         * reload once the app is fully initialised (reloadOptions() needs `user`).
         */
        s_app->options.setSettingsPath(storage);
        s_app->reloadOptions();
        LOGI("Options file: %s\n", s_app->options.getSettingsPath().c_str());

        while (!s_app->wantToQuit())
        {
            s_app->update();
            sf2000_display_flip();
        }

        while (1)
        {
            if (s_environ_cb)
                s_environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
            co_switch(s_main_thread);
        }
    }
    catch (const std::exception& e)
    {
        lcd_bsod("Game error:\n%s", e.what());
    }
    catch (...)
    {
        lcd_bsod("Game error:\nUnknown exception");
    }

    while (1)
    {
        co_switch(s_main_thread);
    }
}

void retro_set_environment(retro_environment_t cb) { s_environ_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { s_video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { s_audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { s_audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { s_input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { s_input_state_cb = cb; }

void retro_init(void)
{
    std::set_terminate(custom_terminate_handler);
    sf2000_sw::SoftwareRasterizer::instance().init();
}

void retro_deinit(void)
{
    if (s_game_thread)
    {
        co_delete(s_game_thread);
        s_game_thread = nullptr;
    }
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
    std::memset(info, 0, sizeof(*info));
    info->library_name     = "Minecraft PE (SF2000)";
    info->library_version  = "0.6.1";
    info->valid_extensions = "pak|zip";
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width   = 320;
    info->geometry.base_height  = 240;
    info->geometry.max_width    = 320;
    info->geometry.max_height   = 240;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = 22050.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port; (void)device;
}

void retro_reset(void) {}

void retro_run(void)
{
    if (s_input_poll_cb)
        s_input_poll_cb();

    if (s_input_state_cb)
    {
        uint32_t mask = 0;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))      mask |= sf2000_input::BTN_B;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))      mask |= sf2000_input::BTN_Y;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT)) mask |= sf2000_input::BTN_SELECT;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))  mask |= sf2000_input::BTN_START;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))     mask |= sf2000_input::BTN_UP;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))   mask |= sf2000_input::BTN_DOWN;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))   mask |= sf2000_input::BTN_LEFT;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))  mask |= sf2000_input::BTN_RIGHT;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))      mask |= sf2000_input::BTN_A;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))      mask |= sf2000_input::BTN_X;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))      mask |= sf2000_input::BTN_L;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))      mask |= sf2000_input::BTN_R;

        sf2000_input::setPadState(mask);
        sf2000_input::poll(s_app);
    }

    if (s_game_thread)
    {
        co_switch(s_game_thread);
    }
}

bool retro_load_game(const struct retro_game_info *info)
{
    (void)info;
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (s_environ_cb)
        s_environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

    s_main_thread = co_active();
    // 512 KB dedicated stack for game coroutine
    s_game_thread = co_create(512 * 1024, game_thread_entry);

    return (s_game_thread != nullptr);
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    (void)game_type; (void)info; (void)num_info;
    return false;
}

void retro_unload_game(void) {}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void *retro_get_memory_data(unsigned id) { (void)id; return nullptr; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
