#include "tdur/ui/vmf_display_runtime.hpp"
#include "tdur/ui/vmf_number.hpp"
#include "tdur/ui/vmf_variable_reference.hpp"
#include "vmf_property_math.hpp"
#include "vmf_transform_ops.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <xmmintrin.h>

namespace tdur::ui {
namespace {
using Index = std::uint32_t;
constexpr Index None = std::numeric_limits<Index>::max();
using Code = VmfDisplayRuntimeErrorCode;
template <class T> using Result = std::expected<T, VmfDisplayRuntimeError>;

auto reject(Code code, std::string_view detail) noexcept {
    return std::unexpected(VmfDisplayRuntimeError{code, detail});
}
auto variable_reject(VmfVariableError nested) noexcept {
    return std::unexpected(VmfDisplayRuntimeError{
        Code::variables, "VMF variable service rejected", nested});
}
std::atomic<std::uint64_t> next_context{1};

std::optional<Index> reserve_context() noexcept {
    auto current = next_context.load(std::memory_order_relaxed);
    while (current <= None) {
        if (next_context.compare_exchange_weak(current, current + 1,
                                               std::memory_order_relaxed))
            return static_cast<Index>(current);
    }
    return std::nullopt;
}
constexpr std::size_t pool(VmfInstanceKind kind) noexcept {
    return kind == VmfInstanceKind::timeline ? 0 : 1;
}
} // namespace

VmfSpriteIdentity vmf_sprite_identity(VmfInstanceHandle h) noexcept {
    return {(std::uint64_t{h.context} << 32U) | h.slot, h.generation};
}
VmfInstanceHandle vmf_instance_handle(VmfSpriteIdentity id) noexcept {
    return {static_cast<Index>(id.instance >> 32U),
            static_cast<Index>(id.instance), id.generation};
}

struct VmfDisplayRuntime::Storage {
    struct Node {
        Index generation = 1;
        bool live{};
        bool bound{}; // Native resource=NULL is distinct from characterID 0.
        Index free_next = None;
        Index owner = None;
        Index child = None;
        Index next = None;
        Index previous = None;
        std::uint32_t character{};
        std::uint16_t depth{};
        std::uint16_t clip{};
        std::uint32_t ratio{};
        std::uint32_t flags{};
        std::array<char, 32> name{};
        // 448480 zeroes the initial node pool, not identity matrices.
        VmfTransform local{0, {}, {}};
        VmfTransform composed{0, {}, {}};
    };
    struct Instance {
        Index generation = 1;
        bool live{};
        bool cached{};
        VmfInstanceKind kind{};
        Index free_next = None;
        Index parent = None;
        Index node = None;
        Index head = None;
        Index cache_head = None;
        Index cache_next = None;
        std::uint32_t character{};
        std::int32_t depth = -1;
        std::int32_t birth = 1;
        std::optional<std::uint32_t> scope;
        VmfDriverFrames frames;
        std::uint32_t play{};
        std::uint32_t flags{};
        std::uint32_t balance{};
        std::size_t node_count{};
        std::size_t cache_count{};
        std::size_t parent_depth = 1;
        // 448680 constructs these once. Reinitialization keeps composed.
        VmfTransform local;
        VmfTransform composed;
    };

    VmfProgramPtr program;
    VmfDisplayRuntimeLimits limits;
    Index context{};
    Index root_index = None;
    std::uint64_t next_scope{};
    std::vector<Instance> instances;
    std::vector<Node> nodes;
    std::array<Index, 2> free_instance{None, None};
    std::array<std::size_t, 2> free_counts{};
    Index free_node = None;
    std::size_t live_instances{};
    std::size_t live_nodes{};
    std::unique_ptr<VmfVariables> dictionary;
    std::vector<std::uint32_t> cleanup_scopes;
    std::size_t cleanup_count{};
    bool text_bindings{};
    VmfTextBindingLimits text_limits;
    std::vector<VmfTextResourceSnapshot> text_resources;
    std::array<std::vector<char>, 2> text_scratch;
    std::size_t active_scratch{};
    std::size_t scratch_length{};

    bool text_ready(std::uint32_t character) const noexcept {
        if (!text_bindings)
            return false;
        const auto *data = program->text_data();
        const auto index = data->field_index(character);
        return index && (data->fields()[*index].raw_flags_2e & 0x10U) != 0;
    }

