#ifndef DMOD_MOD_DEFS_H_dmclk_port
#define DMOD_MOD_DEFS_H_dmclk_port

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_dmclk_port_version
#  define dmod_dmclk_port_version "1.1"
#endif

#ifdef DMOD_dmclk_port
#  define dmod_dmclk_port_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(dmclk_port, MODULE, NAME)
#  define dmod_dmclk_port_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(dmclk_port, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_dmclk_port_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(dmclk_port , DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmclk_port, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(dmclk_port, NAME, DMOD_MAKE_VERSION(VERSION,1.1))
#  define dmod_dmclk_port_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(dmclk_port, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "dmclk_port"
#   endif
#   ifndef DMOD_MODULE_VERSION
#       define DMOD_MODULE_VERSION     "1.1"
#   endif
#   define DMOD_AUTHOR_NAME        "Patryk Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           1
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Library
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_dmclk_port
#  define dmod_dmclk_port_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#else 
#  define dmod_dmclk_port_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(dmclk_port , DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#endif
#  define dmod_dmclk_port_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(dmclk_port, DMOD_MAKE_VERSION(VERSION,1.1), RET, NAME, PARAMS)
#  define dmod_dmclk_port_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmclk_port, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(dmclk_port, NAME, DMOD_MAKE_VERSION(VERSION,1.1))
#  define dmod_dmclk_port_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmclk_port, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(dmclk_port, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,1.1), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmclk_port, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_dmclk_port
