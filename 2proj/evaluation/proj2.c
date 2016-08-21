/*
 * Soubor: proj2.c     v.1.0
 * Autor: Jan Wrona, xwrona00@stud.fit.vutbr.cz
 * Datum: 02.05.2012
 * Projekt: Projekt c. 2 pro predmet IOS
 */

#include <stdio.h>              //kvuli I/O funkcim
#include <stdlib.h>             // kvuli exit(), rand_r(), ...
#include <string.h>             // kvuli stringum
#include <sys/mman.h>           // kvuli shm
#include <sys/types.h>          // kvuli datovym typum
#include <fcntl.h>              // kvuli O_konstantam
#include <unistd.h>             // kvuli ftruncate()
#include <sys/wait.h>           // kvuli wait()
#include <semaphore.h>          // kvuli semaforum
#include <time.h>               // kvuli nanosleep()
#include <errno.h>              // kvuli errno

#define MAX_NAME_LEN 100        //makro pro max. delku jmena vyst. souboru

typedef struct {                //struktura pro ulozeni parametru
  int wcount, rcount, cycles;
  int wsimS, wsimNS, rsimS, rsimNS;
  char file[MAX_NAME_LEN];
  FILE *outFile;
} params_t;

typedef struct {                //struktura pouzivana pro sdilenou pamet
  int data;
  int opCounter, writecount, readcount;
  sem_t mutexR, mutexW, prior, reader, writer, output;
} mystruct_t;

typedef enum {                  //vyctovy typ pro chybove kody
  EPARAMS,
  ELONGNAME,
} ECODES;

const char *ECODEMSG[] = {      //chybove hlaseni
  "Spatne zadane parametry!",
  "Prilis dlouhy nazev vystupniho souboru!",
};

//funkcni prototypy
int doParams(params_t * p_params, int argc, char **argv);
int writer(int wcode, mystruct_t * mystruct, params_t * params);
int reader(int rcode, mystruct_t * mystruct, params_t * params);
int shmCreate(int *shm_fd);
int shmDestroy(int *shm_fd, mystruct_t * mystruct);
int initMystruct(mystruct_t * mystruct);
int destroyMystruct(mystruct_t * mystruct);
int mySleep(int sec, int nanosec);
void myPrintf(FILE * outFile, mystruct_t * mystruct, int code, char *who,
              char *what);
void myPerror(int ecode);

