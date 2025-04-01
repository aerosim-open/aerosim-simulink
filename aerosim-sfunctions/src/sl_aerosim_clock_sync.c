/*
 * sl_aerosim_clock_sync
 *
 * Copyright 2019 The MathWorks, Inc.
 */

/**
 * Simple Apache Kafka consumer using the Kafka driver from librdkafka
 * (https://github.com/edenhill/librdkafka)
 */

#define S_FUNCTION_NAME sl_aerosim_clock_sync
#define S_FUNCTION_LEVEL 2

/*
 * Need to include simstruc.h for the definition of the SimStruct and
 * its associated macro definitions.
 */
#include "simstruc.h"
#include "fixedpoint.h"

#include "rdkafka.h"

// Needed for JSON decoding
#include "jansson.h"

#include <float.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif

#include "mw_kafka_utils.h"
#include "mx_kafka_utils.h"
#include "aerosim_kafka_utils.h"

enum
{
    EP_BROKERS = 0,
    EP_START_CMD_TIMEOUT,
    EP_CLOCK_MSG_TIMEOUT,
    EP_MSG_LEN,
    EP_KEY_LEN,
    EP_OUTPUT_TIMESTAMP,
    EP_CONF,
    EP_TOPIC_CONF,
    EP_COMBINED_CONF_STR,
    EP_TS,
    EP_NumParams
};

#define P_BROKER (ssGetSFcnParam(S, EP_BROKERS))
#define P_START_CMD_TIMEOUT ((double)mxGetScalar((ssGetSFcnParam(S, EP_START_CMD_TIMEOUT))))
#define P_CLOCK_MSG_TIMEOUT ((double)mxGetScalar((ssGetSFcnParam(S, EP_CLOCK_MSG_TIMEOUT))))
#define P_MSG_LEN ((int_T)mxGetScalar((ssGetSFcnParam(S, EP_MSG_LEN))))
#define P_KEY_LEN ((int_T)mxGetScalar((ssGetSFcnParam(S, EP_KEY_LEN))))
#define P_OUTPUT_TIMESTAMP ((int_T)mxGetScalar((ssGetSFcnParam(S, EP_OUTPUT_TIMESTAMP))))
#define P_CONF (ssGetSFcnParam(S, EP_CONF))
#define P_TOPIC_CONF (ssGetSFcnParam(S, EP_TOPIC_CONF))
#define P_COMBINED_CONF_STR (ssGetSFcnParam(S, EP_COMBINED_CONF_STR))
#define P_TS (ssGetSFcnParam(S, EP_TS))

enum
{
    EPW_CLOCK_CONSUMER = 0,
    EPW_ORCHESTRATOR_CONSUMER = 1,
    EPW_SIM_START_STATUS = 2,
    EPW_ORCHESTRATOR_MSG = 3,
    EPW_ORCHESTRATOR_KEY = 4,
    EPW_NumPWorks
};

static int wait_eof = 0; /* number of partitions awaiting EOF */

static char errstr[512]; /* librdkafka API error reporting buffer */

static int getParamString(SimStruct *S, char **strPtr, const mxArray *prm, int epwIdx, char *errorHelp)
{
    int N = (int)mxGetNumberOfElements(prm);
    char *tmp = (char *)malloc(N + 1);
    if (tmp == NULL)
    {
        sprintf(errstr, "Couldn't allocate string for '%s'\n", errorHelp);
        ssSetErrorStatus(S, errstr);
        return 1;
    }
    if (mxGetString(prm, tmp, N + 1))
    {
        sprintf(errstr, "Couldn't retrieve '%s' string\n", errorHelp);
        ssSetErrorStatus(S, errstr);
        return 2;
    }
    if (epwIdx >= 0)
    {
        ssSetPWorkValue(S, epwIdx, tmp);
    }
    *strPtr = tmp;

    return 0;
}

