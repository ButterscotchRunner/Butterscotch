#ifndef _BS_MAP_FILE_H_
#define _BS_MAP_FILE_H_

#include <stdint.h>
#include <stdio.h>

uint8_t *mapFile(FILE *file, size_t size);
void unmapFile(uint8_t *ptr, size_t size);

#endif /* _BS_MAP_FILE_H_ */