/* ZACATEK HLAVNI FUNKCE */
int main(int argc, char **argv)
{
  params_t params;
  int rcode = 0, wcode = 0;
  int shm_fd = 0;
  mystruct_t *mystruct = NULL;

  if (doParams(&params, argc, argv) == 1)       //funkce obstaravajici parametry programu
    return 1;

  pid_t writersPID[params.wcount];      //pole pro ulozeni PID child procesu
  pid_t readersPID[params.rcount];

  if ((strcmp(params.file, "-")) != 0)
  {                             //otevreni (vytvoreni) souboru pro vystupni vypis
    if ((params.outFile = fopen(params.file, "w")) == NULL)
    {
      perror("fopen");
      return 2;
    }
    setvbuf(params.outFile, NULL, _IONBF, 0);   //vypnuti bufferu pro vystupni soubor
  }

  if (shmCreate(&shm_fd) == 1)
  {                             //vytvoreni objektu sdilene pameti
    shmDestroy(&shm_fd, mystruct);
    return 2;
  }
  mystruct =                    //namapovani souboru do pameti
    mmap(NULL, sizeof(mystruct_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd,
         0);
  if (mystruct == MAP_FAILED)
  {
    shmDestroy(&shm_fd, mystruct);
    perror("mmap");
    return 2;
  }

  if (initMystruct(mystruct) == 1)
  {                             //inicializace struktury vcetne inicializace semaforu
    destroyMystruct(mystruct);
    shmDestroy(&shm_fd, mystruct);
    return 2;
  }

  for (int i = 0; i < params.wcount; i++)
  {                             //cyklus pro vytvoreni zadaneho poctu procesu pisaru
    writersPID[i] = fork();
    if (writersPID[i] == -1)
    {                           //pri chybe
      perror("writers fork");
      return 2;
    }
    else if (writersPID[i] == 0)
    {                           //pri uspesnem vytvoreni procesu
      wcode = i + 1;            //prideleni interniho cisla pisarovi
      writer(wcode, mystruct, &params); //volani hlavni pisarovy funkce
      if (params.outFile != NULL)
        fclose(params.outFile); //zavreni souboroveho streamu
      if (munmap(mystruct, sizeof(mystruct_t)) != 0)
        perror("writers munmap");       //unmap sdilene pameti
      exit(0);
    }
  }

  for (int i = 0; i < params.rcount; i++)
  {                             //cyklus pro vytvoreni zadaneho poctu procesu ctenaru
    readersPID[i] = fork();
    if (readersPID[i] == -1)
    {                           //pri chybe
      perror("readers fork");
      return 2;
    }
    else if (readersPID[i] == 0)
    {                           //pri uspesnem vytvoreni procesu
      rcode = i + 1;            //prideleni interniho cisla ctenarovi
      reader(rcode, mystruct, &params); //volani hlavni ctenarovy funkce
      if (params.outFile != NULL)
        fclose(params.outFile); //zavreni soboroveho streamu
      if (munmap(mystruct, sizeof(mystruct_t)) != 0)
        perror("readers munmap");       //unmap sdilene pameti
      exit(0);
    }
  }

  for (int i = 0; i < params.wcount; i++)
    waitpid(writersPID[i], NULL, 0);    //cyklicke cekani na ukonceni pisaru

  sem_wait(&(mystruct->reader));        //zablokuje ctenare
  sem_wait(&(mystruct->writer));        //zablokuje zapis (pouze kvuli ctenarum)
  mystruct->data = 0;
  sem_post(&(mystruct->writer));        //odblokuje zapis
  sem_post(&(mystruct->reader));        //odblokuje ctenare

  // waiting for readers termination
  for (int i = 0; i < params.rcount; i++)
    waitpid(readersPID[i], NULL, 0);    //cyklicke cekani na ukonceni ctenaru

  destroyMystruct(mystruct);    //zniceni semaforu
  shmDestroy(&shm_fd, mystruct);        //unmap sdilene pameti, smazani sdileneho souboru

  if (params.outFile != NULL)
    fclose(params.outFile);     //zavreni souboroveho streamu

  return 0;
}

/* HLAVNI FUNKCE PISARE (WRITERA) */
int writer(int wcode, mystruct_t * mystruct, params_t * params)
{
  for (int i = 0; i < params->cycles; i++)
  {                             //cyklus opakujici celou funkci
    myPrintf(params->outFile, mystruct, wcode, "writer", "new value");
    mySleep(params->wsimS, params->wsimNS);     //simulovany vypocet

    sem_wait(&(mystruct->mutexW));      //zacatek kriticke sekce c.1
    mystruct->writecount++;;    //inkrementace poctu aktivnich pisaru
    if (mystruct->writecount == 1)      //prvni aktivni pisar (zadny jiny)
      sem_wait(&(mystruct->reader));    //zablokuje ctenare
    sem_post(&(mystruct->mutexW));      //konec kriticke sekce c. 1

    myPrintf(params->outFile, mystruct, wcode, "writer", "ready");

    sem_wait(&(mystruct->writer));      //zacatek kriticke sekce c. 2 (zapisovani)
    myPrintf(params->outFile, mystruct, wcode, "writer", "writes a value");
    mystruct->data = wcode;     //zapis dat do shm
    myPrintf(params->outFile, mystruct, wcode, "writer", "written");
    sem_post(&(mystruct->writer));      //konec zapisovaci kriticke sekce c. 2

    sem_wait(&(mystruct->mutexW));      //zacatek kriticke sekce c. 3
    mystruct->writecount--;;    //dekrementace poctu aktivnich pisaru
    if (mystruct->writecount == 0)      //posledni aktivni pisar (zadny jiny)
      sem_post(&(mystruct->reader));    //odblokuje ctenare
    sem_post(&(mystruct->mutexW));      //konec kriticke sekce c. 3
  }
  return 0;
}

/* HLAVNI FUNKCE CTENARE (READRA) */
int reader(int rcode, mystruct_t * mystruct, params_t * params)
{
  int readData = 0;

  do
  {                             //while readData != 0
    myPrintf(params->outFile, mystruct, rcode, "reader", "ready");

    sem_wait(&(mystruct->prior));       //semafor zajistujici prioritu pisaru
    sem_wait(&(mystruct->reader));      //semafor blokujici ctenare pri zapisovani
    sem_wait(&(mystruct->mutexR));      //zacatek kriticke sekce c. 1
    mystruct->readcount++;      //inkrementace poctu aktivnich ctenaru
    if (mystruct->readcount == 1)       //prvni aktivni ctenar (zadny jiny)
      sem_wait(&(mystruct->writer));    //zablokuje pisare
    sem_post(&(mystruct->mutexR));      //konec kriticke sekce c. 1
    sem_post(&(mystruct->reader));
    sem_post(&(mystruct->prior));

    myPrintf(params->outFile, mystruct, rcode, "reader", "reads a value");
    readData = mystruct->data;  //cteni dat ze sdilene pameti
    myPrintf(params->outFile, mystruct, rcode, "reader", "read");

    sem_wait(&(mystruct->mutexR));      //zacatek kriticke sekce c. 2
    mystruct->readcount--;      //dekrementace poctu aktivnich ctenaru
    if (mystruct->readcount == 0)       //posledni aktivni ctenar (zadny jiny)
      sem_post(&(mystruct->writer));    //odblokuje pisare
    sem_post(&(mystruct->mutexR));      //konec kriticke sekce c. 2

    mySleep(params->rsimS, params->rsimNS);     //simulovany vypocet
  } while (readData != 0);
  return 0;
}

/* FUNKCE PRO VYLUCNY VYPIS DAT */
void myPrintf(FILE * outFile, mystruct_t * mystruct, int code, char *who,
              char *what)
{
  if (outFile == NULL)
  {                             //pri vypisu na stdout
    if (strcmp(what, "read") == 0)
    {
      if (sem_wait(&(mystruct->output)) != 0)   //zacatek kriticke sekce
        perror("sem_wait output");
      printf("%d: %s: %d: %s: %d\n", (mystruct->opCounter)++, who, code, what, mystruct->data); //vylucny vypis dat spolu s inkrementaci poctu operaci (vypisu)
      if (sem_post(&(mystruct->output)) != 0)   //konec kriticke sekce
        perror("sem_post output");
    }
    else
    {
      if (sem_wait(&(mystruct->output)) != 0)
        perror("sem_wait output");
      printf("%d: %s: %d: %s\n", (mystruct->opCounter)++, who, code, what);
      if (sem_post(&(mystruct->output)) != 0)
        perror("sem_post output");
    }
  }
  else
  {                             //pri vypisu do souboru
    if (strcmp(what, "read") == 0)
    {
      if (sem_wait(&(mystruct->output)) != 0)
        perror("sem_wait output");
      fprintf(outFile, "%d: %s: %d: %s: %d\n", (mystruct->opCounter)++, who,
              code, what, mystruct->data);
      if (sem_post(&(mystruct->output)) != 0)
        perror("sem_post output");
    }
    else
    {
      if (sem_wait(&(mystruct->output)) != 0)
        perror("sem_wait output");
      fprintf(outFile, "%d: %s: %d: %s\n", (mystruct->opCounter)++, who, code,
              what);
      if (sem_post(&(mystruct->output)) != 0)
        perror("sem_post output");
    }
  }
}

/* FUNKCE PRO VYPIS MYCH CHYBOVYCH HLASENI */
void myPerror(int ecode)
{
  fprintf(stderr, "%s\n", ECODEMSG[ecode]);
}

/* FUNKCE PRO SIMULOVANI VYPOCTU (USPANI PROCESU) */
int mySleep(int sec, int nanosec)
{
  struct timespec sleep;        //struktura definovana v time.h

  sleep.tv_sec = 0;
  sleep.tv_nsec = 0;
  unsigned seed = getpid() * time(NULL);        //vytvoreni seedu pro nanosleep

  if (sec != 0)
    sleep.tv_sec = (time_t) (rand_r(&seed) % (sec + 1));        //generovani nahodneho poctu sekund
  if (nanosec != 0)
    sleep.tv_nsec = (long) (rand_r(&seed) % (nanosec + 1));     //generovani nahodneho poctu nanosekund
  nanosleep(&sleep, NULL);      //uspani procesu
  return 0;
}

/* FUNKCE PRO INICIALIZACI STRUKTURY A SEMAFORU */
int initMystruct(mystruct_t * mystruct)
{
  mystruct->opCounter = 1;
  mystruct->data = -1;
  mystruct->writecount = 0;
  mystruct->readcount = 0;

  if (sem_init(&mystruct->mutexR, 1, 1) != 0)
  {                             //inicializace semaforu s na hodnotu 1
    perror("sem_init mutexR");
    return 1;
  }
  if (sem_init(&mystruct->mutexW, 1, 1) != 0)
  {
    perror("sem_init mutexW");
    return 1;
  }
  if (sem_init(&mystruct->prior, 1, 1) != 0)
  {
    perror("sem_init prior");
    return 1;
  }
  if (sem_init(&mystruct->reader, 1, 1) != 0)
  {
    perror("sem_init prior");
    return 1;
  }
  if (sem_init(&mystruct->writer, 1, 1) != 0)
  {
    perror("sem_init prior");
    return 1;
  }
  if (sem_init(&mystruct->output, 1, 1) != 0)
  {
    perror("sem_init prior");
    return 1;
  }
  return 0;
}

/* FUNKCE PRO ZNICENI VSECH SEMAFORU */
int destroyMystruct(mystruct_t * mystruct)
{
  if (sem_destroy(&mystruct->mutexR) != 0)
  {
    perror("sem_destroy mutexR");
    return 1;
  }
  if (sem_destroy(&mystruct->mutexW) != 0)
  {
    perror("sem_destroy mutexW");
    return 1;
  }
  if (sem_destroy(&mystruct->prior) != 0)
  {
    perror("sem_destroy prior");
    return 1;
  }
  if (sem_destroy(&mystruct->reader) != 0)
  {
    perror("sem_destroy reader");
    return 1;
  }
  if (sem_destroy(&mystruct->writer) != 0)
  {
    perror("sem_destroy writer");
    return 1;
  }
  if (sem_destroy(&mystruct->output) != 0)
  {
    perror("sem_destroy writer");
    return 1;
  }
  return 0;
}

/* FUNKCE PRO VYTVORENI SOBORU PRO SDILENOU PAMET */
int shmCreate(int *shm_fd)
{
  *shm_fd = shm_open("/xwrona00", O_RDWR | O_CREAT | O_TRUNC, 0660);
  if (*shm_fd == -1)
  {                             //vytovreni souboru a prirazeni file descriptoru
    perror("shm_open");
    return 1;
  }
  if (ftruncate(*shm_fd, (off_t) sizeof(mystruct_t)) != 0)
  {                             //roztazeni souboru na velikost mystruct_t
    perror("ftruncate");
    return 1;
  }
  return 0;
}

/* FUNCEK PRO UNMAP A SMAZANI SDILENE PAMETI */
int shmDestroy(int *shm_fd, mystruct_t * mystruct)
{
  if (munmap(mystruct, sizeof(mystruct_t)) != 0)
  {                             //unmapovani sdilene pameti
    perror("munmap");
    return 1;
  }

  if (shm_unlink("/xwrona00") != 0)
  {                             //smazani sdileneho souboru
    perror("unlink");
    return 1;
  }

  if (close(*shm_fd) != 0)
  {                             //uzavreni file descriptoru
    perror("close");
    return 1;
  }
  return 0;
}

/* FUNCE PRO ZPRACOVANI PARAMETRU PROGRAMU */
int doParams(params_t * p_params, int argc, char **argv)
{
  char *strtol_err = NULL;
  int wsimMS, rsimMS;

  p_params->wsimS = 0;
  p_params->rsimS = 0;
  p_params->outFile = NULL;
  errno = 0;

  if (argc != 7)
  {
    myPerror(EPARAMS);
    return 1;
  }

  p_params->wcount = (int) strtol(argv[1], &strtol_err, 10);
  if (strtol_err == argv[1] || *strtol_err != 0 || errno != 0)
  {                             //kontrola chybne zadaneho parametru 
    myPerror(EPARAMS);
    return 1;
  }

  p_params->rcount = (int) strtol(argv[2], &strtol_err, 10);
  if (strtol_err == argv[2] || *strtol_err != 0 || errno != 0)
  {                             //kontrola chybne zadaneho parametru 
    myPerror(EPARAMS);
    return 1;
  }

  p_params->cycles = (int) strtol(argv[3], &strtol_err, 10);
  if (strtol_err == argv[3] || *strtol_err != 0 || errno != 0)
  {                             //kontrola chybne zadaneho parametru 
    myPerror(EPARAMS);
    return 1;
  }

  wsimMS = (int) strtol(argv[4], &strtol_err, 10);
  if (strtol_err == argv[4] || *strtol_err != 0 || errno != 0)
  {                             //kontrola chybne zadaneho parametru 
    myPerror(EPARAMS);
    return 1;
  }
  while (wsimMS > 1000)
  {                             //prepocet milisekund na sekundy
    wsimMS = wsimMS - 1000;
    p_params->wsimS++;
  }
  p_params->wsimNS = wsimMS * 999999;   //prepocet zbyvajicich milisekund na nanosekundy

  rsimMS = (int) strtol(argv[5], &strtol_err, 10);
  if (strtol_err == argv[5] || *strtol_err != 0 || errno != 0)
  {                             //kontrola chybne zadaneho parametru 
    myPerror(EPARAMS);
    return 1;
  }
  while (rsimMS > 1000)
  {                             //prepocet milisekund na sekundy
    rsimMS = rsimMS - 1000;
    p_params->rsimS++;
  }
  p_params->rsimNS = rsimMS * 999999;   //prepocet zbyvajicich milisekund na nanosekundy

  if (strlen(argv[6]) >= MAX_NAME_LEN)
  {
    myPerror(ELONGNAME);
    return 1;
  }
  strcpy(p_params->file, argv[6]);

  return 0;
}
