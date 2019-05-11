/***********************************************************************************************************************************
Execute Perl for Legacy Functionality
***********************************************************************************************************************************/
#include "build.auto.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "version.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/memContext.h"
#include "config/config.h"
#include "perl/config.h"
#include "perl/exec.h"

/***********************************************************************************************************************************
Include LibC code

This file is generated by the LibC xs build.  Including it here allows the functions provided by the C library to be provided by the
pgBackRest binary instead which means the C library does not need to be deployed in production builds.
***********************************************************************************************************************************/
#ifndef HAS_BOOL
#  define HAS_BOOL 1
#endif

#include "perl/libc.auto.c"

/***********************************************************************************************************************************
Include embedded Perl modules
***********************************************************************************************************************************/
typedef struct EmbeddedModule
{
    const char *name;
    const char *data;
} EmbeddedModule;

#include "perl/embed.auto.c"

/***********************************************************************************************************************************
Perl interpreter

This is a silly name but Perl prefers it.
***********************************************************************************************************************************/
static PerlInterpreter *my_perl = NULL;

/***********************************************************************************************************************************
Constants used to build perl options
***********************************************************************************************************************************/
#define PGBACKREST_MODULE                                           PROJECT_NAME "::Main"
#define PGBACKREST_MAIN                                             PGBACKREST_MODULE "::main"

/***********************************************************************************************************************************
Build list of parameters to use for perl main
***********************************************************************************************************************************/
String *
perlMain(void)
{
    FUNCTION_TEST_VOID();

    // Add command arguments to pass to main
    String *commandParam = strNew("");

    for (unsigned int paramIdx = 0; paramIdx < strLstSize(cfgCommandParam()); paramIdx++)
        strCatFmt(commandParam, ",'%s'", strPtr(strLstGet(cfgCommandParam(), paramIdx)));

    // Construct Perl main call
    String *mainCall = strNewFmt(
        "($iResult, $bErrorC, $strMessage) = " PGBACKREST_MAIN "('%s'%s)", cfgCommandName(cfgCommand()), strPtr(commandParam));

    FUNCTION_TEST_RETURN(mainCall);
}

/***********************************************************************************************************************************
Dynamic module loader
***********************************************************************************************************************************/
XS_EUPXS(embeddedModuleGet);
XS_EUPXS(embeddedModuleGet)
{
    // Ensure all parameters were passed
    dVAR; dXSARGS;

    if (items != 1)                                                 // {uncovered_branch - no invalid calls}
       croak_xs_usage(cv, "moduleName");                            // {+uncovered}

    // Get module name
    const char *moduleName = (const char *)SvPV_nolen(ST(0));       // {uncoverable_branch - Perl macro}
    dXSTARG;                                                        // {uncoverable_branch - Perl macro}

    // Find module
    const char *result = NULL;

    for (unsigned int moduleIdx = 0;                                // {uncovered - no invalid modules in embedded Perl}
         moduleIdx < sizeof(embeddedModule) / sizeof(EmbeddedModule); moduleIdx++)
    {
        if (strcmp(embeddedModule[moduleIdx].name, moduleName) == 0)
        {
            result = embeddedModule[moduleIdx].data;
            break;
        }
    }

    // Error if the module was not found
    if (result == NULL)                                             // {uncovered_branch - no invalid modules in embedded Perl}
        croak("unable to load embedded module '%s'", moduleName);   // {+uncovered}

    // Return module data
    sv_setpv(TARG, result);
    XSprePUSH;
    PUSHTARG;                                                       // {uncoverable_branch - Perl macro}

    XSRETURN(1);
}

/***********************************************************************************************************************************
Init the dynaloader so other C modules can be loaded

There are no FUNCTION_TEST* calls because this is a callback from Perl and it doesn't seem wise to mix our stack stuff up in it.
***********************************************************************************************************************************/
#define LOADER_SUB                                                                                                                 \
    "sub\n"                                                                                                                        \
    "{\n"                                                                                                                          \
    "    if ($_[1] =~ /^pgBackRest/)\n"                                                                                            \
    "    {\n"                                                                                                                      \
    "        my $data = pgBackRest::LibC::embeddedModuleGet($_[1]);\n"                                                             \
    "\n"                                                                                                                           \
    "        open my $fh, '<', \\$data;\n"                                                                                         \
    "        return $fh;\n"                                                                                                        \
    "    }\n"                                                                                                                      \
    "}"

