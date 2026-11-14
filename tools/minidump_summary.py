"""Read an x86 minidump without dbghelp.

Metadata only: streams, threads, modules, exception, memory ranges and the
memory region map. Every offset is checked against the file size; any read
outside it raises MinidumpError, never a silent short read. --bytes
prints raw captured memory and is meant for tests.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

SIGNATURE = 0x504D444D  # "MDMP"
STREAMS = {
    3: "ThreadList", 4: "ModuleList", 5: "MemoryList", 6: "Exception",
    7: "SystemInfo", 8: "ThreadExList", 9: "Memory64List",
    14: "UnloadedModuleList", 15: "MiscInfo", 16: "MemoryInfoList",
    17: "ThreadInfoList",
}
STATES = {0x1000: "commit", 0x2000: "reserve", 0x10000: "free"}
TYPES = {0x20000: "private", 0x40000: "mapped", 0x1000000: "image"}
CONTEXT_EIP = 0xB8
CONTEXT_ESP = 0xC4


class MinidumpError(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.data = data

    def take(self, offset, size, what):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise MinidumpError("%s outside the file: offset %d size %d, "
                                "file %d bytes" % (what, offset, size,
                                                   len(self.data)))
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset, what):
        return struct.unpack_from(fmt, self.take(offset, struct.calcsize(fmt),
                                                 what))

    def string(self, rva, what):
        (length,) = self.unpack("<I", rva, what + " length")
        if length % 2:
            raise MinidumpError(what + " has an odd UTF-16 length")
        return self.take(rva + 4, length, what).decode("utf-16-le")


def hexa(value):
    return "0x%x" % value


def parse(data):
    reader = Reader(data)
    (signature, version, count, directory, _checksum, stamp,
     flags) = reader.unpack("<IIIIIIQ", 0, "header")
    if signature != SIGNATURE:
        raise MinidumpError("not a minidump (signature %s)" % hexa(signature))
    result = {"version": version & 0xFFFF, "time_date_stamp": stamp,
              "flags": hexa(flags), "streams": [], "threads": [],
              "modules": [], "unloaded_modules": [], "exception": None,
              "memory": [], "memory_info": [], "thread_info": [],
              "architecture": None}
    for index in range(count):
        kind, size, rva = reader.unpack("<III", directory + 12 * index,
                                        "stream directory")
        reader.take(rva, size, "stream %d" % kind)
        name = STREAMS.get(kind, "type-%d" % kind)
        result["streams"].append({"type": kind, "name": name, "size": size})
        if name == "SystemInfo":
            (result["architecture"],) = reader.unpack("<H", rva, "system")
        elif name == "ThreadList":
            (number,) = reader.unpack("<I", rva, "thread count")
            for slot in range(number):
                (tid, suspend, _cls, _prio, teb, stack_start, stack_size,
                 stack_rva, context_size, context_rva) = reader.unpack(
                     "<IIIIQQIIII", rva + 4 + 48 * slot, "thread")
                context = reader.take(context_rva, context_size, "context")
                eip = esp = None
                if context_size >= CONTEXT_ESP + 4:
                    (eip,) = struct.unpack_from("<I", context, CONTEXT_EIP)
                    (esp,) = struct.unpack_from("<I", context, CONTEXT_ESP)
                reader.take(stack_rva, stack_size, "thread stack")
                result["threads"].append({
                    "thread_id": tid, "suspend_count": suspend,
                    "teb": hexa(teb), "stack_start": hexa(stack_start),
                    "stack_size": stack_size,
                    "eip": None if eip is None else hexa(eip),
                    "esp": None if esp is None else hexa(esp)})
                result["memory"].append({"base": stack_start,
                                         "size": stack_size,
                                         "rva": stack_rva, "why": "stack"})
        elif name == "ModuleList":
            (number,) = reader.unpack("<I", rva, "module count")
            for slot in range(number):
                base, size_of, _sum, _stamp, name_rva = reader.unpack(
                    "<QIIII", rva + 4 + 108 * slot, "module")
                result["modules"].append({
                    "name": reader.string(name_rva, "module name"),
                    "base": hexa(base), "size": size_of})
        elif name == "UnloadedModuleList":
            header, entry, number = reader.unpack("<III", rva, "unloaded")
            for slot in range(number):
                base, size_of, _sum, _stamp, name_rva = reader.unpack(
                    "<QIIII", rva + header + entry * slot, "unloaded module")
                result["unloaded_modules"].append({
                    "name": reader.string(name_rva, "unloaded name"),
                    "base": hexa(base), "size": size_of})
        elif name == "MemoryList":
            (number,) = reader.unpack("<I", rva, "memory count")
            for slot in range(number):
                start, size_of, data_rva = reader.unpack(
                    "<QII", rva + 4 + 16 * slot, "memory descriptor")
                reader.take(data_rva, size_of, "memory bytes")
                result["memory"].append({"base": start, "size": size_of,
                                         "rva": data_rva, "why": "list"})
        elif name == "Exception":
            fields = reader.unpack("<IIIIQQII15Q", rva, "exception")
            tid, code, flags_value, address, number = (
                fields[0], fields[2], fields[3], fields[5], fields[6])
            result["exception"] = {
                "thread_id": tid, "code": hexa(code),
                "flags": flags_value, "address": hexa(address),
                "parameters": [hexa(value)
                               for value in fields[8:8 + min(number, 15)]]}
        elif name == "MemoryInfoList":
            header, entry, number = reader.unpack("<IIQ", rva, "memory info")
            for slot in range(number):
                (base, allocation, _aprot, _pad, region, state, protect,
                 kind_value, _pad2) = reader.unpack(
                     "<QQIIQIIII", rva + header + entry * slot,
                     "memory info entry")
                result["memory_info"].append({
                    "base": base, "allocation_base": allocation,
                    "size": region,
                    "state": STATES.get(state, hexa(state)),
                    "type": TYPES.get(kind_value, hexa(kind_value)),
                    "protect": hexa(protect)})
        elif name == "ThreadInfoList":
            header, entry, number = reader.unpack("<III", rva, "thread info")
            for slot in range(number):
                tid, dump_flags, dump_error, exit_status = reader.unpack(
                    "<IIII", rva + header + entry * slot, "thread info entry")
                result["thread_info"].append({
                    "thread_id": tid, "dump_flags": dump_flags,
                    "dump_error": dump_error, "exit_status": exit_status})
    return result


def region_for(summary, address):
    for info in summary["memory_info"]:
        if info["base"] <= address < info["base"] + info["size"]:
            return info
    return None


def memory_bytes(data, summary, address, length):
    """Bytes captured in the dump for [address, address + length), or None."""
    reader = Reader(data)
    for item in summary["memory"]:
        if item["base"] <= address and \
                address + length <= item["base"] + item["size"]:
            return reader.take(item["rva"] + address - item["base"], length,
                               "memory range")
    return None


def captured(summary, address):
    return any(item["base"] <= address < item["base"] + item["size"]
               for item in summary["memory"])


def printable(summary):
    result = dict(summary)
    result["memory"] = [{"base": hexa(item["base"]), "size": item["size"],
                         "why": item["why"]} for item in summary["memory"]]
    result["memory_info"] = [dict(item, base=hexa(item["base"]),
                                  allocation_base=hexa(item["allocation_base"]))
                             for item in summary["memory_info"]]
    return result


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    parser.add_argument("--range", type=lambda text: int(text, 0))
    parser.add_argument("--bytes", nargs=2, type=lambda text: int(text, 0))
    args = parser.parse_args(argv)
    data = args.dump.read_bytes()
    try:
        summary = parse(data)
    except MinidumpError as error:
        print("minidump error: %s" % error, file=sys.stderr)
        return 2
    output = printable(summary)
    if args.range is not None:
        region = region_for(summary, args.range)
        output["range"] = None if region is None else dict(
            region, base=hexa(region["base"]),
            allocation_base=hexa(region["allocation_base"]))
    if args.bytes is not None:
        chunk = memory_bytes(data, summary, *args.bytes)
        output["bytes"] = None if chunk is None else chunk.hex()
    print(json.dumps(output, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
