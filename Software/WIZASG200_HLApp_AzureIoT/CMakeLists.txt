#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

# CMAKE_MINIMUM_REQUIRED(VERSION 3.8)
cmake_minimum_required(VERSION 3.10)

PROJECT(HLApp_AzureIoT_WIZASG200 C)

azsphere_configure_tools(TOOLS_REVISION "20.07")
azsphere_configure_api(TARGET_API_SET "6")

# Create executable
ADD_EXECUTABLE(${PROJECT_NAME} main.c eventloop_timer_utilities.c parson.c)
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PUBLIC ${AZURE_SPHERE_API_SET_DIR}/usr/include/azureiot)
TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PUBLIC AZURE_IOT_HUB_CONFIGURED)
TARGET_LINK_LIBRARIES(${PROJECT_NAME} m azureiot applibs pthread gcc_s c)

azsphere_target_hardware_definition(${PROJECT_NAME} TARGET_DIRECTORY "../../Hardware/usi_mt3620_bt_evb" TARGET_DEFINITION "usi_mt3620_bt_evb.json")

find_program(POWERSHELL powershell.exe)
if (POWERSHELL)
    # Run validate_manifest script during build
    ADD_CUSTOM_TARGET(ValidateManifest ALL
        COMMAND ${POWERSHELL} -ExecutionPolicy Bypass -NoProfile -NonInteractive -File ${CMAKE_SOURCE_DIR}/script/validate_manifest.ps1
        DEPENDS ${CMAKE_SOURCE_DIR}/app_manifest.json)
else()
    # Warn users without PowerShell to update their manifest
    ADD_CUSTOM_TARGET(ValidateManifest ALL
        COMMAND echo "Please ensure that you have updated app_manifest.json as described in IoTCentral.md or IoTHub.md, as appropriate."
        DEPENDS ${CMAKE_SOURCE_DIR}/app_manifest.json)
endif()

ADD_DEPENDENCIES(ValidateManifest ${PROJECT_NAME})

azsphere_target_add_image_package(${PROJECT_NAME})
