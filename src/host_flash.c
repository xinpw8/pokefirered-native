// host_flash.c — File-backed flash memory for pokefirered-native
//
// Replaces the GBA flash chip drivers (agb_flash.c, agb_flash_mx.c,
// agb_flash_le.c, agb_flash_1m.c) with a 128KB in-memory buffer
// backed by a .sav file on disk.  All upstream save code (save.c,
// load_save.c) works unmodified.

#include "global.h"
#include "gba/flash_internal.h"
#include "host_flash.h"
#include "host_log.h"
#include "host_savestate.h"
#include "save.h"
#include "pokemon_storage_system.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

#define FLASH_BUFFER_SIZE 131072  // 128KB = 1 megabit
#define SECTOR_SIZE_HOST  4096
#define NUM_SECTORS       32

static u8 sFlashBuffer[FLASH_BUFFER_SIZE];
static const char *sSaveFilePath;

static const struct FlashType sHostFlashType = {
    .romSize = FLASH_BUFFER_SIZE,
    .sector = {
        .size  = SECTOR_SIZE_HOST,
        .shift = 12,             // 4096 == 1 << 12
        .count = NUM_SECTORS,
        .top   = 0,
    },
    .wait = { 3, 1 },
    .ids  = { .separate = { .makerId = 0xC2, .deviceId = 0x09 } },
};

// ---------------------------------------------------------------------------
// Globals (replacing agb_flash.c lines 9-18)
// ---------------------------------------------------------------------------

COMMON_DATA u8 gFlashTimeoutFlag = 0;
COMMON_DATA u8 (*PollFlashStatus)(u8 *) = NULL;
COMMON_DATA u16 (*WaitForFlashWrite)(u8, u8 *, u8) = NULL;
COMMON_DATA u16 (*ProgramFlashSector)(u16, void *) = NULL;
COMMON_DATA const struct FlashType *gFlash = NULL;
COMMON_DATA u16 (*ProgramFlashByte)(u16, u32, u8) = NULL;
COMMON_DATA u16 gFlashNumRemainingBytes = 0;
COMMON_DATA u16 (*EraseFlashChip)(void) = NULL;
COMMON_DATA u16 (*EraseFlashSector)(u16) = NULL;
COMMON_DATA const u16 *gFlashMaxTime = NULL;

// ---------------------------------------------------------------------------
// Timeout table (not functionally used but referenced)
// ---------------------------------------------------------------------------

static const u16 sHostMaxTime[] = {
    10, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
    10, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
    2000, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
    2000, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
};

// Forward declarations for format marker (defined further below)
static void HostFlashStampFormatMarker(void);
static s8 HostFlashCheckFormatMarker(void);

// ---------------------------------------------------------------------------
// Disk sync (atomic: write .tmp then rename)
// ---------------------------------------------------------------------------

static void HostFlashSyncToDisk(void)
{
    FILE *f;
    char tmpPath[1024];

    if (sSaveFilePath == NULL)
        return;

    // Stamp format marker so future loads can detect struct-size changes
    HostFlashStampFormatMarker();

    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", sSaveFilePath);

    f = fopen(tmpPath, "wb");
    if (f == NULL)
    {
        HostLogPrintf("[pokefirered-native] save: failed to open %s for writing\n", tmpPath);
        return;
    }

    if (fwrite(sFlashBuffer, 1, FLASH_BUFFER_SIZE, f) != FLASH_BUFFER_SIZE)
    {
        HostLogPrintf("[pokefirered-native] save: write to %s incomplete\n", tmpPath);
        fclose(f);
        return;
    }

    fclose(f);
    if (rename(tmpPath, sSaveFilePath) != 0)
        HostLogPrintf("[pokefirered-native] save: rename %s -> %s failed\n", tmpPath, sSaveFilePath);
    else
        HostLogPrintf("[pokefirered-native] save: written to %s\n", sSaveFilePath);
}

// ---------------------------------------------------------------------------
// Host flash operations (static helpers)
// ---------------------------------------------------------------------------

static u16 HostEraseFlashChip(void)
{
    memset(sFlashBuffer, 0xFF, FLASH_BUFFER_SIZE);
    HostFlashSyncToDisk();
    return 0;
}

static u16 HostEraseFlashSector(u16 sectorNum)
{
    if (sectorNum >= NUM_SECTORS)
        return 0x80FF;

    memset(&sFlashBuffer[sectorNum * SECTOR_SIZE_HOST], 0xFF, SECTOR_SIZE_HOST);
    HostFlashSyncToDisk();
    return 0;
}

static u16 HostProgramFlashByte(u16 sectorNum, u32 offset, u8 data)
{
    if (offset >= SECTOR_SIZE_HOST)
        return 0x8000;

    sFlashBuffer[sectorNum * SECTOR_SIZE_HOST + offset] = data;
    // No sync here -- batched writes are synced by ProgramFlashSector
    return 0;
}

