#ifndef _SLOT_H_
// SLOTS
#define SLOTS_FILE "/slots.bin" // the file where to store slots metadata
#define SLOTS_TOTAL 72          // max number of slots
#define EMPTY_SLOT_NAME "Empty                   "
typedef struct
{
    char name[25];
    // Always 0, and read by nothing. Kept because removing it re-lays out
    // /slots.bin and garbles the names in every slot file already on a unit.
    uint8_t presetID;
    uint8_t scanlines;
    uint8_t scanlinesStrength;
    uint8_t slot;
    uint8_t wantVdsLineFilter;
    uint8_t wantStepResponse;
    uint8_t wantPeaking;
} SlotMeta;

typedef struct
{
    SlotMeta slot[SLOTS_TOTAL]; // the max avaliable slots that can be encoded in a the charset[A-Za-z0-9-._~()!*:,;]
} SlotMetaArray;
#endif