#include <stdio.h>
#include <string.h>

#define MAX 100

typedef struct {
    char lastName[50];
    char firstName[50];
    char work[100];
    char position[100];
    char phones[100];
    char emails[100];
    char social[100];
    char messengers[100];
} Contact;

Contact contacts[MAX];
int count = 0;

void clearInput() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void addContact() {
    if (count >= MAX) {
        printf("Телефонная книга заполнена!\n");
        return;
    }

    printf("\nДобавление контакта\n");

    printf("Фамилия: ");
    scanf("%s", contacts[count].lastName);

    printf("Имя: ");
    scanf("%s", contacts[count].firstName);

    if (strlen(contacts[count].lastName) == 0 ||
        strlen(contacts[count].firstName) == 0) {
        printf("Фамилия и имя обязательны!\n");
        return;
    }

    printf("Место работы: ");
    clearInput();
    fgets(contacts[count].work, 100, stdin);

    printf("Должность: ");
    fgets(contacts[count].position, 100, stdin);

    printf("Телефоны: ");
    fgets(contacts[count].phones, 100, stdin);

    printf("Email: ");
    fgets(contacts[count].emails, 100, stdin);

    printf("Соцсети: ");
    fgets(contacts[count].social, 100, stdin);

    printf("Мессенджеры: ");
    fgets(contacts[count].messengers, 100, stdin);

    count++;
    printf("Контакт добавлен!\n");
}

void showContacts() {
    printf("\nСписок контактов:\n");

    if (count == 0) {
        printf("Пусто\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        printf("%d. %s %s\n", i,
               contacts[i].lastName,
               contacts[i].firstName);
    }
}

void editContact() {
    int index;

    showContacts();
    if (count == 0) return;

    printf("\nВведите индекс контакта: ");
    scanf("%d", &index);

    if (index < 0 || index >= count) {
        printf("Ошибка индекса!\n");
        return;
    }

    printf("Новое имя (или . чтобы оставить): ");
    char temp[100];
    scanf("%s", temp);
    if (strcmp(temp, ".") != 0)
        strcpy(contacts[index].firstName, temp);

    printf("Новая фамилия (или .): ");
    scanf("%s", temp);
    if (strcmp(temp, ".") != 0)
        strcpy(contacts[index].lastName, temp);

    printf("Контакт обновлён!\n");
}

void deleteContact() {
    int index;

    showContacts();
    if (count == 0) return;

    printf("\nВведите индекс для удаления: ");
    scanf("%d", &index);

    if (index < 0 || index >= count) {
        printf("Ошибка индекса!\n");
        return;
    }

    for (int i = index; i < count - 1; i++) {
        contacts[i] = contacts[i + 1];
    }

    count--;
    printf("Контакт удалён!\n");
}

int main() {
    int choice;

    while (1) {
        printf("\n--- ТЕЛЕФОННАЯ КНИГА ---\n");
        printf("1. Добавить контакт\n");
        printf("2. Показать контакты\n");
        printf("3. Редактировать контакт\n");
        printf("4. Удалить контакт\n");
        printf("5. Выход\n");
        printf("Выбор: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1: addContact(); break;
            case 2: showContacts(); break;
            case 3: editContact(); break;
            case 4: deleteContact(); break;
            case 5: return 0;
            default: printf("Неверный выбор!\n");
        }
    }

    return 0;
}