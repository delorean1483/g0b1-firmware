#ifndef APP_TYPES_H
#define APP_TYPES_H
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  UINT8;
typedef uint8_t  BYTE;
typedef uint16_t UINT16;
typedef uint16_t WORD;
typedef int16_t  INT16;
typedef uint32_t UINT32;
typedef int32_t  INT32;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#ifndef ON
#define ON  1
#define OFF 0
#endif
#endif /* APP_TYPES_H */