    VmfInstanceHandle handle(Index i) const noexcept {
        return {context, i, instances[i].generation};
    }
    VmfNodeHandle node_handle(Index i) const noexcept {
        return {context, i, nodes[i].generation};
    }
    Result<Index> resolve(VmfInstanceHandle h) const noexcept {
        if (root_index == None)
            return reject(Code::closed, "VMF display context closed");
        if (h.context != context || h.slot >= instances.size() ||
            !instances[h.slot].live ||
            instances[h.slot].generation != h.generation)
            return reject(Code::invalid_handle, "VMF instance handle");
        return h.slot;
    }
    Result<Index> timeline(VmfInstanceHandle h) const noexcept {
        auto i = resolve(h);
        if (!i)
            return std::unexpected(i.error());
        if (instances[*i].kind != VmfInstanceKind::timeline)
            return reject(Code::wrong_kind, "VMF timeline required");
        return i;
    }
    Result<Index> resolve(VmfNodeHandle h) const noexcept {
        if (root_index == None)
            return reject(Code::closed, "VMF display context closed");
        if (h.context != context || h.slot >= nodes.size() ||
            !nodes[h.slot].live || nodes[h.slot].generation != h.generation)
            return reject(Code::invalid_handle, "VMF node handle");
        return h.slot;
    }
    const assets::VmfSprite& sprite(Index i) const noexcept {
        return program->movie()
            .sprites()[*program->sprite_for_character(instances[i].character)];
    }
    std::uint32_t resource_type(std::uint32_t character) const noexcept {
        return program->movie()
            .records()[*program->movie().definition(character)]
            .type;
    }
    bool owns(Index owner,
              const assets::VmfDisplayLocation& where) const noexcept {
        return where.sprite_index < program->movie().sprites().size() &&
               program->movie().sprites()[where.sprite_index].character_id ==
                   instances[owner].character;
    }
    Index find_node(Index owner, std::uint16_t depth) const noexcept {
        for (auto i = instances[owner].head; i != None; i = nodes[i].next) {
            if (nodes[i].depth == depth)
                return i;
            if (nodes[i].depth > depth)
                break;
        }
        return None;
    }
    Index find_cached(Index owner, std::uint16_t depth) const noexcept {
        for (auto i = instances[owner].cache_head; i != None;
             i = instances[i].cache_next)
            if (instances[i].depth == depth)
                return i;
        return None;
    }
    Index extract_cached(Index owner, std::uint16_t depth) noexcept {
        auto* link = &instances[owner].cache_head;
        while (*link != None) {
            const auto i = *link;
            auto& child = instances[i];
            if (child.depth == depth) {
                *link = child.cache_next;
                child.cache_next = None;
                child.cached = false;
                --instances[owner].cache_count;
                return i;
            }
            link = &child.cache_next;
        }
        return None;
    }
    void cache(Index owner, Index i) noexcept {
        auto& child = instances[i];
        child.cached = true;
        child.node = None;
        child.cache_next = instances[owner].cache_head;
        instances[owner].cache_head = i;
        ++instances[owner].cache_count;
    }
    Index allocate_node(Index owner, std::uint16_t depth) noexcept {
        const auto i = free_node;
        auto& n = nodes[i];
        free_node = n.free_next;
        n.live = true;
        n.free_next = None;
        n.owner = owner;
        n.depth = depth;
        n.child = None;
        n.previous = None;
        auto next = instances[owner].head;
        while (next != None && nodes[next].depth < depth) {
            n.previous = next;
            next = nodes[next].next;
        }
        n.next = next;
        if (n.previous == None)
            instances[owner].head = i;
        else
            nodes[n.previous].next = i;
        if (next != None)
            nodes[next].previous = i;
        ++instances[owner].node_count;
        ++live_nodes;
        return i;
    }
    void release_node(Index i) noexcept {
        auto& n = nodes[i];
        if (n.previous == None)
            instances[n.owner].head = n.next;
        else
            nodes[n.previous].next = n.next;
        if (n.next != None)
            nodes[n.next].previous = n.previous;
        --instances[n.owner].node_count;
        --live_nodes;
        n.live = false;
        n.child = None;
        n.next = n.previous = None;
        // Retire exhausted identity slots rather than creating an ABA alias.
        if (n.generation != None) {
            ++n.generation;
            n.free_next = free_node;
            free_node = i;
        }
    }
    void propagate_node(Index i) noexcept {
        auto& n = nodes[i];
        if (n.child != None &&
            instances[n.child].kind == VmfInstanceKind::timeline) {
            propagate(n.child);
            n.composed = instances[n.child].composed;
        } else {
            n.composed = compose_vmf_transform(instances[n.owner].composed,
                                               n.local, n.composed.flags);
        }
    }
    void propagate(Index i) noexcept {
        auto& instance = instances[i];
        if (instance.parent == None)
            instance.composed = instance.local;
        else
            instance.composed =
                compose_vmf_transform(instances[instance.parent].composed,
                                      instance.local, instance.composed.flags);
        for (auto n = instance.head; n != None; n = nodes[n].next)
            propagate_node(n);
    }
    Index construct(VmfInstanceKind kind, std::uint32_t character, Index parent,
                    Index node) noexcept {
        const auto which = pool(kind);
        const auto i = free_instance[which];
        auto& item = instances[i];
        free_instance[which] = item.free_next;
        --free_counts[which];
        ++live_instances;
        item.free_next = None;
        item.live = true;
        item.cached = false;
        item.parent = parent;
        item.node = node;
        item.character = character;
        item.depth = node == None ? -1 : nodes[node].depth;
        item.birth = parent == None ? 1 : instances[parent].frames.current;
        item.parent_depth =
            parent == None ? 1 : instances[parent].parent_depth + 1;
        item.cache_next = None;
        item.balance = 0;
        if (kind == VmfInstanceKind::timeline) {
            item.frames = {};
            item.play = sprite(i).frame_count == 1 ? 2U : 1U;
            item.flags = 7;
            reset_vmf_transform(item.local);
            propagate(i); // Deliberately retains this pool slot's world flags.
            item.scope = static_cast<Index>(next_scope++);
        } else {
            item.scope.reset();
            item.flags = 0;
            item.play = 0;
        }
        return i;
    }
    // These walks only follow a constructed ownership tree. No public API can
    // inject links; the parent-depth quota bounds recursion, including cache.
    bool needs_services(Index i) const noexcept {
        const auto& item = instances[i];
        if (item.kind == VmfInstanceKind::button)
            return false;
        if ((item.flags & 0x200U) != 0 && !dictionary)
            return true;
        for (auto n = item.head; n != None; n = nodes[n].next) {
            if (nodes[n].bound && resource_type(nodes[n].character) == 37 &&
                !text_ready(nodes[n].character))
                return true;
            if (nodes[n].child != None && needs_services(nodes[n].child))
                return true;
        }
        for (auto c = item.cache_head; c != None; c = instances[c].cache_next)
            if (needs_services(c))
                return true;
        return false;
    }
    // Build the native destruction order without changing topology. This
    // lets a modern dictionary-chain refusal leave both services untouched.
    Result<void> plan_destroy(Index i, Index newly_armed = None) noexcept {
        if (!dictionary)
            return {};
        const auto &item = instances[i];
        if (item.kind == VmfInstanceKind::timeline &&
            ((item.flags & 0x200U) || i == newly_armed)) {
            if (!item.scope || cleanup_count == cleanup_scopes.size())
                return reject(Code::invalid_native_state,
                              "VMF cleanup scope plan");
            cleanup_scopes[cleanup_count++] = *item.scope;
        }
        auto tail = None;
        for (auto n = item.head; n != None; n = nodes[n].next) {
            tail = n;
            const auto child = nodes[n].child;
            if (child != None && instances[child].birth <
                                     std::numeric_limits<std::int32_t>::max()) {
                const auto result = plan_destroy(child, newly_armed);
                if (!result)
                    return result;
            }
        }
        // destroy temporarily caches INT_MAX-born active children at head,
        // then destroys that new cache in reverse active-node order.
        for (auto n = tail; n != None; n = nodes[n].previous) {
            const auto child = nodes[n].child;
            if (child != None && instances[child].birth ==
                                     std::numeric_limits<std::int32_t>::max()) {
                const auto result = plan_destroy(child, newly_armed);
                if (!result)
                    return result;
            }
        }
        for (auto c = item.cache_head; c != None; c = instances[c].cache_next) {
            const auto result = plan_destroy(c, newly_armed);
            if (!result)
                return result;
        }
        return {};
    }
    Result<void>
    commit_cleanup(std::optional<VmfVariableTextWrite> write = {}) noexcept {
        if (!dictionary)
            return {};
        const auto result = dictionary->write_text_and_erase_scopes(
            write, {cleanup_scopes.data(), cleanup_count});
        if (!result)
            return variable_reject(result.error());
        return {};
    }
    template <class Assign>
    Result<void> write_variable(VmfInstanceHandle target,
                                std::optional<std::string_view> name,
                                Assign &&assign) noexcept {
        const auto i = timeline(target);
        if (!i)
            return std::unexpected(i.error());
        if (!dictionary)
            return reject(Code::needs_services, "VMF variable dictionary");
        auto &item = instances[*i];
        if (!item.scope)
            return reject(Code::invalid_native_state, "VMF variable scope");
        if (name) {
            const auto result = assign(*dictionary, *item.scope, *name);
            if (!result &&
                result.error().code != VmfVariableErrorCode::table_full)
                return variable_reject(result.error());
        }
        item.flags |= 0x200U;
        return {};
    }
    std::size_t recyclable(Index i, VmfInstanceKind kind) const noexcept {
        const auto& item = instances[i];
        std::size_t count =
            item.kind == kind && item.generation != None ? 1 : 0;
        for (auto n = item.head; n != None; n = nodes[n].next)
            if (nodes[n].child != None)
                count += recyclable(nodes[n].child, kind);
        for (auto c = item.cache_head; c != None; c = instances[c].cache_next)
            count += recyclable(c, kind);
        return count;
    }
    void destroy(Index i) noexcept {
        auto& item = instances[i];
        // Scope erases were committed in order before topology mutation.
        // Missing services are refused by preflight, never silently skipped.
        while (item.head != None) {
            const auto n = item.head;
            const auto child = nodes[n].child;
            if (child != None) {
                if (instances[child].birth <
                    std::numeric_limits<std::int32_t>::max())
                    destroy(child);
                else
                    cache(i, child);
                nodes[n].child = None;
            }
            release_node(n);
        }
        while (item.cache_head != None) {
            const auto c = item.cache_head;
            item.cache_head = instances[c].cache_next;
            --item.cache_count;
            instances[c].cache_next = None;
            destroy(c);
        }
        item.live = false;
        item.cached = false;
        item.node = None;
        --live_instances;
        if (item.kind == VmfInstanceKind::button) {
            item.parent = None;
            item.character = 0;
            item.birth = 0;
            item.depth = -1;
        }
        if (item.generation != None) {
            ++item.generation;
            const auto which = pool(item.kind);
            item.free_next = free_instance[which];
            free_instance[which] = i;
            ++free_counts[which];
        }
    }
    Result<void> preflight_child(Index owner, Index old,
                                 VmfInstanceKind kind) const noexcept {
        if (instances[owner].parent_depth >= limits.max_parent_depth)
            return reject(Code::resource_limit, "VMF parent depth limit");
        if (kind == VmfInstanceKind::timeline && next_scope > None)
            return reject(Code::resource_limit, "VMF scope identity exhausted");
        const auto available =
            free_counts[pool(kind)] + (old == None ? 0 : recyclable(old, kind));
        if (available == 0)
            return reject(Code::resource_limit, "VMF child pool exhausted");
        const auto next = free_instance[pool(kind)];
        if (next != None && (instances[next].head != None ||
                             instances[next].cache_head != None ||
                             instances[next].node_count != 0 ||
                             instances[next].cache_count != 0))
            return reject(Code::invalid_native_state, "VMF dirty free child");
        return {};
    }
    // A bounded temporary view of the exact point before CALL44BDC0. Public
    // access is serialized and lookup/copy have no external callbacks. Restore
    // the few touched links/scalars on every exit; do not copy entire pools.
    struct TextOverlay {
        Storage &s;
        Index owner, node, cached, cache_previous = None;
        bool created;
        Node old_node;
        Instance old_owner;
        std::optional<Instance> old_cached;
        Index old_free, previous = None, next = None;
        std::size_t old_live;
        TextOverlay(Storage &storage, Index o, Index n, bool fresh, Index cache,
                    std::uint16_t depth, std::uint32_t character) noexcept
            : s(storage), owner(o), node(n), cached(cache), created(fresh),
              old_node(s.nodes[n]), old_owner(s.instances[o]),
              old_free(s.free_node), old_live(s.live_nodes) {
            if (created) {
                (void)s.allocate_node(owner, depth);
                previous = s.nodes[node].previous;
                next = s.nodes[node].next;
            }
            if (cached != None) {
                old_cached = s.instances[cached];
                for (auto c = s.instances[owner].cache_head; c != cached;
                     c = s.instances[c].cache_next)
                    cache_previous = c;
                s.nodes[node].child = s.extract_cached(owner, depth);
                s.instances[cached].node = node;
            }
            s.nodes[node].character = character;
            s.nodes[node].bound = true;
            s.nodes[node].name[0] = '\0';
        }
        ~TextOverlay() {
            if (created) {
                if (previous != None)
                    s.nodes[previous].next = next;
                if (next != None)
                    s.nodes[next].previous = previous;
            }
            s.instances[owner] = old_owner;
            if (cached != None) {
                if (cache_previous != None)
                    s.instances[cache_previous].cache_next = cached;
                s.instances[cached] = *old_cached;
            }
            s.nodes[node] = old_node;
            s.free_node = old_free;
            s.live_nodes = old_live;
        }
        TextOverlay(const TextOverlay &) = delete;
        TextOverlay &operator=(const TextOverlay &) = delete;
    };
    struct TextPlan {
        std::optional<std::size_t> resource;
        std::optional<VmfVariableTextWrite> write;
        Index arm = None;
        bool publish_scratch{};
        std::size_t scratch_length{};
    };
    Result<TextPlan> prepare_text(const VmfDisplayRuntime &host, Index owner,
                                  Index node, bool created, Index cached,
                                  const assets::VmfPlacement &p,
                                  std::uint32_t character) noexcept {
        const auto &data = *program->text_data();
        TextPlan plan;
        plan.resource = data.field_index(character);
        if (!plan.resource || !dictionary)
            return reject(Code::invalid_native_state, "VMF text binding data");
        const auto &field = data.fields()[*plan.resource];
        const auto reference = classify_vmf_variable_reference(
            field.variable_name.data(), text_limits.targets.max_path_bytes);
        if (!reference)
            return reject(Code::resource_limit, "VMF text expression limit");
        const TextOverlay overlay(*this, owner, node, created, cached, p.depth,
                                  character);
        auto target = owner;
        if (reference->target_path) {
            const auto resolved = host.resolve_target(
                handle(owner), *reference->target_path, text_limits.targets);
            if (!resolved)
                return std::unexpected(resolved.error());
            target = resolved->found ? resolved->target->slot : None;
        }
        if (target == None)
            return plan; // Both native resolutions miss; scratch is retained.
        if (!reference->variable_name)
            return reject(Code::invalid_native_state,
                          "VMF text lookup would dereference NULL name");
        if (!instances[target].scope)
            return reject(Code::invalid_native_state, "VMF text target scope");
        auto &scratch = text_scratch[active_scratch ^ 1U];
        const auto copied = dictionary->copy_text(
            *instances[target].scope, *reference->variable_name, scratch);
        if (!copied)
            return variable_reject(copied.error());
        const auto end = std::find(scratch.begin(), scratch.end(), '\0');
        if (end == scratch.end())
            return reject(Code::invalid_native_state, "VMF text terminator");
        plan.publish_scratch = true;
        plan.scratch_length = static_cast<std::size_t>(end - scratch.begin());
        if (!*copied) {
            // Immutable topology/name inputs give the same result to449240's
            // second classification. Reuse it, without an observable callback.
            plan.arm = target;
            plan.write = VmfVariableTextWrite{*instances[target].scope,
                                              *reference->variable_name,
                                              field.initial_text};
        }
        return plan;
    }
    void publish_text(const TextPlan &plan) noexcept {
        if (!plan.resource)
            return;
        const auto &field = program->text_data()->fields()[*plan.resource];
        auto &state = text_resources[*plan.resource];
        state.font_record_index = field.font_record_index;
        if (field.font_record_index)
            state.companion_record_index = field.companion_record_index;
        state.binding_executed = true;
        if (plan.publish_scratch) {
            active_scratch ^= 1U;
            scratch_length = plan.scratch_length;
        }
        if (plan.arm != None)
            instances[plan.arm].flags |= 0x200U;
    }
    Result<VmfPlaceReport> place(const VmfDisplayRuntime &host, Index owner,
                                 const assets::VmfPlacement &p) noexcept {
        if (p.depth >= sprite(owner).raw_0e) {
            ++instances[owner].balance;
            return VmfPlaceReport{VmfPlaceDisposition::ignored_depth, true, {}};
        }
        auto n = find_node(owner, p.depth);
        const bool created = n == None;
        if (created && free_node == None)
            return reject(Code::resource_limit, "VMF node pool exhausted");
        const auto candidate_node = created ? free_node : n;
        const auto cached = find_cached(owner, p.depth);
        const auto active = created ? None : nodes[n].child;
        if (cached != None && active != None)
            return reject(Code::invalid_native_state,
                          "VMF duplicate child depth");
        if (cached != None) {
            for (auto c = instances[cached].cache_next; c != None;
                 c = instances[c].cache_next)
                if (instances[c].depth == p.depth)
                    return reject(Code::invalid_native_state,
                                  "VMF duplicate cached depth");
        }
        const auto candidate = cached != None ? cached : active;
        const bool character_present = (p.flags & 2U) != 0;
        if (!character_present && !nodes[candidate_node].bound)
            return reject(Code::invalid_native_state, "VMF unbound pool node");
        const auto character = character_present
                                   ? *p.character_id
                                   : nodes[candidate_node].character;
        const auto type = resource_type(character);
        if (type == 37 && !text_ready(character))
            return reject(Code::needs_services,
                          "VMF text variable/focus services");
        const bool kept =
            character_present && candidate != None &&
            instances[candidate].birth >= instances[owner].frames.current;
        const auto discarded = character_present && !kept ? candidate : None;
        if (discarded != None && needs_services(discarded))
            return reject(Code::needs_services,
                          "VMF instance cleanup services");
        const bool spawn =
            character_present && !kept && (type == 39 || type == 7);
        const auto kind =
            type == 39 ? VmfInstanceKind::timeline : VmfInstanceKind::button;
        if (spawn) {
            auto capacity = preflight_child(owner, discarded, kind);
            if (!capacity)
                return std::unexpected(capacity.error());
        }

        TextPlan text;
        if (character_present && type == 37) {
            const auto prepared = prepare_text(host, owner, candidate_node,
                                               created, cached, p, character);
            if (!prepared)
                return std::unexpected(prepared.error());
            text = *prepared;
        }
        cleanup_count = 0;
        if (discarded != None) {
            const auto planned = plan_destroy(discarded, text.arm);
            if (!planned)
                return std::unexpected(planned.error());
        }
        const auto cleaned = commit_cleanup(text.write);
        if (!cleaned)
            return std::unexpected(cleaned.error());

        // All added guards precede the native mutation sequence.
        if (created)
            n = allocate_node(owner, p.depth);
        auto& node = nodes[n];
        if (cached != None) {
            node.child = extract_cached(owner, p.depth);
            instances[node.child].node = n;
        }
        if (character_present) {
            node.character = character;
            node.bound = true;
            node.name[0] = '\0';
            publish_text(text);
            if (discarded != None) {
                destroy(discarded);
                node.child = None;
            }
            if (spawn)
                node.child = construct(kind, character, owner, n);
        }
        const auto timeline_child =
            node.child != None &&
                    instances[node.child].kind == VmfInstanceKind::timeline
                ? node.child
                : None;
        VmfTransform backup;
        std::uint32_t overrides{};
        if (kept && timeline_child != None) {
            backup = instances[timeline_child].local;
            overrides = instances[timeline_child].flags;
        }
        if ((p.flags & 1U) == 0) {
            node.flags = 0;
            reset_vmf_transform(node.local);
            if (timeline_child != None)
                reset_vmf_transform(instances[timeline_child].local);
        }
        node.flags |= p.flags;
        if (p.clip_depth)
            node.clip = *p.clip_depth;
        if (p.color_words) {
            node.local.flags |= 1U;
            node.local.color_words = *p.color_words;
            if (timeline_child != None) {
                instances[timeline_child].local.flags |= 1U;
                instances[timeline_child].local.color_words = *p.color_words;
            }
        }
        if (p.matrix_bits) {
            node.local.flags |= 2U;
            node.local.matrix_bits = *p.matrix_bits;
            if (timeline_child != None) {
                instances[timeline_child].local.flags |= 2U;
                instances[timeline_child].local.matrix_bits = *p.matrix_bits;
            }
        }
        if (kept && timeline_child != None) {
            auto& local = instances[timeline_child].local;
            if ((overrides & 0x20U) != 0)
                local.color_words[3] = backup.color_words[3];
            if ((overrides & 8U) != 0)
                local.matrix_bits[4] = backup.matrix_bits[4];
            if ((overrides & 0x10U) != 0)
                local.matrix_bits[5] = backup.matrix_bits[5];
            if ((overrides & 0x140U) != 0) {
                local.matrix_bits[0] = backup.matrix_bits[0];
                local.matrix_bits[2] = backup.matrix_bits[2];
            }
            if ((overrides & 0x180U) != 0) {
                local.matrix_bits[1] = backup.matrix_bits[1];
                local.matrix_bits[3] = backup.matrix_bits[3];
            }
        }
        propagate_node(n);
        if (p.ratio)
            node.ratio = *p.ratio;
        if ((p.flags & 0x20U) != 0) {
            node.name.fill('\0'); // strncpy pads short source names.
            std::copy_n(p.name.begin(),
                        std::min(p.name.size(), std::size_t{31}),
                        node.name.begin());
        }
        if (created)
            ++instances[owner].balance;
        return VmfPlaceReport{created ? VmfPlaceDisposition::created_node
                                      : VmfPlaceDisposition::reused_node,
                              created, node_handle(n)};
    }
    VmfInstanceSnapshot instance_snapshot(Index i) const noexcept {
        const auto& item = instances[i];
        return {handle(i),
                item.kind,
                item.character,
                item.parent == None ? std::nullopt
                                    : std::optional(handle(item.parent)),
                item.node == None ? std::nullopt
                                  : std::optional(node_handle(item.node)),
                item.depth,
                item.birth,
                item.scope,
                item.cached,
                item.frames,
                item.play,
                item.flags,
                item.balance,
                item.node_count,
                item.cache_count,
                item.local,
                item.composed};
    }
    VmfNodeSnapshot node_snapshot(Index i) const noexcept {
        const auto& n = nodes[i];
        return {node_handle(i),
                handle(n.owner),
                n.character,
                n.depth,
                n.clip,
                n.ratio,
                n.flags,
                n.name,
                n.child == None ? std::nullopt : std::optional(handle(n.child)),
                n.local,
                n.composed};
    }
};

VmfDisplayRuntime::VmfDisplayRuntime(std::unique_ptr<Storage> s) noexcept
    : storage_(std::move(s)) {}
VmfDisplayRuntime::~VmfDisplayRuntime() { (void)close(); }

Result<std::unique_ptr<VmfDisplayRuntime>>
VmfDisplayRuntime::create(VmfProgramPtr program,
                          VmfDisplayRuntimeLimits limits) noexcept {
    constexpr auto MaxSlots = std::uint64_t{None} - 1;
    if (!program || limits.max_timelines == 0 ||
        limits.max_timelines > MaxSlots || limits.max_buttons > MaxSlots ||
        limits.max_timelines + std::uint64_t{limits.max_buttons} > MaxSlots ||
        limits.max_nodes > MaxSlots || limits.max_parent_depth == 0 ||
        limits.max_parent_depth > 256)
        return reject(Code::invalid_limits, "VMF display limits/program");
    const auto total = limits.max_timelines + limits.max_buttons;
    auto remaining = limits.max_storage_bytes;
    const auto charge = [&remaining](std::size_t count, std::size_t size) {
        if (count > remaining / size)
            return false;
        remaining -= count * size;
        return true;
    };
    if (!charge(1, sizeof(Storage) + sizeof(VmfDisplayRuntime)) ||
        !charge(total, sizeof(Storage::Instance)) ||
        !charge(limits.max_nodes, sizeof(Storage::Node)))
        return reject(Code::resource_limit, "VMF display storage limit");
    try {
        auto s = std::make_unique<Storage>();
        s->program = std::move(program);
        s->limits = limits;
        s->instances.resize(total);
        s->nodes.resize(limits.max_nodes);
        for (std::size_t k = total; k != 0; --k) {
            const auto i = static_cast<Index>(k - 1);
            auto& item = s->instances[i];
            item.kind = i < limits.max_timelines ? VmfInstanceKind::timeline
                                                 : VmfInstanceKind::button;
            const auto which = pool(item.kind);
            item.free_next = s->free_instance[which];
            s->free_instance[which] = i;
            ++s->free_counts[which];
        }
        for (std::size_t k = s->nodes.size(); k != 0; --k) {
            const auto i = static_cast<Index>(k - 1);
            s->nodes[i].free_next = s->free_node;
            s->free_node = i;
        }
        auto result = std::unique_ptr<VmfDisplayRuntime>(
            new VmfDisplayRuntime(std::move(s)));
        auto context = reserve_context();
        if (!context)
            return reject(Code::resource_limit,
                          "VMF context identity exhausted");
        result->storage_->context = *context;
        result->storage_->root_index = result->storage_->construct(
            VmfInstanceKind::timeline, 0, None, None);
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Code::allocation_failure, "VMF display allocation");
    } catch (const std::length_error&) {
        return reject(Code::resource_limit, "VMF display container limit");
    }
}

