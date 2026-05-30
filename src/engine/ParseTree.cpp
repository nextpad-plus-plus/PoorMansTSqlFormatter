/*
 * ParseTree.cpp — see ParseTree.h. Faithful port of ParseTree.cs.
 */
#include "ParseTree.h"
#include <stdexcept>

namespace pmsf {

using namespace SC;

namespace {
// Regex.IsMatch(text, @"(\r|\n)+") — contains any CR or LF.
bool hasLineBreak(const std::string& s) {
    return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
}
}

ParseTree::ParseTree(const std::string& rootName) {
    root_ = createNode(rootName, "");
    currentContainer_ = root_.get();
}

void ParseTree::setCurrentContainer(Node* value) {
    if (value == nullptr) throw std::runtime_error("CurrentContainer null");
    currentContainer_ = value;
}

void ParseTree::setError() {
    currentContainer_->setAttribute(ANAME_HASERROR, "1");
    root_->setAttribute(ANAME_ERRORFOUND, "1");
}

Node* ParseTree::saveNewElement(const std::string& name, const std::string& value) {
    return saveNewElement(name, value, currentContainer_);
}
Node* ParseTree::saveNewElement(const std::string& name, const std::string& value, Node* target) {
    NodeRef newElement = createNode(name, value);
    Node* raw = newElement.get();
    target->addChild(newElement);
    return raw;
}
Node* ParseTree::saveNewElementWithError(const std::string& name, const std::string& value) {
    Node* newElement = saveNewElement(name, value);
    setError();
    return newElement;
}
Node* ParseTree::saveNewElementAsPriorSibling(const std::string& name, const std::string& value, Node* nodeToSaveBefore) {
    NodeRef newElement = createNode(name, value);
    Node* raw = newElement.get();
    nodeToSaveBefore->parent->insertChildBefore(newElement, nodeToSaveBefore);
    return raw;
}

void ParseTree::startNewContainer(const std::string& name, const std::string& containerOpenValue,
                                  const std::string& containerType) {
    currentContainer_ = saveNewElement(name, "");
    Node* containerOpen = saveNewElement(ENAME_CONTAINER_OPEN, "");
    saveNewElement(ENAME_OTHERKEYWORD, containerOpenValue, containerOpen);
    currentContainer_ = saveNewElement(containerType, "");
}

void ParseTree::startNewStatement() { startNewStatement(currentContainer_); }
void ParseTree::startNewStatement(Node* target) {
    newStatementDue = false;
    Node* newStatement = saveNewElement(ENAME_SQL_STATEMENT, "", target);
    currentContainer_ = saveNewElement(ENAME_SQL_CLAUSE, "", newStatement);
}

void ParseTree::escapeAnyBetweenConditions() {
    if (pathNameMatches(0, ENAME_BETWEEN_UPPERBOUND) && pathNameMatches(1, ENAME_BETWEEN_CONDITION))
        moveToAncestorContainer(2);
}

void ParseTree::escapeMergeAction() {
    if (pathNameMatches(0, ENAME_SQL_CLAUSE)
        && pathNameMatches(1, ENAME_SQL_STATEMENT)
        && pathNameMatches(2, ENAME_MERGE_ACTION)
        && hasNonWhiteSpaceNonCommentContent(currentContainer_))
        moveToAncestorContainer(4);
}

void ParseTree::escapePartialStatementContainers() {
    if (pathNameMatches(0, ENAME_DDL_PROCEDURAL_BLOCK)
        || pathNameMatches(0, ENAME_DDL_OTHER_BLOCK)
        || pathNameMatches(0, ENAME_DDL_DECLARE_BLOCK))
        moveToAncestorContainer(1);
    else if (pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
             && pathNameMatches(1, ENAME_CURSOR_FOR_OPTIONS))
        moveToAncestorContainer(3);
    else if (pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
             && pathNameMatches(1, ENAME_PERMISSIONS_RECIPIENT))
        moveToAncestorContainer(3);
    else if (pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
             && pathNameMatches(1, ENAME_DDL_WITH_CLAUSE)
             && (pathNameMatches(2, ENAME_PERMISSIONS_BLOCK)
                 || pathNameMatches(2, ENAME_DDL_PROCEDURAL_BLOCK)
                 || pathNameMatches(2, ENAME_DDL_OTHER_BLOCK)
                 || pathNameMatches(2, ENAME_DDL_DECLARE_BLOCK)))
        moveToAncestorContainer(3);
    else if (pathNameMatches(0, ENAME_MERGE_WHEN))
        moveToAncestorContainer(2);
    else if (pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
             && (pathNameMatches(1, ENAME_CTE_WITH_CLAUSE)
                 || pathNameMatches(1, ENAME_DDL_DECLARE_BLOCK)))
        moveToAncestorContainer(2);
}

void ParseTree::escapeAnySingleOrPartialStatementContainers() {
    escapeAnyBetweenConditions();
    escapeAnySelectionTarget();
    escapeJoinCondition();

    if (hasNonWhiteSpaceNonCommentContent(currentContainer_)) {
        escapeCursorForBlock();
        escapeMergeAction();
        escapePartialStatementContainers();

        while (true) {
            if (pathNameMatches(0, ENAME_SQL_CLAUSE)
                && pathNameMatches(1, ENAME_SQL_STATEMENT)
                && pathNameMatches(2, ENAME_CONTAINER_SINGLESTATEMENT)) {
                Node* currentSingleContainer = currentContainer_->parent->parent;
                if (pathNameMatches(currentSingleContainer, 1, ENAME_ELSE_CLAUSE)) {
                    currentContainer_ = currentSingleContainer->parent->parent->parent;
                } else {
                    currentContainer_ = currentSingleContainer->parent->parent;
                }
            } else {
                break;
            }
        }
    }
}

void ParseTree::escapeCursorForBlock() {
    if (pathNameMatches(0, ENAME_SQL_CLAUSE)
        && pathNameMatches(1, ENAME_SQL_STATEMENT)
        && pathNameMatches(2, ENAME_CONTAINER_GENERALCONTENT)
        && pathNameMatches(3, ENAME_CURSOR_FOR_BLOCK)
        && hasNonWhiteSpaceNonCommentContent(currentContainer_))
        moveToAncestorContainer(5);
}

Node* ParseTree::escapeAndLocateNextStatementContainer(bool escapeEmptyContainer) {
    escapeAnySingleOrPartialStatementContainers();

    if (pathNameMatches(0, ENAME_BOOLEAN_EXPRESSION)
        && (pathNameMatches(1, ENAME_IF_STATEMENT) || pathNameMatches(1, ENAME_WHILE_LOOP))) {
        return saveNewElement(ENAME_CONTAINER_SINGLESTATEMENT, "", currentContainer_->parent);
    } else if (pathNameMatches(0, ENAME_SQL_CLAUSE)
               && pathNameMatches(1, ENAME_SQL_STATEMENT)
               && (escapeEmptyContainer || hasNonWhiteSpaceNonSingleCommentContent(currentContainer_))) {
        return currentContainer_->parent->parent;
    } else {
        return nullptr;
    }
}

void ParseTree::migrateApplicableCommentsFromContainer(Node* previousContainerElement) {
    Node* migrationContext = previousContainerElement;
    Node* migrationCandidate = lastChild(previousContainerElement);
    Node* insertBeforeNode = currentContainer_;

    while (migrationCandidate != nullptr) {
        if (migrationCandidate->name == ENAME_WHITESPACE) {
            migrationCandidate = previousSibling(migrationCandidate);
            continue;
        } else if (previousSibling(migrationCandidate) != nullptr
                   && nameIn(migrationCandidate->name, ENAMELIST_COMMENT)
                   && nameIn(previousSibling(migrationCandidate)->name, ENAMELIST_NONCONTENT)) {
            Node* prev = previousSibling(migrationCandidate);
            if (prev->name == ENAME_WHITESPACE && hasLineBreak(prev->textValue)) {
                // migrate everything considered so far (backwards from the end)
                while (lastChild(migrationContext) != migrationCandidate) {
                    Node* movingNode = lastChild(migrationContext);
                    NodeRef movingRef = movingNode->parent->removeChild(movingNode);
                    currentContainer_->parent->insertChildBefore(movingRef, insertBeforeNode);
                    insertBeforeNode = movingNode;
                }
                NodeRef candRef = migrationCandidate->parent->removeChild(migrationCandidate);
                currentContainer_->parent->insertChildBefore(candRef, insertBeforeNode);
                insertBeforeNode = migrationCandidate;
                migrationCandidate = lastChild(migrationContext);
            } else {
                migrationCandidate = previousSibling(migrationCandidate);
            }
        } else if (!migrationCandidate->textValue.empty()) {
            migrationCandidate = nullptr;
        } else {
            migrationContext = migrationCandidate;
            migrationCandidate = lastChild(migrationCandidate);
        }
    }
}

void ParseTree::considerStartingNewStatement() {
    escapeAnyBetweenConditions();
    escapeAnySelectionTarget();
    escapeJoinCondition();

    Node* previousContainerElement = currentContainer_;
    Node* nextStatementContainer = escapeAndLocateNextStatementContainer(false);

    if (nextStatementContainer != nullptr) {
        Node* inBetweenContainerElement = currentContainer_;
        startNewStatement(nextStatementContainer);
        if (inBetweenContainerElement != previousContainerElement)
            migrateApplicableCommentsFromContainer(inBetweenContainerElement);
        migrateApplicableCommentsFromContainer(previousContainerElement);
    }
}

void ParseTree::considerStartingNewClause() {
    escapeAnySelectionTarget();
    escapeAnyBetweenConditions();
    escapePartialStatementContainers();
    escapeJoinCondition();

    if (currentContainer_->name == ENAME_SQL_CLAUSE
        && hasNonWhiteSpaceNonSingleCommentContent(currentContainer_)) {
        Node* previousContainerElement = currentContainer_;
        currentContainer_ = saveNewElement(ENAME_SQL_CLAUSE, "", currentContainer_->parent);
        migrateApplicableCommentsFromContainer(previousContainerElement);
    } else if (currentContainer_->name == ENAME_EXPRESSION_PARENS
               || currentContainer_->name == ENAME_IN_PARENS
               || currentContainer_->name == ENAME_SELECTIONTARGET_PARENS
               || currentContainer_->name == ENAME_SQL_STATEMENT) {
        currentContainer_ = saveNewElement(ENAME_SQL_CLAUSE, "");
    }
}

void ParseTree::escapeAnySelectionTarget() {
    if (pathNameMatches(0, ENAME_SELECTIONTARGET))
        currentContainer_ = currentContainer_->parent;
}

void ParseTree::escapeJoinCondition() {
    if (pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
        && pathNameMatches(1, ENAME_JOIN_ON_SECTION))
        moveToAncestorContainer(2);
}

bool ParseTree::findValidBatchEnd() {
    Node* nextStatementContainer = escapeAndLocateNextStatementContainer(true);
    return nextStatementContainer != nullptr
        && (nextStatementContainer->name == ENAME_SQL_ROOT
            || (nextStatementContainer->name == ENAME_CONTAINER_GENERALCONTENT
                && nextStatementContainer->parent
                && nextStatementContainer->parent->name == ENAME_DDL_AS_BLOCK));
}

bool ParseTree::pathNameMatches(int levelsUp, const std::string& nameToMatch) const {
    return pathNameMatches(currentContainer_, levelsUp, nameToMatch);
}
bool ParseTree::pathNameMatches(Node* targetNode, int levelsUp, const std::string& nameToMatch) const {
    Node* currentNode = targetNode;
    while (levelsUp > 0) {
        if (!currentNode) return false;
        currentNode = currentNode->parent;
        levelsUp--;
    }
    return currentNode != nullptr && currentNode->name == nameToMatch;
}

bool ParseTree::hasNonWhiteSpaceNonSingleCommentContent(Node* containerNode) {
    for (const auto& testElement : containerNode->children) {
        const std::string& nm = testElement->name;
        if (nm != ENAME_WHITESPACE
            && nm != ENAME_COMMENT_SINGLELINE
            && nm != ENAME_COMMENT_SINGLELINE_CSTYLE
            && (nm != ENAME_COMMENT_MULTILINE || hasLineBreak(testElement->textValue)))
            return true;
    }
    return false;
}

bool ParseTree::hasNonWhiteSpaceNonCommentContent(Node* containerNode) const {
    return !childrenExcludingNames(containerNode, ENAMELIST_NONCONTENT).empty();
}

Node* ParseTree::getFirstNonWhitespaceNonCommentChildElement(Node* targetElement) const {
    auto v = childrenExcludingNames(targetElement, ENAMELIST_NONCONTENT);
    return v.empty() ? nullptr : v.front();
}
Node* ParseTree::getLastNonWhitespaceNonCommentChildElement(Node* targetElement) const {
    auto v = childrenExcludingNames(targetElement, ENAMELIST_NONCONTENT);
    return v.empty() ? nullptr : v.back();
}

void ParseTree::moveToAncestorContainer(int levelsUp) { moveToAncestorContainer(levelsUp, ""); }
void ParseTree::moveToAncestorContainer(int levelsUp, const std::string& targetContainerName) {
    Node* candidateContainer = currentContainer_;
    while (levelsUp > 0) {
        candidateContainer = candidateContainer->parent;
        levelsUp--;
    }
    if (targetContainerName.empty() || candidateContainer->name == targetContainerName)
        currentContainer_ = candidateContainer;
    else
        throw std::runtime_error("Ancestor node does not match expected name!");
}

} // namespace pmsf
