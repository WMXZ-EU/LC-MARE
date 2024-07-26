
#******************************************************************************
# general
SHELL            := pwsh.exe
export SHELL

# Recursively create list of source and object files in USR_SRC and CORE_SRC
# and corresponding subdirectories.
# The function rwildcard is taken from http://stackoverflow.com/a/12959694)
rwildcard =$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

#  
NOW = $(shell [int](Get-Date -UFormat +%s))
$(info Now = $(NOW))

# get configuration fromm VSCode
$(info Config = $(CONFIG_NAME))
#
BOARD_ID_ = $(lastword $(CONFIG_NAME))

ifeq ($(BOARD_ID_),)
	BOARD_ID_ = 41
endif
BOARD_ID = T$(BOARD_ID_)

TDVERSION 		:= 1.59.0

#
USB_DEVICE		:= USB_MTPDISK_SERIAL 				#<----- edit
LIBS_SHARED		:= SdFat SPI Wire Time			#<----- edit
LIBS_LOCAL		:= DSP						#<----- edit

FLAGS_CPU		:= -mthumb -mfloat-abi=hard -fsingle-precision-constant
ifeq ($(BOARD_ID),T36)
	BOARD_CLASS	:= teensy
	Family 		:= teensy3
	LDFILE		:= mk66fx1m0.ld

	FLAGS_CPU   += -mcpu=cortex-m4  -mfpu=fpv4-sp-d16 
	LIBS        := 
	DEFINES     := -D__MK66FX1M0__ -DARDUINO_TEENSY36

	DEFINES     += -DF_CPU=96000000
	DEFINES		+= -DUSE_SDIO=1
endif

ifeq ($(BOARD_ID),T40)
	BOARD_CLASS	:= teensy
	Family		:= teensy4
	LDFILE		:= imxrt1062.ld
	FLAGS_CPU   += -mcpu=cortex-m7 -mfpu=fpv5-d16 
	LIBS        := -larm_cortexM7lfsp_math
	DEFINES     := -D__IMXRT1062__  -DARDUINO_TEENSY40
	DEFINES     += -DF_CPU=396000000
	DEFINES		+= -DUSE_SDIO=1
endif

ifeq ($(BOARD_ID),T41)
	BOARD_CLASS	:= teensy
	Family		:= teensy4
	LDFILE		:= imxrt1062_t41.ld
	FLAGS_CPU   += -mcpu=cortex-m7 -mfpu=fpv5-d16
	LIBS        := -larm_cortexM7lfsp_math
	DEFINES     := -D__IMXRT1062__  -DARDUINO_TEENSY41
	DEFINES     += -DF_CPU=24000000
endif

DEFINES     	+= -D$(USB_DEVICE)
$(info $(DEFINES))
DEFINES     	+= -DTEENSYDUINO=159 -DARDUINO=10813 -DLAYOUT_US_ENGLISH

# generate target
USR_BASE = $(CURDIR)
PROJECT := $(notdir $(USR_BASE))
TARGET_NAME     := $(PROJECT)_$(BOARD_ID)
$(info Target is : $(TARGET_NAME))

#link to local libraries
LIBS_LOCAL_BASE 	:= $(HOMEPATH)\Documents\Arduino\libraries

ROOT1 := $(HOMEPATH)\AppData\Local\Arduino15\packages\$(BOARD_CLASS)

PJRC_BASE       	:= $(ROOT1)\tools\teensy-tools\$(TDVERSION)
GCC_BASE        	:= $(ROOT1)/tools/teensy-compile/11.3.1/arm/bin

$(info $(PJRC_BASE))

LIBS_SHARED_BASE  	:= $(ROOT1)/hardware/avr/$(TDVERSION)/libraries

#******************************************************************************
# Folders and Files
#******************************************************************************

# local source/library locations
USR_SRC     	:= src

CORE_BASE       := $(ROOT1)/hardware/avr/$(TDVERSION)/cores/$(Family)/
CORE_SRC 	    := $(CORE_BASE)

#
BIN         := .vsteensy/build
USR_BIN     := $(BIN)/src
CORE_BIN    := $(BIN)/core
LIB_BIN     := $(BIN)/lib
CORE_LIB    := $(BIN)/core.a
TARGET_HEX  := $(BIN)/$(TARGET_NAME).hex
TARGET_ELF  := $(BIN)/$(TARGET_NAME).elf
TARGET_LST  := $(BIN)/$(TARGET_NAME).lst

