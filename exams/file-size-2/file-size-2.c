#include "../../lib/lib-misc.h"
#include <dirent.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Implementazione di una lista doppiamente linkata
typedef struct __node {
    unsigned long value;
    struct __node *next;
    struct __node *prev;
} node;

typedef struct {
    node *head;
    unsigned size;
} list;

void init_list(list *l) {
    l->head = NULL;
    l->size = 0;
}

void list_insert(list *l, const int value) {
    node *n = malloc(sizeof(node));
    n->value = value;
    n->prev = NULL;
    n->next = l->head;
    l->head = n;

    if (n->next != NULL)
        n->next->prev = n;

    l->size++;
}

int extract_min(list *l) {
    // controllo ridondante (non dovrei chiamare la funzione quando non ci sono
    // elementi)
    if (l->head == NULL)
        return 0;

    int min;
    node *min_ptr = l->head;
    node *ptr = l->head->next;

    while (ptr != NULL) {
        if (ptr->value < min_ptr->value)
            min_ptr = ptr;

        ptr = ptr->next;
    }

    min = min_ptr->value;

    if (min_ptr->prev != NULL)
        min_ptr->prev->next = min_ptr->next;

    if (min_ptr->next != NULL)
        min_ptr->next->prev = min_ptr->prev;

    if (l->head == min_ptr)
        l->head = l->head->next;

    free(min_ptr);
    l->size--;

    return min;
}

int extract_max(list *l) {
    // controllo ridondante (non dovrei chiamare la funzione quando non ci sono
    // elementi)
    if (l->head == NULL)
        return 0;

    int max;
    node *max_ptr = l->head;
    node *ptr = l->head->next;

    while (ptr != NULL) {
        if (ptr->value < max_ptr->value)
            max_ptr = ptr;

        ptr = ptr->next;
    }

    max = max_ptr->value;

    if (max_ptr->prev != NULL)
        max_ptr->prev->next = max_ptr->next;

    if (max_ptr->next != NULL)
        max_ptr->next->prev = max_ptr->prev;

    if (l->head == max_ptr)
        l->head = l->head->next;

    free(max_ptr);
    l->size--;

    return max;
}

void list_destroy(list *l) {
    node *ptr = l->head;
    node *tmp;

    while (ptr != NULL) {
        tmp = ptr;
        ptr = ptr->next;
        free(tmp);
    }

    free(l);
}

typedef struct {
    list *l;
    unsigned done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} number_set;

void init_number_set(number_set *ns) {
    int err;

    ns->done = 0;
    ns->l = malloc(sizeof(list));
    init_list(ns->l);

    if ((err = pthread_mutex_init(&ns->lock, 0)) != 0)
        exit_with_err("pthread_mutex_init", err);

    if ((err = pthread_cond_init(&ns->cond, 0)) != 0)
        exit_with_err("pthread_cond_init", err);
}

void destroy_number_set(number_set *ns) {
    list_destroy(ns->l);
    pthread_mutex_destroy(&ns->lock);
    pthread_cond_destroy(&ns->cond);
    free(ns);
}

typedef struct {
    // dati privati
    pthread_t tid;
    unsigned thread_n;
    unsigned ndir;
    char *dirname;

    // dati condivisi
    number_set *sh;
} thread_data;

void dir_scanner(void *arg) {
    int err;
    thread_data *td = (thread_data *)arg;
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;
    char pathfile[PATH_MAX];

    // apro la directory destinata al thread
    if ((dp = opendir(td->dirname)) == NULL)
        exit_with_sys_err("opendir");

    printf("[D-%u] scansione della cartella '%s'\n", td->thread_n, td->dirname);

    // itero sugli oggetti del filesystem presenti nella directory
    while ((entry = readdir(dp))) {
        // concateno il path della directory con il nome del file
        snprintf(pathfile, PATH_MAX, "%s/%s", td->dirname, entry->d_name);

        // effettuo uno stat del file
        if (lstat(pathfile, &statbuf) == -1)
            exit_with_sys_err(entry->d_name);

        // verifico se il file considerato è regolare
        if (S_ISREG(statbuf.st_mode)) {
            printf("[D-%u] trovato il file '%s' in %s\n", td->thread_n,
                   entry->d_name, td->dirname);

            // acquisisco il lock sulla struttura dati condivisa
            if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            list_insert(td->sh->l, statbuf.st_size);

            if ((err = pthread_cond_signal(&td->sh->cond)) != 0)
                exit_with_err("pthread_cond_signal", err);

            // rilascio il lock sulla struttura dati condivisa
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
        }
    }

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->done++;

    if ((err = pthread_cond_signal(&td->sh->cond)) != 0)
        exit_with_err("pthread_cond_signal", err);

    if ((err = pthread_cond_signal(&td->sh->cond)) != 0)
        exit_with_err("pthread_cond_signal", err);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    closedir(dp);
}

void add(void *arg) {
    int err;
    thread_data *td = (thread_data *)arg;
    unsigned long min, max, sum;
    unsigned done = 0;

    printf("[ADD-%u]: sono partito\n", td->thread_n);

    while (1) {
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        // verifico le condizioni di operabilità o di terminazione
        while (td->sh->l->size < 2 && td->sh->done != td->ndir)
            if ((err = pthread_cond_wait(&td->sh->cond, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);

        if (td->sh->done == td->ndir && td->sh->l->size == 1) {
            // rilascio il lock sulla struttura dati condivisa
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
            break;
        }

        min = extract_min(td->sh->l);
        max = extract_max(td->sh->l);
        sum = min + max;

        list_insert(td->sh->l, sum);

        printf("[ADD-%u] il minimo (%lu) ed il massimo (%lu) sono stati "
               "sostituiti da %lu; l'insieme ha adesso %u elementi.\n",
               td->thread_n, min, max, sum, td->sh->l->size);

        // rilascio il lock sulla struttura dati condivisa
        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <dir-1> <dir-2> ... <dir-n>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;
    thread_data td[argc + 1];
    number_set *sh = malloc(sizeof(number_set));
    init_number_set(sh);

    // DIR-i
    for (int i = 0; i < argc - 1; i++) {
        td[i].thread_n = i + 1;
        td[i].dirname = argv[i + 1];
        td[i].sh = sh;

        if ((err = pthread_create(&td[i].tid, NULL, (void *)dir_scanner,
                                  &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // ADD-j
    for (int i = 0; i < 2; i++) {
        td[i + argc - 1].sh = sh;
        td[i + argc - 1].thread_n = i + 1;
        td[i + argc - 1].ndir = argc - 1;

        if ((err = pthread_create(&td[i + argc - 1].tid, NULL, (void *)add,
                                  &td[i + argc - 1])) != 0)
            exit_with_err("pthread_create", err);
    }

    for (int i = 0; i < argc + 1; i++)
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);

    printf("[MAIN] i thread secondari hanno terminato e il totale finale è di "
           "%lu byte.\n",
           sh->l->head->value);

    destroy_number_set(sh);
}