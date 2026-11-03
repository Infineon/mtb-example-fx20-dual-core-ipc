################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Top-level application make file.
#
################################################################################
# \copyright
# Copyright 2018-2026, Cypress Semiconductor Corporation (an Infineon company)
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


################################################################################
# Basic Configuration
################################################################################

# Type of ModusToolbox Makefile Options include:
#
# COMBINED    -- Top Level Makefile usually for single standalone application
# APPLICATION -- Top Level Makefile usually for multi project application
# PROJECT     -- Project Makefile under Application
#
MTB_TYPE=APPLICATION

# List of projects which are part of the application.
MTB_PROJECTS=proj_cm0p proj_cm4

## Note: While we need to merge the proj_cm0p and proj_cm4 binaries, the default
## make recipe does not offer enough flexibility to combine the binaries and
## then calculate a SHA256 checksum over it. Hence, all the processing is done
## in the POSTBUILD step of the proj_cm4 build and the resulting binary is
## copied during MERGE step.
MERGE=proj_cm4

include common_app.mk

include $(CY_TOOLS_DIR)/make/application.mk
