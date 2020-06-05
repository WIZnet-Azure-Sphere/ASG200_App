/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

// This sample C application for Azure Sphere demonstrates Azure IoT SDK C APIs
// The application uses the Azure IoT SDK C APIs to
// 1. Use the buttons to trigger sending telemetry to Azure IoT Hub/Central.
// 2. Use IoT Hub/Device Twin to control an LED.

// You will need to provide four pieces of information to use this application, all of which are set
// in the app_manifest.json.
// 1. The Scope Id for your IoT Central application (set in 'CmdArgs')
// 2. The Tenant Id obtained from 'azsphere tenant show-selected' (set in 'DeviceAuthentication')
// 3. The Azure DPS Global endpoint address 'global.azure-devices-provisioning.net'
//    (set in 'AllowedConnections')
// 4. The IoT Hub Endpoint address for your IoT Central application (set in 'AllowedConnections')

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <assert.h>

#include <sys/time.h>
#include <sys/socket.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/log.h>
#include <applibs/networking.h>
#include <applibs/gpio.h>
#include <applibs/storage.h>
#include <applibs/eventloop.h>
#include <applibs/wificonfig.h>
#include <applibs/application.h>

// To target different hardware, you'll need to update the CMake build. The necessary
// steps to do this vary depending on if you are building in Visual Studio, in Visual
// Studio Code or via the command line.
//
// See https://github.com/Azure/azure-sphere-samples/tree/master/Hardware for more details.
//
// This #include imports the wiznet_asg200 abstraction from that hardware definition.
#include <hw/wiznet_asg200.h>

#include "eventloop_timer_utilities.h"

// Azure IoT SDK
#include <iothub_client_core_common.h>
#include <iothub_device_client_ll.h>
#include <iothub_client_options.h>
#include <iothubtransportmqtt.h>
#include <iothub.h>
#include <azure_sphere_provisioning.h>

/// <summary>
/// Exit codes for this application. These are used for the
/// application exit code.  They they must all be between zero and 255,
/// where zero is reserved for successful termination.
/// </summary>
typedef enum
{
    ExitCode_Success = 0,

    ExitCode_TermHandler_SigTerm = 1,
    ExitCode_Main_EventLoopFail = 2,
    ExitCode_ButtonTimer_Consume = 3,
    ExitCode_AzureTimer_Consume = 4,

    ExitCode_Init_EventLoop = 5,
    ExitCode_Init_TwinStatusLed = 6,
    ExitCode_Init_ButtonPollTimer = 7,
    ExitCode_Init_AzureTimer = 8,

    ExitCode_EnableState_SetNetworkEnabled = 9,
    ExitCode_DisableState_SetNetworkEnabled = 10,

    ExitCode_DataReceiveTimer_Consume = 11,
    ExitCode_Init_DataReceiveTimer = 12,

    // socket error code
    ExitCode_Init_Connection = 13,
    ExitCode_Init_SetSockOpt = 14,
    ExitCode_Init_RegisterIo = 15,
    ExitCode_SendMsg_Send = 16,
    ExitCode_SocketHandler_Recv = 17,
    ExitCode_TimerHandler_Consume = 18,
    ExitCode_Init_SendTimer = 19,
} ExitCode;

static int sockFd = -1;
static volatile sig_atomic_t exitCode = ExitCode_Success;

// Use Debug message
#define USE_DEBUG
#undef USE_DEBUG

// Use simulated temperature data
#define SIMUL_DATA
#undef SIMUL_DATA

#include "parson.h" // used to parse Device Twin messages.

// Azure IoT Hub/Central defines.
#define SCOPEID_LENGTH 20
static char scopeId[SCOPEID_LENGTH]; // ScopeId for the Azure IoT Central application, set in
                                     // app_manifest.json, CmdArgs

static const char rtAppComponentId[] = "005180bc-402f-4cb3-a662-72937dbcde47";
static void SendToRTApp(char* buf);
static void SendTimeData(void);
static void AppSocketEventHandler(EventLoop *el, int fd, EventLoop_IoEvents events, void *context);
static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
static const int keepalivePeriodSeconds = 20;
static bool iothubAuthenticated = false;
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context);
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payload,
                         size_t payloadSize, void *userContextCallback);
#if 1 // lawrence
static void TwinReportState(const char *jsonState);
#endif
static void TwinReportBoolState(const char *propertyName, bool propertyValue);
static void ReportStatusCallback(int result, void *context);
static const char *GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);
static const char *getAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult);
static void SetupAzureClient(void);

// static void InitPublicEthernet(void);
static ExitCode InitPublicEthernet(void);
static void InitPublicWifi(void);

// Time sync
static void CheckTimeSyncState(void);
static void GetSystemTime(void);

#ifdef SIMUL_DATA
// Function to generate simulated Temperature data/telemetry
// static void SendSimulatedTemperature(void);
static void SendTelemetry(const unsigned char *key, const unsigned char *value);
#endif
static void SendJsonTelemetry(const unsigned char *value);

// Initialization/Cleanup
static ExitCode InitPeripheralsAndHandlers(void);
static void CloseFdAndPrintError(int fd, const char *fdName);
static void ClosePeripheralsAndHandlers(void);

// Available states
static int OutputStoredWifiNetworks(void);
static int WifiRetrieveStoredNetworks(ssize_t *numberOfNetworksStored,
                                      WifiConfig_StoredNetwork *storedNetworksArray);

