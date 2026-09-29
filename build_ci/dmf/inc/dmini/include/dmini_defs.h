#ifndef DMOD_MOD_DEFS_H_dmini
#define DMOD_MOD_DEFS_H_dmini

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_dmini_version
#  define dmod_dmini_version "0.7"
#endif

#ifdef DMOD_dmini
#  define dmod_dmini_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(dmini, MODULE, NAME)
#  define dmod_dmini_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(dmini, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_dmini_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(dmini , DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmini, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(dmini, NAME, DMOD_MAKE_VERSION(VERSION,0.7))
#  define dmod_dmini_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(dmini, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "dmini"
#   endif
#   ifndef DMOD_MODULE_VERSION
#       define DMOD_MODULE_VERSION     "0.7"
#   endif
#   define DMOD_AUTHOR_NAME        "Patryk Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           1
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Library
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_dmini
#  define dmod_dmini_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#else 
#  define dmod_dmini_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(dmini , DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#endif
#  define dmod_dmini_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(dmini, DMOD_MAKE_VERSION(VERSION,0.7), RET, NAME, PARAMS)
#  define dmod_dmini_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmini, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(dmini, NAME, DMOD_MAKE_VERSION(VERSION,0.7))
#  define dmod_dmini_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmini, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(dmini, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.7), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmini, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_dmini
