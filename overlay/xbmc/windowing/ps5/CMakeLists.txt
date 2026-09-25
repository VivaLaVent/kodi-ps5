set(SOURCES WinSystemPS5.cpp
            WinSystemPS5GLContext.cpp
            VideoSyncPS5.cpp
            ../linux/WinSystemEGL.cpp)

set(HEADERS WinSystemPS5.h
            WinSystemPS5GLContext.h
            VideoSyncPS5.h
            ../linux/WinSystemEGL.h)

core_add_library(windowing_ps5)