// The MT3620 currently handles a maximum of 37 stored wifi networks.
static const unsigned int MAX_NUMBER_STORED_NETWORKS = 37;

#ifdef USE_DEBUG
// Array used to print the network security type as a string
static const char *securityTypeToString[] = {"Unknown", "Open", "WPA2/PSK"};
#endif

// Network status
static bool ethernet_connected = false;
static bool net_switch_toggle = true;
static bool azure_connected = false;
static bool wifi_connected = false;
unsigned int storedNetworkCnt = 0;

// Status LEDs
static int azureStatusLedGpioFd = -1;
static int wifiStatusLedGpioFd = -1;
static int ethStatusLedGpioFd = -1;
#if 1 //lawrence
static int eth1StatusLedGpioFd = -1;
#endif

// LED
// static int deviceTwinStatusLedGpioFd = -1;
static bool statusLedOn = false;

// Timer / polling
static EventLoop *eventLoop = NULL;
static EventLoopTimer *azureTimer = NULL;

// Intercore commuications
static EventRegistration *socketEventReg = NULL;
static EventLoopTimer *sendTimer = NULL;

// Azure IoT poll periods
// static const int AzureIoTDefaultPollPeriodSeconds = 60;
static const int AzureIoTDefaultPollPeriodSeconds = 10;
static const int AzureIoTMinReconnectPeriodSeconds = 10;
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60;

static int azureIoTPollPeriodSeconds = -1;

// Network interface
static const char EthernetInterface[] = "eth0";
static const char WifiInterface[] = "wlan0";

// #define DATA_BUF_SIZE 2048

static void AzureTimerEventHandler(EventLoopTimer *timer);

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
    exitCode = ExitCode_TermHandler_SigTerm;
}

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
{
    Log_Debug("Azure IoT Hub Application with 2 Port ethernet starting.\n");

    exitCode = InitPeripheralsAndHandlers();

    // Init Sphere ethernet/wi-fi interface
    InitPublicEthernet();
    InitPublicWifi();

    if (argc == 2)
    {
        Log_Debug("Setting Azure Scope ID %s\n", argv[1]);
        strncpy(scopeId, argv[1], SCOPEID_LENGTH);
    }
    else
    {
        Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
        return -1;
    }

    // Main loop
    while (exitCode == ExitCode_Success)
    {
        EventLoop_Run_Result result = EventLoop_Run(eventLoop, -1, true);
        // Continue if interrupted by signal, e.g. due to breakpoint being set.
        if (result == EventLoop_Run_Failed && errno != EINTR)
        {
            exitCode = ExitCode_Main_EventLoopFail;
        }
    }

    ClosePeripheralsAndHandlers();
    Log_Debug("Application exiting.\n");

    return exitCode;
}

// Init application socket
static ExitCode InitApplicationSocket(void)
{
    // Open a connection to the RTApp.
    sockFd = Application_Connect(rtAppComponentId);
    if (sockFd == -1)
    {
        Log_Debug("ERROR: Unable to create socket: %d (%s)\n", errno, strerror(errno));
        return ExitCode_Init_Connection;
    }

    Log_Debug("Application socket created.\n");

    // Set timeout, to handle case where real-time capable application does not respond.
    static const struct timeval recvTimeout = {.tv_sec = 5, .tv_usec = 0};
    int result = setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
    if (result == -1)
    {
        Log_Debug("ERROR: Unable to set socket timeout: %d (%s)\n", errno, strerror(errno));
        return ExitCode_Init_SetSockOpt;
    }

    // Register handler for incoming messages from real-time capable application.
    socketEventReg = EventLoop_RegisterIo(eventLoop, sockFd, EventLoop_Input, AppSocketEventHandler,
                                          /* context */ NULL);
    if (socketEventReg == NULL)
    {
        Log_Debug("ERROR: Unable to register socket event: %d (%s)\n", errno, strerror(errno));
        return ExitCode_Init_RegisterIo;
    }

    return ExitCode_Success;
}

// Init Azure Sphere Ethernet
static ExitCode InitPublicEthernet(void)
{
    // Ensure the necessary network interface is enabled.
    int result = Networking_SetInterfaceState(EthernetInterface, true);
    if (result != 0)
    {
        if (errno == EAGAIN) {
            Log_Debug("INFO: The networking stack isn't ready yet, will try again later.\n");
            return ExitCode_Success;
        }
        else {
            //Log_Debug("ERROR: Networking_SetInterfaceState for interface '%s' failed: errno=%d (%s)\n",EthernetInterface, errno, strerror(errno));
            Log_Debug("ERROR: Networking_SetInterfaceState for interface '%s' failed\n", EthernetInterface);
        }
    }

    // DHCP client enable
    Networking_IpConfig ipConfig;
    Networking_IpConfig_Init(&ipConfig);
    Networking_IpConfig_EnableDynamicIp(&ipConfig);
    result = Networking_IpConfig_Apply(EthernetInterface, &ipConfig);
    Networking_IpConfig_Destroy(&ipConfig);
    if (result != 0) {
        Log_Debug("ERROR: Networking_IpConfig_Apply: %d (%s)\n", errno, strerror(errno));
    }
    Log_Debug("INFO: Networking_IpConfig_EnableDynamicIp: %s.\n", EthernetInterface);

    return ExitCode_Success;
}