static u16 HostProgramFlashSector(u16 sectorNum, void *src)
{
    u16 result;

    if (sectorNum >= NUM_SECTORS)
        return 0x80FF;

    result = HostEraseFlashSector(sectorNum);
    if (result != 0)
        return result;

    memcpy(&sFlashBuffer[sectorNum * SECTOR_SIZE_HOST], src, SECTOR_SIZE_HOST);
    HostFlashSyncToDisk();
    return 0;
}

static u16 HostWaitForFlashWrite(u8 phase, u8 *addr, u8 lastData)
{
    (void)phase;
    (void)addr;
    (void)lastData;
    return 0; // instant success
}

static u8 HostPollFlashStatus(u8 *addr)
{
    (void)addr;
    return 0xFF;
}

// ---------------------------------------------------------------------------
// Const FlashSetupInfo structs (replacing agb_flash_mx.c + agb_flash_le.c)
// ---------------------------------------------------------------------------

const struct FlashSetupInfo MX29L010 = {
    .programFlashByte   = HostProgramFlashByte,
    .programFlashSector = HostProgramFlashSector,
    .eraseFlashChip     = HostEraseFlashChip,
    .eraseFlashSector   = HostEraseFlashSector,
    .WaitForFlashWrite  = HostWaitForFlashWrite,
    .maxTime            = sHostMaxTime,
    .type = {
        .romSize = FLASH_BUFFER_SIZE,
        .sector  = { .size = SECTOR_SIZE_HOST, .shift = 12, .count = NUM_SECTORS, .top = 0 },
        .wait    = { 3, 1 },
        .ids     = { .separate = { .makerId = 0xC2, .deviceId = 0x09 } },
    },
};

const struct FlashSetupInfo DefaultFlash = {
    .programFlashByte   = HostProgramFlashByte,
    .programFlashSector = HostProgramFlashSector,
    .eraseFlashChip     = HostEraseFlashChip,
    .eraseFlashSector   = HostEraseFlashSector,
    .WaitForFlashWrite  = HostWaitForFlashWrite,
    .maxTime            = sHostMaxTime,
    .type = {
        .romSize = FLASH_BUFFER_SIZE,
        .sector  = { .size = SECTOR_SIZE_HOST, .shift = 12, .count = NUM_SECTORS, .top = 0 },
        .wait    = { 3, 1 },
        .ids     = { .separate = { .makerId = 0x00, .deviceId = 0x00 } },
    },
};

const struct FlashSetupInfo LE26FV10N1TS = {
    .programFlashByte   = HostProgramFlashByte,
    .programFlashSector = HostProgramFlashSector,
    .eraseFlashChip     = HostEraseFlashChip,
    .eraseFlashSector   = HostEraseFlashSector,
    .WaitForFlashWrite  = HostWaitForFlashWrite,
    .maxTime            = sHostMaxTime,
    .type = {
        .romSize = FLASH_BUFFER_SIZE,
        .sector  = { .size = SECTOR_SIZE_HOST, .shift = 12, .count = NUM_SECTORS, .top = 0 },
        .wait    = { 3, 1 },
        .ids     = { .separate = { .makerId = 0x62, .deviceId = 0x13 } },
    },
};

// ---------------------------------------------------------------------------
// Public flash API (matching flash_internal.h declarations)
// ---------------------------------------------------------------------------

// Native save format marker — stored in the last SECTOR_SIZE bytes of
// the flash buffer (sector 31, normally Trainer Tower slot 2).  When we
// write, we stamp the current struct sizes so we can detect loads from
// builds with incompatible layouts.
#define FORMAT_MARKER_SECTOR   31
#define FORMAT_MARKER_OFFSET   (FORMAT_MARKER_SECTOR * SECTOR_SIZE_HOST)
#define FORMAT_MARKER_MAGIC    0x5046524E  // "PFRN"

struct HostSaveFormatMarker
{
    u32 magic;
    u16 sizeofSaveBlock2;
    u16 sizeofSaveBlock1;
    u16 sizeofPokemonStorage;
    u16 reserved;
};

static bool8 sFormatMarkerValid = FALSE;

static void HostFlashStampFormatMarker(void)
{
    struct HostSaveFormatMarker *marker =
        (struct HostSaveFormatMarker *)&sFlashBuffer[FORMAT_MARKER_OFFSET];

    marker->magic              = FORMAT_MARKER_MAGIC;
    marker->sizeofSaveBlock2   = (u16)sizeof(struct SaveBlock2);
    marker->sizeofSaveBlock1   = (u16)sizeof(struct SaveBlock1);
    marker->sizeofPokemonStorage = (u16)sizeof(struct PokemonStorage);
    marker->reserved           = 0;
    sFormatMarkerValid = TRUE;
}

