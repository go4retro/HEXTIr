
/*
 * registry.h by s.reid  1 June 2020
 * 
 */
#ifndef REGISTRY_H
#define REGISTRY_H

#include "hexbus.h"

#ifdef NEW_REG_CNT
#ifdef INCLUDE_PRINTER
#define PRN_ADD       1
#else
#define PRN_ADD       0
#endif
#ifdef INCLUDE_SERIAL
#define SER_ADD       1
#else
#define SER_ADD       0
#endif
#ifdef INCLUDE_RTC
#define RTC_ADD       1
#else
#define RTC_ADD       0
#endif
// TODO  Why do I need to define 4 extra devices when there are only 3?
#ifdef USE_CFG_DEVICE
#define MAX_REGISTRY   (1 + 1 + 1 + 1 + PRN_ADD + SER_ADD + RTC_ADD) // drive, cfg, null device
#else
#define MAX_REGISTRY   (1 + 1 + 1 + PRN_ADD + SER_ADD + RTC_ADD) // drive, null device
#endif
#else
#define MAX_REGISTRY      8
#endif

typedef void (*cmd_proc)(pab_t *pab);

#ifdef USE_NEW_OPTABLE
// This is used as a marker in our registry to indicate end of operations table.
// It is NOT a valid HexBus command code.
#define HEXCMD_INVALID_MARKER     (hexcmdtype_t)0xAA

typedef struct _cmd_op_t {
  hexcmdtype_t command;
  void (*operation)(pab_t*);
} cmd_op_t;

#else
#define HEXCMD_INVALID_MARKER     0xAA
#endif

typedef struct _registry_entry {
  uint8_t dev_low;
  uint8_t dev_cur;
  uint8_t dev_high;
#ifdef USE_NEW_OPTABLE
  cmd_op_t *oplist;
#else
  cmd_proc *operation;
  uint8_t  *command;
#endif
} registry_entry_t;

typedef struct _registry_t {
  uint8_t num_devices;
  registry_entry_t entry[MAX_REGISTRY];
} registry_t;

extern registry_t  registry;

#endif
