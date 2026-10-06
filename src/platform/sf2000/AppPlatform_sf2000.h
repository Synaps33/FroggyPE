#pragma once

#include "AppPlatform.h"
#include <string>

class AppPlatform_sf2000 : public AppPlatform
{
public:
    AppPlatform_sf2000();
    virtual ~AppPlatform_sf2000();

    TextureData loadTexture(const std::string& filename_, bool textureFolder) override;
    BinaryBlob readAssetFile(const std::string& filename) override;

    void saveScreenshot(const std::string& filename, int glWidth, int glHeight) override {}
    void playSound(const std::string& fn, float volume, float pitch) override;

    int getScreenWidth() override { return 320; }
    int getScreenHeight() override { return 240; }
    float getPixelsPerMillimeter() override { return 5.0f; }

    bool supportsTouchscreen() override { return false; }
    int checkLicense() override { return 0; }
    bool hasBuyButtonWhenInvalidLicense() override { return false; }
    bool isNetworkEnabled(bool onlyWifiAllowed) override { return false; }

    std::string getDateString(int s) override;

private:
    std::string resolvePath(const std::string& filename);
};