void initKafkaConsumer(SimStruct *S, const char* topic, const char* group, int_T p_work_idx)
{
    rd_kafka_t *rk = NULL;        /* Consumer instance handle */
    rd_kafka_topic_t *rkt = NULL; /* Topic object */
    rd_kafka_conf_t *conf = NULL; /* Temporary configuration object */
    rd_kafka_topic_conf_t *topic_conf = NULL;
    rd_kafka_topic_partition_list_t *topics;
    int partition = RD_KAFKA_PARTITION_UA;
    int64_t start_offset = RD_KAFKA_OFFSET_BEGINNING;
    /*
     *  - RD_KAFKA_OFFSET_BEGINNING
     *  - RD_KAFKA_OFFSET_END
     *  - RD_KAFKA_OFFSET_STORED
     *  - RD_KAFKA_OFFSET_TAIL
     */

    char *brokers; /* Argument: broker list */

    int nConf, nTopicConf;

    mwLogInit("simulink");

    if (getParamString(S, &brokers, P_BROKER, -1, "brokers"))
        goto exit_init_kafka;
    mexPrintf("Initializing Kafka Consumer - (brokers: %s, topic: %s, group: %s)\n", brokers, topic, group);

    nConf = mxGetNumberOfElements(P_CONF);
    nTopicConf = mxGetNumberOfElements(P_TOPIC_CONF);
    const char **confArray = getConfArrayFromMX(nConf, P_CONF, nTopicConf, P_TOPIC_CONF);

    if (confArray == NULL)
    {
        ssSetErrorStatus(S, "Couldn't retrieve confArray from parameters");
        return;
    }

    int res = aerosimInitializeKafkaConsumer(&rk, brokers, group, topic, nConf, nTopicConf, confArray, start_offset);
    if (res)
    {
        ssSetErrorStatus(S, "Problems initializing Kafka Consumer\n");
        goto exit_init_kafka;
    }
    ssSetPWorkValue(S, p_work_idx, rk);

exit_init_kafka:
    if (brokers != NULL)
    {
        free(brokers);
    }
    if (conf != NULL)
    {
        rd_kafka_conf_destroy(conf);
    }
}

/**
 * @brief Check if the orchestrator.command matches the desired command
 *
 * @param msg Orchestrator command message
 * @param command Desired command
 * @return true if orchestrator message command matches desired command
 * @return false if orchestrator message command does not match desired command
 */
bool isOrchestratorCommand(char* msg, const char* command) {
    bool rtn = false;

    // Parse and load `data` section of data JSON string
    json_t *root = NULL;  // JSON root of message
    json_t *root_data = NULL;  // JSON root of nested data object
    json_error_t error;
    root = json_loads(msg, 0, &error);

    if(!root) {
        mexPrintf("Received Orchestrator message:\n%s\n", msg);
        mexPrintf("Error parsing Orchestrator message: %s ...\n", error.text);
        return false;
    }

    json_t *msg_data_ref = json_object_get(root, "data");
    json_t *data_str_ref = json_object_get(msg_data_ref, "data");
    const char *data_str = json_string_value(data_str_ref);
    root_data = json_loads(data_str, 0, &error);

    // Parse `command` and check if desired command is received
    if(json_is_object(root_data)) {
        json_t *command_obj = json_object_get(root_data, "command");
        if(json_is_string(command_obj)) {
            mexPrintf("Received orchestrator.command: %s\n", json_string_value(command_obj));
            if(strcmp(json_string_value(command_obj), command) == 0) {
                rtn = true;
            }
        }
    }

    // Free memory and return
    json_decref(root_data);
    json_decref(root);
    return rtn;
}

/*====================*
 * S-function methods *
 *====================*/

/* Function: mdlInitializeSizes ===============================================
 * Abstract:
 *    The sizes information is used by Simulink to determine the S-function
 *    block's characteristics (number of inputs, outputs, states, etc.).
 */
