#include <stdio.h>
#include "contact.h"

int main() {
    int choice;

    while (1) {
        printf("\n1. Добавить\n2. Показать\n3. Редактировать\n4. Удалить\n5. Выход\n");
        scanf("%d", &choice);

        switch (choice) {
            case 1: addContact(); break;
            case 2: showContacts(); break;
            case 3: editContact(); break;
            case 4: deleteContact(); break;
            case 5: freeList(); return 0;
        }
    }
}