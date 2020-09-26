#ifndef CATALOG_H
#define CATALOG_H

#include "drive.h" // for file_t

hexstatus_t hex_read_catalog(file_t* file);
hexstatus_t hex_read_catalog_txt(file_t* file);
hexstatus_t hex_open_catalog(file_t *file, uint8_t lun, uint8_t att, char* path);

#endif /* SRC_CATALOG_H */