static void mdlInitializeSizes(SimStruct *S)
{
    int_T numOutports = 5;
    DTypeId f64_id;

    // printSimMode(S, "mdlInitializeSizes");

    ssSetNumSFcnParams(S, EP_NumParams); /* Number of expected parameters */
    if (ssGetNumSFcnParams(S) != ssGetSFcnParamsCount(S))
        return;

    if (P_OUTPUT_TIMESTAMP != 0)
    {
        numOutports += 1;
    }
    ssSetSFcnParamNotTunable(S, EP_BROKERS);
    ssSetSFcnParamNotTunable(S, EP_START_CMD_TIMEOUT);
    ssSetSFcnParamNotTunable(S, EP_CLOCK_MSG_TIMEOUT);
    ssSetSFcnParamNotTunable(S, EP_MSG_LEN);
    ssSetSFcnParamNotTunable(S, EP_KEY_LEN);
    ssSetSFcnParamNotTunable(S, EP_OUTPUT_TIMESTAMP);
    ssSetSFcnParamNotTunable(S, EP_CONF);
    ssSetSFcnParamNotTunable(S, EP_TOPIC_CONF);
    ssSetSFcnParamNotTunable(S, EP_COMBINED_CONF_STR);
    ssSetSFcnParamNotTunable(S, EP_TS);

    ssSetNumContStates(S, 0);
    ssSetNumDiscStates(S, 0);

    if (!ssSetNumInputPorts(S, 0))
        return;

    if (!ssSetNumOutputPorts(S, numOutports))
        return;
    // The function call output
    ssSetOutputPortWidth(S, 0, 1);
    ssSetOutputPortDataType(S, 0, SS_FCN_CALL);
    // The real message
    ssSetOutputPortWidth(S, 1, P_MSG_LEN);
    ssSetOutputPortDataType(S, 1, SS_INT8);
    // The message length
    ssSetOutputPortWidth(S, 2, 1);
    ssSetOutputPortDataType(S, 2, SS_UINT32);
    // The key
    ssSetOutputPortWidth(S, 3, P_KEY_LEN);
    ssSetOutputPortDataType(S, 3, SS_INT8);
    // The key length
    ssSetOutputPortWidth(S, 4, 1);
    ssSetOutputPortDataType(S, 4, SS_UINT32);

    if (P_OUTPUT_TIMESTAMP != 0)
    {
        // The timestamp
        f64_id =
            ssRegisterDataTypeFxpBinaryPoint(S,
                                             1,  // int isSigned,
                                             64, // int wordLength,
                                             0,  // int fractionLength,
                                             0   // int obeyDataTypeOverride
            );

        if (f64_id == INVALID_DTYPE_ID)
        {
            ssSetErrorStatus(S, "Couldn't register f64 datatype");
            return;
        }
        ssSetOutputPortWidth(S, 5, 1);
        ssSetOutputPortDataType(S, 5, f64_id);
    }
    ssSetNumSampleTimes(S, 1);
    ssSetNumRWork(S, 0);
    ssSetNumIWork(S, 0);
    ssSetNumPWork(S, EPW_NumPWorks);
    ssSetNumModes(S, 0);
    ssSetNumNonsampledZCs(S, 0);

    /* Specify the sim state compliance to be same as a built-in block */
    ssSetSimStateCompliance(S, USE_DEFAULT_SIM_STATE);

    ssSetOptions(S,
                 SS_OPTION_CALL_TERMINATE_ON_EXIT);
}

/* Function: mdlInitializeSampleTimes =========================================
 * Abstract:
 *    This function is used to specify the sample time(s) for your
 *    S-function. You must register the same number of sample times as
 *    specified in ssSetNumSampleTimes.
 */
static void mdlInitializeSampleTimes(SimStruct *S)
{
    real_T *pr, ts, offset = 0.0;
    // printSimMode(S, "mdlInitializeSampleTimes");
    pr = mxGetPr(P_TS);
    ts = pr[0];
    if (mxGetNumberOfElements(P_TS) > 1)
    {
        offset = pr[1];
    }
    ssSetSampleTime(S, 0, ts);
    ssSetOffsetTime(S, 0, offset);

    ssSetCallSystemOutput(S, 0); /* call on first element */
    ssSetModelReferenceSampleTimeDefaultInheritance(S);
}

#define MDL_START /* Change to #undef to remove function */
#if defined(MDL_START)
/* Function: mdlStart =======================================================
 * Abstract:
 *    This function is called once at start of model execution. If you
 *    have states that should be initialized once, this is the place
 *    to do it.
 */
