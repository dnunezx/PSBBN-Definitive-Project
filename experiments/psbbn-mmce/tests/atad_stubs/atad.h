#define ATA_DIR_READ 0
#define ATA_DIR_WRITE 1
#define ATA_F_SEC_ENABLED 2
#define ATA_F_SEC_LOCKED 4
typedef struct { s32 exists, has_packet; u32 total_sectors, security_status; } ata_devinfo_t;
