#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <utility>

namespace engine {

/// Adaptive Radix Tree (ART) mapping int64_t keys to values of type V.
///
/// Design:
///   - 8-byte keys (int64_t) encoded as big-endian for correct lexicographic ordering.
///   - 4 node types: Node4, Node16, Node48, Node256 with lazy expansion.
///   - Path compression to skip shared prefixes.
///   - Node shrinking on remove.
///   - Single-threaded (no synchronization), suitable for per-partition order book.
template <typename V>
class AdaptiveRadixTree {
public:
    AdaptiveRadixTree() = default;
    ~AdaptiveRadixTree() { destroy(root_); }

    // Non-copyable, movable
    AdaptiveRadixTree(const AdaptiveRadixTree&) = delete;
    AdaptiveRadixTree& operator=(const AdaptiveRadixTree&) = delete;

    AdaptiveRadixTree(AdaptiveRadixTree&& other) noexcept
        : root_(other.root_), size_(other.size_) {
        other.root_ = nullptr;
        other.size_ = 0;
    }

    AdaptiveRadixTree& operator=(AdaptiveRadixTree&& other) noexcept {
        if (this != &other) {
            destroy(root_);
            root_ = other.root_;
            size_ = other.size_;
            other.root_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    /// Insert key-value pair. Returns reference to value (creates default if not exists).
    V& insert(int64_t key) {
        uint8_t k[8];
        encode_key(key, k);
        auto* leaf = insert_recursive(root_, k, 0);
        return leaf->value;
    }

    /// Remove key. Returns true if found and removed.
    bool remove(int64_t key) {
        uint8_t k[8];
        encode_key(key, k);
        return remove_recursive(root_, k, 0);
    }

    /// Find value by key. Returns nullptr if not found.
    V* find(int64_t key) {
        uint8_t k[8];
        encode_key(key, k);
        auto* leaf = find_recursive(root_, k, 0);
        return leaf ? &leaf->value : nullptr;
    }

    const V* find(int64_t key) const {
        uint8_t k[8];
        encode_key(key, k);
        auto* leaf = find_recursive(root_, k, 0);
        return leaf ? &leaf->value : nullptr;
    }

    /// Min key value. Returns nullptr if empty.
    V* min() {
        auto* leaf = min_leaf(root_);
        return leaf ? &leaf->value : nullptr;
    }

    /// Max key value. Returns nullptr if empty.
    V* max() {
        auto* leaf = max_leaf(root_);
        return leaf ? &leaf->value : nullptr;
    }

    /// Ordered ascending iteration.
    template <typename Func>
    void for_each_ascending(Func&& func) const {
        for_each_asc(root_, std::forward<Func>(func));
    }

    /// Ordered descending iteration.
    template <typename Func>
    void for_each_descending(Func&& func) const {
        for_each_desc(root_, std::forward<Func>(func));
    }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_t size() const noexcept { return size_; }

private:
    // ─── Key encoding ──────────────────────────────────────────
    // Encode int64_t as big-endian with sign bit flipped so that
    // lexicographic byte ordering matches numeric ordering.
    static void encode_key(int64_t key, uint8_t out[8]) noexcept {
        // Flip sign bit so negatives sort before positives
        uint64_t u = static_cast<uint64_t>(key) ^ (uint64_t(1) << 63);
        for (int i = 0; i < 8; ++i) {
            out[i] = static_cast<uint8_t>(u >> (56 - i * 8));
        }
    }

    static int64_t decode_key(const uint8_t k[8]) noexcept {
        uint64_t u = 0;
        for (int i = 0; i < 8; ++i) {
            u = (u << 8) | k[i];
        }
        u ^= (uint64_t(1) << 63);
        return static_cast<int64_t>(u);
    }

    // ─── Node types ────────────────────────────────────────────

    static constexpr uint8_t NODE4   = 0;
    static constexpr uint8_t NODE16  = 1;
    static constexpr uint8_t NODE48  = 2;
    static constexpr uint8_t NODE256 = 3;
    static constexpr uint8_t LEAF    = 4;

    struct Node {
        uint8_t type;
        uint8_t prefix_len;        // Length of compressed path prefix
        uint8_t prefix[8];         // Compressed path prefix (up to 8 bytes)
        uint16_t num_children;

        Node(uint8_t t) : type(t), prefix_len(0), num_children(0) {
            std::memset(prefix, 0, sizeof(prefix));
        }
    };

    struct Leaf {
        uint8_t type;
        uint8_t key[8];
        V value;

        Leaf() : type(LEAF), value{} {
            std::memset(key, 0, sizeof(key));
        }
    };

    struct Node4 : Node {
        uint8_t keys[4];
        Node* children[4];

        Node4() : Node(NODE4) {
            std::memset(keys, 0, sizeof(keys));
            std::memset(children, 0, sizeof(children));
        }
    };

    struct Node16 : Node {
        uint8_t keys[16];
        Node* children[16];

        Node16() : Node(NODE16) {
            std::memset(keys, 0, sizeof(keys));
            std::memset(children, 0, sizeof(children));
        }
    };

    struct Node48 : Node {
        uint8_t child_index[256];  // Maps byte -> slot index (255 = empty)
        Node* children[48];

        Node48() : Node(NODE48) {
            std::memset(child_index, 255, sizeof(child_index));
            std::memset(children, 0, sizeof(children));
        }
    };

    struct Node256 : Node {
        Node* children[256];

        Node256() : Node(NODE256) {
            std::memset(children, 0, sizeof(children));
        }
    };

    // ─── Node helpers ──────────────────────────────────────────

    static bool is_leaf(const Node* n) noexcept {
        return n != nullptr && reinterpret_cast<const Leaf*>(n)->type == LEAF;
    }

    static Leaf* as_leaf(Node* n) noexcept {
        return reinterpret_cast<Leaf*>(n);
    }

    static const Leaf* as_leaf(const Node* n) noexcept {
        return reinterpret_cast<const Leaf*>(n);
    }

    static Leaf* make_leaf(const uint8_t key[8]) {
        auto* l = new Leaf();
        std::memcpy(l->key, key, 8);
        return l;
    }

    static Node** find_child(Node* n, uint8_t byte) {
        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<Node4*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    if (node->keys[i] == byte) return &node->children[i];
                }
                return nullptr;
            }
            case NODE16: {
                auto* node = static_cast<Node16*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    if (node->keys[i] == byte) return &node->children[i];
                }
                return nullptr;
            }
            case NODE48: {
                auto* node = static_cast<Node48*>(n);
                uint8_t idx = node->child_index[byte];
                if (idx != 255) return &node->children[idx];
                return nullptr;
            }
            case NODE256: {
                auto* node = static_cast<Node256*>(n);
                if (node->children[byte] != nullptr) return &node->children[byte];
                return nullptr;
            }
            default:
                return nullptr;
        }
    }

    static const Node* const* find_child_const(const Node* n, uint8_t byte) {
        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<const Node4*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    if (node->keys[i] == byte) return &node->children[i];
                }
                return nullptr;
            }
            case NODE16: {
                auto* node = static_cast<const Node16*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    if (node->keys[i] == byte) return &node->children[i];
                }
                return nullptr;
            }
            case NODE48: {
                auto* node = static_cast<const Node48*>(n);
                uint8_t idx = node->child_index[byte];
                if (idx != 255) return &node->children[idx];
                return nullptr;
            }
            case NODE256: {
                auto* node = static_cast<const Node256*>(n);
                if (node->children[byte] != nullptr) return &node->children[byte];
                return nullptr;
            }
            default:
                return nullptr;
        }
    }

    static void add_child(Node*& ref, uint8_t byte, Node* child) {
        Node* n = ref;
        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<Node4*>(n);
                if (node->num_children < 4) {
                    // Insert sorted
                    uint16_t pos = node->num_children;
                    for (uint16_t i = 0; i < node->num_children; ++i) {
                        if (byte < node->keys[i]) { pos = i; break; }
                    }
                    // Shift elements right
                    for (uint16_t i = node->num_children; i > pos; --i) {
                        node->keys[i] = node->keys[i - 1];
                        node->children[i] = node->children[i - 1];
                    }
                    node->keys[pos] = byte;
                    node->children[pos] = child;
                    node->num_children++;
                } else {
                    // Grow to Node16
                    auto* new_node = new Node16();
                    new_node->prefix_len = node->prefix_len;
                    std::memcpy(new_node->prefix, node->prefix, node->prefix_len);
                    for (uint16_t i = 0; i < 4; ++i) {
                        new_node->keys[i] = node->keys[i];
                        new_node->children[i] = node->children[i];
                    }
                    new_node->num_children = 4;
                    delete node;
                    ref = new_node;
                    add_child(ref, byte, child);
                }
                break;
            }
            case NODE16: {
                auto* node = static_cast<Node16*>(n);
                if (node->num_children < 16) {
                    uint16_t pos = node->num_children;
                    for (uint16_t i = 0; i < node->num_children; ++i) {
                        if (byte < node->keys[i]) { pos = i; break; }
                    }
                    for (uint16_t i = node->num_children; i > pos; --i) {
                        node->keys[i] = node->keys[i - 1];
                        node->children[i] = node->children[i - 1];
                    }
                    node->keys[pos] = byte;
                    node->children[pos] = child;
                    node->num_children++;
                } else {
                    // Grow to Node48
                    auto* new_node = new Node48();
                    new_node->prefix_len = node->prefix_len;
                    std::memcpy(new_node->prefix, node->prefix, node->prefix_len);
                    for (uint16_t i = 0; i < 16; ++i) {
                        new_node->child_index[node->keys[i]] = static_cast<uint8_t>(i);
                        new_node->children[i] = node->children[i];
                    }
                    new_node->num_children = 16;
                    delete node;
                    ref = new_node;
                    add_child(ref, byte, child);
                }
                break;
            }
            case NODE48: {
                auto* node = static_cast<Node48*>(n);
                if (node->num_children < 48) {
                    uint8_t slot = static_cast<uint8_t>(node->num_children);
                    node->children[slot] = child;
                    node->child_index[byte] = slot;
                    node->num_children++;
                } else {
                    // Grow to Node256
                    auto* new_node = new Node256();
                    new_node->prefix_len = node->prefix_len;
                    std::memcpy(new_node->prefix, node->prefix, node->prefix_len);
                    for (int i = 0; i < 256; ++i) {
                        if (node->child_index[i] != 255) {
                            new_node->children[i] = node->children[node->child_index[i]];
                        }
                    }
                    new_node->num_children = 48;
                    delete node;
                    ref = new_node;
                    add_child(ref, byte, child);
                }
                break;
            }
            case NODE256: {
                auto* node = static_cast<Node256*>(n);
                node->children[byte] = child;
                node->num_children++;
                break;
            }
        }
    }

    static void remove_child(Node*& ref, uint8_t byte) {
        Node* n = ref;
        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<Node4*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    if (node->keys[i] == byte) {
                        // Shift left
                        for (uint16_t j = i; j + 1 < node->num_children; ++j) {
                            node->keys[j] = node->keys[j + 1];
                            node->children[j] = node->children[j + 1];
                        }
                        node->num_children--;
                        // If only one child left and it's not a leaf, collapse
                        if (node->num_children == 1 && !is_leaf(node->children[0])) {
                            Node* child = node->children[0];
                            uint8_t child_byte = node->keys[0];
                            // Prepend this node's prefix + child_byte to child's prefix
                            uint8_t new_prefix[16]; // Temporary
                            uint8_t new_len = 0;
                            std::memcpy(new_prefix + new_len, node->prefix, node->prefix_len);
                            new_len += node->prefix_len;
                            new_prefix[new_len++] = child_byte;
                            std::memcpy(new_prefix + new_len, child->prefix, child->prefix_len);
                            new_len += child->prefix_len;
                            // Copy back (max 8 bytes stored)
                            child->prefix_len = (new_len <= 8) ? new_len : 8;
                            std::memcpy(child->prefix, new_prefix, child->prefix_len);
                            delete node;
                            ref = child;
                        }
                        return;
                    }
                }
                break;
            }
            case NODE16: {
                auto* node = static_cast<Node16*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    if (node->keys[i] == byte) {
                        for (uint16_t j = i; j + 1 < node->num_children; ++j) {
                            node->keys[j] = node->keys[j + 1];
                            node->children[j] = node->children[j + 1];
                        }
                        node->num_children--;
                        // Shrink to Node4 if 4 or fewer children
                        if (node->num_children <= 4) {
                            auto* new_node = new Node4();
                            new_node->prefix_len = node->prefix_len;
                            std::memcpy(new_node->prefix, node->prefix, node->prefix_len);
                            for (uint16_t j = 0; j < node->num_children; ++j) {
                                new_node->keys[j] = node->keys[j];
                                new_node->children[j] = node->children[j];
                            }
                            new_node->num_children = node->num_children;
                            delete node;
                            ref = new_node;
                        }
                        return;
                    }
                }
                break;
            }
            case NODE48: {
                auto* node = static_cast<Node48*>(n);
                uint8_t slot = node->child_index[byte];
                if (slot != 255) {
                    node->children[slot] = nullptr;
                    node->child_index[byte] = 255;
                    node->num_children--;
                    // Shrink to Node16 if 16 or fewer children
                    if (node->num_children <= 16) {
                        auto* new_node = new Node16();
                        new_node->prefix_len = node->prefix_len;
                        std::memcpy(new_node->prefix, node->prefix, node->prefix_len);
                        uint16_t idx = 0;
                        for (int i = 0; i < 256; ++i) {
                            if (node->child_index[i] != 255) {
                                new_node->keys[idx] = static_cast<uint8_t>(i);
                                new_node->children[idx] = node->children[node->child_index[i]];
                                idx++;
                            }
                        }
                        new_node->num_children = idx;
                        delete node;
                        ref = new_node;
                    }
                }
                break;
            }
            case NODE256: {
                auto* node = static_cast<Node256*>(n);
                node->children[byte] = nullptr;
                node->num_children--;
                // Shrink to Node48 if 48 or fewer children
                if (node->num_children <= 48) {
                    auto* new_node = new Node48();
                    new_node->prefix_len = node->prefix_len;
                    std::memcpy(new_node->prefix, node->prefix, node->prefix_len);
                    uint8_t slot = 0;
                    for (int i = 0; i < 256; ++i) {
                        if (node->children[i] != nullptr) {
                            new_node->child_index[i] = slot;
                            new_node->children[slot] = node->children[i];
                            slot++;
                        }
                    }
                    new_node->num_children = slot;
                    delete node;
                    ref = new_node;
                }
                break;
            }
        }
    }

    // ─── Prefix handling ───────────────────────────────────────

    static uint8_t check_prefix(const Node* n, const uint8_t key[8], int depth) {
        uint8_t match = 0;
        uint8_t max_cmp = (n->prefix_len < static_cast<uint8_t>(8 - depth))
                              ? n->prefix_len
                              : static_cast<uint8_t>(8 - depth);
        for (uint8_t i = 0; i < max_cmp; ++i) {
            if (n->prefix[i] != key[depth + i]) break;
            match++;
        }
        return match;
    }

    // ─── Recursive operations ──────────────────────────────────

    Leaf* insert_recursive(Node*& ref, const uint8_t key[8], int depth) {
        if (ref == nullptr) {
            auto* leaf = make_leaf(key);
            ref = reinterpret_cast<Node*>(leaf);
            size_++;
            return leaf;
        }

        if (is_leaf(ref)) {
            Leaf* existing = as_leaf(ref);
            // Check for duplicate key
            if (std::memcmp(existing->key, key, 8) == 0) {
                return existing;  // Already exists
            }

            // Need to split: create new inner node
            auto* new_node = new Node4();
            // Determine shared prefix length from depth
            uint8_t shared = 0;
            while (depth + shared < 8 && existing->key[depth + shared] == key[depth + shared]) {
                shared++;
            }
            new_node->prefix_len = shared;
            std::memcpy(new_node->prefix, key + depth, shared);

            // Add existing leaf
            uint8_t existing_byte = existing->key[depth + shared];
            add_child(reinterpret_cast<Node*&>(new_node), existing_byte, reinterpret_cast<Node*>(existing));

            // Add new leaf
            auto* new_leaf = make_leaf(key);
            uint8_t new_byte = key[depth + shared];
            // We need ref to point to new_node first
            ref = reinterpret_cast<Node*>(new_node);
            add_child(ref, new_byte, reinterpret_cast<Node*>(new_leaf));
            size_++;
            return new_leaf;
        }

        Node* n = ref;
        // Check prefix
        uint8_t prefix_match = check_prefix(n, key, depth);
        if (prefix_match < n->prefix_len) {
            // Prefix mismatch: need to split at this point
            auto* new_node = new Node4();
            new_node->prefix_len = prefix_match;
            std::memcpy(new_node->prefix, n->prefix, prefix_match);

            // Add existing subtree under the diverging byte of old prefix
            uint8_t old_byte = n->prefix[prefix_match];
            // Shorten old node's prefix
            uint8_t remaining = n->prefix_len - prefix_match - 1;
            std::memmove(n->prefix, n->prefix + prefix_match + 1, remaining);
            n->prefix_len = remaining;

            Node* new_node_as_node = reinterpret_cast<Node*>(new_node);
            add_child(new_node_as_node, old_byte, n);

            // Add new leaf
            auto* new_leaf = make_leaf(key);
            uint8_t new_byte = key[depth + prefix_match];
            add_child(new_node_as_node, new_byte, reinterpret_cast<Node*>(new_leaf));
            ref = new_node_as_node;
            size_++;
            return new_leaf;
        }

        depth += n->prefix_len;

        // Find child for next byte
        if (depth >= 8) {
            // Shouldn't happen with correct 8-byte keys, but guard
            return nullptr;
        }

        uint8_t next_byte = key[depth];
        Node** child = find_child(n, next_byte);
        if (child != nullptr) {
            return insert_recursive(*child, key, depth + 1);
        }

        // No child for this byte: add leaf
        auto* new_leaf = make_leaf(key);
        add_child(ref, next_byte, reinterpret_cast<Node*>(new_leaf));
        size_++;
        return new_leaf;
    }

    bool remove_recursive(Node*& ref, const uint8_t key[8], int depth) {
        if (ref == nullptr) return false;

        if (is_leaf(ref)) {
            Leaf* leaf = as_leaf(ref);
            if (std::memcmp(leaf->key, key, 8) == 0) {
                delete leaf;
                ref = nullptr;
                size_--;
                return true;
            }
            return false;
        }

        Node* n = ref;
        uint8_t prefix_match = check_prefix(n, key, depth);
        if (prefix_match < n->prefix_len) {
            return false;  // Key not in tree
        }
        depth += n->prefix_len;

        if (depth >= 8) return false;

        uint8_t next_byte = key[depth];
        Node** child = find_child(n, next_byte);
        if (child == nullptr) return false;

        if (is_leaf(*child)) {
            Leaf* leaf = as_leaf(*child);
            if (std::memcmp(leaf->key, key, 8) == 0) {
                delete leaf;
                *child = nullptr;
                remove_child(ref, next_byte);
                // If the node has no children, remove it
                if (ref != nullptr && !is_leaf(ref) && ref->num_children == 0) {
                    delete_node(ref);
                    ref = nullptr;
                }
                size_--;
                return true;
            }
            return false;
        }

        return remove_recursive(*child, key, depth + 1);
    }

    Leaf* find_recursive(Node* n, const uint8_t key[8], int depth) const {
        if (n == nullptr) return nullptr;

        if (is_leaf(n)) {
            Leaf* leaf = as_leaf(n);
            if (std::memcmp(leaf->key, key, 8) == 0) {
                return leaf;
            }
            return nullptr;
        }

        uint8_t prefix_match = check_prefix(n, key, depth);
        if (prefix_match < n->prefix_len) return nullptr;
        depth += n->prefix_len;

        if (depth >= 8) return nullptr;

        uint8_t next_byte = key[depth];
        auto* child = find_child_const(n, next_byte);
        if (child == nullptr) return nullptr;

        return find_recursive(const_cast<Node*>(*child), key, depth + 1);
    }

    // ─── Min/Max ───────────────────────────────────────────────

    static Leaf* min_leaf(Node* n) {
        if (n == nullptr) return nullptr;
        if (is_leaf(n)) return as_leaf(n);

        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<Node4*>(n);
                // Keys sorted ascending, first is smallest
                return (node->num_children > 0) ? min_leaf(node->children[0]) : nullptr;
            }
            case NODE16: {
                auto* node = static_cast<Node16*>(n);
                return (node->num_children > 0) ? min_leaf(node->children[0]) : nullptr;
            }
            case NODE48: {
                auto* node = static_cast<Node48*>(n);
                for (int i = 0; i < 256; ++i) {
                    if (node->child_index[i] != 255) {
                        return min_leaf(node->children[node->child_index[i]]);
                    }
                }
                return nullptr;
            }
            case NODE256: {
                auto* node = static_cast<Node256*>(n);
                for (int i = 0; i < 256; ++i) {
                    if (node->children[i] != nullptr) {
                        return min_leaf(node->children[i]);
                    }
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
    }

    static Leaf* max_leaf(Node* n) {
        if (n == nullptr) return nullptr;
        if (is_leaf(n)) return as_leaf(n);

        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<Node4*>(n);
                return (node->num_children > 0) ? max_leaf(node->children[node->num_children - 1]) : nullptr;
            }
            case NODE16: {
                auto* node = static_cast<Node16*>(n);
                return (node->num_children > 0) ? max_leaf(node->children[node->num_children - 1]) : nullptr;
            }
            case NODE48: {
                auto* node = static_cast<Node48*>(n);
                for (int i = 255; i >= 0; --i) {
                    if (node->child_index[i] != 255) {
                        return max_leaf(node->children[node->child_index[i]]);
                    }
                }
                return nullptr;
            }
            case NODE256: {
                auto* node = static_cast<Node256*>(n);
                for (int i = 255; i >= 0; --i) {
                    if (node->children[i] != nullptr) {
                        return max_leaf(node->children[i]);
                    }
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
    }

    // ─── Iteration ─────────────────────────────────────────────

    template <typename Func>
    static void for_each_asc(const Node* n, Func&& func) {
        if (n == nullptr) return;

        if (is_leaf(n)) {
            const Leaf* leaf = as_leaf(n);
            int64_t key = decode_key(leaf->key);
            func(key, leaf->value);
            return;
        }

        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<const Node4*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    for_each_asc(node->children[i], func);
                }
                break;
            }
            case NODE16: {
                auto* node = static_cast<const Node16*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    for_each_asc(node->children[i], func);
                }
                break;
            }
            case NODE48: {
                auto* node = static_cast<const Node48*>(n);
                for (int i = 0; i < 256; ++i) {
                    if (node->child_index[i] != 255) {
                        for_each_asc(node->children[node->child_index[i]], func);
                    }
                }
                break;
            }
            case NODE256: {
                auto* node = static_cast<const Node256*>(n);
                for (int i = 0; i < 256; ++i) {
                    if (node->children[i] != nullptr) {
                        for_each_asc(node->children[i], func);
                    }
                }
                break;
            }
        }
    }

    template <typename Func>
    static void for_each_desc(const Node* n, Func&& func) {
        if (n == nullptr) return;

        if (is_leaf(n)) {
            const Leaf* leaf = as_leaf(n);
            int64_t key = decode_key(leaf->key);
            func(key, leaf->value);
            return;
        }

        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<const Node4*>(n);
                for (int i = node->num_children - 1; i >= 0; --i) {
                    for_each_desc(node->children[i], func);
                }
                break;
            }
            case NODE16: {
                auto* node = static_cast<const Node16*>(n);
                for (int i = node->num_children - 1; i >= 0; --i) {
                    for_each_desc(node->children[i], func);
                }
                break;
            }
            case NODE48: {
                auto* node = static_cast<const Node48*>(n);
                for (int i = 255; i >= 0; --i) {
                    if (node->child_index[i] != 255) {
                        for_each_desc(node->children[node->child_index[i]], func);
                    }
                }
                break;
            }
            case NODE256: {
                auto* node = static_cast<const Node256*>(n);
                for (int i = 255; i >= 0; --i) {
                    if (node->children[i] != nullptr) {
                        for_each_desc(node->children[i], func);
                    }
                }
                break;
            }
        }
    }

    // ─── Memory management ─────────────────────────────────────

    static void delete_node(Node* n) {
        if (n == nullptr) return;
        switch (n->type) {
            case NODE4:   delete static_cast<Node4*>(n); break;
            case NODE16:  delete static_cast<Node16*>(n); break;
            case NODE48:  delete static_cast<Node48*>(n); break;
            case NODE256: delete static_cast<Node256*>(n); break;
            default: break;
        }
    }

    static void destroy(Node* n) {
        if (n == nullptr) return;

        if (is_leaf(n)) {
            delete as_leaf(n);
            return;
        }

        switch (n->type) {
            case NODE4: {
                auto* node = static_cast<Node4*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    destroy(node->children[i]);
                }
                delete node;
                break;
            }
            case NODE16: {
                auto* node = static_cast<Node16*>(n);
                for (uint16_t i = 0; i < node->num_children; ++i) {
                    destroy(node->children[i]);
                }
                delete node;
                break;
            }
            case NODE48: {
                auto* node = static_cast<Node48*>(n);
                for (uint16_t i = 0; i < 48; ++i) {
                    if (node->children[i] != nullptr) {
                        destroy(node->children[i]);
                    }
                }
                delete node;
                break;
            }
            case NODE256: {
                auto* node = static_cast<Node256*>(n);
                for (int i = 0; i < 256; ++i) {
                    if (node->children[i] != nullptr) {
                        destroy(node->children[i]);
                    }
                }
                delete node;
                break;
            }
        }
    }

    // ─── Data ──────────────────────────────────────────────────

    Node* root_ = nullptr;
    size_t size_ = 0;
};

}  // namespace engine
