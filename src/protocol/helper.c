/***********************************************************************************************************************************
Protocol Helper
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>

#include "common/crypto/common.h"
#include "common/debug.h"
#include "common/exec.h"
#include "common/memContext.h"
#include "config/config.h"
#include "config/define.h"
#include "config/exec.h"
#include "config/protocol.h"
#include "postgres/version.h"
#include "protocol/helper.h"
#include "version.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
STRING_EXTERN(PROTOCOL_SERVICE_LOCAL_STR,                           PROTOCOL_SERVICE_LOCAL);
STRING_EXTERN(PROTOCOL_SERVICE_REMOTE_STR,                          PROTOCOL_SERVICE_REMOTE);

STRING_STATIC(PROTOCOL_REMOTE_TYPE_PG_STR,                          PROTOCOL_REMOTE_TYPE_PG);
STRING_STATIC(PROTOCOL_REMOTE_TYPE_REPO_STR,                        PROTOCOL_REMOTE_TYPE_REPO);

/***********************************************************************************************************************************
Local variables
***********************************************************************************************************************************/
typedef struct ProtocolHelperClient
{
    Exec *exec;                                                     // Executed client
    ProtocolClient *client;                                         // Protocol client
} ProtocolHelperClient;

static struct
{
    MemContext *memContext;                                         // Mem context for protocol helper

    unsigned int clientRemoteSize;                                  // Remote clients
    ProtocolHelperClient *clientRemote;

    unsigned int clientLocalSize;                                   // Local clients
    ProtocolHelperClient *clientLocal;
} protocolHelper;

/***********************************************************************************************************************************
Init local mem context and data structure
***********************************************************************************************************************************/
static void
protocolHelperInit(void)
{
    // In the protocol helper has not been initialized
    if (protocolHelper.memContext == NULL)
    {
        // Create a mem context to store protocol objects
        MEM_CONTEXT_BEGIN(memContextTop())
        {
            MEM_CONTEXT_NEW_BEGIN("ProtocolHelper")
            {
                protocolHelper.memContext = MEM_CONTEXT_NEW();
            }
            MEM_CONTEXT_NEW_END();
        }
        MEM_CONTEXT_END();
    }
}

/**********************************************************************************************************************************/
bool
repoIsLocal(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RETURN(!cfgOptionTest(cfgOptRepoHost));
}

/**********************************************************************************************************************************/
void
repoIsLocalVerify(void)
{
    FUNCTION_TEST_VOID();

    if (!repoIsLocal())
        THROW_FMT(HostInvalidError, "%s command must be run on the repository host", cfgCommandName(cfgCommand()));

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
bool
pgIsLocal(unsigned int pgIdx)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, pgIdx);
    FUNCTION_LOG_END();

    FUNCTION_LOG_RETURN(BOOL, !cfgOptionTest(cfgOptPgHost + pgIdx));
}

/**********************************************************************************************************************************/
void
pgIsLocalVerify(void)
{
    FUNCTION_TEST_VOID();

    if (!pgIsLocal(0))
        THROW_FMT(HostInvalidError, "%s command must be run on the " PG_NAME " host", cfgCommandName(cfgCommand()));

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Get the command line required for local protocol execution
***********************************************************************************************************************************/
static StringList *
protocolLocalParam(ProtocolStorageType protocolStorageType, unsigned int hostIdx, unsigned int processId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, hostIdx);
        FUNCTION_LOG_PARAM(UINT, processId);
    FUNCTION_LOG_END();

    StringList *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Option replacements
        KeyValue *optionReplace = kvNew();

        // Add the process id -- used when more than one process will be called
        kvPut(optionReplace, VARSTR(CFGOPT_PROCESS_STR), VARUINT(processId));

        // Add the host id
        kvPut(optionReplace, VARSTR(CFGOPT_HOST_ID_STR), VARUINT(hostIdx + 1));

        // Add the remote type
        kvPut(optionReplace, VARSTR(CFGOPT_REMOTE_TYPE_STR), VARSTR(protocolStorageTypeStr(protocolStorageType)));

        // Only enable file logging on the local when requested
        kvPut(
            optionReplace, VARSTR(CFGOPT_LOG_LEVEL_FILE_STR),
            cfgOptionBool(cfgOptLogSubprocess) ? cfgOption(cfgOptLogLevelFile) : VARSTRDEF("off"));

        // Always output errors on stderr for debugging purposes
        kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_STDERR_STR), VARSTRDEF("error"));

        // Disable output to stdout since it is used by the protocol
        kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_CONSOLE_STR), VARSTRDEF("off"));

        result = strLstMove(cfgExecParam(cfgCommand(), cfgCmdRoleLocal, optionReplace, true, false), memContextPrior());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(STRING_LIST, result);
}

