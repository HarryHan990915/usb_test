/* Read-only FAT12 volume, generated on the fly in STORAGE_Read_HS() —
 * no disk image is stored in flash. Header-only and included from a single
 * translation unit (usbd_storage_if.c) so install_html[] is never duplicated.
 *
 * Volume contents: START_HERE.html (install_html[]) + autorun.inf.
 */
#ifndef __FAT12_DISK_H__
#define __FAT12_DISK_H__

#include <stdint.h>
#include <string.h>
#include "stm32h7xx_hal.h"
#include "install_html.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- fixed small volume geometry ------------------------------------- */
#define FAT12_BYTES_PER_SECTOR   512U
#define FAT12_SEC_PER_CLUS       1U
#define FAT12_RESERVED_SECS      1U
#define FAT12_NUM_FATS           2U
#define FAT12_ROOT_ENTRIES       16U

#define FAT12__DIV_CEIL(a, b)    (((a) + (b) - 1U) / (b))

#define FAT12_ROOT_DIR_SECTORS   FAT12__DIV_CEIL((FAT12_ROOT_ENTRIES * 32U), FAT12_BYTES_PER_SECTOR)

#define FAT12_AUTORUN_CLUSTERS   1U
#define FAT12_HTML_CLUSTERS      FAT12__DIV_CEIL(INSTALL_HTML_LEN, (FAT12_BYTES_PER_SECTOR * FAT12_SEC_PER_CLUS))
#define FAT12_DATA_CLUSTERS      (FAT12_AUTORUN_CLUSTERS + FAT12_HTML_CLUSTERS)
#define FAT12_DATA_SECTORS       (FAT12_DATA_CLUSTERS * FAT12_SEC_PER_CLUS)

/* FAT12 packs 2 entries into 3 bytes; entries 0 and 1 are reserved. */
#define FAT12_FAT_ENTRIES        (FAT12_DATA_CLUSTERS + 2U)
#define FAT12_FAT_SECTORS        FAT12__DIV_CEIL(FAT12__DIV_CEIL((FAT12_FAT_ENTRIES * 3U), 2U), FAT12_BYTES_PER_SECTOR)

#define FAT12_FAT1_LBA           FAT12_RESERVED_SECS
#define FAT12_ROOT_DIR_LBA       (FAT12_FAT1_LBA + (FAT12_NUM_FATS * FAT12_FAT_SECTORS))
#define FAT12_DATA_AREA_LBA      (FAT12_ROOT_DIR_LBA + FAT12_ROOT_DIR_SECTORS)
#define FAT12_TOTAL_SECTORS      (FAT12_DATA_AREA_LBA + FAT12_DATA_SECTORS)

#define FAT12_AUTORUN_CLUSTER      2U
#define FAT12_HTML_FIRST_CLUSTER   (FAT12_AUTORUN_CLUSTER + FAT12_AUTORUN_CLUSTERS)

static const uint8_t fat12_autorun_inf[] =
  "[autorun]\r\n"
  "label=PASSMARK\r\n";
#define FAT12_AUTORUN_INF_LEN   (sizeof(fat12_autorun_inf) - 1U) /* drop trailing NUL */

/* ---- little helpers ---------------------------------------------------- */
static inline void fat12_put_u16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static inline void fat12_put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t fat12_volume_serial(void)
{
  return HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
}

static inline uint8_t fat12_lfn_checksum(const uint8_t short_name[11])
{
  uint8_t sum = 0U;
  for (uint8_t i = 0U; i < 11U; i++)
  {
    sum = (uint8_t)((uint8_t)((sum & 1U) ? 0x80U : 0U) + (uint8_t)(sum >> 1) + short_name[i]);
  }
  return sum;
}

/* ---- boot sector --------------------------------------------------------
 * Standard FAT12 BS/BPB layout (see Microsoft "FAT: General Overview of
 * On-Disk Format", fatgen103).
 */
