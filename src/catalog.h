#ifndef SRC_CATALOG_H_
#define SRC_CATALOG_H_


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


#endif /* SRC_CATALOG_H_ */
