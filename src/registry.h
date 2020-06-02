
/*
 * registry.h by s.reid  1 June 2020
 * 
 */
#ifndef REGISTRY_H_
#define REGISTRY_H_

#define MAX_REGISTRY      6

typedef uint8_t (*cmd_proc)(pab_t pab);

#define HEXCMD_INVALID_MARKER     0xAA     // This is used as a marker in our registry to indicate end of operations table. It is NOT a valid HexBus command code.

typedef struct _registry_entry {
  uint8_t device_code_start;
  uint8_t device_code_end;
  cmd_proc *operation;
  uint8_t  *command;
} REGISTRY_ENTRY;

typedef struct _registry {
  uint8_t num_devices;
  REGISTRY_ENTRY entry[MAX_REGISTRY];
} REGISTRY;


#endif