static inline void fat12_build_boot_sector(uint8_t *s)
{
  memset(s, 0, FAT12_BYTES_PER_SECTOR);

  s[0] = 0xEB; s[1] = 0x3C; s[2] = 0x90;         /* BS_jmpBoot */
  memcpy(&s[3], "PASSMARK", 8);                   /* BS_OEMName */
  fat12_put_u16(&s[11], FAT12_BYTES_PER_SECTOR);  /* BPB_BytsPerSec */
  s[13] = FAT12_SEC_PER_CLUS;                     /* BPB_SecPerClus */
  fat12_put_u16(&s[14], FAT12_RESERVED_SECS);     /* BPB_RsvdSecCnt */
  s[16] = FAT12_NUM_FATS;                         /* BPB_NumFATs */
  fat12_put_u16(&s[17], FAT12_ROOT_ENTRIES);      /* BPB_RootEntCnt */
  fat12_put_u16(&s[19], FAT12_TOTAL_SECTORS);     /* BPB_TotSec16 */
  s[21] = 0xF8;                                   /* BPB_Media: fixed disk */
  fat12_put_u16(&s[22], FAT12_FAT_SECTORS);       /* BPB_FATSz16 */
  fat12_put_u16(&s[24], 32U);                     /* BPB_SecPerTrk (unused by USB MSC) */
  fat12_put_u16(&s[26], 64U);                     /* BPB_NumHeads (unused by USB MSC) */
  fat12_put_u32(&s[28], 0U);                      /* BPB_HiddSec */
  fat12_put_u32(&s[32], 0U);                      /* BPB_TotSec32 (unused, TotSec16 covers it) */

  s[36] = 0x80;                                   /* BS_DrvNum */
  s[37] = 0x00;                                   /* BS_Reserved1 */
  s[38] = 0x29;                                   /* BS_BootSig */
  fat12_put_u32(&s[39], fat12_volume_serial());   /* BS_VolID */
  memcpy(&s[43], "PASSMARK   ", 11);              /* BS_VolLab */
  memcpy(&s[54], "FAT12   ", 8);                  /* BS_FilSysType */

  s[510] = 0x55; s[511] = 0xAA;                   /* boot sector signature */
}

/* ---- FAT table (identical content written to both FAT copies) ---------- */
static inline void fat12_set12(uint8_t *fat, uint32_t index, uint16_t value)
{
  uint32_t off = (index * 3U) / 2U;

  if ((index & 1U) == 0U)
  {
    fat[off]     = (uint8_t)value;
    fat[off + 1] = (uint8_t)((fat[off + 1] & 0xF0U) | ((value >> 8) & 0x0FU));
  }
  else
  {
    fat[off]     = (uint8_t)((fat[off] & 0x0FU) | ((value & 0x0FU) << 4));
    fat[off + 1] = (uint8_t)(value >> 4);
  }
}

static inline void fat12_build_fat_sector(uint32_t fat_sector_index, uint8_t *s)
{
  uint8_t fat[FAT12_FAT_SECTORS * FAT12_BYTES_PER_SECTOR];
  uint32_t i;

  memset(fat, 0, sizeof(fat));

  fat12_set12(fat, 0U, 0xF00U | 0xF8U); /* [0]: media descriptor */
  fat12_set12(fat, 1U, 0xFFFU);         /* [1]: reserved */

  fat12_set12(fat, FAT12_AUTORUN_CLUSTER, 0xFFFU); /* autorun.inf: single cluster */

  for (i = 0U; i < FAT12_HTML_CLUSTERS; i++)
  {
    uint32_t c = FAT12_HTML_FIRST_CLUSTER + i;
    uint16_t next = ((i + 1U) < FAT12_HTML_CLUSTERS) ? (uint16_t)(c + 1U) : 0xFFFU;
    fat12_set12(fat, c, next);
  }

  memcpy(s, &fat[fat_sector_index * FAT12_BYTES_PER_SECTOR], FAT12_BYTES_PER_SECTOR);
}

/* ---- root directory -----------------------------------------------------
 * Entries: volume label, AUTORUN.INF, and START_HERE.html (as a VFAT long
 * name pointing at short-name alias STARTHER.HTM).
 */
