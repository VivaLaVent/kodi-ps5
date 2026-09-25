set(SOURCES CPUInfoPS5.cpp
            GPUInfoPS5.cpp
            MemUtils.cpp
            PlatformPS5.cpp
            PS5InterfaceForCLog.cpp
            VideoOutInfo.cpp
            strptime.cpp)

set(HEADERS CPUInfoPS5.h
            GPUInfoPS5.h
            PlatformPS5.h
            PS5InterfaceForCLog.h
            VideoOutInfo.h
            sce/SceAudioOut.h
            sce/ScePad.h
            sce/SceUserService.h)

core_add_library(platform_ps5)
