#include <cstdlib>
#include <filesystem>

#include "ps2_runtime.h"

#ifndef DISNEY_PRINCESS_DEFAULT_CD_IMAGE
#define DISNEY_PRINCESS_DEFAULT_CD_IMAGE ""
#endif

namespace
{
    struct ConfigureDisneyPrincessIo
    {
        ConfigureDisneyPrincessIo()
        {
            const char *imagePath = std::getenv("DPRE_CD_IMAGE");
            if (imagePath == nullptr || imagePath[0] == '\0')
            {
                imagePath = DISNEY_PRINCESS_DEFAULT_CD_IMAGE;
            }
            if (imagePath[0] == '\0')
            {
                return;
            }

            PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
            paths.cdImage = std::filesystem::path(imagePath);
            PS2Runtime::setIoPaths(paths);
        }
    };

    ConfigureDisneyPrincessIo g_configureDisneyPrincessIo;
}
