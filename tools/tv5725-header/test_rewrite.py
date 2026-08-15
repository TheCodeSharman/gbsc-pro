import header
import rewrite


def declared(src):
    return [(t.name, t.seg, t.reg, t.offset, t.width) for t in header.typedefs(src)]


def test_a_corrected_width_is_rewritten_in_place_keeping_its_comment():
    base = "    typedef UReg<0x00, 0x4D, 0, 7> MD_HD1250P_CNTRL;   // counter\n"
    want = header.typedefs("typedef UReg<0x00, 0x4D, 0, 8> MD_HD1250P_CNTRL;")

    out = rewrite.apply_identities(base, want)

    assert out == "    typedef UReg<0x00, 0x4D, 0, 8> MD_HD1250P_CNTRL;   // counter\n"


def test_a_field_the_base_lacks_is_inserted_after_its_predecessor():
    base = """    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;
    typedef UReg<0x05, 0x64, 0, 8> SP_OTHER;
"""
    want = header.typedefs("""
    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;
    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;
    typedef UReg<0x05, 0x64, 0, 8> SP_OTHER;
""")

    out = rewrite.apply_identities(base, want)

    assert declared(out) == [
        ("SP_TEST_EN", 5, 0x63, 0, 1),
        ("SP_TEST_MODULE", 5, 0x63, 1, 3),
        ("SP_OTHER", 5, 0x64, 0, 8),
    ]


def test_a_field_dropped_from_the_target_is_removed_with_its_comment():
    base = """    typedef UReg<0x03, 0x16, 0, 8> VDS_C2_TAG_LOW_SLOP;   // a duplicate
    typedef UReg<0x03, 0x16, 0, 8> VDS_C2_TAG_LOW_SLOPE;
"""
    want = header.typedefs(
        "typedef UReg<0x03, 0x16, 0, 8> VDS_C2_TAG_LOW_SLOPE;")

    out = rewrite.apply_identities(base, want)

    assert declared(out) == [("VDS_C2_TAG_LOW_SLOPE", 3, 0x16, 0, 8)]
    assert "duplicate" not in out


def test_rewording_a_field_changes_no_register():
    """The guarantee that makes a 831-comment pass reviewable: it must be
    provably incapable of moving a field. Anything else buries the four lines
    that change behaviour in twelve hundred that do not."""
    base = """    typedef UReg<0x01, 0x00, 1, 1> IF_MATRIX_BYPS;   // old text
                                                     // wrapped on
    typedef UReg<0x01, 0x00, 2, 1> IF_UV_REVERT;
"""
    out = rewrite.apply_wordings(base, {"IF_MATRIX_BYPS": "Rgb2yuv matrix bypass",
                                        "IF_UV_REVERT": "Y/UV flip control"})

    assert header.compare(base, out) == []
    assert header.wordings(out)["IF_MATRIX_BYPS"] == "Rgb2yuvmatrixbypass"
    assert header.wordings(out)["IF_UV_REVERT"] == "Y/UVflipcontrol"
    assert "old text" not in out and "wrapped on" not in out


def test_a_section_heading_is_not_eaten():
    """`// INPUT FORMATTER REGISTERS` titles the block below it. Sweeping it
    into the first field's comment is how CAPTURE_ENABLE came to be documented
    as "Playback / Capture / Memory Registers"."""
    base = """// INPUT FORMATTER REGISTERS

    typedef UReg<0x01, 0x00, 1, 1> IF_MATRIX_BYPS;   // old
"""
    out = rewrite.apply_wordings(base, {"IF_MATRIX_BYPS": "new text"})

    assert "// INPUT FORMATTER REGISTERS" in out
    assert header.wordings(out)["IF_MATRIX_BYPS"] == "newtext"


def test_a_field_with_no_wording_is_left_bare():
    """No wording means no comment, not a stale one. A description that has
    been withdrawn is worse than none, because it still reads as attested."""
    base = "    typedef UReg<0x05, 0x60, 0, 8> ADC_UNUSED_60;   // fake name\n"

    out = rewrite.apply_wordings(base, {})

    assert header.compare(base, out) == []
    assert "fake name" not in out
    assert header.wordings(out)["ADC_UNUSED_60"] == ""