static void mdlStart(SimStruct *S)
{
    if (ssGetSimMode(S) == SS_SIMMODE_NORMAL)
    {
        // Only initialize Kafka when we're actually running in Simulink
        // printSimMode(S, "mdlStart");

        // Initialize Kafka consumer for orchestrator.commands and clock
        initKafkaConsumer(S, "aerosim.clock", "aerosim.simulink", EPW_CLOCK_CONSUMER);
        initKafkaConsumer(S, "aerosim.orchestrator.commands", "aerosim.simulink", EPW_ORCHESTRATOR_CONSUMER);

        // Initialize sim_start_status
        int* sim_start_status = (int*)malloc(sizeof(int));
        *sim_start_status = 0;
        ssSetPWorkValue(S, EPW_SIM_START_STATUS, sim_start_status);

        // Initialize orchestrator message and key varibles
        int8_T* orchestrator_msg = (int8_T*)malloc(sizeof(int8_T) * P_MSG_LEN);
        int8_T* orchestrator_key = (int8_T*)malloc(sizeof(int8_T) * P_KEY_LEN);
        memset(orchestrator_msg, 0, sizeof(int8_T) * P_MSG_LEN);
        memset(orchestrator_key, 0, sizeof(int8_T) * P_KEY_LEN);
        ssSetPWorkValue(S, EPW_ORCHESTRATOR_MSG, orchestrator_msg);
        ssSetPWorkValue(S, EPW_ORCHESTRATOR_KEY, orchestrator_key);

        mexPrintf("Waiting for orchestrator start command (%.1lf sec timeout)...\n", P_START_CMD_TIMEOUT);
    }
}
#endif /*  MDL_START */

/* Function: mdlOutputs =======================================================
 * Abstract:
 *    In this function, you compute the outputs of your S-function
 *    block.
 */
