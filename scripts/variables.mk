# architecture-dependent variables

#---------------- Source code ----------------
ASMSRC = crc7asm.S

#---------------- Toolchain ----------------
CC = avr-gcc
CPP = avr-g++
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE = avr-size
NM = avr-nm
AVRDUDE = avrdude


#---------------- Bootloader, fuses etc. ----------------
# Set MCU name and length of binary for bootloader
# WARNING: Fuse settings not tested!
MCU    := $(CONFIG_MCU)
CRCGEN := scripts/crcgen-avr.pl

ifeq ($(MCU),atmega128)
  BINARY_LENGTH = 0x1f000
#  EFUSE = 0xff
#  HFUSE = 0x91
#  LFUSE = 0xaf
else ifeq ($(MCU),atmega1281)
  BINARY_LENGTH = 0x1f000
  BOOTLDRSIZE = 0x0800
  EFUSE = 0xff
  HFUSE = 0xd2
  LFUSE = 0xfc
else ifeq ($(MCU),atmega2561)
  BINARY_LENGTH = 0x3f000
  EFUSE = 0xfd
  HFUSE = 0x93
  LFUSE = 0xef
else ifeq ($(MCU),atmega644)
  BINARY_LENGTH = 0xf000
  EFUSE = 0xfd
  HFUSE = 0x91
  LFUSE = 0xef
else ifeq ($(MCU),atmega644p)
  BINARY_LENGTH = 0xf000
  EFUSE = 0xfd
  HFUSE = 0x91
  LFUSE = 0xef
else ifeq ($(MCU),atmega1284p)
  BINARY_LENGTH = 0x1f000
  EFUSE = 0xfd
  HFUSE = 0xd2
  LFUSE = 0xe7
else ifeq ($(MCU),atmega168)
  BINARY_LENGTH = 0x4000
  EFUSE = 0xf9
  HFUSE = 0xdf
  LFUSE = 0xff
else ifeq ($(MCU),atmega328)
  BINARY_LENGTH = 0x8000
  EFUSE = 0xf9
  HFUSE = 0xdf
  LFUSE = 0xff
else ifeq ($(MCU),atmega328p)
  BINARY_LENGTH = 0x8000
  EFUSE = 0xf9
  HFUSE = 0xdf
  LFUSE = 0xff
else
.PHONY: nochip
nochip:
	@echo '=============================================================='
	@echo 'No known target chip specified.'
	@echo
	@echo 'Please edit the Makefile.'
	@exit 1
endif

#---------------- External Memory Options ----------------

# 64 KB of external RAM, starting after internal RAM (ATmega128!),
# used for variables (.data/.bss) and heap (malloc()).
#EXTMEMOPTS = -Wl,-Tdata=0x801100,--defsym=__heap_end=0x80ffff

# 64 KB of external RAM, starting after internal RAM (ATmega128!),
# only used for heap (malloc()).
#EXTMEMOPTS = -Wl,--defsym=__heap_start=0x801100,--defsym=__heap_end=0x80ffff

EXTMEMOPTS =

#---------------- Programming Options (avrdude) ----------------

# Programming hardware: alf avr910 avrisp bascom bsd
# dt006 pavr picoweb pony-stk200 sp12 stk200 stk500 stk500v2
#
# Type: avrdude -c ?
# to get a full listing.
#
#AVRDUDE_PROGRAMMER = stk200
ifdef CONFIG_AVRDUDE_PROGRAMMER
  AVRDUDE_PROGRAMMER := -c $(CONFIG_AVRDUDE_PROGRAMMER)
endif

# com1 = serial port. Use lpt1 to connect to parallel port.
#AVRDUDE_PORT = lpt1    # programmer connected to serial device
ifdef CONFIG_AVRDUDE_PORT
  AVRDUDE_PORT := -P $(CONFIG_AVRDUDE_PORT)
endif

AVRDUDE_WRITE_FLASH = -U flash:w:$(TARGET).hex
# AVRDUDE_WRITE_EEPROM = -U eeprom:w:$(TARGET).eep

# Allow fuse overrides from the config file
ifdef CONFIG_EFUSE
  EFUSE := $(CONFIG_EFUSE)
endif
ifdef CONFIG_HFUSE
  HFUSE := $(CONFIG_HFUSE)
endif
ifdef CONFIG_LFUSE
  LFUSE := $(CONFIG_LFUSE)
endif

# Calculate command line arguments for fuses
AVRDUDE_WRITE_FUSES :=
ifdef EFUSE
  AVRDUDE_WRITE_FUSES += -U efuse:w:$(EFUSE):m
endif
ifdef HFUSE
  AVRDUDE_WRITE_FUSES += -U hfuse:w:$(HFUSE):m
endif
ifdef LFUSE
  AVRDUDE_WRITE_FUSES += -U lfuse:w:$(LFUSE):m
endif


# Uncomment the following if you want avrdude's erase cycle counter.
# Note that this counter needs to be initialized first using -Yn,
# see avrdude manual.
#AVRDUDE_ERASE_COUNTER = -y

# Uncomment the following if you do /not/ wish a verification to be
# performed after programming the device.
#AVRDUDE_NO_VERIFY = -V

# Increase verbosity level.  Please use this when submitting bug
# reports about avrdude. See <http://savannah.nongnu.org/projects/avrdude>
# to submit bug reports.
#AVRDUDE_VERBOSE = -v -v

AVRDUDE_FLAGS = -p $(MCU)
AVRDUDE_FLAGS += $(AVRDUDE_NO_VERIFY)
AVRDUDE_FLAGS += $(AVRDUDE_VERBOSE)
AVRDUDE_FLAGS += $(AVRDUDE_ERASE_COUNTER)
AVRDUDE_FLAGS += $(AVRDUDE_PROGRAMMER) $(AVRDUDE_PORT)

#---------------- Architecture variables ----------------
ARCH_CFLAGS  = -mmcu=$(MCU) -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums -mcall-prologues
ARCH_ASFLAGS = -mmcu=$(MCU)
ARCH_LDFLAGS = $(EXTMEMOPTS)

# these are needed for GCC 4.3.2, which is more aggressive at inlining
# gcc-4.2 knows one of those, but it tends to increase code size
ifeq ($(shell $(CC) --version|scripts/gcctest.pl),YES)
#ARCH_CFLAGS += --param inline-call-cost=3
ARCH_CFLAGS += -fno-inline-small-functions
ARCH_CFLAGS += -fno-move-loop-invariants
ARCH_CFLAGS += -fno-split-wide-types

# turn these on to keep the functions in the same order as in the source
# this is only useful if you're looking at disassembly
#ARCH_CFLAGS += -fno-reorder-blocks
#ARCH_CFLAGS += -fno-reorder-blocks-and-partition
#ARCH_CFLAGS += -fno-reorder-functions
#ARCH_CFLAGS += -fno-toplevel-reorder
endif

#---------------- Config ----------------
ifeq ($(CONFIG_STACK_TRACKING),y)
  ARCH_CFLAGS += -finstrument-functions
endif

