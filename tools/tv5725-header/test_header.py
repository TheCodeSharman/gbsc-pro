import os

import header


def catalogue():
    """Every register-declaring header, concatenated. The catalogue spans one
    file per subsystem, so these checks read all of them or miss a block."""
    return header.catalogue()


def test_a_typedef_yields_the_register_it_names():
    src = "    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;\n"

    assert header.typedefs(src) == [
        header.Typedef(name="SP_TEST_MODULE", seg=5, reg=0x63, offset=1, width=3)
    ]


def test_moving_comments_and_whitespace_is_not_a_register_change():
    before = """
    // Test control test module select
    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;
    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;
"""
    after = """
    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;   // Test control test module
                                                     // select

    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;
"""

    assert header.compare(before, after) == []


def test_a_widened_field_is_reported():
    before = "    typedef UReg<0x00, 0x4D, 0, 7> MD_HD1250P_CNTRL;\n"
    after = "    typedef UReg<0x00, 0x4D, 0, 8> MD_HD1250P_CNTRL;\n"

    assert header.compare(before, after) == [
        "moved   MD_HD1250P_CNTRL: s0 0x4D offset 0 width 7"
        " -> s0 0x4D offset 0 width 8"
    ]


def test_a_name_declared_twice_is_reported_rather_than_silently_collapsed():
    # Two typedefs of one name would let a rewrite drop one of them without
    # `compare` noticing, since it keys on the name.
    src = """
    typedef UReg<0x03, 0x16, 0, 8> VDS_HSCALE;
    typedef UReg<0x03, 0x17, 0, 2> VDS_HSCALE;
"""

    assert header.duplicates(src) == ["VDS_HSCALE"]


def test_rewrapping_a_comment_does_not_change_what_it_says():
    above = """
    // Test control test module select
    // # 0 none # 1 hs_pol_det module
    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;
"""
    trailing = """
    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;   // Test control test module select # 0
                                                     // none # 1 hs_pol_det module
"""

    assert header.wordings(above) == header.wordings(trailing)


def test_an_added_sentence_does_change_what_it_says():
    before = "    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;   // Test bus enable\n"
    after = ("    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;   // Test bus enable\n"
             "                                                 // Test control\n")

    assert header.wordings(before) != header.wordings(after)


def test_a_field_carries_its_comment_whichever_side_of_it_that_sits():
    src = """
    // Vertical stable indicator
    typedef UReg<0x00, 0x00, 0, 1> STATUS_IF_VT_OK;
    typedef UReg<0x00, 0x00, 1, 1> STATUS_IF_HT_OK;   // Horizontal stable
                                                      // indicator
"""

    assert [(t.name, c) for t, c in header.annotated(src)] == [
        ("STATUS_IF_VT_OK", "Vertical stable indicator"),
        ("STATUS_IF_HT_OK", "Horizontal stable indicator"),
    ]


def test_indentation_decides_who_owns_a_comment_between_two_typedefs():
    # ADC_5_00 has its own trailing comment AND is followed by three comment
    # lines that describe ADC_CLK_PA. Only the column they start in tells them
    # apart: a continuation sits under the comment column, a description for
    # the field below sits at the declaration indent.
    src = """
    typedef UReg<0x05, 0x00, 0, 8> ADC_5_00;                          // convenience
    // Clock selection for PA_ADC
    typedef UReg<0x05, 0x00, 0, 2> ADC_CLK_PA;
"""

    assert [(t.name, c) for t, c in header.annotated(src)] == [
        ("ADC_5_00", "convenience"),
        ("ADC_CLK_PA", "Clock selection for PA_ADC"),
    ]


def test_the_section_headings_this_header_actually_uses_are_all_recognised():
    # Every one of these sat above a typedef in the pre-regrouping file. The
    # four at the end were missed first time round and became field
    # descriptions -- CAPTURE_ENABLE was documented as "Playback / Capture /
    # Memory Registers", displacing the datasheet text it should have had.
    for heading in ["STATUS Registers----------", "Miscellaneous Registers",
                    "IF Registers", "VDS Registers", "PIP Registers",
                    "Memory Controller Registers", "OSD Registers",
                    "ADC, SP Registers", "IF", "NEW", "END",
                    "Arbitary names for STATUS_IF register------STATUS_IF",
                    "Deinterlacer / Scaledown registers /",
                    "Playback / Capture / Memory Registers",
                    "HDBypass", "Mode Detect"]:
        assert header.is_heading(heading), heading


def test_a_real_description_is_never_taken_for_a_heading():
    for description in ["whole register for convenience", "fake name",
                        "Mode switch indicator When =1, means input source "
                        "switch the mode",
                        "Enable capture When it's set 1, capture will be turn on",
                        "Offset control for G channel of ADC",
                        "Register bit for the memory controller"]:
        assert not header.is_heading(description), description


