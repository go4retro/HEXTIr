#ifndef SRC_CATALOG_H_
#define SRC_CATALOG_H_

/**
 * Send PGM header bytes and length on hex-bus.
 * num : Number of catalog entries.
 */
void pgm_cat_open(uint16_t num);

/**
 * Send catalog entry on hex-bus.
 */
void pgm_cat_record(uint16_t lineno, uint32_t fsize, const char* filename, char attrib);

/**
 * Send PGM trailer bytes on hex-bus.
 */
void pgm_cat_close(void);


/**
 * Get number of directory (=catalog) entries.
 * directory : The path to the directory.
 */
uint16_t cat_get_length(const char* directory);

/**
 * Calculate the PGM file length.
 * dirnum : The number of directory entries.
 */
uint16_t pgm_file_length(uint16_t dirnum);



#endif /* SRC_CATALOG_H_ */