// Init Azure Sphere Wi-Fi
void InitPublicWifi(void)
{
    // Ensure the necessary network interface is enabled.
    int result = Networking_SetInterfaceState(WifiInterface, true);
    if (result < 0)
    {
        if (errno == EAGAIN)
        {
            Log_Debug("INFO: The networking stack isn't ready yet, will try again later.\n");
        }
        else
        {
            Log_Debug("ERROR: Networking_SetInterfaceState for interface '%s' failed: errno=%d (%s)\n", WifiInterface, errno, strerror(errno));
        }
        return;
    }
}

/// <summary>
///     Check whether time sync is enabled on the device. If it is enabled, the current time may be
///     overwritten by NTP.
/// </summary>
static void CheckTimeSyncState(void)
{
    bool isTimeSyncEnabled = false;
    int result = Networking_TimeSync_GetEnabled(&isTimeSyncEnabled);
    if (result != 0) {
        Log_Debug("ERROR: Networking_TimeSync_GetEnabled failed: %s (%d).\n", strerror(errno),
                  errno);
        return;
    }

    // If time sync is enabled, NTP can reset the time
    if (!isTimeSyncEnabled) {
        Networking_TimeSync_SetEnabled(true);
        Log_Debug("INFO: Networking_TimeSync_SetEnabled called\r\n");
    }
}

static void GetSystemTime(void)
{
    // Ask for CLOCK_REALTIME to obtain the current system time. This is not to be confused with the
    // hardware RTC used below to persist the time.
    struct timespec currentTime;
    if (clock_gettime(CLOCK_REALTIME, &currentTime) == -1) {
        Log_Debug("ERROR: clock_gettime failed with error code: %s (%d).\n", strerror(errno),
                  errno);
        return;
    } else {
#if 0
        uint8_t buf[128];
        snprintf(buf, sizeof(buf), "%d", currentTime.tv_sec);
        SendToRTApp(buf);
#else
        SendTimeData();
#endif
    }
}

static void SendTimeData(void)
{
    char timeBuf[64];
    time_t t;
    time(&t);
    struct tm *tm = gmtime(&t);
    if (strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm) != 0) {
        Log_Debug("INFO: Time data: %s\n", timeBuf);
    }

    int bytesSent = send(sockFd, timeBuf, strlen(timeBuf), 0);
    if (bytesSent == -1) {
        Log_Debug("ERROR: Unable to send message: %d (%s)\n", errno, strerror(errno));
        exitCode = ExitCode_SendMsg_Send;
        return;
    }
}

/// <summary>
///     Helper function for TimerEventHandler sends message to real-time capable application.
/// </summary>
static void SendToRTApp(char* buf)
{
    int bytesSent = send(sockFd, buf, strlen(buf), 0);
    if (bytesSent == -1) {
        Log_Debug("ERROR: Unable to send message: %d (%s)\n", errno, strerror(errno));
        exitCode = ExitCode_SendMsg_Send;
        return;
    }
}

/// <summary>
///     Handle socket event by reading incoming data from real-time capable application.
/// </summary>
static void AppSocketEventHandler(EventLoop *el, int fd, EventLoop_IoEvents events, void *context)
{
    // Read response from real-time capable application.
    // If the RTApp has sent more than 32 bytes, then truncate.
    // char rxBuf[32];
    char rxBuf[256] = {0};
    // for (int i = 0; i < sizeof(rxBuf); i++) {
    //     rxBuf[i] = NULL;
    // }

    int bytesReceived = recv(fd, rxBuf, sizeof(rxBuf), 0);

#if 1 //lawrence
    if (eth1StatusLedGpioFd >= 0)
    {
        GPIO_SetValue(eth1StatusLedGpioFd, GPIO_Value_Low);
    }
#endif

    if (bytesReceived == -1)
    {
#if 1 //lawrence
        if (eth1StatusLedGpioFd >= 0)
        {
            GPIO_SetValue(eth1StatusLedGpioFd, GPIO_Value_High);
        }
#endif
        Log_Debug("ERROR: Unable to receive message: %d (%s)\n", errno, strerror(errno));
        exitCode = ExitCode_SocketHandler_Recv;
        return;
    }

    Log_Debug("Received %d bytes: %s\r\n", bytesReceived, rxBuf);

#if 1
    // Send received data from RT Core to IoT Hub
    if (iothubAuthenticated)
    {
        // SendTelemetry("RTdata", rxBuf);
        SendJsonTelemetry(rxBuf);
        IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
    }
    else
    {
        Log_Debug("Iot Hub not authenticated.\r\n");
    }
#endif
}

/// <summary>
///    Outputs the stored Wi-Fi networks.
/// </summary>
/// <returns>0 in case of success, any other value in case of failure</returns>
static int OutputStoredWifiNetworks(void)
{
    unsigned int i, j;
    ssize_t numberOfNetworksStored;
    WifiConfig_StoredNetwork storedNetworksArray[MAX_NUMBER_STORED_NETWORKS];
    int result = WifiRetrieveStoredNetworks(&numberOfNetworksStored, storedNetworksArray);
    if (result != 0)
    {
        return -1;
    }
    if (numberOfNetworksStored == 0)
    {
        return 0;
    }

#ifdef USE_DEBUG
    Log_Debug("INFO: Stored Wi-Fi networks:\n");
#endif
    for (i = 0; i < numberOfNetworksStored; ++i)
    {
        assert(storedNetworksArray[i].security < 3);
#ifdef USE_DEBUG
        for (j = 0; j < storedNetworksArray[i].ssidLength; ++j)
        {
            Log_Debug("%c", isprint(storedNetworksArray[i].ssid[j]) ? storedNetworksArray[i].ssid[j]
                                                                    : '.');
        }
        Log_Debug(" : %s : %s : %s\n", securityTypeToString[storedNetworksArray[i].security],
                  storedNetworksArray[i].isEnabled ? "Enabled" : "Disabled",
                  storedNetworksArray[i].isConnected ? "Connected" : "Disconnected");
#endif

        // change wifi id as connected
        if (storedNetworksArray[i].isConnected)
        {
            wifi_connected = true;
            Log_Debug("WiFi[%d] Connected \n", i);
        }
    }
    storedNetworkCnt = i;
    return 0;
}

