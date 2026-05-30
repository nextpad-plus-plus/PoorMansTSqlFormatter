/*
 * Node.cpp — see Node.h. Faithful port of NodeImpl.cs + NodeExtensions.cs.
 */
#include "Node.h"
#include <algorithm>

namespace pmsf {

std::string Node::getAttributeValue(const std::string& aName) const {
    auto it = attributes.find(aName);
    return it != attributes.end() ? it->second : std::string();
}
bool Node::hasAttribute(const std::string& aName) const {
    return attributes.find(aName) != attributes.end();
}
void Node::setAttribute(const std::string& name, const std::string& value) {
    attributes[name] = value;
}
void Node::removeAttribute(const std::string& name) {
    attributes.erase(name);
}

void Node::addChild(const NodeRef& child) {
    // C# throws if the child already has a parent.
    child->parent = this;
    children.push_back(child);
}

void Node::insertChildBefore(const NodeRef& newChild, Node* existingChild) {
    newChild->parent = this;
    auto it = std::find_if(children.begin(), children.end(),
                           [&](const NodeRef& c) { return c.get() == existingChild; });
    children.insert(it, newChild);
}

NodeRef Node::removeChild(Node* child) {
    auto it = std::find_if(children.begin(), children.end(),
                           [&](const NodeRef& c) { return c.get() == child; });
    if (it == children.end()) return nullptr;
    NodeRef removed = *it;
    children.erase(it);
    removed->parent = nullptr;
    return removed;
}

NodeRef Node::refForChild(Node* child) const {
    for (const auto& c : children)
        if (c.get() == child) return c;
    return nullptr;
}

NodeRef createNode(const std::string& name, const std::string& textValue) {
    auto n = std::make_shared<Node>();
    n->name = name;
    n->textValue = textValue;
    return n;
}

// ── NodeExtensions ──────────────────────────────────────────────────────────
Node* followingChild(Node* value, Node* fromChild) {
    if (!value) return nullptr;
    bool targetFound = false;
    for (const auto& child : value->children) {
        if (targetFound) return child.get();
        if (child.get() == fromChild) targetFound = true;
    }
    return nullptr;
}

Node* previousChild(Node* value, Node* fromChild) {
    if (!value) return nullptr;
    Node* previousSibling = nullptr;
    for (const auto& child : value->children) {
        if (child.get() == fromChild) return previousSibling;
        previousSibling = child.get();
    }
    return nullptr;
}

Node* nextSibling(Node* value) {
    if (!value || !value->parent) return nullptr;
    return followingChild(value->parent, value);
}

Node* previousSibling(Node* value) {
    if (!value || !value->parent) return nullptr;
    return previousChild(value->parent, value);
}

Node* rootContainer(Node* value) {
    if (!value) return nullptr;
    Node* cur = value;
    while (cur->parent) cur = cur->parent;
    return cur;
}

bool nameIn(const std::string& name, const std::vector<std::string>& names) {
    for (const auto& n : names) if (n == name) return true;
    return false;
}

std::vector<Node*> childrenByName(Node* value, const std::string& name) {
    std::vector<Node*> out;
    if (!value) return out;
    for (const auto& c : value->children) if (c->name == name) out.push_back(c.get());
    return out;
}

std::vector<Node*> childrenByNames(Node* value, const std::vector<std::string>& names) {
    std::vector<Node*> out;
    if (!value) return out;
    for (const auto& c : value->children) if (nameIn(c->name, names)) out.push_back(c.get());
    return out;
}

std::vector<Node*> childrenExcludingNames(Node* value, const std::vector<std::string>& names) {
    std::vector<Node*> out;
    if (!value) return out;
    for (const auto& c : value->children) if (!nameIn(c->name, names)) out.push_back(c.get());
    return out;
}

Node* childByName(Node* value, const std::string& name) {
    auto v = childrenByName(value, name);
    return v.empty() ? nullptr : v.front();
}
Node* childByNames(Node* value, const std::vector<std::string>& names) {
    auto v = childrenByNames(value, names);
    return v.empty() ? nullptr : v.front();
}
Node* childExcludingNames(Node* value, const std::vector<std::string>& names) {
    auto v = childrenExcludingNames(value, names);
    return v.empty() ? nullptr : v.front();
}

// Faithful port of NodeExtensions.ExtractStructureBetween.
// `remainder` is the root NodeRef (returned, keeps the whole copied subtree
// alive via parent→children ownership); `remainderPosition` is a raw cursor
// into it. When remainderPosition is the root, its owning NodeRef is `remainder`.
NodeRef extractStructureBetween(Node* startingElement, Node* endingElement) {
    Node* currentNode = startingElement;
    Node* previousNode = nullptr;
    NodeRef remainder = nullptr;
    Node* remainderPosition = nullptr;

    while (currentNode != nullptr) {
        if (currentNode == endingElement) break;

        if (previousNode != nullptr) {
            NodeRef copyOfThisNode = createNode(currentNode->name, currentNode->textValue);
            for (const auto& attr : currentNode->attributes)
                copyOfThisNode->setAttribute(attr.first, attr.second);

            if (remainderPosition == nullptr) {
                remainder = copyOfThisNode;
                remainderPosition = copyOfThisNode.get();
            } else if (currentNode == previousNode->parent && remainderPosition->parent != nullptr) {
                remainderPosition = remainderPosition->parent;
            } else if (currentNode == previousNode->parent && remainderPosition->parent == nullptr) {
                // remainderPosition is the current root → its NodeRef is `remainder`.
                copyOfThisNode->addChild(remainder);
                remainder = copyOfThisNode;
                remainderPosition = copyOfThisNode.get();
            } else if (currentNode == nextSibling(previousNode) && remainderPosition->parent != nullptr) {
                remainderPosition->parent->addChild(copyOfThisNode);
                remainderPosition = copyOfThisNode.get();
            } else if (currentNode == nextSibling(previousNode) && remainderPosition->parent == nullptr) {
                NodeRef copyOfParent = createNode(currentNode->parent->name, currentNode->parent->textValue);
                copyOfParent->addChild(remainder);          // old root
                copyOfParent->addChild(copyOfThisNode);
                remainder = copyOfParent;
                remainderPosition = copyOfThisNode.get();
            } else {
                // must be a child
                remainderPosition->addChild(copyOfThisNode);
                remainderPosition = copyOfThisNode.get();
            }
        }

        Node* nextNode = nullptr;
        if (previousNode != nullptr && !currentNode->children.empty()
            && currentNode != previousNode->parent) {
            nextNode = currentNode->children.front().get();
        } else if (nextSibling(currentNode) != nullptr) {
            nextNode = nextSibling(currentNode);
        } else {
            nextNode = currentNode->parent;
        }

        previousNode = currentNode;
        currentNode = nextNode;
    }

    return remainder;
}

} // namespace pmsf
