/* Read-only ISO9660 (CD-ROM) volume, generated on the fly in
 * STORAGE_Read_HS() -- no disk image is stored in flash. Header-only and
 * included from a single translation unit (usbd_storage_if.c) so
 * install_html[] is never duplicated.
 *
 * This keeps the device reporting as CD-ROM (SCSI peripheral type 0x05) so
 * Windows' AutoPlay still honors AUTORUN.INF's action=/shellexecute= --
 * that only works for optical media; it's disabled for generic USB disks
 * since Windows 7. See ECMA-119 (ISO 9660) for the on-disk structures.
 *
 * Volume contents: AUTORUN.INF (static) + INSTALL.HTM (install_html[]).
 */
#ifndef __ISO9660_DISK_H__
#define __ISO9660_DISK_H__

#include <stdint.h>
#include <string.h>
#include "install_html.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- fixed small volume geometry --------------------------------------
 * ECMA-119 mandates a 16-sector (32 KB) blank "System Area" before any
 * volume descriptor; that requirement is why the original static image
 * was ~56 KB even though the real content is under 5 KB. Generating it
 * on the fly means that padding costs nothing.
 */
#define ISO_SECTOR_SIZE            2048U
#define ISO_SYSTEM_AREA_SECTORS    16U

#define ISO_PVD_LBA                (ISO_SYSTEM_AREA_SECTORS)
#define ISO_TERMINATOR_LBA         (ISO_PVD_LBA + 1U)
#define ISO_PATH_TABLE_L_LBA       (ISO_TERMINATOR_LBA + 1U)
#define ISO_PATH_TABLE_M_LBA       (ISO_PATH_TABLE_L_LBA + 1U)
#define ISO_ROOT_DIR_LBA           (ISO_PATH_TABLE_M_LBA + 1U)
#define ISO_AUTORUN_DATA_LBA       (ISO_ROOT_DIR_LBA + 1U)
#define ISO_HTML_DATA_LBA          (ISO_AUTORUN_DATA_LBA + 1U)

#define ISO__DIV_CEIL(a, b)        (((a) + (b) - 1U) / (b))
#define ISO_HTML_SECTORS           ISO__DIV_CEIL(INSTALL_HTML_LEN, ISO_SECTOR_SIZE)

#define ISO9660_BYTES_PER_SECTOR   ISO_SECTOR_SIZE
#define ISO9660_TOTAL_SECTORS      (ISO_HTML_DATA_LBA + ISO_HTML_SECTORS)

/* Path table holds exactly one entry (the root): LEN_DI=1 + 1 pad byte = 10
 * bytes. This is a structural constant of "one root entry", not a magic
 * number -- it doesn't grow with INSTALL_HTML_LEN or file count here since
 * only the root directory itself is ever recorded in the path table. */
#define ISO_PATH_TABLE_BYTES       10U

static const uint8_t iso_autorun_inf[] =
  "[autorun]\r\n"
  "label=USB-PD Tester\r\n"
  "action=Open the USB-PD Tester setup guide\r\n"
  "shellexecute=INSTALL.HTM\r\n";
#define ISO_AUTORUN_INF_LEN   (sizeof(iso_autorun_inf) - 1U) /* drop trailing NUL */

/* ---- ECMA-119 numeric field helpers (7.2/7.3: plain + "both-endian") --- */
static inline void iso_put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void iso_put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void iso_put_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void iso_put_be32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline void iso_put_723(uint8_t *p, uint16_t v) { iso_put_le16(&p[0], v); iso_put_be16(&p[2], v); }
static inline void iso_put_733(uint8_t *p, uint32_t v) { iso_put_le32(&p[0], v); iso_put_be32(&p[4], v); }

/* ---- directory record (ECMA-119 9.1), used both inside the PVD (for the
 * root's self-record) and inside the root directory extent itself. ---- */
static inline uint8_t iso_build_dir_record(uint8_t *out, uint32_t extent_lba, uint32_t data_len,
                                            uint8_t flags, const uint8_t *ident, uint8_t ident_len)
{
  uint8_t rec_len = (uint8_t)(33U + ident_len + (((ident_len % 2U) == 0U) ? 1U : 0U));

  memset(out, 0, rec_len);
  out[0] = rec_len;
  out[1] = 0U; /* extended attribute record length */
  iso_put_733(&out[2], extent_lba);
  iso_put_733(&out[10], data_len);
  out[18] = 125U; /* recording date/time: 2025-01-01 (arbitrary, not load-bearing) */
  out[19] = 1U;
  out[20] = 1U;
  out[25] = flags; /* 0x02 = directory, 0x00 = file */
  iso_put_723(&out[28], 1U); /* volume sequence number */
  out[32] = ident_len;
  memcpy(&out[33], ident, ident_len);

  return rec_len;
}

