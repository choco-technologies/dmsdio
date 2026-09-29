#ifndef DMOD_MOD_DEFS_H_dmdrvi
#define DMOD_MOD_DEFS_H_dmdrvi

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_dmdrvi_version
#  define dmod_dmdrvi_version "2.1"
#endif

#ifdef DMOD_dmdrvi
#  define dmod_dmdrvi_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(dmdrvi, MODULE, NAME)
#  define dmod_dmdrvi_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(dmdrvi, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_dmdrvi_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(dmdrvi , DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmdrvi, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(dmdrvi, NAME, DMOD_MAKE_VERSION(VERSION,2.1))
#  define dmod_dmdrvi_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(dmdrvi, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "dmdrvi"
#   endif
#   ifndef DMOD_MODULE_VERSION
#       define DMOD_MODULE_VERSION     "2.1"
#   endif
#   define DMOD_AUTHOR_NAME        "Patryk Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           1
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Library
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_dmdrvi
#  define dmod_dmdrvi_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#else 
#  define dmod_dmdrvi_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(dmdrvi , DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#endif
#  define dmod_dmdrvi_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(dmdrvi, DMOD_MAKE_VERSION(VERSION,2.1), RET, NAME, PARAMS)
#  define dmod_dmdrvi_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmdrvi, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(dmdrvi, NAME, DMOD_MAKE_VERSION(VERSION,2.1))
#  define dmod_dmdrvi_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmdrvi, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(dmdrvi, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,2.1), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmdrvi, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_dmdrvi