// Returns:  1 = marker present and matching
//           0 = no marker (legacy save, allow it)
//          -1 = marker present but sizes mismatch (reject)
static s8 HostFlashCheckFormatMarker(void)
{
    const struct HostSaveFormatMarker *marker =
        (const struct HostSaveFormatMarker *)&sFlashBuffer[FORMAT_MARKER_OFFSET];

    if (marker->magic != FORMAT_MARKER_MAGIC)
        return 0; // no marker — legacy save, allow

    if (marker->sizeofSaveBlock2 != (u16)sizeof(struct SaveBlock2)
     || marker->sizeofSaveBlock1 != (u16)sizeof(struct SaveBlock1)
     || marker->sizeofPokemonStorage != (u16)sizeof(struct PokemonStorage))
    {
        HostLogPrintf("[pokefirered-native] save: FORMAT MISMATCH — "
                      "save has SB2=%u SB1=%u PS=%u, "
                      "build has SB2=%lu SB1=%lu PS=%lu. "
                      "Discarding incompatible save.\n",
                      marker->sizeofSaveBlock2,
                      marker->sizeofSaveBlock1,
                      marker->sizeofPokemonStorage,
                      (unsigned long)sizeof(struct SaveBlock2),
                      (unsigned long)sizeof(struct SaveBlock1),
                      (unsigned long)sizeof(struct PokemonStorage));
        return -1; // mismatch — reject
    }

    return 1; // marker present and matching
}

void HostFlashInit(const char *savePath)
{
    FILE *f;
    long fileSize;

    sSaveFilePath = savePath;
    sFormatMarkerValid = FALSE;
    memset(sFlashBuffer, 0xFF, FLASH_BUFFER_SIZE);

    /* Protect the file path pointer from savestate restore —
     * the pointer must stay valid for the current process. */
    HostSavestateProtectRegion(&sSaveFilePath, sizeof(sSaveFilePath));

    if (savePath == NULL)
        return;

    f = fopen(savePath, "rb");
    if (f == NULL)
        return;

    // Validate file size — must be exactly 128KB flash image
    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize != FLASH_BUFFER_SIZE)
    {
        HostLogPrintf("[pokefirered-native] save: %s is %ld bytes "
                      "(expected %d). Ignoring corrupt/incompatible save.\n",
                      savePath, fileSize, FLASH_BUFFER_SIZE);
        fclose(f);
        return;
    }

    fread(sFlashBuffer, 1, FLASH_BUFFER_SIZE, f);
    fclose(f);

    // Check struct-size format marker.
    // -1 = sizes mismatch (definitely incompatible, discard)
    //  0 = no marker (legacy save, allow — can't verify)
    //  1 = marker matches (all good)
    {
        s8 markerResult = HostFlashCheckFormatMarker();

        if (markerResult < 0)
        {
            HostLogPrintf("[pokefirered-native] save: incompatible format "
                          "in %s — treating as empty.\n", savePath);
            memset(sFlashBuffer, 0xFF, FLASH_BUFFER_SIZE);
        }
        else if (markerResult == 0)
        {
            HostLogPrintf("[pokefirered-native] save: no format marker "
                          "in %s — loading as legacy save.\n", savePath);
        }
    }
}

u16 IdentifyFlash(void)
{
    // Wire up all function pointers to host implementations
    ProgramFlashByte   = HostProgramFlashByte;
    ProgramFlashSector = HostProgramFlashSector;
    EraseFlashChip     = HostEraseFlashChip;
    EraseFlashSector   = HostEraseFlashSector;
    WaitForFlashWrite  = HostWaitForFlashWrite;
    PollFlashStatus    = HostPollFlashStatus;
    gFlashMaxTime      = sHostMaxTime;
    gFlash             = &sHostFlashType;
    return 0;
}

u16 SetFlashTimerIntr(u8 timerNum, void (**intrFunc)(void))
{
    (void)timerNum;
    (void)intrFunc;
    return 0; // no-op, timers unnecessary on native
}

u16 ReadFlashId(void)
{
    return 0xC209; // MX chip ID
}

void ReadFlash(u16 sectorNum, u32 offset, void *dest, u32 size)
{
    u32 addr = (u32)sectorNum * SECTOR_SIZE_HOST + offset;

    if (addr + size > FLASH_BUFFER_SIZE)
        size = FLASH_BUFFER_SIZE - addr;

    memcpy(dest, &sFlashBuffer[addr], size);
}

