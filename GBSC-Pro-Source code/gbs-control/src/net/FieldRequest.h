#ifndef FIELD_REQUEST_H_
#define FIELD_REQUEST_H_

#include <stdint.h>

// Many register fields named in ONE HTTP request. docs/register-bus-ownership.md
//
// Every register read is handed to loop() through RegisterQueue, so a host
// asking for them one at a time pays a round trip AND a loop pass each: 608
// reads take minutes and starve the loop being measured, which is how a
// transient becomes unobservable.
//
// The wire form is addresses rather than names -- `seg.reg.offset.width`, all
// decimal, comma separated. The firmware has no runtime name table and shipping 956
// of them would cost flash and RAM on a part already at 78% and 57%; the
// catalogue that resolves names lives on the host.
class FieldRequest
{
public:
    struct Field
    {
        uint8_t segment;
        uint8_t reg;
        uint8_t offset;
        uint8_t width;
    };

    // Bounded because the buffer is static: no allocation in a network
    // callback. 48 fields is a regpanel refresh in one request.
    static const uint8_t Capacity = 48;

    // VDS_FR_SELECT is 32 bits and nine SDRAM address fields are 21, so a field
    // spans up to four registers. Reading one narrower than it is truncates the
    // value and says nothing, which is why this is the catalogue's widest rather
    // than a comfortable round number.
    static const uint8_t MaxWidth = 32;

    // What bytesFor() can return, and the buffer the caller must supply.
    static const uint8_t MaxRegisters = 5;

    FieldRequest();

    // True only if the WHOLE spec parsed. A half-parsed list would answer with
    // values the caller cannot match to what it asked for.
    bool parse(const char *spec);

    uint8_t count() const;
    const Field &at(uint8_t index) const;

    // How many registers the reader must fetch for this field.
    static uint8_t bytesFor(const Field &field);

    // The field's value out of those bytes, little end first.
    static uint32_t valueFrom(const Field &field, const uint8_t *bytes);

private:
    Field fields_[Capacity];
    uint8_t count_;
};

#endif  // FIELD_REQUEST_H_
