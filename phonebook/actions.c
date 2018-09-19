#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "actions.h"

// to skip newline in stdin
void remove_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = '\0';
}

void add_contact(node *list) {
    char temp_str[MAX_STR_LEN + 1];

    printf("Enter name: ");
    fgets(temp_str, MAX_STR_LEN + 1, stdin);
    remove_newline(temp_str);

    char *name_str = malloc(strlen(temp_str));
    assert(name_str != NULL);
    strcpy(name_str, temp_str);

    printf("Enter surname: ");
    fgets(temp_str, MAX_STR_LEN + 1, stdin);
    remove_newline(temp_str);

    char *surname_str = malloc(strlen(temp_str));
    assert(surname_str != NULL);
    strcpy(surname_str, temp_str);

    printf("Enter phone number: ");
    fgets(temp_str, MAX_STR_LEN + 1, stdin);
    remove_newline(temp_str);

    char *phone_str = malloc(strlen(temp_str));
    assert(phone_str != NULL);
    strcpy(phone_str, temp_str);

    //create new node
    node *new_contact = malloc(sizeof(node));
    assert(new_contact != NULL);
    new_contact->data.name = name_str;
    new_contact->data.surname = surname_str;
    new_contact->data.phone_num = phone_str;

    // add node to list
    new_contact->next = list->next;
    list->next = new_contact;
    printf("Contact was successfully added.\n");
}

void delete_contact(node *list) {
    char name_str[MAX_STR_LEN + 1];
    char surname_str[MAX_STR_LEN + 1];

    printf("Enter name: ");
    fgets(name_str, MAX_STR_LEN + 1, stdin);
    remove_newline(name_str);

    printf("Enter surname: ");
    fgets(surname_str, MAX_STR_LEN + 1, stdin);
    remove_newline(surname_str);

    node *prev = list;
    node *temp = list->next;
    while (temp != NULL) {
        if (strcmp(name_str, temp->data.name) == 0 && strcmp(surname_str, temp->data.surname) == 0) {
            // delete node and exit
            prev->next = temp->next;
            free(temp);
            temp = prev->next;
            printf("Contact was successfully deleted.\n");
            return;

        } else {
            prev = temp;
            temp = temp->next;
        }
    }
    printf("Specified contact was not found.\n");
}

void search_for_name(node *list) {
    char name_str[MAX_STR_LEN + 1];

    printf("Enter name: ");
    fgets(name_str, MAX_STR_LEN + 1, stdin);
    remove_newline(name_str);

    int found_count = 0;

    node *temp = list->next;
    while (temp != NULL) {
        if (strcmp(name_str, temp->data.name) == 0) {
            printf("%s %s - %s\n", temp->data.name, temp->data.surname, temp->data.phone_num);
            found_count++;
        }
        temp = temp->next;
    }

    if (0 == found_count)
        printf("There is no contacts with specified name.\n");
}

void print_contacts(node *list) {
    int found_count = 0;
    node *temp = list->next;
    while (temp != NULL) {
        printf("%s %s - %s\n", temp->data.name, temp->data.surname, temp->data.phone_num);
        temp = temp->next;
        found_count++;
    }

    if (0 == found_count)
        printf("Empty phonebook.\n");
}