Result<std::unique_ptr<VmfDisplayRuntime>>
VmfDisplayRuntime::create_with_variables(
    VmfProgramPtr program, VmfVariableLimits variable_limits,
    VmfDisplayRuntimeLimits limits) noexcept {
    if (limits.max_timelines > limits.max_storage_bytes / sizeof(std::uint32_t))
        return reject(Code::resource_limit, "VMF cleanup plan storage");
    auto reduced = limits;
    reduced.max_storage_bytes -= limits.max_timelines * sizeof(std::uint32_t);
    auto result = create(std::move(program), reduced);
    if (!result)
        return std::unexpected(result.error());
    auto dictionary = VmfVariables::create(variable_limits);
    if (!dictionary)
        return variable_reject(dictionary.error());
    try {
        auto &s = *(*result)->storage_;
        s.cleanup_scopes.resize(limits.max_timelines);
        s.dictionary = std::move(*dictionary);
        s.limits = limits;
        return result;
    } catch (const std::bad_alloc &) {
        return reject(Code::allocation_failure, "VMF cleanup plan allocation");
    } catch (const std::length_error &) {
        return reject(Code::resource_limit, "VMF cleanup plan size");
    }
}

Result<std::unique_ptr<VmfDisplayRuntime>>
VmfDisplayRuntime::create_with_text_bindings(
    VmfProgramPtr program, VmfTextBindingLimits text_limits,
    VmfVariableLimits variable_limits,
    VmfDisplayRuntimeLimits limits) noexcept {
    if (!program || !program->text_data())
        return reject(Code::invalid_record, "VMF decoded text data required");
    if (program->movie().header().profile != assets::VmfProfile::pc_2_8)
        return reject(Code::needs_services, "VMF donor text input profile");
    if (text_limits.max_scratch_bytes == 0)
        return reject(Code::invalid_limits, "VMF text scratch needs NUL space");
    auto reduced = limits;
    const auto fields = program->text_data()->fields();
    if (fields.size() >
        reduced.max_storage_bytes / sizeof(VmfTextResourceSnapshot))
        return reject(Code::resource_limit, "VMF text cache storage");
    reduced.max_storage_bytes -=
        fields.size() * sizeof(VmfTextResourceSnapshot);
    if (text_limits.max_scratch_bytes > reduced.max_storage_bytes / 2)
        return reject(Code::resource_limit, "VMF text scratch storage");
    reduced.max_storage_bytes -= 2 * text_limits.max_scratch_bytes;
    auto result =
        create_with_variables(std::move(program), variable_limits, reduced);
    if (!result)
        return std::unexpected(result.error());
    try {
        auto &s = *(*result)->storage_;
        s.text_resources.resize(fields.size());
        for (std::size_t i = 0; i != fields.size(); ++i) {
            auto &state = s.text_resources[i];
            state.character_id = fields[i].character_id;
            state.selector_2f = text_limits.context_byte_2c != 0 &&
                                        (fields[i].raw_flags_2e & 8U) != 0
                                    ? 1U
                                    : 0U;
        }
        for (auto &buffer : s.text_scratch)
            buffer.resize(text_limits.max_scratch_bytes);
        s.text_limits = text_limits;
        s.text_bindings = true;
        s.limits = limits;
        return result;
    } catch (const std::bad_alloc &) {
        return reject(Code::allocation_failure, "VMF text binding allocation");
    } catch (const std::length_error &) {
        return reject(Code::resource_limit, "VMF text binding size");
    }
}

