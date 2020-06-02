
/*
 * registry.h by s.reid  1 June 2020
 * 
 */
#ifndef REGISTRY_H
#define REGISTRY_H

#define MAX_REGISTRY      6

typedef uint8_t (*cmd_proc)(pab_t pab);

#define HEXCMD_INVALID_MARKER     0xAA     // This is used as a marker in our registry to indicate end of operations table. It is NOT a valid HexBus command code.

typedef struct _registry_entry {
  uint8_t device_code_start;
  uint8_t device_code_end;
  cmd_proc *operation;
  uint8_t  *command;
} registry_entry_t;

typedef struct _registry_t {
  uint8_t num_devices;
  registry_entry_t entry[MAX_REGISTRY];
} registry_t;


#endif
