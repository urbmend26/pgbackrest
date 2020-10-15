/***********************************************************************************************************************************
Test Common Command Routines
***********************************************************************************************************************************/
#include <fcntl.h>
#include <unistd.h>

#include "common/stat.h"
#include "version.h"

#include "common/harnessConfig.h"

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("cmdBegin() and cmdEnd()"))
    {
        cmdInit();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("single parameter");

        StringList *argList = strLstNew();
        strLstAddZ(argList, PROJECT_BIN);
        strLstAddZ(argList, "--" CFGOPT_STANZA "=test");
        strLstAddZ(argList, "--" CFGOPT_ARCHIVE_ASYNC);
        strLstAddZ(argList, "--" CFGOPT_PG1_PATH "=/pg1");
        strLstAddZ(argList, CFGCMD_ARCHIVE_GET);
        strLstAddZ(argList, "param1");
        harnessCfgLoadRaw(strLstSize(argList), strLstPtr(argList));

        TEST_RESULT_VOID(cmdBegin(), "command begin with command parameter");
        harnessLogResult(
            "P00   INFO: archive-get command begin " PROJECT_VERSION ": [param1] --archive-async --pg1-path=/pg1 --stanza=test");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("multiple parameters");

        argList = strLstNew();
        strLstAddZ(argList, PROJECT_BIN);
        strLstAddZ(argList, "--" CFGOPT_STANZA "=test");
        strLstAddZ(argList, "--" CFGOPT_ARCHIVE_ASYNC);
        strLstAddZ(argList, "--" CFGOPT_PG1_PATH "=/pg1");
        strLstAddZ(argList, CFGCMD_ARCHIVE_GET);
        strLstAddZ(argList, "param1");
        strLstAddZ(argList, "param 2");
        harnessCfgLoadRaw(strLstSize(argList), strLstPtr(argList));

        TEST_RESULT_VOID(cmdBegin(), "command begin with command parameters");
        harnessLogResult(
            "P00   INFO: archive-get command begin " PROJECT_VERSION ": [param1, \"param 2\"] --archive-async --pg1-path=/pg1"
                " --stanza=test");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("reset, negate, list, hash options");

        argList = strLstNew();
        strLstAddZ(argList, PROJECT_BIN);
        strLstAddZ(argList, "--no-" CFGOPT_CONFIG);
        strLstAddZ(argList, "--" CFGOPT_STANZA "=test");
        strLstAddZ(argList, "--" CFGOPT_PG1_PATH "=/pg1");
        strLstAddZ(argList, "--" CFGOPT_PG2_PATH "=/pg2");
        strLstAddZ(argList, "--" CFGOPT_REPO1_CIPHER_TYPE "=aes-256-cbc");
        strLstAddZ(argList, "--reset-" CFGOPT_REPO1_HOST);
        strLstAddZ(argList, "--" CFGOPT_REPO1_PATH "=/path/to the/repo");
        strLstAddZ(argList, "--" CFGOPT_DB_INCLUDE "=db1");
        strLstAddZ(argList, "--" CFGOPT_DB_INCLUDE "=db2");
        strLstAddZ(argList, "--" CFGOPT_RECOVERY_OPTION "=standby_mode=on");
        strLstAddZ(argList, "--" CFGOPT_RECOVERY_OPTION "=primary_conninfo=blah");
        strLstAddZ(argList, CFGCMD_RESTORE);
        setenv(HRN_PGBACKREST_ENV CFGOPT_REPO1_CIPHER_PASS, "SECRET-STUFF", true);
        harnessCfgLoadRaw(strLstSize(argList), strLstPtr(argList));

        TEST_RESULT_VOID(cmdBegin(), "command begin");

        #define RESULT_OPTION                                                                                                      \
             " --no-config --db-include=db1 --db-include=db2 --pg1-path=/pg1 --pg2-path=/pg2 --recovery-option=standby_mode=on"    \
             " --recovery-option=primary_conninfo=blah --repo1-cipher-pass=<redacted> --repo1-cipher-type=aes-256-cbc"             \
             " --reset-repo1-host --repo1-path=\"/path/to the/repo\" --stanza=test"

        harnessLogResult("P00   INFO: restore command begin " PROJECT_VERSION ":" RESULT_OPTION);

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("check options in cache");

        TEST_RESULT_STR_Z(cmdOption(), RESULT_OPTION, "option cache");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("command begin does not log when level is too low");

        harnessLogLevelSet(logLevelWarn);
        TEST_RESULT_VOID(cmdBegin(), "command begin");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("command end does not log when level is too low");

        TEST_RESULT_VOID(cmdEnd(0, NULL), "command end");
        harnessLogLevelReset();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("command end with error");

        TEST_RESULT_VOID(cmdEnd(25, strNew("aborted with exception [025]")), "command end");
        harnessLogResult("P00   INFO: restore command end: aborted with exception [025]");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("command end with time");

        TEST_RESULT_VOID(cmdEnd(0, NULL), "command end");
        hrnLogReplaceAdd("\\([0-9]+ms\\)", "[0-9]+", "TIME", false);
        TEST_RESULT_LOG(
            "P00   INFO: restore command end: completed successfully ([TIME]ms)");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("command end with stat and without time");

        statInc(STRDEF("test"));
        cfgOptionSet(cfgOptLogTimestamp, cfgSourceParam, BOOL_FALSE_VAR);

        harnessLogLevelSet(logLevelDetail);

        TEST_RESULT_VOID(cmdEnd(0, NULL), "command end");
        TEST_RESULT_LOG(
            "P00 DETAIL: statistics: {\"test\":{\"total\":1}}\n"
            "P00   INFO: restore command end: completed successfully");

        harnessLogLevelReset();
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
