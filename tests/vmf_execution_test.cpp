#include "fixtures/vmf_scalar_vectors.hpp"
#include "tdur/ui/vmf_execution.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include <xmmintrin.h>

#pragma fenv_access(on)

namespace execution_fixture {
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
} // namespace execution_fixture
void* operator new(std::size_t size) {
    if (execution_fixture::count_allocations)
        ++execution_fixture::allocations;
    if (auto* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using namespace tdur::ui;
using namespace tdur::assets;
using Bytes = std::vector<std::byte>;
std::size_t checks = 0, failures = 0;
void check(bool ok, std::string_view message) {
    ++checks;
    if (!ok && ++failures <= 20)
        std::cerr << "FAIL: " << message << '\n';
}
void put(Bytes& out, std::size_t offset, std::uint32_t value, unsigned width,
         VmfProfile profile) {
    for (unsigned i = 0; i < width; ++i)
        out.at(offset + i) = static_cast<std::byte>(
            value >> (8 * (profile == VmfProfile::pc_2_8 ? i : width - i - 1)));
}
struct Builder {
    explicit Builder(VmfProfile p) : profile(p) {}
    VmfProfile profile;
    Bytes bytes;
    std::vector<std::size_t> offsets, compact;
    std::size_t position{};
    void op(std::uint16_t code, Bytes payload = {}) {
        const auto at = bytes.size();
        offsets.push_back(at);
        compact.push_back(position);
        bytes.resize(at + (code < 0x80 ? 2 : 4) + payload.size());
        put(bytes, at, code, 2, profile);
        if (code >= 0x80) {
            put(bytes, at + 2, static_cast<std::uint32_t>(payload.size()), 2,
                profile);
            std::copy(payload.begin(), payload.end(), bytes.begin() + at + 4);
            if (bytes.size() & 1U)
                bytes.push_back(std::byte{});
        }
        position += code < 0x80 ? 1 : 3 + payload.size();
    }
    void push(std::string_view text) {
        Bytes payload(text.size() + 2);
        for (std::size_t i = 0; i < text.size(); ++i)
            payload[i + 1] = static_cast<std::byte>(text[i]);
        op(0x96, std::move(payload));
    }
    void target(std::size_t branch, std::size_t target) {
        const auto delta = static_cast<std::int32_t>(compact[target]) -
                           static_cast<std::int32_t>(compact[branch] + 5);
        put(bytes, offsets[branch] + 4, static_cast<std::uint16_t>(delta), 2,
            profile);
    }
    VmfMovie movie() const {
        Bytes record(8 + bytes.size());
        while (record.size() % 4)
            record.push_back(std::byte{});
        put(record, 0, 12, 4, profile);
        put(record, 4, static_cast<std::uint32_t>(record.size()), 4, profile);
        std::copy(bytes.begin(), bytes.end(), record.begin() + 8);
        Bytes root(28);
        put(root, 0, 39, 4, profile);
        put(root, 4, 28, 4, profile);
        put(root, 12, 1, 2, profile);
        put(root, 20, 28, 4, profile);
        root.insert(root.end(), record.begin(), record.end());
        const auto show = root.size();
        root.resize(show + 16);
        put(root, show, 1, 4, profile);
        put(root, show + 4, 8, 4, profile);
        put(root, 24, static_cast<std::uint32_t>(show + 8), 4, profile);
        put(root, 16, static_cast<std::uint32_t>(root.size()), 4, profile);
        Bytes out(72);
        out[0] = std::byte{'V'};
        out[1] = std::byte{'M'};
        out[2] = std::byte{'F'};
        out[5] = std::byte{2};
        put(out, 6, profile == VmfProfile::pc_2_8 ? 8 : 5, 2, profile);
        out[8] = profile == VmfProfile::pc_2_8 ? std::byte{2} : std::byte{1};
        put(out, 60, 30, 2, profile);
        put(out, 62, 1, 2, profile);
        Bytes canonical(out.begin(), out.begin() + 68);
        if (profile == VmfProfile::xbox_2_5)
            for (const auto at : {6, 10, 60, 62, 64, 66})
                std::swap(canonical[at], canonical[at + 1]);
        std::uint32_t crc = 0xffffffffU;
        for (const auto byte : canonical) {
            crc ^= std::to_integer<std::uint32_t>(byte);
            for (unsigned i = 0; i < 8; ++i)
                crc = (crc >> 1) ^ (crc & 1U ? 0xedb88320U : 0U);
        }
        put(out, 68, crc ^ 0xffffffffU, 4, profile);
        out.insert(out.end(), root.begin(), root.end());
        const auto tail = out.size();
        out.resize(tail + 8);
        put(out, tail, 0x54584958U, 4, profile);
        auto parsed = VmfMovie::parse(out, profile);
        if (!parsed)
            throw std::runtime_error("source-owned script fixture invalid");
        return std::move(*parsed);
    }
};
VmfStack prepared_stack() {
    auto scratch = VmfTextScratch::create();
    if (!scratch)
        throw std::runtime_error("scratch allocation");
    auto result = VmfStack::create(std::move(*scratch));
    if (!result)
        throw std::runtime_error("stack allocation");
    return std::move(*result);
}
auto run(const VmfMovie& movie, VmfExecutionCursor& cursor, VmfStack& stack,
         VmfExecutionLimits limits = {}) {
    execution_fixture::allocations = 0;
    execution_fixture::count_allocations = true;
    auto result = execute_vmf_actions(movie, cursor, stack, limits);
    execution_fixture::count_allocations = false;
    check(execution_fixture::allocations == 0, "execution does not allocate");
    return result;
}
void scalar_oracles(VmfProfile profile) {
    auto stack = prepared_stack();
    const auto saved_csr = _mm_getcsr();
    std::fenv_t saved;
    if (std::fegetenv(&saved) != 0)
        throw std::runtime_error("FP save");
    for (const auto code :
         {0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x18}) {
        Builder builder(profile);
        builder.op(static_cast<std::uint16_t>(code));
        builder.op(0);
        const auto movie = builder.movie();
        for (unsigned mode = 0; mode != 4; ++mode) {
            _mm_setcsr(((saved_csr | 0x1f80U) & ~0xe07fU) | mode * 0x2000U);
            for (const auto& vector : vmf_scalar_fixture::vectors) {
                if (vector.opcode != code)
                    continue;
                stack.clear();
                check(stack.push_number_bits(vector.a).has_value(), "oracle a");
                if (!vector.unary)
                    check(stack.push_number_bits(vector.b).has_value(),
                          "oracle b");
                VmfExecutionCursor cursor;
                const auto result = run(movie, cursor, stack);
                check(result && result->status == VmfExecutionStatus::ended &&
                          result->instructions_executed == 2 && cursor.ended,
                      "scalar program reaches End");
                check(stack.size() == 1, "one scalar result");
                const auto value = stack.pop_value();
                if (vector.boolean) {
                    auto text = value ? value->text_reference() : std::nullopt;
                    check(value && value->kind() == VmfValueKind::owned_text &&
                              text &&
                              text->text() ==
                                  (vector.expected[mode] ? "1" : "0"),
                          "native booleans are owned byte strings");
                } else {
                    const auto bits =
                        value ? value->number_bits() : std::nullopt;
                    const auto expected = vector.expected[mode];
                    check(bits && ((expected & 0x7fffffffU) > 0x7f800000U
                                       ? std::isnan(std::bit_cast<float>(*bits))
                                       : *bits == expected),
                          "frozen rational numeric result");
                }
            }
        }
    }
    check(std::fesetenv(&saved) == 0, "FP restore");
    _mm_setcsr(saved_csr);
}
void strings_and_branches(VmfProfile profile) {
    auto stack = prepared_stack();
    for (const auto opcode : {0x13, 0x21}) {
        Builder builder(profile);
        builder.op(static_cast<std::uint16_t>(opcode));
        builder.op(0);
        const auto movie = builder.movie();
        check(stack.push_number(1).has_value() &&
                  stack.push_number(2).has_value(),
              "alias inputs");
        VmfExecutionCursor cursor;
        check(run(movie, cursor, stack).has_value(), "alias scalar execution");
        const auto result = stack.pop_text();
        check(result && result->text() == (opcode == 0x13 ? "1" : "11"),
              "two numeric pops preserve shared-buffer alias");
    }
    Builder concat(profile);
    concat.push("left");
    concat.push("right");
    concat.op(0x21);
    concat.op(0);
    const auto concat_movie = concat.movie();
    VmfExecutionCursor cursor;
    for (int i = 0; i != 3; ++i) {
        const auto report = run(concat_movie, cursor, stack, {1, 100});
        check(report &&
                  report->status == VmfExecutionStatus::instruction_limit &&
                  report->instructions_executed == 1 &&
                  cursor.next_action == static_cast<std::size_t>(i + 1),
              "budget stops at next instruction without replay");
    }
    const auto end = run(concat_movie, cursor, stack, {1, 100});
    check(end && end->status == VmfExecutionStatus::ended, "resumed End");
    const auto text = stack.pop_text();
    check(text && text->text() == "leftright", "resumed concatenation");
    const auto ended_again = run(concat_movie, cursor, stack);
    check(ended_again && ended_again->instructions_executed == 0,
          "ended cursor is idempotent");

    Builder branch(profile);
    branch.push("1");
    branch.op(0x9d, Bytes(2));
    branch.push("bad");
    branch.push("good");
    branch.op(0);
    branch.target(1, 3);
    const auto branch_movie = branch.movie();
    cursor = {};
    check(run(branch_movie, cursor, stack).has_value(), "compact If execution");
    const auto good = stack.pop_text();
    check(good && good->text() == "good" && stack.size() == 0,
          "compact jump skips bad Push");
    Builder loop(profile);
    loop.op(0x99, Bytes(2));
    loop.op(0);
    loop.target(0, 0);
    const auto loop_movie = loop.movie();
    cursor = {};
    const auto bounded = run(loop_movie, cursor, stack, {7, 100});
    check(bounded && bounded->status == VmfExecutionStatus::instruction_limit &&
              bounded->instructions_executed == 7 && cursor.next_action == 0,
          "backedge bounded by resumable instruction budget");
    const auto resumed = run(loop_movie, cursor, stack, {3, 100});
    check(resumed && resumed->instructions_executed == 3,
          "same loop cursor resumes");

    Builder extract(profile);
    extract.push("abcd");
    extract.push("2");
    extract.push("2");
    extract.op(0x15);
    extract.op(0);
    const auto extraction = extract.movie();
    cursor = {};
    check(run(extraction, cursor, stack).has_value(), "extract program");
    const auto middle = stack.pop_text();
    check(middle && middle->text() == "bc", "one-based byte substring");
    Builder invalid(profile);
    invalid.push("abcd");
    invalid.push("0");
    invalid.push("-1");
    invalid.op(0x15);
    invalid.op(0);
    const auto invalid_movie = invalid.movie();
    cursor = {};
    const auto error = run(invalid_movie, cursor, stack);
    check(!error && error.error().code == VmfExecutionErrorCode::string_range &&
              cursor.faulted,
          "negative substring length is an explicit terminal fault");
    const auto retry = run(invalid_movie, cursor, stack);
    check(!retry && retry.error().code == VmfExecutionErrorCode::invalid_cursor,
          "faulted cursor cannot replay partial pops");
    stack.clear();
    Builder length(profile);
    length.push("\xc3\xa9");
    length.op(0x14);
    length.op(0);
    const auto length_movie = length.movie();
    cursor = {};
    check(run(length_movie, cursor, stack).has_value(),
          "byte length execution");
    const auto two = stack.pop_number();
    check(two && *two == 2, "length counts bytes, not Unicode");
    cursor = {};
    const auto limit = run(concat_movie, cursor, stack, {10, 2});
    check(!limit && limit.error().code == VmfExecutionErrorCode::text_limit &&
              stack.size() == 0,
          "Push charges logical bytes before mutation");
}
void host_boundaries(VmfProfile profile) {
    auto stack = prepared_stack();
    for (const auto opcode : {4, 5, 6, 7, 0x1c, 0x1d, 0x20, 0x22, 0x23, 0x26,
                              0x81, 0x83, 0x8b, 0x8c, 0x9a, 0x9e, 0x9f}) {
        Builder builder(profile);
        builder.push("argument");
        Bytes payload;
        if (opcode == 0x81)
            payload.resize(2);
        if (opcode == 0x83)
            payload.resize(2);
        if (opcode == 0x8b || opcode == 0x8c || opcode == 0x9a ||
            opcode == 0x9f)
            payload.resize(1);
        builder.op(static_cast<std::uint16_t>(opcode), payload);
        builder.op(0);
        const auto movie = builder.movie();
        VmfExecutionCursor cursor;
        const auto stopped = run(movie, cursor, stack);
        check(stopped && stopped->status == VmfExecutionStatus::needs_host &&
                  stopped->instructions_executed == 1 &&
                  stopped->next_opcode == opcode && cursor.next_action == 1 &&
                  stack.size() == 1,
              "host boundary stops before effects");
        const auto repeated = run(movie, cursor, stack);
        check(repeated && repeated->status == VmfExecutionStatus::needs_host &&
                  repeated->instructions_executed == 0 && stack.size() == 1,
              "waiting host does not replay Push or consume arguments");
        stack.clear();
    }
}
void cursor_and_native_edges(VmfProfile profile) {
    auto stack = prepared_stack();
    Builder builder(profile);
    builder.push("pending");
    builder.op(0);
    const auto movie = builder.movie();
    VmfExecutionCursor forged;
    forged.ended = true;
    const auto fake_end = run(movie, forged, stack);
    check(!fake_end &&
              fake_end.error().code == VmfExecutionErrorCode::invalid_cursor,
          "ended flag cannot assert completion before a Push");
    VmfExecutionCursor cursor;
    const auto zero_budget = run(movie, cursor, stack, {0, 100});
    check(zero_budget &&
              zero_budget->status == VmfExecutionStatus::instruction_limit &&
              cursor.next_action == 0 && stack.size() == 0,
          "zero budget preserves the unexecuted instruction");
    const auto oversized = run(movie, cursor, stack, {1, 16'777'217});
    check(!oversized &&
              oversized.error().code == VmfExecutionErrorCode::invalid_limits,
          "length conversion domain guard");
    cursor = {};
    cursor.script_index = movie.scripts().size();
    check(!run(movie, cursor, stack), "script cursor bounds");
    cursor = {};
    cursor.next_action = movie.scripts()[0].action_count;
    check(!run(movie, cursor, stack), "action cursor bounds");

    Builder conditional(profile);
    conditional.op(0x9d, Bytes(2));
    conditional.push("not_taken");
    conditional.push("taken");
    conditional.op(0);
    conditional.target(0, 2);
    const auto condition_movie = conditional.movie();
    for (const auto bits : {0x7fc00000U, 0x80000000U}) {
        cursor = {};
        check(stack.push_number_bits(bits).has_value(), "If edge input");
        const auto report = run(condition_movie, cursor, stack);
        check(report && report->status == VmfExecutionStatus::ended &&
                  stack.size() == (bits == 0x7fc00000U ? 1U : 2U),
              "If treats NaN as true and signed zero as false");
        stack.clear();
    }
    cursor = {};
    check(run(condition_movie, cursor, stack).has_value() && stack.size() == 2,
          "empty If does not take its branch");
    stack.clear();
    Builder underflow(profile);
    underflow.op(0x17);
    underflow.op(0x0a);
    underflow.op(0);
    const auto underflow_movie = underflow.movie();
    cursor = {};
    check(run(underflow_movie, cursor, stack).has_value(),
          "empty Pop no-op and missing numeric operands");
    const auto zero = stack.pop_number();
    check(zero && std::bit_cast<std::uint32_t>(*zero) == 0,
          "missing Add inputs produce native +0");
}

void private_corpus(const std::filesystem::path& directory) {
    std::size_t movies = 0, scripts = 0, ended = 0, host = 0, limited = 0,
                faults = 0, steps = 0;
    std::array<std::size_t, 256> host_opcodes{};
    auto stack = prepared_stack();
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".vmf")
            continue;
        if (++movies > 39 || entry.file_size() > 64U * 1024U * 1024U)
            throw std::runtime_error("corpus bounds");
        const auto name = entry.path().filename().string();
        if (!name.starts_with("xbox_") && !name.starts_with("pc_"))
            throw std::runtime_error("corpus profile");
        Bytes bytes(static_cast<std::size_t>(entry.file_size()));
        std::ifstream input(entry.path(), std::ios::binary);
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input)
            throw std::runtime_error("corpus read");
        auto movie = VmfMovie::parse(bytes, name.starts_with("xbox_")
                                                ? VmfProfile::xbox_2_5
                                                : VmfProfile::pc_2_8);
        if (!movie)
            throw std::runtime_error("corpus parse");
        for (std::size_t i = 0; i < movie->scripts().size(); ++i) {
            stack.clear(); // Explicit isolated-entry diagnostic, not native
                           // scheduling.
            VmfExecutionCursor cursor{i};
            const auto report = run(*movie, cursor, stack, {1000, 65536});
            ++scripts;
            if (!report) {
                ++faults;
                continue;
            }
            steps += report->instructions_executed;
            if (report->status == VmfExecutionStatus::ended)
                ++ended;
            else if (report->status == VmfExecutionStatus::needs_host) {
                ++host;
                ++host_opcodes[report->next_opcode];
            } else
                ++limited;
        }
    }
    check(movies == 39 && scripts == 27685, "frozen private corpus size");
    check(ended + host + limited + faults == scripts,
          "all isolated outcomes recorded");
    std::cout
        << "{\"corpus_movies\":" << movies << ",\"scripts\":" << scripts
        << ",\"ended\":" << ended << ",\"needs_host\":" << host
        << ",\"limited\":" << limited << ",\"faults\":" << faults
        << ",\"instructions\":" << steps
        << ",\"entry_assumption\":\"empty isolated stack\",\"host_opcodes\":{";
    bool comma = false;
    for (std::size_t i = 0; i != host_opcodes.size(); ++i)
        if (host_opcodes[i]) {
            if (comma)
                std::cout << ',';
            comma = true;
            std::cout << '"' << i << "\":" << host_opcodes[i];
        }
    std::cout << "}}\n";
}
} // namespace
int main(int argc, char** argv) {
    for (const auto profile : {VmfProfile::pc_2_8, VmfProfile::xbox_2_5}) {
        scalar_oracles(profile);
        strings_and_branches(profile);
        host_boundaries(profile);
        cursor_and_native_edges(profile);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--corpus")
        private_corpus(argv[2]);
    else if (argc != 1)
        check(false, "usage: vmf_execution_test [--corpus directory]");
    std::cout << "{\"scope\":\"vmf-execution\",\"bits\":" << sizeof(void*) * 8
              << ",\"checks\":" << checks << ",\"failures\":" << failures
              << ",\"game_executed\":false}\n";
    return failures ? 1 : 0;
}