#******************************************************************************
# BINARIES
#******************************************************************************
CC          := $(GCC_BASE)/arm-none-eabi-gcc
CXX         := $(GCC_BASE)/arm-none-eabi-g++
AR          := $(GCC_BASE)/arm-none-eabi-gcc-ar
OBJCOPY     := $(GCC_BASE)/arm-none-eabi-objcopy
SIZE        := $(GCC_BASE)/arm-none-eabi-size
OBJDUMP     := $(GCC_BASE)/arm-none-eabi-objdump

UPL_PJRC    := $(PJRC_BASE)/teensy_post_compile -test -file=$(TARGET_NAME) 
UPL_PJRC    += -path=$(BIN) -tools=$(PJRC_BASE) -board=$(BOARD_ID) -reboot

#******************************************************************************
# Source and Include Files
#******************************************************************************

#User Sources -----------------------------------------------------------------
INO_FILES  		:= $(call rwildcard,$(USR_BASE)/,*.ino)
INO_OBJ			:= $(INO_FILES:$(USR_BASE)/%.ino=$(USR_BIN)/%.o)
#
USR_S_FILES    := $(call rwildcard,$(USR_SRC)/,*.S)
USR_C_FILES    := $(call rwildcard,$(USR_SRC)/,*.c)
USR_CPP_FILES  := $(call rwildcard,$(USR_SRC)/,*.cpp) 
USR_OBJ        += $(USR_S_FILES:$(USR_SRC)/%.S=$(USR_BIN)/%.o) 
USR_OBJ        += $(USR_C_FILES:$(USR_SRC)/%.c=$(USR_BIN)/%.o) 
USR_OBJ        += $(USR_CPP_FILES:$(USR_SRC)/%.cpp=$(USR_BIN)/%.o)

# Core library sources --------------------------------------------------------
CORE_CPP_FILES := $(call rwildcard,$(CORE_SRC)/,*.cpp)
CORE_C_FILES   := $(call rwildcard,$(CORE_SRC)/,*.c)
CORE_S_FILES   := $(call rwildcard,$(CORE_SRC)/,*.S)
CORE_OBJ       := $(CORE_S_FILES:$(CORE_SRC)/%.S=$(CORE_BIN)/%.o) 
CORE_OBJ       += $(CORE_C_FILES:$(CORE_SRC)/%.c=$(CORE_BIN)/%.o)
CORE_OBJ       += $(CORE_CPP_FILES:$(CORE_SRC)/%.cpp=$(CORE_BIN)/%.o)


# User library sources  (see) https://github.com/arduino/arduino/wiki/arduino-ide-1.5:-library-specification
LIB_DIRS_SHARED  := $(foreach d, $(LIBS_SHARED), $(LIBS_SHARED_BASE)/$d/ $(LIBS_SHARED_BASE)/$d/utility/)
LIB_DIRS_SHARED  += $(foreach d, $(LIBS_SHARED), $(LIBS_SHARED_BASE)/$d/src/ $(dir $(call rwildcard,$(LIBS_SHARED_BASE)/$d/src/,*/.)))

LIB_DIRS_LOCAL   := $(foreach d, $(LIBS_LOCAL), $(LIBS_LOCAL_BASE)/$d/ $(LIBS_LOCAL_BASE)/$d/utility/ )
LIB_DIRS_LOCAL   += $(foreach d, $(LIBS_LOCAL), $(LIBS_LOCAL_BASE)/$d/src/ $(dir $(call rwildcard,$(LIBS_LOCAL_BASE)/$d/src/,*/.)))

LIB_CPP_SHARED  := $(foreach d, $(LIB_DIRS_SHARED),$(call wildcard,$d*.cpp))
LIB_C_SHARED    := $(foreach d, $(LIB_DIRS_SHARED),$(call wildcard,$d*.c))
LIB_S_SHARED    := $(foreach d, $(LIB_DIRS_SHARED),$(call wildcard,$d*.S))

