"""TH16: the minidump reader on a synthetic dump, then on every truncation."""
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import minidump_summary  # noqa: E402


def build():
    """Header, directory and three streams: SystemInfo, ThreadList, Memory."""
    context = bytearray(0xCC)
    struct.pack_into("<I", context, minidump_summary.CONTEXT_EIP, 0x13DB27F0)
    struct.pack_into("<I", context, minidump_summary.CONTEXT_ESP, 0x0019FF00)
    stack = b"STACKBYTES-PROBE"
    memory = b"TDUR-DUMP-SENTRY" * 2
    header_size, directory_size = 32, 3 * 12
    system_rva = header_size + directory_size
    system = struct.pack("<HHH", 0, 6, 0x9E0A) + bytes(50)
    thread_rva = system_rva + len(system)
    thread_size = 4 + 48
    context_rva = thread_rva + thread_size
    stack_rva = context_rva + len(context)
    memory_list_rva = stack_rva + len(stack)
    memory_list_size = 4 + 16
    memory_rva = memory_list_rva + memory_list_size
    thread = struct.pack("<I", 1) + struct.pack(
        "<IIIIQQIIII", 4242, 1, 0x20, 0, 0x7EFDD000, 0x0019F000,
        len(stack), stack_rva, len(context), context_rva)
    memory_list = struct.pack("<I", 1) + struct.pack(
        "<QII", 0x13DB2000, len(memory), memory_rva)
    directory = (struct.pack("<III", 7, len(system), system_rva) +
                 struct.pack("<III", 3, thread_size, thread_rva) +
                 struct.pack("<III", 5, memory_list_size, memory_list_rva))
    header = struct.pack("<IIIIIIQ", minidump_summary.SIGNATURE, 0xA793, 3,
                         header_size, 0, 1700000000, 0x1821)
    return (header + directory + system + thread + bytes(context) + stack +
            memory_list + memory)


def main():
    data = build()
    summary = minidump_summary.parse(data)
    assert summary["architecture"] == 0, summary
    assert [s["name"] for s in summary["streams"]] == [
        "SystemInfo", "ThreadList", "MemoryList"], summary["streams"]
    assert summary["threads"] == [{
        "thread_id": 4242, "suspend_count": 1, "teb": "0x7efdd000",
        "stack_start": "0x19f000", "stack_size": 16,
        "eip": "0x13db27f0", "esp": "0x19ff00"}], summary["threads"]
    assert minidump_summary.captured(summary, 0x13DB2010)
    assert not minidump_summary.captured(summary, 0x13DB2020)
    assert minidump_summary.memory_bytes(
        data, summary, 0x13DB2000, 16) == b"TDUR-DUMP-SENTRY"
    assert minidump_summary.memory_bytes(data, summary, 0x13DB2018, 16) is None
    assert summary["exception"] is None
    # Every truncation is a clear error, never a silent partial result.
    for length in range(len(data)):
        try:
            minidump_summary.parse(data[:length])
        except minidump_summary.MinidumpError:
            continue
        raise AssertionError("truncation at %d bytes parsed silently" % length)
    try:
        minidump_summary.parse(b"XXXX" + data[4:])
    except minidump_summary.MinidumpError as error:
        assert "signature" in str(error)
    else:
        raise AssertionError("wrong signature accepted")
    print("PASS minidump reader: exact fields, %d truncations refused"
          % len(data))


if __name__ == "__main__":
    main()
