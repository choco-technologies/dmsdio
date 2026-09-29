#ifndef DMOD_MOD_DEFS_H_test_dmsdio
#define DMOD_MOD_DEFS_H_test_dmsdio

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_test_dmsdio_version
#  define dmod_test_dmsdio_version "0.1"
#endif

#ifdef DMOD_test_dmsdio
#  define dmod_test_dmsdio_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(test_dmsdio, MODULE, NAME)
#  define dmod_test_dmsdio_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(test_dmsdio, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_test_dmsdio_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(test_dmsdio , DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(test_dmsdio, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(test_dmsdio, NAME, DMOD_MAKE_VERSION(VERSION,0.1))
#  define dmod_test_dmsdio_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(test_dmsdio, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "test_dmsdio"
#   endif
#   ifndef DMOD_MODULE_VERSION
#       define DMOD_MODULE_VERSION     "0.1"
#   endif
#   define DMOD_AUTHOR_NAME        "Patryk Kubiak"
#   define DMOD_STACK_SIZE         1024
#   define DMOD_PRIORITY           0
#   define DMOD_MODULE_TYPE        Dmod_ModuleType_Application
#   define DMOD_MANUAL_LOAD        OFF
#else
#  ifdef DMOD_MAL_test_dmsdio
#  define dmod_test_dmsdio_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#else 
#  define dmod_test_dmsdio_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(test_dmsdio , DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#endif
#  define dmod_test_dmsdio_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(test_dmsdio, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_test_dmsdio_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(test_dmsdio, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(test_dmsdio, NAME, DMOD_MAKE_VERSION(VERSION,0.1))
#  define dmod_test_dmsdio_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(test_dmsdio, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(test_dmsdio, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.1), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(test_dmsdio, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_test_dmsdio
