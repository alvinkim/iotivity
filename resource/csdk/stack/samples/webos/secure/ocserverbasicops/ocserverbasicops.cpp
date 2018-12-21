/*******************************************************************
 *
 * Copyright (c) 2018 LG Electronics, Inc.
 * Copyright 2014 Intel Mobile Communications GmbH All Rights Reserved.
 *
 *-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 */

#include "iotivity_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <signal.h>
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
#include "ocstack.h"
#include "ocpayload.h"
#include "ocserverbasicops.h"
#include "common.h"


/// This example is using experimental API, so there is no guarantee of support for future release,
/// nor any there any guarantee that breaking changes will not occur across releases.
#include "experimental/logger.h"

#include <luna-service2/lunaservice.h>
#include <glib.h>
#include <pbnjson.h>
#include <pthread.h>
#include "logging.h"

pthread_t threadId_server;
PmLogContext gLogContext;
PmLogContext gLogLibContext;

int gQuitFlag = 0;

static LEDResource LED;
// This variable determines instance number of the LED resource.
// Used by POST method to create a new instance of LED resource.
static int gCurrLedInstance = 0;
#define SAMPLE_MAX_NUM_POST_INSTANCE  2
static LEDResource gLedInstance[SAMPLE_MAX_NUM_POST_INSTANCE];

char *gResourceUri= (char *)"/a/led";

//Secure Virtual Resource database for Iotivity Server
//It contains Server's Identity and the PSK credentials
//of other devices which the server trusts
static char CRED_FILE[] = "oic_svr_db_server.dat";

static LSHandle *pLsHandle = NULL;
static GMainLoop *mainloop = NULL;

OCRepPayload* getPayload(const char* uri, int64_t power, bool state)
{
    OCRepPayload* payload = OCRepPayloadCreate();
    if(!payload)
    {
        OCSAMPLE_LOG_ERROR(TAG, 0, "Failed to allocate Payload");
        return nullptr;
    }

    OCRepPayloadSetUri(payload, uri);
    OCRepPayloadSetPropBool(payload, "state", state);
    OCRepPayloadSetPropInt(payload, "power", power);

    return payload;
}

//This function takes the request as an input and returns the response
OCRepPayload* constructResponse (OCEntityHandlerRequest *ehRequest)
{
    if(ehRequest->payload && ehRequest->payload->type != PAYLOAD_TYPE_REPRESENTATION)
    {
        OCSAMPLE_LOG_ERROR(TAG, 0, "Incoming payload not a representation");
        return nullptr;
    }

    OCRepPayload* input = reinterpret_cast<OCRepPayload*>(ehRequest->payload);

    LEDResource *currLEDResource = &LED;

    if (ehRequest->resource == gLedInstance[0].handle)
    {
        currLEDResource = &gLedInstance[0];
        gResourceUri = (char *) "/a/led/0";
    }
    else if (ehRequest->resource == gLedInstance[1].handle)
    {
        currLEDResource = &gLedInstance[1];
        gResourceUri = (char *) "/a/led/1";
    }

    if(OC_REST_PUT == ehRequest->method
        || OC_REST_POST == ehRequest->method)
    {
        // Get pointer to query
        int64_t pow;
        if(OCRepPayloadGetPropInt(input, "power", &pow))
        {
            currLEDResource->power = pow;
        }

        bool state;
        if(OCRepPayloadGetPropBool(input, "state", &state))
        {
            currLEDResource->state = state;
        }
    }

    return getPayload(gResourceUri, currLEDResource->power, currLEDResource->state);
}

OCEntityHandlerResult ProcessGetRequest (OCEntityHandlerRequest *ehRequest,
        OCRepPayload **payload)
{
    OCEntityHandlerResult ehResult;

    OCRepPayload *getResp = constructResponse(ehRequest);

    if(getResp)
    {
        *payload = getResp;
        ehResult = OC_EH_OK;
    }
    else
    {
        ehResult = OC_EH_ERROR;
    }

    return ehResult;
}

OCEntityHandlerResult ProcessPutRequest (OCEntityHandlerRequest *ehRequest,
        OCRepPayload **payload)
{
    OCEntityHandlerResult ehResult;

    OCRepPayload *putResp = constructResponse(ehRequest);

    if(putResp)
    {
        *payload = putResp;
        ehResult = OC_EH_OK;
    }
    else
    {
        ehResult = OC_EH_ERROR;
    }

    return ehResult;
}

