################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Top-level application make file.
#
################################################################################
# \copyright
# Copyright (2024), Cypress Semiconductor Corporation (an Infineon company)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

include ../common.mk

################################################################################
# Basic Configuration
################################################################################

# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=proj_cm0p

# If set to "true" or "1", display full command-lines when building.
VERBOSE=

# Include the Board Support Package (BSP) Makefile to obtain the DEVICE variable definition
-include bsps/TARGET_$(TARGET)/bsp.mk

# Specify the default device part number (MPN).
# NOTE: This default setting is intended for internal use only and does not impact user builds, even if the MPN is modified.
DEVICE ?= CYUSB4024-BZXI

# Set the path to linker script based on MPN selection.
ifeq ($(filter $(DEVICE),CYUSB4024-BZXI CYUSB4014-FCAXI CYUSB3084-FCAXI CYUSB3284-FCAXI),$(DEVICE))
    LDPATH=LinkerScripts/default
else
    $(error Unsupported MPN: $(DEVICE))
endif

# Name of CORE to use. Only CM0P supported.
# CM0P -- Cortex M0+
#
CORE=CM0P

################################################################################
# Advanced Configuration
################################################################################

# Enable optional code that is ordinarily disabled by default.
#
# Available components depend on the specific targeted hardware and firmware
# in use. In general, if you have
#
#    COMPONENTS=foo bar
#
# ... then code in directories named COMPONENT_foo and COMPONENT_bar will be
# added to the build
#
COMPONENTS=FX20_$(CORE)

# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS=

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
SOURCES=$(wildcard ../shared/source/COMPONENT_CM0P/*.c)

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
INCLUDES=../shared/include

# Add additional defines to the build process (without a leading -D).
DEFINES= \
        BCLK__BUS_CLK__HZ=75000000 \
        DEBUG_INFRA_EN=1 \
        USBFS_LOGS_ENABLE=0 \
        CY_IPC_DEFAULT_CFG_DISABLE

# Conditionally append BLOAD_ENABLE=1 if BLENABLE is set to yes
ifeq ($(BLENABLE), yes)
    DEFINES += BLOAD_ENABLE=1
endif

# Select softfp or hardfp floating point. Default is softfp.
VFP_SELECT=

# Additional / custom C compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
ifeq ($(CORE), CM0P)
    ifeq ($(TOOLCHAIN), GCC_ARM)
        CFLAGS= -Os -Og -mcpu=cortex-m0plus -mthumb -ffunction-sections -fdata-sections -g
    else ifeq ($(TOOLCHAIN), ARM)
        CFLAGS= -Os -Og -mcpu=Cortex-M0plus -mthumb -ffunction-sections -fdata-sections -g \
                --target=arm-arm-none-eabi -fno-rtti -fno-exceptions -Wno-error -fshort-wchar -fshort-enums
    else ifeq ($(TOOLCHAIN), IAR)
        CFLAGS= -Ohz --diag_suppress Pe111
    endif
endif

# Additional / custom C++ compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CXXFLAGS=

# Additional / custom assembler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
ASFLAGS=

# Additional / custom linker flags.
ifeq ($(CORE), CM0P)
    ifeq ($(TOOLCHAIN), GCC_ARM)
        LDFLAGS=-Wl,--start-group -mcpu=cortex-m0plus -mthumb --entry=Reset_Handler -Wl,--gc-sections -g -ffunction-sections -finline-functions -Os -Wl,--end-group
    else ifeq ($(TOOLCHAIN), ARM)
        LDFLAGS=--cpu=Cortex-M0plus --entry=Reset_Handler --diag_suppress=L6329W,L6314W
    else ifeq ($(TOOLCHAIN), IAR)
        LDFLAGS=--cpu Cortex-M0+ --manual_dynamic_initialization --entry Reset_Handler
    endif
endif

# Additional / custom libraries to link in to the application.
LDLIBS=

# Linker script selection
ifeq ($(BLENABLE), yes)
    # Use loadable linker script for CM0P core
    ifeq ($(TOOLCHAIN), GCC_ARM)
        LINKER_SCRIPT = ${LDPATH}/fx_cm0plus_loadable.ld
    else ifeq ($(TOOLCHAIN), ARM)
        LINKER_SCRIPT = ${LDPATH}/fx_cm0plus_loadable.sct
    else ifeq ($(TOOLCHAIN), IAR)
        LINKER_SCRIPT = ${LDPATH}/fx_cm0plus_loadable.icf
    endif
else
    # Use linker script for CM0P core
    ifeq ($(TOOLCHAIN), GCC_ARM)
        LINKER_SCRIPT = ${LDPATH}/fx_cm0plus.ld
    else ifeq ($(TOOLCHAIN), ARM)
        LINKER_SCRIPT = ${LDPATH}/fx_cm0plus.sct
    else ifeq ($(TOOLCHAIN), IAR)
        LINKER_SCRIPT = ${LDPATH}/fx_cm0plus.icf
    endif
endif

# Custom pre-build commands to run.
PREBUILD=

# Custom post-build commands to run.
POSTBUILD=

################################################################################
# Paths
################################################################################

# Relative path to the project directory (default is the Makefile's directory).
#
# This controls where automatic source code discovery looks for code.
CY_APP_PATH=

# Relative path to the shared repo location.
#
# All .mtb files have the format, <URI>#<COMMIT>#<LOCATION>. If the <LOCATION> field
# begins with $$ASSET_REPO$$, then the repo is deposited in the path specified by
# the CY_GETLIBS_SHARED_PATH variable. The default location is one directory level
# above the current app directory.
# This is used with CY_GETLIBS_SHARED_NAME variable, which specifies the directory name.
CY_GETLIBS_SHARED_PATH=../

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

include $(CY_TOOLS_DIR)/make/start.mk
