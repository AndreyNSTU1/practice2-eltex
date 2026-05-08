#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_PRIORITY 256

typedef struct Node {
    int data;
    int priority;
    struct Node* next;
} Node;

typedef struct {
    Node* heads[MAX_PRIORITY];
} PriorityQueue;

// ===== функции =====

void initQueue(PriorityQueue* q) {
    for (int i = 0; i < MAX_PRIORITY; i++)
        q->heads[i] = NULL;
}

void enqueue(PriorityQueue* q, int data, int priority) {
    Node* newNode = malloc(sizeof(Node));
    newNode->data = data;
    newNode->priority = priority;
    newNode->next = NULL;

    if (!q->heads[priority]) {
        q->heads[priority] = newNode;
    } else {
        Node* t = q->heads[priority];
        while (t->next) t = t->next;
        t->next = newNode;
    }
}

Node* dequeueHighest(PriorityQueue* q) {
    for (int i = MAX_PRIORITY - 1; i >= 0; i--) {
        if (q->heads[i]) {
            Node* n = q->heads[i];
            q->heads[i] = n->next;
            return n;
        }
    }
    return NULL;
}

Node* dequeueByPriority(PriorityQueue* q, int p) {
    if (q->heads[p]) {
        Node* n = q->heads[p];
        q->heads[p] = n->next;
        return n;
    }
    return NULL;
}

Node* dequeueAtLeast(PriorityQueue* q, int p) {
    for (int i = MAX_PRIORITY - 1; i >= p; i--) {
        if (q->heads[i]) {
            Node* n = q->heads[i];
            q->heads[i] = n->next;
            return n;
        }
    }
    return NULL;
}

void freeNode(Node* n) {
    free(n);
}

// ===== main =====

int main() {
    PriorityQueue q;
    initQueue(&q);

    int choice, data, priority;

    while (1) {
        printf("\n1. Добавить\n2. max\n3. по приоритету\n4. >= приоритета\n0. выход\n> ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                printf("data: ");
                scanf("%d", &data);
                printf("priority: ");
                scanf("%d", &priority);
                enqueue(&q, data, priority);
                break;

            case 2: {
                Node* n = dequeueHighest(&q);
                if (n) {
                    printf("data=%d p=%d\n", n->data, n->priority);
                    freeNode(n);
                } else printf("пусто\n");
                break;
            }

            case 3:
                printf("priority: ");
                scanf("%d", &priority);
                {
                    Node* n = dequeueByPriority(&q, priority);
                    if (n) {
                        printf("data=%d p=%d\n", n->data, n->priority);
                        freeNode(n);
                    } else printf("нет\n");
                }
                break;

            case 4:
                printf("min priority: ");
                scanf("%d", &priority);
                {
                    Node* n = dequeueAtLeast(&q, priority);
                    if (n) {
                        printf("data=%d p=%d\n", n->data, n->priority);
                        freeNode(n);
                    } else printf("нет\n");
                }
                break;

            case 0:
                return 0;
        }
    }
}