/**********************************************************************************************************************************/
ProtocolClient *
protocolLocalGet(ProtocolStorageType protocolStorageType, unsigned int hostIdx, unsigned int processId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, hostIdx);
        FUNCTION_LOG_PARAM(UINT, processId);
    FUNCTION_LOG_END();

    protocolHelperInit();

    // Allocate the client cache
    if (protocolHelper.clientLocalSize == 0)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            protocolHelper.clientLocalSize = cfgOptionUInt(cfgOptProcessMax) + 1;
            protocolHelper.clientLocal = memNew(protocolHelper.clientLocalSize * sizeof(ProtocolHelperClient));

            for (unsigned int clientIdx = 0; clientIdx < protocolHelper.clientLocalSize; clientIdx++)
                protocolHelper.clientLocal[clientIdx] = (ProtocolHelperClient){.exec = NULL};
        }
        MEM_CONTEXT_END();
    }

    ASSERT(processId <= protocolHelper.clientLocalSize);

    // Create protocol object
    ProtocolHelperClient *protocolHelperClient = &protocolHelper.clientLocal[processId - 1];

    if (protocolHelperClient->client == NULL)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            // Execute the protocol command
            protocolHelperClient->exec = execNew(
                cfgExe(), protocolLocalParam(protocolStorageType, hostIdx, processId),
                strNewFmt(PROTOCOL_SERVICE_LOCAL "-%u process", processId),
                (TimeMSec)(cfgOptionDbl(cfgOptProtocolTimeout) * 1000));
            execOpen(protocolHelperClient->exec);

            // Create protocol object
            protocolHelperClient->client = protocolClientNew(
                strNewFmt(PROTOCOL_SERVICE_LOCAL "-%u protocol", processId),
                PROTOCOL_SERVICE_LOCAL_STR, execIoRead(protocolHelperClient->exec), execIoWrite(protocolHelperClient->exec));

            protocolClientMove(protocolHelperClient->client, execMemContext(protocolHelperClient->exec));
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_LOG_RETURN(PROTOCOL_CLIENT, protocolHelperClient->client);
}

