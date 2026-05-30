/*
 * ParseTree.h — faithful port of ParseTree.cs.
 *
 * A stateful tree builder that wraps the root Node and a CurrentContainer
 * cursor, with the container-navigation / clause-and-statement-escaping helpers
 * the parser drives. Owns the whole tree via `root` (a NodeRef).
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include "Node.h"
#include "SqlConstants.h"
#include <string>

namespace pmsf {

class ParseTree {
public:
    explicit ParseTree(const std::string& rootName);

    Node* root() const { return root_.get(); }
    NodeRef rootRef() const { return root_; }

    Node* currentContainer() const { return currentContainer_; }
    void setCurrentContainer(Node* value);  // C# CurrentContainer setter

    bool newStatementDue = false;

    void setError();

    Node* saveNewElement(const std::string& name, const std::string& value);
    Node* saveNewElement(const std::string& name, const std::string& value, Node* target);
    Node* saveNewElementWithError(const std::string& name, const std::string& value);
    Node* saveNewElementAsPriorSibling(const std::string& name, const std::string& value, Node* nodeToSaveBefore);

    void startNewContainer(const std::string& name, const std::string& containerOpenValue,
                           const std::string& containerType);
    void startNewStatement();
    void startNewStatement(Node* target);

    void escapeAnyBetweenConditions();
    void escapeMergeAction();
    void escapePartialStatementContainers();
    void escapeAnySingleOrPartialStatementContainers();
    void considerStartingNewStatement();
    void considerStartingNewClause();
    void escapeAnySelectionTarget();
    void escapeJoinCondition();
    bool findValidBatchEnd();

    bool pathNameMatches(int levelsUp, const std::string& nameToMatch) const;
    bool pathNameMatches(Node* targetNode, int levelsUp, const std::string& nameToMatch) const;

    bool hasNonWhiteSpaceNonCommentContent(Node* containerNode) const;
    Node* getFirstNonWhitespaceNonCommentChildElement(Node* targetElement) const;
    Node* getLastNonWhitespaceNonCommentChildElement(Node* targetElement) const;

    void moveToAncestorContainer(int levelsUp);
    void moveToAncestorContainer(int levelsUp, const std::string& targetContainerName);

private:
    NodeRef root_;
    Node* currentContainer_ = nullptr;

    void escapeCursorForBlock();
    Node* escapeAndLocateNextStatementContainer(bool escapeEmptyContainer);
    void migrateApplicableCommentsFromContainer(Node* previousContainerElement);
    static bool hasNonWhiteSpaceNonSingleCommentContent(Node* containerNode);
};

} // namespace pmsf
