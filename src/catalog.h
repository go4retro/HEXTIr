#ifndef SRC_CATALOG_H_
#define SRC_CATALOG_H_

/**
 * Send PGM header bytes and length on hex-bus.
 * num : Number of catalog entries.
 */
void pgm_cat_open(uint16_t num_entries);

/**
 * Send catalog entry on hex-bus.
 */
void pgm_cat_record(uint16_t lineno, uint32_t fsize, const char* filename, char attrib);

/**
 * Send PGM trailer bytes on hex-bus.
 */
void pgm_cat_close(void);

/**
 * Calculate the PGM file length.
 */
uint16_t pgm_cat_file_length(uint16_t num_entries);

/**
 * Get the maximum file length for the OPEN/INPUT text catalog file.
 */
uint16_t txt_max_cat_file_length(void);

/**
 * Get number of directory (=catalog) entries.
 */
uint16_t cat_get_num_entries(FATFS* fsp, const char* directory);

/**
 * Return true if catalog entry shall be skipped.
 */
BOOL cat_skip_file(const char* filename);


char* byte_to_kb(uint32_t bytes, char* buf, uint8_t len);

#endif /* SRC_CATALOG_H_ */