/***********************************************************************************************************************************
Free the protocol client and underlying exec'd process. Log any errors as warnings since it is not worth terminating the process
while closing a local/remote that has already completed its work. The warning will be an indication that something is not right.
***********************************************************************************************************************************/
static void
protocolHelperClientFree(ProtocolHelperClient *protocolHelperClient)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM_P(VOID, protocolHelperClient);
    FUNCTION_LOG_END();

    if (protocolHelperClient->client != NULL)
    {
        // Try to shutdown the protocol but only warn on error
        TRY_BEGIN()
        {
            protocolClientFree(protocolHelperClient->client);
        }
        CATCH_ANY()
        {
            LOG_WARN(errorMessage());
        }
        TRY_END();

        // Try to end the child process but only warn on error
        TRY_BEGIN()
        {
            execFree(protocolHelperClient->exec);
        }
        CATCH_ANY()
        {
            LOG_WARN(errorMessage());
        }
        TRY_END();

        protocolHelperClient->client = NULL;
        protocolHelperClient->exec = NULL;
    }

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
void
protocolLocalFree(unsigned int processId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, processId);
    FUNCTION_LOG_END();

    if (protocolHelper.clientLocal != NULL)
    {
        ASSERT(processId <= protocolHelper.clientLocalSize);
        protocolHelperClientFree(&protocolHelper.clientLocal[processId - 1]);
    }

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Get the command line required for remote protocol execution
***********************************************************************************************************************************/
static StringList *
protocolRemoteParam(ProtocolStorageType protocolStorageType, unsigned int hostIdx)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, hostIdx);
    FUNCTION_LOG_END();

    // Is this a repo remote?
    bool isRepo = protocolStorageType == protocolStorageTypeRepo;

    // Fixed parameters for ssh command
    StringList *result = strLstNew();
    strLstAddZ(result, "-o");
    strLstAddZ(result, "LogLevel=error");
    strLstAddZ(result, "-o");
    strLstAddZ(result, "Compression=no");
    strLstAddZ(result, "-o");
    strLstAddZ(result, "PasswordAuthentication=no");

    // Append port if specified
    ConfigOption optHostPort = isRepo ? cfgOptRepoHostPort : cfgOptPgHostPort + hostIdx;

    if (cfgOptionTest(optHostPort))
    {
        strLstAddZ(result, "-p");
        strLstAdd(result, strNewFmt("%u", cfgOptionUInt(optHostPort)));
    }

    // Append user/host
    strLstAdd(
        result,
        strNewFmt(
            "%s@%s", strZ(cfgOptionStr(isRepo ? cfgOptRepoHostUser : cfgOptPgHostUser + hostIdx)),
            strZ(cfgOptionStr(isRepo ? cfgOptRepoHost : cfgOptPgHost + hostIdx))));

    // Option replacements
    KeyValue *optionReplace = kvNew();

    // Replace config options with the host versions
    unsigned int optConfig = isRepo ? cfgOptRepoHostConfig : cfgOptPgHostConfig + hostIdx;

    kvPut(optionReplace, VARSTR(CFGOPT_CONFIG_STR), cfgOptionSource(optConfig) != cfgSourceDefault  ? cfgOption(optConfig) : NULL);

    unsigned int optConfigIncludePath = isRepo ? cfgOptRepoHostConfigIncludePath : cfgOptPgHostConfigIncludePath + hostIdx;

    kvPut(
        optionReplace, VARSTR(CFGOPT_CONFIG_INCLUDE_PATH_STR),
        cfgOptionSource(optConfigIncludePath) != cfgSourceDefault ? cfgOption(optConfigIncludePath) : NULL);

    unsigned int optConfigPath = isRepo ? cfgOptRepoHostConfigPath : cfgOptPgHostConfigPath + hostIdx;

    kvPut(
        optionReplace, VARSTR(CFGOPT_CONFIG_PATH_STR),
        cfgOptionSource(optConfigPath) != cfgSourceDefault ? cfgOption(optConfigPath) : NULL);

    // Update/remove repo/pg options that are sent to the remote
    const String *repoHostPrefix = STR(cfgDefOptionName(cfgDefOptRepoHost));
    const String *repoPrefix = strNewFmt("%s-", PROTOCOL_REMOTE_TYPE_REPO);
    const String *pgHostPrefix = STR(cfgDefOptionName(cfgDefOptPgHost));
    const String *pgPrefix = strNewFmt("%s-", PROTOCOL_REMOTE_TYPE_PG);

    for (ConfigOption optionId = 0; optionId < CFG_OPTION_TOTAL; optionId++)
    {
        ConfigDefineOption optionDefId = cfgOptionDefIdFromId(optionId);
        const String *optionDefName = STR(cfgDefOptionName(optionDefId));
        bool remove = false;

        // Remove repo host options that are not needed on the remote.  The remote is not expecting to see host settings and it
        // could get confused about the locality of the repo, i.e. local or remote.
        if (strBeginsWith(optionDefName, repoHostPrefix))
        {
            remove = true;
        }
        // Remove repo options when the remote type is pg since they won't be used
        else if (strBeginsWith(optionDefName, repoPrefix))
        {
            if (protocolStorageType == protocolStorageTypePg)
                remove = true;
        }
        // Remove pg host options that are not needed on the remote.  The remote is not expecting to see host settings and it could
        // get confused about the locality of pg, i.e. local or remote.
        else if (strBeginsWith(optionDefName, pgHostPrefix))
        {
            remove = true;
        }
        else if (strBeginsWith(optionDefName, pgPrefix))
        {
            // Remove unrequired/defaulted pg options when the remote type is repo since they won't be used
            if (protocolStorageType == protocolStorageTypeRepo)
            {
                remove = !cfgDefOptionRequired(cfgCommand(), optionDefId) || cfgDefOptionDefault(cfgCommand(), optionDefId) != NULL;
            }
            // Else move/remove pg options with index > 0 since they won't be used
            else if (cfgOptionIndex(optionId) > 0)
            {
                // If the option index matches the host-id then this is a pg option that the remote needs.  Since the remote expects
                // to find pg options in index 0, copy the option to index 0.
                if (cfgOptionIndex(optionId) == hostIdx)
                {
                    kvPut(
                        optionReplace, VARSTRZ(cfgOptionName(optionId - hostIdx)),
                        cfgOptionSource(optionId) != cfgSourceDefault ? cfgOption(optionId) : NULL);
                }

                // Remove pg options that are not needed on the remote.  The remote is only going to look at index 0 so the options
                // in higher indexes will not be used and just add clutter which makes debugging harder.
                remove = true;
            }
        }

        // Remove options that have been marked for removal if they are not already null or invalid. This is more efficient because
        // cfgExecParam() won't have to search through as large a list looking for overrides.
        if (remove && cfgOptionTest(optionId))
            kvPut(optionReplace, VARSTRZ(cfgOptionName(optionId)), NULL);
    }

    // Set local so host settings configured on the remote will not accidentally be picked up
    kvPut(
        optionReplace,
        protocolStorageType == protocolStorageTypeRepo ?
            VARSTRZ(cfgOptionName(cfgOptRepoLocal)) : VARSTRZ(cfgOptionName(cfgOptPgLocal)),
        BOOL_TRUE_VAR);

    // Don't pass host-id to the remote.  The host will always be in index 0.
    kvPut(optionReplace, VARSTR(CFGOPT_HOST_ID_STR), NULL);

    // Add the process id if not set. This means that the remote is being started from the main process and should always get a
    // process id of 0.
    if (!cfgOptionTest(cfgOptProcess))
        kvPut(optionReplace, VARSTR(CFGOPT_PROCESS_STR), VARINT(0));

    // Don't pass log-path or lock-path since these are host specific
    kvPut(optionReplace, VARSTR(CFGOPT_LOG_PATH_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_LOCK_PATH_STR), NULL);

    // ??? Don't pass restore options which the remote doesn't need and are likely to contain spaces because they might get mangled
    // on the way to the remote depending on how SSH is set up on the server.  This code should be removed when option passing with
    // spaces is resolved.
    kvPut(optionReplace, VARSTR(CFGOPT_TYPE_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_TARGET_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_TARGET_EXCLUSIVE_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_TARGET_ACTION_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_TARGET_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_TARGET_TIMELINE_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_RECOVERY_OPTION_STR), NULL);

    // Only enable file logging on the remote when requested
    kvPut(
        optionReplace, VARSTR(CFGOPT_LOG_LEVEL_FILE_STR),
        cfgOptionBool(cfgOptLogSubprocess) ? cfgOption(cfgOptLogLevelFile) : VARSTRDEF("off"));

    // Always output errors on stderr for debugging purposes
    kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_STDERR_STR), VARSTRDEF("error"));

    // Disable output to stdout since it is used by the protocol
    kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_CONSOLE_STR), VARSTRDEF("off"));

    // Add the remote type
    kvPut(optionReplace, VARSTR(CFGOPT_REMOTE_TYPE_STR), VARSTR(protocolStorageTypeStr(protocolStorageType)));

    StringList *commandExec = cfgExecParam(cfgCommand(), cfgCmdRoleRemote, optionReplace, false, true);
    strLstInsert(commandExec, 0, cfgOptionStr(isRepo ? cfgOptRepoHostCmd : cfgOptPgHostCmd + hostIdx));
    strLstAdd(result, strLstJoin(commandExec, " "));

    FUNCTION_LOG_RETURN(STRING_LIST, result);
}