EXTERN_C void boot_DynaLoader (pTHX_ CV* cv);

static void xs_init(pTHX)
{
    dXSUB_SYS;
    PERL_UNUSED_CONTEXT;

    // Register the LibC functions by registering the boot function and calling it
    newXS("pgBackRest::LibC::boot", boot_pgBackRest__LibC, __FILE__);
    eval_pv("pgBackRest::LibC::boot()", TRUE);

    // Register the embedded module getter
    newXS("pgBackRest::LibC::embeddedModuleGet", embeddedModuleGet, __FILE__);

    // DynaLoader is a special case
    newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, __FILE__);
}

/***********************************************************************************************************************************
Evaluate a perl statement
***********************************************************************************************************************************/
static void
perlEval(const String *statement)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, statement);
    FUNCTION_TEST_END();

    eval_pv(strPtr(statement), TRUE);

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Initialize Perl
***********************************************************************************************************************************/
static void
perlInit(void)
{
    FUNCTION_TEST_VOID();

    if (!my_perl)
    {
        // Initialize Perl with dummy args and environment
        int argc = 1;
        const char *argv[1] = {strPtr(cfgExe())};
        const char *env[1] = {NULL};
        PERL_SYS_INIT3(&argc, (char ***)&argv, (char ***)&env);

        // Create the interpreter
        const char *embedding[] = {"", "-e", "0"};
        my_perl = perl_alloc();
        perl_construct(my_perl);

        // Don't let $0 assignment update the proctitle or embedding[0]
        PL_origalen = 1;

        // Start the interpreter
        perl_parse(my_perl, xs_init, 3, (char **)embedding, NULL);
        PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
        perl_run(my_perl);

        // Use customer loader to get all embedded modules
        eval_pv("splice(@INC, 0, 0, " LOADER_SUB ");", true);

        // Now that the custom loader is installed, load the main module;
        eval_pv("use " PGBACKREST_MODULE ";", true);

        // Set config data -- this is done separately to avoid it being included in stack traces
        perlEval(strNewFmt(PGBACKREST_MAIN "ConfigSet('%s', '%s')", strPtr(cfgExe()), strPtr(perlOptionJson())));
    }

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Execute main function in Perl
***********************************************************************************************************************************/
int
perlExec(void)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    // Initialize Perl
    perlInit();

    // Run perl main function
    perlEval(perlMain());

    // Return result code
    int code = (int)SvIV(get_sv("iResult", 0));                                     // {uncoverable_branch - Perl macro}
    bool errorC = (int)SvIV(get_sv("bErrorC", 0));                                  // {uncoverable_branch - Perl macro}
    char *message = SvPV_nolen(get_sv("strMessage", 0));                            // {uncoverable_branch - Perl macro}

    if (code >= errorTypeCode(&AssertError))                                        // {uncovered_branch - tested in integration}
    {
        if (errorC)                                                                 // {+uncovered}
            RETHROW();                                                              // {+uncovered}
        else
            THROW_CODE(code, strlen(message) == 0 ? PERL_EMBED_ERROR : message);    // {+uncovered}
    }

    FUNCTION_LOG_RETURN(INT, code);                                                 // {+uncovered}
}

/***********************************************************************************************************************************
Free Perl objects

Don't bother freeing Perl itself since we are about to exit.
***********************************************************************************************************************************/
void
perlFree(int result)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(INT, result);
    FUNCTION_TEST_END();

    if (my_perl != NULL)
        perlEval(strNewFmt(PGBACKREST_MAIN "Cleanup(%d)", result));

    FUNCTION_TEST_RETURN_VOID();
}
