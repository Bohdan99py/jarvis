#pragma once
// -------------------------------------------------------
// jarvis_core_export.h
//   Макрос экспорта/импорта для классов JarvisCore.dll
//
//   В core/ собирается с JARVIS_CORE_LIBRARY → __declspec(dllexport)
//   В app/  собирается без него              → __declspec(dllimport)
//
//   На не-Windows платформах макрос разворачивается в пустоту.
// -------------------------------------------------------

#if defined(_WIN32) || defined(_WIN64)
    #ifdef JARVIS_CORE_LIBRARY
        #define JARVIS_CORE_EXPORT __declspec(dllexport)
    #else
        #define JARVIS_CORE_EXPORT __declspec(dllimport)
    #endif
#else
    #define JARVIS_CORE_EXPORT
#endif