/// <summary>
///     Checks if the device is connected to any Wi-Fi networks.
/// </summary>
/// <returns>0 in case of success, any other value in case of failure</returns>
static ExitCode CheckNetworkReady(void)
{
    bool isNetworkReady;
    int result = Networking_IsNetworkingReady(&isNetworkReady);
    int eth0_status = 0;
    int wlan0_status = 0;
    Networking_InterfaceConnectionStatus status;

    if (result != 0)
    {
        Log_Debug("\nERROR: Networking_IsNetworkingReady failed: %s (%d).\n", strerror(errno), errno);
        return result;
    }

#ifdef USE_DEBUG
    if (!isNetworkReady) {
        Log_Debug("INFO: Internet connectivity is not available.\n");
    } else {
        Log_Debug("INFO: Internet connectivity is available.\n");
    }
#endif

#if 1 // check wifi network
    if (OutputStoredWifiNetworks() != 0)
    {
        return -1;
    }
#endif // check wifi network

    ssize_t count = Networking_GetInterfaceCount();
    if (count == -1)
    {
        Log_Debug("ERROR: Networking_GetInterfaceCount: errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }

    // Read current status of all interfaces.
    size_t bytesRequired = ((size_t)count) * sizeof(Networking_NetworkInterface);
    Networking_NetworkInterface *interfaces = malloc(bytesRequired);
    if (!interfaces)
    {
        abort();
    }

    ssize_t actualCount = Networking_GetInterfaces(interfaces, (size_t)count);
    if (actualCount == -1) {
        Log_Debug("ERROR: Networking_GetInterfaces: errno=%d (%s)\n", errno, strerror(errno));
    }

    for (ssize_t i = 0; i < actualCount; ++i)
    {
        int result = Networking_GetInterfaceConnectionStatus(interfaces[i].interfaceName, &status);
        if (result != 0)
        {
            Log_Debug("ERROR: Networking_GetInterfaceConnectionStatus: errno=%d (%s)\n", errno, strerror(errno));
            return -1;
        }

        if (strstr(interfaces[i].interfaceName, "wlan0"))
        {
#ifdef USE_DEBUG
            Log_Debug("INFO: %s \n", interfaces[i].interfaceName);
#endif
            switch (status)
            {
            case 0x01:
                wlan0_status = 1;
                wifi_connected = false;
#ifdef USE_DEBUG
                Log_Debug("INFO: wlan0_status 0x01 \n");
#endif
                if (!ethernet_connected)
                {
                    for (unsigned int i = 0; i < storedNetworkCnt; i++)
                    {
                        result = WifiConfig_SetNetworkEnabled(i, true);
                        if (result < 0)
                        {
                            exitCode = ExitCode_EnableState_SetNetworkEnabled;
                            return -1;
                        }
                    }
                }
                break;

            case 0x03:
                wlan0_status = 2;
                wifi_connected = false;
#ifdef USE_DEBUG
                Log_Debug("INFO: wlan0_status 0x03 \n");
#endif
                break;

            case 0x0f:
                wlan0_status = 3;
                wifi_connected = true;
#ifdef USE_DEBUG
                Log_Debug("INFO: wlan0_status 0x0F \n");
#endif
                if (ethernet_connected)
                {
#ifdef USE_DEBUG
                    Log_Debug("INFO: Ethernet linked -> Wi-Fi disable \n");
#endif
                    for (unsigned int i = 0; i < storedNetworkCnt; i++)
                    {
                        result = WifiConfig_SetNetworkEnabled((int)i, false);
                        if (result < 0)
                        {
                            exitCode = ExitCode_DisableState_SetNetworkEnabled;
                            return -1;
                        }
                    }
                }
                break;

            default:
                wifi_connected = false;
                break;
            }
        }

        if (strstr(interfaces[i].interfaceName, "eth0"))
        {
            switch (status)
            {
            case 0x01:
                eth0_status = 1;
                ethernet_connected = false;
#ifdef USE_DEBUG
                Log_Debug("INFO: eth0_status 0x01 \n");
#endif
                break;

            case 0x03:
                eth0_status = 2;
                ethernet_connected = false;
#ifdef USE_DEBUG
                Log_Debug("INFO: eth0_status 0x03 \n");
#endif
                break;

            case 0x0f:
                eth0_status = 3;
                ethernet_connected = true;
#ifdef USE_DEBUG
                Log_Debug("INFO: eth0_status 0x0F \n");
#endif
                if (wifi_connected)
                {
                    for (unsigned int i = 0; i < storedNetworkCnt; i++)
                    {
                        result = WifiConfig_SetNetworkEnabled((int)i, false);
                        if (result < 0)
                        {
                            exitCode = ExitCode_DisableState_SetNetworkEnabled;
                            return -1;
                        }
                    }
                }
                break;

            default:
                ethernet_connected = false;
                break;
            }
        }
    }

    // set azure connect trigger

#ifdef USE_DEBUG
    Log_Debug("azure_connected: %d \n", azure_connected);
#endif
    if (!azure_connected)
    {
        net_switch_toggle = true;
#ifdef USE_DEBUG
        Log_Debug("net_switch_toggle: %d \n", net_switch_toggle);
#endif
    }
    else
    {
        if ((wlan0_status == 3) && (eth0_status == 3))
        {
            net_switch_toggle = true;
#ifdef USE_DEBUG
            Log_Debug("net_switch_toggle: %d \n", net_switch_toggle);
#endif
        }
        if ((wlan0_status == 1) && (eth0_status == 1))
        {
            net_switch_toggle = true; // true
#ifdef USE_DEBUG
            Log_Debug("net_switch_toggle: %d \n", net_switch_toggle);
#endif
        }
    }

    free(interfaces);
}

/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void AzureTimerEventHandler(EventLoopTimer *timer)
{
    if (CheckNetworkReady() != 0) {
        Log_Debug("Failed NetworkReady \n");
        exitCode = ExitCode_AzureTimer_Consume;
        return;
    }

    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        exitCode = ExitCode_AzureTimer_Consume;
        return;
    }

    CheckTimeSyncState();
    GetSystemTime();

    bool isNetworkReady = false;
    if (Networking_IsNetworkingReady(&isNetworkReady) != -1) {
        if (isNetworkReady && !iothubAuthenticated) {
            SetupAzureClient();
        }
    }
    else
    {
        Log_Debug("Failed to get Network state\n");
    }

#if 1 // lawrence - azure led
    if (iothubAuthenticated) {
        if (azureStatusLedGpioFd >= 0) {
            GPIO_SetValue(azureStatusLedGpioFd, GPIO_Value_Low);
        }
    } else {
        if (azureStatusLedGpioFd >= 0) {
            GPIO_SetValue(azureStatusLedGpioFd, GPIO_Value_High);
        }
    }
#endif

    if (ethernet_connected) {
        if (ethStatusLedGpioFd >= 0) {
            GPIO_SetValue(ethStatusLedGpioFd, GPIO_Value_Low);
        }
    } else {
        if (ethStatusLedGpioFd >= 0) {
            GPIO_SetValue(ethStatusLedGpioFd, GPIO_Value_High);
        }
    }

    if (wifi_connected) {
        if (wifiStatusLedGpioFd >= 0) {
            GPIO_SetValue(wifiStatusLedGpioFd, GPIO_Value_Low);
        }
    } else {
        if (wifiStatusLedGpioFd >= 0) {
            GPIO_SetValue(wifiStatusLedGpioFd, GPIO_Value_High);
        }
    }

#if 0
    if (iothubAuthenticated) {
#ifdef SIMUL_DATA
        SendSimulatedTemperature();
#endif
        IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
    }
#endif
    
}

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>ExitCode_Success if all resources were allocated successfully; otherwise another
/// ExitCode value which indicates the specific failure.</returns>
static ExitCode InitPeripheralsAndHandlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

    eventLoop = EventLoop_Create();
    if (eventLoop == NULL)
    {
        Log_Debug("Could not create event loop.\n");
        return ExitCode_Init_EventLoop;
    }

#if 1 // init led
    Log_Debug("Opening WIZNET_ASG200_CONNECTION_STATUS_LED as output\n");
    azureStatusLedGpioFd =
        GPIO_OpenAsOutput(WIZNET_ASG200_CONNECTION_STATUS_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    if (azureStatusLedGpioFd < 0)
    {
        Log_Debug("ERROR: Could not open LED: %s (%d).\n", strerror(errno), errno);
        return -1;
    }

    Log_Debug("Opening WIZNET_ASG200_ETH0_STATUS_LED as output\n");
    ethStatusLedGpioFd =
        GPIO_OpenAsOutput(WIZNET_ASG200_ETH0_STATUS_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    if (ethStatusLedGpioFd < 0)
    {
        Log_Debug("ERROR: Could not open LED: %s (%d).\n", strerror(errno), errno);
        return -1;
    }

    Log_Debug("Opening WIZNET_ASG200_WLAN_STATUS_LED as output\n");
    wifiStatusLedGpioFd =
        GPIO_OpenAsOutput(WIZNET_ASG200_WLAN_STATUS_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    if (wifiStatusLedGpioFd < 0)
    {
        Log_Debug("ERROR: Could not open LED: %s (%d).\n", strerror(errno), errno);
        return -1;
    }
#if 1 //lawrence
    Log_Debug("Opening WIZNET_ASG200_ETH1_STATUS_LED as output\n");
    eth1StatusLedGpioFd =
        GPIO_OpenAsOutput(WIZNET_ASG200_ETH1_STATUS_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    if (eth1StatusLedGpioFd < 0)
    {
        Log_Debug("ERROR: Could not open LED: %s (%d).\n", strerror(errno), errno);
        return -1;
    }
#endif
#endif // init led

    azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
    struct timespec azureTelemetryPeriod = {.tv_sec = azureIoTPollPeriodSeconds, .tv_nsec = 0};
    azureTimer =
        CreateEventLoopPeriodicTimer(eventLoop, &AzureTimerEventHandler, &azureTelemetryPeriod);
    if (azureTimer == NULL)
    {
        return ExitCode_Init_AzureTimer;
    }

    InitApplicationSocket();

    return ExitCode_Success;
}

/// <summary>
///     Retrieves the stored networks on the device.
/// </summary>
/// <param name="numberOfNetworksStored">Output param used to retrieve the number of stored
/// networks on the device</param>
/// <param name="storedNetworksArray">Output param used to maintain the stored networks on the
/// device.</param>
/// <returns>Returns 0 in case of success, -1 otherwise</returns>
static int WifiRetrieveStoredNetworks(ssize_t *numberOfNetworksStored,
                                      WifiConfig_StoredNetwork *storedNetworksArray)
{
    ssize_t temporaryNumberOfNetworks = WifiConfig_GetStoredNetworkCount();
    if (temporaryNumberOfNetworks < 0)
    {
        //Log_Debug("ERROR: WifiConfig_GetStoredNetworkCount failed: %s (%d).\n", strerror(errno), errno);
        Log_Debug("ERROR: WifiConfig_GetStoredNetworkCount failed\n");
        return -1;
    }

    assert(temporaryNumberOfNetworks <= MAX_NUMBER_STORED_NETWORKS);

    if (temporaryNumberOfNetworks == 0)
    {
        *numberOfNetworksStored = 0;
        return 0;
    }

    WifiConfig_StoredNetwork temporaryStoredNetworksArray[temporaryNumberOfNetworks];
    temporaryNumberOfNetworks = WifiConfig_GetStoredNetworks(temporaryStoredNetworksArray,
                                                             (size_t)temporaryNumberOfNetworks);
    if (temporaryNumberOfNetworks < 0)
    {
        //Log_Debug("ERROR: WifiConfig_GetStoredNetworks failed: %s (%d).\n", strerror(errno), errno);
        Log_Debug("ERROR: WifiConfig_GetStoredNetworks failed\n");
        return -1;
    }
    for (size_t i = 0; i < temporaryNumberOfNetworks; ++i)
    {
        storedNetworksArray[i] = temporaryStoredNetworksArray[i];
    }

    memcpy(storedNetworksArray, temporaryStoredNetworksArray, (size_t)temporaryNumberOfNetworks);

    *numberOfNetworksStored = temporaryNumberOfNetworks;

    return 0;
}

/// <summary>
///     Closes a file descriptor and prints an error on failure.
/// </summary>
/// <param name="fd">File descriptor to close</param>
/// <param name="fdName">File descriptor name to use in error message</param>
static void CloseFdAndPrintError(int fd, const char *fdName)
{
    if (fd >= 0)
    {
        int result = close(fd);
        if (result != 0)
        {
            Log_Debug("ERROR: Could not close fd %s: %s (%d).\n", fdName, strerror(errno), errno);
        }
    }
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
    DisposeEventLoopTimer(azureTimer);
    DisposeEventLoopTimer(sendTimer);
    EventLoop_UnregisterIo(eventLoop, socketEventReg);
    EventLoop_Close(eventLoop);

    Log_Debug("Closing file descriptors\n");

    // Leave the LEDs off
    if (azureStatusLedGpioFd >= 0)
    {
        GPIO_SetValue(azureStatusLedGpioFd, GPIO_Value_High);
    }
    if (ethStatusLedGpioFd >= 0)
    {
        GPIO_SetValue(ethStatusLedGpioFd, GPIO_Value_High);
    }
    if (wifiStatusLedGpioFd >= 0)
    {
        GPIO_SetValue(wifiStatusLedGpioFd, GPIO_Value_High);
    }
#if 1 //lawrence
    if (eth1StatusLedGpioFd >= 0)
    {
        GPIO_SetValue(eth1StatusLedGpioFd, GPIO_Value_High);
    }
#endif

    CloseFdAndPrintError(azureStatusLedGpioFd, "azureStatusLed");
    CloseFdAndPrintError(ethStatusLedGpioFd, "ethStatusLed");
    CloseFdAndPrintError(wifiStatusLedGpioFd, "wifiStatusLed");
#if 1 //lawrence
    CloseFdAndPrintError(eth1StatusLedGpioFd, "eth1StatusLed");
#endif
    CloseFdAndPrintError(sockFd, "Socket");
}

/// <summary>
///     Sets the IoT Hub authentication state for the app
///     The SAS Token expires which will set the authentication state
/// </summary>
static void HubConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                        IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
                                        void *userContextCallback)
{
    iothubAuthenticated = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
    Log_Debug("IoT Hub Authenticated: %s\n", GetReasonString(reason));
#if 1 // lawrence
    if (iothubAuthenticated)
    {
        // Send static device twin properties when connection is established
        TwinReportState(
            "{\"Manufacturer\":\"WIZnet\",\"Model\":\"ASG200-DEMO\",\"HLAppVer\":\"1.0.1\",\"RTAppVer\":\"1.0.0\",\"LocalNetwork\":\"true\"}");
    }
#endif
}

/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
static void SetupAzureClient(void)
{
    if (iothubClientHandle != NULL)
    {
        IoTHubDeviceClient_LL_Destroy(iothubClientHandle);
    }

    AZURE_SPHERE_PROV_RETURN_VALUE provResult =
        IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, AzureIoTDefaultPollPeriodSeconds * 1000,
                                                                          &iothubClientHandle);
    Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
              getAzureSphereProvisioningResultString(provResult));

    if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK)
    {

        // If we fail to connect, reduce the polling frequency, starting at
        // AzureIoTMinReconnectPeriodSeconds and with a backoff up to
        // AzureIoTMaxReconnectPeriodSeconds
        if (azureIoTPollPeriodSeconds == AzureIoTDefaultPollPeriodSeconds) {
            azureIoTPollPeriodSeconds = AzureIoTMinReconnectPeriodSeconds;
        } else {
            azureIoTPollPeriodSeconds *= 2;
            if (azureIoTPollPeriodSeconds > AzureIoTMaxReconnectPeriodSeconds) {
                azureIoTPollPeriodSeconds = AzureIoTMaxReconnectPeriodSeconds;
            }
        }

        struct timespec azureTelemetryPeriod = {azureIoTPollPeriodSeconds, 0};
        SetEventLoopTimerPeriod(azureTimer, &azureTelemetryPeriod);

        Log_Debug("ERROR: failure to create IoTHub Handle - will retry in %i seconds.\n",
                  azureIoTPollPeriodSeconds);
        return;
    }

    // Successfully connected, so make sure the polling frequency is back to the default
    azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
    struct timespec azureTelemetryPeriod = {.tv_sec = azureIoTPollPeriodSeconds, .tv_nsec = 0};
    SetEventLoopTimerPeriod(azureTimer, &azureTelemetryPeriod);

    iothubAuthenticated = true;

    if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
                                        &keepalivePeriodSeconds) != IOTHUB_CLIENT_OK)
    {
        Log_Debug("ERROR: failure setting option \"%s\"\n", OPTION_KEEP_ALIVE);
        return;
    }

    if (IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, TwinCallback,
                                                    NULL) != IOTHUB_CLIENT_OK)
    {
        Log_Debug("ERROR: failure set device twin callback\n");
        return;
    }

    IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle,
                                                      HubConnectionStatusCallback, NULL);
}

