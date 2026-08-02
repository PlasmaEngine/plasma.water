#pragma once

#include <Foundation/Basics.h>

// Configure the DLL Import/Export Define
#if PL_ENABLED(PL_COMPILE_ENGINE_AS_DLL)
#  ifdef BUILDSYSTEM_BUILDING_WATERPLUGIN_LIB
#    define PL_WATER_DLL PL_DECL_EXPORT
#  elif defined(BUILDSYSTEM_FOLDING_PLUGIN_IMPORTS)
#    define PL_WATER_DLL
#  else
#    define PL_WATER_DLL PL_DECL_IMPORT
#  endif
#else
#  define PL_WATER_DLL
#endif
