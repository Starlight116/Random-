/*------------------------------------------------TestAleatoire.c-------------------------------------------------------------------------------*
* Auteur : Camille Protat                                                                                                                       *
* Date : 03/12/2023                                                                                                                             *
* Description : Ce programme en C vise à générer des nombres aléatoires à l'aide de deux fonctions différentes pour comparer l'homogénéité      *
*    de la répartition des nombres générés par les 2. Une des fonctions utilise rand() et l'autre utilise une méthode écrite par moi.           *
*    Les chiffres générés sont définis entre 0 et 1.Il utilise des processus et des threads  																	*
*	  pour paralléliser la génération de ces nombres aléatoires et crée un histogramme pour chacune des fonctions.                               *
*-----------------------------------------------------------------------------------------------------------------------------------------------*/

// --------------------------Inclusion des bibliothèques---------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>

//-----------------------------------Constantes-----------------------------------------------
#define NUM_VALUES 100000 //nombre de données produites par processus
#define NUM_PROCESSES 100 //nombre de processus
#define NUM_THREADS_IN_PROCESS 10 //nombre de threads dans chaques processus
#define NUM_BINS 10 //tranches de données, entre 0 et 1 on en met 10 pour avoir des tranches de 0.1 
//mais on pourrait chercher à être plus précis
//-------------------------------------Structures----------------------------------------------
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *tab;
};

//----------------------------------Variables--------------------------------------------------
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; //initialise le mutex.
int sem_id; //id du sémaphore
int shm_id_rand,shm_id_myrand; //id des tabs partagés

//-------------------------------------Fonctions------------------------------------------------

/*
 * MyRand génère un nombre pseudo aléatoire entre 0 et 1 
 * en utilisant time et clock
*/
double myRand() {
    double pseudoAleatoire = (double)time(NULL) * clock() / (RAND_MAX);
    return pseudoAleatoire - (long)pseudoAleatoire;
}

// useRand normalise juste rand() pour qu'il soit entre 0 et 1
double useRand(){
    return (double)rand() / RAND_MAX;
}

//crée et initialise le sémaphore
void init_semaphore() {
    sem_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Erreur de création du sémaphore");
        exit(EXIT_FAILURE);
    }

    union semun arg;
    arg.val = 1; // Initial value of the semaphore
    if (semctl(sem_id, 0, SETVAL, arg) == -1) {
        perror("Erreur d'initialisation du sémaphore");
        exit(EXIT_FAILURE);
    }
}

//permet d'utiliser l'opération wait du sémaphore
void wait_semaphore() {
    struct sembuf op = {0, -1, 0}; 
    if (semop(sem_id, &op, 1) == -1) {
        perror("Erreur wait() de sémaphore");
        exit(EXIT_FAILURE);
    }
}

//permet d'utiliser l'opération signal du sémaphore
void signal_semaphore() {
    struct sembuf op = {0, 1, 0};
    if (semop(sem_id, &op, 1) == -1) {
        perror("Erreur signal de sémaphore");
        exit(EXIT_FAILURE);
    }
}
/*
 * utilisé dans un thread, génère une partie des nombres aléatoires produits avec myRand()
 * et enregistre leur répartition dans le tableau partagé.
 * On utilise un mutex pour être sur qu'il n'y ai pas 2 threads en même temps qui
 * modifient le tableau.
*/
void *genere_random_myRand(void* arg){
    int * tab = (int *) arg;
    for(int i = 0; i < NUM_VALUES / NUM_THREADS_IN_PROCESS; ++i){
        pthread_mutex_lock(&mutex);
        double randValue = myRand();
        int bin = (int)(randValue * NUM_BINS);
        tab[bin]++;
        pthread_mutex_unlock(&mutex);
    }
    pthread_exit(NULL);
}

/*
 * utilisé dans un thread, génère une partie des nombres aléatoires produits avec useRand()
 * et enregistre leur répartition dans le tableau partagé.
 * On utilise un mutex pour être sur qu'il n'y ai pas 2 threads en même temps qui
 * modifient le tableau.
*/
void *genere_random_useRand(void* arg){
    int * tab = (int *) arg;
    for(int i = 0; i < NUM_VALUES / NUM_THREADS_IN_PROCESS; ++i){
        pthread_mutex_lock(&mutex);
        double randValue = useRand();
        int bin = (int)(randValue * NUM_BINS);
        tab[bin]++;
        pthread_mutex_unlock(&mutex);
    }
    pthread_exit(NULL);
}
/*
* On appelle processus_enfant() dans chaque processus, on y crée les threads qui vont ensuite
* appeler le bon genere_random en fonction de l'argument randFunction
* On utilise le semaphore ici pour éviter les conflits entre chaques processus quand on édite le tableau partagé.
* Dans les faits, chaques threads de chaques processus ne pourra pas modifier une case du tableau
* si un autre threads est déjà entrain d'écrire dessus.
*/
void processus_enfant(int *tableau_distrib, double (*randFunction)()) {
    wait_semaphore();
    void* randFuncName;
    if(randFunction == myRand){
        randFuncName = genere_random_myRand;
    } else {
        randFuncName = genere_random_useRand;
    }

    pthread_t threads[NUM_THREADS_IN_PROCESS];

    // Chaque thread travaille sur l'ensemble du tableau pour un nombre limité de valeurs générées
    for (int i = 0; i < NUM_THREADS_IN_PROCESS; ++i) {
        pthread_create(&threads[i], NULL, randFuncName, (void *)tableau_distrib);
    }

    //on attend la fin de tout les threads

    for (int j = 0; j < NUM_THREADS_IN_PROCESS; ++j) {
        pthread_join(threads[j], NULL);
    }

    signal_semaphore();

    exit(EXIT_SUCCESS);
}