static void mdlOutputs(SimStruct *S, int_T tid)
{
    if (!ssIsMajorTimeStep(S))
    {
        // Only trigger synchronization on major time steps
        return;
    }

    // Retrieve sim_start_status from p-work-vector
    // 0 = Waiting for orchestrator start command;  1 = Orchestrator start command received;  -1 = Time-out
    int* sim_start_status = (int*)ssGetPWorkValue(S, EPW_SIM_START_STATUS);

    // Wait for Orchestrator start command
    if(*sim_start_status == 0) {
        // Initialize timeout variables for orchestrator start command
        const double TIMEOUT_SEC = (P_START_CMD_TIMEOUT == -1) ? DBL_MAX : P_START_CMD_TIMEOUT;
        double elapsed_sec = 0.0;
        clock_t start = clock();
        clock_t end = start;

        // Retrieve orchestrator.command Kafka consumer handle and message/key data buffers
        rd_kafka_t *rk = (rd_kafka_t *)ssGetPWorkValue(S, EPW_ORCHESTRATOR_CONSUMER);
        int8_T* orchestrator_msg = (int8_T*)ssGetPWorkValue(S, EPW_ORCHESTRATOR_MSG);
        int8_T* orchestrator_key = (int8_T*)ssGetPWorkValue(S, EPW_ORCHESTRATOR_KEY);
        uint32_T orchestrator_msgLen = 0;
        uint32_T orchestrator_keyLen = 0;

        while (elapsed_sec < TIMEOUT_SEC) {
            // Returns ret = 0 if no message, ret = 1 if msg was received.
            int ret = mwConsumeKafkaMessage(rk, orchestrator_msg, &orchestrator_msgLen, P_MSG_LEN,
                                            orchestrator_key, &orchestrator_keyLen, P_KEY_LEN, NULL);

            if (ret == 0) {
                // Keep waiting for the orchestrator command message
                Sleep(0.01);
                end = clock();
                elapsed_sec = ((double)(end - start)) / CLOCKS_PER_SEC;
                continue;
            }

            // Orchestrator command message received, break if `start` command is received
            if(isOrchestratorCommand(orchestrator_msg, "start")) {
                mexPrintf("Orchestrator start command received... Sending initial sync message\n");
                *sim_start_status = 1;
                mexPrintf("Starting simulation ...\n");
                break;
            }

            // Orchestrator command is not a start command, clear data buffer and re-try
            memset(orchestrator_msg, 0, sizeof(int8_T) * P_MSG_LEN);
            memset(orchestrator_key, 0, sizeof(int8_T) * P_KEY_LEN);
        }

        // Orchestrator start command timed-out, stop simulation
        if(*sim_start_status != 1) {
            mexPrintf("Orchestrator start command was not received after %.1lf seconds... stopping simulation\n", TIMEOUT_SEC);
            *sim_start_status = -1;
            ssSetStopRequested(S, 1);
        }

    }
    // Orchestrator start command received, start receiving aerosim.clock message
    else if(*sim_start_status == 1) {
        // 1. Block in a polling loop to wait for the target aerosim.clock message tick group
        int ret = 0;
        rd_kafka_t *rk = (rd_kafka_t *)ssGetPWorkValue(S, EPW_CLOCK_CONSUMER);

        int8_T *msg = (int8_T *)ssGetOutputPortSignal(S, 1);
        uint32_T *msgLen = (uint32_T *)ssGetOutputPortSignal(S, 2);
        int8_T *key = (int8_T *)ssGetOutputPortSignal(S, 3);
        uint32_T *keyLen = (uint32_T *)ssGetOutputPortSignal(S, 4);

        int64_T *timestamp = NULL;
        if (P_OUTPUT_TIMESTAMP)
        {
            timestamp = (int64_T *)ssGetOutputPortSignal(S, 5);
        }

        const double TIMEOUT_SEC = (P_CLOCK_MSG_TIMEOUT == -1) ? DBL_MAX : P_CLOCK_MSG_TIMEOUT;
        double elapsed_sec = 0.0;
        clock_t start = clock();
        clock_t end = start;

        // Retrieve and setup Orchestrator variables
        int orchestrator_ret = 0;
        bool is_orchestrator_stop_cmd = false;
        rd_kafka_t *orchestrator_rk = (rd_kafka_t *)ssGetPWorkValue(S, EPW_ORCHESTRATOR_CONSUMER);
        int8_T* orchestrator_msg = (int8_T*)ssGetPWorkValue(S, EPW_ORCHESTRATOR_MSG);
        int8_T* orchestrator_key = (int8_T*)ssGetPWorkValue(S, EPW_ORCHESTRATOR_KEY);
        memset(orchestrator_msg, 0, sizeof(int8_T) * P_MSG_LEN);
        memset(orchestrator_key, 0, sizeof(int8_T) * P_KEY_LEN);
        uint32_T orchestrator_msgLen = 0;
        uint32_T orchestrator_keyLen = 0;


        while (elapsed_sec < TIMEOUT_SEC)
        {
            // Check for orchestrator stop command
            orchestrator_ret = mwConsumeKafkaMessage(orchestrator_rk, orchestrator_msg, &orchestrator_msgLen, P_MSG_LEN,
                                                    orchestrator_key, &orchestrator_keyLen, P_KEY_LEN, NULL);

            if(orchestrator_ret != 0) {
                // Orchestrator command message received, break if `stop` command is received
                if(isOrchestratorCommand(orchestrator_msg, "stop")) {
                    mexPrintf("Orchestrator stop command received... stopping simulation\n");
                    is_orchestrator_stop_cmd = true;
                    break;
                }

                // Orchestrator command is not a stop command, clear data buffer and re-try
                memset(orchestrator_msg, 0, sizeof(int8_T) * P_MSG_LEN);
                memset(orchestrator_key, 0, sizeof(int8_T) * P_KEY_LEN);
            }

            // Returns ret = 0 if no message, ret = 1 if msg was received.
            // TODO Modify mwConsumeKafkaMessage() to take the poll timeout as a parameter.
            ret = mwConsumeKafkaMessage(rk, msg, msgLen, P_MSG_LEN,
                                        key, keyLen, P_KEY_LEN, timestamp);

            if (ret == 0)
            {
                // Keep waiting for the simclock message that allows the next tick.
                end = clock();
                elapsed_sec = ((double)(end - start)) / CLOCKS_PER_SEC;
                continue;
            }

            // Call the subsystem attached
            if (!ssCallSystemWithTid(S, 0, tid))
            {
                /* Error occurred which will be reported by Simulink */
                return;
            }

            // aerosim.clock message received, exit while loop
            break;
        }

        if(is_orchestrator_stop_cmd) {
            // Orchestrator stop command received, stop simulation
            ssSetStopRequested(S, 1);
        } else if(ret == 0) {
            // aerosim.clock message receiver timed-out, stop simulation
            mexPrintf("aerosim.clock message was not received after %.1lf seconds... stopping simulation\n", TIMEOUT_SEC);
            ssSetStopRequested(S, 1);
        }
    }
}

