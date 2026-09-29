#ifndef DMOD_MOD_DEFS_H_dmdma
#define DMOD_MOD_DEFS_H_dmdma

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_dmdma_version
#  define dmod_dmdma_version "0.4"
#endif

#ifdef DMOD_dmdma
#  define dmod_dmdma_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(dmdma, MODULE, NAME)
#  define dmod_dmdma_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(dmdma, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_dmdma_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(dmdma , DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmdma, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(dmdma, NAME, DMOD_MAKE_VERSION(VERSION,0.4))
#  define dmod_dmdma_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(dmdma, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "dmdma"
#   endif
#   ifndef DMOD_MODULE_VERSION
#       define DMOD_MODULE_VERSION     "0.4"
#   endif
#   define DMOD_AUTHOR_NAME        "Patryk Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           1
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Library
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_dmdma
#  define dmod_dmdma_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#else 
#  define dmod_dmdma_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(dmdma , DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#endif
#  define dmod_dmdma_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(dmdma, DMOD_MAKE_VERSION(VERSION,0.4), RET, NAME, PARAMS)
#  define dmod_dmdma_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmdma, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(dmdma, NAME, DMOD_MAKE_VERSION(VERSION,0.4))
#  define dmod_dmdma_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmdma, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(dmdma, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.4), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmdma, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_dmdma
