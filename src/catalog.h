#ifndef CATALOG_H
#define CATALOG_H

#include "drive.h" // for file_t

// Send PGM header bytes and length on hex-bus.
void cat_open_pgm(uint16_t num_entries);

//Send catalog entry on hex-bus in PGM format.
void cat_write_record_pgm(uint16_t lineno, uint32_t fsize, const char* filename, char attrib);

//Send PGM trailer bytes on hex-bus.
void cat_close_pgm(void);

// Calculate the PGM file length.
uint16_t cat_file_length_pgm(uint16_t num_entries);


// Get the maximum file length for the OPEN/INPUT text catalog file.
uint16_t cat_max_file_length_txt(void);

// Send catalog entry on hex-bus in text format.
void cat_write_txt(uint16_t* dirnum, uint32_t fsize, const char* filename, char attrib);


// Get number of directory (=catalog) entries.
uint16_t cat_get_num_entries(FATFS* fsp, const char* directory, const char* pattern);

// Return true if catalog entry shall be skipped.
BOOL cat_skip_file(const char* filename, const char* pattern);

// Return true if string matches the pattern.
int wild_cmp(const char* pattern, const char* string);

// Convert bytes to kBytes and format to ##.# . In case size >= 100kB returns 99.9.
char* cat_bytes_to_kb(uint32_t bytes, char* buf, uint8_t len);

uint8_t hex_read_catalog(file_t* file);

uint8_t hex_read_catalog_txt(file_t* file);

uint8_t hex_open_catalog(file_t *file, uint8_t lun, uint8_t att);

#endif /* SRC_CATALOG_H */
