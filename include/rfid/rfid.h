#ifndef _RFID_H
#define _RFID_H

#include <stdio.h>


#define DEBUGP(x, args ...) fprintf(stderr, "%s(%d):%s: " x, __FILE__, __LINE__, __FUNCTION__, ## args)
extern const char *rfid_hexdump(const void *data, unsigned int len);

int rfid_init();

#endif /* _RFID_H */