/// <summary>
///     Callback invoked when a Device Twin update is received from IoT Hub.
///     Updates local state for 'showEvents' (bool).
/// </summary>
/// <param name="payload">contains the Device Twin JSON document (desired and reported)</param>
/// <param name="payloadSize">size of the Device Twin JSON document</param>
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payload,
                         size_t payloadSize, void *userContextCallback)
{
    size_t nullTerminatedJsonSize = payloadSize + 1;
    char *nullTerminatedJsonString = (char *)malloc(nullTerminatedJsonSize);
    if (nullTerminatedJsonString == NULL)
    {
        Log_Debug("ERROR: Could not allocate buffer for twin update payload.\n");
        abort();
    }

    // Copy the provided buffer to a null terminated buffer.
    memcpy(nullTerminatedJsonString, payload, payloadSize);
    // Add the null terminator at the end.
    nullTerminatedJsonString[nullTerminatedJsonSize - 1] = 0;

    JSON_Value *rootProperties = NULL;
    rootProperties = json_parse_string(nullTerminatedJsonString);
    if (rootProperties == NULL)
    {
        Log_Debug("WARNING: Cannot parse the string as JSON content.\n");
        goto cleanup;
    }

    JSON_Object *rootObject = json_value_get_object(rootProperties);
    JSON_Object *desiredProperties = json_object_dotget_object(rootObject, "desired");
    if (desiredProperties == NULL)
    {
        desiredProperties = rootObject;
    }

    // Handle the Device Twin Desired Properties here.
    JSON_Object *LEDState = json_object_dotget_object(desiredProperties, "StatusLED");
    if (LEDState != NULL)
    {
        statusLedOn = (bool)json_object_get_boolean(LEDState, "value");
        // GPIO_SetValue(deviceTwinStatusLedGpioFd,
        //               (statusLedOn == true ? GPIO_Value_Low : GPIO_Value_High));
        TwinReportBoolState("StatusLED", statusLedOn);
    }

cleanup:
    // Release the allocated memory.
    json_value_free(rootProperties);
    free(nullTerminatedJsonString);
}