/* ---- Primary Volume Descriptor (ECMA-119 8.4) --------------------------- */
static inline void iso_build_pvd(uint8_t *s)
{
  static const uint8_t root_ident = 0x00U;

  s[0] = 1U; /* Primary Volume Descriptor */
  memcpy(&s[1], "CD001", 5);
  s[6] = 1U;

  memset(&s[8], 0x20, 32);  /* system identifier: unused */
  memset(&s[40], 0x20, 32); /* volume identifier */
  memcpy(&s[40], "USB_PD_TESTER", 13);

  iso_put_733(&s[80], ISO9660_TOTAL_SECTORS); /* volume space size */
  iso_put_723(&s[120], 1U);                   /* volume set size */
  iso_put_723(&s[124], 1U);                   /* volume sequence number */
  iso_put_723(&s[128], ISO_SECTOR_SIZE);      /* logical block size */
  iso_put_733(&s[132], ISO_PATH_TABLE_BYTES); /* path table size */
  iso_put_le32(&s[140], ISO_PATH_TABLE_L_LBA);
  iso_put_be32(&s[148], ISO_PATH_TABLE_M_LBA);

  (void)iso_build_dir_record(&s[156], ISO_ROOT_DIR_LBA, ISO_SECTOR_SIZE, 0x02U, &root_ident, 1U);

  memset(&s[190], 0x20, 128); /* volume set identifier */
  memset(&s[318], 0x20, 128); /* publisher identifier */
  memset(&s[446], 0x20, 128); /* data preparer identifier */
  memset(&s[574], 0x20, 128); /* application identifier */
  memset(&s[702], 0x20, 37);  /* copyright file identifier */
  memset(&s[739], 0x20, 37);  /* abstract file identifier */
  memset(&s[776], 0x20, 37);  /* bibliographic file identifier */

  memset(&s[813], '0', 16); /* volume creation date/time: unspecified */
  memset(&s[830], '0', 16); /* volume modification date/time: unspecified */
  memset(&s[847], '0', 16); /* volume expiration date/time: unspecified */
  memset(&s[864], '0', 16); /* volume effective date/time: unspecified */

  s[881] = 1U; /* file structure version */
}

static inline void iso_build_terminator(uint8_t *s)
{
  s[0] = 255U; /* Volume Descriptor Set Terminator */
  memcpy(&s[1], "CD001", 5);
  s[6] = 1U;
}

/* ---- path table (ECMA-119 9.4): a single root entry, in both the
 * little-endian ("L") and big-endian ("M") copies the PVD points at. ---- */
static inline void iso_build_path_table(uint8_t *s, uint8_t big_endian)
{
  s[0] = 1U; /* length of directory identifier */
  s[1] = 0U; /* extended attribute record length */

  if (big_endian != 0U)
  {
    iso_put_be32(&s[2], ISO_ROOT_DIR_LBA);
    iso_put_be16(&s[6], 1U);
  }
  else
  {
    iso_put_le32(&s[2], ISO_ROOT_DIR_LBA);
    iso_put_le16(&s[6], 1U);
  }
  s[8] = 0x00U; /* directory identifier: root */
  s[9] = 0x00U; /* padding to even record length */
}

/* ---- root directory extent: "." "..", AUTORUN.INF, INSTALL.HTM -------- */
static inline void iso_build_root_dir(uint8_t *s)
{
  static const uint8_t self_ident = 0x00U;
  static const uint8_t parent_ident = 0x01U;
  static const uint8_t autorun_name[] = "AUTORUN.INF;1";
  static const uint8_t html_name[] = "INSTALL.HTM;1";
  uint32_t off = 0U;

  off += iso_build_dir_record(&s[off], ISO_ROOT_DIR_LBA, ISO_SECTOR_SIZE, 0x02U, &self_ident, 1U);
  off += iso_build_dir_record(&s[off], ISO_ROOT_DIR_LBA, ISO_SECTOR_SIZE, 0x02U, &parent_ident, 1U);
  off += iso_build_dir_record(&s[off], ISO_AUTORUN_DATA_LBA, ISO_AUTORUN_INF_LEN, 0x00U,
                               autorun_name, (uint8_t)(sizeof(autorun_name) - 1U));
  (void)off;
  off += iso_build_dir_record(&s[off], ISO_HTML_DATA_LBA, INSTALL_HTML_LEN, 0x00U,
                               html_name, (uint8_t)(sizeof(html_name) - 1U));
  (void)off;
}

/* ---- top-level dispatch ------------------------------------------------ */
static inline void ISO9660_BuildSector(uint32_t lba, uint8_t *out)
{
  memset(out, 0, ISO_SECTOR_SIZE);

  if (lba == ISO_PVD_LBA)
  {
    iso_build_pvd(out);
  }
  else if (lba == ISO_TERMINATOR_LBA)
  {
    iso_build_terminator(out);
  }
  else if (lba == ISO_PATH_TABLE_L_LBA)
  {
    iso_build_path_table(out, 0U);
  }
  else if (lba == ISO_PATH_TABLE_M_LBA)
  {
    iso_build_path_table(out, 1U);
  }
  else if (lba == ISO_ROOT_DIR_LBA)
  {
    iso_build_root_dir(out);
  }
  else if (lba == ISO_AUTORUN_DATA_LBA)
  {
    memcpy(out, iso_autorun_inf, ISO_AUTORUN_INF_LEN);
  }
  else if ((lba >= ISO_HTML_DATA_LBA) && (lba < ISO9660_TOTAL_SECTORS))
  {
    uint32_t byte_off = (lba - ISO_HTML_DATA_LBA) * ISO_SECTOR_SIZE;

    if (byte_off < INSTALL_HTML_LEN)
    {
      uint32_t n = INSTALL_HTML_LEN - byte_off;

      if (n > ISO_SECTOR_SIZE)
      {
        n = ISO_SECTOR_SIZE;
      }
      memcpy(out, &install_html[byte_off], n);
    }
  }
  /* else: system area (LBA < 16) or beyond the volume -- stays zero */
}

#ifdef __cplusplus
}
#endif

#endif /* __ISO9660_DISK_H__ */
