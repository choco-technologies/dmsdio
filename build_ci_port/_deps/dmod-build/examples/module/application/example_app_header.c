#include "dmod.h"

#ifndef DMOD_MODULE_NAME
#   define DMOD_MODULE_NAME "<Unnamed module>"
#endif

#ifndef DMOD_AUTHOR_NAME
#   define DMOD_AUTHOR_NAME "<Unknown author>"
#endif

#ifndef DMOD_MODULE_VERSION
#   define DMOD_MODULE_VERSION "0.0"
#endif

#ifndef DMOD_MODULE_TYPE
#   define Unknown
#endif

#ifndef DMOD_STACK_SIZE
#   define DMOD_STACK_SIZE 1024
#endif

#ifndef DMOD_PRIORITY
#   define DMOD_PRIORITY 0
#endif

#ifndef DMOD_MANUAL_LOAD
#   define DMOD_MANUAL_LOAD false
#endif

extern void DMOD_WEAK_SYMBOL dmod_preinit(void);
extern int  DMOD_WEAK_SYMBOL dmod_init(const Dmod_Config_t *Config);
extern int  DMOD_WEAK_SYMBOL main(int argc, char** argv);
extern int  DMOD_WEAK_SYMBOL dmod_deinit(void);
extern int  DMOD_WEAK_SYMBOL dmod_signal( int SignalNumber );

extern Dmod_License_t DMOD_WEAK_SYMBOL License;
extern void* __footer_start;

volatile const Dmod_ModuleHeader_t ModuleHeader DMOD_SECTION(".header") DMOD_USED = 
{
    .Signature          = DMOD_HEADER_SIGNATURE,
    .HeaderSize         = sizeof( Dmod_ModuleHeader_t ),
    .DmodVersion        = DMOD_VERSION,
    .PointerSize        = sizeof(void*),
    .Arch               = DMOD_ARCH,
    .CpuName            = DMOD_CPU_NAME,
    .Name               = "example_app",
    .Author             = "Patryk Kubiak",
    .Version            = "0.1",
    .Preinit.Ptr        = dmod_preinit,
    .Init.Ptr           = dmod_init,
    .Main.Ptr           = main,
    .Deinit.Ptr         = dmod_deinit,
    .Signal.Ptr         = dmod_signal,
    .RequiredStackSize  = 1024,
    .Priority           = 0,
    .ModuleType         = Dmod_ModuleType_Application,
    .License.Ptr        = &License,
    .Footer.Ptr         = &__footer_start,
    .ManualLoad         = OFF,
};

volatile const Dmod_ModuleHeader_t* DMOD_Header DMOD_GLOBAL_POINTER = &ModuleHeader;
