#pragma once
// -------------------------------------------------------
// jarvis_core_export.h — Макросы экспорта для DLL
// -------------------------------------------------------

#include <QtCore/qglobal.h>

#if defined(JARVIS_CORE_LIBRARY)
#  define JARVIS_CORE_EXPORT Q_DECL_EXPORT
#else
#  define JARVIS_CORE_EXPORT Q_DECL_IMPORT
#endif