VmfInstanceHandle VmfDisplayRuntime::root() const noexcept {
    return storage_->root_index == None
               ? VmfInstanceHandle{}
               : storage_->handle(storage_->root_index);
}
VmfProgramPtr VmfDisplayRuntime::program() const noexcept {
    return storage_->program;
}
const VmfVariables *VmfDisplayRuntime::variable_dictionary() const noexcept {
    return storage_->dictionary.get();
}
Result<VmfTextResourceSnapshot> VmfDisplayRuntime::inspect_text_resource(
    std::uint32_t character) const noexcept {
    const auto &s = *storage_;
    if (s.root_index == None)
        return reject(Code::closed, "VMF display context closed");
    if (!s.text_bindings)
        return reject(Code::needs_services, "VMF text binding backend");
    const auto index = s.program->text_data()->field_index(character);
    if (!index)
        return reject(Code::invalid_record, "VMF text resource identity");
    return s.text_resources[*index];
}
Result<std::size_t> VmfDisplayRuntime::copy_text_scratch(
    std::span<char> destination) const noexcept {
    const auto &s = *storage_;
    if (s.root_index == None)
        return reject(Code::closed, "VMF display context closed");
    if (!s.text_bindings)
        return reject(Code::needs_services, "VMF text binding backend");
    if (s.scratch_length >= destination.size())
        return reject(Code::resource_limit, "VMF text scratch destination");
    std::copy_n(s.text_scratch[s.active_scratch].begin(), s.scratch_length + 1,
                destination.begin());
    return s.scratch_length;
}
Result<void> VmfDisplayRuntime::set_variable_number_bits(
    VmfInstanceHandle target, std::optional<std::string_view> name,
    std::uint32_t bits) noexcept {
    return storage_->write_variable(
        target, name,
        [bits](VmfVariables &vars, std::uint32_t scope, std::string_view key) {
            return vars.set_number_bits(scope, key, bits);
        });
}
Result<void>
VmfDisplayRuntime::set_variable_text(VmfInstanceHandle target,
                                     std::optional<std::string_view> name,
                                     std::string_view text) noexcept {
    return storage_->write_variable(
        target, name,
        [text](VmfVariables &vars, std::uint32_t scope, std::string_view key) {
            return vars.set_text(scope, key, text);
        });
}
Result<void>
VmfDisplayRuntime::set_variable_linked(VmfInstanceHandle target,
                                       std::optional<std::string_view> name,
                                       const VmfTextReference &text) noexcept {
    return storage_->write_variable(
        target, name,
        [&text](VmfVariables &vars, std::uint32_t scope, std::string_view key) {
            return vars.set_linked(scope, key, text);
        });
}
Result<void>
VmfDisplayRuntime::apply_timeline_action(VmfInstanceHandle target,
                                         VmfTimelineAction action,
                                         std::uint16_t frame_index) noexcept {
    auto index = storage_->timeline(target);
    if (!index)
        return std::unexpected(index.error());
    auto& item = storage_->instances[*index];
    std::int32_t requested{};
    switch (action) {
    case VmfTimelineAction::play:
        item.play = 1;
        return {};
    case VmfTimelineAction::stop:
        item.play = 2;
        return {};
    case VmfTimelineAction::next_frame:
        requested = item.frames.current + 1;
        break;
    case VmfTimelineAction::previous_frame:
        requested = item.frames.current - 1;
        break;
    case VmfTimelineAction::goto_frame:
        // The native WORD is widened BEFORE adding 1 (FFFF -> 65536).
        requested = std::int32_t{frame_index} + 1;
        break;
    default:
        return reject(Code::invalid_driver_state, "VMF timeline action");
    }
    // Previous at frame 1 leaves even an older pending request untouched.
    // Driver bounds guarantee that the arithmetic above cannot overflow.
    if (requested > 0) {
        requested = std::min(
            requested, std::int32_t{storage_->sprite(*index).frame_count});
        item.frames.requested = requested;
        item.frames.request_pending = requested != item.frames.current;
    }
    if (action == VmfTimelineAction::goto_frame)
        item.play = 2;
    return {};
}
Result<VmfTargetResolution>
VmfDisplayRuntime::resolve_target(VmfInstanceHandle origin,
                                  std::string_view path,
                                  VmfTargetLookupLimits limits) const noexcept {
    const auto index = storage_->timeline(origin);
    if (!index)
        return std::unexpected(index.error());
    // Inspect at most max_path_bytes + 1 bytes, without overflowing that sum.
    // A NUL at the limit is valid; an inaccessible view end is a logical NUL.
    std::size_t length = 0;
    while (length < path.size() && path[length] != '\0') {
        if (length == limits.max_path_bytes)
            return reject(Code::resource_limit, "VMF target path byte limit");
        ++length;
    }
    path = path.substr(0, length);
    std::size_t visits = 0;
    const auto visit = [&]() {
        if (visits == limits.max_visits)
            return false;
        ++visits;
        return true;
    };
    auto current = *index;
    std::size_t position = 0;
    if (!path.empty() && path[0] == '/') {
        while (storage_->instances[current].parent != None) {
            if (!visit())
                return reject(Code::resource_limit, "VMF target visit limit");
            current = storage_->instances[current].parent;
        }
        ++position;
    }
    for (;;) {
        if (position == path.size() || path[position] == ':')
            return VmfTargetResolution{true, storage_->handle(current)};
        if (path.size() - position >= 2 && path[position] == '.' &&
            path[position + 1] == '.') {
            if (!visit())
                return reject(Code::resource_limit, "VMF target visit limit");
            current = storage_->instances[current].parent;
            position += 2;
        } else {
            auto child = None;
            for (auto n = storage_->instances[current].head; n != None;
                 n = storage_->nodes[n].next) {
                if (!visit())
                    return reject(Code::resource_limit,
                                  "VMF target visit limit");
                const auto& node = storage_->nodes[n];
                if (node.child == None ||
                    storage_->instances[node.child].kind !=
                        VmfInstanceKind::timeline)
                    continue;
                auto input = position;
                bool matches = true;
                for (const auto stored : node.name) {
                    if (stored == '\0')
                        break;
                    const auto query =
                        input == path.size()
                            ? 0U
                            : static_cast<unsigned char>(path[input]);
                    if (((query ^ static_cast<unsigned char>(stored)) &
                         0xdfU) != 0) {
                        matches = false;
                        break;
                    }
                    // DF matching can treat stored space as query NUL. The
                    // following native read would leave the C-string domain.
                    if (input == path.size())
                        return reject(Code::invalid_native_state,
                                      "VMF target comparison beyond NUL");
                    ++input;
                }
                if (matches && (input == path.size() || path[input] == ':' ||
                                path[input] == '/')) {
                    child = node.child;
                    position = input;
                    break;
                }
            }
            current = child;
        }
        if (current == None)
            return VmfTargetResolution{false, std::nullopt};
        if (position < path.size() && path[position] == '/')
            ++position;
    }
}