u32 VerifyFlashSector(u16 sectorNum, u8 *src)
{
    u8 *tgt = &sFlashBuffer[(u32)sectorNum * SECTOR_SIZE_HOST];
    u32 size = SECTOR_SIZE_HOST;

    while (size-- != 0)
    {
        if (*tgt++ != *src++)
            return (u32)(tgt - 1); // nonzero = mismatch address (convention)
    }

    return 0;
}

u32 VerifyFlashSectorNBytes(u16 sectorNum, u8 *src, u32 n)
{
    u8 *tgt = &sFlashBuffer[(u32)sectorNum * SECTOR_SIZE_HOST];

    while (n-- != 0)
    {
        if (*tgt++ != *src++)
            return (u32)(tgt - 1);
    }

    return 0;
}

u32 ProgramFlashSectorAndVerify(u16 sectorNum, u8 *src)
{
    u8 i;
    u32 result;

    for (i = 0; i < 3; i++)
    {
        result = ProgramFlashSector(sectorNum, src);
        if (result != 0)
            continue;

        result = VerifyFlashSector(sectorNum, src);
        if (result == 0)
            break;
    }

    return result;
}

u32 ProgramFlashSectorAndVerifyNBytes(u16 sectorNum, void *dataSrc, u32 n)
{
    u8 i;
    u32 result;

    for (i = 0; i < 3; i++)
    {
        result = ProgramFlashSector(sectorNum, dataSrc);
        if (result != 0)
            continue;

        result = VerifyFlashSectorNBytes(sectorNum, dataSrc, n);
        if (result == 0)
            break;
    }

    return result;
}

void SwitchFlashBank(u8 bankNum)
{
    (void)bankNum;
    // no-op: flat 128KB buffer, no banking needed
}

void StartFlashTimer(u8 phase)
{
    (void)phase;
    // no-op
}

void StopFlashTimer(void)
{
    // no-op
}

void FlashTimerIntr(void)
{
    // no-op
}

void SetReadFlash1(u16 *dest)
{
    (void)dest;
    PollFlashStatus = HostPollFlashStatus;
}

u8 ReadFlash1(u8 *addr)
{
    return *addr;
}

u16 WaitForFlashWrite_Common(u8 phase, u8 *addr, u8 lastData)
{
    (void)phase;
    (void)addr;
    (void)lastData;
    return 0;
}

u16 EraseFlashChip_MX(void)
{
    return HostEraseFlashChip();
}

u16 EraseFlashSector_MX(u16 sectorNum)
{
    return HostEraseFlashSector(sectorNum);
}

u16 ProgramFlashByte_MX(u16 sectorNum, u32 offset, u8 data)
{
    return HostProgramFlashByte(sectorNum, offset, data);
}

u16 ProgramFlashSector_MX(u16 sectorNum, void *src)
{
    return HostProgramFlashSector(sectorNum, src);
}

// ---------------------------------------------------------------------------
// Test helper: inject a minimal valid save so LoadGameSave returns
// SAVE_STATUS_OK.  Writes NUM_SECTORS_PER_SLOT (14) sectors into
// save slot 1 (sectors 0-13), each with a valid signature, correct
// sector id, counter = 1, and a matching checksum.
// ---------------------------------------------------------------------------

// Duplicate the upstream checksum algorithm so we are self-contained.
static u16 HostCalculateChecksum(const void *data, u16 size)
{
    u16 i;
    u32 checksum = 0;
    const u8 *p = (const u8 *)data;

    for (i = 0; i < size / 4; i++)
    {
        u32 val = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
        checksum += val;
        p += 4;
    }

    return (u16)((checksum >> 16) + checksum);
}

void HostFlashInjectTestSave(void)
{
    u16 i;

    // Sector data sizes per sector id, matching the upstream
    // sSaveSlotLayout.  For simplicity we use SECTOR_DATA_SIZE (3968)
    // for all; any zero-filled data will checksum consistently.
    // The upstream sSaveSlotLayout entries are:
    //   sector 0  = sizeof(SaveBlock2) chunk 0  (capped at SECTOR_DATA_SIZE)
    //   sector 1-4 = sizeof(SaveBlock1) chunks 0-3
    //   sector 5-13 = sizeof(PokemonStorage) chunks 0-8
    // All data bytes within each sector are 0 (freshly zeroed).

    for (i = 0; i < NUM_SECTORS_PER_SLOT; i++)
    {
        struct SaveSector *sector =
            (struct SaveSector *)&sFlashBuffer[(u32)i * SECTOR_SIZE_HOST];

        // Clear the entire sector first
        memset(sector, 0, SECTOR_SIZE_HOST);

        // Set footer fields
        sector->id        = i;
        sector->signature = SECTOR_SIGNATURE;
        sector->counter   = 1;
        sector->checksum  = HostCalculateChecksum(sector->data, SECTOR_DATA_SIZE);
    }
}