// Permet d'afficher un histogramme avec les pourcentage de répartition et des barres proprotionnelles a celui-ci
// C'est un joli affichage qui permet assez rapidement d'avoir un visuel concret mais qui n'est pas suffisant
// pour évaluer notre random en comparaison avec l'aléatoire de C
void afficher_histogram(int *tab) {
    int totalCount = NUM_VALUES*NUM_PROCESSES;

    for (int i = 0; i < NUM_BINS; ++i) {
        double binDeb = i * (1.0 / NUM_BINS);
        double binFin = (i + 1) * (1.0 / NUM_BINS);

        double pourcentage = (tab[i] / (double)totalCount) * 100;

        printf("[%0.2f - %0.2f]: %6.2f%% | ", binDeb, binFin, pourcentage);

        int etoiles = (int)(pourcentage * 0.5);  
        for (int j = 0; j < etoiles; ++j) {
            printf("*");
        }

        printf("\n");
    }
}

//Permet d'afficher la moyenne de la répartition des 2 randoms
//On calcule cette moyenne et on la compare a ce qu'elle devrait être si l'aléatoire était parfait
// avec une marge d'erreur plus ou moins importante en fonction de ce qu'on essaye de comparer
void afficher_statistics(int *tab, const char *title) {
    int totalCount = NUM_VALUES*NUM_PROCESSES;
    double moyenneReelle = 0.5;  // si la répartition était parfaite on aurait cette moyenne
    double margeErreur = 0.005;  // La marge d'erreur est ajustable

    double moyenneEstime = 0.0;

    // Calculate the estimated mean
    for (int i = 0; i < NUM_BINS; ++i) {
        double binDeb = i * (1.0 / NUM_BINS);
        double binFin = (i + 1) * (1.0 / NUM_BINS);
        double binCentre = (binDeb + binFin) / 2.0;

        moyenneEstime += binCentre * (tab[i] / (double)totalCount);
    }

    printf("\n%s Statistiques avec %i valeurs:\n", title, NUM_VALUES*NUM_PROCESSES);
    printf("Moyenne Estimée: %0.5f\n", moyenneEstime);
    printf("Marge d'erreur: %0.5f\n", margeErreur);

    //comparaisons des moyennes en prenant en compte la marge d'erreur
    if (moyenneEstime >= moyenneReelle - margeErreur && moyenneEstime <= moyenneReelle + margeErreur) {
        printf("La moyenne estimée est dans une marge acceptable, on considère que la répartition est bonne.\n");
    } else {
        printf("La moyenne estimée n'est dans une marge acceptable !\n");
    }
}

int main() {
    //initialisation du sémaphore
    init_semaphore();
    
    //initialisation des tableaux partagés
    shm_id_myrand = shmget(IPC_PRIVATE, NUM_BINS * sizeof(int), IPC_CREAT | 0666);
    shm_id_rand = shmget(IPC_PRIVATE, NUM_BINS * sizeof(int), IPC_CREAT | 0666);
    
    //remise a 0 des tableaux partagés
    int*tableau_distrib_myRand = (int *)shmat(shm_id_myrand, NULL, 0);
    int *tableau_distrib_rand = (int *)shmat(shm_id_rand, NULL, 0);

    //CRÉATION DES PROCESSUS POUR MYRAND
    pid_t pid;
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        pid = fork();
        if (pid == 0) {
            // Processus enfant
            processus_enfant(tableau_distrib_myRand, myRand);
        } else if (pid < 0) {
            perror("Erreur de création du processus fils !");
            exit(EXIT_FAILURE);
        }
    }

    //Attente des processus
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        wait(NULL);
    }

    // Afficher l'histogramme pour myRand()
    printf("\nHistogram pour myRand():\n");
    afficher_histogram(tableau_distrib_myRand);
    afficher_statistics(tableau_distrib_myRand,"myRand()");

    // CREATION DES PROCESSUS POUR RAND
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        srand(time(NULL));
        
        pid = fork();
        if (pid == 0) {
            // Processus enfant
            processus_enfant(tableau_distrib_rand, useRand);
        } else if (pid < 0) {
            perror("Erreur de création du processus fils !");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_PROCESSES; ++i) {
        wait(NULL); // Attendre que tous les processus enfants se terminent
    }

    // Afficher l'histogramme pour rand()
    printf("\nHistogram pour rand():\n");
    afficher_histogram(tableau_distrib_rand);
    afficher_statistics(tableau_distrib_rand,"rand()");

    // Nettoyage
    shmdt(tableau_distrib_myRand);
    shmdt(tableau_distrib_rand);
    shmctl(shm_id_rand, IPC_RMID, NULL);
    shmctl(shm_id_myrand, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    // Détruire le mutex
    pthread_mutex_destroy(&mutex);

    return 0;
}
