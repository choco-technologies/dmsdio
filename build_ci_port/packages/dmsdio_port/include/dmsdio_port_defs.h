#ifndef DMOD_MOD_DEFS_H_dmsdio_port
#define DMOD_MOD_DEFS_H_dmsdio_port

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_dmsdio_port_version
#  define dmod_dmsdio_port_version "0.1"
#endif

#ifdef DMOD_dmsdio_port
#  define dmod_dmsdio_port_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(dmsdio_port, MODULE, NAME)
#  define dmod_dmsdio_port_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(dmsdio_port, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_dmsdio_port_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(dmsdio_port , DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmsdio_port, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(dmsdio_port, NAME, DMOD_MAKE_VERSION(VERSION,0.1))
#  define dmod_dmsdio_port_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(dmsdio_port, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "dmsdio_port"
#   endif
#   ifndef DMOD_MODULE_VERSION
#       define DMOD_MODULE_VERSION     "0.1"
#   endif
#   define DMOD_AUTHOR_NAME        "Patryk Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           1
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Library
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_dmsdio_port
#  define dmod_dmsdio_port_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#else 
#  define dmod_dmsdio_port_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(dmsdio_port , DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#endif
#  define dmod_dmsdio_port_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(dmsdio_port, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_dmsdio_port_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(dmsdio_port, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(dmsdio_port, NAME, DMOD_MAKE_VERSION(VERSION,0.1))
#  define dmod_dmsdio_port_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmsdio_port, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(dmsdio_port, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.1), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(dmsdio_port, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_dmsdio_port