Result<std::optional<std::uint32_t>>
VmfDisplayRuntime::find_label(VmfInstanceHandle target,
                              std::string_view label) const noexcept {
    const auto index = storage_->timeline(target);
    if (!index)
        return std::unexpected(index.error());
    const auto sprite = *storage_->program->sprite_for_character(
        storage_->instances[*index].character);
    const auto bounded_text = [](std::string_view text) {
        std::array<char, 64> result{};
        const auto count = std::min(text.size(), std::size_t{63});
        if (count != 0)
            std::copy_n(text.data(), count, result.data());
        return result;
    };
    const auto query = bounded_text(label);
    const auto& movie = storage_->program->movie();
    // Parsed labels retain record order and immediate sprite ownership;
    // nested definition bodies are therefore excluded as in the native scan.
    for (const auto& candidate : movie.labels()) {
        if (candidate.parent_sprite_index != sprite)
            continue;
        const auto bytes = movie.bytes(candidate.text);
        if (!bytes)
            return reject(Code::invalid_record, "VMF label text range");
        const auto name = bounded_text(
            {reinterpret_cast<const char*>(bytes->data()), bytes->size()});
        // Preserve the calling CRT's LC_CTYPE. This establishes no original
        // game locale and makes no claim about Unicode or MSVCR71 high bytes.
        if (_strnicmp(name.data(), query.data(), 63) == 0)
            return std::optional<std::uint32_t>{
                std::uint32_t{candidate.frame_index} + 1U};
    }
    return std::optional<std::uint32_t>{};
}