def test_a_section_heading_is_not_the_next_field_s_description():
    # An earlier pass folded "// STATUS Registers----------" into STATUS_00's
    # description. A heading describes the block below it, not one typedef.
    src = """
    // STATUS Registers----------
    typedef UReg<0x00, 0x00, 0, 8> STATUS_00;   // whole register for convenience
"""

    assert [(t.name, c) for t, c in header.annotated(src)] == [
        ("STATUS_00", "whole register for convenience"),
    ]


def test_two_names_for_the_same_bits_are_reported():
    """The inverse of duplicates(): one field, two names, both usable.

    `duplicates` catches the same NAME declared twice, which `identities` would
    silently collapse. This catches the same BITS declared under two names,
    which nothing collapses -- both compile, both work, and picking the wrong
    one is invisible.

    That is not hypothetical. 2c5af95 removed 32 invented field names lifted
    from wrapped lines in the PDF, and thirteen survived it precisely BECAUSE
    they collided with a real field rather than inventing a new address:

        s3_1a[4]    K_EN     shadowing  VDS_FLOCK_EN
        s3_29[7:0]  AIN      shadowing  VDS_BLEV_GAIN     ("...GAIN")
        s3_33[7:0]  _GAIN    shadowing  VDS_SVM_GAIN
        s3_3e[4]    BYPS     shadowing  VDS_DYN_BYPS

    A generated bring-up block then picked the fragments over the real names,
    which is how they were found -- it was full of `GBS::AIN::write()`.
    """
    src = """
    typedef UReg<0x03, 0x1A, 4, 1> VDS_FLOCK_EN;
    typedef UReg<0x03, 0x1A, 4, 1> K_EN;
    typedef UReg<0x03, 0x1A, 5, 1> VDS_FREERUN_FID;
"""

    assert header.aliases(src) == [((3, 0x1A, 4, 1), ["K_EN", "VDS_FLOCK_EN"])]


def test_a_name_without_an_underscore_is_reported_as_a_wrap_fragment():
    """Every real TV5725 field name carries a module prefix, so it has an
    underscore. A name without one is a tail lifted from a line the PDF wrapped.

    fielddocs.py has enforced this on the DOCS parse since 2c5af95 -- it is the
    rule that stops `AIN` and `LIP` becoming fields -- but nothing enforced it
    on the header, and two got in by another path.
    """
    src = """
    typedef UReg<0x03, 0x20, 0, 11> VDS_VSYN_SIZE1;
    typedef UReg<0x03, 0x20, 0, 8> ZE1;
"""

    assert header.fragments(src) == ["ZE1"]


def test_the_shipped_header_declares_no_wrap_fragment():
    """The real header, as a regression.

    `aliases()` cannot catch these: it keys on exact bit-identity, and a
    fragment that is a STRICT SUBRANGE of the real field collides with nothing.
    ZE1 is s3_20[7:0] inside VDS_VSYN_SIZE1's [10:0] -- the PDF wrapped
    "VDS_VSYN_SI/ZE1" and the extractor kept the tail and the first slice only.

    Widening `aliases()` to containment is the wrong fix and was measured as
    such: the header has 159 strict containments and 158 are legitimate
    whole-byte convenience names over their own bit fields (PLLAD_CONTROL_00_5x11
    over PLLAD_PDZ, SP_CS_0x3E over SP_H_COAST). Exactly one is a fragment, and
    what distinguishes it is the missing underscore.
    """
    assert header.fragments(catalogue()) == []


def test_the_shipped_header_declares_no_field_twice_under_two_names():
    """The real header, as a regression. Read it rather than a fixture: the
    point is that Tv5725.h itself stays clean, not that the parser works.

    **THERE ARE NO INTENDED ALIASES.** Prefer the datasheet's name unless the
    firmware's has a tangible benefit; resolve each pair to ONE name rather than
    tolerating both. The rule is about benefit, not provenance, so pairs go
    opposite ways:

        INT_RST_0/1/4         dropped. INT_CONTROL_RST_SOGBAD, _SOGSWITCH and
                              _NOHSYNC say WHICH interrupt where the datasheet
                              gives a bare index, and each already carries
                              "[datasheet: INT_RST_0]" in its comment, so
                              nothing becomes unfindable.
        SDRAM_RESET_CONTROL   dropped. It is commented "fake name", so it has no
                              benefit to weigh, and MEM_INI_REG -- the
                              datasheet's, with a real description -- wins.

    A tolerated list is where an invented fragment hides. An empty assertion
    cannot hide anything.
    """
    src = catalogue()

    assert header.aliases(src) == []