/// <summary>
///     Converts the IoT Hub connection status reason to a string.
/// </summary>
static const char *GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
    static char *reasonString = "unknown reason";
    switch (reason)
    {
    case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
        reasonString = "IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN";
        break;
    case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED";
        break;
    case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
        reasonString = "IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL";
        break;
    case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED";
        break;
    case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_NO_NETWORK";
        break;
    case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
        reasonString = "IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR";
        break;
    case IOTHUB_CLIENT_CONNECTION_OK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_OK";
        break;
    }
    return reasonString;
}

/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
static const char *getAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
    switch (provisioningResult.result)
    {
    case AZURE_SPHERE_PROV_RESULT_OK:
        return "AZURE_SPHERE_PROV_RESULT_OK";
    case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
        return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
    case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
    case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
    default:
        return "UNKNOWN_RETURN_VALUE";
    }
}

// #ifdef SIMUL_DATA
#if 1
/// <summary>
///     Sends telemetry to IoT Hub
/// </summary>
/// <param name="key">The telemetry item to update</param>
/// <param name="value">new telemetry value</param>
static void SendTelemetry(const unsigned char *key, const unsigned char *value)
{
    static char eventBuffer[1024] = {0};
    static const char *EventMsgTemplate = "{ \"%s\": \"%s\" }";
    int len = snprintf(eventBuffer, sizeof(eventBuffer), EventMsgTemplate, key, value);
    if (len < 0)
        return;

    Log_Debug("Sending IoT Hub Message: %s\n", eventBuffer);

    bool isNetworkingReady = false;
    if ((Networking_IsNetworkingReady(&isNetworkingReady) == -1) || !isNetworkingReady)
    {
        Log_Debug("WARNING: Cannot send IoTHubMessage because network is not up.\n");
        return;
    }

    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(eventBuffer);

    if (messageHandle == 0)
    {
        Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
        return;
    }

    // Set system properties
    (void)IoTHubMessage_SetContentTypeSystemProperty(messageHandle, "application%2fjson");
    (void)IoTHubMessage_SetContentEncodingSystemProperty(messageHandle, "utf-8");

    if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendMessageCallback,
                                             /*&callback_param*/ 0) != IOTHUB_CLIENT_OK)
    {
        Log_Debug("WARNING: failed to hand over the message to IoTHubClient\n");
    }
    else
    {
        Log_Debug("INFO: IoTHubClient accepted the message for delivery\n");
    }

    IoTHubMessage_Destroy(messageHandle);
}
#endif

