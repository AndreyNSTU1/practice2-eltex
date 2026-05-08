#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Contact {
    char lastName[50];
    char firstName[50];
    char work[100];
    char position[100];
    char phones[100];
    char emails[100];
    char social[100];
    char messengers[100];

    struct Contact* prev;
    struct Contact* next;
} Contact;

Contact* head = NULL;

// ===== сравнение для упорядочивания =====
int compare(Contact* a, Contact* b) {
    int c = strcmp(a->lastName, b->lastName);
    if (c == 0)
        c = strcmp(a->firstName, b->firstName);
    return c;
}

// ===== создание узла =====
Contact* createContact() {
    Contact* c = (Contact*)malloc(sizeof(Contact));

    printf("\nДобавление контакта\n");

    printf("Фамилия: ");
    scanf("%s", c->lastName);

    printf("Имя: ");
    scanf("%s", c->firstName);

    printf("Место работы: ");
    getchar();
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

// ===== вставка в упорядоченный список =====
void addContact() {
    Contact* newC = createContact();

    if (head == NULL) {
        head = newC;
        return;
    }

    Contact* cur = head;

    // вставка в начало
    if (compare(newC, head) < 0) {
        newC->next = head;
        head->prev = newC;
        head = newC;
        return;
    }

    while (cur->next != NULL && compare(cur->next, newC) < 0) {
        cur = cur->next;
    }

    newC->next = cur->next;
    if (cur->next != NULL)
        cur->next->prev = newC;

    cur->next = newC;
    newC->prev = cur;

    printf("Контакт добавлен!\n");
}

// ===== вывод =====
void showContacts() {
    if (!head) {
        printf("\nСписок пуст\n");
        return;
    }

    printf("\nСписок контактов:\n");

    int i = 0;
    Contact* cur = head;

    while (cur) {
        printf("%d. %s %s\n", i,
               cur->lastName,
               cur->firstName);
        cur = cur->next;
        i++;
    }
}

// ===== поиск по индексу =====
Contact* getByIndex(int index) {
    Contact* cur = head;
    int i = 0;

    while (cur && i < index) {
        cur = cur->next;
        i++;
    }
    return cur;
}

// ===== редактирование =====
void editContact() {
    showContacts();
    if (!head) return;

    int index;
    printf("\nВведите индекс: ");
    scanf("%d", &index);

    Contact* c = getByIndex(index);
    if (!c) {
        printf("Ошибка!\n");
        return;
    }

    printf("Новое имя (или .): ");
    char temp[50];
    scanf("%s", temp);
    if (strcmp(temp, ".") != 0)
        strcpy(c->firstName, temp);

    printf("Новая фамилия (или .): ");
    scanf("%s", temp);
    if (strcmp(temp, ".") != 0)
        strcpy(c->lastName, temp);

    printf("Контакт обновлён (сортировка не пересчитывается)\n");
}

// ===== удаление =====
void deleteContact() {
    showContacts();
    if (!head) return;

    int index;
    printf("\nВведите индекс: ");
    scanf("%d", &index);

    Contact* c = getByIndex(index);
    if (!c) {
        printf("Ошибка!\n");
        return;
    }

    if (c->prev)
        c->prev->next = c->next;
    else
        head = c->next;

    if (c->next)
        c->next->prev = c->prev;

    free(c);

    printf("Удалено!\n");
}

// ===== освобождение памяти =====
void freeList() {
    Contact* cur = head;
    while (cur) {
        Contact* tmp = cur;
        cur = cur->next;
        free(tmp);
    }
}

// ===== main =====
int main() {
    int choice;

    while (1) {
        printf("\n--- ТЕЛЕФОННАЯ КНИГА (DLL) ---\n");
        printf("1. Добавить\n");
        printf("2. Показать\n");
        printf("3. Редактировать\n");
        printf("4. Удалить\n");
        printf("5. Выход\n");
        printf("Выбор: ");

        scanf("%d", &choice);

        switch (choice) {
            case 1: addContact(); break;
            case 2: showContacts(); break;
            case 3: editContact(); break;
            case 4: deleteContact(); break;
            case 5: freeList(); return 0;
            default: printf("Ошибка!\n");
        }
    }
}