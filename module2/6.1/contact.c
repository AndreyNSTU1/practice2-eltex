#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contact.h"

static Contact* head = NULL;

// сравнение
static int compare(Contact* a, Contact* b) {
    int c = strcmp(a->lastName, b->lastName);
    if (c == 0)
        c = strcmp(a->firstName, b->firstName);
    return c;
}

// создание
static Contact* createContact() {
    Contact* c = (Contact*)malloc(sizeof(Contact));

    printf("\nДобавление контакта\n");

    printf("Фамилия: ");
    scanf("%s", c->lastName);

    printf("Имя: ");
    scanf("%s", c->firstName);

    getchar();
    printf("Место работы: ");
    fgets(c->work, 100, stdin);

    printf("Должность: ");
    fgets(c->position, 100, stdin);

    printf("Телефоны: ");
    fgets(c->phones, 100, stdin);

    printf("Email: ");
    fgets(c->emails, 100, stdin);

    printf("Соцсети: ");
    fgets(c->social, 100, stdin);

    printf("Мессенджеры: ");
    fgets(c->messengers, 100, stdin);

    c->prev = c->next = NULL;
    return c;
}

void addContact() {
    Contact* newC = createContact();

    if (!head) {
        head = newC;
        return;
    }

    if (compare(newC, head) < 0) {
        newC->next = head;
        head->prev = newC;
        head = newC;
        return;
    }

    Contact* cur = head;

    while (cur->next && compare(cur->next, newC) < 0)
        cur = cur->next;

    newC->next = cur->next;
    if (cur->next)
        cur->next->prev = newC;

    cur->next = newC;
    newC->prev = cur;

    printf("Контакт добавлен!\n");
}

void showContacts() {
    if (!head) {
        printf("\nСписок пуст\n");
        return;
    }

    Contact* cur = head;
    int i = 0;

    while (cur) {
        printf("%d. %s %s\n", i, cur->lastName, cur->firstName);
        cur = cur->next;
        i++;
    }
}

static Contact* getByIndex(int index) {
    Contact* cur = head;
    int i = 0;

    while (cur && i < index) {
        cur = cur->next;
        i++;
    }
    return cur;
}

void editContact() {
    showContacts();
    if (!head) return;

    int index;
    printf("Индекс: ");
    scanf("%d", &index);

    Contact* c = getByIndex(index);
    if (!c) return;

    char temp[50];

    printf("Новое имя (. = пропуск): ");
    scanf("%s", temp);
    if (strcmp(temp, ".") != 0)
        strcpy(c->firstName, temp);

    printf("Новая фамилия (. = пропуск): ");
    scanf("%s", temp);
    if (strcmp(temp, ".") != 0)
        strcpy(c->lastName, temp);
}

void deleteContact() {
    showContacts();
    if (!head) return;

    int index;
    scanf("%d", &index);

    Contact* c = getByIndex(index);
    if (!c) return;

    if (c->prev)
        c->prev->next = c->next;
    else
        head = c->next;

    if (c->next)
        c->next->prev = c->prev;

    free(c);

    printf("Удалено!\n");
}

void freeList() {
    Contact* cur = head;

    while (cur) {
        Contact* tmp = cur;
        cur = cur->next;
        free(tmp);
    }

    head = NULL;
}