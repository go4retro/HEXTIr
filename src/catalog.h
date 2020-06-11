#ifndef SRC_CATALOG_H_
#define SRC_CATALOG_H_

typedef struct {
	UINT pgmlen;
	UCHAR linenumber;
	FIL *fp;
} Catalog;

void  Catalog_init(Catalog* self);
UCHAR Catalog_open(Catalog* self, FATFS* fs, const char* fname);
void  Catalog_write(Catalog* self, const char* cstr);
void  Catalog_close(Catalog*);

#endif /* SRC_CATALOG_H_ */
