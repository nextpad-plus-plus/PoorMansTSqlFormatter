/*
 * Node.h — faithful port of ParseStructure/Node.cs, NodeImpl.cs, NodeFactory.cs
 * and NodeExtensions.cs.
 *
 * The parse tree is an XML-like tree of named nodes with string attributes,
 * ordered children and text content. We model node ownership with shared_ptr
 * children + a raw parent pointer (faithful to the C# GC tree; no cycles).
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace pmsf {

class Node;
using NodeRef = std::shared_ptr<Node>;

class Node {
public:
    std::string name;
    std::string textValue;
    Node* parent = nullptr;                 // non-owning (parent outlives children)
    std::vector<NodeRef> children;
    std::unordered_map<std::string, std::string> attributes;

    // GetAttributeValue returns "" when absent (C# returns null; call sites that
    // need the distinction use hasAttribute()).
    std::string getAttributeValue(const std::string& aName) const;
    bool hasAttribute(const std::string& aName) const;
    void setAttribute(const std::string& name, const std::string& value);
    void removeAttribute(const std::string& name);

    void addChild(const NodeRef& child);
    void insertChildBefore(const NodeRef& newChild, Node* existingChild);
    // Returns the removed child (keeps it alive so callers can re-parent it).
    NodeRef removeChild(Node* child);

    // Find the owning shared_ptr for a raw child pointer (helper for moves).
    NodeRef refForChild(Node* child) const;
};

// NodeFactory.CreateNode
NodeRef createNode(const std::string& name, const std::string& textValue);

// ── NodeExtensions (free functions; operate on raw Node* for traversal) ─────
Node* followingChild(Node* parent, Node* fromChild);
Node* previousChild(Node* parent, Node* fromChild);
Node* nextSibling(Node* value);
Node* previousSibling(Node* value);
Node* rootContainer(Node* value);

std::vector<Node*> childrenByName(Node* value, const std::string& name);
std::vector<Node*> childrenByNames(Node* value, const std::vector<std::string>& names);
std::vector<Node*> childrenExcludingNames(Node* value, const std::vector<std::string>& names);

// SingleOrDefault semantics: return the single match, or nullptr if none.
// (Valid trees never produce >1 here; we return the first match defensively.)
Node* childByName(Node* value, const std::string& name);
Node* childByNames(Node* value, const std::vector<std::string>& names);
Node* childExcludingNames(Node* value, const std::vector<std::string>& names);

NodeRef extractStructureBetween(Node* startingElement, Node* endingElement);

// convenience
bool nameIn(const std::string& name, const std::vector<std::string>& names);

} // namespace pmsf
