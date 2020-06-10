#ifndef SRC_CATALOG_H_
#define SRC_CATALOG_H_


UCHAR catalog_open(FATFS* fs, const char* fname);
void catalog_write(const char* cstr);
void catalog_close(void);

#endif /* SRC_CATALOG_H_ */
