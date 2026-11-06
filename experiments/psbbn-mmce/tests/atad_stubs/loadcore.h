#include <stdint.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef int32_t s32;
struct irx_export_table { int unused; };
#define IRX_ID(name, major, minor)
#define MODULE_NO_RESIDENT_END 1
#define MODULE_RESIDENT_END 0
int RegisterLibraryEntries(struct irx_export_table *table);