/**********************************************************************************************************************************/
ProtocolClient *
protocolRemoteGet(ProtocolStorageType protocolStorageType, unsigned int hostIdx)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, hostIdx);
    FUNCTION_LOG_END();

    // Is this a repo remote?
    bool isRepo = protocolStorageType == protocolStorageTypeRepo;

    protocolHelperInit();

    // Allocate the client cache
    if (protocolHelper.clientRemoteSize == 0)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            // The number of remotes allowed is the greater of allowed repo or pg configs + 1 (0 is reserved for connections from
            // the main process).  Since these are static and only one will be true it presents a problem for coverage.  We think
            // that pg remotes will always be greater but we'll protect that assumption with an assertion.
            ASSERT(cfgDefOptionIndexTotal(cfgDefOptPgPath) >= cfgDefOptionIndexTotal(cfgDefOptRepoPath));

            protocolHelper.clientRemoteSize = cfgDefOptionIndexTotal(cfgDefOptPgPath) + 1;
            protocolHelper.clientRemote = memNew(protocolHelper.clientRemoteSize * sizeof(ProtocolHelperClient));

            for (unsigned int clientIdx = 0; clientIdx < protocolHelper.clientRemoteSize; clientIdx++)
                protocolHelper.clientRemote[clientIdx] = (ProtocolHelperClient){.exec = NULL};
        }
        MEM_CONTEXT_END();
    }

    // Determine protocol id for the remote.  If the process option is set then use that since we want the remote protocol id to
    // match the local protocol id. Otherwise set to 0 since the remote is being started from a main process and there should only
    // be one remote per host.
    unsigned int processId = 0;

    if (cfgOptionTest(cfgOptProcess))
        processId = cfgOptionUInt(cfgOptProcess);

    CHECK(hostIdx < protocolHelper.clientRemoteSize);

    // Create protocol object
    ProtocolHelperClient *protocolHelperClient = &protocolHelper.clientRemote[hostIdx];

    if (protocolHelperClient->client == NULL)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            unsigned int optHost = isRepo ? cfgOptRepoHost : cfgOptPgHost + hostIdx;

            // Execute the protocol command
            protocolHelperClient->exec = execNew(
                cfgOptionStr(cfgOptCmdSsh), protocolRemoteParam(protocolStorageType, hostIdx),
                strNewFmt(PROTOCOL_SERVICE_REMOTE "-%u process on '%s'", processId, strZ(cfgOptionStr(optHost))),
                (TimeMSec)(cfgOptionDbl(cfgOptProtocolTimeout) * 1000));
            execOpen(protocolHelperClient->exec);

            // Create protocol object
            protocolHelperClient->client = protocolClientNew(
                strNewFmt(PROTOCOL_SERVICE_REMOTE "-%u protocol on '%s'", processId, strZ(cfgOptionStr(optHost))),
                PROTOCOL_SERVICE_REMOTE_STR, execIoRead(protocolHelperClient->exec), execIoWrite(protocolHelperClient->exec));

            // Get cipher options from the remote if none are locally configured
            if (isRepo && strEq(cfgOptionStr(cfgOptRepoCipherType), CIPHER_TYPE_NONE_STR))
            {
                // Options to query
                VariantList *param = varLstNew();
                varLstAdd(param, varNewStrZ(cfgOptionName(cfgOptRepoCipherType)));
                varLstAdd(param, varNewStrZ(cfgOptionName(cfgOptRepoCipherPass)));

                VariantList *optionList = configProtocolOption(protocolHelperClient->client, param);

                if (!strEq(varStr(varLstGet(optionList, 0)), CIPHER_TYPE_NONE_STR))
                {
                    cfgOptionSet(cfgOptRepoCipherType, cfgSourceConfig, varLstGet(optionList, 0));
                    cfgOptionSet(cfgOptRepoCipherPass, cfgSourceConfig, varLstGet(optionList, 1));
                }
            }

            protocolClientMove(protocolHelperClient->client, execMemContext(protocolHelperClient->exec));
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_LOG_RETURN(PROTOCOL_CLIENT, protocolHelperClient->client);
}