LIB_CPP_LOCAL   := $(foreach d, $(LIB_DIRS_LOCAL),$(call wildcard,$d/*.cpp))
LIB_C_LOCAL     := $(foreach d, $(LIB_DIRS_LOCAL),$(call wildcard,$d/*.c))
LIB_S_LOCAL     := $(foreach d, $(LIB_DIRS_LOCAL),$(call wildcard,$d/*.S))

LIB_OBJ         := $(LIB_CPP_SHARED:$(LIBS_SHARED_BASE)/%.cpp=$(LIB_BIN)/%.o)  
LIB_OBJ         += $(LIB_CPP_LOCAL:$(LIBS_LOCAL_BASE)/%.cpp=$(LIB_BIN)/%.o)
LIB_OBJ         += $(LIB_C_SHARED:$(LIBS_SHARED_BASE)/%.c=$(LIB_BIN)/%.o) 
LIB_OBJ         += $(LIB_C_LOCAL:$(LIBS_LOCAL_BASE)/%.c=$(LIB_BIN)/%.o)
LIB_OBJ         += $(LIB_S_SHARED:$(LIBS_SHARED_BASE)/%.S=$(LIB_BIN)/%.o)  
LIB_OBJ         += $(LIB_S_LOCAL:$(LIBS_LOCAL_BASE)/%.S=$(LIB_BIN)/%.o)


# Includes -------------------------------------------------------------
INCLUDE        := -I./$(USR_SRC) -I$(CORE_SRC)
INCLUDE        += $(foreach d, $(LIB_DIRS_SHARED), -I$d)
INCLUDE        += $(foreach d, $(LIB_DIRS_LOCAL), -I$d)


# Generate directories --------------------------------------------------------
DIRECTORIES :=  $(sort $(dir $(CORE_OBJ) $(USR_OBJ) $(LIB_OBJ)))
generateDirs := $(foreach d, $(DIRECTORIES), $(shell if(-not(test-path "$d")) {mkdir "$d"}))


# force compilation of config to update version date
$(shell (ls $(USR_SRC)/Config.h).LastWriteTime = get-Date)

#******************************************************************************
# Flags and Defines
#******************************************************************************
FLAGS_OPT   := -Os --specs=nano.specs -u _printf_float -u _scanf_float
FLAGS_COM   := -g -Wall -ffunction-sections -fdata-sections -nostdlib -MMD
FLAGS_LSP   := 

FLAGS_CPP   := -std=gnu++17 -fno-exceptions -fpermissive -fno-rtti -fno-threadsafe-statics
FLAGS_CPP   += -felide-constructors -Wno-error=narrowing
FLAGS_C     := 
FLAGS_S     := -x assembler-with-cpp

FLAGS_LD    := -Wl,--gc-sections,--relax,--print-memory-usage,--defsym=__rtc_localtime=$(NOW)
FLAGS_LD    += -T$(CORE_BASE)/$(LDFILE)

LIBS		+= -lm -lstdc++

CPP_FLAGS   := $(FLAGS_CPU) $(FLAGS_OPT) $(FLAGS_COM) $(DEFINES) $(FLAGS_CPP)
C_FLAGS     := $(FLAGS_CPU) $(FLAGS_OPT) $(FLAGS_COM) $(DEFINES) $(FLAGS_C)
S_FLAGS     := $(FLAGS_CPU) $(FLAGS_OPT) $(FLAGS_COM) $(DEFINES) $(FLAGS_S)
LD_FLAGS    := $(FLAGS_CPU) $(FLAGS_OPT) $(FLAGS_LSP) $(FLAGS_LD)
AR_FLAGS    := rcs

#******************************************************************************
# Rules:
#******************************************************************************

.PHONY: directories all rebuild upload clean cleanUser cleanCore cleanLib

all:  $(TARGET_LST) $(TARGET_HEX)

rebuild: clean all

clean:   cleanUser cleanCore cleanLib
	$(info cleaning done)

upload: all
	$(info upload:  $(UPL_PJRC))	
	@$(UPL_PJRC)


# Core library ----------------------------------------------------------------
$(CORE_BIN)/%.o: $(CORE_SRC)/%.S
	$(info CORE [ASM] $(notdir $<))
	@$(CC) $(S_FLAGS) $(INCLUDE) -o $@ -c $<

$(CORE_BIN)/%.o: $(CORE_SRC)/%.c
	$(info CORE [CC] $(notdir $<))
	@$(CC) $(C_FLAGS) $(INCLUDE) -o $@ -c $<

$(CORE_BIN)/%.o: $(CORE_SRC)/%.cpp
	$(info CORE [CPP] $(notdir $<))
	@$(CXX) $(CPP_FLAGS) $(INCLUDE) -o $@ -c $<

$(CORE_LIB) : $(CORE_OBJ)
	$(info CORE [AR])
	@$(AR) $(AR_FLAGS) $@ $^

# Shared Libraries ------------------------------------------------------------
$(LIB_BIN)/%.o: $(LIBS_SHARED_BASE)/%.S
	$(info LIB [ASM] $(notdir $<))
	@$(CC) $(S_FLAGS) $(INCLUDE) -o $@ -c $<

$(LIB_BIN)/%.o: $(LIBS_SHARED_BASE)/%.cpp
	$(info LIB [CPP] $(notdir $<))
	@$(CXX) $(CPP_FLAGS) $(INCLUDE) -o $@ -c $<

$(LIB_BIN)/%.o: $(LIBS_SHARED_BASE)/%.c
	$(info LIB [CC] $(notdir $<))
	@$(CC) $(C_FLAGS) $(INCLUDE) -o $@ -c $<

# Local Libraries -------------------------------------------------------------
$(LIB_BIN)/%.o: $(LIBS_LOCAL_BASE)/%.S
	$(info LIB [ASM] $(notdir $<))
	@$(CC) $(S_FLAGS) $(INCLUDE) -o $@ -c $<

$(LIB_BIN)/%.o: $(LIBS_LOCAL_BASE)/%.cpp
	$(info LIB [CPP] $(notdir $<))
	@$(CXX) $(CPP_FLAGS) $(INCLUDE) -o $@ -c $<

$(LIB_BIN)/%.o: $(LIBS_LOCAL_BASE)/%.c
	$(info LIB [CC] $(notdir $<))
	@$(CC) $(C_FLAGS) $(INCLUDE) -o $@ -c $<


# Handle user sources ---------------------------------------------------------
$(USR_BIN)/%.o: $(USR_SRC)/%.S
	$(info USER [ASM] $(notdir $<))
	@$(CC) $(S_FLAGS) $(INCLUDE) -o $@ -c $< 

$(USR_BIN)/%.o: $(USR_SRC)/%.c
	$(info USER [CC] $(notdir $<))
	@$(CC) $(C_FLAGS) $(INCLUDE) -o $@ -c $<

$(USR_BIN)/%.o: $(USR_SRC)/%.cpp
	$(info USER [CPP] $(notdir $<))
	@$(CXX) $(CPP_FLAGS) $(INCLUDE) -o $@ -c $<

$(USR_BIN)/%.o: $(USR_BASE)/%.ino
	$(info USER [INO] $(notdir $<))
	@$(CC) $(CPP_FLAGS) $(INCLUDE) -x c++ -o $@ -c $< -x none

# Linking ---------------------------------------------------------------------
$(TARGET_ELF): $(CORE_LIB) $(LIB_OBJ) $(USR_OBJ) $(INO_OBJ)
	$(info [LD] $@)
	@$(CC) $(LD_FLAGS) -o $@ $(INO_OBJ) $(USR_OBJ) $(LIB_OBJ) $(CORE_LIB) $(LIBS)

%.lst: %.elf
	@$(info [LST] $@)
	@$(OBJDUMP) -d -S --demangle --no-show-raw-insn --syms $<  > $@	

%.hex: %.elf
	$(info [HEX] $@)
	@$(SIZE) $<
	@$(OBJCOPY) -O ihex -R.eeprom $< $@
	
	
# Cleaning --------------------------------------------------------------------
cleanUser:
	$(info Cleaning user binaries...)
	$(shell if((test-path "$(USR_BIN)")) {rm -recurse -force  "$(USR_BIN)"})
	$(shell if((test-path "$(TARGET_ELF)")) {rm  "$(TARGET_ELF)"})
	$(shell if((test-path "$(TARGET_HEX)")) {rm  "$(TARGET_HEX)"})
	$(shell if((test-path "$(TARGET_LST)")) {rm  "$(TARGET_LST)"})

cleanCore:
	$(info Cleaning core binaries...)
	$(shell if((test-path "$(CORE_BIN)")) {rm -recurse -force  "$(CORE_BIN)"})
	$(shell if((test-path "$(CORE_LIB)")) {rm  "$(CORE_LIB)"})

cleanLib:
	$(info Cleaning user library binaries...)
	$(shell if((test-path "$(LIB_BIN)")) {rm -recurse -force  "$(LIB_BIN)"})

# compiler generated dependency info ------------------------------------------
-include $(CORE_OBJ:.o=.d)
-include $(USR_OBJ:.o=.d)
-include $(LIB_OBJ:.o=.d)
-include $(INO_OBJ:.o=.d)