Result<bool>
VmfDisplayRuntime::apply_goto_label(VmfInstanceHandle target,
                                    std::string_view label) noexcept {
    const auto frame = apply_host_goto_label(target, label, 0);
    if (!frame)
        return std::unexpected(frame.error());
    return frame->has_value();
}

Result<std::optional<std::uint32_t>>
VmfDisplayRuntime::apply_host_goto_label(VmfInstanceHandle target,
                                         std::string_view label,
                                         std::uint8_t play) noexcept {
    const auto frame = find_label(target, label);
    if (!frame)
        return std::unexpected(frame.error());
    // find_label validated this handle; no callbacks can invalidate it here.
    auto& item = storage_->instances[target.slot];
    if (*frame) {
        const auto requested = static_cast<std::int32_t>(std::min(
            **frame, std::uint32_t{storage_->sprite(target.slot).frame_count}));
        item.frames.requested = requested;
        item.frames.request_pending = requested != item.frames.current;
    }
    item.play = play != 0 ? 1U : 2U;
    return *frame;
}

Result<void>
VmfDisplayRuntime::set_driver_frames(VmfInstanceHandle owner,
                                     VmfDriverFrames frames) noexcept {
    auto i = storage_->timeline(owner);
    if (!i)
        return std::unexpected(i.error());
    const auto count = storage_->sprite(*i).frame_count;
    if (frames.current < 1 || frames.current > std::int32_t{count} + 1 ||
        frames.requested < 1 || frames.requested > count)
        return reject(Code::invalid_driver_state, "VMF driver frame range");
    storage_->instances[*i].frames = frames;
    return {};
}
Result<void> VmfDisplayRuntime::set_driver_properties(
    VmfInstanceHandle owner, VmfTransform local, std::uint32_t flags) noexcept {
    auto i = storage_->timeline(owner);
    if (!i)
        return std::unexpected(i.error());
    if ((flags & 0x200U) != 0 && !storage_->dictionary)
        return reject(Code::needs_services, "VMF variable cleanup service");
    // Transform/override publication must not forget a registered scope.
    // Native property writers OR their override bits; cleanup remains armed.
    if (storage_->dictionary)
        flags |= storage_->instances[*i].flags & 0x200U;
    storage_->instances[*i].local = local;
    storage_->instances[*i].flags = flags;
    storage_->propagate(*i);
    return {};
}
Result<void> VmfDisplayRuntime::set_property(VmfInstanceHandle target,
                                             std::uint32_t index,
                                             std::string_view text) noexcept {
    const auto i = storage_->timeline(target);
    if (!i)
        return std::unexpected(i.error());
    auto &item = storage_->instances[*i];
    if (index == 7 || index == 16 || index == 17) {
        const auto mask = index == 7 ? 1U : index == 16 ? 2U : 4U;
        item.flags = !text.empty() && text.front() == '1' ? item.flags | mask
                                                          : item.flags & ~mask;
        return {}; // No448940: even composed-transform flags stay untouched.
    }
    if (index != 0 && index != 1 && index != 2 && index != 3 && index != 6 &&
        index != 10 && index != 18)
        return {}; // Including13: the disabled diagnostic has no state effect.

    const auto value = vmf_number_from_string(text);
    if (index == 18 || item.parent == None)
        return {}; // 18 still converts; geometric writes guard parent AFTER it.
    const auto value_bits = std::bit_cast<std::uint32_t>(value);
    switch (index) {
    case 0:
    case 1:
        item.local.flags |= 2U;
        item.local.matrix_bits[index == 0 ? 4 : 5] =
            detail::multiply_vmf_transform_bits(value_bits, 0x41a00000U);
        item.flags |= index == 0 ? 8U : 0x10U;
        break;
    case 2:
    case 3: {
        item.local.flags |= 2U;
        const auto scale = std::bit_cast<float>(
            detail::multiply_vmf_transform_bits(value_bits, 0x3c23d70aU));
        if (index == 2)
            detail::set_vmf_property_x_scale(item.local, scale);
        else
            detail::set_vmf_property_y_scale(item.local, scale);
        item.flags |= index == 2 ? 0x40U : 0x80U;
        break;
    }
    case 6: {
        const auto scaled = detail::multiply_vmf_transform_bits(
            detail::multiply_vmf_transform_bits(value_bits, 0x43800000U),
            0x3c23d70aU);
        const auto alpha =
            _mm_cvttss_si32(_mm_set_ss(std::bit_cast<float>(scaled)));
        item.local.color_words[3] = static_cast<std::uint16_t>(alpha);
        item.local.flags |= 1U;
        item.flags |= 0x20U;
        break;
    }
    case 10:
        item.local.flags |= 2U;
        detail::set_vmf_property_rotation(item.local, value);
        item.flags |= 0x100U;
        break;
    default:
        break;
    }
    storage_->propagate(*i);
    return {};
}
Result<VmfPlaceReport>
VmfDisplayRuntime::place(VmfInstanceHandle owner,
                         std::size_t placement) noexcept {
    auto i = storage_->timeline(owner);
    if (!i)
        return std::unexpected(i.error());
    const auto entries = storage_->program->display().placements();
    if (placement >= entries.size() ||
        !storage_->owns(*i, entries[placement].location))
        return reject(Code::invalid_record, "VMF placement owner/index");
    return storage_->place(*this, *i, entries[placement]);
}
Result<bool> VmfDisplayRuntime::remove(VmfInstanceHandle owner,
                                       std::size_t removal) noexcept {
    auto i = storage_->timeline(owner);
    if (!i)
        return std::unexpected(i.error());
    const auto entries = storage_->program->display().removals();
    if (removal >= entries.size() ||
        !storage_->owns(*i, entries[removal].location))
        return reject(Code::invalid_record, "VMF removal owner/index");
    auto& s = *storage_;
    const auto depth = entries[removal].depth;
    const auto n = s.find_node(*i, depth);
    if (n == None) {
        --s.instances[*i].balance;
        return false;
    }
    const auto cached = s.find_cached(*i, depth);
    const auto active = s.nodes[n].child;
    if (cached != None && active != None)
        return reject(Code::invalid_native_state, "VMF duplicate child depth");
    // 4483B0 calls44A440 only when the text resource's bit2E&10 is clear.
    // The explicit binding backend covers the other subset, with no input
    // registration to remove. Legacy and editable-text boundaries remain.
    if ((s.resource_type(s.nodes[n].character) == 37 &&
         !s.text_ready(s.nodes[n].character)) ||
        (cached != None && s.needs_services(cached)) ||
        (active != None && s.needs_services(active)))
        return reject(Code::needs_services, "VMF removal services");
    s.cleanup_count = 0;
    for (const auto child : {cached, active}) {
        if (child != None &&
            s.instances[child].birth < s.instances[*i].frames.current) {
            const auto planned = s.plan_destroy(child);
            if (!planned)
                return std::unexpected(planned.error());
        }
    }
    const auto cleaned = s.commit_cleanup();
    if (!cleaned)
        return std::unexpected(cleaned.error());
    (void)s.extract_cached(*i, depth);
    s.nodes[n].child = None; // 446FD0 clears before destruction callbacks.
    for (const auto child : {cached, active}) {
        if (child == None)
            continue;
        if (s.instances[child].birth < s.instances[*i].frames.current)
            s.destroy(child);
        else
            s.cache(*i, child);
    }
    s.release_node(n);
    --s.instances[*i].balance;
    return true;
}
Result<void> VmfDisplayRuntime::rewind(VmfInstanceHandle owner) noexcept {
    auto index = storage_->timeline(owner);
    if (!index)
        return std::unexpected(index.error());
    auto& s = *storage_;
    const auto i = *index;
    if (s.needs_services(i))
        return reject(Code::needs_services, "VMF rewind cleanup services");
    auto& item = s.instances[i];
    s.cleanup_count = 0;
    for (auto c = item.cache_head; c != None; c = s.instances[c].cache_next) {
        if (s.instances[c].birth > item.frames.requested) {
            const auto planned = s.plan_destroy(c);
            if (!planned)
                return planned;
        }
    }
    for (auto n = item.head; n != None; n = s.nodes[n].next) {
        const auto child = s.nodes[n].child;
        if (child != None && s.instances[child].birth > item.frames.requested) {
            const auto planned = s.plan_destroy(child);
            if (!planned)
                return planned;
        }
    }
    const auto cleaned = s.commit_cleanup();
    if (!cleaned)
        return cleaned;
    for (auto c = item.cache_head; c != None;) {
        if (s.instances[c].birth > item.frames.requested) {
            (void)s.extract_cached(
                i, static_cast<std::uint16_t>(s.instances[c].depth));
            s.destroy(c);
            c = item.cache_head; // Native restart after each destruction.
        } else {
            c = s.instances[c].cache_next;
        }
    }
    while (item.head != None) {
        const auto n = item.head;
        const auto child = s.nodes[n].child;
        if (child != None) {
            if (s.instances[child].birth > item.frames.requested)
                s.destroy(child);
            else
                s.cache(i, child);
            s.nodes[n].child = None;
        }
        s.release_node(n);
    }
    return {};
}
Result<void> VmfDisplayRuntime::close() noexcept {
    if (storage_->root_index == None)
        return {};
    if (storage_->needs_services(storage_->root_index))
        return reject(Code::needs_services, "VMF context cleanup services");
    storage_->cleanup_count = 0;
    const auto planned = storage_->plan_destroy(storage_->root_index);
    if (!planned)
        return planned;
    const auto cleaned = storage_->commit_cleanup();
    if (!cleaned)
        return cleaned;
    storage_->destroy(storage_->root_index);
    storage_->root_index = None;
    return {};
}
Result<VmfInstanceSnapshot>
VmfDisplayRuntime::inspect_instance(VmfInstanceHandle h) const noexcept {
    auto i = storage_->resolve(h);
    if (!i)
        return std::unexpected(i.error());
    return storage_->instance_snapshot(*i);
}
Result<std::optional<VmfNodeHandle>>
VmfDisplayRuntime::playback_head(VmfInstanceHandle owner) const noexcept {
    const auto i = storage_->timeline(owner);
    if (!i)
        return std::unexpected(i.error());
    const auto head = storage_->instances[*i].head;
    return head == None ? std::nullopt
                        : std::optional(storage_->node_handle(head));
}
Result<std::optional<VmfNodeHandle>>
VmfDisplayRuntime::playback_next(VmfNodeHandle node) const noexcept {
    const auto i = storage_->resolve(node);
    if (!i)
        return std::unexpected(i.error());
    const auto next = storage_->nodes[*i].next;
    return next == None ? std::nullopt
                        : std::optional(storage_->node_handle(next));
}
Result<void>
VmfDisplayRuntime::playback_reset_balance(VmfInstanceHandle owner) noexcept {
    const auto i = storage_->timeline(owner);
    if (!i)
        return std::unexpected(i.error());
    storage_->instances[*i].balance = 0;
    return {};
}
Result<VmfNodeSnapshot>
VmfDisplayRuntime::inspect_node(VmfNodeHandle h) const noexcept {
    auto i = storage_->resolve(h);
    if (!i)
        return std::unexpected(i.error());
    return storage_->node_snapshot(*i);
}
Result<VmfDisplaySnapshot>
VmfDisplayRuntime::snapshot(std::size_t max_output_bytes) const noexcept {
    const auto& s = *storage_;
    if (s.root_index == None)
        return reject(Code::closed, "VMF display context closed");
    auto remaining = max_output_bytes;
    const auto charge = [&remaining](std::size_t count, std::size_t size) {
        if (count > remaining / size)
            return false;
        remaining -= count * size;
        return true;
    };
    if (!charge(1, sizeof(VmfDisplaySnapshot)) ||
        !charge(s.live_instances, sizeof(VmfInstanceSnapshot)) ||
        !charge(s.live_nodes, sizeof(VmfNodeSnapshot)))
        return reject(Code::resource_limit, "VMF snapshot storage limit");
    try {
        VmfDisplaySnapshot result;
        result.program = s.program;
        result.instances.reserve(s.live_instances);
        result.nodes.reserve(s.live_nodes);
        for (std::size_t k = 0; k < s.instances.size(); ++k) {
            const auto i = static_cast<Index>(k);
            if (!s.instances[i].live)
                continue;
            result.instances.push_back(s.instance_snapshot(i));
            for (auto n = s.instances[i].head; n != None; n = s.nodes[n].next)
                result.nodes.push_back(s.node_snapshot(n));
        }
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Code::allocation_failure, "VMF snapshot allocation");
    } catch (const std::length_error&) {
        return reject(Code::resource_limit, "VMF snapshot container limit");
    }
}

} // namespace tdur::ui
