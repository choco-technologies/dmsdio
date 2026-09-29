#ifndef DMOD_MOD_DEFS_H_dmgpio
#define DMOD_MOD_DEFS_H_dmgpio

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_dmgpio_version
#  define dmod_dmgpio_version "2.0"
#endif

#ifdef DMOD_dmgpio
#  define dmod_dmgpio_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(dmgpio, MODULE, NAME)
#  define dmod_dmgpio_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(dmgpio, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_dmgpio_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(dmgpio , DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmgpio, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(dmgpio, NAME, DMOD_MAKE_VERSION(VERSION,2.0))
#  define dmod_dmgpio_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(dmgpio, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "dmgpio"
#   endif
#   ifndef DMOD_MODULE_VERSION
#       define DMOD_MODULE_VERSION     "2.0"
#   endif
#   define DMOD_AUTHOR_NAME        "Patryk Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           1
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Library
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_dmgpio
#  define dmod_dmgpio_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#else 
#  define dmod_dmgpio_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(dmgpio , DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#endif
#  define dmod_dmgpio_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(dmgpio, DMOD_MAKE_VERSION(VERSION,2.0), RET, NAME, PARAMS)
#  define dmod_dmgpio_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmgpio, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(dmgpio, NAME, DMOD_MAKE_VERSION(VERSION,2.0))
#  define dmod_dmgpio_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmgpio, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(dmgpio, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,2.0), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmgpio, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_dmgpio
