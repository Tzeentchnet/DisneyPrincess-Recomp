#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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
            std::string recorded;
#ifdef DISNEY_PRINCESS_CD_IMAGE_MARKER
            if (imagePath[0] == '\0')
            {
                // Written by scripts/recompile.ps1, so a direct launch also works.
                std::ifstream marker(DISNEY_PRINCESS_CD_IMAGE_MARKER);
                std::getline(marker, recorded);
                while (!recorded.empty() && (recorded.back() == '\r' || recorded.back() == ' '))
                    recorded.pop_back();
                imagePath = recorded.c_str();
            }
#endif
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
