#ifndef CONTACT_H
#define CONTACT_H

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

void addContact();
void showContacts();
void editContact();
void deleteContact();
void freeList();

#endif