// JSON String value
static void SendJsonTelemetry(const unsigned char *value)
{
    static char eventBuffer[256] = {0};
    static const char *EventMsgTemplate = "%s";

    int len = snprintf(eventBuffer, sizeof(eventBuffer), EventMsgTemplate, value);
    if (len < 0)
        return;

    Log_Debug("Sending IoT Hub Message: %s\n", eventBuffer);

    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(eventBuffer);
    if (messageHandle == 0)
    {
        Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
        return;
    }

    // Set system properties
    (void)IoTHubMessage_SetContentTypeSystemProperty(messageHandle, "application%2fjson");
    (void)IoTHubMessage_SetContentEncodingSystemProperty(messageHandle, "utf-8");

    if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendMessageCallback,
                                             /*&callback_param*/ 0) != IOTHUB_CLIENT_OK)
    {
        Log_Debug("WARNING: failed to hand over the message to IoTHubClient: %s (%d)\n", strerror(errno), errno);
    }
    else
    {
        Log_Debug("INFO: IoTHubClient accepted the message for delivery\n");
    }

    IoTHubMessage_Destroy(messageHandle);
}

/// <summary>
///     Callback confirming message delivered to IoT Hub.
/// </summary>
/// <param name="result">Message delivery status</param>
/// <param name="context">User specified context</param>
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context)
{
    Log_Debug("INFO: Message received by IoT Hub. Result is: %d\n", result);
}

