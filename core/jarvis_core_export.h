#pragma once
// -------------------------------------------------------
// jarvis_core_export.h
//   Макрос экспорта/импорта для классов JarvisCore.
//
//   Поддерживает три режима:
//
//   1) JARVIS_CORE_STATIC     — статическая сборка, макрос пустой
//      (текущий режим в CMakeLists.txt)
//
//   2) JARVIS_CORE_LIBRARY    — мы внутри ядра, экспортируем
//      __declspec(dllexport)
//
//   3) ничего не определено   — мы используем ядро как DLL
//      __declspec(dllimport)
// -------------------------------------------------------

#if defined(JARVIS_CORE_STATIC)
    // Статическая библиотека — экспорт не нужен
    #define JARVIS_CORE_EXPORT
#elif defined(_WIN32) || defined(_WIN64)
    #ifdef JARVIS_CORE_LIBRARY
        #define JARVIS_CORE_EXPORT __declspec(dllexport)
    #else
        #define JARVIS_CORE_EXPORT __declspec(dllimport)
    #endif
#else
    #define JARVIS_CORE_EXPORT
#endif
