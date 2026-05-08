#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------ СТРУКТУРЫ ------------------

typedef struct Contact {
    char lastName[50];
    char firstName[50];
} Contact;

typedef struct Node {
    Contact data;
    struct Node *left;
    struct Node *right;
    int height;
} Node;

// ------------------ УТИЛИТЫ ------------------

int height(Node *n) {
    return n ? n->height : 0;
}

int max(int a, int b) {
    return (a > b) ? a : b;
}

Node* createNode(Contact c) {
    Node* node = (Node*)malloc(sizeof(Node));
    node->data = c;
    node->left = node->right = NULL;
    node->height = 1;
    return node;
}

// ------------------ СРАВНЕНИЕ ------------------

int compare(Contact a, Contact b) {
    int res = strcmp(a.lastName, b.lastName);
    if (res == 0)
        return strcmp(a.firstName, b.firstName);
    return res;
}

// ------------------ ПОВОРОТЫ AVL ------------------

Node* rotateRight(Node* y) {
    Node* x = y->left;
    Node* T2 = x->right;

    x->right = y;
    y->left = T2;

    y->height = max(height(y->left), height(y->right)) + 1;
    x->height = max(height(x->left), height(x->right)) + 1;

    return x;
}

Node* rotateLeft(Node* x) {
    Node* y = x->right;
    Node* T2 = y->left;

    y->left = x;
    x->right = T2;

    x->height = max(height(x->left), height(x->right)) + 1;
    y->height = max(height(y->left), height(y->right)) + 1;

    return y;
}

int getBalance(Node* n) {
    return n ? height(n->left) - height(n->right) : 0;
}

// ------------------ ВСТАВКА ------------------

Node* insert(Node* node, Contact c) {
    if (!node)
        return createNode(c);

    if (compare(c, node->data) < 0)
        node->left = insert(node->left, c);
    else
        node->right = insert(node->right, c);

    node->height = 1 + max(height(node->left), height(node->right));

    int balance = getBalance(node);

    // LL
    if (balance > 1 && compare(c, node->left->data) < 0)
        return rotateRight(node);

    // RR
    if (balance < -1 && compare(c, node->right->data) > 0)
        return rotateLeft(node);

    // LR
    if (balance > 1 && compare(c, node->left->data) > 0) {
        node->left = rotateLeft(node->left);
        return rotateRight(node);
    }

    // RL
    if (balance < -1 && compare(c, node->right->data) < 0) {
        node->right = rotateRight(node->right);
        return rotateLeft(node);
    }

    return node;
}

// ------------------ ПОИСК ------------------

Node* search(Node* root, char* lastName) {
    if (!root) return NULL;

    int cmp = strcmp(lastName, root->data.lastName);

    if (cmp == 0)
        return root;
    else if (cmp < 0)
        return search(root->left, lastName);
    else
        return search(root->right, lastName);
}

// ------------------ ВЫВОД ДЕРЕВА ------------------

void printTree(Node* root, int space) {
    if (!root) return;

    const int COUNT = 5;
    space += COUNT;

    printTree(root->right, space);

    printf("\n");
    for (int i = COUNT; i < space; i++)
        printf(" ");
    printf("%s %s\n", root->data.lastName, root->data.firstName);

    printTree(root->left, space);
}

// ------------------ MAIN ------------------

int main() {
    Node* root = NULL;
    int choice;

    while (1) {
        printf("\n--- ТЕЛЕФОННАЯ КНИГА (AVL ДЕРЕВО) ---\n");
        printf("1. Добавить контакт\n");
        printf("2. Показать дерево\n");
        printf("3. Поиск по фамилии\n");
        printf("4. Выход\n");
        printf("Выбор: ");

        scanf("%d", &choice);

        if (choice == 1) {
            Contact c;
            printf("Фамилия: ");
            scanf("%s", c.lastName);
            printf("Имя: ");
            scanf("%s", c.firstName);

            root = insert(root, c);
            printf("Добавлено!\n");
        }
        else if (choice == 2) {
            printf("\nДерево контактов:\n");
            printTree(root, 0);
        }
        else if (choice == 3) {
            char name[50];
            printf("Фамилия: ");
            scanf("%s", name);

            Node* found = search(root, name);
            if (found)
                printf("Найден: %s %s\n",
                       found->data.lastName,
                       found->data.firstName);
            else
                printf("Не найден\n");
        }
        else break;
    }

    return 0;
}