/// <summary>
///     Creates and enqueues a report containing the name and value pair of a Device Twin reported
///     property. The report is not sent immediately, but it is sent on the next invocation of
///     IoTHubDeviceClient_LL_DoWork().
/// </summary>
/// <param name="propertyName">the IoT Hub Device Twin property name</param>
/// <param name="propertyValue">the IoT Hub Device Twin property value</param>
static void TwinReportBoolState(const char *propertyName, bool propertyValue)
{
    if (iothubClientHandle == NULL)
    {
        Log_Debug("ERROR: client not initialized\n");
    }
    else
    {
        static char reportedPropertiesString[30] = {0};
        int len = snprintf(reportedPropertiesString, 30, "{\"%s\":%s}", propertyName,
                           (propertyValue == true ? "true" : "false"));
        if (len < 0)
            return;

        if (IoTHubDeviceClient_LL_SendReportedState(
                iothubClientHandle, (unsigned char *)reportedPropertiesString,
                strlen(reportedPropertiesString), ReportStatusCallback, 0) != IOTHUB_CLIENT_OK)
        {
            Log_Debug("ERROR: failed to set reported state for '%s'.\n", propertyName);
        }
        else
        {
            Log_Debug("INFO: Reported state for '%s' to value '%s'.\n", propertyName,
                      (propertyValue == true ? "true" : "false"));
        }
    }
}

#if 1 // lawrence
/// <summary>
///     Enqueues a report containing Device Twin reported properties. The report is not sent
///     immediately, but it is sent on the next invocation of IoTHubDeviceClient_LL_DoWork().
/// </summary>
static void TwinReportState(const char *jsonState)
{
    if (iothubClientHandle == NULL)
    {
        Log_Debug("ERROR: Azure IoT Hub client not initialized.\n");
    }
    else
    {
        if (IoTHubDeviceClient_LL_SendReportedState(
                iothubClientHandle, (const unsigned char *)jsonState, strlen(jsonState),
                ReportStatusCallback, NULL) != IOTHUB_CLIENT_OK)
        {
            Log_Debug("ERROR: Azure IoT Hub client error when reporting state '%s'.\n", jsonState);
        }
        else
        {
            Log_Debug("INFO: Azure IoT Hub client accepted request to report state '%s'.\n",
                      jsonState);
        }
    }
}
#endif

/// <summary>
///     Callback invoked when the Device Twin reported properties are accepted by IoT Hub.
/// </summary>
static void ReportStatusCallback(int result, void *context)
{
    Log_Debug("INFO: Device Twin reported properties update result: HTTP status code %d\n", result);
}

#if 0
/// <summary>
///     Generates a simulated Temperature and sends to IoT Hub.
/// </summary>
void SendSimulatedTemperature(void)
{
    static float temperature = 30.0;
    float deltaTemp = (float)(rand() % 20) / 20.0f;
    if (rand() % 2 == 0) {
        temperature += deltaTemp;
    } else {
        temperature -= deltaTemp;
    }

    char tempBuffer[20];
    int len = snprintf(tempBuffer, 20, "%3.2f", temperature);
    if (len > 0)
        SendTelemetry("Temperature", tempBuffer);
}
#endif