OCEntityHandlerResult ProcessPostRequest (OCEntityHandlerRequest *ehRequest,
        OCEntityHandlerResponse *response, OCRepPayload **payload)
{
    OCRepPayload *respPLPost_led = nullptr;
    OCEntityHandlerResult ehResult = OC_EH_OK;

    /*
     * The entity handler determines how to process a POST request.
     * Per the REST paradigm, POST can also be used to update representation of existing
     * resource or create a new resource.
     * In the sample below, if the POST is for /a/led then a new instance of the LED
     * resource is created with default representation (if representation is included in
     * POST payload it can be used as initial values) as long as the instance is
     * lesser than max new instance count. Once max instance count is reached, POST on
     * /a/led updated the representation of /a/led.
     */

    if (ehRequest->resource == LED.handle)
    {
        if (gCurrLedInstance < SAMPLE_MAX_NUM_POST_INSTANCE)
        {
            // Create new LED instance
            char newLedUri[15] = "/a/led/";
            size_t newLedUriLength = strlen(newLedUri);
            snprintf((newLedUri + newLedUriLength), (sizeof(newLedUri) - newLedUriLength), "%d", gCurrLedInstance);

            respPLPost_led = OCRepPayloadCreate();
            OCRepPayloadSetUri(respPLPost_led, gResourceUri);
            OCRepPayloadSetPropString(respPLPost_led, "createduri", newLedUri);

            if (0 == createLEDResource (newLedUri, &gLedInstance[gCurrLedInstance], false, 0))
            {
                OCSAMPLE_LOG_INFO(TAG, 0, "Created new LED instance");
                gLedInstance[gCurrLedInstance].state = 0;
                gLedInstance[gCurrLedInstance].power = 0;
                gCurrLedInstance++;
                strncpy ((char *)response->resourceUri, newLedUri, MAX_URI_LENGTH);
                ehResult = OC_EH_RESOURCE_CREATED;
            }
        }
        else
        {
            respPLPost_led = constructResponse(ehRequest);
        }
    }
    else
    {
        for (int i = 0; i < SAMPLE_MAX_NUM_POST_INSTANCE; i++)
        {
            if (ehRequest->resource == gLedInstance[i].handle)
            {
                if (i == 0)
                {
                    respPLPost_led = constructResponse(ehRequest);
                    break;
                }
                else if (i == 1)
                {
                    respPLPost_led = constructResponse(ehRequest);
                }
            }
        }
    }

    if (respPLPost_led != NULL)
    {
        *payload = respPLPost_led;
        ehResult = OC_EH_OK;
    }
    else
    {
        OCSAMPLE_LOG_INFO(TAG, 0, "Payload was NULL");
        ehResult = OC_EH_ERROR;
    }

    return ehResult;
}

OCEntityHandlerResult
OCEntityHandlerCb (OCEntityHandlerFlag flag,
        OCEntityHandlerRequest *entityHandlerRequest,
        void* /*callbackParam*/)
{
    OCSAMPLE_LOG_INFO(TAG, 0, "Inside entity handler - flags: 0x%x", flag);

    OCEntityHandlerResult ehResult = OC_EH_ERROR;
    OCEntityHandlerResponse response = { 0, 0, OC_EH_ERROR, 0, 0, { },{ 0 }, false };
    // Validate pointer
    if (!entityHandlerRequest)
    {
        OCSAMPLE_LOG_ERROR(TAG, 0, "Invalid request pointer");
        return OC_EH_ERROR;
    }

    OCRepPayload* payload = nullptr;

    if (flag & OC_REQUEST_FLAG)
    {
        OCSAMPLE_LOG_INFO(TAG, 0, "Flag includes OC_REQUEST_FLAG");
        if (entityHandlerRequest)
        {
            if (OC_REST_GET == entityHandlerRequest->method)
            {
                OCSAMPLE_LOG_INFO(TAG, 0, "Received OC_REST_GET from client");
                ehResult = ProcessGetRequest (entityHandlerRequest, &payload);
            }
            else if (OC_REST_PUT == entityHandlerRequest->method)
            {
                OCSAMPLE_LOG_INFO(TAG, 0, "Received OC_REST_PUT from client");
                ehResult = ProcessPutRequest (entityHandlerRequest, &payload);
            }
            else if (OC_REST_POST == entityHandlerRequest->method)
            {
                OCSAMPLE_LOG_INFO(TAG, 0, "Received OC_REST_POST from client");
                ehResult = ProcessPostRequest (entityHandlerRequest, &response, &payload);
            }
            else
            {
                OCSAMPLE_LOG_INFO(TAG, 0, "Received unsupported method %d from client",
                        entityHandlerRequest->method);
                ehResult = OC_EH_ERROR;
            }

            if (ehResult == OC_EH_OK && ehResult != OC_EH_FORBIDDEN)
            {
                // Format the response.  Note this requires some info about the request
                response.requestHandle = entityHandlerRequest->requestHandle;
                response.resourceHandle = entityHandlerRequest->resource;
                response.ehResult = ehResult;
                response.payload = reinterpret_cast<OCPayload*>(payload);
                response.numSendVendorSpecificHeaderOptions = 0;
                memset(response.sendVendorSpecificHeaderOptions, 0, sizeof response.sendVendorSpecificHeaderOptions);
                memset(response.resourceUri, 0, sizeof(response.resourceUri));
                // Indicate that response is NOT in a persistent buffer
                response.persistentBufferFlag = 0;

                // Send the response
                if (OCDoResponse(&response) != OC_STACK_OK)
                {
                    OCSAMPLE_LOG_ERROR(TAG, 0, "Error sending response");
                    ehResult = OC_EH_ERROR;
                }
            }
        }
    }

    OCRepPayloadDestroy(payload);
    return ehResult;
}