/**********************************************************************************************************************************/
void
protocolRemoteFree(unsigned int hostIdx)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, hostIdx);
    FUNCTION_LOG_END();

    if (protocolHelper.clientRemote != NULL)
        protocolHelperClientFree(&protocolHelper.clientRemote[hostIdx]);

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
void
protocolKeepAlive(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    if (protocolHelper.memContext != NULL)
    {
        for (unsigned int clientIdx  = 0; clientIdx < protocolHelper.clientRemoteSize; clientIdx++)
        {
            if (protocolHelper.clientRemote[clientIdx].client != NULL)
                protocolClientNoOp(protocolHelper.clientRemote[clientIdx].client);
        }
    }

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Getters/Setters
***********************************************************************************************************************************/
ProtocolStorageType
protocolStorageTypeEnum(const String *type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, type);
    FUNCTION_TEST_END();

    ASSERT(type != NULL);

    if (strEq(type, PROTOCOL_REMOTE_TYPE_PG_STR))
        FUNCTION_TEST_RETURN(protocolStorageTypePg);
    else if (strEq(type, PROTOCOL_REMOTE_TYPE_REPO_STR))
        FUNCTION_TEST_RETURN(protocolStorageTypeRepo);

    THROW_FMT(AssertError, "invalid protocol storage type '%s'", strZ(type));
}

const String *
protocolStorageTypeStr(ProtocolStorageType type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, type);
    FUNCTION_TEST_END();

    switch (type)
    {
        case protocolStorageTypePg:
            FUNCTION_TEST_RETURN(PROTOCOL_REMOTE_TYPE_PG_STR);

        case protocolStorageTypeRepo:
            FUNCTION_TEST_RETURN(PROTOCOL_REMOTE_TYPE_REPO_STR);
    }

    THROW_FMT(AssertError, "invalid protocol storage type %u", type);
}

/**********************************************************************************************************************************/
void
protocolFree(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    if (protocolHelper.memContext != NULL)
    {
        // Free remotes
        for (unsigned int clientIdx = 0; clientIdx < protocolHelper.clientRemoteSize; clientIdx++)
            protocolRemoteFree(clientIdx);

        // Free locals
        for (unsigned int clientIdx = 1; clientIdx <= protocolHelper.clientLocalSize; clientIdx++)
            protocolLocalFree(clientIdx);
    }

    FUNCTION_LOG_RETURN_VOID();
}
