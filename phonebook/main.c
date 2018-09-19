#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "actions.h"


int main() {
    // create empty node for head
    node *head = malloc(sizeof(node));
    assert(head != NULL);
    head->data = (contact){.name = "Empty", .surname = "node", .phone_num = "---"};
    head->next = NULL;

    printf("1. Add contact\n"
           "2. Delete contact\n"
           "3. Search by name\n"
           "4. Print contacts\n"
           "5. Exit\n");

    int c;
    while ((c = getchar()) != EOF) {
        getchar(); // skip newline
        switch (c) {
            case '1':
                add_contact(head);
                break;
            case '2':
                delete_contact(head);
                break;
            case '3':
                search_for_name(head);
                break;
            case '4':
                print_contacts(head);
                break;
            case '5':
                return 0;
            default:
                printf("Invalid option. Try any one from the list below:\n");
                break;
        }
        printf("1. Add contact\n"
               "2. Delete contact\n"
               "3. Search by name\n"
               "4. Print contacts\n"
               "5. Exit\n");
    }
    return 0;
}

