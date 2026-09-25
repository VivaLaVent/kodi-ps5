set(SOURCES WinSystemPS5.cpp
            WinSystemPS5GLContext.cpp
            ../linux/WinSystemEGL.cpp)

set(HEADERS WinSystemPS5.h
            WinSystemPS5GLContext.h
            ../linux/WinSystemEGL.h)

core_add_library(windowing_ps5)
