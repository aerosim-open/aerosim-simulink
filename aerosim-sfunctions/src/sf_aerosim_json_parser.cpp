/*
 * sf_aerosim_json_parser
 *
 * Based on the MathWorks reference JSON decoder S-function sf_decode_flat_json_object.cpp
 * Copyright 2019 The MathWorks, Inc.
 */

#define S_FUNCTION_NAME sf_aerosim_json_parser
#define S_FUNCTION_LEVEL 2

#include <chrono>
#include <cmath>

#include "simstruc.h"
#include "fixedpoint.h"

// Needed for JSON decoding
#include "jansson.h"

enum
{
    EP_JSON_ENCODE = 0,
    EP_JSON_LEN,
    EP_OUT_LENGTH,
    EP_IN_LENGTH,
    EP_STRING_LIST,
    EP_STRING_LIST_TYPE,
    EP_STRING_LIST_RTW,
    EP_NumParams
};

typedef enum
{
    SF_DIR_DECODE = 1,
    SF_DIR_ENCODE
} SFCodeDir_T;

#define P_JSON_ENCODE ((int_T)mxGetScalar((ssGetSFcnParam(S, EP_JSON_ENCODE))))
#define P_JSON_LEN ((int_T)mxGetScalar((ssGetSFcnParam(S, EP_JSON_LEN))))
#define P_OUT_LENGTH ((int_T)mxGetScalar((ssGetSFcnParam(S, EP_OUT_LENGTH))))
#define P_IN_LENGTH ((int_T)mxGetScalar((ssGetSFcnParam(S, EP_IN_LENGTH))))
#define P_STRING_LIST (ssGetSFcnParam(S, EP_STRING_LIST))
#define P_STRING_LIST_TYPE (ssGetSFcnParam(S, EP_STRING_LIST_TYPE))
#define P_STRING_LIST_RTW (ssGetSFcnParam(S, EP_STRING_LIST_RTW))

static char errstr[512];

static void populateJSONObject(const char *fieldName, const char *fieldType, json_t *obj, SimStruct *S, int k) {
    json_t *curr_obj = obj;

    // String tokenize fieldName by '.' notation and retrieve last_token, which is the target variable name
    char* token_str = strdup(fieldName);
    char* last_token = strrchr(token_str, '.')+1;
    char* token = strtok(token_str, ".");
    token = strtok(NULL, ".");
    while(token) {
        // Traverse/create the json object tree until it reaches the target variable
        json_t *child_obj = json_object_get(curr_obj, token);
        if (!child_obj) {
            if(strcmp(token, last_token) == 0) {
                break;
            }
            child_obj = json_object();
            json_object_set_new(curr_obj, token, child_obj);
        }
        token = strtok(NULL, ".");
        curr_obj = child_obj;
    }

    // Create and set the target json variable according to the field type
    if(strcmp(fieldType, "double") == 0)        json_object_set_new(curr_obj, token, json_real(         ((real_T*)ssGetInputPortSignal(S, k))[0])   );
    else if(strcmp(fieldType, "single") == 0)   json_object_set_new(curr_obj, token, json_real(         ((real32_T*)ssGetInputPortSignal(S, k))[0]) );
    else if (strcmp(fieldType, "int8") == 0)    json_object_set_new(curr_obj, token, json_integer(      ((int8_T*)ssGetInputPortSignal(S, k))[0])   );
    else if (strcmp(fieldType, "uint8") == 0)   json_object_set_new(curr_obj, token, json_integer(      ((uint8_T*)ssGetInputPortSignal(S, k))[0])  );
    else if (strcmp(fieldType, "int16") == 0)   json_object_set_new(curr_obj, token, json_integer(      ((int16_T*)ssGetInputPortSignal(S, k))[0])  );
    else if (strcmp(fieldType, "uint16") == 0)  json_object_set_new(curr_obj, token, json_integer(      ((uint16_T*)ssGetInputPortSignal(S, k))[0]) );
    else if (strcmp(fieldType, "int32") == 0)   json_object_set_new(curr_obj, token, json_integer(      ((int32_T*)ssGetInputPortSignal(S, k))[0])  );
    else if (strcmp(fieldType, "uint32") == 0)  json_object_set_new(curr_obj, token, json_integer(      ((uint32_T*)ssGetInputPortSignal(S, k))[0]) );
    else if (strcmp(fieldType, "int64") == 0)   json_object_set_new(curr_obj, token, json_integer(      ((int64_T*)ssGetInputPortSignal(S, k))[0])  );
    else if (strcmp(fieldType, "uint64") == 0)  json_object_set_new(curr_obj, token, json_integer(      ((uint64_T*)ssGetInputPortSignal(S, k))[0]) );
    else if (strcmp(fieldType, "bool") == 0)    json_object_set_new(curr_obj, token, json_boolean(      ((boolean_T*)ssGetInputPortSignal(S, k))[0]));
    else if (strcmp(fieldType, "string") == 0)  json_object_set_new(curr_obj, token, json_string(       (char*)ssGetInputPortSignal(S, k))          );

    free(token_str);
}