/* SIGINT handler: set gQuitFlag to 1 for graceful termination */
void handleSigInt(int signum)
{
    if (signum == SIGINT)
    {
        gQuitFlag = 1;
        g_main_loop_quit(mainloop);
    }
}

FILE* server_fopen(const char *path, const char *mode)
{
    if (0 == strcmp(path, OC_SECURITY_DB_DAT_FILE_NAME))
    {
        return fopen(CRED_FILE, mode);
    }
    else
    {
        return fopen(path, mode);
    }
}

static gboolean serverStarter(gpointer user_data)
{
    if (!gQuitFlag)
    {
        if (OCProcess() != OC_STACK_OK)
        {
            OCSAMPLE_LOG_ERROR(TAG, 0, "OCStack process error");
        }
        return TRUE;
    }
    else
    {
        OCSAMPLE_LOG_INFO(TAG, 0, "Stopping serverStarter loop...");
        return FALSE;
    }
}

int main(int /*argc*/, char* /*argv*/[])
{
    struct timespec timeout;
    LSError lserror;
    LSErrorInit(&lserror);
    (void) PmLogGetContext("OCSERVERBASICOPS", &gLogContext);
    (void) PmLogGetContext("OCSERVERBASICOPS-LIB", &gLogLibContext);
    PmLogSetLibContext(gLogLibContext);

    mainloop = g_main_loop_new(NULL, FALSE);

    // Initialize g_main_loop
    if (!mainloop) {
        OCSAMPLE_LOG_ERROR(TAG, 0, "Failed to create main loop");
        return 0;
    }

    OCSAMPLE_LOG_INFO(TAG, 0, "OCServer is starting...");

    if (!LSRegister("org.ocf.webossample.ocserverbasicops", &pLsHandle, &lserror)) {
        OCSAMPLE_LOG_ERROR(TAG, 0, "Failed to register LS Handle");
        LSErrorLog(gLogContext, "LS_SRVC_ERROR", &lserror);
        return 0;
    }

    if (!LSGmainAttach(pLsHandle, mainloop, &lserror)) {
        OCSAMPLE_LOG_ERROR(TAG, 0, "Failed to attach main loop: %s", &lserror);
        LSErrorLog(gLogContext, "LS_SRVC_ATTACH_ERROR", &lserror);
        return 0;
    }

    // Initialize Persistent Storage for SVR database
    OCPersistentStorage ps = { server_fopen, fread, fwrite, fclose, unlink };
    OCRegisterPersistentStorageHandler(&ps);

    if (OCInit(NULL, 0, OC_SERVER) != OC_STACK_OK)
    {
        OCSAMPLE_LOG_ERROR(TAG, 0, "OCStack init error");
        return 0;
    }

    /*
     * Declare and create the example resource: LED
     */
    createLEDResource(gResourceUri, &LED, false, 0);

    timeout.tv_sec  = 0;
    timeout.tv_nsec = 100000000L;

    // Break from loop with Ctrl-C
    OCSAMPLE_LOG_INFO(TAG, 0, "Entering ocserver main loop...");
    signal(SIGINT, handleSigInt);

    //pthread_create(&threadId_server, NULL, serverStarter, (void *)NULL);
    OCSAMPLE_LOG_INFO(TAG, 0, "Entering serverStarter main loop...");
    g_timeout_add_seconds(1, serverStarter, NULL);
    g_main_loop_run(mainloop);

    OCSAMPLE_LOG_INFO(TAG, 0, "Exiting ocserver main loop...");

    if (OCStop() != OC_STACK_OK)
    {
        OCSAMPLE_LOG_ERROR(TAG, 0, "OCStack process error");
        return 0;
    }

    return 0;
}

int createLEDResource (char *uri, LEDResource *ledResource, bool resourceState, int64_t resourcePower)
{
    if (!uri)
    {
        OCSAMPLE_LOG_ERROR(TAG, 0, "Resource URI cannot be NULL");
        return -1;
    }

    ledResource->state = resourceState;
    ledResource->power= resourcePower;
    OCStackResult res = OCCreateResource(&(ledResource->handle),
            "core.led",
            OC_RSRVD_INTERFACE_DEFAULT,
            uri,
            OCEntityHandlerCb,
            NULL,
            OC_DISCOVERABLE|OC_OBSERVABLE | OC_SECURE);
    OCSAMPLE_LOG_INFO(TAG, 0, "Created LED resource with result: %s", getResult(res));

    return 0;
}
