#ifndef DMOD_MOD_DEFS_H_log_step
#define DMOD_MOD_DEFS_H_log_step

#include "dmod_defs.h"
#include "dmod_types.h"

#ifndef dmod_log_step_version
#  define dmod_log_step_version "0.1"
#endif

#ifdef DMOD_log_step
#  define dmod_log_step_api_to_mal(MODULE,NAME)                            \
            DMOD_API_TO_MAL(log_step, MODULE, NAME)
#  define dmod_log_step_api_to_mal_ex(NAME_IN, MODULE_MAL, NAME_MAL)       \
            DMOD_API_TO_MAL_EX(log_step, MODULE_IN, NAME_IN, MODULE_MAL, NAME_MAL)
#  define dmod_log_step_api(VERSION, RET, NAME, PARAMS)                    \
            DMOD_INPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_global_api(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_INPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_mal(VERSION, RET, NAME, PARAMS)                    \
            DMOD_MAL_OUTPUT_API(log_step , DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_global_mal(VERSION, RET, NAME, PARAMS)             \
            DMOD_GLOBAL_MAL_OUTPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_api_declaration(VERSION, RET, NAME, PARAMS)        \
            DMOD_INPUT_API_DECLARATION(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_dif(VERSION, RET, NAME, PARAMS)                    \
            DMOD_DIF_FUNCTION_TYPE_DECLARATION(log_step, VERSION, RET, NAME, PARAMS);\
            DMOD_DIF_SIGNATURE_REGISTRATION(log_step, NAME, DMOD_MAKE_VERSION(VERSION,0.1))
#  define dmod_log_step_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
            DMOD_DIF_API_DECLARATION(log_step, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#   ifndef DMOD_MODULE_NAME
#       define DMOD_MODULE_NAME        "log_step"
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
#  ifdef DMOD_MAL_log_step
#  define dmod_log_step_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_INPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_INPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#else 
#  define dmod_log_step_mal(VERSION, RET, NAME, PARAMS)            \
                DMOD_MAL_OUTPUT_API(log_step , DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_global_mal(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_MAL_OUTPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#endif
#  define dmod_log_step_api(VERSION, RET, NAME, PARAMS)            \
                DMOD_OUTPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_global_api(VERSION, RET, NAME, PARAMS)     \
                DMOD_GLOBAL_OUTPUT_API(log_step, DMOD_MAKE_VERSION(VERSION,0.1), RET, NAME, PARAMS)
#  define dmod_log_step_dif(VERSION, RET, NAME, PARAMS)            \
                DMOD_DIF_FUNCTION_TYPE_DECLARATION(log_step, VERSION, RET, NAME, PARAMS);\
                DMOD_DIF_SIGNATURE_REGISTRATION(log_step, NAME, DMOD_MAKE_VERSION(VERSION,0.1))
#  define dmod_log_step_dif_api_declaration(VERSION, IMPL_MODULE, RET, NAME, PARAMS)  \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(log_step, IMPL_MODULE, NAME) PARAMS; \
                _DMOD_DIF_API_REGISTRATION(log_step, IMPL_MODULE, DMOD_MAKE_VERSION(VERSION,0.1), NAME) \
                RET DMOD_MAKE_DIF_API_FUNCTION_NAME(log_step, IMPL_MODULE, NAME) PARAMS
#endif

#endif // DMOD_MOD_DEFS_H_log_step