static void getDataFromJSONField(const char *fieldName, json_t *obj, real_T* rtn_number, char* rtn_str, bool* rtn_bool)
{
    json_t *curr_obj = obj;

    // String tokenize fieldName by '.' notation
    char* token_str = strdup(fieldName);
    char* token = strtok(token_str, ".");
    token = strtok(NULL, ".");
    while(token) {
        // Keep searching for the leaf node down the JSON tree
        curr_obj = json_object_get(curr_obj, token);
        if (!curr_obj) {
            mexPrintf("No such JSON field - fieldName: %s, token: %s\n", fieldName, token);
            free(token_str);
            return;
        }
        token = strtok(NULL, ".");
    }

    // Set return value based on type
    if(json_is_number(curr_obj)) {
        *rtn_number = json_number_value(curr_obj);
    } else if(json_is_boolean(curr_obj)) {
        *rtn_bool = (bool)json_boolean_value(curr_obj);
    } else if(json_is_string(curr_obj)) {
        sprintf(rtn_str, "%s", json_string_value(curr_obj));
    } else {
        mexPrintf("Bad type for JSON value - fieldName: %s\n", fieldName);
    }

    free(token_str);
    return;
}

static char *getStringFromParamCellString(SimStruct *S, const mxArray *P, int idx)
{
    static char gsfpErr[1024];

    if (mxGetClassID(P) != mxCELL_CLASS)
    {
        sprintf(gsfpErr, "The parameter must be a cell array\n");
        ssSetErrorStatus(S, gsfpErr);
        return NULL;
    }

    const mxArray *Pel = mxGetCell(P, idx);
    if (mxGetClassID(Pel) != mxCHAR_CLASS)
    {
        sprintf(gsfpErr, "All elements must be character arrays. Element [%d] isn't.\n", idx);
        ssSetErrorStatus(S, gsfpErr);
        return NULL;
    }

    mwSize N = (mwSize)1 + mxGetNumberOfElements(Pel);
    char *newStr = new char[N];
    if (newStr == NULL)
    {
        sprintf(gsfpErr, "Couldn't allocate memory for element [%d]\n", idx);
        ssSetErrorStatus(S, gsfpErr);
        return NULL;
    }

    if (mxGetString(Pel, newStr, N))
    {
        delete[] newStr;
        newStr = NULL;
        sprintf(gsfpErr, "Couldn't read string element [%d]]\n", idx);
        ssSetErrorStatus(S, gsfpErr);
    }
    return newStr;
}

