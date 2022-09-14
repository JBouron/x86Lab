#include <x86lab/snapshot.hpp>
#include <algorithm>
#include <map>
#include <functional>

namespace X86Lab {

// Tree-like data structure of blocks of memory. Each node of the tree is
// either a leaf node which contains data for a particular range of the memory;
// or an intermediate node which does not contain data but two pointers to
// sub-node which range adds up to the intermediate node's range.
// Building a new tree from an existing one permits re-using nodes from the
// existing tree if the memory under those nodes have not changed.
class BlockTree {
public:
    // Construct a BlockTree.
    // @param base: The base tree to build from. nullptr if this is the first
    // tree built.
    // @param data: A read-only copy of the memory to be represented by the
    // tree.
    // @param size: The size of the memory.
    BlockTree(std::shared_ptr<BlockTree> const base,
              u8 const * const data,
              u64 const size) :
        m_memSize(size),
        m_root(build(base, data, size)) {}

    // Read a buffer from the memory described by this tree.
    // @param offset: The offset at which to read from.
    // @param size: The size of the buffer to read in bytes.
    // @return: An allocated buffer of length `size` bytes, which content is
    // initialized to the content of the memory in range [offset; offest + size]
    // Note: Attempting to read outside of the memory boundaries reads only
    // zeroes.
    std::unique_ptr<u8> read(u64 const offset, u64 const size) const {
        std::unique_ptr<u8> buf(new u8[size]);
        std::memset(buf.get(), 0, size);
        if (offset >= m_memSize) {
            // Nothing to read.
            return buf;
        }

        u64 const toRead(std::min(size, m_memSize - offset));
        m_root->read(buf.get(), offset, toRead);
        return buf;
    }

private:
    // A Node in a BlockTree. A node covers a well defined range of memory
    // [offset; offset + size]. The data for this range is either stored in this
    // node directly (e.g. this is a leaf node) or under the children of that
    // node.
    // A node either has 0 or exactly 2 children. If a node N has children A and
    // B, then the following invariants hold:
    //  - A::offset == N::offset
    //  - A::offset + A::size == B::offset
    //  - A::size + B::size == N::size.
    // from above, it follows that:
    //  - A::size == B::size == N::size / 2
    //  - N::offset + N::size / 2 == A::offset + A::size == B::offset
    // A node's size is always a power of 2.
    class Node {
    public:
        // The minimum node size allowed. A Node of this size cannot be split
        // into two sub-nodes.
        static constexpr u64 MinSize = 64;

        // Create a leaf node.
        // @param offset: The offset of the range of memory defined by that
        // node.
        // @param size: The size in bytes of the range of memory defined by that
        // node.
        // @param data: The data for the range of memory. This pointer must be
        // `size` bytes long.
        Node(u64 const offset, u64 const size, std::shared_ptr<u8> const data) :
            m_offset(offset), m_size(size), m_data(data) {}

        // Create an intermediate node.
        // @param offset: The offset of the range of memory defined by that
        // node.
        // @param size: The size in bytes of the range of memory defined by that
        // node.
        // @param left: The first child of this node, which describes the
        // first/left half of the memory range.
        // @param right: The second child of this node, which describes the
        // second/right half of the memory range.
        // /!\ This function enforces the invariants defined above, that is that
        // the union of the ranges defined by left and right must be exactly
        // equal to the range of this node.
        Node(u64 const offset,
             u64 const size,
             std::shared_ptr<Node> const left,
             std::shared_ptr<Node> const right) :
            m_offset(offset), m_size(size), m_left(left), m_right(right) {
            // Check invariants.
            assert(!!left&&!!right);
            assert((left->m_size + right->m_size) == m_size);
            assert(left->m_offset == m_offset);
            assert(right->m_offset == left->m_offset + left->m_size);
            assert(left->m_size == right->m_size);
        }

        // Read part of the memory range described by this Node. If this is a
        // leaf node then the data is directly copied into the destination
        // buffer, otherwise this function recurses into the children nodes to
        // fill the buffer.
        // @param dest: The buffer to read into.
        // @param relOff: The offset to read from. This is relative to the
        // start of memory range described by this node.
        // @param len: The number of bytes to read.
        void read(u8 * const dest, u64 const relOff, u64 const len) const {
            // The requested range must fully lie within this node.
            assert(len <= m_size);

            if (isLeaf()) {
                // This is a leaf node, the data is readily available, just copy
                // it into dest and ret.
                std::memcpy(dest, m_data.get() + relOff, len);
                return;
            }

            // This is an intermediate, we need to recurse on each child.
            u64 const middle(m_size / 2);
            bool const recurseLeft(relOff < middle);
            bool const recurseRight(middle < relOff + len);

            if (recurseLeft) {
                // Read the left child. A couple of things here:
                //  - Because left::offset == this::offset, the offset at which
                //  to start reading does not change, hence:
                u64 const leftOff(relOff);
                //  - We can only read up to the middle of the parent, since
                //  data after that is stored on the right child, hence:
                u64 const leftLen(std::min(relOff + len, middle) - leftOff);
                //  - Data stored on left child comes first, hence:
                u8 * const leftPtr(dest);
                m_left->read(leftPtr, leftOff, leftLen);
            }
            if (recurseRight) {
                // Read from the right child. Since right::offset !=
                // this::offset, doing so is a bit more tricky than reading from
                // the left child.
                // The absolute offset (e.g. relative to this node) at which we
                // need to start reading.
                u64 const rightOffAbs(std::max(middle, relOff));
                // The offset for the recursive call must be relative to the
                // right node, hence - middle.
                u64 const rightOff(rightOffAbs - middle);
                u64 const rightLen(relOff + len - rightOffAbs);
                // If we have been reading data from the left node, then there
                // is already data in dest that we need to skip over. Otherwise
                // the requested data is fully contained in the right child and
                // we simply write at dest.
                u8 * const rightPtr(dest + (recurseLeft ? middle - relOff : 0));
                m_right->read(rightPtr, rightOff, rightLen);
            }
        }

