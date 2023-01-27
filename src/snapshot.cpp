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
    std::vector<u8> read(u64 const offset, u64 const size) const {
        std::vector<u8> buf(size, 0);
        if (offset >= m_memSize) {
            // Nothing to read.
            return buf;
        }

        u64 const toRead(std::min(size, m_memSize - offset));
        m_root->read(buf.data(), offset, toRead);
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

std::vector<u8> Snapshot::readPhysicalMemory(u64 const offset,
                                             u64 const size) const {
    return m_blockTree->read(offset, size);
}

// A entry in a page table. The beauty of X86_64 is that all level are sharing
// the same entry layout.
struct Entry {
    bool present : 1;
    bool writable : 1;
    bool userpage : 1;
    bool writeThrough : 1;
    bool cacheDisable : 1;
    bool accessed : 1;
    bool dirty : 1;
    bool pat : 1;
    bool global : 1;
    u16 : 3;
    // Technically the offset is not 52 bits but ~48 bits. That's fine, the
    // unused bits are 0 anyway (I think!).
    u64 next : 52;

    u64 nextTableOffset() const {
        return next << 12;
    }
} __attribute__((packed));

// Return type of the map<>() function below mapping a linear address to a
// physical offset. This is some sort of option which either contains the
// corresponding physical offset or an "invalid" address.
class MapResult {
public:
    // Construct a MapResult that contains a valid mapping.
    // @param phyAddr: The corresponding physical offset to the linear address
    // that was mapped.
    MapResult(u64 const phyOff) : m_isMapped(true), m_physicalOffset(phyOff) {}

    // Construct a MapResult that does not contain a mapping, e.g. the linear
    // address was not mapped to physical memory.
    MapResult() : m_isMapped(false), m_physicalOffset(0) {}

    // Check if this value contains a valid mapping.
    // @return: true if the linear address mapped to a physical offset, in which
    // case that physical offset can be retreived with physicalOffset. false if
    // the linear address is not mapped to physical memory.
    operator bool() const {
        return m_isMapped;
    }

    // Get the physical offset of the mapping. Must only be called when the
    // operator bool returns true, e.g. when a mapping exists.
    u64 physicalOffset() const {
        assert(m_isMapped);
        return m_physicalOffset;
    }

private:
    bool const m_isMapped;
    u64 const m_physicalOffset;
};

// Walk the page table to map a linear address to its corresponding physical
// address.
// @template L: The page table level at which we currently are.
// @param mem: The physical memory.
// @param tableOffset: The physical offset of the table to walk next.
// @param lAddr: The linear address to map.
// @return: The physical address to which `lAddr` is mapped to.
template<u64 L>
MapResult map(BlockTree const& mem, u64 const tableOffset, u64 const lAddr) {
    // Compute the bits that are used to index the current table of level L.
    u64 const mask(0b111111111);
    u64 const entryIdx((lAddr >> (12 + (L - 1) * 9)) & mask);
    u64 const entryOffset(tableOffset + entryIdx * sizeof(Entry));
    // Read the entry from the table.
    std::vector<u8> const raw(mem.read(entryOffset, sizeof(Entry)));
    Entry const * const entry(reinterpret_cast<Entry const*>(raw.data()));
    if (entry->present) {
        // Recurse on the next table.
        return map<L-1>(mem, entry->nextTableOffset(), lAddr);
    } else {
        // The linear address is not mapped. Return an invalid address.
        return MapResult();
    }
}

// Specialization of map<>() for level 0, e.g. a physical frame. This is the
// base case of the recursion.
template<>
MapResult map<0>(BlockTree const& mem __attribute__((unused)),
               u64 const frameOffset,
               u64 const lAddr) {
    // Sanity check that the address is page aligned.
    u64 const pageOffsetMask((1ULL << 12) - 1);
    assert(!(frameOffset & pageOffsetMask));
    u64 const pAddr(frameOffset | (lAddr & pageOffsetMask));
    return pAddr;
}

std::vector<u8> Snapshot::readLinearMemory(u64 const offset,
                                           u64 const size) const {
    // When reading linear memory we need to read page by page since they might
    // not be continous in physical memory.
    u64 const startPageIdx(offset >> 12);
    u64 const endPageIdx((offset + size - 1) >> 12);
    u64 const pml4Offset(m_regs.cr3 & ~((1 << 12) - 1));
    // The resulting buffer to return.
    std::vector<u8> result;
    for (u64 i(startPageIdx); i <= endPageIdx; ++i) {
        u64 const readStartLinOffset(std::max(offset, i * PAGE_SIZE));
        u64 const readEndLinOffset(std::min(offset + size, (i+1) * PAGE_SIZE));
        u64 const readLen(readEndLinOffset - readStartLinOffset);

        // Map the linear address to read from.
        MapResult const mapRes(map<4>(*m_blockTree.get(),
                                      pml4Offset,
                                      readStartLinOffset));
        if (mapRes) {
            // The linear address is mapped to physical memory. Read the
            // associated physical memory and append to the result buffer.
            u64 const readOff(mapRes.physicalOffset());
            // Read data for this page from physical memory.
            std::vector<u8> const data(m_blockTree->read(readOff, readLen));
            // Copy over the data in destination buffer.
            result.insert(result.end(), data.begin(), data.end());
        } else {
            // When the linear address is not mapped, we stop here. We can't
            // possibly go to the next page because then the caller would see
            // continous data that is not continuous in the linear address
            // space.
            break;
        }
    }
    return result;
}
}