static int getParamString(SimStruct *S, char **strPtr, const mxArray *prm, int epwIdx, char *errorHelp)
{
    int N = (int)mxGetNumberOfElements(prm) + 1;
    char *tmp = new char[N];
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
    ssSetPWorkValue(S, epwIdx, tmp);
    *strPtr = tmp;

    return 0;
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
    ssSetNumSFcnParams(S, EP_NumParams); /* Number of expected parameters */
    if (ssGetNumSFcnParams(S) != ssGetSFcnParamsCount(S))
    {
        /* Return if number of expected != number of actual parameters */
        return;
    }

    int k, numFields = mxGetNumberOfElements(P_STRING_LIST);

    ssSetSFcnParamNotTunable(S, EP_JSON_ENCODE);
    ssSetSFcnParamNotTunable(S, EP_JSON_LEN);
    ssSetSFcnParamNotTunable(S, EP_OUT_LENGTH);
    ssSetSFcnParamNotTunable(S, EP_IN_LENGTH);
    ssSetSFcnParamNotTunable(S, EP_STRING_LIST);
    ssSetSFcnParamNotTunable(S, EP_STRING_LIST_TYPE);
    ssSetSFcnParamNotTunable(S, EP_STRING_LIST_RTW);

    ssSetNumContStates(S, 0);
    ssSetNumDiscStates(S, 0);

    if (P_JSON_ENCODE == SF_DIR_DECODE)
    {
        // DECODING
        int nIn = (P_IN_LENGTH != 0) ? 2 : 1;
        if (!ssSetNumInputPorts(S, nIn))
            return;
        ssSetInputPortDataType(S, 0, SS_INT8);
        ssSetInputPortWidth(S, 0, P_JSON_LEN);
        ssSetInputPortRequiredContiguous(S, 0, true); /*direct input signal access*/
        ssSetInputPortDirectFeedThrough(S, 0, 1);

        if (P_IN_LENGTH != 0)
        {
            ssSetInputPortDataType(S, 1, SS_UINT32);
            ssSetInputPortWidth(S, 1, 1);
            ssSetInputPortRequiredContiguous(S, 1, true); /*direct input signal access*/
            ssSetInputPortDirectFeedThrough(S, 1, 1);
        }

        if (!ssSetNumOutputPorts(S, numFields))
            return;

        // Create uint64 and int64 types
        DTypeId dtId_Int64  = ssRegisterDataTypeInteger(S,1,64,0);
        DTypeId dtId_Uint64 = ssRegisterDataTypeInteger(S,0,64,0);

        // Assign output port type and width
        for (k = 0; k < numFields; ++k)
        {
            // Get field type
            const char *fieldType = (const char *)getStringFromParamCellString(S, P_STRING_LIST_TYPE, k);
            // Default all port width to 1
            ssSetOutputPortWidth(S, k, 1);
            // Assign port type
            if(strcmp(fieldType, "double") == 0) ssSetOutputPortDataType(S, k, SS_DOUBLE);
            else if(strcmp(fieldType, "single") == 0) ssSetOutputPortDataType(S, k, SS_SINGLE);
            else if (strcmp(fieldType, "int8") == 0) ssSetOutputPortDataType(S, k, SS_INT8);
            else if (strcmp(fieldType, "uint8") == 0) ssSetOutputPortDataType(S, k, SS_UINT8);
            else if (strcmp(fieldType, "int16") == 0) ssSetOutputPortDataType(S, k, SS_INT16);
            else if (strcmp(fieldType, "uint16") == 0) ssSetOutputPortDataType(S, k, SS_UINT16);
            else if (strcmp(fieldType, "int32") == 0) ssSetOutputPortDataType(S, k, SS_INT32);
            else if (strcmp(fieldType, "uint32") == 0) ssSetOutputPortDataType(S, k, SS_UINT32);
            else if (strcmp(fieldType, "int64") == 0) ssSetOutputPortDataType(S, k, dtId_Int64);
            else if (strcmp(fieldType, "uint64") == 0) ssSetOutputPortDataType(S, k, dtId_Uint64);
            else if (strcmp(fieldType, "bool") == 0) ssSetOutputPortDataType(S, k, SS_BOOLEAN);
            else if (strcmp(fieldType, "string") == 0) {
                // Assign port width to P_JSON_LEN for string type
                ssSetOutputPortDataType(S, k, SS_UINT8);
                ssSetOutputPortWidth(S, k, P_JSON_LEN);
            }

            delete[] fieldType;
        }
    }
    else
    {
        // ENCODING
        int nOut = (P_OUT_LENGTH != 0) ? 2 : 1;
        if (!ssSetNumInputPorts(S, numFields))
            return;

        // Create uint64 and int64 types
        DTypeId dtId_Int64  = ssRegisterDataTypeInteger(S,1,64,0);
        DTypeId dtId_Uint64 = ssRegisterDataTypeInteger(S,0,64,0);

        for (k = 0; k < numFields; ++k)
        {
            // Get field type
            const char *fieldType = (const char *)getStringFromParamCellString(S, P_STRING_LIST_TYPE, k);
            // Default all port width to 1
            ssSetInputPortWidth(S, k, 1);
            // Assign port type
            if(strcmp(fieldType, "double") == 0) ssSetInputPortDataType(S, k, SS_DOUBLE);
            else if(strcmp(fieldType, "single") == 0) ssSetInputPortDataType(S, k, SS_SINGLE);
            else if (strcmp(fieldType, "int8") == 0) ssSetInputPortDataType(S, k, SS_INT8);
            else if (strcmp(fieldType, "uint8") == 0) ssSetInputPortDataType(S, k, SS_UINT8);
            else if (strcmp(fieldType, "int16") == 0) ssSetInputPortDataType(S, k, SS_INT16);
            else if (strcmp(fieldType, "uint16") == 0) ssSetInputPortDataType(S, k, SS_UINT16);
            else if (strcmp(fieldType, "int32") == 0) ssSetInputPortDataType(S, k, SS_INT32);
            else if (strcmp(fieldType, "uint32") == 0) ssSetInputPortDataType(S, k, SS_UINT32);
            else if (strcmp(fieldType, "int64") == 0) ssSetInputPortDataType(S, k, dtId_Int64);
            else if (strcmp(fieldType, "uint64") == 0) ssSetInputPortDataType(S, k, dtId_Uint64);
            else if (strcmp(fieldType, "bool") == 0) ssSetInputPortDataType(S, k, SS_BOOLEAN);
            else if (strcmp(fieldType, "string") == 0) {
                // Assign port width to P_JSON_LEN for string type
                ssSetInputPortDataType(S, k, SS_UINT8);
                ssSetInputPortWidth(S, k, P_JSON_LEN);
            }

            ssSetInputPortRequiredContiguous(S, k, true); /*direct input signal access*/
            ssSetInputPortDirectFeedThrough(S, k, 1);

            delete[] fieldType;
        }

        if (!ssSetNumOutputPorts(S, nOut))
            return;
        ssSetOutputPortWidth(S, 0, P_JSON_LEN);
        ssSetOutputPortDataType(S, 0, SS_INT8);
        if (P_OUT_LENGTH != 0)
        {
            ssSetOutputPortWidth(S, 1, 1);
            ssSetOutputPortDataType(S, 1, SS_UINT32);
        }
    }

    ssSetNumSampleTimes(S, 1);
    ssSetNumRWork(S, 0);
    ssSetNumIWork(S, 0);
    if (ssRTWGenIsCodeGen(S))
    {
        ssSetNumPWork(S, 0);
    }
    else
    {
        ssSetNumPWork(S, numFields);
    }
    ssSetNumModes(S, 0);
    ssSetNumNonsampledZCs(S, 0);

    /* Specify the sim state compliance to be same as a built-in block */
    ssSetSimStateCompliance(S, USE_DEFAULT_SIM_STATE);

    ssSetOptions(S, 0 | SS_OPTION_CALL_TERMINATE_ON_EXIT);
}

