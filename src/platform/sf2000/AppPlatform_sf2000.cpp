#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include "AppPlatform_sf2000.h"
#include "SoundSystemSF2000.h"
#include "platform/log.h"
#include <cstdio>
#include <cstring>
#include <sstream>

AppPlatform_sf2000::AppPlatform_sf2000()
{
}

AppPlatform_sf2000::~AppPlatform_sf2000()
{
}

std::string AppPlatform_sf2000::resolvePath(const std::string& filename)
{
    static const char* prefixes[] = {
        "",
        "/mnt/sda1/mcpe/",
        "/mnt/sda1/mcpe/data/",
        "/mnt/sda1/ROMS/mcpe/",
        "/mnt/sda1/ROMS/mcpe/data/",
        "data/",
        "mcpe/",
        "mcpe/data/",
        "/mnt/sd/mcpe/",
        "/mnt/sd/mcpe/data/"
    };

    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
    {
        std::string p = prefixes[i] + filename;
        FILE* fp = fopen(p.c_str(), "rb");
        if (fp)
        {
            fclose(fp);
            return p;
        }
    }

    return filename;
}

TextureData AppPlatform_sf2000::loadTexture(const std::string& filename_, bool textureFolder)
{
    TextureData out;
    std::string filename = textureFolder ? ("data/images/" + filename_) : filename_;
    std::string path = resolvePath(filename);

    int w = 0, h = 0, channels = 0;
    unsigned char* raw = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!raw)
    {
        // Try fallback if textureFolder had data/images/
        if (textureFolder)
        {
            path = resolvePath("images/" + filename_);
            raw = stbi_load(path.c_str(), &w, &h, &channels, 4);
        }
    }

    if (raw)
    {
        size_t sz = (size_t)w * h * 4;
        out.data = new unsigned char[sz];
        std::memcpy(out.data, raw, sz);
        stbi_image_free(raw);

        out.w = w;
        out.h = h;
        out.numBytes = (int)sz;
        out.transparent = true;
        out.memoryHandledExternally = false;
        out.format = TEXF_UNCOMPRESSED_8888;
    }
    else
    {
        LOGW("AppPlatform_sf2000: Failed to load texture: %s\n", filename_.c_str());
    }

    return out;
}

BinaryBlob AppPlatform_sf2000::readAssetFile(const std::string& filename)
{
    std::string path = resolvePath(filename);
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        path = resolvePath("data/" + filename);
        fp = fopen(path.c_str(), "rb");
    }

    if (!fp)
        return BinaryBlob();

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0)
    {
        fclose(fp);
        return BinaryBlob();
    }

    BinaryBlob blob;
    blob.size = (int)sz;
    blob.data = new unsigned char[sz];
    fread(blob.data, 1, sz, fp);
    fclose(fp);

    return blob;
}

void AppPlatform_sf2000::playSound(const std::string& fn, float volume, float pitch)
{
    // Handled directly via SoundEngine -> SoundSystemSF2000
    (void)fn; (void)volume; (void)pitch;
}

std::string AppPlatform_sf2000::getDateString(int s)
{
    std::stringstream ss;
    ss << s << " s (UTC)";
    return ss.str();
}