/* Function: mdlTerminate =====================================================
 * Abstract:
 *    In this function, you should perform any actions that are necessary
 *    at the termination of a simulation.  For example, if memory was
 *    allocated in mdlStart, this is the place to free it.
 */
static void mdlTerminate(SimStruct *S)
{
    if (ssGetSimMode(S) == SS_SIMMODE_NORMAL)
    {
        // We only need to terminate properly in normal simulation mode
        static int verbose = 1;
        void **pwp = ssGetPWork(S);
        if (pwp == NULL)
        {
            // This was just a model update, no need to free resources.
            return;
        }
        mexPrintf("sl_aerosim_clock_sync@mdlTerminate(): Freeing up used resources\n");

        rd_kafka_t *rk = (rd_kafka_t *)ssGetPWorkValue(S, EPW_CLOCK_CONSUMER);
        mwTerminateKafkaConsumer(rk);

        rd_kafka_t *orchestrator_rk = (rd_kafka_t *)ssGetPWorkValue(S, EPW_ORCHESTRATOR_CONSUMER);
        mwTerminateKafkaConsumer(orchestrator_rk);

        int* sim_start_status = (int*) ssGetPWorkValue(S, EPW_SIM_START_STATUS);
        free(sim_start_status);

        int8_T* orchestrator_msg = (int8_T*)ssGetPWorkValue(S, EPW_ORCHESTRATOR_MSG);
        free(orchestrator_msg);

        int8_T* orchestrator_key = (int8_T*)ssGetPWorkValue(S, EPW_ORCHESTRATOR_KEY);
        free(orchestrator_key);

        ssSetPWorkValue(S, EPW_CLOCK_CONSUMER, NULL);
        ssSetPWorkValue(S, EPW_ORCHESTRATOR_CONSUMER, NULL);
        ssSetPWorkValue(S, EPW_SIM_START_STATUS, NULL);
        ssSetPWorkValue(S, EPW_ORCHESTRATOR_MSG, NULL);
        ssSetPWorkValue(S, EPW_ORCHESTRATOR_KEY, NULL);
    }
}

#define MDL_RTW /* Change to #undef to remove function */
#if defined(MDL_RTW) && defined(MATLAB_MEX_FILE)
static void mdlRTW(SimStruct *S)
{
    char *brokers = NULL, *group = NULL, *topic = NULL, *confArray = NULL;
    int32_T nConf = mxGetNumberOfElements(P_CONF);
    int32_T nTopicConf = mxGetNumberOfElements(P_TOPIC_CONF);

    if (getParamString(S, &brokers, P_BROKER, -1, "brokers"))
        goto sl_aerosim_clock_sync_mdl_rtw_exit;
    if (getParamString(S, &confArray, P_COMBINED_CONF_STR, -1, "confArray"))
        goto sl_aerosim_clock_sync_mdl_rtw_exit;
    if (getParamString(S, &brokers, P_BROKER, -1, "brokers"))
        return;

    if (!ssWriteRTWParamSettings(S, 6,
                                 SSWRITE_VALUE_QSTR, "Brokers", (const void *)brokers,
                                 SSWRITE_VALUE_DTYPE_NUM, "nConf", (const void *)&nConf, SS_INT32,
                                 SSWRITE_VALUE_DTYPE_NUM, "nTopicConf", (const void *)&nTopicConf, SS_INT32,
                                 SSWRITE_VALUE_VECT_STR, "ConfArray", (const char_T *)confArray, nConf + nTopicConf))
    {
        return; // (error reporting will be handled by SL)
    }
sl_aerosim_clock_sync_mdl_rtw_exit:
    if (brokers != NULL)
        free(brokers);
    if (confArray != NULL)
        free(confArray);

    // static int getParamString(SimStruct *S, char **strPtr, const mxArray *prm, int epwIdx, char *errorHelp)
}
#endif /* MDL_RTW */

/*=============================*
 * Required S-function trailer *
 *=============================*/

#ifdef MATLAB_MEX_FILE /* Is this file being compiled as a MEX-file? */
#include "simulink.c"  /* MEX-file interface mechanism */
#else
#include "cg_sfun.h" /* Code generation registration function */
#endif