static inline void fat12_build_root_dir(uint32_t sector_in_root, uint8_t *s)
{
  static const uint8_t short_name[11] = "STARTHERHTM";
  static const uint16_t name_u16[15] =
  {
    'S', 'T', 'A', 'R', 'T', '_', 'H', 'E', 'R', 'E', '.', 'h', 't', 'm', 'l'
  };
  uint8_t chk;
  uint8_t *e;
  uint16_t chunk[13];
  uint32_t i;

  memset(s, 0, FAT12_BYTES_PER_SECTOR);

  if (sector_in_root != 0U)
  {
    return; /* all entries fit in the first root-directory sector */
  }

  e = s;

  /* volume label */
  memcpy(&e[0], "PASSMARK   ", 11);
  e[11] = 0x08; /* ATTR_VOLUME_ID */
  e += 32;

  /* AUTORUN.INF */
  memcpy(&e[0], "AUTORUN INF", 11);
  e[11] = 0x21; /* ATTR_READ_ONLY | ATTR_ARCHIVE */
  fat12_put_u16(&e[26], FAT12_AUTORUN_CLUSTER); /* FstClusLO */
  fat12_put_u32(&e[28], FAT12_AUTORUN_INF_LEN);
  e += 32;

  chk = fat12_lfn_checksum(short_name);

  /* LFN entry, sequence 2 (last/highest), characters 14-15 ("ml") */
  for (i = 0U; i < 13U; i++)
  {
    chunk[i] = 0xFFFFU;
  }
  chunk[0] = name_u16[13];
  chunk[1] = name_u16[14];
  chunk[2] = 0x0000U;

  e[0] = 0x42; /* ordinal 2 | last-logical-entry flag */
  memcpy(&e[1],  &chunk[0], 5U * 2U);
  e[11] = 0x0F; /* ATTR_LONG_NAME */
  e[12] = 0x00;
  e[13] = chk;
  memcpy(&e[14], &chunk[5], 6U * 2U);
  fat12_put_u16(&e[26], 0U);
  memcpy(&e[28], &chunk[11], 2U * 2U);
  e += 32;

  /* LFN entry, sequence 1, characters 1-13 ("START_HERE.ht") */
  for (i = 0U; i < 13U; i++)
  {
    chunk[i] = name_u16[i];
  }

  e[0] = 0x01;
  memcpy(&e[1],  &chunk[0], 5U * 2U);
  e[11] = 0x0F;
  e[12] = 0x00;
  e[13] = chk;
  memcpy(&e[14], &chunk[5], 6U * 2U);
  fat12_put_u16(&e[26], 0U);
  memcpy(&e[28], &chunk[11], 2U * 2U);
  e += 32;

  /* short 8.3 entry backing the long name */
  memcpy(&e[0], short_name, 11);
  e[11] = 0x21; /* ATTR_READ_ONLY | ATTR_ARCHIVE */
  fat12_put_u16(&e[26], FAT12_HTML_FIRST_CLUSTER);
  fat12_put_u32(&e[28], INSTALL_HTML_LEN);
}

/* ---- data clusters ------------------------------------------------------- */
static inline void fat12_build_data_sector(uint32_t lba, uint8_t *s)
{
  uint32_t data_sector_index = lba - FAT12_DATA_AREA_LBA;
  uint32_t cluster = 2U + (data_sector_index / FAT12_SEC_PER_CLUS);

  memset(s, 0, FAT12_BYTES_PER_SECTOR);

  if (cluster == FAT12_AUTORUN_CLUSTER)
  {
    memcpy(s, fat12_autorun_inf, FAT12_AUTORUN_INF_LEN);
    return;
  }

  if ((cluster >= FAT12_HTML_FIRST_CLUSTER) &&
      (cluster < (FAT12_HTML_FIRST_CLUSTER + FAT12_HTML_CLUSTERS)))
  {
    uint32_t cluster_in_file = cluster - FAT12_HTML_FIRST_CLUSTER;
    uint32_t byte_off = cluster_in_file * FAT12_BYTES_PER_SECTOR;

    if (byte_off < INSTALL_HTML_LEN)
    {
      uint32_t n = INSTALL_HTML_LEN - byte_off;

      if (n > FAT12_BYTES_PER_SECTOR)
      {
        n = FAT12_BYTES_PER_SECTOR;
      }
      memcpy(s, &install_html[byte_off], n);
    }
  }
}

/* ---- top-level dispatch --------------------------------------------------- */
static inline void FAT12_BuildSector(uint32_t lba, uint8_t *out)
{
  if (lba == 0U)
  {
    fat12_build_boot_sector(out);
  }
  else if (lba < FAT12_ROOT_DIR_LBA)
  {
    uint32_t fat_rel = (lba - FAT12_FAT1_LBA) % FAT12_FAT_SECTORS;
    fat12_build_fat_sector(fat_rel, out);
  }
  else if (lba < FAT12_DATA_AREA_LBA)
  {
    fat12_build_root_dir(lba - FAT12_ROOT_DIR_LBA, out);
  }
  else if (lba < FAT12_TOTAL_SECTORS)
  {
    fat12_build_data_sector(lba, out);
  }
  else
  {
    memset(out, 0, FAT12_BYTES_PER_SECTOR);
  }
}

#ifdef __cplusplus
}
#endif

#endif /* __FAT12_DISK_H__ */
