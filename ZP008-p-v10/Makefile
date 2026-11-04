##########################################################################################################################
# ZP008-p-v10 - magna peripheral on a PY32F002B (TSSOP20, 24 KB flash / 3 KB RAM)
##########################################################################################################################

######################################
# target
######################################
TARGET = ZP008-p-v10


######################################
# building variables
######################################
# debug build?
DEBUG = 1
# optimization
OPT = -Og


#######################################
# paths
#######################################
# Build path
BUILD_DIR = build

# Portable protocol engine shared with the controller side (protocol.cpp)
BITBANG_DIR = ../bitbang_interface/src

# CMSIS, HAL, startup, linker script and SystemInit come from the Puya
# submodule inside this project instead of a local copy:
#   git submodule update --init ZP008-p-v10/PY32F002B_Firmware
# Only the project files stay in this tree (Src/, incl. the HAL configuration
# and interrupt header the HAL expects next to the application).
PUYA_DIR   = PY32F002B_Firmware
PUYA_TMPL  = $(PUYA_DIR)/Templates/PY32F002Bxx_Templates
PUYA_HAL   = $(PUYA_DIR)/Drivers/PY32F002B_HAL_Driver
PUYA_CMSIS = $(PUYA_DIR)/Drivers/CMSIS

ifeq ($(wildcard $(PUYA_TMPL)/EIDE/startup_py32f002bxx.s),)
$(error $(PUYA_DIR) submodule is missing: run 'git submodule update --init ZP008-p-v10/PY32F002B_Firmware' from the repo root)
endif


######################################
# source
######################################
# C sources
C_SOURCES =  \
Src/main.c \
Src/app_clock.c \
Src/app_io.c \
Src/app_peripheral.c \
Src/bsp/py32f002b_hal_msp.c \
Src/bsp/py32f002b_it.c \
$(PUYA_HAL)/Src/py32f002b_hal.c \
$(PUYA_HAL)/Src/py32f002b_hal_rcc.c \
$(PUYA_HAL)/Src/py32f002b_hal_rcc_ex.c \
$(PUYA_HAL)/Src/py32f002b_hal_gpio.c \
$(PUYA_HAL)/Src/py32f002b_hal_pwr.c \
$(PUYA_HAL)/Src/py32f002b_hal_cortex.c

# C++ sources
CPP_SOURCES =  \
Src/link.cpp \
$(BITBANG_DIR)/protocol.cpp


# ASM sources
ASM_SOURCES =  \
$(PUYA_TMPL)/EIDE/startup_py32f002bxx.s

# Vendor sources that cannot go through the object-per-file-name rules below.
# The template Src directory also holds a main.c, py32f002b_it.c and
# py32f002b_hal_msp.c; keeping it off the vpath stops those from silently
# shadowing the project files of the same name.
VENDOR_SOURCES =  \
$(PUYA_TMPL)/Src/system_py32f002b.c


#######################################
# binaries
#######################################
PREFIX = arm-none-eabi-
# The gcc compiler bin path can be either defined in make command via GCC_PATH variable (> make GCC_PATH=xxx)
# either it can be added to the PATH environment variable.
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
CXX = $(GCC_PATH)/$(PREFIX)g++
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# CFLAGS
#######################################
# cpu
CPU = -mcpu=cortex-m0plus

# fpu
# NONE for Cortex-M0/M0+/M3

# float-abi


# mcu
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# macros for gcc
# AS defines
AS_DEFS =

# C defines
C_DEFS =  \
-DUSE_HAL_DRIVER\
-DPY32F002Bx5


# AS includes
AS_INCLUDES =

# C includes
C_INCLUDES =  \
-ISrc \
-I$(BITBANG_DIR) \
-I$(PUYA_HAL)/Inc \
-I$(PUYA_CMSIS)/Device/PY32F0xx/Include \
-I$(PUYA_CMSIS)/Include


# compile gcc flags
ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

# C++ flags: the protocol engine is C++17. Exceptions and RTTI are disabled to
# keep the image small on this 24 KB / 3 KB part, and thread-safe statics are
# pointless on a single-threaded target. -Wno-register silences the CMSIS
# intrinsics, which still use the C++17-removed keyword.
CXXFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -std=gnu++17 \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wno-register \
            -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
CXXFLAGS += -g -gdwarf-2
endif


# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
CXXFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"


#######################################
# LDFLAGS
#######################################
# link script
LDSCRIPT = $(PUYA_TMPL)/EIDE/py32f002bx5.ld

# libraries
LIBS = -lstdc++ -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -Wl,--print-memory-usage

# default action: build all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin


#######################################
# build the application
#######################################
# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
# list of C++ program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CPP_SOURCES:.cpp=.o)))
vpath %.cpp $(sort $(dir $(CPP_SOURCES)))
# list of ASM program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))
# list of vendor objects: prefixed so they never collide with project objects
VENDOR_OBJECTS = $(addprefix $(BUILD_DIR)/vendor_,$(notdir $(VENDOR_SOURCES:.c=.o)))
OBJECTS += $(VENDOR_OBJECTS)

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.cpp Makefile | $(BUILD_DIR)
	$(CXX) -c $(CXXFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cpp=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(VENDOR_OBJECTS): $(BUILD_DIR)/vendor_%.o: $(PUYA_TMPL)/Src/%.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@

#######################################
# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
