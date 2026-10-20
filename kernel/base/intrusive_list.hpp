#pragma once

#include "kernel/base/assert.hpp"
#include "kernel/base/types.hpp"

namespace barry {

struct DefaultListTag {};

template<typename T, typename Tag = DefaultListTag>
class IntrusiveListNode {
public:
    bool is_linked() const { return m_next != nullptr; }

protected:
    IntrusiveListNode() = default;
    ~IntrusiveListNode() { VERIFY(!is_linked()); }

private:
    template<typename, typename>
    friend class IntrusiveList;

    IntrusiveListNode* m_next { nullptr };
    IntrusiveListNode* m_prev { nullptr };
};

template<typename T, typename Tag = DefaultListTag>
class IntrusiveList {
public:
    using Node = IntrusiveListNode<T, Tag>;

    IntrusiveList() { reset_sentinel(); }

    IntrusiveList(IntrusiveList const&) = delete;
    IntrusiveList& operator=(IntrusiveList const&) = delete;

    ~IntrusiveList() { m_sentinel.m_next = nullptr; }

    bool is_empty() const { return m_sentinel.m_next == &m_sentinel; }
    usize size() const { return m_size; }

    void append(T& element) { link(node_of(element), m_sentinel.m_prev, &m_sentinel); }
    void prepend(T& element) { link(node_of(element), &m_sentinel, m_sentinel.m_next); }

    void remove(T& element) {
        Node* node = node_of(element);
        VERIFY(node->is_linked());
        node->m_prev->m_next = node->m_next;
        node->m_next->m_prev = node->m_prev;
        node->m_next = nullptr;
        node->m_prev = nullptr;
        --m_size;
    }

    T* first() { return is_empty() ? nullptr : element_of(m_sentinel.m_next); }
    T* last() { return is_empty() ? nullptr : element_of(m_sentinel.m_prev); }

    T* take_first() {
        T* element = first();
        if (element != nullptr)
            remove(*element);
        return element;
    }

    class Iterator {
    public:
        explicit Iterator(Node* node) : m_node(node) {}

        T& operator*() const { return *element_of(m_node); }
        T* operator->() const { return element_of(m_node); }

        Iterator& operator++() {
            m_node = m_node->m_next;
            return *this;
        }

        bool operator==(Iterator const& other) const { return m_node == other.m_node; }

    private:
        Node* m_node;
    };

    Iterator begin() { return Iterator(m_sentinel.m_next); }
    Iterator end() { return Iterator(&m_sentinel); }

private:
    static_assert(is_base_of_v<IntrusiveListNode<T, Tag>, T>,
                  "T must derive from IntrusiveListNode<T, Tag>");

    static Node* node_of(T& element) { return static_cast<Node*>(&element); }
    static T* element_of(Node* node) { return static_cast<T*>(node); }

    void reset_sentinel() {
        m_sentinel.m_next = &m_sentinel;
        m_sentinel.m_prev = &m_sentinel;
    }

    void link(Node* node, Node* before, Node* after) {
        VERIFY(!node->is_linked());
        node->m_prev = before;
        node->m_next = after;
        before->m_next = node;
        after->m_prev = node;
        ++m_size;
    }

    Node m_sentinel;
    usize m_size { 0 };
};

}  // namespace barry