        // Check if this node is a leaf node.
        // @return: true if this is a leaf node, false if it is an intermediate
        // node.
        bool isLeaf() const {
            return !!m_data;
        }

        // Get a pointer on the left child node of this node.
        // @return: Pointer to the left child. nullptr if the current node is a
        // leaf node.
        std::shared_ptr<Node> leftNode() const {
            return m_left;
        }

        // Get a pointer on the right child node of this node.
        // @return: Pointer to the right child. nullptr if the current node is a
        // leaf node.
        std::shared_ptr<Node> rightNode() const {
            return m_right;
        }

    private:
        // Offset and size of the memory region covered by this node.
        u64 m_offset;
        u64 m_size;

        // Left child. nullptr if this node is a leaf node.
        std::shared_ptr<Node> m_left;
        // Right child. nullptr if this node is a leaf node.
        std::shared_ptr<Node> m_right;
        // Data of this node. nullptr if this node is an intermediate node.
        std::shared_ptr<u8> m_data;
    };

    // Size of the memory described by this BlockTree.
    u64 m_memSize;

    // Root node of the block tree.
    std::shared_ptr<Node> m_root;

    // Build a BlockTree from a base tree. This creates an optimized BlockTree
    // that re-uses nodes from the base tree if the memory ranges described by
    // those node are identical in `memory`.
    // @param base: The base BlockTree to build from.
    // @param data: The latest memory content. This is the memory that will be
    // described by the new tree.
    // @param size: The size of the memory.
    // @return: The root node of the new tree covering the entire memory.
    static std::shared_ptr<Node> build(std::shared_ptr<BlockTree> const base,
                                       u8 const * const data,
                                       u64 const size) {
        assert(!(size % Node::MinSize));
        // Helper lambda recursively building the new tree one node at a time.
        // This lambda builds a new node for range data[offset;offset + size]
        // using baseNode as the base. If the data in this range is identical to
        // the data described by baseNode then baseNode is returned. Otherwise
        // this returns a new Node describing the latest data[offset;offset +
        // size].
        // If baseNode is nullptr then this always allocate a leaf node.
        std::function<std::shared_ptr<Node> (std::shared_ptr<Node>,u64,u64)>
            inner([&](std::shared_ptr<Node> const baseNode,
                      u64 const offset,
                      u64 const size) {
            assert(size >= Node::MinSize);
            if (!baseNode) {
                // Base-case, nothing to base on, just build a leaf node for
                // that range of memory.
                std::shared_ptr<u8> leafData(new u8[size]);
                std::memcpy(leafData.get(), data + offset, size);
                return std::shared_ptr<Node>(new Node(offset, size, leafData));
            }

            // We have a baseNode for this range. Read its data and compare to
            // the latest data.
            std::unique_ptr<u8> baseNodeData(new u8[size]);
            baseNode->read(baseNodeData.get(), 0, size);
            if (!std::memcmp(baseNodeData.get(), data + offset, size)) {
                // Data is identical to baseNode, we can re-use this node.
                return baseNode;
            } else if (size == Node::MinSize) {
                // The data is not identical to baseNode, we need to allocated a
                // new node. However, we have reached the minimum allowed node
                // size. Hence create a leaf node.
                std::shared_ptr<u8> leafData(new u8[size]);
                std::memcpy(leafData.get(), data + offset, size);
                return std::shared_ptr<Node>(new Node(offset, size, leafData));
            } else {
                // The data is different than the base. Create an intermediate
                // node that breaks this range in half as this range will most
                // likely change in the future, at which point we might have
                // more chance for re-use.
                u64 const middleOff(offset + size / 2);
                std::shared_ptr<Node> recLeft(
                    inner(baseNode->leftNode(), offset, size / 2));
                std::shared_ptr<Node> recRight(
                    inner(baseNode->rightNode(), middleOff, size / 2));
                return std::shared_ptr<Node>(
                    new Node(offset, size, recLeft, recRight));
            }
        });
        return inner(!!base ? base->m_root : nullptr, 0, size);
    }
};

Snapshot::Snapshot(std::unique_ptr<Vm::State> state) :
    Snapshot(nullptr, std::move(state)) {}

Snapshot::Snapshot(std::shared_ptr<Snapshot> const base,
                   std::unique_ptr<Vm::State> state) :
    m_baseSnapshot(base),
    m_regs(state->registers()),
    m_blockTree(new BlockTree(!!base ? base->m_blockTree : nullptr,
                            state->memory().data.get(),
                            state->memory().size)) {}

std::shared_ptr<Snapshot> Snapshot::base() const {
    return m_baseSnapshot;
}

bool Snapshot::hasBase() const {
    return m_baseSnapshot != nullptr;
}

Snapshot::Registers const& Snapshot::registers() const {
    return m_regs;
}

std::unique_ptr<u8> Snapshot::readPhysicalMemory(u64 const offset,
                                                 u64 const size) const {
    return m_blockTree->read(offset, size);
}
}
