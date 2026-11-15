"""Malformed archive framing and recognized ONYO members fail even unused."""
import struct

from support import archive, member, object_bytes


def malformed_cases(h, root):
    good = member('unused.o/', object_bytes('unused'))
    header = good[:60]
    malformed = {
        'partial-header': b'!<arch>\n' + b'x' * 59,
        'trailing-garbage': b'!<arch>\n' + good + b'x',
        'bad-header-trailer': b'!<arch>\n' + header[:58] + b'xx' + good[60:],
        'nonnumeric-size': b'!<arch>\n' + header[:48] + b'garbage   ' + good[58:],
        'negative-size': b'!<arch>\n' + header[:48] + b'-1        ' + good[58:],
        'size-junk-suffix': b'!<arch>\n' + header[:48] + b'1x        ' + good[58:],
        'member-truncated': b'!<arch>\n' + good[:-2],
        'odd-padding-missing': archive(('notes/', b'x'))[:-1],
        'bsd-name-too-large': archive(('#1/99', b'small')),
        'bsd-name-nonnumeric': archive(('#1/x', b'small')),
        'gnu-name-outside-table': archive(('//', b'long_name.o/\n'), ('/999', b'notes')),
        'gnu-name-without-table': archive(('/0', b'notes')),
        'recognized-onyo-short': archive(('broken.o/', b'ONYO')),
    }
    original = object_bytes('unused_bad')
    for label, offset, value in (
        ('unsupported-version', 4, 99),
        ('too-many-sections', 12, 5),
        ('symbol-table-overflow', 16, 0xFFFFFFFF),
        ('relocation-table-overflow', 20, 0xFFFFFFFF),
        ('string-table-overflow', 24, 0xFFFFFFFF),
    ):
        broken = bytearray(original)
        struct.pack_into('<I', broken, offset, value)
        malformed[label] = archive(('broken.o/', bytes(broken)))
    broken = bytearray(original)
    struct.pack_into('<Q', broken, 64 + 24, 0xFFFFFFFFFFFFFFFF)
    malformed['section-data-outside-file'] = archive(('broken.o/', bytes(broken)))
    broken = bytearray(original)
    struct.pack_into('<I', broken, 64 + 12, 3)
    malformed['unused-section-align-not-power-of-two'] = archive(('broken.o/', bytes(broken)))
    # Add one structurally valid R_ONYO_32 relocation before the string
    # table. Its self-reference and 4-byte patch fit the tiny text section.
    relocatable = bytearray(original[:136]
                            + struct.pack('<IIQIi', 0, 2, 0, 0, 0)
                            + original[136:])
    struct.pack_into('<I', relocatable, 20, 1)
    struct.pack_into('<Q', relocatable, 64 + 24, len(relocatable) - 4)
    valid_reloc = h.write('valid_unused_reloc.a', archive(('valid.o/', bytes(relocatable))))
    yield 'unused-valid-relocation-control', lambda: h.execute([root, valid_reloc])
    for label, offset, fmt, value in (
        ('reloc-section-index', 136, '<I', 99),
        ('reloc-type', 140, '<I', 99),
        ('reloc-symbol-index', 152, '<I', 99),
        ('reloc-patch-outside-section', 144, '<Q', 4),
    ):
        broken = bytearray(relocatable)
        struct.pack_into(fmt, broken, offset, value)
        malformed['unused-' + label] = archive(('broken.o/', bytes(broken)))
    for name, data in malformed.items():
        # A valid member prevents a generic "no objects" rejection from
        # masquerading as validation of the malformed member that follows.
        path = h.write('bad-' + name + '.a', data[:8] + good + data[8:])
        yield 'reject-' + name, lambda a=path: h.link([root, a], reject=True)