/* Function: mdlInitializeSampleTimes =========================================
 * Abstract:
 *    This function is used to specify the sample time(s) for your
 *    S-function. You must register the same number of sample times as
 *    specified in ssSetNumSampleTimes.
 */
static void mdlInitializeSampleTimes(SimStruct *S)
{
    ssSetSampleTime(S, 0, INHERITED_SAMPLE_TIME);
    ssSetOffsetTime(S, 0, 0.0);
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
    if (!ssRTWGenIsCodeGen(S))
    {
        int k, numFields = mxGetNumberOfElements(P_STRING_LIST);
        for (k = 0; k < numFields; ++k)
        {
            ssSetPWorkValue(S, k, getStringFromParamCellString(S, P_STRING_LIST, k));
            // mexPrintf("Found string '%s'\n", ssGetPWorkValue(S, k));
        }
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
    if (P_JSON_ENCODE == SF_DIR_DECODE)
    {
        // DECODING
        json_t *root = NULL;        // JSON root of message
        json_t *root_data = NULL;   // JSON root of nested data object
        json_error_t error;         // JSON error object

        // Get JSON string from input port
        const char *u = (const char *)ssGetInputPortSignal(S, 0);
        int k, numFields = mxGetNumberOfElements(P_STRING_LIST);

        // Load a new JSON object from the input as the root
        if (P_IN_LENGTH != 0) {
            // Block is configured to use an input port to set the length of the input
            uint32_T *pu = (uint32_T *)ssGetInputPortSignal(S, 1);
            uint32_T len = *pu;
            root = json_loadb(u, len, 0, &error);
        } else {
            // Block is configured to use the input string to determine its length
            root = json_loads(u, 0, &error);
        }

        // Discard empty JSON string or bad JSON (May need a warning)
        if(!root) {
            return;
        }

        // Temp variable for storing return data from getDataFromJSONField()
        real_T rtn_number = 0.0;
        char* rtn_str = new char[P_JSON_LEN];
        bool rtn_bool = false;

        // Retrieve root metadata and parse `type_name`
        json_t *root_metadata = json_object_get(root, "metadata");
        getDataFromJSONField("metadata.type_name", root_metadata, &rtn_number, rtn_str, &rtn_bool);

        // Retrieve root_data based on `type_name`
        if(strcmp(rtn_str, "aerosim::types::JsonData") == 0) {
            // Intermediate json_t references for retrieving root data (example below)
            //   {"metadata":{"topic":"aerosim.simulink_out","type_name":"aerosim::types::JsonData","timestamp_sim":{"sec":0,"nanosec":0},"timestamp_platform":{"sec":1739720382,"nanosec":432190100}},"data":{"data":"{\"position.z\":0.0}"}}
            json_t *msg_data_ref = json_object_get(root, "data");
            json_t *data_str_ref = json_object_get(msg_data_ref, "data");
            const char *data_str = json_string_value(data_str_ref);
            root_data = json_loads(data_str, 0, &error);
        } else {
            root_data = json_object_get(root, "data");
        }

        // Retrieve JSON data and set output port signals
        if (json_is_object(root_metadata) && json_is_object(root_data))
        {
            for (k = 0; k < numFields; ++k) {
                const char *fieldName = (const char *)ssGetPWorkValue(S, k);
                const char *fieldType = (const char *)getStringFromParamCellString(S, P_STRING_LIST_TYPE, k);

                // Retrieve JSON data
                if(strncmp(fieldName, "metadata", 8) == 0) {
                    getDataFromJSONField(fieldName, root_metadata, &rtn_number, rtn_str, &rtn_bool);
                } else {
                    getDataFromJSONField(fieldName, root_data, &rtn_number, rtn_str, &rtn_bool);
                }

                // Set output signal based on output type
                if(strcmp(fieldType, "double") == 0)        { real_T *Y = (real_T*)ssGetOutputPortSignal(S, k); Y[0] = (real_T)rtn_number; }
                else if(strcmp(fieldType, "single") == 0)   { real32_T *Y = (real32_T*)ssGetOutputPortSignal(S, k); Y[0] = (real32_T)rtn_number; }
                else if (strcmp(fieldType, "int8") == 0)    { int8_T *Y = (int8_T*)ssGetOutputPortSignal(S, k); Y[0] = (int8_T)rtn_number; }
                else if (strcmp(fieldType, "uint8") == 0)   { uint8_T *Y = (uint8_T*)ssGetOutputPortSignal(S, k); Y[0] = (uint8_T)rtn_number; }
                else if (strcmp(fieldType, "int16") == 0)   { int16_T *Y = (int16_T*)ssGetOutputPortSignal(S, k); Y[0] = (int16_T)rtn_number; }
                else if (strcmp(fieldType, "uint16") == 0)  { uint16_T *Y = (uint16_T*)ssGetOutputPortSignal(S, k); Y[0] = (uint16_T)rtn_number; }
                else if (strcmp(fieldType, "int32") == 0)   { int32_T *Y = (int32_T*)ssGetOutputPortSignal(S, k); Y[0] = (int32_T)rtn_number; }
                else if (strcmp(fieldType, "uint32") == 0)  { uint32_T *Y = (uint32_T*)ssGetOutputPortSignal(S, k); Y[0] = (uint32_T)rtn_number; }
                else if (strcmp(fieldType, "int64") == 0)   { int64_T *Y = (int64_T*)ssGetOutputPortSignal(S, k); Y[0] = (int64_T)rtn_number; }
                else if (strcmp(fieldType, "uint64") == 0)  { uint64_T *Y = (uint64_T*)ssGetOutputPortSignal(S, k); Y[0] = (uint64_T)rtn_number; }
                else if (strcmp(fieldType, "bool") == 0)    { boolean_T *Y = (boolean_T*)ssGetOutputPortSignal(S, k); Y[0] = (boolean_T)rtn_bool; }
                else if (strcmp(fieldType, "string") == 0) {
                    uint8_t *Y = (uint8_t*)ssGetOutputPortSignal(S, k);
                    sprintf((char*)Y, rtn_str);
                }

                delete[] fieldType;
            }
        }

        // Free memory
        delete[] rtn_str;
        json_decref(root_data);
        json_decref(root);
    }
    else
    {
        // ENCODING
        time_T simulinkTime = ssGetT(S);
        const auto simulinkSec = static_cast<int>(std::floor(simulinkTime));
        const auto simulinkNanosec = static_cast<int>(std::round((simulinkTime - simulinkSec) * 1e9));

        const auto duration = std::chrono::system_clock::now().time_since_epoch();
        int platformSec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        int platformNanosec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() % 1000000000;

        int k, numFields = mxGetNumberOfElements(P_STRING_LIST);

        json_t *root_metadata = json_object();
        json_t *root_data = json_object();
        bool is_typename_jsondata = false;

        // Populate metadata and data JSON objects
        for (k = 0; k < numFields; ++k) {
            const char *fieldName = (const char *)ssGetPWorkValue(S, k);
            const char *fieldType = (const char *)getStringFromParamCellString(S, P_STRING_LIST_TYPE, k);

            if(strncmp(fieldName, "metadata", 8) == 0) {
                populateJSONObject(fieldName, fieldType, root_metadata, S, k);
            } else {
                populateJSONObject(fieldName, fieldType, root_data, S, k);
            }

            // Determine if `type_name` is "aerosim::types::JsonData"
            if(strcmp(fieldName, "metadata.type_name") == 0) {
                is_typename_jsondata = (strcmp((char*)ssGetInputPortSignal(S, k), "aerosim::types::JsonData") == 0) ? true : false;
            }

            delete[] fieldType;
        }

        // Create final JSON string
        json_t *root = json_object();
        json_object_set_new(root, "metadata", root_metadata);
        json_t *msg_data_obj = json_object();
        if(is_typename_jsondata) {
            char* data_str = json_dumps(root_data, JSON_COMPACT);
            json_object_set_new(msg_data_obj, "data", json_string(data_str));
            json_object_set_new(root, "data", msg_data_obj);
            free(data_str);
        } else {
            json_object_set_new(root, "data", root_data);
        }
        char* root_str = json_dumps(root, JSON_COMPACT);

        // Retrieve output ports
        int8_T *Y = (int8_T *)ssGetOutputPortSignal(S, 0);
        int root_str_len = strlen(root_str);

        if (root_str_len > P_JSON_LEN)
        {
            ssSetErrorStatus(S, "Max length setting is too small for the encoded message.");
            // Set message output string to empty and message length to 0
            sprintf((char*)Y, "");
            if (P_OUT_LENGTH != 0)
            {
                uint32_T *msgLen = (uint32_T *)ssGetOutputPortSignal(S, 1);
                *msgLen = 0;
            }
        } else {
            // Set message output string and message length
            sprintf((char*)Y, root_str);
            if (P_OUT_LENGTH != 0)
            {
                uint32_T *msgLen = (uint32_T *)ssGetOutputPortSignal(S, 1);
                *msgLen = root_str_len;
            }
        }

        // Free memory
        free(root_str);
        json_decref(root_metadata);
        json_decref(root_data);
        json_decref(msg_data_obj);
        json_decref(root);
    }
}

static void mdlTerminate(SimStruct *S)
{
    void **pwp = ssGetPWork(S);
    if (pwp == NULL)
    {
        // This was just a model update, no need to free resources.
        return;
    }

    int k, numFields = mxGetNumberOfElements(P_STRING_LIST);
    for (k = 0; k < numFields; ++k)
    {
        char *msg = (char *)ssGetPWorkValue(S, k);
        if (msg != NULL)
        {
            delete[] msg;
            ssSetPWorkValue(S, k, NULL);
        }
    }
}

#define MDL_RTW /* Change to #undef to remove function */
#if defined(MDL_RTW) && defined(MATLAB_MEX_FILE)
static void mdlRTW(SimStruct *S)
{
    int N = 1 + mxGetNumberOfElements(P_STRING_LIST_RTW);
    char *str = new char[N];
    int32_T useInLength = (int32_T) P_IN_LENGTH;
    int32_T useOutLength = (int32_T) P_OUT_LENGTH;
    if (str == NULL)
    {
        ssSetErrorStatus(S, "Couldn''t allocate string in mdlRTW");
        return;
    }

    if (mxGetString(P_STRING_LIST_RTW, str, N))
    {
        ssSetErrorStatus(S, "Couldn't read string in mdlRTW");
        delete[] str;
        return;
    }

    int numFields = mxGetNumberOfElements(P_STRING_LIST);

    int32_T isEncoding = P_JSON_ENCODE == SF_DIR_ENCODE;
    if (!ssWriteRTWParamSettings(S, 4,
                                 SSWRITE_VALUE_VECT_STR, "JSONFieldList", (const char_T *)str, numFields,
                                 SSWRITE_VALUE_DTYPE_NUM, "IsEncoding", (const void *)&isEncoding, SS_INT32,
                                 SSWRITE_VALUE_DTYPE_NUM, "UseInLength", (const void *)&useInLength, SS_INT32,
                                 SSWRITE_VALUE_DTYPE_NUM, "UseOutLength", (const void *)&useOutLength, SS_INT32))
    {
        return; // (error reporting will be handled by SL)
    }

    delete[] str;
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
