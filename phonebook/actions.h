#pragma once

#define MAX_STR_LEN 80

typedef struct contact {
    char *name;
    char *surname;
    char *phone_num;
} contact;

typedef struct node {
    contact data;
    struct node *next;
} node;

void add_contact(node *list);

void delete_contact(node *list);

void search_for_name(node *list);

void print_contacts(node *list);
